#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
Why the host last reset, as its own chipset recorded it.

    reset_reason.py boot     at boot: read the record, log it, keep it, and clear it for the next boot
    reset_reason.py follow   what was kept at this boot moved to kern.boottime as it reads now, after
                             a clock step; nothing is read (65-xgs-report, before the reporter)
    reset_reason.py show     what was kept at this boot, as JSON

WHY. A host that resets without a shutdown leaves no dump and no log line that says why. The XGS 3300
did so on 2026-10-17 (#454): under a minute after its last log line, with the hardware watchdog armed
at 128 s, no machine-check record at the next boot and no crash dump - and nothing on the appliance
could say what had reset it. (No machine-check record could have shown: FreeBSD clears the banks as it
starts and does not log what they held - its sys/x86/x86/mca.c, read on 2026-11-04.) The AMD FCH beside the host's processor keeps that answer: from family
17h on, FCH::PM::S5_RESET_STATUS - power-management register 0xC0, at 0xFED803C0 in memory - holds one
bit per cause of the last reset: the watchdog, a sync flood after an uncorrected error, a thermal
trip, the reset pin, the power button, a software reset through port 0xCF9 and others. Linux prints
it at boot since 2025 ("x86/CPU/AMD: Print the reason for the last reset").

Read by hand on the XGS 3300 on 2026-10-18, after an ordinary reboot, it held 0x00080800: bit 19, the
reset written to port 0xCF9 that a reboot ends in, and bit 11, which the published list does not
name. Nothing in it told of the reset of the day before - several boots had passed since - so it
is read at every boot, or not at all. Written back the same day, bit 19 cleared and bit 11 stayed:
bit 11 is a state of this chipset, not the cause of a reset.

HOW. The start hook 06-xgs-reset-reason runs `boot` early in every boot. The register is read
through /dev/mem, logged under xgs-reset-reason, and kept in /var/db/os-xgs-npu/reset-reason.json
with the boot it belongs to; the problem reporter names it in a report of a restart without a
shutdown. Then the value is written back: the bits are write-one-to-clear and some of them are never
cleared by the hardware, so without it a bit could outlive the reset that set it - Linux's follow-up
fix does the same ("Prevent reset reasons from being retained across reboot").

WHICH BOOT A RECORD IS. The record's boot is kern.boottime, which the problem reporter and the
Overview page compare with their own, within five seconds. kern.boottime moves with the clock,
though: ntpd's first step comes once it has heard its servers, which can be after this hook and before
the reporter's (65-xgs-report), and until 2026-11-06 a step of more than five seconds between the two
lost the reset's cause from its report - and a second `boot` after one read the cleared register and
kept "no cause" over the cause. So this boot's record is also marked in the kernel's environment
(MARK, its read_at), which every boot starts anew from the loader and no clock step moves: `boot`
does not read again where it finds the mark, and `follow`, run by 65-xgs-report just before the
reporter, moves the record's boot to kern.boottime as it then reads. Where kenv refuses the mark,
both go by kern.boottime as before, and `follow` changes nothing.

Only on an AMD processor of family 17h or later (Family= in dmesg.boot), where the register exists.
Never fails the caller.
"""

import json
import os
import re
import subprocess
import sys
import time

ADDRESS = 0xFED80300 + 0xC0		# FCH::PM::S5_RESET_STATUS
MEM = os.environ.get('XGS_RESET_MEM', '/dev/mem')
OFFSET = int(os.environ.get('XGS_RESET_OFFSET', str(ADDRESS)))	# another place in MEM, for the tests
DMESG = os.environ.get('XGS_RESET_DMESG', '/var/run/dmesg.boot')
KEPT = os.environ.get('XGS_RESET_STATE', '/var/db/os-xgs-npu/reset-reason.json')
TAG = 'xgs-reset-reason'
KENV = '/bin/kenv'
MARK = 'xgs.reset_reason.read_at'	# in the kernel's environment: this boot's record (WHICH BOOT A RECORD IS)

# The bits with a published meaning (the AMD list Linux carries); every other bit is printed by its
# number. Bit 19 is what an ordinary reboot leaves here (the XGS 3300, 2026-10-18).
REASONS = {
    0: 'the thermal trip pin',
    1: 'the power button held for four seconds',
    2: 'the shutdown pin',
    4: 'a remote power-off (ASF)',
    9: "the processor's own thermal limit",
    16: 'the system reset pin',
    17: 'a software PCI reset',
    18: 'software wrote 0x4 to port 0xCF9',
    19: 'software wrote 0x6 to port 0xCF9, as a reboot does',
    20: 'software wrote 0xE to port 0xCF9',
    21: 'a sleep-state transition (ACPI)',
    22: 'the keyboard reset pin',
    23: 'the processor shut down, as after a triple fault',
    24: 'the boot failed before its timer ran out',
    25: 'the hardware watchdog expired',
    26: 'a remote reset (ASF)',
    27: 'a sync flood after an uncorrected error',
    29: "the chipset and the processor's power controller failed the warm-reset handshake",
    30: 'a parity error',
    31: 'a sync flood asked for by software',
}


def decode(value):
    """The set bits, lowest first, each with what it means."""
    return [{'bit': b, 'reason': REASONS.get(b, 'no published meaning')}
            for b in range(32) if value >> b & 1]


def describe(value):
    bits = decode(value)
    if not bits:
        return '0x%08x - no cause recorded' % value
    return '0x%08x - %s' % (value, '; '.join('bit %d: %s' % (b['bit'], b['reason']) for b in bits))


def has_register():
    """An AMD processor of family 17h or later: dmesg.boot's Origin= and Family= lines."""
    try:
        with open(DMESG, 'r', encoding='utf-8', errors='replace') as handle:
            text = handle.read()
    except OSError:
        return False
    match = re.search(r'Family=0x([0-9a-fA-F]+)', text)
    return 'AuthenticAMD' in text and bool(match) and int(match.group(1), 16) >= 0x17


def boot_time():
    try:
        raw = subprocess.run(['/sbin/sysctl', '-n', 'kern.boottime'], capture_output=True,
                             text=True, errors='replace', timeout=10).stdout
    except (OSError, subprocess.SubprocessError):
        return 0
    match = re.search(r'sec = (\d+)', raw)
    return int(match.group(1)) if match else 0


def log(message):
    try:
        subprocess.run(['/usr/bin/logger', '-t', TAG, message], timeout=10)
    except (OSError, subprocess.SubprocessError):
        pass


def keep(record):
    try:
        os.makedirs(os.path.dirname(KEPT), exist_ok=True)
        with open(KEPT + '.new', 'w', encoding='utf-8') as handle:
            json.dump(record, handle, indent=1)
        os.replace(KEPT + '.new', KEPT)
    except OSError:
        return False
    return True


def kept_now():
    try:
        with open(KEPT, 'r', encoding='utf-8') as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {}


def kenv_get(name):
    """A variable of the kernel's environment, or None where it is not set or kenv cannot be run."""
    try:
        out = subprocess.run([KENV, '-q', name], capture_output=True, text=True, errors='replace',
                             timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    value = out.stdout.strip()
    return value if out.returncode == 0 and value else None


def kenv_set(name, value):
    try:
        subprocess.run([KENV, '%s=%s' % (name, value)], capture_output=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        pass


def at_boot():
    if not has_register():
        print('no AMD family 17h or later processor here; nothing to read')
        return 0
    # Once a boot: a second read finds the record already cleared, and would keep "nothing" over
    # the cause this boot started with. The mark says this boot read it, whatever the clock did since;
    # without one, kern.boottime within five seconds does (WHICH BOOT A RECORD IS).
    boot = boot_time()
    earlier = kept_now()
    if not isinstance(earlier, dict):
        earlier = {}
    if kenv_get(MARK) is not None or (boot and abs(int(earlier.get('boot') or 0) - boot) <= 5):
        print('read already at this boot: ' + describe(int(earlier.get('value', '0'), 16)))
        return 0
    try:
        fd = os.open(MEM, os.O_RDONLY)
        try:
            value = int.from_bytes(os.pread(fd, 4, OFFSET), 'little')
        finally:
            os.close(fd)
    except OSError as error:
        log('cannot read the reset record: %s' % error.strerror)
        return 0
    # Cleared apart from the read, so that a host that refuses writes to /dev/mem (a raised
    # securelevel) still has its reason read and kept.
    cleared, after = False, None
    if value:
        try:
            fd = os.open(MEM, os.O_RDWR)
            try:
                os.pwrite(fd, value.to_bytes(4, 'little'), OFFSET)
                after = int.from_bytes(os.pread(fd, 4, OFFSET), 'little')
                cleared = True
            finally:
                os.close(fd)
        except OSError:
            pass
    record = {
        'boot': boot, 'read_at': int(time.time()), 'value': '0x%08x' % value,
        'bits': decode(value), 'cleared': cleared,
        'after': None if after is None else '0x%08x' % after,
    }
    if keep(record):
        kenv_set(MARK, record['read_at'])
    message = 'last reset: ' + describe(value)
    if value and not cleared:
        message += ' (could not be cleared)'
    elif after:
        message += ' (0x%08x left after clearing)' % after
    log(message)
    print(message)
    return 0


def follow():
    """This boot's record - the mark names it - moved to kern.boottime as it reads now, so that the
    reporter, run next, finds it after a clock step. Nothing is read from the chipset."""
    record = kept_now()
    boot = boot_time()
    if not isinstance(record, dict) or not boot or kenv_get(MARK) != str(record.get('read_at')):
        return 0
    moved = boot - int(record.get('boot') or 0)
    if moved:
        record['boot'] = boot
        keep(record)
        # A step the five seconds would have taken in is not worth a line; one that would not is.
        if abs(moved) > 5:
            log('the clock moved %+d s since the reset record was read; kept with the boot\'s time as it reads now'
                % moved)
    return 0


def show():
    try:
        with open(KEPT, 'r', encoding='utf-8') as handle:
            print(handle.read().strip())
    except OSError:
        print('{}')
    return 0


def main(argv):
    command = argv[1] if len(argv) > 1 else 'show'
    if command == 'boot':
        return at_boot()
    if command == 'follow':
        return follow()
    if command == 'show':
        return show()
    print('usage: reset_reason.py boot|follow|show', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
