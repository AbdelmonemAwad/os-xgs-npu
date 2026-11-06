#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""kernel-follow.sh away from the appliance, where the kernel's sources cannot be had: what it says of
the next boot with a module installed and with none (#818). And the stop hook at a reboot while another
run holds the follower's lock: the next boot sent to kernel.old where the module is built for it, and the
hook's line saying so only then. And the upgrade hook, with the new kernel still a staged set: the
follower cut off at its time limit, and a failed rebuild said to leave the next boot on kernel.old where
the module is built for the kernel in /boot. And the mark of a fall-back the follower armed itself, which
uninstall.sh reads: written once nextboot took it and not for a try that could not arm one, and taken
away with the fall-back once the module is rebuilt (the review's U12). logger, sysctl, lockf, tar,
pciconf and make are stand-ins in copies of the scripts, uname answers the kernel that is running, and
every other path is a variable they have.

    python3 tools/test-kernel-follow.py
"""
import os
import subprocess
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', 'install', 'kernel-follow.sh')
STOP = os.path.join(HERE, '..', 'src', 'etc', 'rc.syshook.d', 'stop', '20-octep')
UPGRADE = os.path.join(HERE, '..', 'src', 'etc', 'rc.syshook.d', 'upgrade', '20-octep')
KERNEL = 'FreeBSD 15.1-RELEASE-p6 stable/26.7-n284176-c962f76493b6 SMP'
OLD = 'FreeBSD 15.1-RELEASE-p4 stable/26.7-n284159-abbbe67a45e7 SMP'
OLDER = 'FreeBSD 15.1-RELEASE-p2 stable/26.7-n284102-0f1e2d3c4b5a SMP'
OLDEST = 'FreeBSD 15.1-RELEASE-p1 stable/26.7-n283674-12334a596709 SMP'
# A kernel of the major release before, as kernel.old is after an upgrade from 26.1 to 26.7 (P19)
MAJOR_BEFORE = 'FreeBSD 14.3-RELEASE-p16 stable/26.1-n274021-5a6b7c8d9e0f SMP'


class StandIns(unittest.TestCase):
    """A made-up /boot in a temporary directory, its kernel the one to follow."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        os.makedirs(os.path.join(self.dir, 'modules'))
        self.kernel('kernel', KERNEL)

    def tearDown(self):
        self.tmp.cleanup()

    def path(self, name):
        return os.path.join(self.dir, name)

    def kernel(self, name, version):
        with open(self.path(name), 'w') as f:
            f.write('\0\0' + version + '\n\0')

    def module(self, stamp):
        with open(self.path('modules/octep.ko'), 'w') as f:
            f.write('ELF')
        with open(self.path('modules/octep.ko.kernel'), 'w') as f:
            f.write(stamp + '\n')

    def stub(self, name, body):
        path = self.path(name)
        with open(path, 'w', newline='\n') as f:
            f.write('#!/bin/sh\n%s\n' % body)
        os.chmod(path, 0o755)
        return path

    def recorder(self, name, into):
        return self.stub(name, "printf '%%s\\n' \"$*\" >> '%s'" % self.path(into))

    def read(self, name):
        try:
            with open(self.path(name)) as f:
                return f.read()
        except FileNotFoundError:
            return ''

    def copy(self, source, name, swaps):
        """source with each command of swaps replaced by its stand-in"""
        with open(source, encoding='utf-8') as f:
            text = f.read()
        for command, stand_in in swaps:
            text = text.replace(command, stand_in)
        path = self.path(name)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        os.chmod(path, 0o755)
        return path

    def env(self, **more):
        env = dict(os.environ)
        for name in ('OCTEP_FOLLOW_LOCKED', 'PINONLY'):
            env.pop(name, None)
        env.update(MODDIR=self.path('modules'), KERNEL=self.path('kernel'),
                   KERNEL_OLD=self.path('kernel.old'), NEXTBOOT=self.path('nextboot'),
                   PENDINGDIR=self.path('pending'), PENDING_KERNEL=self.path('kernel.pending'),
                   PINNED=self.path('pinned'), ARMED=self.path('armed'), SAID=self.path('said'),
                   LOCK=self.path('lock'),
                   BUILDROOT=self.path('build'), SRCTREE=self.path('src'),
                   FETCH=self.stub('fetch-sources.sh', 'echo "no sources for c962f76493b6 here, '
                                   'and NONET=1 forbids fetching them."; exit 1'))
        env.update(more)
        return env


class NoSources(StandIns):

    def follow(self):
        script = self.copy(SCRIPT, 'kernel-follow.sh', (('/usr/bin/logger', self.stub('logger', 'exit 0')),
                                                        ('/sbin/sysctl', self.stub('sysctl', 'exit 1'))))
        env = self.env(OCTEP_FOLLOW_LOCKED='1', FORCE='1', NOFETCH='1',
                       UNAME=self.stub('uname', 'echo "%s"' % KERNEL))
        return subprocess.run(['/bin/sh', script], env=env, capture_output=True, text=True, timeout=60)

    def test_with_no_module_it_says_no_front_ports_and_arms_nothing(self):
        done = self.follow()
        said = done.stdout + done.stderr
        self.assertEqual(done.returncode, 1)
        self.assertIn('could not get kernel sources for ' + KERNEL, said)
        self.assertIn('octep.ko is installed, so the next boot brings no front ports up', said)
        self.assertNotIn('will refuse', said)
        self.assertNotIn('kernel.old', said)
        self.assertFalse(os.path.exists(self.path('pinned')))

    def test_with_a_module_for_another_kernel_it_says_the_refusal(self):
        self.module(OLD)
        done = self.follow()
        said = done.stdout + done.stderr
        self.assertEqual(done.returncode, 1)
        self.assertIn('the next boot will refuse the module and the front ports will not come up', said)
        self.assertIn('no usable /boot/kernel.old', said)
        # tried once this boot, and nothing armed: the mark of a try, and none of a fall-back
        self.assertTrue(os.path.exists(self.path('pinned')))
        self.assertFalse(os.path.exists(self.path('armed')))


class AtShutdown(StandIns):
    """The stop hook at a reboot after a kernel update: /boot/kernel the new kernel, kernel.old the one
    running, and another run holding the follower's lock - one started while the kernel was still a
    staged set, still fetching its sources - until the hook's time limit cuts its own run off. Until
    2026-11-06 nothing armed the fall-back to kernel.old then, and the hook said the next boot took it."""

    def setUp(self):
        super().setUp()
        self.kernel('kernel.old', OLD)

    def follower(self):
        return self.copy(SCRIPT, 'kernel-follow.sh', (
            ('/usr/bin/logger', self.recorder('logger', 'log')),
            ('/sbin/sysctl', self.stub('sysctl', 'exit 1')),
            # the lock is held: wait, as lockf -t 900 does, until the time limit ends it
            ('/usr/bin/lockf', self.stub('lockf', ": > '%s'\nexec sleep 10" % self.path('waited')))))

    def environment(self, **more):
        return self.env(UNAME=self.stub('uname', 'echo "%s"' % OLD),
                        NEXTBOOT=self.recorder('nextboot', 'nextboot.args'), **more)

    def stop(self):
        hook = self.copy(STOP, 'stop-20-octep', (
            ('/usr/sbin/pciconf', self.stub('pciconf', "echo 'octep0@pci0:1:0:0: class=0x0b4000 "
                                                        "rev=0x09 hdr=0x00 vendor=0x177d device=0xa300'")),
            ('/usr/bin/logger', self.recorder('logger', 'log'))))
        env = self.environment(FOLLOW=self.follower(), LIMIT='1')
        return subprocess.run(['/bin/sh', hook], env=env, capture_output=True, text=True, timeout=60)

    def pin_only(self):
        env = self.environment(PINONLY='1')
        return subprocess.run(['/bin/sh', self.follower()], env=env, capture_output=True, text=True,
                              timeout=60)

    def test_a_module_for_the_old_kernel_is_sent_to_kernel_old_with_the_lock_held(self):
        self.module(OLD)
        done = self.stop()
        log = self.read('log')
        self.assertEqual(done.returncode, 0)
        self.assertTrue(os.path.exists(self.path('waited')), 'the run under the time limit took no lock')
        self.assertEqual(self.read('nextboot.args').splitlines(), ['-k kernel.old'])
        self.assertTrue(os.path.exists(self.path('pinned')))
        self.assertTrue(os.path.exists(self.path('armed')), 'the fall-back it armed is not marked as its own')
        self.assertIn('-t octep-stop -p daemon.notice gave up after 1s - the next boot takes kernel.old, '
                      'which the module is built for', log)

    def test_a_module_built_for_the_new_kernel_arms_nothing(self):
        self.module(KERNEL)
        done = self.stop()
        log = self.read('log')
        self.assertEqual(done.returncode, 0)
        self.assertTrue(os.path.exists(self.path('waited')), 'the run under the time limit took no lock')
        self.assertEqual(self.read('nextboot.args'), '')
        self.assertFalse(os.path.exists(self.path('pinned')))
        self.assertFalse(os.path.exists(self.path('armed')))
        self.assertIn('-t octep-stop -p daemon.notice gave up after 1s - the module is built for the '
                      'kernel the next boot takes', log)
        self.assertNotIn('takes kernel.old', log)

    def test_a_module_built_for_neither_kernel_is_not_said_to_take_kernel_old(self):
        self.module(OLDER)
        done = self.stop()
        log = self.read('log')
        self.assertEqual(done.returncode, 0)
        self.assertEqual(self.read('nextboot.args'), '')
        self.assertFalse(os.path.exists(self.path('armed')))
        self.assertIn('the module does not match /boot/kernel.old either', log)
        self.assertIn('-t octep-stop -p daemon.notice gave up after 1s - nothing sent the next boot to '
                      'kernel.old', log)
        self.assertNotIn('takes kernel.old', log)

    def test_a_kernel_old_of_another_major_release_is_not_armed(self):
        # P19: after a major upgrade kernel.old is the release before's kernel, under the new release's
        # userland, which FreeBSD does not support. Until 2026-11-06 the module built for it was enough.
        self.kernel('kernel.old', MAJOR_BEFORE)
        self.module(MAJOR_BEFORE)
        done = self.pin_only()
        self.assertEqual(done.returncode, 1)
        self.assertIn('/boot/kernel.old is FreeBSD 14.3-RELEASE-p16, of another major release than FreeBSD '
                      '15.1-RELEASE-p6; the next boot is not sent to it', done.stdout)
        self.assertEqual(self.read('nextboot.args'), '')
        self.assertFalse(os.path.exists(self.path('pinned')))

    def test_a_kernel_old_of_the_same_major_release_is_armed(self):
        self.module(OLD)
        done = self.pin_only()
        self.assertEqual(done.returncode, 0)
        self.assertEqual(self.read('nextboot.args').splitlines(), ['-k kernel.old'])
        self.assertNotIn('another major release', done.stdout)

    def test_with_no_module_nothing_is_armed_and_no_lock_waited_for(self):
        # The stop hook says nothing on a machine with no module, so the step it runs first is asked.
        done = self.pin_only()
        self.assertEqual(done.returncode, 3)
        self.assertIn('nothing to fall back with', done.stdout)
        self.assertFalse(os.path.exists(self.path('waited')))
        self.assertEqual(self.read('nextboot.args'), '')
        self.assertFalse(os.path.exists(self.path('pinned')))
        self.assertFalse(os.path.exists(self.path('armed')))


class Rebuilt(StandIns):
    """A cron tick after a kernel update: /boot/kernel the new kernel, kernel.old the one running and the
    module built for it, the sources there. The fall-back is armed before the build, marked as the
    follower's own, and both let go of once the module is built for /boot/kernel (the review's U12)."""

    def setUp(self):
        super().setUp()
        self.kernel('kernel.old', OLD)
        self.module(OLD)
        os.makedirs(self.path('src'))
        for name in ('Makefile', 'octep.c', 'octep.h'):
            open(self.path('src/' + name), 'w').close()
        os.makedirs(self.path('sys/sys'))
        open(self.path('sys/sys/param.h'), 'w').close()

    def test_the_fall_back_armed_first_is_marked_and_let_go_of_once_built(self):
        armed = self.path('armed')
        script = self.copy(SCRIPT, 'kernel-follow.sh', (
            ('/usr/bin/logger', self.stub('logger', 'exit 0')),
            ('/sbin/sysctl', self.stub('sysctl', 'exit 1')),
            ('/var/db/os-xgs-npu/build.log', self.path('build.log')),
            # make as a build that compiled every source, its machine headers from the sources given
            ('/usr/bin/make', self.stub('make', 'for a in "$@"; do case "$a" in SYSDIR=*) s=${a#SYSDIR=} ;; '
                                                'esac; done\nln -s "$s/x86" machine\nfor c in *.c; do '
                                                ': > "${c%.c}.o"; done\n: > octep.ko'))))
        env = self.env(OCTEP_FOLLOW_LOCKED='1', UNAME=self.stub('uname', 'echo "%s"' % OLD),
                       FETCH=self.stub('fetch-here.sh', 'echo "SYSDIR=%s"' % self.path('sys')),
                       # what nextboot was asked, and whether the follower's own mark was there then
                       NEXTBOOT=self.stub('nextboot', "printf '%%s %%s\\n' \"$*\" \"$([ -f '%s' ] && echo marked "
                                                      "|| echo unmarked)\" >> '%s'"
                                          % (armed, self.path('nextboot.args'))))
        done = subprocess.run(['/bin/sh', script], env=env, capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.read('modules/octep.ko.kernel').strip(), KERNEL)
        self.assertEqual(self.read('nextboot.args').splitlines(), ['-k kernel.old unmarked', '-D marked'])
        self.assertFalse(os.path.exists(armed))
        self.assertFalse(os.path.exists(self.path('pinned')))


class AtUpgrade(StandIns):
    """The upgrade hook between an update's packages and its kernel: the new kernel a staged set, /boot/kernel
    the one running, kernel.old the one before that, and the new kernel's sources not to be had. Until
    2026-11-06 the hook waited for the follower for as long as it took, and said of every failed rebuild
    that the next boot would refuse the module - where the reboot after it takes kernel.old with the ports
    (AtShutdown)."""

    def setUp(self):
        super().setUp()
        self.kernel('kernel', OLD)
        self.kernel('kernel.old', OLDER)
        self.kernel('staged', KERNEL)
        os.makedirs(self.path('pending'))
        open(self.path('pending/kernel-26.7.8-amd64.txz'), 'w').close()
        open(self.path('kernel.pending'), 'w').close()

    def upgrade(self, held=False):
        """The hook, LIMIT 1 s; with held, another run holds the follower's lock past it."""
        swaps = [('/usr/bin/logger', self.recorder('logger', 'log')),
                 ('/sbin/sysctl', self.stub('sysctl', 'exit 1')),
                 # tar -xOf <set> boot/kernel/kernel: the staged kernel, read out of its set
                 ('/usr/bin/tar', self.stub('tar', "cat '%s'" % self.path('staged')))]
        if held:
            swaps.append(('/usr/bin/lockf', self.stub('lockf', ": > '%s'\nexec sleep 20" % self.path('waited'))))
        follower = self.copy(SCRIPT, 'kernel-follow.sh', swaps)
        hook = self.copy(UPGRADE, 'upgrade-20-octep', (
            ('/usr/sbin/pciconf', self.stub('pciconf', "echo 'octep0@pci0:1:0:0: class=0x0b4000 "
                                                        "rev=0x09 hdr=0x00 vendor=0x177d device=0xa300'")),
            ('/usr/bin/logger', self.recorder('logger', 'log'))))
        env = self.env(UNAME=self.stub('uname', 'echo "%s"' % OLD),
                       NEXTBOOT=self.recorder('nextboot', 'nextboot.args'), FOLLOW=follower, LIMIT='1')
        if not held:
            env['OCTEP_FOLLOW_LOCKED'] = '1'
        began = time.monotonic()
        done = subprocess.run(['/bin/sh', hook], env=env, capture_output=True, text=True, timeout=60)
        return done, time.monotonic() - began

    def test_a_failed_rebuild_leaves_the_next_boot_on_kernel_old_where_the_module_is_built_for_it(self):
        self.module(OLD)
        done, _ = self.upgrade()
        log = self.read('log')
        self.assertEqual(done.returncode, 0)
        self.assertIn('-t octep -p daemon.err the module could NOT be rebuilt for the kernel this update '
                      'installs', log)
        self.assertIn('-t octep -p daemon.err the reboot that follows tries again', log)
        self.assertIn('-t octep-follow -p daemon.err once the staged kernel is applied, /boot/kernel.old is '
                      'the kernel this module is built for', log)
        self.assertNotIn('the next boot will refuse', log)
        self.assertNotIn('falling back would not help', log)
        # armed at the reboot, by the stop hook, once kernel.old is that kernel - not here
        self.assertEqual(self.read('nextboot.args'), '')
        self.assertFalse(os.path.exists(self.path('pinned')))

    def test_a_major_upgrade_whose_rebuild_failed_is_not_said_to_take_kernel_old(self):
        # P19: across a major release, the kernel in /boot now is no fall-back once the staged one is
        # applied - FreeBSD does not support the new release's userland on it.
        self.kernel('kernel', MAJOR_BEFORE)
        self.kernel('kernel.old', 'FreeBSD 14.3-RELEASE-p12 stable/26.1-n273900-0a1b2c3d4e5f SMP')
        self.module(MAJOR_BEFORE)
        done, _ = self.upgrade()
        log = self.read('log')
        self.assertEqual(done.returncode, 0)
        self.assertIn('once the staged kernel is applied, /boot/kernel.old is FreeBSD 14.3-RELEASE-p16, of '
                      'another major release than FreeBSD 15.1-RELEASE-p6; the next boot is not sent to it', log)
        self.assertNotIn('is the kernel this module is built for', log)
        self.assertEqual(self.read('nextboot.args'), '')

    def test_a_module_built_for_neither_kernel_is_still_said_to_be_refused(self):
        self.module(OLDEST)
        done, _ = self.upgrade()
        log = self.read('log')
        self.assertEqual(done.returncode, 0)
        self.assertIn('-t octep-follow -p daemon.err the next boot will refuse the module and the front ports '
                      'will not come up', log)
        self.assertNotIn('once the staged kernel is applied', log)
        self.assertEqual(self.read('nextboot.args'), '')

    def test_a_follower_that_does_not_finish_is_cut_off_at_the_time_limit(self):
        self.module(OLD)
        done, took = self.upgrade(held=True)
        log = self.read('log')
        self.assertEqual(done.returncode, 0)
        self.assertTrue(os.path.exists(self.path('waited')), 'the follower took no lock')
        self.assertLess(took, 10, 'the hook waited for the follower past its time limit')
        self.assertIn('-t octep -p daemon.err the module was not rebuilt within 1s for the kernel this '
                      'update installs; the update goes on', log)
        self.assertIn('-t octep -p daemon.err the reboot that follows tries again', log)
        # the run said nothing while it waited for the lock, and an empty line is not an account of it
        self.assertNotIn('-t octep -p daemon.err   \n', log)
        self.assertEqual(self.read('nextboot.args'), '')

    def test_a_module_already_built_for_the_staged_kernel_says_nothing(self):
        self.module(KERNEL)
        done, _ = self.upgrade()
        self.assertEqual(done.returncode, 0)
        self.assertEqual([line for line in self.read('log').splitlines() if line.startswith('-t octep -p')],
                         [])


class Install(StandIns):
    """The follower's last step, the module it built moved into place with its stamp, and a step of it
    that fails: the review's P15 (2026-11-06). The module and its stamp are two files; until then the
    module was moved and then its stamp, neither checked, and a failure between the two left the new
    module under the stamp of the kernel running - loaded on that kernel at its next boot, at the wrong
    offsets - or the reverse. The build is a stand-in for make that writes which kernel its module is
    for, and what a boot would load is bringup.sh's own decision (CHECK=1) on each of the two kernels:
    a module built for that kernel, or a refusal, never one built for the other."""

    def setUp(self):
        super().setUp()
        self.kernel('kernel.old', OLD)			# the kernel running; /boot/kernel is the new one
        self.module(OLD)
        self.built('modules/octep.ko', OLD)
        os.makedirs(self.path('sys/sys/amd64/include'))
        open(self.path('sys/sys/param.h'), 'w').close()
        os.makedirs(self.path('src'))
        for name in ('Makefile', 'octep.c', 'octep.h'):
            open(self.path('src/' + name), 'w').close()

    def built(self, name, version):
        with open(self.path(name), 'w') as f:
            f.write('ELF built for %s\n' % version)

    def follow(self, fails=None, force=False):
        """kernel-follow.sh built and installed for the kernel in /boot; with fails, the one move whose
        source ends so fails, as a rename does on a failing disk."""
        mv = self.stub('mv', 'case "$1" in *%s) echo "mv: rename $1 to $2: Input/output error" >&2; exit 1 ;; '
                             'esac\nexec /bin/mv "$@"' % (fails or '/nothing'))
        make = self.stub('make', 'ln -s "${1#SYSDIR=}/sys/amd64/include" machine\n: > octep.o\n'
                                 "echo 'ELF built for %s' > octep.ko" % KERNEL)
        script = self.copy(SCRIPT, 'kernel-follow.sh', (
            ('/usr/bin/logger', self.recorder('logger', 'log')),
            ('/sbin/sysctl', self.stub('sysctl', 'exit 1')),
            ('/usr/bin/make', make), ('/bin/mv', mv),
            ('/var/db/os-xgs-npu/build.log', self.path('build.log'))))
        env = self.env(OCTEP_FOLLOW_LOCKED='1', NOFETCH='1', UNAME=self.stub('uname', 'echo "%s"' % OLD),
                       NEXTBOOT=self.recorder('nextboot', 'nextboot.args'),
                       FETCH=self.stub('fetch-here.sh', 'echo "SYSDIR=%s"' % self.path('sys')))
        if force:
            env['FORCE'] = '1'
        return subprocess.run(['/bin/sh', script], env=env, capture_output=True, text=True, timeout=60)

    def loads(self, running):
        """What bringup.sh would load on a boot of `running`: the kernel its module is built for, or None
        for a refusal. Its decision is in its own log file: off a terminal, its lines go to logger."""
        bringup = self.copy(os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'octep', 'bringup.sh'),
                            'bringup.sh', (('/usr/bin/uname', self.stub('uname-boot', 'echo "%s"' % running)),
                                           ('/usr/bin/logger', self.stub('logger-boot', 'exit 0'))))
        log = self.path('bringup.log')
        if os.path.exists(log):
            os.unlink(log)
        env = dict(os.environ, MODULE=self.path('modules/octep.ko'), KLDSTAT=self.stub('kldstat', 'exit 1'),
                   LOGFILE=log, CHECK='1')
        done = subprocess.run(['/bin/sh', bringup], env=env, capture_output=True, text=True, timeout=60)
        if done.returncode != 0:
            return None
        loaded = self.read('bringup.log').strip().rsplit('CHECK=1: would load ', 1)[1]
        return self.read(os.path.relpath(loaded, self.dir)).strip()[len('ELF built for '):]

    def assert_no_boot_loads_another_kernels(self):
        for running in (OLD, KERNEL):
            self.assertIn(self.loads(running), (running, None),
                          'a boot of %s would load a module built for another kernel' % running)

    def test_the_module_and_its_stamp_go_in_together(self):
        done = self.follow()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((self.loads(KERNEL), self.loads(OLD)), (KERNEL, OLD))

    def test_a_stamp_that_cannot_follow_its_module_leaves_it_refused(self):
        done = self.follow(fails='.kernel.new')
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assert_no_boot_loads_another_kernels()
        # the kernel running keeps its ports, from the module kept for it
        self.assertEqual(self.loads(OLD), OLD)
        self.assertIn('its stamp could not be put beside it: it is stamped for no kernel', done.stderr)
        # and the next run, its moves working again, keeps that one: the module stamped for no kernel is
        # not made the module kept for the old one
        done = self.follow()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((self.loads(KERNEL), self.loads(OLD)), (KERNEL, OLD))

    def test_a_module_that_cannot_move_leaves_the_old_pair_as_it_was(self):
        done = self.follow(fails='octep.ko.new')
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assert_no_boot_loads_another_kernels()
        self.assertEqual(self.read('modules/octep.ko.kernel').strip(), OLD)
        self.assertIn('untouched and still matches ' + OLD, done.stderr)

    def test_a_stamp_that_cannot_be_written_touches_nothing(self):
        os.makedirs(self.path('modules/octep.ko.kernel.new'))	# a write to it fails
        done = self.follow()
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assert_no_boot_loads_another_kernels()
        self.assertEqual(self.read('modules/octep.ko').strip(), 'ELF built for ' + OLD)
        self.assertEqual(self.read('modules/octep.ko.kernel').strip(), OLD)

    def test_a_rebuild_for_the_same_kernel_is_never_refused_between_its_steps(self):
        """install.sh's FORCE=1, the module already built for the kernel in /boot: whatever step fails,
        the module in place is one built for it, under its stamp."""
        self.module(KERNEL)
        self.built('modules/octep.ko', KERNEL)
        for fails in ('.kernel.new', 'octep.ko.new'):
            done = self.follow(fails=fails, force=True)
            self.assertEqual(self.loads(KERNEL), KERNEL, fails + ': ' + done.stdout + done.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
