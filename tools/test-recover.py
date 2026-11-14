#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""scripts/xgs/recover.sh, the coprocessor taken up again in place, on both families: what it counts as
done. Two things it did not count until 2026-11-06 (a review of the code):

  - OPNsense's reload of its interfaces, rc.reload_all: its result went to /dev/null, and a reload
    that stopped part-way was a recovery done on the heartbeat alone (the review's O8). It is a
    recovery not done now, which fastpath.py restarts the appliance for, and what the reload said last
    is in its lines;
  - on the XGS 3300, the device's detach that the driver refuses for a moment and says "try again"
    (octep_detach): the first refusal was final, and the appliance was restarted (O9). It is asked
    again now, every five seconds, six times in all.

recover.sh runs as a copy whose paths point at stand-ins - the driver's sysctls, kldstat, kldunload,
devctl, pciconf, the coprocessor's console log, the reset bridge, rc.reload_all - under /bin/sh, so on
Linux. sleep is a stand-in that does not sleep, and on the XGS 3300 each sleep is a moment in which the
coprocessor's console says that its new boot waits for the host. Nothing here comes from an appliance.

    python3 tools/test-recover.py
"""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
RECOVER = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'recover.sh')
POSIX = os.path.exists('/bin/sh') and sys.platform != 'win32'
WAITING = 'Waiting for USFP application to connect'
# the running kernel as uname -v says it, and two others a module may be stamped for
KERNEL_NOW = 'FreeBSD 15.1-RELEASE-p6 stable/26.7-n275000-c962f76493b6 SMP'
KERNEL_BEFORE = 'FreeBSD 15.1-RELEASE-p5 stable/26.7-n274800-0a1b2c3d4e5f SMP'
KERNEL_NEXT = 'FreeBSD 15.1-RELEASE-p7 stable/26.7-n275200-5f4e3d2c1b0a SMP'
FATAL = ("PHP Fatal error:  Uncaught Error: Call to undefined function made_up() in "
         "/usr/local/etc/inc/plugins.inc.d/made-up.inc:12")


@unittest.skipUnless(POSIX, 'recover.sh runs under /bin/sh')
class Appliance(unittest.TestCase):
    """A made-up appliance of FAMILY, its coprocessor taken as gone by the driver and its fast path
    answering the heartbeat again once the driver is up."""

    FAMILY = None

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = self.dir = self.tmp.name
        self.bin = os.path.join(d, 'bin')
        for sub in ('bin', 'sysctl', 'scripts/xgs', 'scripts/octep', 'scripts/npuctl', 'tmp'):
            os.makedirs(os.path.join(d, sub))
        self.write('calls', None)
        self.write('npu-console.log', 'an earlier boot of the coprocessor')
        drv = 'octep' if self.FAMILY == 'octeontx' else 'npuep'
        self.node('dev.%s.0.gone' % drv, '1')
        self.node('dev.%s.0.gone_at' % drv, '100')
        self.node('dev.%s.0.heartbeat.state' % drv, 'alive')
        self.write('detach-refusals', '0')
        self.write('reload-exit', '0')
        self.write('reload-said', 'Configuring interfaces...done.')

        # sysctl -n <node>: the node's file, or no such node
        self.script(os.path.join(self.bin, 'sysctl'),
                    '[ "$1" = -n ] || exit 0\n[ -f "%s/sysctl/$2" ] || exit 1\ncat "%s/sysctl/$2"'
                    % (d, d))
        # FreeBSD's stat -f %z: the size
        self.script(os.path.join(self.bin, 'stat'), 'for f; do :; done\nwc -c < "$f" | tr -d " "')
        # no sleeping; on the XGS 3300 the coprocessor's new boot waits for the host meanwhile
        self.script(os.path.join(self.bin, 'sleep'),
                    'echo "sleep $*" >> "%s/calls"\necho "%s" >> "%s/npu-console.log"'
                    % (d, WAITING if self.FAMILY == 'octeontx' else 'nothing', d))
        self.script(os.path.join(d, 'scripts', 'xgs', 'board.sh'),
                    'echo "%s %s"' % ('202' if self.FAMILY == 'octeontx' else '201', self.FAMILY))
        if self.FAMILY == 'octeontx':
            self.script(os.path.join(self.bin, 'pciconf'),
                        "[ \"$1\" = -l ] && printf 'octep0@pci0:1:0:0:\\tclass=0x0b4000 rev=0x09 hdr=0x00 "
                        "vendor=0x177d device=0xa300\\n'")
            self.stub('kldstat', 'case "$*" in *octep.ko.prev*) exit 1 ;; esac')
            self.stub('kldunload', 'exit 1')		# refused: IPsec associations point into the module
            # its detach refused as many times as detach-refusals says, then done
            self.stub('devctl', 'if [ "$1" = detach ]; then n=$(cat "%s/detach-refusals")\n'
                                '\t[ "$n" -gt 0 ] || exit 0\n\techo $((n - 1)) > "%s/detach-refusals"\n'
                                '\techo "devctl: Failed to detach octep0: Device busy" >&2\n\texit 1\nfi'
                      % (d, d))
            self.stub(os.path.join('..', 'scripts', 'octep', 'bringup.sh'))
        else:
            self.script(os.path.join(self.bin, 'pciconf'),
                        "case $1 in\n-l) printf 'npuep0@pci0:1:0:0:\\tclass=0x020000 card=0x00000000 "
                        "chip=0x708011ab rev=0x00 hdr=0x00\\n' ;;\n-r) echo 708011ab ;;\nesac")
            self.stub('kldstat', 'case "$*" in *npuep.ko.prev*) exit 1 ;; esac')
            self.stub('kldunload')
            self.stub('python3')		# the reset bridge's pulse, mcp2210.py
            self.script(os.path.join(self.bin, 'ifconfig'), '[ "$1" = npup1 ]')
            self.stub(os.path.join('..', '07-npuep'))
        for name in ('kldload', 'configctl', 'daemon'):
            self.stub(name)
        # the Intrusion Detection's restart (ids-ports.sh): what it says is in ids-said
        self.write('ids-said', None)
        self.stub(os.path.join('..', 'scripts', 'xgs', 'ids-ports.sh'), 'cat "%s/ids-said"' % d)
        # the running kernel, as uname -v says it, and /boot/modules with the module and no stamp beside it
        os.makedirs(os.path.join(d, 'modules'))
        self.write(os.path.join('modules', '%s.ko' % drv), 'a module')
        self.script(os.path.join(self.bin, 'uname'), 'echo "%s"' % KERNEL_NOW)
        self.stub(os.path.join('..', 'rc.reload_all'),
                  'cat "%s/reload-said"\nexit $(cat "%s/reload-exit")' % (d, d))
        self.env = dict(os.environ, PATH=self.bin + os.pathsep + '/usr/bin:/bin', XGS_RECOVER_LOCKED='1',
                        TMPDIR=os.path.join(d, 'tmp'))

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, name, text):
        with open(os.path.join(self.dir, name), 'w', encoding='utf-8', newline='\n') as f:
            if text is not None:
                f.write(text + '\n')

    def node(self, name, value):
        self.write(os.path.join('sysctl', name), value)

    def script(self, path, body):
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('#!/bin/sh\n%s\n' % body)
        os.chmod(path, 0o755)
        return path

    def stub(self, name, body='exit 0'):
        """A command in bin/ - or beside it, named from there - that writes down each call."""
        return self.script(os.path.normpath(os.path.join(self.bin, name)),
                           'echo "%s $*" >> "%s/calls"\n%s' % (os.path.basename(name), self.dir, body))

    def calls(self, name):
        with open(os.path.join(self.dir, 'calls'), encoding='utf-8') as f:
            return [line.rstrip('\n') for line in f if line.split(' ', 1)[0] == name]

    def recover(self):
        """A copy of recover.sh, its paths pointing at the stand-ins, run to its end."""
        d = self.dir
        with open(RECOVER, encoding='utf-8') as f:
            text = f.read()
        for real, fake in (
                ('PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin',
                 'PATH=%s:/usr/bin:/bin' % self.bin),
                ('LIB=/usr/local/opnsense/scripts', 'LIB=%s' % os.path.join(d, 'scripts')),
                ('CON=/var/log/xgs/npu-console.log', 'CON=%s' % os.path.join(d, 'npu-console.log')),
                ('LOCK=/var/run/xgs-recover.lock', 'LOCK=%s' % os.path.join(d, 'recover.lock')),
                ('/var/db/os-xgs-npu/switch-jumbo.state', os.path.join(d, 'switch-jumbo.state')),
                ('/var/run/xgs-npuep-boot', os.path.join(d, 'xgs-npuep-boot')),
                ('/usr/local/etc/rc.syshook.d/early/07-npuep', os.path.join(d, '07-npuep')),
                ('/usr/local/etc/rc.reload_all', os.path.join(d, 'rc.reload_all')),
                ('/usr/bin/uname', os.path.join(self.bin, 'uname')),
                ('/boot/modules/', os.path.join(d, 'modules') + '/')):
            self.assertIn(real, text, 'nothing in recover.sh to stand in for: %s' % real)
            text = text.replace(real, fake)
        path = self.script(os.path.join(d, 'recover.sh'), text.split('\n', 1)[1])
        return subprocess.run(['/bin/sh', path], capture_output=True, text=True, errors='replace',
                              timeout=120, env=self.env)


class Reload(object):
    """OPNsense's reload, counted on both families (O8)."""

    def test_a_reload_that_does_not_finish_is_a_recovery_not_done(self):
        self.write('reload-said', 'Configuring interfaces...\n' + FATAL)
        self.write('reload-exit', '255')
        done = self.recover()
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("OPNsense's reload of its interfaces did not finish (rc.reload_all exited 255); "
                      "not taken up in place", done.stdout)
        self.assertIn('rc.reload_all: ' + FATAL, done.stdout)
        self.assertNotIn('taken up in place in', done.stdout)
        # nothing begun behind a recovery that was not done: the management link, the switch's ports
        self.assertEqual(self.calls('daemon'), [])

    # decision row 9 (2026-11-13): the fast path back by itself before the driver took the coprocessor as
    # gone (heartbeat.gone_ms past dead_ms) - nothing restarted, and a success
    def test_a_fast_path_back_before_gone_restarts_nothing(self):
        drv = 'octep' if self.FAMILY == 'octeontx' else 'npuep'
        self.node('dev.%s.0.gone' % drv, '0')
        done = self.recover()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertRegex(done.stdout, r'the fast path answered again after \d+ s; nothing restarted')
        for name in ('python3', 'kldunload', 'kldload', 'devctl', 'rc.reload_all', 'daemon'):
            self.assertEqual(self.calls(name), [], name)

    def test_a_fast_path_back_after_gone_is_said_and_recovered_for(self):
        """gone set: the driver sends it nothing until it is brought up again, so the recovery goes on"""
        done = self.recover()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(done.stdout.count('had already taken the coprocessor as gone'), 1)
        self.assertNotIn('nothing restarted', done.stdout)
        self.assertEqual(len(self.calls('rc.reload_all')), 1)

    def test_a_dead_fast_path_waits_for_gone_and_is_refused_without_it(self):
        drv = 'octep' if self.FAMILY == 'octeontx' else 'npuep'
        self.node('dev.%s.0.gone' % drv, '0')
        self.node('dev.%s.0.heartbeat.state' % drv, 'dead')
        done = self.recover()
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn('is not 1), and a reset now could stop the host', done.stdout)
        self.assertEqual(self.calls('rc.reload_all'), [])

    def test_a_reload_that_finishes_is_taken_up_in_place(self):
        done = self.recover()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn('the fast path answers again: taken up in place in', done.stdout)
        self.assertEqual(len(self.calls('rc.reload_all')), 1)
        self.assertEqual(os.listdir(os.path.join(self.dir, 'tmp')), [], 'what the reload said is left behind')

    # #1153 (item M1, 2026-11-11): the Intrusion Detection in Netmap mode stayed on the rings of the ports
    # a reload destroyed. ids-ports.sh is asked once, after the interfaces are reloaded and the fast path
    # answers, and what it says is a line of the recovery's.
    def test_the_intrusion_detection_is_asked_once_at_the_end(self):
        self.write('ids-said', 'the Intrusion Detection restarted: in Netmap mode on npup8, whose interfaces '
                               'were made again')
        done = self.recover()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(len(self.calls('ids-ports.sh')), 1)
        lines = done.stdout.splitlines()
        at = [i for i, l in enumerate(lines) if 'the Intrusion Detection restarted' in l]
        self.assertEqual(len(at), 1, done.stdout)
        self.assertGreater(at[0], [i for i, l in enumerate(lines) if 'taken up in place in' in l][0])
        with open(os.path.join(self.dir, 'calls'), encoding='utf-8') as f:
            order = [l.split(' ', 1)[0] for l in f]
        self.assertLess(order.index('rc.reload_all'), order.index('ids-ports.sh'))

    def test_nothing_from_the_intrusion_detection_says_nothing(self):
        done = self.recover()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(len(self.calls('ids-ports.sh')), 1)
        self.assertNotIn('Intrusion Detection', done.stdout)

    def test_a_recovery_not_done_leaves_the_intrusion_detection_alone(self):
        self.write('reload-exit', '255')
        done = self.recover()
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertEqual(self.calls('ids-ports.sh'), [])

    def test_a_fast_path_back_by_itself_leaves_the_intrusion_detection_alone(self):
        drv = 'octep' if self.FAMILY == 'octeontx' else 'npuep'
        self.node('dev.%s.0.gone' % drv, '0')
        done = self.recover()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.calls('ids-ports.sh'), [])


class Octeon(Reload, Appliance):
    """The XGS 3300's recovery: the driver's module stays loaded - IPsec associations point into it - so
    its device is detached and attached again."""

    FAMILY = 'octeontx'

    def detached(self):
        return [c for c in self.calls('devctl') if c.startswith('devctl detach')]

    def test_a_detach_refused_for_a_moment_is_asked_again(self):
        self.write('detach-refusals', '2')
        done = self.recover()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.detached(), ['devctl detach octep0'] * 3)
        self.assertEqual(self.calls('devctl')[-1], 'devctl attach pci0:1:0:0')
        self.assertIn('its device detached, asked 3 time(s), and attached again', done.stdout)
        self.assertIn('taken up in place in', done.stdout)

    def test_a_detach_that_stays_refused_is_given_up_after_six_asks(self):
        self.write('detach-refusals', '100')
        done = self.recover()
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertEqual(len(self.detached()), 6)
        self.assertNotIn('devctl attach pci0:1:0:0', self.calls('devctl'))
        self.assertIn('the driver did not unload, nor its device detach, asked 6 times; not taken up '
                      'in place', done.stdout)
        self.assertEqual(self.calls('rc.reload_all'), [])

    def test_a_module_that_unloads_needs_no_detach(self):
        self.stub('kldunload')
        done = self.recover()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.calls('devctl'), [])
        self.assertEqual(self.calls('kldunload'), ['kldunload octep.ko'])


class Armada(Reload, Appliance):
    """The XGS 136's recovery: the coprocessor reset from the host, the driver unloaded and loaded again.
    Its detach never refuses (npuep_detach), so nothing here is asked twice."""

    FAMILY = 'armada'

    def module(self, name, kernel):
        """A module in /boot/modules, with the stamp of the kernel it was built for (None: no stamp)."""
        self.write(os.path.join('modules', name), 'a module')
        if kernel is not None:
            self.write(os.path.join('modules', name + '.kernel'), kernel)

    def loaded(self):
        return [c.replace(os.path.join(self.dir, 'modules') + '/', '/boot/modules/') for c in self.calls('kldload')]

    def test_the_driver_is_loaded_again_once(self):
        done = self.recover()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.calls('kldunload'), ['kldunload npuep.ko'])
        self.assertEqual(self.loaded(), ['kldload /boot/modules/npuep.ko'])

    # The module loaded again is the one built for the running kernel, chosen as 07-npuep chooses it,
    # and chosen before the coprocessor is reset (the release check's review of 2026-11-07).
    def test_npuep_ko_built_for_this_kernel_is_loaded(self):
        self.module('npuep.ko', KERNEL_NOW)
        self.module('npuep.ko.prev', KERNEL_BEFORE)
        done = self.recover()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.loaded(), ['kldload /boot/modules/npuep.ko'])

    def test_npuep_ko_for_the_next_kernel_takes_prev_built_for_this_one(self):
        self.module('npuep.ko', KERNEL_NEXT)
        self.module('npuep.ko.prev', KERNEL_NOW)
        done = self.recover()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.calls('kldunload'), ['kldunload npuep.ko'])
        self.assertEqual(self.loaded(), ['kldload /boot/modules/npuep.ko.prev'])

    def test_a_driver_from_prev_is_unloaded_by_that_name(self):
        self.stub('kldstat', 'exit 0')		# the driver came from npuep.ko.prev
        self.module('npuep.ko', KERNEL_NEXT)
        self.module('npuep.ko.prev', KERNEL_NOW)
        done = self.recover()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.calls('kldunload'), ['kldunload npuep.ko.prev'])
        self.assertEqual(self.loaded(), ['kldload /boot/modules/npuep.ko.prev'])

    # the release check's review of 0.1.0-alpha.15.1: what the reset would leave nothing to recover with is
    # refused before it - no file to load again, and a driver loaded from neither file
    def test_no_module_file_to_load_again_resets_nothing(self):
        os.remove(os.path.join(self.dir, 'modules', 'npuep.ko'))
        done = self.recover()
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn('to load again; not taken up in place - nothing reset', done.stdout)
        self.assertEqual(self.calls('python3'), [])
        self.assertEqual(self.calls('kldunload'), [])
        self.assertEqual(self.calls('kldload'), [])

    def test_a_driver_from_neither_file_resets_nothing(self):
        self.stub('kldstat', 'case "$*" in *-m*) exit 0 ;; *) exit 1 ;; esac')	# loaded, from another file
        done = self.recover()
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn('the driver came from neither npuep.ko nor npuep.ko.prev', done.stdout)
        self.assertEqual(self.calls('python3'), [])
        self.assertEqual(self.calls('kldunload'), [])

    def test_no_module_for_this_kernel_resets_nothing(self):
        self.module('npuep.ko', KERNEL_NEXT)
        self.module('npuep.ko.prev', KERNEL_BEFORE)
        done = self.recover()
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn('npuep.ko is built for another kernel and no npuep.ko.prev is for this one; not '
                      'taken up in place - nothing reset', done.stdout)
        # nothing done: the coprocessor not reset (mcp2210.py's pulse), the driver not unloaded or loaded
        self.assertEqual(self.calls('python3'), [])
        self.assertEqual(self.calls('kldunload'), [])
        self.assertEqual(self.calls('kldload'), [])
        self.assertNotIn('resetting the coprocessor', done.stdout)


IDS_PORTS = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'ids-ports.sh')
SOURCE = os.path.join(HERE, '..')


@unittest.skipUnless(POSIX, 'ids-ports.sh runs under /bin/sh')
class IntrusionDetection(unittest.TestCase):
    """scripts/xgs/ids-ports.sh (#1153): OPNsense's Intrusion Detection restarted after the front ports
    were made again, when it runs in Netmap mode on one of them - and only then. Run as a copy whose
    paths point at stand-ins: Suricata's process (pgrep), the Intrusion Detection's model and the
    interfaces (php), configd (configctl), the interfaces there (ifconfig), the locks (lockf)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = self.dir = self.tmp.name
        self.bin = os.path.join(d, 'bin')
        os.makedirs(self.bin)
        open(os.path.join(d, 'calls'), 'w').close()
        self.running(True)
        self.settings('1 netmap', 'npup8')
        self.write('interfaces', 'npup1\nnpup8\nigc0\nvlan01\nlagg0\nbridge0')
        self.write('ifconfig-vlan01', 'vlan01: flags=8943<UP>\n\tvlan: 5 vlanproto: 802.1q vlanpcp: 0 '
                                      'parent interface: npup8')
        self.write('ifconfig-lagg0', 'lagg0: flags=8943<UP>\n\tlaggproto lacp\n\tlaggport: npup2 '
                                     'flags=1c<ACTIVE>\n\tlaggport: npup3 flags=1c<ACTIVE>')
        self.write('ifconfig-bridge0', 'bridge0: flags=8843<UP>\n\tmember: npup8 flags=143<LEARNING>')
        self.stub('pgrep', '[ -f "%s/suricata-running" ]' % d)
        self.stub('php', 'cat "%s/settings"' % d)
        self.stub('configctl', 'exit 0')
        self.stub('sleep', 'exit 0')
        # ifconfig <dev>: what ifconfig-<dev> holds, for a device in interfaces
        self.script('ifconfig', 'grep -qx "$1" "%s/interfaces" || exit 1\ncat "%s/ifconfig-$1" 2>/dev/null; exit 0'
                    % (d, d))
        # lockf [-s] [-k] [-t n] file command...: the recovery's lock held while recover-held is there
        self.stub('lockf', 'while :; do case $1 in -s|-k) shift ;; -t) shift 2 ;; *) break ;; esac; done\n'
                           'case $1 in *recover.lock) [ -f "%s/recover-held" ] && exit 75 ;; esac\n'
                           'shift\nexec "$@"' % d)
        self.env = dict(os.environ, PATH=self.bin + os.pathsep + '/usr/bin:/bin')
        self.env.pop('XGS_RECOVER_LOCKED', None)
        self.env.pop('XGS_IDS_LOCKED', None)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, name, text):
        with open(os.path.join(self.dir, name), 'w', encoding='utf-8', newline='\n') as f:
            f.write(text + '\n')

    def script(self, name, body):
        path = os.path.join(self.bin, name)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('#!/bin/sh\n%s\n' % body)
        os.chmod(path, 0o755)
        return path

    def stub(self, name, body='exit 0'):
        return self.script(name, 'echo "%s $*" >> "%s/calls"\n%s' % (name, self.dir, body))

    def calls(self, name):
        with open(os.path.join(self.dir, 'calls'), encoding='utf-8') as f:
            return [line.rstrip('\n') for line in f if line.split(' ', 1)[0] == name]

    def running(self, yes):
        path = os.path.join(self.dir, 'suricata-running')
        if yes:
            open(path, 'w').close()
        elif os.path.exists(path):
            os.remove(path)

    def settings(self, first, *devices):
        """The Intrusion Detection's "<enabled> <mode>", then each device it is given."""
        self.write('settings', '\n'.join((first,) + devices))

    def run_it(self, **env):
        d = self.dir
        with open(IDS_PORTS, encoding='utf-8') as f:
            text = f.read()
        for real, fake in (
                ('PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin',
                 'PATH=%s:/usr/bin:/bin' % self.bin),
                ('LOCK=/var/run/xgs-ids-ports.lock', 'LOCK=%s' % os.path.join(d, 'ids.lock')),
                ('RECOVER_LOCK=/var/run/xgs-recover.lock', 'RECOVER_LOCK=%s' % os.path.join(d, 'recover.lock')),
                ('OVERLAY=/usr/local/etc/suricata/conf.d/xgs-inspector.yaml',
                 'OVERLAY=%s' % os.path.join(d, 'xgs-inspector.yaml')),
                ('/usr/local/bin/php', os.path.join(self.bin, 'php')),
                ('/usr/bin/true', 'true')):
            self.assertIn(real, text, 'nothing in ids-ports.sh to stand in for: %s' % real)
            text = text.replace(real, fake)
        path = os.path.join(d, 'ids-ports.sh')
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        return subprocess.run(['/bin/sh', path], capture_output=True, text=True, errors='replace',
                              timeout=60, env=dict(self.env, **env))

    def restarts(self):
        return [c for c in self.calls('configctl') if c == 'configctl ids restart']

    def test_netmap_on_a_front_port_is_restarted_once(self):
        done = self.run_it()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.restarts(), ['configctl ids restart'])
        self.assertEqual(done.stdout.strip(), 'the Intrusion Detection restarted: in Netmap mode on npup8, '
                                              'whose interfaces were made again')

    def test_suricata_not_running_is_left_alone(self):
        """at boot it starts after the ports are made, and one stopped by hand stays stopped"""
        self.running(False)
        done = self.run_it()
        self.assertEqual((done.returncode, done.stdout), (0, ''))
        self.assertEqual(self.calls('configctl'), [])
        self.assertEqual(self.calls('php'), [])

    def test_off_pcap_divert_and_other_ports_are_left_alone(self):
        for first, devices in (('0 netmap', ('npup8',)), ('1 pcap', ('npup8',)), ('1 divert', ('npup8',)),
                               ('1 netmap', ('igc0',)), ('1 netmap', ('bridge0',)), ('1 netmap', ()),
                               ('', ())):
            with self.subTest(settings=first, devices=devices):
                self.settings(first, *devices)
                done = self.run_it()
                self.assertEqual((done.returncode, done.stdout), (0, ''))
                self.assertEqual(self.restarts(), [])

    def test_a_vlan_or_lagg_over_a_front_port_is_restarted(self):
        for dev in ('vlan01', 'lagg0'):
            with self.subTest(dev=dev):
                open(os.path.join(self.dir, 'calls'), 'w').close()
                self.settings('1 netmap', 'igc0', dev)
                done = self.run_it()
                self.assertEqual(self.restarts(), ['configctl ids restart'])
                self.assertIn('in Netmap mode on %s,' % dev, done.stdout)

    def test_the_inspector_interface_from_the_overlay_counts(self):
        self.settings('1 netmap', 'igc0')
        self.write('xgs-inspector.yaml', 'netmap.900: interface\nnetmap.900.interface: oxips0')
        self.write('interfaces', 'igc0\noxips0')
        done = self.run_it()
        self.assertEqual(self.restarts(), ['configctl ids restart'])
        self.assertIn('on oxips0,', done.stdout)

    def test_not_in_the_middle_of_a_recovery(self):
        """a recovery restarts it itself at its end; while one runs nobody else does"""
        open(os.path.join(self.dir, 'recover-held'), 'w').close()
        done = self.run_it()
        self.assertEqual(self.restarts(), [])
        self.assertIn('a recovery in place runs, and restarts it at its end', done.stdout)
        done = self.run_it(XGS_RECOVER_LOCKED='1')		# the recovery itself asking
        self.assertEqual(self.restarts(), ['configctl ids restart'])

    def test_a_second_caller_at_once_gives_no_second_restart(self):
        self.stub('lockf', 'case "$*" in *ids.lock*) exit 75 ;; esac\nexit 0')	# another run holds it
        done = self.run_it()
        self.assertEqual(done.stdout, '')
        self.assertEqual(self.restarts(), [])

    def test_a_port_still_missing_is_waited_for_and_said(self):
        self.write('interfaces', 'igc0')
        done = self.run_it()
        self.assertEqual(len(self.calls('sleep')), 60)
        self.assertEqual(self.restarts(), ['configctl ids restart'])
        self.assertIn('(still missing after 60 s: npup8)', done.stdout)

    def test_a_restart_configd_refused_is_said(self):
        self.stub('configctl', 'exit 1')
        done = self.run_it()
        self.assertIn('the Intrusion Detection asked to restart, and configd said it failed', done.stdout)

    def test_the_other_reloads_ask_it_too(self):
        """reload.sh after the interfaces and rules, bringup.sh when it made the ports and is not a recovery's"""
        with open(os.path.join(SOURCE, 'contrib', 'npuep', 'reload.sh'), encoding='utf-8') as f:
            reload = f.read()
        self.assertLess(reload.rindex('configctl filter reload'), reload.index('ids-ports.sh'))
        self.assertLess(reload.index('ids-ports.sh'), reload.rindex('mgmt-link.sh coprocessor'))
        with open(os.path.join(SOURCE, 'src', 'opnsense', 'scripts', 'octep', 'bringup.sh'),
                  encoding='utf-8') as f:
            bringup = f.read()
        call = bringup[bringup.rindex('if [ "${HAD:-0}" -eq 0 ]'):]
        self.assertIn('[ -z "${XGS_RECOVER_LOCKED:-}" ]', call)
        self.assertIn('ids-ports.sh', call)


if __name__ == '__main__':
    sys.exit(0 if unittest.main(exit=False).result.wasSuccessful() else 1)
