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
import re
import shutil
import subprocess
import sys
import tempfile
import time
import types
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
        self.boot = BOOT
        self.heartbeat = {}		# dev.octep.0.heartbeat.<name>: value, when a test gives it one
        self.dmesg = '---<<BOOT>>---\nCopyright (c) 1992-2025 The FreeBSD Project.\n'
        self.next_kernel = KERNEL	# the version /boot/kernel/kernel carries: the running one, until an update
        self.on_disk = '15.1-RELEASE-p4'	# freebsd-version -k: the kernel on disk, the running one until an update
        self.last = 'shutdown time                              Thu Jan  1 2026 06:57\n'

    def run(self, argv, timeout=10):
        name = argv[0]
        if name == '/sbin/sysctl':
            return {
                'kern.boottime': '{ sec = %d, usec = 0 } Sat Oct 17 08:52:00 2026' % self.boot,
                'kern.version': KERNEL + '\n    root@build:/usr/obj/kernel\n',
                'dev.octep.0.%desc': 'Cavium OCTEON TX CN83XX PCIe endpoint (Sophos XGS NPU)' if self.attached else '',
                'dev.octep.0.ready': self.ready,
                'dev.octep.0.barmap_version': '2',
                'kern.shutdown.dumpdevname': 'ada0p3',
            }.get(argv[-1], self.heartbeat.get(argv[-1].replace('dev.octep.0.heartbeat.', ''), '')
                  if argv[-1].startswith('dev.octep.0.heartbeat.') else '')
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
        if name == '/bin/sh' and argv[-1].endswith('/boot/kernel/kernel'):
            return self.next_kernel + '\n' if self.next_kernel else ''
        if argv[-1].endswith('portmap.py'):
            return json.dumps({'module': {'slots': {'A': {'assembly.partno': 'AMDA0207-0001',
                                                            'assembly.revision': '02'}}}})
        if name.endswith('opnsense-version'):
            return '26.7.7\n'
        if name.endswith('freebsd-version'):
            return self.on_disk + '\n'
        if name == '/usr/bin/last':
            return self.last
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
            return self.dmesg
        return ''


class Relay:
    """The relay, as a function: issues by fingerprint, comments on a repeat, and its refusal of a
    repository it does not file in, as relay/worker.js's validate() answers it."""

    def __init__(self):
        self.received = []
        self.attempts = []		# every report that reached it, refused or not
        self.issues = {}
        self.down = False
        self.refuse = set()		# the repositories it does not take
        self.address = None		# the issue's address it answers, when a test gives it one

    def post(self, document):
        if self.down:
            return {'status': 'failed', 'message': 'the relay could not be reached: test'}
        self.attempts.append(document)
        if document['repo'] in self.refuse:
            return {'status': 'failed', 'code': 400,
                    'message': "the relay answered 400 not one of the project's repositories"}
        self.received.append(document)
        key = (document['repo'], document['fingerprint'])
        if key not in self.issues:
            self.issues[key] = len(self.issues) + 1
        number = self.issues[key]
        return {'status': 'ok', 'issue': number,
                'url': self.address or 'https://github.com/%s/%s/issues/%d' % (document['owner'], document['repo'], number)}


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

    def command(self, *argv):
        """One of the script's commands as the page runs it, its answer thrown away."""
        sys.stdout = open(os.devnull, 'w')
        try:
            return self.reporter.main(['reporter.py'] + list(argv))
        finally:
            sys.stdout.close()
            sys.stdout = sys.__stdout__

    # ----------------------------------------------------------------------------- crash dumps

    def dump(self, number, panic, frames, msgbuf=None):
        """A crash dump as savecore leaves it: info.N, and textdump.tar.N with ddb.txt and msgbuf.txt."""
        import io
        import tarfile
        self.write('/var/crash/info.%d' % number,
                   'Dump header from device: /dev/ada0p3\n  Panic String: %s\n' % panic)
        trace = ''.join('%s() at %s+0x1%d/frame 0xfffffe00d3990b%02d\n' % (f, f, i, i)
                        for i, f in enumerate(frames))
        with tarfile.open(os.path.join(self.root, 'var/crash/textdump.tar.%d' % number), 'w') as tar:
            for name, body in (('ddb.txt', 'db:0:kdb.enter.default>  bt\n' + trace),
                               ('msgbuf.txt', msgbuf or 'panic: %s\n' % panic)):
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

    def test_a_crash_report_carries_the_trap_and_what_came_before_it(self):
        # #755: a page fault reported with the backtrace's end alone said neither what faulted nor
        # what the kernel had logged just before it
        before = ['[%d] npuep0: giu: an ordinary line %d' % (2900 + i, i) for i in range(30)]
        before.append('[2984] npuep0: giu: the last line before the trap')
        trap = ['[2985] Fatal trap 12: page fault while in kernel mode',
                '[2985] cpuid = 2; apic id = 02',
                '[2985] fault virtual address\t= 0x10',
                '[2985] fault code\t\t= supervisor read data, page not present',
                '[2985] instruction pointer\t= 0x20:0xffffffff80e3dba5',
                '[2985] current process\t\t= 2 (clock (0))',
                '[2985] trap number\t\t= 12',
                '[2985] panic: page fault']
        tail = ['[2985] cpuid = 2', '[2985] KDB: stack backtrace:'] + [
            '[2985] frame%d() at frame%d+0x1/frame 0xfffffe00%04d' % (i, i, i) for i in range(20)]
        common = ['kdb_enter', 'panic', 'trap_pfault', 'calltrap']
        self.dump(0, 'page fault', common + ['in6_selecthlim', 'tcp_default_output', 'tcp_timer_rexmt'],
                  msgbuf='\n'.join(before + trap + tail) + '\n')
        problem = self.reporter.from_crash([])[0]
        lines = problem['evidence']
        self.assertEqual(lines[0], 'panic: page fault')
        for wanted in ('fault virtual address', 'current process', 'the last line before the trap',
                       'ddb: in6_selecthlim() at'):
            self.assertTrue(any(wanted in l for l in lines), wanted)
        # the message buffer's end is the same backtrace again: left out when ddb.txt has one
        self.assertFalse(any('frame19()' in l for l in lines))
        # and it survives the report's own limit on lines
        found = {}
        self.reporter.remember(found, self.reporter.Scrubber(), problem)
        kept = list(found.values())[0]['evidence']
        self.assertTrue(any('fault virtual address' in l for l in kept))
        self.assertTrue(any('the last line before the trap' in l for l in kept))
        self.assertTrue(any('tcp_timer_rexmt' in l for l in kept))
        # with no trap in the buffer, its last lines as before
        self.assertEqual(self.reporter.msgbuf_evidence(['a', 'b', 'c'], tail=2), ['b', 'c'])
        self.assertEqual(self.reporter.msgbuf_evidence(['a', 'b'], tail=0), [])

    # ---------------------------------------------------------------------------------- cleaning

    def test_scrubber_takes_out_what_names_the_machine(self):
        scrub = self.reporter.Scrubber()
        text = scrub('serial %s uuid %s on gatekeeper.example.org from 192.0.2.51/24 '
                     'mac de:ad:be:ef:00:02 v6 2001:db8::5e00:53ff:fe00:1/64 mail owner@example.org '
                     'npu NPU99887766 at 08:45:56 OPNsense 26.7.7 kept 127.0.0.1 169.254.254.2 '
                     'fe80::1%%oxp3 gateway fe80::ff:fe00:1%%pppoe0/64 '
                     'SSH: SHA256 Q2xhdWRlIG1hZGUgdGhpcyBmaW5nZXJwcmludCB1cA '
                     'HTTPS: SHA256 01 23 45 67 89 AB CD EF 01 23 45 67 89 AB CD EF\n'
                     'FE DC BA 98 76 54 32 10 FE DC BA 98 76 54 32 10' % (SERIAL, UUID))
        for secret in (SERIAL, UUID, 'gatekeeper', 'example.org', '192.0.2.51', 'de:ad:be:ef:00:02',
                       '2001:db8::5e00', 'owner@', 'NPU99887766', 'Q2xhdWRl', '01 23 45', 'FE DC BA'):
            self.assertNotIn(secret, text)
        for kept in ('08:45:56', '26.7.7', '127.0.0.1', '169.254.254.2', 'fe80::1', 'OPNsense'):
            self.assertIn(kept, text)
        # A link-local address made from a MAC carries the MAC: it goes, said to be link-local (#844).
        self.assertNotIn('fe80::ff:fe00:1', text)
        self.assertIn('gateway <ipv6 link-local>%pppoe0/64', text)

    def test_scrubber_takes_out_an_address_that_ends_a_sentence_and_a_disk_serial(self):
        scrub = self.reporter.Scrubber()
        text = scrub('My LAN gateway is 192.168.1.1. It stopped at 192.0.2.51.\n'
                     'then ...198.51.100.7 and peer:2001:db8::5 went quiet.\n'
                     'ada0: Serial Number S3Z9NB0K123456\nS/N: KX1234ZZ on the label')
        for secret in ('192.168.1.1', '192.0.2.51', '198.51.100.7', '2001:db8::5', 'S3Z9NB0K123456',
                       'KX1234ZZ'):
            self.assertNotIn(secret, text)
        self.assertIn('is <ipv4>. It', text)
        self.assertIn('ada0: Serial Number <withheld>', text)
        # what only looks like an address stays: a version of five parts, the vendor's build in the
        # appliance table, a serial console
        kept = scrub('version 1.2.3.4.5 and 10.0.0.1.2, Coprocessor software | vendor build 21.0.0.169, '
                     'fast path 43e1f0dc, on the serial console at 38400')
        for word in ('1.2.3.4.5', '10.0.0.1.2', 'vendor build 21.0.0.169', 'serial console'):
            self.assertIn(word, kept)

    def test_scrubber_takes_the_machine_s_names_whole(self):
        # The host here is gatekeeper.example.org. Its name inside another word stays (a host named
        # "home" ate "homepage" until 2026-11-06); the name itself, with a label before it or a
        # domain after it, still goes.
        scrub = self.reporter.Scrubber()
        text = scrub('gatekeepers and gatekeeper-ish words, example.organic; gatekeeper said it; '
                     'gatekeeper.lan and vpn.example.org and GATEKEEPER.EXAMPLE.ORG went quiet')
        for kept in ('gatekeepers', 'gatekeeper-ish', 'example.organic'):
            self.assertIn(kept, text)
        self.assertIn('<withheld> said it', text)
        self.assertIn('<withheld>.lan', text)
        self.assertIn('vpn.<withheld>', text)
        self.assertNotIn('EXAMPLE', text)

    # ---------------------------------------------------------------------------------- the logs

    def test_first_pass_reads_no_history_then_new_lines_are_found(self):
        self.write(self.log, self.line(11, 'fanctl', 'restore failed before install'), 'a')
        self.assertEqual(self.collect()['found'], 0)
        self.write(self.log, ''.join([
            self.line(11, 'fanctl', 'restore failed: the chip did not answer at 192.0.2.51'),
            self.line(13, 'kernel', '[222] octep0: giu: timed out waiting for ring 3 (status 0x0000dead)'),
            self.line(13, 'kernel', '<6>[125] oxp12: promiscuous on'),
            self.line(13, 'kernel', '[224] octep0: rpc: 44332 commands, 0 refused, 0 timed out'),
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

    def test_an_appliance_whose_owner_never_chose_sends_nothing(self):
        # Since 2026-10-23 nothing leaves an appliance unless its owner chooses: with no reporting
        # settings saved at all, a problem is found and kept, and waits for Send.
        self.write('/conf/config.xml', CONFIG.replace(
            '<OPNsense><XGS><reporting><mode>%s</mode><email>owner@example.org</email></reporting></XGS></OPNsense>', ''))
        self.assertEqual(self.reporter.settings()['mode'], 'manual')
        self.collect()
        self.write(self.log, self.line(11, 'fanctl', 'restore failed'), 'a')
        self.collect()
        self.assertEqual(len(self.relay.received), 0)
        self.assertEqual(self.reporter.listing()['rows'][0]['status'], 'new')

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

    def test_a_report_the_relay_refuses_spends_nothing_and_is_not_sent_again_by_itself(self):
        # Until 2026-11-06 a refusal spent one of the day's new reports and went again at every look:
        # os-watchdog's, which the relay did not file for (#794), held back every other new report.
        os.makedirs(os.path.join(self.root, 'usr/local/opnsense/scripts/watchdog'))
        self.relay.refuse = {'os-watchdog'}
        self.collect()
        self.write(self.log, self.line(11, 'os-watchdog', 'the watchdog could not be armed'), 'a')
        for _ in range(6):
            self.collect()
        self.assertEqual([d['repo'] for d in self.relay.attempts], ['os-watchdog'])
        row = self.reporter.listing()['rows'][0]
        self.assertEqual((row['status'], row['refused']), ('failed', True))
        self.assertEqual(row['error'], "the relay answered 400 not one of the project's repositories: "
                                       'it is kept here, and sent again only when Send is pressed')
        self.assertEqual(self.reporter.load('sends.json', {})['new'], self.reporter.NEW_PER_DAY)
        # not among the reports the header says are tried again
        self.assertEqual(self.reporter.load('summary.json', {})['waiting'], 0)
        # a report of a part the relay takes goes the same day
        self.write(self.log, self.line(11, 'fanctl', 'restore failed'), 'a')
        self.collect()
        self.assertEqual([d['repo'] for d in self.relay.received], ['os-fanctl'])
        # the owner's Send tries it again, and a second refusal spends nothing either
        self.command('send', row['id'])
        self.assertEqual(len(self.relay.attempts), 3)
        self.assertEqual(self.reporter.load('sends.json', {})['new'], self.reporter.NEW_PER_DAY - 1)
        self.assertTrue(self.reporter.entries()[row['id']]['refused'])
        # and once the relay takes it, Send sends it
        self.relay.refuse = set()
        self.command('send', row['id'])
        entry = self.reporter.entries()[row['id']]
        self.assertEqual((entry['status'], entry.get('refused')), ('sent', None))

    def test_the_relay_s_refusal_is_read_with_its_status(self):
        import io
        import urllib.error
        from unittest import mock
        fresh = load_reporter(self.root, self.state)
        body = io.BytesIO(json.dumps({'status': 'failed', 'message': "not one of the project's repositories"}).encode())
        refusal = urllib.error.HTTPError(fresh.RELAY, 400, 'Bad Request', {}, body)
        with mock.patch.object(fresh.urllib.request, 'urlopen', side_effect=refusal):
            answer = fresh.post({'schema': 1})
        self.assertEqual(answer, {'status': 'failed', 'code': 400,
                                  'message': "the relay answered 400 not one of the project's repositories"})

    def test_the_issue_s_address_is_kept_only_when_it_is_https(self):
        # XGS: Problem Reports makes the kept address a link. Until 2026-11-06 whatever the relay
        # answered was kept, javascript: as well (the review's G6); the issue's number stays either way.
        self.collect()
        self.relay.address = 'javascript:alert(document.domain)'
        self.write(self.log, self.line(11, 'fanctl', 'restore failed'), 'a')
        self.collect()
        self.relay.address = 'https://github.com/AbdelmonemAwad/os-fanctl/issues/2'
        self.write(self.log, self.line(11, 'fanctl', 'the chip did not answer', when='2026-10-17T08:45:00+00:00'), 'a')
        self.collect()
        rows = sorted(self.reporter.listing()['rows'], key=lambda r: r['issue'])
        self.assertEqual([(r['status'], r['issue'], r['url']) for r in rows],
                         [('sent', 1, ''), ('sent', 2, 'https://github.com/AbdelmonemAwad/os-fanctl/issues/2')])

    def test_the_relay_files_for_every_part_the_reporter_knows(self):
        # #794 gave the reporter five more parts, and the relay none of them: every report of theirs was
        # refused for good. The relay's list, the one its setup installs the App on, and the page's,
        # held to the reporter's.
        parts = sorted(p['name'] for p in self.reporter.PARTS)

        def listed(name, pattern):
            with open(os.path.join(HERE, '..', name), encoding='utf-8') as handle:
                block = re.search(pattern, handle.read(), re.DOTALL).group(1)
            return sorted(re.findall(r"'([\w-]+)'", block))
        self.assertEqual(listed('relay/worker.js', r'const REPOS = new Set\(\[(.*?)\]\)'), parts)
        self.assertEqual(listed('relay/setup.mjs', r'const REPOS = \[(.*?)\];'), parts)
        self.assertEqual(listed('src/opnsense/mvc/app/views/OPNsense/XGS/reports.volt',
                                r'const parts = \[(.*?)\];'), parts)

    # ---------------------------------------------------------------------------------- the rest

    def test_one_failing_configd_action_is_one_problem(self):
        # configd puts a new request id in every line it logs (#623, #627, #629, #630)
        lines = ["configd.py: [%s] Script action failed with Command '/usr/local/opnsense/scripts/xgs/"
                 "portstats.py show ' returned non-zero exit status 127." % rid
                 for rid in ('46c9f3b0-477e-42d8-b1e2-7ba02ecc31cb', 'dde52a52-8398-4abe-a591-be5e4217bc49')]
        self.assertEqual(len({self.reporter.fingerprint('os-xgs-npu', 'log-error', l) for l in lines}), 1)

    @staticmethod
    def configd_failure(rid, command, status):
        """A failing configd action as configd logs it: the command in quotes, twice, its traceback between."""
        return ("configd.py: [%s] Script action failed with Command '%s' returned non-zero exit status %d. "
                'at Traceback (most recent call last):   File "/usr/local/opnsense/service/modules/actions/'
                'script_output.py", line 93, in execute     subprocess.run(script_command, shell=True) '
                "subprocess.CalledProcessError: Command '%s' returned non-zero exit status %d."
                % (rid, command, status, command, status))

    def test_failing_configd_actions_are_told_apart_by_their_script(self):
        # Three scripts' actions failed on 2026-10-18 and 20 and were three issues only because the
        # request id was still in their lines (#481, #581, #623); with it gone, until 2026-11-06 the
        # quoted command went too, and every failing action of a part was one problem.
        fp = lambda line: self.reporter.fingerprint('os-xgs-npu', 'log-error', line)
        show = fp(self.configd_failure('0a1b2c3d-4e5f-4a6b-8c7d-9e0f1a2b3c4d',
                                       '/usr/local/opnsense/scripts/xgs/portstats.py show ', 127))
        other = fp(self.configd_failure('1b2c3d4e-5f6a-4b7c-8d9e-0f1a2b3c4d5e',
                                        '/usr/local/opnsense/scripts/xgs/ipsec.py ', 126))
        self.assertNotEqual(show, other)
        # the same script again - another request, other arguments, another status - is the same one
        self.assertEqual(show, fp(self.configd_failure('2c3d4e5f-6a7b-4c8d-9e0f-1a2b3c4d5e6f',
                                                       '/usr/local/opnsense/scripts/xgs/portstats.py show oxp3', 1)))

    def test_fingerprints_without_a_quoted_command_are_as_they_were(self):
        # The relay files a report on the issue its fingerprint opened: a fingerprint that moves is a
        # second issue for the same problem. These are the values the code before 2026-11-06 gave.
        fp = self.reporter.fingerprint
        for part, kind, line, before in (
                ('os-fanctl', 'log-error', 'fanctl restore failed: the chip did not answer after 10 ms', '821c0a5ea08d3b71'),
                ('os-xgs-npu', 'driver', 'octep0: giu: timed out waiting for ring 3 (status 0x0000dead)', '4dfe87ac29cfec82'),
                ('os-xgs-npu', 'log-error', 'xgs-assign could not reach 192.0.2.51 at 08:45:56, pid 4242', '8247a3e1a9ff187a'),
                ('os-xgs-npu', 'log-error', "octep-follow ring.c:88:9: error: variable 'n' set but not used", '2b8489ad366590a3'),
                ('os-fanctl', 'log-error', "fanctl cannot open '/dev/cuau1' at 38400", 'd8aa5ffbef0b1876')):
            self.assertEqual(fp(part, kind, line), before, line)

    def test_php_error_in_a_page_is_the_page_s_part(self):
        self.collect()
        self.write('/var/lib/php/tmp/PHP_errors.log',
                   '[17-Oct-2026 08:42:00 UTC] PHP Fatal error:  Uncaught Error: x in '
                   '/usr/local/opnsense/mvc/app/controllers/OPNsense/XGS/Api/PortsController.php:60\n'
                   '[17-Oct-2026 08:42:01 UTC] PHP Warning:  y in /usr/local/www/index.php:3\n')
        self.collect()
        rows = self.reporter.listing()['rows']
        self.assertEqual([(r['part'], r['kind']) for r in rows], [('os-xgs-npu', 'php')])

    def test_a_look_that_could_not_read_the_coprocessor_keeps_the_vendor_build(self):
        # coprocessor.py writes "unread" when its last look could not read the coprocessor, and keeps
        # the reading before it beside it: a report carries that one's vendor build (2026-11-06).
        self.write('/var/db/os-xgs-npu/coprocessor.json', json.dumps({'at': 2, 'error': 'unread'}))
        self.write('/var/db/os-xgs-npu/coprocessor.last-good.json',
                   json.dumps({'at': 1, 'software': {'sfversion': '21.0.0.169'}}))
        self.assertEqual(self.reporter.coprocessor_software()['vendor_build'], '21.0.0.169')

    def test_module_built_for_another_kernel(self):
        self.write('/boot/modules/octep.ko.kernel', 'FreeBSD 15.1-RELEASE-p3 stable/26.7-n284000-0123456789ab SMP\n')
        self.collect()
        kinds = [r['kind'] for r in self.reporter.listing()['rows']]
        self.assertIn('module-mismatch', kinds)

    def test_a_module_rebuilt_for_the_next_boot_s_kernel_waits_for_its_reboot(self):
        # As install/kernel-follow.sh leaves it after an update, before the reboot (#881): the module
        # stamped for the kernel in /boot, the one it replaced kept as .prev, stamped for the running
        # kernel - which the bring-up loads should the driver be loaded again before that boot.
        later = 'FreeBSD 15.1-RELEASE-p6 stable/26.7-n284176-c962f76493b6 SMP'
        other = 'FreeBSD 15.1-RELEASE-p5 stable/26.7-n284170-0123456789ab SMP'
        modules = os.path.join(self.root, 'boot/modules')
        for module in ('octep', 'npuep'):
            for name in os.listdir(modules):
                os.unlink(os.path.join(modules, name))
            ko = '/boot/modules/%s.ko' % module
            self.write(ko, 'ELF, built for the kernel in /boot')
            self.write(ko + '.kernel', later + '\n')
            self.write(ko + '.prev', 'ELF, the one running')
            self.write(ko + '.prev.kernel', KERNEL + '\n')
            self.machine.next_kernel = later
            self.assertEqual(self.reporter.from_module(), [], module)
            # not when the kernel in /boot is not the one it was built for
            self.machine.next_kernel = other
            self.assertEqual([p['kind'] for p in self.reporter.from_module()], ['module-mismatch'], module)
            # nor when the module kept is not the running kernel's: nothing could be loaded at all
            self.machine.next_kernel = later
            self.write(ko + '.prev.kernel', other + '\n')
            self.assertEqual([p['kind'] for p in self.reporter.from_module()], ['module-mismatch'], module)
            # nor when no module was kept, its stamp alone left
            self.write(ko + '.prev.kernel', KERNEL + '\n')
            os.unlink(os.path.join(modules, module + '.ko.prev'))
            self.assertEqual([p['kind'] for p in self.reporter.from_module()], ['module-mismatch'], module)
        # and as the owner meets it: nothing in the queue while the rebuilt module waits
        self.write('/boot/modules/npuep.ko.prev', 'ELF, the one running')
        self.collect()
        self.assertNotIn('module-mismatch', self.kinds())

    def test_driver_not_ready_after_ten_minutes(self):
        self.machine.ready = '0'
        self.collect()
        kinds = [r['kind'] for r in self.reporter.listing()['rows']]
        self.assertIn('driver-not-ready', kinds)

    def test_fast_path_stopped_is_reported_with_this_boots_lines(self):
        # As the second XGS 3300 had it on 2026-10-22, its fast path stopped for 45 seconds.
        self.machine.heartbeat = {'state': 'alive', 'deaths': '1', 'revivals': '1', 'dead_ms': '30000',
                                  'sent': '1092', 'answered': '1084', 'unmatched': '8'}
        self.machine.dmesg = ('[4100] octep0: heartbeat: no answer from the fast path for 15 s - a boot before\n'
                              '---<<BOOT>>---\n[99] octep0: heartbeat: the fast path answers, in 139 us\n'
                              '[5425] octep0: heartbeat: no answer from the fast path for 19 s\n'
                              '[5435] octep0: heartbeat: the fast path has not answered for 30 s - taken as stopped\n'
                              '[5454] octep0: heartbeat: the fast path answers again, after 48 s without an answer\n')
        self.collect()
        stopped = [e for e in self.reporter.entries().values() if e['kind'] == 'fast-path-stopped']
        self.assertEqual(len(stopped), 1)
        evidence = '\n'.join(stopped[0]['evidence'])
        self.assertIn('taken as stopped', evidence)
        self.assertIn('deaths: 1', evidence)
        self.assertNotIn('a boot before', evidence)
        self.assertIn('answering again', stopped[0]['summary'])

    def test_fast_path_stopped_is_reported_in_the_first_minutes_too(self):
        # The readiness checks wait ten minutes after boot; the heartbeat does not - the first
        # restart test on 2026-10-22 stopped the fast path four minutes into a boot.
        self.machine.boot = int(time.time()) - 240
        self.machine.heartbeat = {'state': 'dead', 'deaths': '1', 'dead_ms': '30000', 'since_answer_ms': '159000'}
        self.collect()
        self.assertIn('fast-path-stopped', [e['kind'] for e in self.reporter.entries().values()])

    def test_a_heartbeat_that_never_stopped_is_not_reported(self):
        self.machine.heartbeat = {'state': 'alive', 'deaths': '0', 'sent': '900', 'answered': '900'}
        self.collect()
        self.assertNotIn('fast-path-stopped', [e['kind'] for e in self.reporter.entries().values()])

    # ------------------------------------------------------------------ conditions: one occurrence

    STOPPED = 'c39b28ee9afc201c'	# the fast path's fingerprint on this family: the one its issues have

    def stopped_once(self):
        """The fast path taken as stopped once in this boot, as the second XGS 3300 had it on 2026-10-22."""
        self.machine.heartbeat = {'state': 'alive', 'deaths': '1', 'revivals': '1', 'dead_ms': '30000'}

    def test_a_condition_that_holds_is_one_occurrence_and_sends_no_repeat(self):
        # Until 2026-11-06 every look counted it again, 288 times a day, and the repeat that went once a
        # day carried a sighting from after any fix: the relay reopened its issue "seen again after a fix".
        self.stopped_once()
        self.collect()
        entry = self.reporter.entries()[self.STOPPED]
        # found an hour ago, and its report sent a day ago
        entry.update(first_seen=entry['first_seen'] - 3600, last_seen=entry['last_seen'] - 3600,
                     sent_at=entry['sent_at'] - 90000)
        self.reporter.store(entry)
        for _ in range(3):
            self.collect()
        entry = self.reporter.entries()[self.STOPPED]
        self.assertEqual(entry['count'], 1)
        self.assertEqual(entry['last_seen'], entry['first_seen'])	# still the time it began
        self.assertEqual([d['kind'] for d in self.relay.received], ['fast-path-stopped'])

    def test_a_dismissal_holds_while_the_condition_holds(self):
        # Until 2026-11-06 the next look, five minutes on, brought a dismissed condition back.
        self.write('/conf/config.xml', CONFIG % 'manual')
        self.stopped_once()
        self.collect()
        self.command('dismiss', self.STOPPED)
        self.collect()
        self.collect()
        self.assertEqual(self.reporter.entries()[self.STOPPED]['status'], 'dismissed')
        # a second stop in the boot changes it: the dismissal was for the first
        self.machine.heartbeat.update(deaths='2', revivals='2')
        self.collect()
        entry = self.reporter.entries()[self.STOPPED]
        self.assertEqual((entry['status'], entry['count']), ('new', 2))
        self.assertIn('2 time(s) since boot', entry['summary'])

    def test_a_condition_seen_to_clear_and_come_back_is_seen_again(self):
        # recover.sh takes the coprocessor up again in place, and the driver's counters start again
        # with its reload: a look that finds no stop, then a stop, is the fast path stopping again.
        self.stopped_once()
        self.collect()
        self.collect()
        entry = self.reporter.entries()[self.STOPPED]
        self.assertEqual(entry['count'], 1)
        entry.update(first_seen=entry['first_seen'] - 3600, last_seen=entry['last_seen'] - 3600)
        self.reporter.store(entry)
        self.machine.heartbeat = {'state': 'alive', 'deaths': '0', 'sent': '40', 'answered': '40'}
        self.collect()
        self.machine.heartbeat = {'state': 'dead', 'deaths': '1', 'dead_ms': '30000'}
        self.collect()
        entry = self.reporter.entries()[self.STOPPED]
        self.assertEqual(entry['count'], 2)
        self.assertGreater(entry['last_seen'], entry['first_seen'])
        # its repeat goes after a day, as any problem's, and says when it came back
        entry['sent_at'] -= 90000
        self.reporter.store(entry)
        self.collect()
        self.assertEqual([d['count'] for d in self.relay.received], [1, 2])
        self.assertIn('and %s.' % self.reporter.iso(entry['last_seen']), self.relay.received[1]['body'])
        # and a stop in another boot is another again
        self.machine.boot += 1800
        self.collect()
        self.assertEqual(self.reporter.entries()[self.STOPPED]['count'], 3)

    def test_a_missing_driver_is_one_occurrence_a_boot_and_a_module_one_until_it_matches(self):
        other = 'FreeBSD 15.1-RELEASE-p3 stable/26.7-n284000-0123456789ab SMP\n'
        absent, mismatch = 'a38314a99b1177c1', '6b3373e33214aba5'	# the fingerprints their issues have
        self.machine.attached = False
        self.write('/boot/modules/octep.ko.kernel', other)
        for _ in range(3):
            self.collect()

        def counts():
            return {e['id']: e['count'] for e in self.reporter.entries().values()}
        self.assertEqual(counts(), {absent: 1, mismatch: 1})
        # the next boot: the driver missing again is another occurrence; the module is the one it was
        self.machine.boot += 1800
        self.collect()
        self.assertEqual(counts(), {absent: 2, mismatch: 1})
        # the module rebuilt for the running kernel, and later one built for another again
        self.write('/boot/modules/octep.ko.kernel', KERNEL + '\n')
        self.collect()
        self.write('/boot/modules/octep.ko.kernel', other)
        self.collect()
        self.assertEqual(counts(), {absent: 2, mismatch: 2})

    def test_a_fan_incident_is_read_as_os_fanctl_writes_it(self):
        # os-fanctl's restore writes {"at", "failed": [{"channel", "detail"}], "restored"} and keeps it
        # until a restore succeeds (fanctl.py). Until 2026-11-06 it was read for "what", "reason" and
        # "message", which it never had, and counted again at every look.
        at = BOOT + 600
        incident = {'at': at + 0.25, 'restored': ['fan1'], 'failed': [
            {'channel': 'fan2', 'detail': 'fan2 could not be handed back: another fanctl-io holds the window'}]}
        self.write('/var/db/fanctl/incident.json', json.dumps(incident))
        for _ in range(3):
            self.collect()
        fans = [e for e in self.reporter.entries().values() if e['kind'] == 'fan-incident']
        self.assertEqual(len(fans), 1)
        fan = fans[0]
        self.assertEqual(fan['id'], '06839efd912f799f')		# every fan incident's issue has this one
        self.assertEqual(fan['title'], 'fan incident: fan2 could not be handed back: another fanctl-io holds the window')
        self.assertEqual((fan['count'], fan['first_seen']), (1, at))
        # a restore that fails again writes another incident
        incident['at'] = at + 3600.5
        self.write('/var/db/fanctl/incident.json', json.dumps(incident))
        self.collect()
        fan = self.reporter.entries()[fan['id']]
        self.assertEqual((fan['count'], fan['last_seen']), (2, at + 3600))

    def test_a_queue_from_before_takes_what_it_finds_as_what_held(self):
        # An appliance updated from a release before 2026-11-06: no record of what held at the last
        # look, and a condition its looks counted again and again, its repeat due.
        self.stopped_once()
        self.collect()
        kept = os.path.join(self.state, 'conditions.json')
        if os.path.exists(kept):
            os.unlink(kept)
        entry = self.reporter.entries()[self.STOPPED]
        entry.update(count=300, sent_count=12, sent_at=entry['sent_at'] - 90000)
        self.reporter.store(entry)
        self.collect()
        self.collect()
        entry = self.reporter.entries()[self.STOPPED]
        self.assertEqual((entry['count'], entry['sent_count']), (300, 300))
        self.assertEqual(len(self.relay.received), 1)

    def driver_absent(self):
        self.machine.attached = False
        self.collect()
        absent = [e for e in self.reporter.entries().values() if e['kind'] == 'driver-absent']
        self.assertEqual(len(absent), 1)
        return absent[0]['evidence']

    def test_driver_absent_says_the_module_was_never_built_and_why(self):
        # As the XGS 126 of #625 and #657 had it: no module, and the build log saying why.
        self.write('/var/db/os-xgs-npu/install-build.log', ''.join(
            'octep-follow: %s\n' % line for line in [
                'kernel         : FreeBSD 15.1-RELEASE-p4 stable/26.7-n284159-abbbe67a45e7 SMP'] + [
                'step %d' % i for i in range(20)] + [
                'no sources for abbbe67a45e7 here, and NONET=1 forbids fetching them.',
                'the module cannot be built for FreeBSD 15.1-RELEASE-p4 stable/26.7-n284159-abbbe67a45e7 SMP here']))
        evidence = self.driver_absent()
        self.assertIn('module: /boot/modules/octep.ko is not installed', evidence)
        self.assertTrue(any('NONET=1 forbids fetching' in line for line in evidence))
        # only the last lines of the log: the first of its twenty-three is not among them
        self.assertFalse(any('kernel         :' in line for line in evidence))
        self.assertLessEqual(len(evidence), 30)

    def test_driver_absent_says_what_the_installed_module_was_built_for(self):
        self.write('/boot/modules/octep.ko', 'ELF')
        evidence = self.driver_absent()
        self.assertIn('module: /boot/modules/octep.ko, built for %s' % KERNEL, evidence)
        self.assertIn('loaded: no', evidence)
        self.assertIn('/var/db/os-xgs-npu/install-build.log: not there', evidence)

    def install_this_boot(self, stamp=KERNEL):
        """As the XGS 2300 of #818 had it at 00:55 UTC on 2026-11-02: install.sh built and stamped the
        module twenty minutes into the boot, and nothing had loaded it."""
        self.write('/boot/modules/octep.ko', 'ELF')
        self.write('/boot/modules/octep.ko.kernel', stamp + '\n')
        self.write('/var/db/os-xgs-npu/install-build.log',
                   'module stamp   : <none>\n'
                   'installed /boot/modules/octep.ko, stamped for the kernel the next boot will use\n')
        later = BOOT + 1200
        for name in ('boot/modules/octep.ko', 'var/db/os-xgs-npu/install-build.log'):
            os.utime(os.path.join(self.root, name), (later, later))
        self.machine.attached = False

    def kinds(self):
        return [e['kind'] for e in self.reporter.entries().values()]

    def test_a_module_installed_in_this_boot_waits_for_its_restart(self):
        self.install_this_boot()
        self.collect()
        self.assertNotIn('driver-absent', self.kinds())

    def test_a_module_that_was_there_at_boot_and_is_not_attached_is_reported(self):
        self.install_this_boot()
        earlier = BOOT - 600
        os.utime(os.path.join(self.root, 'boot/modules/octep.ko'), (earlier, earlier))
        self.collect()
        self.assertIn('driver-absent', self.kinds())

    def test_a_driver_that_attached_in_this_boot_and_is_gone_is_reported(self):
        self.install_this_boot()
        self.machine.dmesg += '[72] octep0: dp: oxp11 carries port tag 35328\n'
        self.collect()
        self.assertIn('driver-absent', self.kinds())

    def test_driver_absent_evidence_is_this_boot_s(self):
        # The message buffer outlives a reboot: the boot before, whose driver attached, is not
        # evidence for this one, whose driver did not.
        self.machine.dmesg = ('[90] octep0: attached; the boot before\n'
                              '---<<BOOT>>---\n[12] KLD octep.ko: depends on kernel - not available\n')
        evidence = self.driver_absent()
        self.assertTrue(any('KLD octep.ko' in line for line in evidence))
        self.assertFalse(any('the boot before' in line for line in evidence))

    def test_a_module_installed_in_this_boot_for_another_kernel_is_reported(self):
        self.install_this_boot('FreeBSD 15.1-RELEASE-p6 stable/26.7-n284176-c962f76493b6 SMP')
        self.collect()
        self.assertIn('driver-absent', self.kinds())

    def test_an_untested_model_is_not_held_against_a_driver_that_is_not_there(self):
        # #657, 2026-11-02 00:55 UTC: the XGS 2300 of #818 sent "the coprocessor could not be read"
        # from the boot its driver was installed in.
        real = self.machine.run
        self.reporter.run = lambda argv, timeout=10: (
            '202 octeontx rnd\n' if argv[-1].endswith('board.sh') else real(argv, timeout))
        looked = []
        fake = types.ModuleType('portcheck')
        fake.check = lambda: looked.append(1) or {
            'verdict': 'agrees', 'model': 'XGS 2300', 'assembly': 'AMDA0202-0002', 'lines': ['10 of 10']}
        sys.modules['portcheck'] = fake
        try:
            self.machine.attached = False
            self.assertEqual(self.reporter.from_untested_model(), [])
            self.assertEqual((looked, self.reporter.load('untested.json', {})), ([], {}))
            self.machine.attached = True
            found = self.reporter.from_untested_model()
            self.assertEqual(len(found), 1)
            self.assertIn('they agree', found[0]['summary'])
        finally:
            sys.modules.pop('portcheck', None)

    def test_failed_update(self):
        self.write('/var/db/os-xgs-npu/update/apply.json',
                   json.dumps({'status': 'failed', 'message': 'the build <b>failed</b>', 'target': 'abcdef1234567890'}))
        self.write('/var/db/os-xgs-npu/update/apply.log', 'cc: error: x\nmake: stopped\n')
        self.collect()
        rows = self.reporter.listing()['rows']
        self.assertIn(('os-xgs-npu', 'update-failed'), [(r['part'], r['kind']) for r in rows])

    def test_a_failed_download_is_one_sighting_and_keeps_its_reason(self):
        """#971: one failed attempt was "seen 7 time(s)" - once a collect - and its title had lost why."""
        apply = {'status': 'failed', 'sha': '40492ce5e6420000', 'started_at': 1793595300,
                 'finished_at': 1793595310,
                 'message': 'the download failed: <urlopen error [Errno 51] Network is unreachable>'}
        self.write('/var/db/os-xgs-npu/update/apply.json', json.dumps(apply))
        self.write('/var/db/os-xgs-npu/update/apply.log', 'the download failed\n')
        for _ in range(3):
            self.collect()
        rows = [r for r in self.reporter.listing()['rows'] if r['kind'] == 'update-failed']
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['count'], 1)
        self.assertIn('Network is unreachable', rows[0]['title'])
        apply.update(started_at=1793597100, finished_at=1793597110)		# a second attempt, failed too
        self.write('/var/db/os-xgs-npu/update/apply.json', json.dumps(apply))
        self.collect()
        rows = [r for r in self.reporter.listing()['rows'] if r['kind'] == 'update-failed']
        self.assertEqual(rows[0]['count'], 2)

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

    @staticmethod
    def shutdown_record(when):
        """A shutdown as last -y prints it: in local time, the year before the time, to the minute."""
        tm = time.localtime(when)
        return 'shutdown time                              %s %s %2d %d %02d:%02d\n' % (
            ('Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun')[tm.tm_wday],
            ('Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec')[tm.tm_mon - 1],
            tm.tm_mday, tm.tm_year, tm.tm_hour, tm.tm_min)

    def test_the_shutdown_before_a_boot_is_not_its_end(self):
        # A boot that ended in a crash has no shutdown record; the newest one is the shutdown that ended
        # the boot before it, from moments before its start, and last(1) prints it to the minute. Until
        # 2026-11-06 a record up to a minute before the boot's start still counted as its orderly end,
        # and the crash went unreported.
        minute = (BOOT - 3600) // 60 * 60
        for started, record in ((minute + 40, minute + 15),	# in the minute the crashed boot started
                                (minute, minute - 30)):		# the minute before one that started on it
            self.machine.last = self.shutdown_record(record)
            self.reporter.save('boots.json', {'last_boot': started})
            self.assertEqual([p['kind'] for p in self.reporter.from_reset()], ['unexpected-reset'],
                             self.machine.last)
        # the boot's own shutdown, in a minute after the one it started in, is its orderly end as before
        for started, record in ((minute + 40, minute + 40 + 3000), (minute, minute + 60)):
            self.machine.last = self.shutdown_record(record)
            self.reporter.save('boots.json', {'last_boot': started})
            self.assertEqual(self.reporter.from_reset(), [], self.machine.last)

    def test_unexpected_reset_evidence_leaves_out_other_programs_lines(self):
        # 2026-11-06: the names an owner chose - a gateway's, a user's - came in with other programs'
        # lines (#601); only the kernel's, the logger's, devices', address clients' and ours are kept.
        self.reporter.save('boots.json', {'last_boot': BOOT - 3600})
        before = time.strftime('%Y-%m-%dT%H:%M:%S+00:00', time.gmtime(BOOT - 120))
        self.write(self.log, ''.join([
            self.line(13, 'dhcp6c', 'Sending Solicit on oxp3', before),
            self.line(13, 'sshd', 'Accepted publickey for alice from 192.0.2.9 port 51234', before),
            self.line(13, 'dpinger', 'MY_HOME_WAN_GW 192.0.2.1: Alarm latency 0us stddev 0us loss 100%', before),
            self.line(13, 'kernel', '<6>[4021] oxp3: link state changed to DOWN', before),
            self.line(13, 'xgs-watchdog', 'feeding the watchdog', before),
        ]), 'a')
        self.reporter.collect(at_boot=True)
        reset = [e for e in self.reporter.entries().values() if e['kind'] == 'unexpected-reset']
        self.assertEqual(len(reset), 1)
        evidence = '\n'.join(reset[0]['evidence'])
        for kept in ('Solicit', 'link state changed', 'feeding the watchdog'):
            self.assertIn(kept, evidence)
        for gone in ('alice', 'MY_HOME_WAN_GW', 'sshd', 'dpinger'):
            self.assertNotIn(gone, evidence)

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

    def test_a_panic_without_a_dump_is_read_from_the_message_buffer(self):
        # 2026-10-26: an XGS 126 reset a minute into a boot, bit 19 alone and no dump; what the
        # kernel said last is only in the buffer the warm reset kept.
        self.machine.dmesg = ('---<<BOOT>>---\nCopyright (c) 1992-2025 The FreeBSD Project.\n'
                              'npuep0: the coprocessor answers\n'
                              'Fatal trap 12: page fault while in kernel mode\n'
                              'fault virtual address\t= 0x10\n'
                              'current process\t\t= 0 (if_io_tqg_1)\n'
                              'panic: page fault\n'
                              'cpuid = 1\n'
                              '---<<BOOT>>---\nCopyright (c) 1992-2025 The FreeBSD Project.\n')
        self.reporter.save('boots.json', {'last_boot': BOOT - 3600})
        self.reporter.collect(at_boot=True)
        reset = [e for e in self.reporter.entries().values() if e['kind'] == 'unexpected-reset']
        self.assertEqual(len(reset), 1)
        self.assertIn('the kernel panicked and restarted the machine itself', reset[0]['summary'])
        self.assertIn('kernel: panic: page fault', reset[0]['evidence'])
        self.assertIn('kernel: Fatal trap 12: page fault while in kernel mode', reset[0]['evidence'])
        # a panic is another problem than a reset with nothing said
        self.assertEqual(reset[0]['id'], self.reporter.fingerprint(
            'os-xgs-npu', 'unexpected-reset', 'unexpected reset panic'))

    def test_a_second_reset_is_reported_as_its_own_boot_with_its_own_lines(self):
        # #701: the repeat said the first boot had ended, with both boots' lines mixed.
        stamp = lambda t: time.strftime('%Y-%m-%dT%H:%M:%S+00:00', time.gmtime(t))
        self.reporter.save('boots.json', {'last_boot': BOOT - 3600})
        self.write(self.log, self.line(13, 'kernel', 'the first boot ends', stamp(BOOT - 120)), 'a')
        self.reporter.collect(at_boot=True)
        self.machine.boot = BOOT + 3000
        self.write(self.log, self.line(13, 'kernel', 'the second boot ends', stamp(BOOT + 2900)), 'a')
        self.reporter.collect(at_boot=True)
        reset = [e for e in self.reporter.entries().values() if e['kind'] == 'unexpected-reset']
        self.assertEqual(len(reset), 1)
        self.assertEqual(reset[0]['count'], 2)
        self.assertIn(self.reporter.iso(BOOT), reset[0]['summary'])
        self.assertNotIn(self.reporter.iso(BOOT - 3600), reset[0]['summary'])
        self.assertTrue(any('the second boot ends' in line for line in reset[0]['evidence']))
        self.assertFalse(any('the first boot ends' in line for line in reset[0]['evidence']))

    # ------------------------------------------- a boot that went before its look, and a shutdown's end

    def started(self, *boots):
        """Boots the early hook 05-xgs-report wrote down that no look took away: they went before the
        start hook ran the reporter."""
        os.makedirs(self.state, exist_ok=True)
        with open(os.path.join(self.state, 'boots-unreported'), 'w') as handle:
            handle.write(''.join('%d\n' % b for b in boots))

    def shut_down(self, when):
        """The stop hook's record of a shutdown begun in order."""
        os.makedirs(self.state, exist_ok=True)
        with open(os.path.join(self.state, 'clean-shutdown'), 'w') as handle:
            handle.write('%d\n' % when)

    def resets(self):
        return [e for e in self.reporter.entries().values() if e['kind'] == 'unexpected-reset']

    def test_a_boot_that_went_before_its_look_is_the_one_reported(self):
        # #1072, P18 (2026-11-06): no boot was written down before the start hook ran the reporter, so a
        # boot that went in its driver's load was none - the boot before it was judged instead, found to
        # have shut down in order, and the crash went unreported.
        self.reporter.save('boots.json', {'last_boot': BOOT - 7200})
        self.shut_down(BOOT - 3700)		# the boot looked at last, shut down in order
        self.started(BOOT - 3600)		# the one after it, gone before its look
        self.reporter.collect(at_boot=True)
        reset = self.resets()
        self.assertEqual(len(reset), 1)
        self.assertIn('The boot that started at %s ended without an orderly shutdown'
                      % self.reporter.iso(BOOT - 3600), reset[0]['summary'])
        self.assertNotIn(self.reporter.iso(BOOT - 7200), reset[0]['summary'])
        # every record taken away, so the next boot judges none of them again
        for name in ('boots-unreported', 'boot-started', 'clean-shutdown'):
            self.assertFalse(os.path.exists(os.path.join(self.state, name)), name)
        self.assertEqual(self.reporter.load('boots.json', {})['last_boot'], BOOT)

    def test_each_boot_since_the_last_look_is_judged_up_to_the_next_ones_start(self):
        stamp = lambda t: time.strftime('%Y-%m-%dT%H:%M:%S+00:00', time.gmtime(t))
        self.reporter.save('boots.json', {'last_boot': BOOT - 7200})
        self.started(BOOT - 3600, BOOT - 1800)
        self.write(self.log, ''.join([
            self.line(13, 'kernel', 'the boot looked at last ends', stamp(BOOT - 3700)),
            self.line(13, 'kernel', 'the first boot gone before its look ends', stamp(BOOT - 1900)),
            self.line(13, 'kernel', 'the second ends', stamp(BOOT - 100))]), 'a')
        problems = self.reporter.from_reset()
        self.assertEqual([p['signature'] for p in problems],
                         ['unexpected reset unread', 'unexpected reset unread', 'unexpected reset cold'])
        for problem, started, said in zip(problems, (BOOT - 7200, BOOT - 3600, BOOT - 1800),
                                          ('the boot looked at last ends', 'the first boot gone before its look ends',
                                           'the second ends')):
            self.assertIn('The boot that started at %s ended' % self.reporter.iso(started), problem['summary'])
            self.assertEqual([line.split(': ', 1)[1] for line in problem['evidence'] if line.split(' ')[1] == 'kernel:'],
                             [said], problem['evidence'])

    def test_a_boot_gone_before_its_look_by_an_orderly_shutdown_is_not_reported(self):
        # its owner shut it down while it booted; and the boot before it was ended by reboot(8), whose
        # record the stop hook's, written again since, no longer is
        self.reporter.save('boots.json', {'last_boot': BOOT - 7200})
        self.machine.last = self.shutdown_record(BOOT - 3650)
        self.shut_down(BOOT - 100)
        self.started(BOOT - 3600)
        self.assertEqual(self.reporter.from_reset(), [])

    def test_a_shutdown_that_panicked_after_its_hooks_is_reported(self):
        # #1072, P13 (2026-11-06): the stop hook writes its record as a shutdown begins, and the kernel's
        # own part - its sync, the drivers' shutdown methods - comes after it; a panic there read as an
        # orderly end, though the message buffer the restart kept held it.
        self.reporter.save('boots.json', {'last_boot': BOOT - 3600})
        self.shut_down(BOOT - 60)
        self.machine.dmesg = ('---<<BOOT>>---\nCopyright (c) 1992-2025 The FreeBSD Project.\n'
                              'Syncing disks, vnodes remaining... 0 done\n'
                              'All buffers synced.\n'
                              'Fatal trap 12: page fault while in kernel mode\n'
                              'current process\t\t= 1 (init)\n'
                              'panic: page fault\n'
                              '---<<BOOT>>---\nCopyright (c) 1992-2025 The FreeBSD Project.\n')
        self.reporter.collect(at_boot=True)
        reset = self.resets()
        self.assertEqual(len(reset), 1)
        self.assertEqual(reset[0]['title'], 'the appliance panicked while it shut down')
        self.assertIn('began an orderly shutdown and did not finish it', reset[0]['summary'])
        self.assertIn('kernel: panic: page fault', reset[0]['evidence'])
        self.assertEqual(reset[0]['id'], self.reporter.fingerprint(
            'os-xgs-npu', 'unexpected-reset', 'unexpected reset panic in shutdown'))

    def test_a_shutdown_that_finished_is_not_reported_from_its_message_buffer(self):
        # the boot before as a support bundle's dmesg.txt keeps one that ended in order: its sync and its
        # uptime, then the next boot
        self.reporter.save('boots.json', {'last_boot': BOOT - 3600})
        self.shut_down(BOOT - 60)
        self.machine.dmesg = ('---<<BOOT>>---\nCopyright (c) 1992-2025 The FreeBSD Project.\n'
                              '[2049] Syncing disks, vnodes remaining... 0 done\n'
                              '[2050] All buffers synced.\n'
                              '[2051] Uptime: 34m11s\n'
                              '---<<BOOT>>---\nCopyright (c) 1992-2025 The FreeBSD Project.\n')
        self.assertEqual(self.reporter.from_reset(), [])

    @unittest.skipUnless(os.path.exists('/bin/sh') and sys.platform != 'win32', 'the hooks run under /bin/sh')
    def test_the_hooks_and_the_look_across_a_boot_that_went(self):
        """The early hook and the stop hook themselves, in copies that write into this test's state, and
        the look at boot: a boot that went in its driver's load is the one reported at the next."""
        hooks = os.path.join(HERE, '..', 'src', 'etc', 'rc.syshook.d')
        clock = os.path.join(self.tmp, 'clock')

        def hook(level, name):
            with open(os.path.join(hooks, level, name), encoding='utf-8') as f:
                text = f.read().replace('/var/db/os-xgs-npu/reports', self.state)
            for real, stand_in in (('/sbin/sysctl', 'echo "{ sec = $(cat %s), usec = 0 } Sat Oct 17 08:52:00 2026"'),
                                   ('/bin/date', 'cat %s')):
                path = os.path.join(self.tmp, os.path.basename(real))
                with open(path, 'w', newline='\n') as f:
                    f.write('#!/bin/sh\n' + stand_in % clock + '\n')
                os.chmod(path, 0o755)
                text = text.replace(real, path)
            path = os.path.join(self.tmp, name)
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)
            self.assertEqual(subprocess.run(['/bin/sh', path], timeout=30).returncode, 0)

        def at(when):
            with open(clock, 'w') as f:
                f.write('%d\n' % when)

        for boot, gone in ((BOOT - 7200, False), (BOOT - 3600, True), (BOOT, False)):
            at(boot)
            hook('early', '05-xgs-report')
            if gone:
                continue			# it went in its driver's load, before its look
            self.machine.boot = boot
            self.reporter.collect(at_boot=True)
            at(boot + 3000)
            if boot != BOOT:
                hook('stop', '90-xgs-report')
        reset = self.resets()
        self.assertEqual(len(reset), 1)
        self.assertIn('The boot that started at %s ended without an orderly shutdown'
                      % self.reporter.iso(BOOT - 3600), reset[0]['summary'])

    def test_untested_model_reports_are_told_apart_by_model(self):
        # #786: normalise() made AMDA0201 and AMDA0202 one string, and two models one issue.
        fp = self.reporter.fingerprint
        one = fp('os-xgs-npu', 'untested-model', 'untested model AMDA0201-0001 agrees', 'AMDA0201-0001')
        two = fp('os-xgs-npu', 'untested-model', 'untested model AMDA0202-0001 agrees', 'AMDA0202-0001')
        self.assertNotEqual(one, two)
        # the same model's report again, from another appliance or another boot, is the same one
        self.assertEqual(one, fp('os-xgs-npu', 'untested-model', 'untested model AMDA0201-0001 agrees',
                                 'AMDA0201-0001'))
        # and a problem with no key keeps the fingerprint it always had
        self.assertEqual(fp('os-xgs-npu', 'log-error', 'restore failed after 10 ms'),
                         fp('os-xgs-npu', 'log-error', 'restore failed after 20 ms', None))

    def manual(self, title, description, part='os-xgs-npu'):
        """A report written by hand, as the page sends it."""
        document = base64.b64encode(json.dumps({'title': title, 'description': description,
                                                'part': part}).encode()).decode()
        sys.stdout = open(os.devnull, 'w')
        try:
            self.reporter.main(['reporter.py', 'manual', document])
        finally:
            sys.stdout.close()
            sys.stdout = sys.__stdout__

    def test_manual_report_and_show(self):
        self.manual('The LCD shows garbage', 'Since the update, at 192.168.1.1', 'os-fanctl')
        row = self.reporter.listing()['rows'][0]
        self.assertEqual((row['part'], row['kind']), ('os-fanctl', 'manual'))
        shown = self.reporter.payload(self.reporter.entries()[row['id']], 'manual',
                                      self.reporter.system_block(), self.reporter.Scrubber())
        self.assertNotIn('192.168.1.1', shown['body'])
        self.assertIn('The LCD shows garbage', shown['title'])

    def test_the_kernel_row_is_the_running_kernel_with_the_one_on_disk_beside_it(self):
        """#1072, P10 (2026-11-06): the release came from freebsd-version -k - the kernel on disk - and
        the commit from the running one, so between an update and its reboot the row named a kernel that
        does not exist."""
        self.assertEqual(self.reporter.system_block()['kernel'], '15.1-RELEASE-p4 abbbe67a45e7')
        self.machine.on_disk = '15.1-RELEASE-p6'
        self.assertEqual(self.reporter.system_block()['kernel'],
                         '15.1-RELEASE-p4 abbbe67a45e7 (the next boot takes 15.1-RELEASE-p6, installed)')

    def test_manual_reports_are_their_owners_own(self):
        """Issue 975: one title was one report, the second's text dropped here or filed elsewhere."""
        def written():
            return sorted((e['title'], e['summary'], e['count'])
                          for e in self.reporter.entries().values() if e['kind'] == 'manual')

        # One title, two texts: two reports, each with its own text.
        self.manual('question', 'the offload counter stays at 0')
        self.manual('question', 'Port2 is slower with hardware offload')
        # Titles that differ in their numbers alone are two reports as well.
        self.manual('XGS 2300 Port 2 drops', 'after the update')
        self.manual('XGS 3300 Port 3 drops', 'after the update')
        self.assertEqual(written(), [('XGS 2300 Port 2 drops', 'after the update', 1),
                                     ('XGS 3300 Port 3 drops', 'after the update', 1),
                                     ('question', 'Port2 is slower with hardware offload', 1),
                                     ('question', 'the offload counter stays at 0', 1)])

        # The same report sent again from this appliance is counted on the first.
        self.manual('question', 'the offload counter stays at 0')
        self.assertIn(('question', 'the offload counter stays at 0', 2), written())
        self.assertEqual(len(written()), 4)

        # The same title and text from another appliance is that appliance's own report: the relay
        # files reports by their fingerprint, so this one never lands on another owner's issue.
        own = self.reporter.appliance_id
        try:
            self.reporter.appliance_id = lambda: 'another-appliance'
            self.manual('question', 'the offload counter stays at 0')
        finally:
            self.reporter.appliance_id = own
        self.assertEqual(len(written()), 5)
        self.assertEqual(sorted(c for t, s, c in written() if s == 'the offload counter stays at 0'), [1, 2])

        # And none carries the fingerprint a title alone gives, which every appliance's "question" shared.
        keyless = self.reporter.fingerprint('os-xgs-npu', 'manual', 'manual question')
        self.assertNotIn(keyless, self.reporter.entries())


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


class CountTest(unittest.TestCase):
    """scripts/xgs/count.py: off unless the owner turned "Count this appliance" on (2026-10-23)."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='count-')
        spec = importlib.util.spec_from_file_location(
            'count', os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'count.py'))
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        self.module.CONFIG = os.path.join(self.tmp, 'config.xml')

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def config(self, reporting):
        with open(self.module.CONFIG, 'w', encoding='utf-8') as handle:
            handle.write('<?xml version="1.0"?>\n<opnsense><OPNsense><XGS>%s</XGS></OPNsense></opnsense>\n' % reporting)

    def test_off_unless_turned_on(self):
        self.assertFalse(self.module.enabled())		# no config.xml at all
        self.config('')
        self.assertFalse(self.module.enabled())		# the plugin never saved
        self.config('<reporting><mode>manual</mode></reporting>')
        self.assertFalse(self.module.enabled())
        self.config('<reporting><count>0</count></reporting>')
        self.assertFalse(self.module.enabled())
        self.config('<reporting><count>1</count></reporting>')
        self.assertTrue(self.module.enabled())


if __name__ == '__main__':
    unittest.main(verbosity=2)
