#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""Count down on the console and wait for a key - the one question 30-xgs-assign asks.

    countdown.py        exit 0 when the time runs out, 1 when a key is pressed

Thirty seconds, or XGS_ASSIGN_SECONDS. A key pressed before the question appears is not an answer
to it, so the console's input is flushed first. With no console to open there is nobody to press
a key, and the time is taken as run out.
"""
import os
import select
import sys
import termios
import time

SECONDS = int(os.environ.get('XGS_ASSIGN_SECONDS', '30'))
ASK = 'XGS: assigning the front ports by the names on the chassis in %2d s - press any key to assign them by hand '


def main():
    try:
        fd = os.open('/dev/console', os.O_RDWR | os.O_NOCTTY)
    except OSError:
        return 0
    saved = termios.tcgetattr(fd)
    quiet = termios.tcgetattr(fd)
    quiet[3] &= ~(termios.ICANON | termios.ECHO)
    quiet[6][termios.VMIN] = 0
    quiet[6][termios.VTIME] = 0
    pressed = False
    try:
        termios.tcsetattr(fd, termios.TCSANOW, quiet)
        termios.tcflush(fd, termios.TCIFLUSH)
        # Every number is shown, each until its own second ends: computed from the clock, a second
        # that ran long under a booting machine's load skipped one (7, then 5, seen under QEMU).
        start = time.monotonic()
        for left in range(SECONDS, 0, -1):
            os.write(fd, ('\r' + ASK % left).encode())
            wait = max(0.0, start + (SECONDS - left + 1) - time.monotonic())
            ready, _, _ = select.select([fd], [], [], wait)
            if ready and os.read(fd, 64):
                pressed = True
                break
    finally:
        termios.tcsetattr(fd, termios.TCSANOW, saved)
        if pressed:
            os.write(fd, b'\r\nXGS: stopped - assign the ports by hand: console menu option 1, or the GUI\r\n')
        else:
            os.write(fd, b'\r\nXGS: assigning the front ports\r\n')
        os.close(fd)
    return 1 if pressed else 0


if __name__ == '__main__':
    sys.exit(main())
