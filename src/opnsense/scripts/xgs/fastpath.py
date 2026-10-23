#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""What is done when the coprocessor's fast path stops answering its heartbeat - the owner's choice.

    fastpath.py check     once a minute (xgs_cron): restart the appliance if the setting says so
    fastpath.py show      the heartbeat and what this would do now, as JSON

THE HEARTBEAT is the driver's (dev.octep.0.heartbeat, dev.npuep.0.heartbeat;
docs/measurements/heartbeat-2026-10-22.md): the host asks the fast path every five seconds, and with
no answer it is late at 15 seconds and taken as stopped at 30, each said in the system log by the
driver itself the moment it happens. On the XGS 136, whose fast path drops the heartbeat message
unanswered, the answer is the beat being taken off the host's queue by the fast path's process -
the same states and lines.

WHAT IS DONE is xgs.general.fastpath_restart. 0, the default: it is said and nothing more - the
driver's lines in the system log, and a problem report (reporter.py, kind fast-path-stopped). 2, 5
or 10: once the fast path has been taken as stopped and gone that many minutes without an answer,
the appliance is restarted - the report collected first, so it is not lost with the boot. Nothing
else brings a stopped fast path back: every frame through the front ports goes through it, so the
host has no connection it could take back and carry on its own. The vendor's host panics the box
after 120 seconds, with that switched off by default; this one restarts it cleanly, and only when
asked to.

NEVER A LOOP: at most RESTARTS_PER_DAY automatic restarts in any 24 hours, kept in STATE. Past that
it says so, once a boot, and leaves the appliance up for somebody to look at.
"""

import json
import os
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

CONFIG = os.environ.get('XGS_FASTPATH_CONFIG', '/conf/config.xml')
STATE = os.environ.get('XGS_FASTPATH_STATE', '/var/db/os-xgs-npu/fastpath.json')
NODES = ('dev.octep.0.heartbeat', 'dev.npuep.0.heartbeat')
CHOICES = (0, 2, 5, 10)
RESTARTS_PER_DAY = 3
DAY = 86400
# The driver's own tag, so the log shows it beside the driver's lines; a warning, not an error: the
# report is reporter.py's own kind, and an error line would make a second, generic one (measured
# 2026-10-22, the first restart test queued a log-error report of this line).
TAG = 'octep'
REPORTER = '/usr/local/opnsense/scripts/xgs/reporter.py'
REBOOT = '/usr/local/etc/rc.reboot'


def sysctl(name):
    try:
        done = subprocess.run(['/sbin/sysctl', '-n', name], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    return done.stdout.strip() if done.returncode == 0 else None


def heartbeat():
    """The heartbeat's state as the driver keeps it, or None on a host without one."""
    for node in NODES:
        state = sysctl(node + '.state')
        if state is None:
            continue
        hb = {'node': node, 'state': state}
        for key in ('on', 'since_answer_ms', 'sent', 'answered', 'deaths', 'revivals', 'rtt_us'):
            v = sysctl('%s.%s' % (node, key))
            hb[key] = int(v) if v is not None and v.isdigit() else None
        return hb
    return None


def setting():
    """xgs.general.fastpath_restart, in minutes: 0 unless it is one of the choices."""
    try:
        root = ET.parse(CONFIG).getroot()
    except (OSError, ET.ParseError):
        return 0
    try:
        minutes = int(root.findtext('OPNsense/XGS/general/fastpath_restart') or '0')
    except ValueError:
        return 0
    return minutes if minutes in CHOICES else 0


def boot_time():
    raw = sysctl('kern.boottime') or ''
    try:
        return int(raw.split('sec =')[1].split(',')[0])
    except (IndexError, ValueError):
        return 0


def load_state():
    try:
        with open(STATE) as f:
            kept = json.load(f)
        return kept if isinstance(kept, dict) else {}
    except (OSError, ValueError):
        return {}


def save_state(kept):
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    tmp = STATE + '.new'
    with open(tmp, 'w') as f:
        json.dump(kept, f)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, STATE)


def decide(hb, minutes, restarts, now):
    """What to do now, and why: ('none'|'restart'|'refuse', reason). Pure, for the tests."""
    if hb is None:
        return 'none', 'no heartbeat on this host'
    if minutes == 0:
        return 'none', 'the setting says to say so and nothing more'
    if hb.get('state') != 'dead':
        return 'none', 'the fast path is %s' % hb.get('state')
    silent = hb.get('since_answer_ms') or 0
    if silent < minutes * 60000:
        return 'none', 'taken as stopped %d s ago, the setting waits %d min' % (silent // 1000, minutes)
    recent = [t for t in restarts if now - t < DAY]
    if len(recent) >= RESTARTS_PER_DAY:
        return 'refuse', ('the fast path has not answered for %d s, and the appliance was already '
                          'restarted for it %d times in 24 hours - left up for somebody to look at'
                          % (silent // 1000, len(recent)))
    return 'restart', ('the coprocessor\'s fast path has not answered its heartbeat for %d s - '
                       'restarting the appliance, as XGS: Settings says after %d minutes'
                       % (silent // 1000, minutes))


def log(priority, message):
    try:
        subprocess.run(['/usr/bin/logger', '-t', TAG, '-p', 'daemon.' + priority, message], timeout=10)
    except (OSError, subprocess.SubprocessError):
        pass


def check():
    hb = heartbeat()
    minutes = setting()
    kept = load_state()
    now = int(time.time())
    action, reason = decide(hb, minutes, kept.get('restarts', []), now)
    if action == 'refuse':
        boot = boot_time()
        if kept.get('refused_boot') != boot:
            log('warning', reason)
            kept['refused_boot'] = boot
            save_state(kept)
        return 0
    if action != 'restart':
        return 0
    kept['restarts'] = [t for t in kept.get('restarts', []) if now - t < DAY] + [now]
    save_state(kept)
    log('warning', reason)
    # The report first: the heartbeat's counters go with this boot.
    try:
        subprocess.run([REPORTER, 'collect'], capture_output=True, timeout=120)
    except (OSError, subprocess.SubprocessError):
        pass
    subprocess.Popen([REBOOT], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, start_new_session=True)
    return 0


def show():
    hb = heartbeat()
    minutes = setting()
    kept = load_state()
    action, reason = decide(hb, minutes, kept.get('restarts', []), int(time.time()))
    print(json.dumps({'heartbeat': hb, 'restart_after_min': minutes, 'now': action, 'why': reason,
                      'restarts_24h': len([t for t in kept.get('restarts', []) if time.time() - t < DAY])}))
    return 0


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else ''
    if what == 'check':
        sys.exit(check())
    if what == 'show':
        sys.exit(show())
    print(__doc__.split('\n\n')[1], file=sys.stderr)
    sys.exit(64)
