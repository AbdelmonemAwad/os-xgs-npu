#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""What is done when the coprocessor's fast path stops answering its heartbeat - the owner's choice.

    fastpath.py check     once a minute (xgs_cron): take the coprocessor up again, or restart the
                          appliance, as the setting says
    fastpath.py show      the heartbeat and what this would do now, as JSON

THE HEARTBEAT is the driver's (dev.octep.0.heartbeat, dev.npuep.0.heartbeat;
docs/measurements/heartbeat-2026-10-22.md): the host asks the fast path every five seconds, and with
no answer it is late at 15 seconds and taken as stopped at 30, each said in the system log by the
driver itself the moment it happens. On the XGS 136, whose fast path drops the heartbeat message
unanswered, the answer is the beat being taken off the host's queue by the fast path's process -
the same states and lines. And dev.<driver>.0.gone: the driver has taken its coprocessor as gone -
its endpoint stopped decoding (the ARMADA family, whose link goes down as the coprocessor resets),
or its fast path stopped answering (the OCTEON TX family, whose link stays up while it boots again,
so the driver stops reading it then: #768).

WHAT IS DONE is xgs.general.fastpath_restart.
  1, the default since 2026-10-26: the coprocessor taken up again in place, the host running
     (recover.sh: the family's own way, measured on both before it was written) - and if that
     fails, the appliance restarted. Until then the OCTEON TX host froze as its coprocessor reset
     and its hardware watchdog restarted it four minutes later; the ARMADA one stayed up with no
     port working.
  0: it is said and nothing more - the driver's lines in the system log, and a problem report
     (reporter.py, kind fast-path-stopped).
  2, 5 or 10: once the fast path has been taken as stopped and gone that many minutes without an
     answer, the appliance is restarted - the report collected first, so it is not lost with the
     boot.
Nothing else brings a stopped fast path back: every frame through the front ports goes through it,
so the host has no connection it could take back and carry on its own. The vendor's host panics the
box after 120 seconds, with that switched off by default.

NEVER A LOOP: at most RECOVERIES_PER_DAY recoveries and RESTARTS_PER_DAY automatic restarts in any
24 hours, kept in STATE; a recovery that failed is not tried again for the same stop, which is
restarted for instead. Past the restarts it says so, once a boot, and leaves the appliance up for
somebody to look at.
"""

try:
    import fcntl
except ImportError:		# not on the appliance: tools/test-fastpath.py runs anywhere
    fcntl = None
import json
import os
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

CONFIG = os.environ.get('XGS_FASTPATH_CONFIG', '/conf/config.xml')
STATE = os.environ.get('XGS_FASTPATH_STATE', '/var/db/os-xgs-npu/fastpath.json')
NODES = ('dev.octep.0.heartbeat', 'dev.npuep.0.heartbeat')
CHOICES = (0, 1, 2, 5, 10)
DEFAULT = 1			# the model's default, for a configuration that has never saved the page
RESTARTS_PER_DAY = 3
RECOVERIES_PER_DAY = 6
DAY = 86400
# The driver's own tag, so the log shows it beside the driver's lines; a warning, not an error: the
# report is reporter.py's own kind, and an error line would make a second, generic one (measured
# 2026-10-22, the first restart test queued a log-error report of this line).
TAG = 'octep'
REPORTER = '/usr/local/opnsense/scripts/xgs/reporter.py'
REBOOT = '/usr/local/etc/rc.reboot'
RECOVER = os.environ.get('XGS_FASTPATH_RECOVER', '/usr/local/opnsense/scripts/xgs/recover.sh')
RECOVER_LOCK = '/var/run/xgs-recover.lock'	# recover.sh's own, held while it runs
RECOVER_SECONDS = 900


def sysctl(name):
    try:
        done = subprocess.run(['/sbin/sysctl', '-n', name], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    return done.stdout.strip() if done.returncode == 0 else None


def heartbeat():
    """The heartbeat's state as the driver keeps it, or None on a host without one; with 'gone',
    whether the driver has taken its coprocessor as gone."""
    for node in NODES:
        state = sysctl(node + '.state')
        if state is None:
            continue
        hb = {'node': node, 'state': state}
        for key in ('on', 'since_answer_ms', 'sent', 'answered', 'deaths', 'revivals', 'rtt_us'):
            v = sysctl('%s.%s' % (node, key))
            hb[key] = int(v) if v is not None and v.isdigit() else None
        gone = sysctl(node.rsplit('.', 1)[0] + '.gone')
        hb['gone'] = gone == '1'
        return hb
    return None


def setting():
    """xgs.general.fastpath_restart: DEFAULT for a configuration without it, 0 for anything that is
    not one of the choices."""
    try:
        root = ET.parse(CONFIG).getroot()
    except (OSError, ET.ParseError):
        return DEFAULT
    raw = root.findtext('OPNsense/XGS/general/fastpath_restart')
    if raw is None or raw.strip() == '':
        return DEFAULT
    try:
        minutes = int(raw)
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


def restart_or_refuse(silent, restarts, now, why):
    recent = [t for t in restarts if now - t < DAY]
    if len(recent) >= RESTARTS_PER_DAY:
        return 'refuse', ('the fast path has not answered for %d s, and the appliance was already '
                          'restarted for it %d times in 24 hours - left up for somebody to look at'
                          % (silent // 1000, len(recent)))
    return 'restart', why


def decide(hb, minutes, restarts, now, recoveries=(), failed=False):
    """What to do now, and why: ('none'|'recover'|'restart'|'refuse', reason). Pure, for the tests.
    recoveries: when the coprocessor was last taken up again; failed: the last try, for this stop,
    did not bring the fast path back."""
    if hb is None:
        return 'none', 'no heartbeat on this host'
    if minutes == 0:
        return 'none', 'the setting says to say so and nothing more'
    silent = hb.get('since_answer_ms') or 0
    if minutes == 1:
        if not hb.get('gone') and hb.get('state') != 'dead':
            return 'none', 'the fast path is %s' % hb.get('state')
        what = ('the driver has taken the coprocessor as gone' if hb.get('gone') else
                'the coprocessor\'s fast path has not answered its heartbeat for %d s' % (silent // 1000))
        if failed:
            return restart_or_refuse(silent, restarts, now, '%s, and taking it up again in place did '
                                     'not bring it back - restarting the appliance' % what)
        recent = [t for t in recoveries if now - t < DAY]
        if len(recent) >= RECOVERIES_PER_DAY:
            return restart_or_refuse(silent, restarts, now, '%s, and it was already taken up again %d '
                                     'times in 24 hours - restarting the appliance' % (what, len(recent)))
        return 'recover', '%s - taking it up again in place, as XGS: Settings says' % what
    if hb.get('state') != 'dead':
        return 'none', 'the fast path is %s' % hb.get('state')
    if silent < minutes * 60000:
        return 'none', 'taken as stopped %d s ago, the setting waits %d min' % (silent // 1000, minutes)
    return restart_or_refuse(silent, restarts, now,
                             'the coprocessor\'s fast path has not answered its heartbeat for %d s - '
                             'restarting the appliance, as XGS: Settings says after %d minutes'
                             % (silent // 1000, minutes))


def log(priority, message):
    try:
        subprocess.run(['/usr/bin/logger', '-t', TAG, '-p', 'daemon.' + priority, message], timeout=10)
    except (OSError, subprocess.SubprocessError):
        pass


def recovering():
    """Whether recover.sh is running now: it holds its lock for as long as it runs."""
    if fcntl is None:
        return False
    try:
        fd = os.open(RECOVER_LOCK, os.O_RDONLY)
    except OSError:
        return False
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        return True
    finally:
        os.close(fd)
    return False


def collect_report():
    # The report first: the heartbeat's counters go with this boot, or with the driver's reload.
    try:
        subprocess.run([REPORTER, 'collect'], capture_output=True, timeout=120)
    except (OSError, subprocess.SubprocessError):
        pass


def restart(kept, now, reason):
    kept['restarts'] = [t for t in kept.get('restarts', []) if now - t < DAY] + [now]
    save_state(kept)
    log('warning', reason)
    collect_report()
    subprocess.Popen([REBOOT], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, start_new_session=True)


def failed_now(kept, hb, boot):
    """Whether the last recovery failed for the stop going on now: the same boot, and no answer
    from the fast path since (its deaths counter as it was then)."""
    f = kept.get('recover_failed') or {}
    return bool(f) and f.get('boot') == boot and f.get('deaths') == (hb or {}).get('deaths')


def check():
    if recovering():
        return 0
    hb = heartbeat()
    minutes = setting()
    kept = load_state()
    now = int(time.time())
    boot = boot_time()
    action, reason = decide(hb, minutes, kept.get('restarts', []), now,
                            kept.get('recoveries', []), failed_now(kept, hb, boot))
    if action == 'refuse':
        if kept.get('refused_boot') != boot:
            log('warning', reason)
            kept['refused_boot'] = boot
            save_state(kept)
        return 0
    if action == 'restart':
        restart(kept, now, reason)
        return 0
    if action != 'recover':
        return 0
    kept['recoveries'] = [t for t in kept.get('recoveries', []) if now - t < DAY] + [now]
    kept.pop('recover_failed', None)
    save_state(kept)
    log('warning', reason)
    collect_report()
    try:
        done = subprocess.run([RECOVER], capture_output=True, text=True, timeout=RECOVER_SECONDS)
        lines, ok = done.stdout.splitlines(), done.returncode == 0
    except (OSError, subprocess.SubprocessError) as failure:
        lines, ok = ['recover.sh did not finish: %s' % failure], False
    for line in lines[-20:]:
        log('notice', 'recover: ' + line)
    if ok:
        return 0
    # Not back: this stop is restarted for, now - the next check would only find it the same.
    hb = heartbeat()
    kept = load_state()
    kept['recover_failed'] = {'boot': boot, 'deaths': (hb or {}).get('deaths'), 'at': now}
    save_state(kept)
    action, reason = decide(hb or {'state': 'dead', 'gone': True}, minutes, kept.get('restarts', []),
                            int(time.time()), kept.get('recoveries', []), True)
    if action == 'restart':
        restart(kept, int(time.time()), reason)
    elif action == 'refuse' and kept.get('refused_boot') != boot:
        log('warning', reason)
        kept['refused_boot'] = boot
        save_state(kept)
    return 0


def show():
    hb = heartbeat()
    minutes = setting()
    kept = load_state()
    now = int(time.time())
    action, reason = decide(hb, minutes, kept.get('restarts', []), now, kept.get('recoveries', []),
                            failed_now(kept, hb, boot_time()))
    print(json.dumps({'heartbeat': hb, 'setting': minutes, 'restart_after_min': minutes if minutes > 1 else 0,
                      'now': 'recovering' if recovering() else action, 'why': reason,
                      'restarts_24h': len([t for t in kept.get('restarts', []) if now - t < DAY]),
                      'recoveries_24h': len([t for t in kept.get('recoveries', []) if now - t < DAY])}))
    return 0


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else ''
    if what == 'check':
        sys.exit(check())
    if what == 'show':
        sys.exit(show())
    print(__doc__.split('\n\n')[1], file=sys.stderr)
    sys.exit(64)
