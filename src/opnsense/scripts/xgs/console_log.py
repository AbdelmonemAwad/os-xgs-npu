#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
Everything the coprocessor prints on its console, kept in a file on the host - so a coprocessor that
resets or fails leaves its last words where they can be read (discovery 27, #454).
    console_log.py start     start the logger, detached
    console_log.py stop      stop it
    console_log.py status    one line: logging, or why not
    console_log.py run       the logger itself, what start runs under daemon(8)
WHY. The vendor's board package has xgs-npu-serial-logger.sh for this, "to catch NPU crashes". The
console says nothing while nothing happens - a minute of it read 0 bytes on the XGS 3300 and the XGS
136 - and when the XGS 3300's coprocessor was reset by its own watchdog it began printing its boot
stub, and the host froze tens of milliseconds later (#768): only what was on the disk by then was left
(docs/measurements/console-logger-2026-10-25.md). So every read is written at once, with O_SYNC: a
shell pipeline into dd did not do that - dd waits for a whole block - and kept nothing of a reset.
HOW. It only reads DEV. A byte written there is a keystroke for the coprocessor's boot firmware, whose
menus and countdown take one (npu-run.sh, #559), so nothing here writes to it. It holds the port under
npu-run.sh's console lock (LOCK, flock - lockf(1)'s kind), and gives both up within half a second of
a caller that needs the console - npu-run.sh when the management link does not answer, switch-jumbo.sh
at boot - leaving PAUSE; it takes them again once PAUSE is gone, or older than PAUSE_MAX seconds
(a caller that died). LOG is moved to LOG.1 when it grows past MAX bytes.
Never fails the caller: a host with no console port reports so and exits 0.
"""
import errno
import fcntl
import os
import select
import signal
import subprocess
import sys
import termios
import time
import tty

DEV = '/dev/cuau2'
LOCK = '/var/run/xgs-npu-console.lock'
PAUSE = '/var/run/xgs-console-log.pause'
PIDFILE = '/var/run/xgs-console-log.pid'
LOG = '/var/log/xgs/npu-console.log'
PAUSE_MAX = 300
MAX = 1048576


def paused():
    """PAUSE left by a caller using the console, and not left behind by one that died."""
    try:
        return time.time() - os.stat(PAUSE).st_mtime < PAUSE_MAX
    except OSError:
        return False


# Each line as a BSD syslog line - "Oct 28 16:30:01 coprocessor: what it printed" - so OPNsense's log
# viewer, which shows this file as XGS: Coprocessor Console, fills its Date and Process columns: a
# bare line filled only Line, and the page read three quarters empty (2026-10-28). The time is the
# host's, local as syslog-ng writes the plugin's other logs, when the line's first byte came. No
# severity: the console carries none, and a line without one passes every severity filter, where
# the page opens on Warning. Carriage returns are dropped and empty lines skipped.
def stamp(process):
    now = time.localtime()
    return ('%s %2d %s %s: ' % (time.strftime('%b', now), now.tm_mday, time.strftime('%H:%M:%S', now),
                                process)).encode()


class Lines:
    """The console's bytes cut into stamped lines as they come - a line not yet finished is written
    with what it has, as every read is (#768), and the next read goes on with it."""

    def __init__(self):
        self.start = True

    def feed(self, data):
        out = bytearray()
        parts = data.replace(b'\r', b'').split(b'\n')
        for i, part in enumerate(parts):
            if part:
                if self.start:
                    out += stamp('coprocessor')
                    self.start = False
                out += part
            if i < len(parts) - 1 and not self.start:
                out += b'\n'
                self.start = True
        return bytes(out)


def unfinished():
    """A line the last writer left without its end, which the next line would otherwise join."""
    try:
        with open(LOG, 'rb') as f:
            f.seek(-1, os.SEEK_END)
            return f.read(1) != b'\n'
    except OSError:
        return False


def note(text):
    with open(LOG, 'ab') as f:
        f.write((b'\n' if unfinished() else b'') + stamp('console_log') + text.encode() + b'\n')


def open_log():
    try:
        if os.stat(LOG).st_size > MAX:
            os.replace(LOG, LOG + '.1')
    except OSError:
        pass
    log = os.open(LOG, os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_SYNC, 0o644)
    if unfinished():
        os.write(log, b'\n')
    return log


def session():
    """Hold the console until PAUSE appears, writing what it prints; False if it was not to be had."""
    lock = os.open(LOCK, os.O_RDWR | os.O_CREAT, 0o644)
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        os.close(lock)
        return False
    port = log = None
    try:
        # 115200, raw, no echo - as npu-run.sh sets it, on the port's lock-in device too
        subprocess.run(['/bin/stty', '-f', DEV + '.init', 'speed', '115200', 'raw', '-echo'],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        port = os.open(DEV, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
        # at once: setraw's default waits for what is queued to go out, and a UART that has lost its
        # interrupt sends nothing - the first XGS 3300's logger waited so on 2026-10-26 with the lock
        # held, and every caller of the console was refused (#768)
        tty.setraw(port, termios.TCSANOW)
        attrs = termios.tcgetattr(port)
        attrs[4] = attrs[5] = termios.B115200
        termios.tcsetattr(port, termios.TCSANOW, attrs)
        log = open_log()
        lines = Lines()
        while not paused():
            ready, _, _ = select.select([port], [], [], 0.5)
            if not ready:
                continue
            try:
                data = os.read(port, 4096)
            except OSError as e:
                if e.errno in (errno.EAGAIN, errno.EINTR):
                    continue
                raise
            if data:
                os.write(log, lines.feed(data))
                # at a line's end, so no line is cut between the two files - or at twice the size,
                # for a console that sends no line end at all
                size = os.fstat(log).st_size
                if size > MAX and (lines.start or size > 2 * MAX):
                    os.close(log)
                    log = open_log()
    finally:
        if port is not None:
            os.close(port)
        if log is not None:
            os.close(log)
        os.close(lock)
    return True


def run():
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    note('started')
    while True:
        try:
            held = not paused() and session()
        except Exception as e:
            # a port that will not open, a log that cannot be written: said once a minute at most
            note(str(e))
            time.sleep(60)
            continue
        if not held:
            time.sleep(1)


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
        if not os.path.exists(DEV):
            print('no console port %s: nothing to log' % DEV)
        elif running():
            print('logging already')
        else:
            subprocess.run(['/usr/sbin/daemon', '-f', '-p', PIDFILE, sys.executable,
                            os.path.abspath(__file__), 'run'])
            print('logging the coprocessor\'s console to %s' % LOG)
            subprocess.run(['/usr/bin/logger', '-t', 'xgs-console',
                            'logging the coprocessor\'s console to %s' % LOG])
    elif what == 'stop':
        if running():
            with open(PIDFILE) as f:
                os.kill(int(f.read().strip()), signal.SIGTERM)
    elif what == 'status':
        if running():
            try:
                size = os.stat(LOG).st_size
            except OSError:
                size = 0
            print('logging the coprocessor\'s console to %s, %d bytes%s'
                  % (LOG, size, ', paused for a caller using the console' if paused() else ''))
        else:
            print('not logging the coprocessor\'s console')
    else:
        print('usage: console_log.py start|stop|status|run', file=sys.stderr)
        return 64
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'status'))
    except Exception as e:
        print('console_log.py: %s' % e, file=sys.stderr)
        sys.exit(0)
