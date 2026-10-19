#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
The problem reporter, away from the appliance: src/opnsense/scripts/xgs/reporter.py run against a
made-up machine in a temporary directory - its logs, its config.xml, its commands - and a relay that
is a function. Every line of log and every name here is invented; nothing comes from an appliance.

    python3 tools/test-reporter.py
"""

import base64
import importlib.util
import json
import os
import shutil
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'reporter.py')

SERIAL = 'XYZ0AMDA0202-0004QQ12345678'
UUID = '0a1b2c3d-1111-2222-3333-444455556666'
BOOT = int(time.time()) - 7200
KERNEL = 'FreeBSD 15.1-RELEASE-p4 stable/26.7-n284159-abbbe67a45e7 SMP'

CONFIG = """<?xml version="1.0"?>
<opnsense>
  <system><hostname>gatekeeper</hostname><domain>example.org</domain></system>
  <OPNsense><XGS><reporting><mode>%s</mode><email>owner@example.org</email></reporting></XGS></OPNsense>
</opnsense>
"""


def load_reporter(root, state):
    os.environ['XGS_REPORT_ROOT'] = root
    os.environ['XGS_REPORT_STATE'] = state
    os.environ['XGS_REPORT_CONFIG'] = '/conf/config.xml'
    spec = importlib.util.spec_from_file_location('reporter', SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Machine:
    """The commands reporter.py runs, answered the way the XGS 3300 answers them."""

    def __init__(self):
        self.ready = '1'
        self.attached = True
        self.stamp = KERNEL

    def run(self, argv, timeout=10):
        name = argv[0]
        if name == '/sbin/sysctl':
            return {
                'kern.boottime': '{ sec = %d, usec = 0 } Sat Oct 17 08:52:00 2026' % BOOT,
                'kern.version': KERNEL + '\n    root@build:/usr/obj/kernel\n',
                'dev.octep.0.%desc': 'Cavium OCTEON TX CN83XX PCIe endpoint (Sophos XGS NPU)' if self.attached else '',
                'dev.octep.0.ready': self.ready,
                'dev.octep.0.barmap_version': '2',
                'kern.shutdown.dumpdevname': 'ada0p3',
            }.get(argv[-1], '')
        if name == '/bin/kenv':
            return {
                'smbios.system.serial': SERIAL, 'smbios.planar.serial': SERIAL,
                'smbios.system.uuid': UUID, 'smbios.system.maker': 'Sophos',
                'smbios.system.product': 'XGS', 'smbios.system.version': '3300r1',
                'smbios.planar.version': '09', 'smbios.processor.version': 'AMD Ryzen Embedded V1780B   ',
                'smbios.memory.enabled': '16777216', 'smbios.bios.version': 'V110',
                'smbios.bios.reldate': '02/22/2021', 'smbios.chassis.serial': 'Default string',
            }.get(argv[-1], '')
        if name == '/bin/sh' and argv[-1].endswith('board.sh'):
            return '202 octeontx\n'
        if argv[-1].endswith('portmap.py'):
            return json.dumps({'module': {'slots': {'A': {'assembly.partno': 'AMDA0207-0001',
                                                            'assembly.revision': '02'}}}})
        if name.endswith('opnsense-version'):
            return '26.7.7\n'
        if name.endswith('freebsd-version'):
            return '15.1-RELEASE-p4\n'
        if name == '/usr/bin/last':
            return 'shutdown time                              Thu Jan  1 2026 06:57\n'
        if name == '/usr/sbin/pciconf':
            return ('igb0@pci0:2:0:0:\tclass=0x020000 rev=0x03 hdr=0x00 vendor=0x8086 device=0x1539 subvendor=0x8086 subdevice=0x0000\n'
                    "    vendor     = 'Intel Corporation'\n"
                    "    device     = 'I211 Gigabit Network Connection'\n"
                    'octep0@pci0:1:0:0:\tclass=0x0b8000 rev=0x01 hdr=0x00 vendor=0x177d device=0xa300 subvendor=0x177d subdevice=0xa300\n'
                    "    vendor     = 'Cavium, Inc.'\n"
                    "    device     = 'OCTEON TX CN83XX'\n"
                    'xhci0@pci0:3:0:3:\tclass=0x0c0330 rev=0x00 hdr=0x00 vendor=0x1022 device=0x15e0 subvendor=0x1022 subdevice=0x7914\n'
                    "    device     = 'USB controller'\n")
        if name == '/sbin/dmesg':
            return '---<<BOOT>>---\nCopyright (c) 1992-2025 The FreeBSD Project.\n'
        return ''


class Relay:
    """The relay, as a function: issues by fingerprint, comments on a repeat."""

    def __init__(self):
        self.received = []
        self.issues = {}
        self.down = False

    def post(self, document):
        if self.down:
            return {'status': 'failed', 'message': 'the relay could not be reached: test'}
        self.received.append(document)
        key = (document['repo'], document['fingerprint'])
        if key not in self.issues:
            self.issues[key] = len(self.issues) + 1
        number = self.issues[key]
        return {'status': 'ok', 'issue': number,
                'url': 'https://github.com/%s/%s/issues/%d' % (document['owner'], document['repo'], number)}


class ReporterTest(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='reporter-')
        self.root = os.path.join(self.tmp, 'root')
        self.state = os.path.join(self.tmp, 'state')
        for d in ('conf', 'var/log/system', 'var/log/xgs', 'var/log/configd', 'var/lib/php/tmp',
                  'var/crash', 'var/db/os-xgs-npu/update', 'var/db/fanctl', 'boot/modules',
                  'usr/local/opnsense/scripts/xgs', 'usr/local/opnsense/scripts/fanctl',
                  'usr/local/share/os-xgs-npu', 'usr/local/share/os-fanctl'):
            os.makedirs(os.path.join(self.root, d), exist_ok=True)
        self.write('/conf/config.xml', CONFIG % 'automatic')
        self.write('/boot/modules/octep.ko.kernel', KERNEL + '\n')
        self.write('/usr/local/share/os-xgs-npu/installed.json',
                   json.dumps({'commit': '016abe9a1b2c3d4e', 'date': '2026-10-17', 'source': 'update'}))
        self.write('/usr/local/share/os-fanctl/installed.json', json.dumps({'commit': '1a7bd4c99887766'}))
        self.write('/var/db/os-xgs-npu/npu-platform.txt',
                   'npu0.assembly.partno=AMDA0202-0004\nnpu0.assembly.revision=09\n'
                   'npu0.assembly.serial=NPU99887766\nnpu0.eth.mac=de:ad:be:ef:00:01\n'
                   'npu0.assembly.vendor=LAA\nnpu0.slotA.assembly.vendor=AIA\n'
                   'npu0.device0=CN8365\nnpu0.device1=88E6193X\n')
        self.log = '/var/log/system/system_20261017.log'
        self.write(self.log, self.line(13, 'dhcp6c', 'Sending Solicit on oxp3'))
        self.reporter = load_reporter(self.root, self.state)
        self.machine = Machine()
        self.relay = Relay()
        self.reporter.run = self.machine.run
        self.reporter.post = self.relay.post

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def write(self, name, text, mode='w'):
        with open(os.path.join(self.root, name.lstrip('/')), mode, encoding='utf-8') as handle:
            handle.write(text)

    @staticmethod
    def line(pri, program, message, when='2026-10-17T08:40:00+00:00'):
        return '<%d>1 %s gatekeeper.example.org %s 123 - [meta sequenceId="1"] %s\n' % (
            pri, when, program, message)

    def collect(self):
        return self.reporter.collect()

    # ----------------------------------------------------------------------------- crash dumps

    def dump(self, number, panic, frames):
        """A crash dump as savecore leaves it: info.N, and textdump.tar.N with ddb.txt and msgbuf.txt."""
        import io
        import tarfile
        self.write('/var/crash/info.%d' % number,
                   'Dump header from device: /dev/ada0p3\n  Panic String: %s\n' % panic)
        trace = ''.join('%s() at %s+0x1%d/frame 0xfffffe00d3990b%02d\n' % (f, f, i, i)
                        for i, f in enumerate(frames))
        with tarfile.open(os.path.join(self.root, 'var/crash/textdump.tar.%d' % number), 'w') as tar:
            for name, body in (('ddb.txt', 'db:0:kdb.enter.default>  bt\n' + trace),
                               ('msgbuf.txt', 'panic: %s\n' % panic)):
                data = body.encode('utf-8')
                member = tarfile.TarInfo(name)
                member.size = len(data)
                tar.addfile(member, io.BytesIO(data))

    def test_crash_dumps_are_told_apart_by_where_they_happened(self):
        common = ['kdb_enter', 'panic', 'trap_pfault', 'calltrap']
        self.dump(0, 'page fault', common + ['key_allocsa', 'octep_ipsec_rx', 'octep_dp_oq_service'])
        self.dump(1, 'page fault', common + ['pf_find_state_all', 'npuep_pf_state_read', 'npuflow_sweep'])
        self.dump(2, 'sleeping thread holds npurpc',
                  ['kdb_enter', 'panic', 'propagate_priority', 'turnstile_wait', '__mtx_lock_sleep',
                   'npurpc_sysctl_command', 'sysctl_root_handler_locked'])
        os.symlink('info.2', os.path.join(self.root, 'var/crash/info.last'))
        problems = self.reporter.from_crash([])
        # three dumps, three problems: info.last is a link to the newest, not a fourth dump
        self.assertEqual(len(problems), 3)
        prints = {self.reporter.fingerprint(p['part'], p['kind'], p['signature']) for p in problems}
        # two page faults in different places are two problems, not one
        self.assertEqual(len(prints), 3)
        self.assertIn('key_allocsa', problems[0]['signature'])
        self.assertNotIn('trap_pfault', problems[0]['signature'])
        # an npuep module frame named npurpc_ is the driver's own
        self.assertIn("names the coprocessor's driver", problems[2]['summary'])
        # a backtrace with no driver frame says so
        self.dump(3, 'general protection fault', common + ['strlen', 'zfs_freebsd_fplookup_symlink', 'namei'])
        later = self.reporter.from_crash(['info.%d@%d' % (n, int(os.path.getmtime(
            os.path.join(self.root, 'var/crash/info.%d' % n)))) for n in range(3)])
        self.assertEqual(len(later), 1)
        self.assertIn('does not name', later[0]['summary'])

    # ---------------------------------------------------------------------------------- cleaning

    def test_scrubber_takes_out_what_names_the_machine(self):
        scrub = self.reporter.Scrubber()
        text = scrub('serial %s uuid %s on gatekeeper.example.org from 192.0.2.51/24 '
                     'mac de:ad:be:ef:00:02 v6 2001:db8::5e00:53ff:fe00:1/64 mail owner@example.org '
                     'npu NPU99887766 at 08:45:56 OPNsense 26.7.7 kept 127.0.0.1 169.254.254.2 '
                     'fe80::1%%oxp3 SSH: SHA256 Q2xhdWRlIG1hZGUgdGhpcyBmaW5nZXJwcmludCB1cA '
                     'HTTPS: SHA256 01 23 45 67 89 AB CD EF 01 23 45 67 89 AB CD EF\n'
                     'FE DC BA 98 76 54 32 10 FE DC BA 98 76 54 32 10' % (SERIAL, UUID))
        for secret in (SERIAL, UUID, 'gatekeeper', 'example.org', '192.0.2.51', 'de:ad:be:ef:00:02',
                       '2001:db8::5e00', 'owner@', 'NPU99887766', 'Q2xhdWRl', '01 23 45', 'FE DC BA'):
            self.assertNotIn(secret, text)
        for kept in ('08:45:56', '26.7.7', '127.0.0.1', '169.254.254.2', 'fe80::1', 'OPNsense'):
            self.assertIn(kept, text)

    # ---------------------------------------------------------------------------------- the logs

    def test_first_pass_reads_no_history_then_new_lines_are_found(self):
        self.write(self.log, self.line(11, 'fanctl', 'restore failed before install'), 'a')
        self.assertEqual(self.collect()['found'], 0)
        self.write(self.log, ''.join([
            self.line(11, 'fanctl', 'restore failed: the chip did not answer at 192.0.2.51'),
            self.line(13, 'kernel', '[222] octep0: giu: timed out waiting for ring 3 (status 0x0000dead)'),
            self.line(13, 'kernel', '<6>[125] oxp12: promiscuous on'),
            self.line(13, 'kernel', '<6>[126] em0: link state changed to DOWN'),
            self.line(27, 'octep-follow', 'the build failed; /boot/modules/octep.ko is untouched'),
            self.line(11, 'configd.py', "Script action failed with Command '/usr/local/opnsense/scripts/fanctl/x.py' "
                                        'returned non-zero exit status 1'),
            self.line(11, 'sshd', 'error: something that is not ours'),
            self.line(14, 'fanctl', 'an ordinary informational line'),
        ]), 'a')
        result = self.collect()
        self.assertEqual(result['found'], 4)
        rows = self.reporter.listing()['rows']
        parts = sorted((r['part'], r['kind']) for r in rows)
        self.assertEqual(parts, [('os-fanctl', 'log-error'), ('os-fanctl', 'log-error'),
                                 ('os-xgs-npu', 'driver'), ('os-xgs-npu', 'log-error')])
        self.assertEqual(len(self.relay.received), 4)
        for document in self.relay.received:
            blob = json.dumps(document)
            self.assertNotIn('192.0.2.51', blob)
            self.assertNotIn(SERIAL, blob)
            self.assertNotIn('gatekeeper', blob)
            self.assertNotIn('owner@example.org', document['body'])
            self.assertEqual(document['contact'], 'owner@example.org')	# to the relay only
            self.assertIn('Sophos XGS 3300r1', document['body'])
            self.assertIn('| Board | AMDA0202-0004, vendor LAA, octeontx |', document['body'])
            self.assertIn('| Board revision | 09 |', document['body'])
            self.assertIn('igb0: I211 Gigabit Network Connection (8086:1539 rev 03)', document['body'])
            self.assertIn('octep0: OCTEON TX CN83XX (177d:a300 rev 01)', document['body'])
            self.assertNotIn('xhci0', document['body'])
            self.assertNotIn('NPU99887766', document['body'])
            self.assertIn('bay A: AMDA0207-0001 rev 02, vendor AIA', document['body'])
            self.assertIn('CN8365', document['body'])
            self.assertIn('built for the running kernel: yes', document['body'])
            self.assertEqual(len(document['appliance']), 12)

    def test_one_event_over_several_lines_is_one_report(self):
        # The kernel follower explains a failed build over four lines in one second (#509): one
        # report with all four, and a line of the same program a minute later is a report of its own.
        self.write(self.log, self.line(14, 'octep-follow', 'nothing to report'), 'a')
        self.collect()
        self.write(self.log, ''.join([
            self.line(11, 'octep-follow', 'the build failed; /boot/modules/octep.ko is untouched',
                      when='2026-10-18T16:16:37+00:00'),
            self.line(11, 'octep-follow', 'module     : FreeBSD 15.1-RELEASE-p4 stable/26.7-n284159-abbbe67a45e7 SMP',
                      when='2026-10-18T16:16:37+00:00'),
            self.line(11, 'octep-follow', 'kernel.old : FreeBSD 15.1-RELEASE-p1 stable/26.7-n283674-12334a596709 SMP',
                      when='2026-10-18T16:16:38+00:00'),
            self.line(11, 'octep-follow', 'the whole log is /var/db/os-xgs-npu/build.log',
                      when='2026-10-18T16:16:38+00:00'),
            self.line(11, 'octep-follow', 'the build failed again',
                      when='2026-10-18T16:17:40+00:00'),
        ]), 'a')
        self.assertEqual(self.collect()['found'], 2)
        self.assertEqual(len(self.relay.received), 2)
        first = next(d for d in self.relay.received if 'is untouched' in d['body'])
        self.assertIn('kernel.old', first['body'])
        self.assertIn('the whole log is', first['body'])
        self.assertNotIn('the build failed again', first['body'])

    def test_a_repeat_is_counted_and_mentioned_once_a_day(self):
        self.collect()
        message = 'restore failed: the chip did not answer after %d ms'
        self.write(self.log, self.line(11, 'fanctl', message % 10), 'a')
        self.collect()
        self.write(self.log, self.line(11, 'fanctl', message % 20), 'a')
        self.collect()
        rows = self.reporter.listing()['rows']
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['count'], 2)
        self.assertTrue(rows[0]['repeat'])
        self.assertEqual(len(self.relay.received), 1)		# the repeat waits a day
        entry = self.reporter.entries()[rows[0]['id']]
        entry['sent_at'] -= 90000
        self.reporter.store(entry)
        self.collect()
        self.assertEqual(len(self.relay.received), 2)
        self.assertEqual(self.relay.received[1]['count'], 2)

    def test_manual_mode_waits_for_send_and_off_looks_at_nothing(self):
        self.write('/conf/config.xml', CONFIG % 'manual')
        self.collect()
        self.write(self.log, self.line(11, 'fanctl', 'restore failed'), 'a')
        self.collect()
        self.assertEqual(len(self.relay.received), 0)
        row = self.reporter.listing()['rows'][0]
        self.assertEqual(row['status'], 'new')
        sys.stdout = open(os.devnull, 'w')
        try:
            self.reporter.main(['reporter.py', 'send', row['id']])
        finally:
            sys.stdout.close()
            sys.stdout = sys.__stdout__
        self.assertEqual(len(self.relay.received), 1)
        self.assertEqual(self.relay.received[0]['mode'], 'manual')
        self.write('/conf/config.xml', CONFIG % 'off')
        self.write(self.log, self.line(11, 'fanctl', 'another failure'), 'a')
        self.assertEqual(self.collect()['found'], 0)

    def test_a_relay_that_is_down_is_tried_again(self):
        self.collect()
        self.relay.down = True
        self.write(self.log, self.line(11, 'fanctl', 'restore failed'), 'a')
        self.collect()
        row = self.reporter.listing()['rows'][0]
        self.assertEqual(row['status'], 'failed')
        self.relay.down = False
        self.collect()
        self.assertEqual(self.reporter.listing()['rows'][0]['status'], 'sent')

    def test_the_day_allowance_holds(self):
        self.collect()
        # A minute apart: eight failures, not one failure told over eight lines.
        self.write(self.log, ''.join(self.line(11, 'fanctl', 'failure kind %s' % c,
                                               when='2026-10-17T08:%02d:00+00:00' % (40 + i))
                                     for i, c in enumerate('abcdefgh')), 'a')
        self.collect()
        self.assertEqual(len(self.relay.received), self.reporter.NEW_PER_DAY)

    # ---------------------------------------------------------------------------------- the rest

    def test_php_error_in_a_page_is_the_page_s_part(self):
        self.collect()
        self.write('/var/lib/php/tmp/PHP_errors.log',
                   '[17-Oct-2026 08:42:00 UTC] PHP Fatal error:  Uncaught Error: x in '
                   '/usr/local/opnsense/mvc/app/controllers/OPNsense/XGS/Api/PortsController.php:60\n'
                   '[17-Oct-2026 08:42:01 UTC] PHP Warning:  y in /usr/local/www/index.php:3\n')
        self.collect()
        rows = self.reporter.listing()['rows']
        self.assertEqual([(r['part'], r['kind']) for r in rows], [('os-xgs-npu', 'php')])

    def test_module_built_for_another_kernel(self):
        self.write('/boot/modules/octep.ko.kernel', 'FreeBSD 15.1-RELEASE-p3 stable/26.7-n284000-0123456789ab SMP\n')
        self.collect()
        kinds = [r['kind'] for r in self.reporter.listing()['rows']]
        self.assertIn('module-mismatch', kinds)

    def test_driver_not_ready_after_ten_minutes(self):
        self.machine.ready = '0'
        self.collect()
        kinds = [r['kind'] for r in self.reporter.listing()['rows']]
        self.assertIn('driver-not-ready', kinds)

    def test_failed_update(self):
        self.write('/var/db/os-xgs-npu/update/apply.json',
                   json.dumps({'status': 'failed', 'message': 'the build <b>failed</b>', 'target': 'abcdef1234567890'}))
        self.write('/var/db/os-xgs-npu/update/apply.log', 'cc: error: x\nmake: stopped\n')
        self.collect()
        rows = self.reporter.listing()['rows']
        self.assertIn(('os-xgs-npu', 'update-failed'), [(r['part'], r['kind']) for r in rows])

    def test_unexpected_reset_is_found_at_boot_and_a_clean_one_is_not(self):
        self.reporter.save('boots.json', {'last_boot': BOOT - 3600})
        self.write(self.log, self.line(13, 'dhcp6c', 'Sending Solicit on oxp3',
                                       time.strftime('%Y-%m-%dT%H:%M:%S+00:00', time.gmtime(BOOT - 120))), 'a')
        self.reporter.collect(at_boot=True)
        rows = self.reporter.listing()['rows']
        reset = [r for r in rows if r['kind'] == 'unexpected-reset']
        self.assertEqual(len(reset), 1)
        entry = self.reporter.entries()[reset[0]['id']]
        self.assertIn('lost power', entry['summary'])
        self.assertTrue(any('Solicit' in line for line in entry['evidence']))
        # the next boot, after an orderly shutdown
        os.makedirs(self.state, exist_ok=True)
        with open(os.path.join(self.state, 'clean-shutdown'), 'w') as handle:
            handle.write('%d\n' % (BOOT - 50))
        before = len(self.reporter.listing()['rows'])
        self.reporter.save('boots.json', {'last_boot': BOOT - 100})
        self.reporter.collect(at_boot=True)
        self.assertEqual(len(self.reporter.listing()['rows']), before)

    def test_unexpected_reset_names_what_the_chipset_recorded(self):
        # As 06-xgs-reset-reason keeps it: the watchdog's bit, a reboot's, and one with no meaning.
        kept = {'boot': BOOT, 'value': '0x02080800', 'bits': [
            {'bit': 11, 'reason': 'no published meaning'},
            {'bit': 19, 'reason': 'software wrote 0x6 to port 0xCF9, as a reboot does'},
            {'bit': 25, 'reason': 'the hardware watchdog expired'}]}
        self.write('/var/db/os-xgs-npu/reset-reason.json', json.dumps(kept))
        self.reporter.save('boots.json', {'last_boot': BOOT - 3600})
        self.reporter.collect(at_boot=True)
        reset = [e for e in self.reporter.entries().values() if e['kind'] == 'unexpected-reset']
        self.assertEqual(len(reset), 1)
        self.assertIn('the hardware watchdog expired', reset[0]['summary'])
        self.assertIn('0x02080800', reset[0]['evidence'][0])
        # the named bits are in the fingerprint: a watchdog reset and a sync flood are two problems
        self.assertEqual(reset[0]['id'], self.reporter.fingerprint(
            'os-xgs-npu', 'unexpected-reset', 'unexpected reset cold bits 19,25'))
        # a record kept at another boot says nothing about this one
        self.assertIsNone(self.reporter.reset_reason(BOOT + 600))

    def test_manual_report_and_show(self):
        document = base64.b64encode(json.dumps({'title': 'The LCD shows garbage',
                                                'description': 'Since the update, at 192.168.1.1',
                                                'part': 'os-fanctl'}).encode()).decode()
        sys.stdout = open(os.devnull, 'w')
        try:
            self.reporter.main(['reporter.py', 'manual', document])
        finally:
            sys.stdout.close()
            sys.stdout = sys.__stdout__
        row = self.reporter.listing()['rows'][0]
        self.assertEqual((row['part'], row['kind']), ('os-fanctl', 'manual'))
        shown = self.reporter.payload(self.reporter.entries()[row['id']], 'manual',
                                      self.reporter.system_block(), self.reporter.Scrubber())
        self.assertNotIn('192.168.1.1', shown['body'])
        self.assertIn('The LCD shows garbage', shown['title'])


class ResetReasonTest(unittest.TestCase):
    """scripts/xgs/reset_reason.py against a file standing in for /dev/mem and a made-up dmesg.boot."""

    AMD = 'CPU: AMD Ryzen Embedded V1780B (3352.67-MHz K8-class CPU)\n  Origin="AuthenticAMD"  Id=0x810f11  Family=0x17  Model=0x11  Stepping=1\n'

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='reset-reason-')
        self.mem = os.path.join(self.tmp, 'mem')
        self.dmesg = os.path.join(self.tmp, 'dmesg.boot')
        self.kept = os.path.join(self.tmp, 'db', 'reset-reason.json')
        with open(self.mem, 'wb') as handle:
            handle.write(bytes(16) + (0x02080800).to_bytes(4, 'little') + bytes(12))
        os.environ.update({'XGS_RESET_MEM': self.mem, 'XGS_RESET_OFFSET': '16',
                           'XGS_RESET_DMESG': self.dmesg, 'XGS_RESET_STATE': self.kept})
        spec = importlib.util.spec_from_file_location(
            'reset_reason', os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'reset_reason.py'))
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        self.module.log = lambda message: None
        self.module.boot_time = lambda: BOOT

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def boot(self, dmesg):
        with open(self.dmesg, 'w', encoding='utf-8') as handle:
            handle.write(dmesg)
        sys.stdout = open(os.devnull, 'w')
        try:
            self.module.main(['reset_reason.py', 'boot'])
        finally:
            sys.stdout.close()
            sys.stdout = sys.__stdout__

    def test_the_record_is_read_kept_and_written_back(self):
        self.boot(self.AMD)
        with open(self.kept, encoding='utf-8') as handle:
            kept = json.load(handle)
        self.assertEqual((kept['boot'], kept['value'], kept['cleared']), (BOOT, '0x02080800', True))
        self.assertEqual([b['bit'] for b in kept['bits']], [11, 19, 25])
        self.assertEqual(kept['bits'][2]['reason'], 'the hardware watchdog expired')
        self.assertIn('no published meaning', kept['bits'][0]['reason'])
        # written back as it was read: on the chipset that clears the bits; a file keeps them
        self.assertEqual(kept['after'], '0x02080800')
        # once a boot: a second read at the same boot keeps the first
        with open(self.mem, 'r+b') as handle:
            handle.seek(16)
            handle.write(bytes(4))
        self.boot(self.AMD)
        with open(self.kept, encoding='utf-8') as handle:
            self.assertEqual(json.load(handle)['value'], '0x02080800')

    def test_nothing_is_read_where_the_register_does_not_exist(self):
        for dmesg in ('  Origin="GenuineIntel"  Id=0x906ea  Family=0x6  Model=0x9e\n',
                      '  Origin="AuthenticAMD"  Id=0x730f01  Family=0x16  Model=0x30\n'):
            self.boot(dmesg)
            self.assertFalse(os.path.exists(self.kept))

    def test_describe(self):
        self.assertEqual(self.module.describe(0), '0x00000000 - no cause recorded')
        self.assertEqual(self.module.describe(1 << 27),
                         '0x08000000 - bit 27: a sync flood after an uncorrected error')


if __name__ == '__main__':
    unittest.main(verbosity=2)
