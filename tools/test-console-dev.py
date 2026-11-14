#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""scripts/xgs/console-dev.sh away from the appliance: which device it gives as the coprocessor's
console - the host's UART at I/O port 0x3e8 - from the kernel's devices as devinfo -rv lists them, or
from boot messages, in the forms FreeBSD 15.1 prints them. uart2 on the XGS 136 r1 and the XGS 3300;
uart4 on an XGS 107, whose chipset's own two UARTs are numbered first (#748's support bundle, its uart
lines as they are there); another unit on an XGS 136 r2, whose uart2 is the chipset's UART at
0xfedc9000 (#601) - the r2's tree here is made up from what #601 says of it, uart3 for the port at
0x3e8. And /dev/cuau2 where nothing better is known (exit 1) or no UART is at that address (exit 2),
and as before whatever the kernel says with the knob kenv xgs.console.by_address=0. The kernel is a
stand-in devinfo, kenv a stand-in, and the saved texts reach the script through XGS_CONSOLE_DEVINFO,
as a support bundle's dmesg.txt can be given to it by hand. /bin/sh, so on Linux or FreeBSD.

    python3 tools/test-console-dev.py
"""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'console-dev.sh')
PANEL = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'panel', 'panel.sh')
POSIX = os.path.exists('/bin/sh') and sys.platform != 'win32'

# devinfo -rv as FreeBSD 15.1 lays it out: a device's line, the kinds of resource it holds four places
# further in, their ranges four more, its children two places further in than itself.
HEAD = r'''nexus0
  cryptosoft0 <software crypto>
  ram0 <System RAM>
      I/O memory addresses:
          0x0-0x9ffff
          0x100000-0x7fffffff
  apic0 <APIC resources>
      I/O memory addresses:
          0xfec00000-0xfec0001f
  acpi0 <ALASKA A M I >
      Interrupt request lines:
          0x9
      I/O ports:
          0x40b
          0x4d0-0x4d1
      I/O memory addresses:
          0xfedc0000-0xfedc0fff
    cpu0 <ACPI CPU> pnpinfo _HID=none _UID=0 _CID=none at handle=\_PR_.C000
    pcib0 <ACPI Host-PCI bridge> pnpinfo _HID=PNP0A08 _UID=0 _CID=PNP0A03 at handle=\_SB_.PCI0
        I/O ports:
            0xcf8-0xcff
      pci0 <ACPI PCI bus>
          PCI domain 0 bus numbers:
              0
        hostb0 <Host to PCI bridge> pnpinfo vendor=0x1022 device=0x15d0 class=0x060000 at slot=0 function=0
'''


def legacy(unit, irq, start, uid, handle):
    """A UART on the legacy COM port at start, as acpi0's child."""
    return ('    uart%d <16550 or compatible> pnpinfo _HID=PNP0501 _UID=%d _CID=none'
            ' at handle=%s\n'
            '        Interrupt request lines:\n            %#x\n'
            '        I/O ports:\n            %#x-%#x\n' % (unit, uid, handle, irq, start, start + 7))


def chipset(unit, irq, *ranges):
    """The chipset's own UART, memory-mapped (AMDI0020), as acpi0's child."""
    return ('    uart%d <16x50 with 256 byte FIFO> pnpinfo _HID=AMDI0020 _UID=%d _CID=none'
            ' at handle=\\_SB_.FUR%d\n'
            '        Interrupt request lines:\n            %#x\n'
            '        I/O memory addresses:\n' % (unit, unit, unit, irq)
            + ''.join('            %#x-%#x\n' % (r, r + 0xfff) for r in ranges))


TAIL = '''    acpi_sysresource0 <System Resource> pnpinfo _HID=PNP0C02 _UID=0 _CID=none at handle=\\_SB_.SIO1
        I/O ports:
            0x2e-0x2f
'''

COM1, COM3 = r'\_SB_.PCI0.SBRG.UAR1', r'\_SB_.PCI0.SBRG.UAR3'
R1 = HEAD + legacy(0, 4, 0x3f8, 1, COM1) + legacy(2, 3, 0x3e8, 3, COM3) + TAIL
X3300 = (HEAD + legacy(0, 4, 0x3f8, 1, r'\_SB_.PCI0.UAR1') + legacy(1, 4, 0x2f8, 2, r'\_SB_.PCI0.UAR2')
         + legacy(2, 5, 0x3e8, 3, r'\_SB_.PCI0.UAR3') + TAIL)
X107 = (HEAD + chipset(2, 10, 0xfedc6000) + chipset(3, 11, 0xfedc8000) + legacy(0, 4, 0x3f8, 1, COM1)
        + legacy(4, 5, 0x3e8, 3, COM3) + TAIL)
R2 = (HEAD + chipset(2, 3, 0xfedc9000, 0xfedc7000) + legacy(0, 4, 0x3f8, 1, COM1)
      + legacy(3, 3, 0x3e8, 3, COM3) + TAIL)
# No UART at 0x3e8: the chipset's own at uart2, and the port's ACPI device there with no driver on it
NONE = (HEAD + chipset(2, 3, 0xfedc9000, 0xfedc7000) + legacy(0, 4, 0x3f8, 1, COM1)
        + '    unknown pnpinfo _HID=PNP0501 _UID=3 _CID=none at handle=\\_SB_.PCI0.SBRG.UAR3\n'
        '        I/O ports:\n            0x3e8-0x3ef\n' + TAIL)

# The XGS 107's boot messages as #748's bundle keeps them (dmesg.txt): two boots, the same UARTs.
BOOT_107 = '''[1] acpi_button0: <Power Button> on acpi0
[1] uart2: <16x50 with 256 byte FIFO> iomem 0xfedc6000-0xfedc7fff irq 10 on acpi0
[1] uart3: <16x50 with 256 byte FIFO> iomem 0xfedc8000-0xfedc9fff irq 11 on acpi0
[1] uart: ns8250: UART FCR is broken (0x1)
[1] uart0: <16550 or compatible> port 0x3f8-0x3ff irq 4 flags 0x10 on acpi0
[1] uart0: console (38400,n,8,1)
[1] uart: ns8250: UART FCR is broken (0x1)
[1] uart4: <16550 or compatible> port 0x3e8-0x3ef irq 5 on acpi0
[1] hwpstate0: <Cool`n'Quiet 2.0> on cpu0
'''
DMESG_107 = ('---<<BOOT>>---\nCopyright (c) 1992-2025 The FreeBSD Project.\n' + BOOT_107
             + '[2780] uhub0: detached\n[2780] npuep0: shutdown: bus mastering disabled\n'
             + '---<<BOOT>>---\nCopyright (c) 1992-2025 The FreeBSD Project.\n' + BOOT_107)
# An XGS 136 r2's as #601 tells of them - uart2 the chipset's, detached and attached again - with the
# port at 0x3e8 taken as uart3.
DMESG_R2 = '''---<<BOOT>>---
[1] uart2: <16x50 with 256 byte FIFO> iomem 0xfedc9000-0xfedc9fff,0xfedc7000-0xfedc7fff irq 3 on acpi0
[1] uart0: <16550 or compatible> port 0x3f8-0x3ff irq 4 flags 0x10 on acpi0
[1] uart0: console (115200,n,8,1)
[1] uart3: <16550 or compatible> port 0x3e8-0x3ef irq 3 on acpi0
[2592] uart2: detached
[2592] uart2: <16x50 with 256 byte FIFO> iomem 0xfedc9000-0xfedc9fff,0xfedc7000-0xfedc7fff irq 3 on acpi0
'''


@unittest.skipUnless(POSIX, 'console-dev.sh runs under /bin/sh')
class ConsoleDev(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def file(self, name, text, mode=0o644):
        path = os.path.join(self.dir, name)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        os.chmod(path, mode)
        return path

    def run_script(self, saved=None, devinfo=None, dmesg=None, knob=None, verbose=True, args=()):
        """console-dev.sh [-v]: with saved, XGS_CONSOLE_DEVINFO names a file of that text (None: not set);
        otherwise devinfo prints devinfo (None: there is no devinfo), dmesg is /var/run/dmesg.boot (None:
        there is none), and kenv -q xgs.console.by_address prints knob (None: not set); args after -v (an
        address). Its lines and exit."""
        env = {name: value for name, value in os.environ.items()
               if name not in ('XGS_CONSOLE_DEVINFO', 'KENV', 'DEVINFO', 'DMESG_BOOT')}
        if saved is not None:
            env['XGS_CONSOLE_DEVINFO'] = self.file('saved.txt', saved)
        env['DEVINFO'] = (os.path.join(self.dir, 'no-devinfo') if devinfo is None else
                          self.file('devinfo', '#!/bin/sh\n[ "$1" = -rv ] || exit 1\ncat "%s"\n'
                                    % self.file('tree.txt', devinfo), 0o755))
        env['DMESG_BOOT'] = (os.path.join(self.dir, 'no-dmesg.boot') if dmesg is None else
                             self.file('dmesg.boot', dmesg))
        env['KENV'] = self.file('kenv', '#!/bin/sh\n[ "$1 $2" = "-q xgs.console.by_address" ] || exit 1\n'
                                + ('exit 1\n' if knob is None else 'echo %s\n' % knob), 0o755)
        done = subprocess.run(['/bin/sh', SCRIPT] + (['-v'] if verbose else []) + list(args), env=env,
                              capture_output=True, text=True, errors='replace', timeout=30)
        return done.stdout.splitlines(), done.returncode

    def test_the_xgs_136_r1_and_the_xgs_3300_keep_uart2(self):
        for tree in (R1, X3300):
            lines, rc = self.run_script(saved=tree)
            self.assertEqual((lines[0], rc), ('/dev/cuau2', 0), lines)
            self.assertTrue(lines[1].startswith('uart2 at I/O port 0x3e8-0x3ef ('), lines)

    def test_an_xgs_107_s_console_is_uart4(self):
        """#748's bundle: uart2 and uart3 the chipset's own, memory-mapped; the port at 0x3e8 uart4."""
        for text in (X107, DMESG_107):
            lines, rc = self.run_script(saved=text)
            self.assertEqual((lines[0], rc), ('/dev/cuau4', 0), lines)
            self.assertTrue(lines[1].startswith('uart4 at I/O port 0x3e8-0x3ef ('), lines)

    def test_an_xgs_136_r2_s_console_is_not_its_uart2(self):
        """#601: uart2 the chipset's UART at 0xfedc9000, detached and attached again in its log."""
        for text in (R2, DMESG_R2):
            lines, rc = self.run_script(saved=text)
            self.assertEqual((lines[0], rc), ('/dev/cuau3', 0), lines)

    def test_no_uart_at_0x3e8_is_cuau2_and_said_to_be_no_console_port(self):
        """The port's ACPI device with no driver on it is not a UART; a whole tree without one at 0x3e8
        says so, with what uart2 is."""
        lines, rc = self.run_script(saved=NONE)
        self.assertEqual((lines[0], rc), ('/dev/cuau2', 2), lines)
        self.assertIn('uart2 at I/O memory 0xfedc9000-0xfedc9fff', lines[1])
        self.assertIn("/dev/cuau2 is not the console's port", lines[1])
        # boot messages that name uart2 as the chipset's, and no UART at 0x3e8
        boot = '\n'.join(line for line in DMESG_R2.splitlines() if 'uart3' not in line) + '\n'
        lines, rc = self.run_script(saved=boot)
        self.assertEqual((lines[0], rc), ('/dev/cuau2', 2), lines)
        # a tree with no UART at all: no uart2 either
        lines, rc = self.run_script(saved=HEAD + TAIL)
        self.assertEqual((lines[0], rc), ('/dev/cuau2', 2), lines)
        self.assertIn('no uart2', lines[1])

    def test_nothing_known_is_cuau2_as_before(self):
        """Nothing read, or a part of a list that names neither a UART at 0x3e8 nor uart2: exit 1, which
        leaves npu-run.sh attaching the UART again as before."""
        uart0 = legacy(0, 4, 0x3f8, 1, COM1)
        boot_without_uart2 = '---<<BOOT>>---\n' + BOOT_107.replace('uart2', 'uart5').replace('uart4', 'uart6')
        boot_without_uart2 = boot_without_uart2.replace('port 0x3e8', 'port 0x2e8')
        for text in ('', uart0, boot_without_uart2):
            lines, rc = self.run_script(saved=text)
            self.assertEqual((lines[0], rc), ('/dev/cuau2', 1), (text, lines))
            self.assertIn('as before', lines[1])
        # the last boot of the messages counts: one that ends before its UARTs are found says nothing
        lines, rc = self.run_script(saved=DMESG_107 + '---<<BOOT>>---\nCopyright (c) 1992-2025\n')
        self.assertEqual((lines[0], rc), ('/dev/cuau2', 1), lines)

    def test_a_saved_file_that_is_not_there_is_cuau2_as_before(self):
        env = dict(os.environ, XGS_CONSOLE_DEVINFO=os.path.join(self.dir, 'not-there'),
                   KENV=os.path.join(self.dir, 'no-kenv'), DEVINFO=os.path.join(self.dir, 'no-devinfo'))
        done = subprocess.run(['/bin/sh', SCRIPT, '-v'], env=env, capture_output=True, text=True,
                              errors='replace', timeout=30)
        self.assertEqual((done.stdout.splitlines()[0], done.returncode), ('/dev/cuau2', 1), done.stdout)

    def test_the_kernel_is_asked_through_devinfo_then_its_boot_messages(self):
        lines, rc = self.run_script(devinfo=X107)
        self.assertEqual((lines, rc), (['/dev/cuau4', 'uart4 at I/O port 0x3e8-0x3ef (devinfo -rv)'], 0))
        # devinfo's whole tree is the answer: no UART at 0x3e8 there is 2, whatever the messages say
        lines, rc = self.run_script(devinfo=NONE, dmesg=DMESG_107)
        self.assertEqual((lines[0], rc), ('/dev/cuau2', 2), lines)
        # without devinfo, this boot's messages
        lines, rc = self.run_script(dmesg=DMESG_107)
        self.assertEqual((lines[0], rc), ('/dev/cuau4', 0), lines)
        self.assertTrue(lines[1].endswith('dmesg.boot)'), lines)
        # without either
        lines, rc = self.run_script()
        self.assertEqual((lines[0], rc), ('/dev/cuau2', 1), lines)

    def test_the_knob_gives_cuau2_as_before(self):
        """kenv xgs.console.by_address=0: /dev/cuau2 and exit 1 - npu-run.sh attaching the UART again as
        before - whatever devinfo says; any other value finds the console by its address."""
        lines, rc = self.run_script(devinfo=X107, knob='0')
        self.assertEqual((lines[0], rc), ('/dev/cuau2', 1), lines)
        self.assertIn('xgs.console.by_address=0', lines[1])
        lines, rc = self.run_script(devinfo=X107, knob='1')
        self.assertEqual((lines[0], rc), ('/dev/cuau4', 0), lines)

    def test_without_v_one_line(self):
        lines, rc = self.run_script(saved=X107, verbose=False)
        self.assertEqual((lines, rc), (['/dev/cuau4'], 0))

    def test_hex_in_capitals_and_a_port_found_by_hints(self):
        lines, rc = self.run_script(saved=R1.replace('0x3e8-0x3ef', '0x3E8-0x3EF'))
        self.assertEqual((lines[0], rc), ('/dev/cuau2', 0), lines)
        lines, rc = self.run_script(saved='uart5: <16550 or compatible> at port 0x3e8-0x3ef irq 3 on isa0\n')
        self.assertEqual((lines[0], rc), ('/dev/cuau5', 0), lines)
        # 0x3e80 is not 0x3e8
        lines, rc = self.run_script(saved='uart5: <16550 or compatible> port 0x3e80-0x3e87 irq 3 on acpi0\n')
        self.assertEqual((lines[0], rc), ('/dev/cuau2', 1), lines)

    def test_another_address_the_front_panel_s_at_0x2f8(self):
        """console-dev.sh 0x2f8: the front panel's UART (panel/panel.sh), found by its address as the
        console is - uart1 on the XGS 3300, whatever the unit elsewhere - with /dev/cuau1, the unit it had
        where this has run, as its fallback, and exit 2 with what uart1 is when none is at 0x2f8."""
        lines, rc = self.run_script(saved=X3300, args=['0x2f8'])
        self.assertEqual((lines, rc), (['/dev/cuau1', 'uart1 at I/O port 0x2f8-0x2ff (%s)'
                                        % os.path.join(self.dir, 'saved.txt')], 0))
        moved = HEAD + chipset(1, 10, 0xfedc6000) + legacy(5, 3, 0x2f8, 2, r'\_SB_.PCI0.UAR2') + TAIL
        for text in (moved, moved.replace('0x2f8', '0x2F8')):
            lines, rc = self.run_script(saved=text, args=['0X2F8'])
            self.assertEqual((lines[0], rc), ('/dev/cuau5', 0), lines)
        lines, rc = self.run_script(saved='uart3: <16550 or compatible> port 0x2f8-0x2ff irq 3 on acpi0\n',
                                    args=['0x2f8'])
        self.assertEqual((lines[0], rc), ('/dev/cuau3', 0), lines)
        # an XGS 107: no UART at 0x2f8, and uart1 nowhere - not /dev/cuau1's port
        lines, rc = self.run_script(saved=X107, args=['0x2f8'])
        self.assertEqual((lines[0], rc), ('/dev/cuau1', 2), lines)
        self.assertIn('no UART at I/O port 0x2f8', lines[1])
        self.assertIn("no uart1: /dev/cuau1 is not the console's port", lines[1])
        lines, rc = self.run_script(saved=HEAD + chipset(1, 10, 0xfedc6000) + TAIL, args=['0x2f8'])
        self.assertEqual((lines[0], rc), ('/dev/cuau1', 2), lines)
        self.assertIn('uart1 at I/O memory 0xfedc6000-0xfedc6fff', lines[1])
        # the console's address given is the default's answer; nothing known is the fallback as before
        self.assertEqual(self.run_script(saved=X107, args=['0x3e8']), self.run_script(saved=X107))
        lines, rc = self.run_script(args=['0x2f8'])
        self.assertEqual((lines[0], rc), ('/dev/cuau1', 1), lines)
        lines, rc = self.run_script(devinfo=X107, knob='0', args=['0x2f8'])
        self.assertEqual((lines[0], rc), ('/dev/cuau1', 1), lines)
        lines, rc = self.run_script(saved=X3300, verbose=False, args=['0x3f8'])
        self.assertEqual((lines, rc), (['/dev/cuau0'], 0))

    def test_an_address_that_is_not_a_legacy_com_port_is_a_usage_error(self):
        for bad in ('0x3e0', '1000', 'uart2', '0x3e8x', ''):
            lines, rc = self.run_script(saved=X3300, args=[bad])
            self.assertEqual((lines, rc), ([], 64), bad)

    def panel(self, rc, port='/dev/cuau9'):
        """panel.sh clear with console-dev.sh 0x2f8 answering port and exit rc; PORT not set. Its stderr
        and exit, and what console-dev.sh was asked."""
        asked = os.path.join(self.dir, 'asked')
        stub = self.file('console-dev.sh', 'echo "$*" > "%s"\necho %s\nexit %d\n' % (asked, port, rc))
        env = {name: value for name, value in os.environ.items() if name != 'PORT'}
        env['CONSOLE_DEV'] = stub
        done = subprocess.run(['/bin/sh', PANEL, 'clear'], env=env, capture_output=True, text=True,
                              errors='replace', timeout=30)
        with open(asked, encoding='utf-8') as f:
            return done.stderr, done.returncode, f.read().strip()

    def test_the_panel_finds_its_port_at_0x2f8(self):
        """panel.sh asks console-dev.sh for 0x2f8, where /dev/cuau1 was written in: the unit found is the
        one opened (here one that is not there, so it stops at that); none at 0x2f8 opens nothing."""
        err, rc, asked = self.panel(0)
        self.assertEqual((rc, asked), (1, '0x2f8'))
        self.assertIn('/dev/cuau9 is not there', err)
        err, rc, asked = self.panel(1, '/dev/cuau1-not-here')
        self.assertIn('/dev/cuau1-not-here is not there', err)
        err, rc, asked = self.panel(2, '/dev/cuau1')
        self.assertEqual(rc, 1)
        self.assertIn('no UART at I/O port 0x2f8 for the front panel; nothing written', err)
        self.assertNotIn('is not there', err)


if __name__ == '__main__':
    unittest.main(verbosity=1)
