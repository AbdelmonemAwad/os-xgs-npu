#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
The coprocessor's own watchdog feeder, looked after from the host (#768).
    feeder.py start     start the watcher, detached
    feeder.py stop      stop it
    feeder.py status    one line: watching, and the last thing it put right
    feeder.py run       the watcher itself, what start runs under daemon(8)
WHY. The coprocessor's Linux arms its hardware watchdog at boot and feeds it every five seconds from
busybox's `watchdog -t 5 /dev/watchdog`, which /etc/init.d/S15watchdog starts - the same on vendor
versions 21, 22 and 23. If that one process stops while the rest of the coprocessor runs, the watchdog
resets the coprocessor about a minute later. An XGS 3300's host does not come back from that reset: a
read of the device while it boots never completes, and the host's own watchdog restarts the appliance
some four minutes on (#768). An XGS 136 takes its coprocessor up again, without the restart but not
without the minutes with nothing passing. A feeder that stopped alone costs either one a reset it did
not need.
HOW. Every EVERY seconds the coprocessor is asked, through scripts/xgs/npu-run.sh - the management
link, or the console when that does not answer - for its uptime and its feeder's state:
  running            nothing to do;
  stopped (T, t)     a SIGSTOP: it is sent SIGCONT, in the same session (CONTINUE), at any uptime -
                     a feeder stopped in a boot's first minutes is reset as surely;
  none, or a zombie  started again by the coprocessor's own init script, as at its boot.
A correction is said in the system log (xgs-feeder) and kept in STATE for the support bundle.
Not started again: a feeder missing from a coprocessor up for less than SETTLE seconds, whose init
may not have started it yet. Not touched: a coprocessor that does not answer at all - its own
watchdog is right to reset that one. The looks run in a thread of their own (looker).
A lab run that stops the feeder on purpose stops this first: feeder.py stop.
THE MOMENT ITS FAST PATH STOPS (#819). On 2026-10-31 the first XGS 3300's coprocessor printed the
first line of a kernel oops and nothing after it; its watchdog leaves a hung coprocessor about a
minute before resetting it, and fastpath.py, at the next minute of cron, took it up again five
seconds short of that. So the driver's gone latch is read here every POLL seconds - a number in the
driver's memory, read without its lock (the heartbeat's state takes it, and the lock is destroyed
in the driver's detach before newbus takes the device's sysctls away), nothing asked of the
coprocessor - and the moment the driver takes the coprocessor as gone:
  - its kernel is asked for its state over its console, SysRq l, w and p (npu-sysrq.sh, which
    asks nothing once the coprocessor's own watchdog may have reset it), what it prints kept in
    the console's log - but not when the gone is this feeder's own quiesce (A LINUX THAT STOPS
    below): its watchdog's reset is then due within the minute, and keys could stop its boot;
  - fastpath.py check is run at once rather than at the next minute - the owner's choice in XGS:
    Settings decides, as it does from cron.
And once a coprocessor that booted again answers, the crash records its kernel left in its pstore
(dmesg-ramoops - written when an oops runs to its end) are read and kept in the console's log, each
record once (STATE keeps their digests). Its console record is not: the console's log has that.
A LINUX THAT STOPS WITHOUT A PANIC (#768). The coprocessor's own watchdog resets it about a minute after
its Linux stops feeding it, and the XGS 3300's host does not come back from its driver's accesses across
that reset (2026-11-02: with the root port's completion timeout at 1 to 10 ms, and with the coprocessor's
bus mastering off). A panic is slowed down instead - see A PANIC, SLOWED DOWN below; a hang has to be
seen coming. So every POLL seconds one echo request goes to
the coprocessor's end of the management link (LINK), and once it has gone EARLY seconds without an
answer the console is asked alone, ahead of every other caller of npu-run.sh (URGENT) - but only when
the link had answered for STEADY seconds without a gap longer than GAP before it went silent, no
recovery is running (recover.sh's lock), the fast path's heartbeat is alive and the driver not yet
gone. A console that answers means the link alone is down: the same session continues the watchdog
feeder if it is stopped (CONTINUE) - left to a later session, a stopped feeder's reset froze the host
(#1002) - and the link is given its address again if it stays silent RETELL seconds, at most once in
RETELL_EVERY seconds and not while a recovery runs. A console that shows no prompt to two carriage
returns is asked once more when the link has been silent SILENT seconds, with one carriage return;
still silent, and the link not answering one more echo, its Linux has stopped, and octep is told
(QUIESCE) to stop touching the coprocessor before its watchdog resets it; the stop above follows, and
fastpath.py takes it up again. The looks run in a thread of their own, so that the polls go on while
one lasts - over the console half a minute and more: run between two polls, a look blinded the watch
while it lasted, and a stop inside one was decided after the coprocessor's reset (#1012, 2 of 2). A
look that finds the console silent, its link's ssh not answering either, while the link answers its
echoes is said, and not acted on (watch_userland): its userland may have stopped under a kernel that
answers, but a console that is alive can show no prompt as well. The second XGS 3300
showed why the steady link is asked for (2026-11-02): right after a recovery its link answered once
and went quiet for half a minute while OPNsense set its interfaces up again, its console - which drops
characters in the first minutes of a boot - did not answer either, and a healthy coprocessor was taken
as stopped. A watch started on a coprocessor long up - after an update of this
package, or by hand - had its own first two minutes unseen that way, and a stop in them went on to the
reset (2026-11-02, #987). So a look made during the link's present run of answers that finds the
coprocessor up LONG_UP seconds - long past its boot, and its console with it - makes the run steady
from its first answer. After a boot or a recovery no look can, and the two minutes are still asked for.
While the link is silent and that is not yet decided, no look is started: the watch's own ask of
the console is the one that has to come in time then. Only octep has QUIESCE: the XGS 136's driver
sees its link go down.
And every look sets the coprocessor's panic timeout (PANIC_WAIT, on octep's family only), so that a
panic waits for the driver to let go instead of restarting at once.
Never fails the caller: a host with no coprocessor to ask says so and exits 0.
"""
import fcntl
import hashlib
import json
import os
import signal
import subprocess
import sys
import threading
import time

NPU_RUN = '/usr/local/opnsense/scripts/xgs/npu-run.sh'
SYSRQ = '/usr/local/opnsense/scripts/xgs/npu-sysrq.sh'
FASTPATH = '/usr/local/opnsense/scripts/xgs/fastpath.py'
CONSOLE_LOG = '/usr/local/opnsense/scripts/xgs/console_log.py'
STATE = '/var/db/os-xgs-npu/feeder.json'
PIDFILE = '/var/run/xgs-feeder.pid'
EVERY = 20
SETTLE = 120
POLL = 2
GONE = 'dev.octep.0.gone'
LINK = '169.254.254.2'		# the coprocessor's end of the management link (npuctl/mgmt-link.sh)
EARLY = 10			# seconds of silence on the link after which its console is asked
SILENT = 30			# seconds of silence on the link after which a silent console stops the driver
RETELL = 45			# seconds of silence after which a link found down alone is given its address again
RETELL_EVERY = 600		# the least time between two of those, over every silence
STEADY = 120
GAP = 3 * POLL + 1		# a gap no longer than this - an echo lost, with its wait - does not end a link's steady run
LONG_UP = 600			# a coprocessor up this long is past its boot (see A LINUX THAT STOPS)
FRESH = 40			# how long after a look began its silent console still counts (watch_userland)
UPTIME = {'at': None, 'uptime': None}	# the last look's answer: when (time.monotonic()), and the uptime
LOOKED = {'proof': None, 'silent': None, 'ssh': None}	# when the last look that had an answer began (or
				# the watch's own ask of the console was answered), the last look that found the
				# console silent (NO_PROMPT), and the last one answered over the link's ssh -
				# time.monotonic()
NPU_EXIT = {'rc': None, 'link': False}	# npu-run.sh's exit at the look's last ask; whether the link took it
OVER_LINK = 'over the link'	# npu-run.sh's word on stderr when the link's ssh takes it (NPU_RUN_SAY_LINK)
WATCH = {'holding': False}	# the watch weighing a silence: no look is started meanwhile
USERLAND = {'said': None}	# the run of answers (its start) in which watch_userland last said what it saw
RETOLD = {'at': None}		# when (time.monotonic()) the link was last given its address again
QUIESCED = {'at': None}		# when (time.monotonic()) this feeder last told octep its coprocessor is about to reset
CONSOLE_CONTINUED = {'pid': None}	# the feeder the watch's last ask of the console continued, if any
RECOVER_LOCK = '/var/run/xgs-recover.lock'	# recover.sh's own, held while it runs (as fastpath.py reads it)
BUSY = 75			# npu-run.sh's exit when another caller holds the console
NO_PROMPT = 69			# npu-run.sh's exit when its console showed no Linux prompt, asked twice (NPU_RUN_TWICE)
URGENT = '/var/run/xgs-npu-console.urgent'	# npu-run.sh's mark: the watch's ask of the console comes first
# The longest the watch's first ask waits for another caller's session under way. A console session
# holds the console about its seconds and seven more, and portstats.py and os-fanctl's board curve ask
# for 20: some 27 s, which 25 fell short of (a review of the watch, 2026-11-04). The ask's own
# timeout - its wait and 40 s - and the mark's minute still cover it.
URGENT_WAIT = 30
MGMT_LINK = '/usr/local/opnsense/scripts/npuctl/mgmt-link.sh'
STATE_LOCK = threading.Lock()	# the looks and the watch both write STATE
QUIESCE = 'dev.octep.0.quiesce'
HEARTBEAT = 'dev.octep.0.heartbeat.state'
# The crash records the coprocessor's kernel left, read without keeping pstore mounted.
PSTORE = ('mount -t pstore -o ro pstore /sys/fs/pstore 2>/dev/null; '
          'for f in /sys/fs/pstore/dmesg-*; do [ -f "$f" ] && { echo "=== $f"; cat "$f"; }; done; '
          'umount /sys/fs/pstore 2>/dev/null; true')
# A stopped feeder is sent SIGCONT in the same session that finds it - "continued <pid>" - not in a
# second one. Over the console each session is a login of its own, and with the management link down a
# look and its SIGCONT did not fit in the minute the coprocessor's watchdog gives: the second XGS
# 3300's host froze at its reset, 2 of 2 (#1002). The watch's own ask of the console sends it too. At
# any uptime: a SIGCONT is harmless, and a feeder stopped in a boot's first SETTLE seconds is reset as
# surely as a later one (the review of the fix for #1002, A3).
CONTINUE = '[ -n "$p" ] && grep -q "^State:.[Tt]" /proc/$p/status && kill -CONT $p && echo "continued $p"'
# One command, read back line by line: the coprocessor's uptime in seconds, the feeder's pid or
# "none", its State line from /proc as it was found, and "continued <pid>" if it was stopped.
# busybox has no pgrep -f here, but has pidof.
ASK = ('cut -d. -f1 /proc/uptime; p=$(pidof watchdog | cut -d" " -f1); echo "pid ${p:-none}"; '
       '[ -n "$p" ] && grep -m1 "^State:" /proc/$p/status; ' + CONTINUE + '; true')
# A PANIC, SLOWED DOWN (#768). An OCTEON TX coprocessor's kernel restarts a second after a panic (its
# kernel.panic is 1), too soon for the driver: on 2026-11-02 that restart froze the host in four of
# five panics on the first XGS 3300 and in the one tried on the second. Told to wait PANIC_WAIT seconds
# instead, its fast path stops with it, the driver takes it as gone when the heartbeat dies at 30 s,
# and the restart comes after: the host stayed up in all four such runs, two on each XGS 3300 - the
# last with the value set by this look itself - the coprocessor taken up again in place. The value is
# a run-time one, gone with every boot of the coprocessor, so each look sets it again and reads it
# back - "panic <before> <after>". Only where octep is: the XGS 136's link goes down with its
# coprocessor, and its driver lets go at once.
PANIC_WAIT = 60
PANIC_ASK = ('; t=$(cat /proc/sys/kernel/panic 2>/dev/null); '
             '[ "$t" = "%d" ] || echo %d > /proc/sys/kernel/panic; '
             'echo "panic ${t:-unknown} $(cat /proc/sys/kernel/panic 2>/dev/null)"' % (PANIC_WAIT, PANIC_WAIT))
PANIC_TOLD = {'boot': None}	# the coprocessor's boot (its start, in host time) last said about


def say(text):
    subprocess.run(['/usr/bin/logger', '-t', 'xgs-feeder', text], capture_output=True)


def npu(seconds, command):
    """What the coprocessor printed, or None when it could not be asked - npu-run.sh's exit kept in
    NPU_EXIT, and whether the link's ssh carried the command, which npu-run.sh says on stderr when it is
    asked to (NPU_RUN_SAY_LINK). Over the console it is asked twice before it is taken as silent
    (NPU_RUN_TWICE)."""
    NPU_EXIT.update(rc=None, link=False)
    try:
        done = subprocess.run(['/bin/sh', NPU_RUN, str(seconds), command], stdin=subprocess.DEVNULL,
                              capture_output=True, text=True, errors='replace', timeout=seconds + 60,
                              env=dict(os.environ, NPU_RUN_TWICE='1', NPU_RUN_SAY_LINK='1'))
    except (OSError, subprocess.SubprocessError):
        return None
    NPU_EXIT.update(rc=done.returncode,
                    link=OVER_LINK in [line.strip() for line in (done.stderr or '').splitlines()])
    return done.stdout if done.returncode == 0 else None


def look():
    """(uptime, pid, state letter, panic timeout before this look, the pid it continued or None) of
    the feeder as the coprocessor reports it, or None."""
    out = npu(8, ASK + PANIC_ASK if sysctl(GONE) is not None else ASK)
    if out is None:
        return None
    uptime = pid = state = panic = continued = None
    for line in out.replace('\r', '').splitlines():
        line = line.strip()
        if line.isdigit() and uptime is None:
            uptime = int(line)
        elif line.startswith('pid '):
            pid = line[4:]
        elif line.startswith('State:'):
            parts = line.split()
            state = parts[1] if len(parts) > 1 else None
        elif line.startswith('panic '):
            panic = line[6:]
        elif line.startswith('continued '):
            continued = line[10:]
    if uptime is None or pid is None:
        return None
    return uptime, (None if pid == 'none' else pid), state, panic, continued


def load():
    try:
        with open(STATE, encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save(state):
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    with open(STATE + '.tmp', 'w', encoding='utf-8') as f:
        json.dump(state, f)
    os.replace(STATE + '.tmp', STATE)


def keep(found, done, pid):
    with STATE_LOCK:
        state = load()
        state['last'] = {'at': int(time.time()), 'found': found, 'done': done, 'pid': pid}
        state['count'] = state.get('count', 0) + 1
        save(state)


def gone():
    """'1' while the driver has taken the coprocessor as gone, '0' otherwise, None with no such
    driver - a plain number in its memory, read without its lock."""
    try:
        done = subprocess.run(['/sbin/sysctl', '-n', GONE], capture_output=True, text=True, errors='replace', timeout=5)
        value = done.stdout.strip()
        return value if value in ('0', '1') else None
    except (OSError, subprocess.SubprocessError):
        return None


def at_the_stop():
    """The driver has just taken the coprocessor as gone: the kernel's state over the console, then
    the owner's choice at once. Not the kernel's state when the gone is this feeder's own quiesce: its
    Linux answered on neither link nor console, and its watchdog resets it within the minute - SysRq's
    breaks and keys sent then could land in its boot firmware's countdown and stop the boot that
    fastpath.py waits for (npu-sysrq.sh's own guard counts from a dead heartbeat, and a quiesced one is
    still alive; a review of the release, 2026-11-03). Nor when the heartbeat is not dead: the gone was
    then not the fast path's silence - octep marks the beat dead before it lets go for that - but octep
    finding BAR0's readiness register without its magic, the reset already under way and the console
    its boot firmware's (#1030; on 2026-11-04 gone read 1 with the heartbeat still alive), and
    npu-sysrq.sh's guard lets every key through then (a review of the watch, 2026-11-06)."""
    beat = sysctl(HEARTBEAT)
    if QUIESCED['at'] is not None and time.monotonic() - QUIESCED['at'] < 60:
        say('the coprocessor\'s kernel is not asked for its state over its console: the driver was told '
            'it is about to be reset, and keys sent now could reach its boot firmware')
    elif beat != 'dead':
        say('the coprocessor\'s kernel is not asked for its state over its console: the driver let go of '
            'it while its fast path\'s heartbeat read %s, not dead - its reset may be under way, and keys '
            'sent now could reach its boot firmware' % (beat or 'nothing'))
    elif os.access(SYSRQ, os.R_OK):
        try:
            subprocess.run(['/bin/sh', SYSRQ, 'lwp', '5'], stdin=subprocess.DEVNULL,
                           capture_output=True, timeout=60)
            say('the coprocessor\'s fast path stopped answering: its kernel was asked for its state over '
                'its console (SysRq l, w, p), kept in /var/log/xgs/npu-console.log')
        except (OSError, subprocess.SubprocessError):
            pass
    if os.access(FASTPATH, os.R_OK):
        subprocess.Popen([sys.executable, FASTPATH, 'check'], stdin=subprocess.DEVNULL,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)


def crash_records(uptime):
    """Once for each boot of the coprocessor: the crash records its kernel left in pstore, kept in the
    console's log - each one once."""
    state = load()
    booted = int(time.time()) - uptime
    if abs(booted - state.get('pstore_boot', 0)) < 60:
        return
    out = npu(15, PSTORE)
    if out is None:
        return
    with STATE_LOCK:
        state = load()		# again: the watch may have kept something meanwhile
        state['pstore_boot'] = booted
        seen = state.get('pstore_seen', [])
        fresh = []
        for record in out.replace('\r', '').split('=== ')[1:]:
            digest = hashlib.sha1(record.encode()).hexdigest()
            if digest not in seen:
                fresh.append('=== ' + record)
                seen.append(digest)
        state['pstore_seen'] = seen[-20:]
        save(state)
    if fresh:
        console_keep(''.join(fresh), 'pstore, after the coprocessor booted again')
        say('the coprocessor\'s kernel had left %d crash record(s) in its pstore: kept in '
            '/var/log/xgs/npu-console.log' % len(fresh))


def console_keep(text, who):
    """Text into the console's log, as console_log.py keep writes a caller's read."""
    path = '/var/run/xgs-feeder-keep.%d' % os.getpid()
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)
    try:
        subprocess.run([sys.executable, CONSOLE_LOG, 'keep', path, who], capture_output=True, timeout=30)
    finally:
        os.unlink(path)


def watch_beat(beat):
    """The gone latch read once: at_the_stop when it turns to 1 - not for a feeder started while it
    already was - and the value to compare the next read with."""
    now = gone()
    if now is None:
        return beat
    if now == '1' and beat == '0':
        try:
            at_the_stop()
        except Exception as e:
            say('feeder.py: at the stop: %s' % e)
    return now


def answers_on_link():
    """One echo request to the coprocessor's end of the management link, given a second."""
    try:
        return subprocess.run(['/sbin/ping', '-c', '1', '-t', '1', '-q', LINK], capture_output=True,
                              timeout=5).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def sysctl(name):
    """A sysctl's value, None when there is no such sysctl."""
    try:
        done = subprocess.run(['/sbin/sysctl', '-n', name], capture_output=True, text=True, errors='replace', timeout=5)
    except (OSError, subprocess.SubprocessError):
        return None
    return done.stdout.strip() if done.returncode == 0 else None


def want_console():
    """URGENT marked: npu-run.sh's other callers stand aside while it is fresh."""
    try:
        with open(URGENT, 'a'):
            pass
        os.utime(URGENT, None)
    except OSError:
        pass


def release_console():
    try:
        os.unlink(URGENT)
    except OSError:
        pass


def console_answers(wait=5, twice=True):
    """The coprocessor asked on its console alone, the link being what is silent - ahead of every other
    caller of npu-run.sh (URGENT, kept until the silence is decided), and waiting up to wait seconds for
    a session already under way: True when it answered, False when it showed no Linux prompt (NO_PROMPT),
    None when it gave no answer either way - held, refused, or its answer lost after a prompt. Asked
    twice before it is taken as silent (NPU_RUN_TWICE), unless twice is False: the
    ask at SILENT that confirms a console an earlier ask found silent. The same session continues a
    stopped watchdog feeder (CONTINUE), and CONSOLE_CONTINUED says which: with the link down, the looks
    that would have found it are held while the silence is weighed (#1002)."""
    CONSOLE_CONTINUED['pid'] = None
    want_console()
    env = dict(os.environ, NPU_RUN_NO_LINK='1', NPU_RUN_URGENT='1', NPU_RUN_WAIT=str(int(wait)))
    env.pop('NPU_RUN_TWICE', None)
    if twice:
        env['NPU_RUN_TWICE'] = '1'
    try:
        done = subprocess.run(['/bin/sh', NPU_RUN, '4', 'echo alive; p=$(pidof watchdog | cut -d" " -f1); '
                               + CONTINUE], stdin=subprocess.DEVNULL, capture_output=True, text=True,
                              errors='replace', timeout=wait + 40, env=env)
    except (OSError, subprocess.SubprocessError):
        return None
    if done.returncode == BUSY:
        return None
    # npu-run.sh prints what came after the begin marker even when the end marker did not come (exit
    # 1): "alive" there is the coprocessor's shell answering, a stopped feeder perhaps continued too -
    # taken as a silent console, the driver would be quiesced for a reset that no longer comes. Read line
    # by line, as look() reads: the word in a kernel message on the console is not the shell's answer.
    lines = [line.strip() for line in done.stdout.replace('\r', '').splitlines()]
    for line in lines:
        if line.startswith('continued ') and line[10:].isdigit():
            CONSOLE_CONTINUED['pid'] = line[10:]
    if 'alive' in lines:
        return True
    # Only npu-run.sh's NO_PROMPT is a console found silent. Its exit 1 says nothing of the coprocessor's
    # Linux - the console refused (the driver gone, the handshake not complete, no device), the host's
    # UART not attached again, or the prompt shown and the answer lost, a quote dropped from the command
    # leaving the shell in a continuation prompt - and the looks take none of it for a silence either.
    # Until 2026-11-06 all of it was a silent console, and a live Linux could be quiesced (F2).
    return False if done.returncode == NO_PROMPT else None


def recovering():
    """Whether recover.sh is running now: it holds its lock for as long as it runs."""
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


def tell_driver():
    try:
        return subprocess.run(['/sbin/sysctl', '-q', QUIESCE + '=1'], capture_output=True,
                              timeout=5).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def past_boot(link):
    """Whether a look made during the link's present run of answers found the coprocessor up LONG_UP
    seconds by the link's last answer. A look from before the run says nothing of it: the coprocessor
    may have started again in the silence before."""
    seen = dict(UPTIME)		# one copy: the looks write it from their own thread
    at, uptime = seen['at'], seen['uptime']
    if at is None or at < link.get('up', link['ok']):
        return False
    return link['ok'] - (at - uptime) >= LONG_UP


def steady(link):
    """Whether the link's present run of answers is STEADY seconds long, or a look during it found the
    coprocessor long past its boot."""
    return link['ok'] - link.get('up', link['ok']) >= STEADY or past_boot(link)


def retell_link():
    """The coprocessor's end of the management link given its address again, in the background, as
    recover.sh does after a recovery (mgmt-link.sh coprocessor): found down while the console answers,
    nothing else gives it back short of a boot or a recovery, and until it answers the watch has this
    silence decided and every look goes over the console (the review of the fix for #1002, A4)."""
    if not os.access(MGMT_LINK, os.R_OK):
        return False
    try:
        if subprocess.run(['/bin/pgrep', '-f', 'mgmt-link.sh coprocessor'], capture_output=True,
                          timeout=5).returncode == 0:
            return False
        return subprocess.run(['/usr/sbin/daemon', '-f', '/bin/sh', MGMT_LINK, 'coprocessor'],
                              capture_output=True, timeout=10).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def retell(link, gap, now):
    """retell_link, once in a silence decided "alive" - and at most once in RETELL_EVERY seconds over all
    of them, and not while a recovery runs or the driver has the coprocessor as gone. A link that flaps
    on a coprocessor long up makes each new run steady at its first look, and had a retell at each
    silence, each one reconfiguring NPUMGMT and trying the console for up to five minutes; and a
    recovery gives the link its address itself, at its end (a review of the watch, 2026-11-04). Held
    back, it is asked again at a later poll of the same silence."""
    if RETOLD['at'] is not None and now - RETOLD['at'] < RETELL_EVERY:
        return link
    if recovering() or sysctl(GONE) != '0':
        return link
    if retell_link():
        RETOLD['at'] = now
        say('the coprocessor\'s end of its management link is given its address again (mgmt-link.sh '
            'coprocessor): silent for %d s while its console answers' % int(gap))
    return dict(link, retold=True)


def stop_the_driver(why):
    """octep told that the coprocessor's own watchdog is about to reset it (QUIESCE): said, and kept. A
    look's silent console from before it says nothing of the coprocessor that comes back (LOOKED)."""
    LOOKED['silent'] = None
    told = tell_driver()
    if told:
        QUIESCED['at'] = time.monotonic()
    say('%s: its Linux has stopped, and its own watchdog resets it within the minute - %s'
        % (why, 'octep told to stop touching it first' if told else 'octep could not be told'))
    # kept as the other records are: a state file that cannot be written - a full /var/db - took the
    # watch out before it returned its decision, and octep was told again at every poll
    try:
        keep('silent', 'the driver told to stop touching it' if told else 'the driver could not be told',
             None)
    except (OSError, ValueError) as e:
        say('feeder.py: %s' % e)
    return told


def console_alive(link, gap, now, held, again=''):
    """The console answered the watch's own ask: the link alone is down. Said, a feeder the same session
    continued said and kept, and the answer kept as the last proof of life (LOOKED): a look's silent
    console from before it is outranked by it."""
    release_console()
    LOOKED['proof'] = now
    say('the coprocessor has not answered on its management link for %d s, but its console does%s: '
        'left as it is%s' % (int(gap), again, held))
    pid = CONSOLE_CONTINUED['pid']
    if pid:
        say('the coprocessor\'s watchdog feeder (pid %s) was stopped: continued, over its console' % pid)
        try:
            keep('stopped', 'continued', pid)
        except (OSError, ValueError) as e:
            say('feeder.py: %s' % e)
    return dict(link, acted=True, why='alive', said=True)


def watch_link(link, now=None):
    """The management link looked at once - see A LINUX THAT STOPS above. link is {'ok': when it last
    answered, None if it never has; 'up': when its present run of answers began; 'acted': whether this
    silence has been decided, and 'why' - 'alive' when its console answered, 'told' when octep was;
    'asked', 'struck': whether its console has been asked in it, and when it was found silent}; the
    next is returned.

    The silence is the time since the last answer, nothing taken off it: the looks run in a thread of
    their own and the polls go on meanwhile. Until 2026-11-04 a look ran between two polls and its
    seconds were lent to the link (#1008, for #1001), and a stop inside a slow look was counted from the
    look's end - after the coprocessor's reset, 2 of 2 (#1012).

    The console is asked at EARLY rather than SILENT, so that a stopped feeder is continued in time
    even behind another caller's session: the first ask waits up to URGENT_WAIT for it, and the callers
    that come after stand aside (URGENT). An answer decides at once: the link alone is down. A console
    silent to two carriage returns before SILENT is asked once more at SILENT, with one carriage return,
    and octep is told when that finds it silent too and one more echo goes unanswered - a Linux that only
    stalled answers its link or its console again first. A console held all along decides nothing."""
    now = time.monotonic() if now is None else now
    gap = None if link.get('ok') is None else now - link['ok']
    if answers_on_link():
        if link.get('said'):
            say('the coprocessor answers on its management link again')
        if link.get('asked') and not link.get('acted'):
            release_console()
        steady_run = gap is not None and gap <= GAP
        return {'ok': now, 'up': link.get('up', now) if steady_run else now, 'acted': False}
    if gap is None or gap < EARLY:
        return link
    if link.get('acted'):
        if link.get('why') == 'alive' and gap >= RETELL and not link.get('retold'):
            return retell(link, gap, now)
        return link
    # a link that had not answered steadily is one coming up - after a boot, or a recovery's own steps -
    # unless a look during its run found the coprocessor long past its boot
    if not steady(link) or recovering() or \
            sysctl(QUIESCE) is None or sysctl(GONE) != '0' or sysctl(HEARTBEAT) != 'alive':
        if link.get('asked'):
            release_console()
        return dict(link, acted=True)
    busy = link.get('busy', 0)
    held = ' (no answer either way from its console at %d look(s) before)' % busy if busy else ''
    if link.get('struck') is None:
        begun = time.monotonic()
        answered = console_answers(URGENT_WAIT if gap < SILENT else 5)
        now += time.monotonic() - begun
        gap = now - link['ok']
        link = dict(link, asked=True)
        if answered is None:
            # held for all of the wait, by a session begun before the mark - or refused, or its answer
            # lost after a prompt: no answer either way, asked again at the next poll
            return dict(link, busy=busy + 1)
        if answered:
            return console_alive(link, gap, now, held)
        link = dict(link, struck=now)
    if gap < SILENT:
        return link
    found = link['struck'] - link['ok']
    again = ''
    if found < SILENT:
        # Found silent before SILENT, up to some 20 s ago: asked once more, with one carriage return and
        # a short wait - 3 to 4 s with no prompt - so that the quiesce does not rest on that answer
        # alone: a console that stalled for some seconds around the first ask answers by now, a Linux
        # that has stopped does not (a review of the watch, 2026-11-04). With no answer either way - held
        # by a holder the mark does not keep off, refused, or a prompt shown and its answer lost - it is
        # asked again at the next poll: the quiesce never rests on the first answer alone (until
        # 2026-11-06 the first answer stood, F2).
        asked_at = gap
        begun = time.monotonic()
        answered = console_answers(5, twice=False)
        now += time.monotonic() - begun
        gap = now - link['ok']
        if answered:
            return console_alive(link, gap, now, held,
                                 ' - asked again after it showed no prompt at %d s' % int(found))
        if answered is None:
            return link
        again = ', found silent at %d s and again at %d s' % (int(found), int(asked_at))
    # One echo after the asks: the first can wait URGENT_WAIT for another caller's session, and a link
    # that answered again meanwhile was quiesced all the same (a review of the watch, 2026-11-04).
    if answers_on_link():
        release_console()
        return {'ok': now, 'up': now, 'acted': False}
    release_console()
    stop_the_driver('the coprocessor has answered neither on its management link for %d s nor on its '
                    'console%s%s, while its fast path still does' % (int(gap), again, held))
    return dict(link, acted=True, why='told', said=True)


def holding(link, now=None):
    """Whether no look is started: the link silent, and the silence not yet decided."""
    now = time.monotonic() if now is None else now
    return link.get('ok') is not None and not link.get('acted') and now - link['ok'] > 2 * POLL


def watch_userland(link, now=None):
    """The looks weighed while the link answers its echoes on a steady run: a look that found the console
    without a Linux prompt to two carriage returns (NO_PROMPT), the link's ssh not answering it either, is
    said once for each run of answers - with whether a look over ssh had an answer earlier in the run -
    and not acted on. Its userland may have stopped under a kernel that answers every echo (the review's
    F6: the host froze at the reset); but a console that is alive shows no prompt as well - a login
    asking for a password, as one was left on the XGS 136 (#1010), a shell in a continuation prompt, a
    command in the foreground - and octep told of a reset that does not come ends in a recovery that finds
    no prompt to reboot the coprocessor at, and so in a restart of the appliance (2026-11-02). Until
    npu-run.sh's Ctrl-C and an answer over ssh in the run are measured as the evidence, F6 is said here
    and left to the coprocessor's own watchdog, as before (a review of the watch, 2026-11-04).
    A look counts only when it began in the present run of answers, at most FRESH seconds ago, and after
    the last answer to a look or to the watch's own ask: one from before says nothing of this run - a
    recovery, or a boot's first minutes, came between. Nor after the link alone was found down: that
    lasts as long as the link stays down, and one look's no prompt an hour on told octep. The checks for
    a silent link come first, so that the line marks what would have been told. True when it was said."""
    now = time.monotonic() if now is None else now
    if link.get('ok') is None or link.get('acted') or now - link['ok'] > 2 * POLL or not steady(link):
        return False
    began = link.get('up', link['ok'])
    seen = dict(LOOKED)		# one copy: the looks write it from their own thread
    silent = seen['silent']
    if silent is None or silent < began or now - silent > FRESH or \
            (seen['proof'] is not None and silent < seen['proof']) or USERLAND['said'] == began:
        return False
    if recovering() or sysctl(QUIESCE) is None or sysctl(GONE) != '0' or sysctl(HEARTBEAT) != 'alive':
        return False
    USERLAND['said'] = began
    ssh = seen['ssh'] is not None and seen['ssh'] >= began
    say('a look found the coprocessor\'s console without a Linux prompt and its management link\'s ssh not '
        'answering, while the link answers its echoes: not acted on; %s'
        % ('a look over ssh had an answer earlier in the link\'s present run' if ssh else
           'no look over ssh had an answer in the link\'s present run'))
    return True


def panic_wait(uptime, panic):
    """What the look found of the coprocessor's panic timeout ("<before> <after>"), said once for each
    of its boots: a look finds it otherwise only on the first look after a boot, and sets it then."""
    words = (panic or '').split()
    if not words or words[0] == str(PANIC_WAIT):
        return
    boot = int(time.time()) - uptime
    if PANIC_TOLD['boot'] is not None and abs(boot - PANIC_TOLD['boot']) <= 2 * EVERY:
        return
    PANIC_TOLD['boot'] = boot
    if len(words) > 1 and words[1] == str(PANIC_WAIT):
        say('the coprocessor would have restarted %s s after a panic, before the driver lets go of it: it '
            'waits %d s now (#768)' % (words[0], PANIC_WAIT))
    else:
        say('the coprocessor restarts %s s after a panic, and could not be told to wait %d s (#768)'
            % (words[-1], PANIC_WAIT))


def once():
    """One look, and what it put right: None when nothing needed it."""
    started = time.monotonic()
    NPU_EXIT.update(rc=None, link=False)
    seen = look()
    if seen is None:
        # its console asked twice and silent, its link's ssh not answering either: kept for the watch,
        # which says so (watch_userland)
        if NPU_EXIT['rc'] == NO_PROMPT:
            LOOKED['silent'] = started
        return 'unreachable'
    uptime, pid, state, panic, continued = seen
    # taken when the answer is in: the boot it gives is never earlier than the real one
    UPTIME.update(at=time.monotonic(), uptime=uptime)
    LOOKED['proof'] = started
    if NPU_EXIT['link']:
        # answered over the link's ssh: F6 is ssh working in the link's run, and then not
        LOOKED['ssh'] = started
    panic_wait(uptime, panic)
    if pid is not None and state in ('T', 't') and pid.isdigit():
        # continued by the look itself (CONTINUE), at any uptime; asked again only if that did not take
        if continued == pid:
            done = 'continued'
        else:
            answer = npu(5, 'kill -CONT %s && echo continued' % pid)
            done = 'continued' if answer and 'continued' in answer else 'could not continue it'
        say('the coprocessor\'s watchdog feeder (pid %s) was stopped: %s' % (pid, done))
        keep('stopped', done, pid)
        return done
    if uptime < SETTLE:
        return None
    try:
        crash_records(uptime)
    except (OSError, subprocess.SubprocessError, ValueError) as e:
        say('feeder.py: the coprocessor\'s pstore: %s' % e)
    if pid is None or state == 'Z':
        answer = npu(8, '/etc/init.d/S15watchdog start; sleep 1; pidof watchdog')
        new = (answer or '').replace('\r', '').split()
        done = 'started again (pid %s)' % new[-1] if new and new[-1].isdigit() else 'could not start it'
        say('the coprocessor\'s watchdog feeder was %s: %s' % ('a zombie' if pid else 'not running', done))
        keep('zombie' if pid else 'gone', done, pid)
        return done
    return None


def look_if_due(due, unreachable, now=None):
    """A look, if one is due and the watch is not weighing a silence (WATCH): the next due, and whether
    the coprocessor is out of reach - said once, when it goes and when it comes back."""
    now = time.monotonic() if now is None else now
    if WATCH['holding'] or now < due:
        return due, unreachable
    due = now + EVERY
    try:
        result = once()
    except Exception as e:
        result = None
        say('feeder.py: %s' % e)
    if result == 'unreachable' and not unreachable:
        unreachable = True
        say('the coprocessor does not answer: its watchdog feeder is not looked at until it does')
    elif result != 'unreachable' and unreachable:
        unreachable = False
        say('the coprocessor answers again: its watchdog feeder is looked at every %d s' % EVERY)
    return due, unreachable


def looker():
    """The looks, in a thread of their own (#1012): one lasts as long as its transport takes - over the
    console, after ssh's own wait, half a minute and more - and the watch's polls go on meanwhile."""
    due, unreachable = 0.0, False
    while True:
        try:
            due, unreachable = look_if_due(due, unreachable)
        except Exception as e:
            say('feeder.py: the looks: %s' % e)
        time.sleep(1)


def terminated(*_):
    """SIGTERM: the console's mark taken back on the way out - a watch stopped while it asked left it
    behind, and every other caller of npu-run.sh was refused until the next start, or for its minute."""
    release_console()
    sys.exit(0)


def run():
    signal.signal(signal.SIGTERM, terminated)
    release_console()		# a mark left by a watch that ended while it asked
    beat = None			# the gone latch at the last POLL; None until it is first read
    link = {'ok': None, 'acted': False}
    looks = None
    while True:
        if looks is None or not looks.is_alive():
            looks = threading.Thread(target=looker, name='looks', daemon=True)
            looks.start()
        beat = watch_beat(beat)
        try:
            link = watch_link(link)
            watch_userland(link)
        except Exception as e:
            say('feeder.py: the watch: %s' % e)
        WATCH['holding'] = holding(link)
        time.sleep(POLL)


def running():
    try:
        with open(PIDFILE) as f:
            os.kill(int(f.read().strip()), 0)
        return True
    except (OSError, ValueError):
        return False


def main(what):
    if what == 'run':
        run()
    elif what == 'start':
        if not os.path.exists(NPU_RUN):
            print('no %s: no coprocessor to ask' % NPU_RUN)
        elif running():
            print('watching already')
        else:
            subprocess.run(['/usr/sbin/daemon', '-f', '-p', PIDFILE, sys.executable,
                            os.path.abspath(__file__), 'run'])
            print('watching the coprocessor\'s watchdog feeder every %d s' % EVERY)
    elif what == 'stop':
        if running():
            with open(PIDFILE) as f:
                os.kill(int(f.read().strip()), signal.SIGTERM)
    elif what == 'status':
        try:
            with open(STATE, encoding='utf-8') as f:
                last = json.load(f).get('last')
        except (OSError, ValueError):
            last = None
        line = 'watching the coprocessor\'s watchdog feeder' if running() else 'not watching'
        if last:
            line += '; last put right %s UTC: %s, %s' % (
                time.strftime('%Y-%m-%d %H:%M', time.gmtime(last['at'])), last['found'], last['done'])
        print(line)
    else:
        print('usage: feeder.py start|stop|status|run', file=sys.stderr)
        return 64
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'status'))
    except KeyboardInterrupt:
        sys.exit(0)
