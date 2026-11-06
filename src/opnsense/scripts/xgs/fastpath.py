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
     port working. The stop's report is found before the recovery and sent behind it.
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
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

CONFIG = os.environ.get('XGS_FASTPATH_CONFIG', '/conf/config.xml')
STATE = os.environ.get('XGS_FASTPATH_STATE', '/var/db/os-xgs-npu/fastpath.json')
BOOT_TOKEN = '/var/run/xgs-fastpath-boot'	# this boot (boot_time)
NODES = ('dev.octep.0.heartbeat', 'dev.npuep.0.heartbeat')
CHOICES = (0, 1, 2, 5, 10)
DEFAULT = 1			# the model's default, for a configuration that has never saved the page
RESTARTS_PER_DAY = 3
RECOVERIES_PER_DAY = 6
# The day's restarts and recoveries are counted on the wall clock: they span the host's boots, which its
# monotonic clock does not. A time the clock has since been stepped back past - its age below zero -
# counts as recent, on purpose: a host whose clock starts behind until NTP sets it finds the restarts of
# the boots before in its future, and a bound on such ages, or such times set to now, would let each
# boot forget them and restart again (the review's F14; a boot itself is told apart by boot_time).
DAY = 86400
# The driver's own tag, so the log shows it beside the driver's lines; a warning, not an error: the
# report is reporter.py's own kind, and an error line would make a second, generic one (measured
# 2026-10-22, the first restart test queued a log-error report of this line).
TAG = 'octep'
REPORTER = '/usr/local/opnsense/scripts/xgs/reporter.py'
REBOOT = '/usr/local/etc/rc.reboot'
RECOVER = os.environ.get('XGS_FASTPATH_RECOVER', '/usr/local/opnsense/scripts/xgs/recover.sh')
RECOVER_LOCK = '/var/run/xgs-recover.lock'	# recover.sh's own, held while it runs
# This script's own, held for a whole check: the recovery and the restart that may follow it are
# one decision. Until it was (2026-10-26, the first trial on the XGS 136), a check that found the
# recovery over - failed, its lock let go - started a second one while the first was restarting.
CHECK_LOCK = '/var/run/xgs-fastpath.lock'
RECOVER_SECONDS = 900
# How long the check waits for reporter.py to find and queue, sending nothing, before a recovery or a
# restart (collect_report): the finding alone takes seconds, and more is a collect from the reporter's
# cron holding its lock - sending through the fast path that has stopped - which the recovery does not
# wait out. A recovery has to begin within some 100 s of the coprocessor's own reset, or recover.sh
# misses the line its new boot prints (recover.sh's START).
FIND_SECONDS = 30
# How long the processes of a recovery ended at its cap have to go (end_recovery): a SIGKILL ends a
# process at once, or one in a kernel call - kldunload, devctl - when that call returns.
GROUP_GRACE = 30


def sysctl(name):
    try:
        done = subprocess.run(['/sbin/sysctl', '-n', name], capture_output=True, text=True, errors='replace', timeout=10)
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
    """This boot, as the checks tell one from another (restarting, refused_boot, recover_failed): a token
    the first caller in a boot leaves in BOOT_TOKEN - /var/run, which is emptied at boot, as
    temperatures.sh keeps its own. Until 2026-11-06 it was kern.boottime's seconds, which a step of the
    clock moves: the check after one took the same boot for another, and asked for a second restart
    while one was under way, or took the coprocessor up again for a stop whose recovery had failed (the
    review's F14). None when it can be neither read nor left."""
    try:
        with open(BOOT_TOKEN, encoding='ascii') as f:
            token = f.read().strip()
    except (OSError, ValueError):
        token = ''
    if token:
        return token
    mine = '%s.%d' % (BOOT_TOKEN, os.getpid())
    try:
        with open(mine, 'w', encoding='ascii') as f:
            f.write(os.urandom(8).hex() + '\n')
        try:
            os.link(mine, BOOT_TOKEN)	# the first one left stands: a check and show() can begin at once
        except FileExistsError:
            pass
        with open(BOOT_TOKEN, encoding='ascii') as f:
            token = f.read().strip()
    except (OSError, ValueError):
        token = ''
    finally:
        try:
            os.unlink(mine)
        except OSError:
            pass
    return token or None


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
    """reporter.py collect before a recovery or a restart: what went wrong found and queued, and nothing
    sent (XGS_REPORT_NO_SEND). Found first: the heartbeat's counters go with the driver's reload in
    recover.sh, or with the boot, and the report of the stop with them - reporter.py reads it from
    heartbeat.deaths, which a driver brought up again starts at 0. Nothing sent: every frame goes
    through the fast path that has stopped, so a report sent now can only fail, each after its relay's
    20 s - and until 2026-11-06, with reporting automatic, the recovery and the restart waited for that,
    up to 120 s more of the outage (#1072, F6). What is queued is sent behind a
    recovery (send_reports), and after a restart by the next boot's reporter.py boot and its cron."""
    try:
        subprocess.run([REPORTER, 'collect'], capture_output=True, timeout=FIND_SECONDS,
                       env=dict(os.environ, XGS_REPORT_NO_SEND='1'))
    except (OSError, subprocess.SubprocessError):
        pass


def send_reports():
    """reporter.py collect once a recovery has brought the fast path back, in the background so that it
    holds no later check: what collect_report queued sent as reporting is set - automatic sends it,
    manual keeps it for the page's Send - and what the recovery itself logged found."""
    try:
        subprocess.Popen([REPORTER, 'collect'], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
    except OSError:
        pass


def restart(kept, now, reason, boot):
    kept['restarts'] = [t for t in kept.get('restarts', []) if now - t < DAY] + [now]
    # this boot's restart asked for: the next minute's check, while the stop hooks still run, leaves it
    kept['restarting'] = boot
    save_state(kept)
    log('warning', reason)
    # the report found and queued before the boot takes the counters, and sent from the next one
    collect_report()
    subprocess.Popen([REBOOT], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, start_new_session=True)


def members(group):
    """The pids of process group group's processes that still run - a zombie its stopped parent has not
    reaped runs no more - as ps(1) lists them; None when it could not be read."""
    try:
        done = subprocess.run(['/bin/ps', '-A', '-o', 'pid=', '-o', 'pgid=', '-o', 'stat='],
                              capture_output=True, text=True, errors='replace', timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    if done.returncode != 0:
        return None
    pids = []
    for line in done.stdout.splitlines():
        words = line.split()
        if len(words) == 3 and words[0].isdigit() and words[1] == str(group) and \
                not words[2].startswith('Z'):
            pids.append(int(words[0]))
    return pids


def end_recovery(group, grace=GROUP_GRACE):
    """recover.sh ended at its cap, all of it. It runs in a session of its own, so group - the process
    this check started - leads its process group, and is lockf: recover.sh runs itself again under
    lockf at its top, and lockf holds the lock its child, recover.sh, does not. lockf is
    stopped where it is, so that it can neither let the lock go nor reap; every other process of the
    group is killed, again and again, until none is left; and lockf last. So the lock is free only once
    nothing of the recovery runs, and no second recovery - this check's next minute, one by hand - and
    no watch of feeder.py's (recovering()) takes the recovery for over while a step of it still runs.
    Lines for the log: none, or the processes still there after grace - in a kernel call that has not
    returned - whose lock goes with lockf all the same: held on, it would keep every later recovery, and
    the watch, away for the rest of the boot. With ps not read, the group is killed at once."""
    try:
        os.kill(group, signal.SIGSTOP)
    except OSError:
        pass
    end = time.monotonic() + grace
    while True:
        found = members(group)
        left = [pid for pid in found or [] if pid != group]
        if not left or time.monotonic() >= end:
            break
        for pid in left:
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass
        time.sleep(0.2)
    try:
        os.killpg(group, signal.SIGKILL)
    except OSError:
        pass
    if not left:
        return []
    return ['recover.sh\'s process(es) %s still there %d s after they were killed, in a kernel call: its '
            'lock let go with lockf all the same' % (' '.join(str(pid) for pid in left), grace)]


def run_recover(seconds=None):
    """recover.sh run to its end, or for RECOVER_SECONDS: (the lines it said, whether it took the
    coprocessor up again). What it said before it was stopped is kept too (#985): on 2026-11-02 a run
    stopped at 900 s left only "did not finish", and not which of its steps it had been waiting in.
    communicate() keeps those lines when its time runs out, and gives them all when it is asked again -
    with what came after, up to the end of the recovery.

    In a session and a process group of its own, and ended whole at the cap (end_recovery). Until
    2026-11-06 subprocess.run's timeout killed its direct child alone - lockf, which recover.sh had
    become - and recover.sh carried on under no lock, in the middle of a driver's reload or of
    OPNsense's, while this check restarted the appliance or a later one could start a second recovery
    (#1072, F7).

    Its stderr with its stdout, in the order they came: what kldunload, devctl or lockf said of why a
    step failed is said nowhere else. Until 2026-11-06 it was read and thrown away (the review's F13)."""
    try:
        child = subprocess.Popen([RECOVER], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                 errors='replace', start_new_session=True)
    except (OSError, subprocess.SubprocessError) as failure:
        return ['recover.sh did not finish: %s' % failure], False
    with child:
        try:
            said, _ = child.communicate(timeout=seconds or RECOVER_SECONDS)
        except subprocess.TimeoutExpired as failure:
            left = end_recovery(child.pid)
            try:
                said, _ = child.communicate(timeout=5)
            except subprocess.TimeoutExpired as late:
                # its output still held open by something outside the group: what came is kept
                said = late.stdout or b''
            if isinstance(said, bytes):
                said = said.decode('utf-8', 'replace')
            ended = '' if left else '; every process of it ended, its lock last'
            return said.splitlines() + ['recover.sh did not finish: %s%s' % (failure, ended)] + left, False
    return said.splitlines(), child.returncode == 0


def failed_now(kept, hb, boot):
    """Whether the last recovery failed for the stop going on now: the same boot, and no answer
    from the fast path since (its deaths counter as it was then)."""
    f = kept.get('recover_failed') or {}
    return bool(f) and f.get('boot') == boot and f.get('deaths') == (hb or {}).get('deaths')


def check_lock():
    """This check's lock, kept open until the process ends; None while another check holds it."""
    if fcntl is None:
        return -1
    fd = os.open(CHECK_LOCK, os.O_CREAT | os.O_RDWR, 0o600)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        os.close(fd)
        return None
    return fd


def check():
    if check_lock() is None or recovering():
        return 0
    hb = heartbeat()
    minutes = setting()
    kept = load_state()
    now = int(time.time())
    boot = boot_time()
    # A restart asked for in this boot is under way: a reboot's stop hooks can take minutes (the kernel
    # follower's alone up to ten), and until 2026-11-06 the next minute's check found the same stop and
    # asked for another - two restarts counted against the day's three.
    if boot and kept.get('restarting') == boot:
        return 0
    action, reason = decide(hb, minutes, kept.get('restarts', []), now,
                            kept.get('recoveries', []), failed_now(kept, hb, boot))
    if action == 'refuse':
        if kept.get('refused_boot') != boot:
            log('warning', reason)
            kept['refused_boot'] = boot
            save_state(kept)
        return 0
    if action == 'restart':
        restart(kept, now, reason, boot)
        return 0
    if action != 'recover':
        return 0
    kept['recoveries'] = [t for t in kept.get('recoveries', []) if now - t < DAY] + [now]
    kept.pop('recover_failed', None)
    save_state(kept)
    log('warning', reason)
    # the stop's report found before the recovery, which reloads the driver and its counters, and sent
    # behind it, through the fast path it brought back
    collect_report()
    lines, ok = run_recover()
    for line in lines[-20:]:
        log('notice', 'recover: ' + line)
    if ok:
        send_reports()
        return 0
    # Not back: this stop is restarted for, now - the next check would only find it the same.
    hb = heartbeat()
    kept = load_state()
    kept['recover_failed'] = {'boot': boot, 'deaths': (hb or {}).get('deaths'), 'at': now}
    save_state(kept)
    action, reason = decide(hb or {'state': 'dead', 'gone': True}, minutes, kept.get('restarts', []),
                            int(time.time()), kept.get('recoveries', []), True)
    if action == 'restart':
        restart(kept, int(time.time()), reason, boot)
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
