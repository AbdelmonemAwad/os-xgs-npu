#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The XGS 87's eMMC as the system disk, away from an XGS 87.

system-disk.sh, given what `mount -p`, kern.geom.conftxt and `zpool list -vHP` print: UFS on OPNsense's
gpt/rootfs label and ZFS on a gpt/ label, on the eMMC and on a USB disk, an encrypted partition, and a
root that lies on no disk.

kernel-follow.sh and the upgrade hook, with stand-ins, on a made-up /boot with /boot/kernel,
/boot/kernel.old and /boot/modules, after every run asking what the loader would load for the kernel the
next boot takes - the build in that kernel's own directory, or the one in /boot/modules - and whether
that build is the one for that kernel, as the module itself checks: an ordinary update that put a new
kernel in /boot (the fall-back armed first, the build being replaced kept in kernel.old, the fall-back
let go once fchemmc is built, and kept while it is not); the fall-back alone at a reboot, for fchemmc
where the module above is already built, and refused for the module above where kernel.old has no
fchemmc; an upgrade with its kernel staged (the running kernel's build kept in /boot/kernel, which the
kernel's update then renames to kernel.old, and the upgrade stopped where fchemmc is not built for the
staged kernel); a build left in /boot/kernel taken out once /boot/modules has the same; and every one of
those with the system disk not the eMMC, where nothing is put into a kernel's directory and nothing
differs from before.

image-fchemmc.sh: what an image build puts into an image root, and nothing where the build fails; and
what an install then does with it - kept where the controller is, taken away where it is not or where
the board is not one this project installs on, so that after the first boot an image root is what it
was before; and left where the running system's disk is the eMMC.

    python3 tools/test-emmc-root.py
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(HERE, '..')
FOLLOW = os.path.join(TOP, 'install', 'kernel-follow.sh')
UPGRADE = os.path.join(TOP, 'src', 'etc', 'rc.syshook.d', 'upgrade', '20-octep')
STOP = os.path.join(TOP, 'src', 'etc', 'rc.syshook.d', 'stop', '20-octep')
SYSTEMDISK = os.path.join(TOP, 'src', 'opnsense', 'scripts', 'xgs', 'system-disk.sh')
IMAGE = os.path.join(TOP, 'install', 'image-fchemmc.sh')
INSTALL = os.path.join(TOP, 'install', 'install.sh')
UNINSTALL = os.path.join(TOP, 'install', 'uninstall.sh')
SH = shutil.which('sh')

K0 = 'FreeBSD 15.1-RELEASE-p6 stable/26.7-n284176-c962f76493b6 SMP'
K1 = 'FreeBSD 15.1-RELEASE-p7 stable/26.7-n284300-2b4c6d8e0f1a SMP'
KM1 = 'FreeBSD 15.1-RELEASE-p4 stable/26.7-n284159-abbbe67a45e7 SMP'
OTHER = 'FreeBSD 15.1-RELEASE-p2 stable/26.7-n284102-0f1e2d3c4b5a SMP'

NPUEP_PCI = 'npuep0@pci0:3:0:0: class=0x058000 rev=0x00 hdr=0x00 vendor=0x11ab device=0x7080'
EMMC_PCI = 'none2@pci0:0:20:7: class=0x080501 rev=0x01 hdr=0x00 vendor=0x1022 device=0x7906'


class Tree(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def path(self, name):
        return os.path.join(self.dir, name)

    def write(self, name, text):
        os.makedirs(os.path.dirname(self.path(name)), exist_ok=True)
        with open(self.path(name), 'w', newline='\n') as f:
            f.write(text)
        return self.path(name)

    def read(self, name):
        try:
            with open(self.path(name)) as f:
                return f.read()
        except FileNotFoundError:
            return None

    def stub(self, name, body):
        self.write(name, '#!/bin/sh\n%s\n' % body)
        os.chmod(self.path(name), 0o755)
        return self.path(name)


# ---------------------------------------------------------------- system-disk.sh

CONFTXT = '''0 DISK mmcsd0 15758000128 512 hd 0 sc 0
1 PART mmcsd0p1 272629760 512 i 1 o 20480 ty efi xs GPT xt c12a7328-f81f-11d2-ba4b-00a0c93ec93b
2 LABEL gpt/efifs 272629760 512 i 0 o 0
1 PART mmcsd0p2 524288 512 i 2 o 272650240 ty freebsd-boot xs GPT xt 83bd6b9d-7f41-11dc-be0b-001560b84f0f
2 LABEL gpt/bootfs 524288 512 i 0 o 0
1 PART mmcsd0p3 13336248320 512 i 3 o 273174528 ty freebsd-ufs xs GPT xt 516e7cb6-6ecf-11d6-8ff8-00022d09712b
2 LABEL gpt/rootfs 13336248320 512 i 0 o 0
1 PART mmcsd0p4 2147483648 512 i 4 o 13609422848 ty freebsd-swap xs GPT xt 516e7cb5-6ecf-11d6-8ff8-00022d09712b
2 LABEL gpt/swapfs 2147483648 512 i 0 o 0
0 DISK mmcsd0boot0 4194304 512 hd 0 sc 0
0 DISK da0 31004295168 512 hd 255 sc 63
1 PART da0p1 209715200 512 i 1 o 20480 ty efi xs GPT xt c12a7328-f81f-11d2-ba4b-00a0c93ec93b
1 PART da0p3 30000000000 512 i 3 o 210239488 ty freebsd-ufs xs GPT xt 516e7cb6-6ecf-11d6-8ff8-00022d09712b
2 LABEL gpt/usbroot 30000000000 512 i 0 o 0
0 DISK nda0 64023257088 512 hd 0 sc 0
1 PART nda0p4 60000000000 512 i 4 o 2148532224 ty freebsd-zfs xs GPT xt 516e7cba-6ecf-11d6-8ff8-00022d09712b
2 LABEL gpt/zfs0 60000000000 512 i 0 o 0
0 DISK mmcsd1 7948206080 512 hd 0 sc 0
1 PART mmcsd1p4 7000000000 512 i 4 o 2148532224 ty freebsd-zfs xs GPT xt 516e7cba-6ecf-11d6-8ff8-00022d09712b
2 LABEL gpt/zfs1 7000000000 512 i 0 o 0
2 ELI mmcsd1p4.eli 7000000000 4096
'''


@unittest.skipIf(SH is None, 'no sh here')
class SystemDisk(Tree):
    """system-disk.sh with mount -p, kern.geom.conftxt and zpool list -vHP given as files."""

    def disk(self, mount, pool=None):
        self.write('mounts', mount + '\n/dev/gpt/efifs /boot/efi msdosfs rw 2 2\n')
        self.write('conftxt', CONFTXT)
        env = dict(os.environ, MOUNTS=self.path('mounts'), CONFTXT=self.path('conftxt'))
        if pool is not None:
            env['ZPOOLS'] = self.write('zpools', pool)
        listed = subprocess.run([SH, SYSTEMDISK], env=env, capture_output=True, text=True, timeout=30)
        asked = subprocess.run([SH, SYSTEMDISK, '-e'], env=env, capture_output=True, text=True, timeout=30)
        return listed.stdout.split(), asked.returncode

    def test_ufs_on_the_installers_rootfs_label_on_the_emmc(self):
        self.assertEqual(self.disk('/dev/gpt/rootfs / ufs rw 1 1'), (['mmcsd0'], 0))

    def test_ufs_on_the_emmc_partition_itself(self):
        self.assertEqual(self.disk('/dev/mmcsd0p3 / ufs rw 1 1'), (['mmcsd0'], 0))

    def test_ufs_on_a_usb_disk(self):
        self.assertEqual(self.disk('/dev/gpt/usbroot / ufs rw 1 1'), (['da0'], 1))

    def test_zfs_on_a_label_on_an_nvme_disk(self):
        pool = 'zroot\t59.5G\t1.2G\t58.3G\t-\t-\t0%\t2%\t1.00x\tONLINE\t-\n\t/dev/gpt/zfs0\t59.5G\t1.2G\t58.3G\t-\t-\t0%\t2.01%\t-\tONLINE\n'
        self.assertEqual(self.disk('zroot/ROOT/default / zfs rw,noatime,nfsv4acls 0 0', pool), (['nda0'], 1))

    def test_zfs_on_an_encrypted_emmc_partition(self):
        pool = 'zroot\t6.5G\t1.2G\t5.3G\t-\t-\t0%\t18%\t1.00x\tONLINE\t-\n\t/dev/mmcsd1p4.eli\t6.5G\t1.2G\t5.3G\t-\t-\t0%\t18%\t-\tONLINE\n'
        self.assertEqual(self.disk('zroot/ROOT/default / zfs rw,noatime,nfsv4acls 0 0', pool), (['mmcsd1'], 0))

    def test_a_root_on_no_disk_cannot_be_told(self):
        self.assertEqual(self.disk('tmpfs / tmpfs rw 0 0'), ([], 2))
        self.assertEqual(self.disk('/dev/md0.uzip / ufs ro 0 0'), ([], 2))


# ---------------------------------------------------------------- the follower and the upgrade hook

@unittest.skipIf(SH is None, 'no sh here')
class Boots(Tree):
    """A made-up /boot: /boot/kernel, /boot/kernel.old, /boot/modules with npuep.ko and fchemmc.ko, the
    sources of both, and a nextboot that remembers what it was asked."""

    def setUp(self):
        super().setUp()
        for d in ('boot/modules', 'boot/loader.conf.d', 'src', 'fsrc', 'sys/sys', 'sys/amd64/include'):
            os.makedirs(self.path(d), exist_ok=True)
        self.write('sys/sys/param.h', '')
        for name in ('Makefile', 'npuep.c'):
            self.write('src/' + name, '')
        for name in ('Makefile', 'fchemmc.c'):
            self.write('fsrc/' + name, '')

    def kernel(self, where, version):
        self.write('boot/%s/kernel' % where, '\0\0' + version + '\n\0')

    def npuep(self, stamp, prev=None):
        self.write('boot/modules/npuep.ko', 'ELF npuep for %s\n' % stamp)
        self.write('boot/modules/npuep.ko.kernel', stamp + '\n')
        if prev:
            self.write('boot/modules/npuep.ko.prev', 'ELF npuep for %s\n' % prev)
            self.write('boot/modules/npuep.ko.prev.kernel', prev + '\n')

    def fchemmc(self, built, where='modules'):
        self.write('boot/%s/fchemmc.ko' % where, 'ELF fchemmc for %s\n' % built)
        self.write('boot/%s/fchemmc.ko.kernel' % where, built + '\n')
        self.write('boot/loader.conf.d/fchemmc.conf', 'fchemmc_load="YES"\n')

    def staged(self, version):
        self.write('staged', '\0\0' + version + '\n\0')
        self.write('pending/kernel-26.7.9-amd64.txz', '')
        self.write('kernel.pending', '26.7.9\n')

    def apply_staged(self):
        """opnsense-update's install_kernel(): kernel.old removed, kernel renamed to it, the new one in."""
        shutil.rmtree(self.path('boot/kernel.old'), ignore_errors=True)
        os.rename(self.path('boot/kernel'), self.path('boot/kernel.old'))
        with open(self.path('staged')) as f:
            self.write('boot/kernel/kernel', f.read())
        shutil.rmtree(self.path('pending'))
        os.unlink(self.path('kernel.pending'))

    def new_boot(self):
        """What a reboot does to the follower's marks: /var/run is emptied, nextboot's one shot spent."""
        for name in ('pinned', 'armed', 'nextboot.state'):
            if os.path.exists(self.path(name)):
                os.unlink(self.path(name))

    def follower(self, fails=False):
        make = self.stub('make', 'ln -s "${1#SYSDIR=}/amd64/include" machine\n'
                                 'case "$*" in *FCHEMMC_KERNEL=*) %s ;; '
                                 '*) : > npuep.o; echo "ELF npuep" > npuep.ko ;; esac'
                         % ('echo "error: no" >&2; exit 1' if fails else
                            'echo "ELF fchemmc for ${2#FCHEMMC_KERNEL=}" > fchemmc.ko'))
        with open(FOLLOW, encoding='utf-8') as f:
            text = f.read()
        for command, stand_in in (('/usr/bin/logger', self.stub('logger', 'printf "%%s\\n" "$*" >> "%s"'
                                                                 % self.path('log'))),
                                  ('/sbin/sysctl', self.stub('sysctl', 'exit 1')),
                                  ('/usr/bin/make', make),
                                  ('/usr/bin/tar', self.stub('tar', 'cat "%s"' % self.path('staged'))),
                                  ('/var/db/os-xgs-npu/build.log', self.path('build.log'))):
            text = text.replace(command, stand_in)
        self.write('kernel-follow.sh', text)
        os.chmod(self.path('kernel-follow.sh'), 0o755)
        return self.path('kernel-follow.sh')

    def env(self, emmc, **more):
        env = dict(os.environ)
        for name in ('OCTEP_FOLLOW_LOCKED', 'PINONLY', 'FORCE', 'DRY', 'EMMCCHECK'):
            env.pop(name, None)
        nextboot = self.stub('nextboot', 'printf "%%s\\n" "$*" >> "%s"\ncase "$1" in -k) echo "$2" > "%s" ;; '
                                         '-D) rm -f "%s" ;; esac'
                             % (self.path('nextboot.args'), self.path('nextboot.state'),
                                self.path('nextboot.state')))
        env.update(OCTEP_FOLLOW_LOCKED='1', NOFETCH='1', DRIVER='npuep', EMMCROOT='1' if emmc else '0',
                   MODDIR=self.path('boot/modules'), KERNEL=self.path('boot/kernel/kernel'),
                   KERNEL_OLD=self.path('boot/kernel.old/kernel'), NEXTBOOT=nextboot,
                   PENDINGDIR=self.path('pending'), PENDING_KERNEL=self.path('kernel.pending'),
                   PINNED=self.path('pinned'), ARMED=self.path('armed'), SAID=self.path('said'),
                   LOCK=self.path('lock'), RESULT=self.path('result'), BUILDROOT=self.path('build'),
                   SRCTREE=self.path('src'),
                   FETCH=self.stub('fetch.sh', 'echo "SYSDIR=%s"' % self.path('sys')),
                   UNAME=self.stub('uname', 'echo "%s"' % K0),
                   FCHEMMC_SRCTREE=self.path('fsrc'), FCHEMMC_BUILDROOT=self.path('fbuild'),
                   FCHEMMC_BUILDLOG=self.path('fbuild.log'), FCHEMMC_SAID=self.path('fsaid'),
                   FCHEMMC_LOADER=self.path('boot/loader.conf.d/fchemmc.conf'))
        env.update(more)
        return env

    def follow(self, emmc, fails=False, **more):
        return subprocess.run([SH, self.follower(fails)], env=self.env(emmc, **more), capture_output=True,
                              text=True, timeout=60)

    def upgrade(self, emmc, fails=False):
        hook = self.read_swapped(UPGRADE, (('/usr/sbin/pciconf', self.stub('pciconf', "echo '%s'" % NPUEP_PCI)),
                                           ('/usr/bin/logger', self.stub('hooklogger', 'printf "%%s\\n" "$*" >> "%s"'
                                                                        % self.path('log')))))
        return subprocess.run([SH, hook], env=self.env(emmc, FOLLOW=self.follower(fails), LIMIT='30'),
                              capture_output=True, text=True, timeout=120)

    def read_swapped(self, source, swaps):
        with open(source, encoding='utf-8') as f:
            text = f.read()
        for command, stand_in in swaps:
            text = text.replace(command, stand_in)
        return self.write(os.path.basename(source) + '.hook', text)

    def nextboot(self):
        return (self.read('nextboot.args') or '').splitlines()

    # What the loader does: the next boot's kernel - kernel.old where nextboot sends it - and the
    # fchemmc.ko of that kernel's own directory, or /boot/modules' where it has none.
    def next_kernel(self):
        return 'kernel.old' if (self.read('nextboot.state') or '').strip() == 'kernel.old' else 'kernel'

    def loaded(self, where):
        own = self.read('boot/%s/fchemmc.ko' % where)
        return own if own is not None else self.read('boot/modules/fchemmc.ko')

    def finds_its_disk(self, where):
        version = re.search(r'FreeBSD [^\n\0]*', self.read('boot/%s/kernel' % where)).group(0)
        return self.loaded(where) == 'ELF fchemmc for %s\n' % version

    def assert_next_boot_finds_its_disk(self):
        where = self.next_kernel()
        self.assertTrue(self.finds_its_disk(where), 'the next boot takes /boot/%s, and the loader loads %r for it'
                        % (where, self.loaded(where)))

    def kernel_dirs_hold_no_fchemmc(self):
        for where in ('kernel', 'kernel.old'):
            if os.path.isdir(self.path('boot/' + where)):
                self.assertEqual([n for n in os.listdir(self.path('boot/' + where)) if 'fchemmc' in n], [])


class AfterAnUpdate(Boots):
    """An ordinary update: opnsense-update -bk put the new kernel in /boot/kernel and kernel.old is the one
    running; npuep and fchemmc are both built for kernel.old. The follower's tick, or its whole run at the
    reboot."""

    def setUp(self):
        super().setUp()
        self.kernel('kernel', K1)
        self.kernel('kernel.old', K0)
        self.npuep(K0)
        self.fchemmc(K0)

    def test_on_the_emmc_the_fall_back_is_armed_first_and_let_go_once_fchemmc_is_built(self):
        done = self.follow(emmc=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.nextboot(), ['-k kernel.old', '-D'])
        self.assertEqual(self.read('boot/modules/fchemmc.ko'), 'ELF fchemmc for %s\n' % K1)
        # the build it replaced, kept with the kernel it is for
        self.assertEqual(self.read('boot/kernel.old/fchemmc.ko'), 'ELF fchemmc for %s\n' % K0)
        self.assertEqual(self.read('boot/kernel.old/fchemmc.ko.kernel'), K0 + '\n')
        self.assertIsNone(self.read('boot/kernel/fchemmc.ko'))
        self.assertFalse(os.path.exists(self.path('armed')))
        self.assert_next_boot_finds_its_disk()
        self.assertTrue(self.finds_its_disk('kernel.old'), 'a boot of kernel.old from the boot menu')

    def test_on_the_emmc_a_failed_fchemmc_build_keeps_the_next_boot_on_kernel_old(self):
        done = self.follow(emmc=True, fails=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.read('boot/modules/npuep.ko.kernel'), K1 + '\n')
        self.assertEqual(self.nextboot(), ['-k kernel.old'], 'let go for npuep alone')
        self.assertTrue(os.path.exists(self.path('armed')))
        self.assertEqual(self.read('boot/modules/fchemmc.ko.kernel'), K0 + '\n')
        self.assert_next_boot_finds_its_disk()
        # And at every boot after it, until fchemmc is built: the one shot is armed again.
        self.new_boot()
        self.follow(emmc=True, fails=True)
        self.assertEqual(self.nextboot(), ['-k kernel.old', '-k kernel.old'])
        self.assert_next_boot_finds_its_disk()
        # Built at last (an install's FORCE=1): let go, and kernel.old keeps its own.
        self.follow(emmc=True, FORCE='1')
        self.assertEqual(self.nextboot()[-1], '-D')
        self.assert_next_boot_finds_its_disk()
        self.assertTrue(self.finds_its_disk('kernel.old'))

    def test_on_the_emmc_the_fall_back_is_armed_before_a_build_that_never_finishes(self):
        # npuep already rebuilt for the new kernel; fchemmc's build cut off - a reboot, or the stop hook's
        # time limit - before it is done.
        self.npuep(K1, prev=K0)
        follower = self.follower()
        self.stub('make', 'exec sleep 30')
        done = subprocess.run(['timeout', '3', SH, follower], env=self.env(True), capture_output=True,
                              text=True, timeout=60)
        self.assertEqual(done.returncode, 124)
        self.assertEqual(self.nextboot(), ['-k kernel.old'])
        self.assert_next_boot_finds_its_disk()

    def test_not_on_the_emmc_nothing_differs(self):
        done = self.follow(emmc=False, fails=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.nextboot(), ['-k kernel.old', '-D'], 'npuep alone decides, as before')
        self.kernel_dirs_hold_no_fchemmc()
        self.new_boot()
        self.follow(emmc=False, FORCE='1')
        self.assertEqual(self.read('boot/modules/fchemmc.ko.kernel'), K1 + '\n')
        self.kernel_dirs_hold_no_fchemmc()


class AtTheReboot(Boots):
    """The stop hook's PINONLY=1 run: /boot/kernel the new kernel, kernel.old the one running."""

    def setUp(self):
        super().setUp()
        self.kernel('kernel', K1)
        self.kernel('kernel.old', K0)

    def pin_only(self, emmc):
        return self.follow(emmc, PINONLY='1')

    def test_on_the_emmc_fchemmc_alone_sends_the_next_boot_to_kernel_old(self):
        self.npuep(K1, prev=K0)
        self.fchemmc(K0)
        done = self.pin_only(emmc=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.nextboot(), ['-k kernel.old'])
        self.assertTrue(os.path.exists(self.path('armed')))
        self.assertIn('the system disk is the eMMC, and fchemmc is not built for', self.read('log'))
        self.assert_next_boot_finds_its_disk()

    def test_not_on_the_emmc_the_module_built_for_the_new_kernel_arms_nothing(self):
        self.npuep(K1, prev=K0)
        self.fchemmc(K0)
        done = self.pin_only(emmc=False)
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertEqual(self.nextboot(), [])

    def test_on_the_emmc_no_fchemmc_for_either_kernel_is_said_as_an_error(self):
        self.npuep(K0)
        self.fchemmc(OTHER)
        done = self.pin_only(emmc=True)
        self.assertEqual(done.returncode, 1)
        self.assertEqual(self.nextboot(), [])
        self.assertIn('the next boot finds no system disk', done.stderr)

    def test_on_the_emmc_npuep_never_sends_the_next_boot_where_fchemmc_is_not(self):
        # fchemmc built for the new kernel, and no build of it kept for kernel.old; npuep built for kernel.old
        self.npuep(K0)
        self.fchemmc(K1)
        done = self.pin_only(emmc=True)
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertEqual(self.nextboot(), [])
        self.assertIn('would find no fchemmc built for it, so no system disk', done.stdout + self.read('log'))
        self.assert_next_boot_finds_its_disk()
        # Away from the eMMC, the fall-back for npuep, as before.
        done = self.pin_only(emmc=False)
        self.assertEqual(done.returncode, 0)
        self.assertEqual(self.nextboot(), ['-k kernel.old'])


class AtAnUpgrade(Boots):
    """OPNsense's upgrade.sh: the new kernel a staged set, /boot/kernel the one running, kernel.old the one
    before; the upgrade hook runs the follower, and the kernel is applied only where the hook succeeded."""

    def setUp(self):
        super().setUp()
        self.kernel('kernel', K0)
        self.kernel('kernel.old', KM1)
        self.staged(K1)
        self.npuep(K0)
        self.fchemmc(K0)

    def test_on_the_emmc_the_running_kernels_build_goes_with_it_to_kernel_old(self):
        done = self.upgrade(emmc=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.read('boot/modules/fchemmc.ko'), 'ELF fchemmc for %s\n' % K1)
        self.assertEqual(self.read('boot/kernel/fchemmc.ko'), 'ELF fchemmc for %s\n' % K0)
        self.apply_staged()
        self.assert_next_boot_finds_its_disk()
        self.assertTrue(self.finds_its_disk('kernel.old'), 'a boot of kernel.old after the upgrade')
        # The first tick on the new kernel leaves both as they are.
        self.follow(emmc=True)
        self.assertTrue(self.finds_its_disk('kernel'))
        self.assertTrue(self.finds_its_disk('kernel.old'))

    def test_on_the_emmc_an_upgrade_fchemmc_cannot_be_built_for_is_stopped(self):
        done = self.upgrade(emmc=True, fails=True)
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn('the system disk is the eMMC, and fchemmc is not built for the staged kernel:', done.stdout)
        self.assertIn('the upgrade is stopped before it is applied', done.stdout)
        self.assertIn('The system stays on the release it runs now.', done.stdout)
        self.assertEqual(self.read('boot/modules/fchemmc.ko.kernel'), K0 + '\n')
        # Not applied: the next boot is the kernel running now, with its fchemmc.
        self.assert_next_boot_finds_its_disk()

    def test_not_on_the_emmc_the_upgrade_goes_on(self):
        done = self.upgrade(emmc=False, fails=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertNotIn('stopped', done.stdout)
        self.kernel_dirs_hold_no_fchemmc()

    def test_the_check_holds_nothing_with_no_kernel_staged(self):
        shutil.rmtree(self.path('pending'))
        os.unlink(self.path('kernel.pending'))
        self.fchemmc(OTHER)
        done = self.follow(emmc=True, EMMCCHECK='1')
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)

    def test_a_build_left_in_boot_kernel_goes_once_boot_modules_has_the_same(self):
        # An upgrade stopped by another hook after this one kept the running kernel's build in /boot/kernel.
        self.upgrade(emmc=True)
        shutil.rmtree(self.path('pending'))
        os.unlink(self.path('kernel.pending'))
        self.assertIsNotNone(self.read('boot/kernel/fchemmc.ko'))
        self.assert_next_boot_finds_its_disk()
        done = self.follow(emmc=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.read('boot/modules/fchemmc.ko.kernel'), K0 + '\n')
        self.assertIsNone(self.read('boot/kernel/fchemmc.ko'))
        self.assert_next_boot_finds_its_disk()


class Hooks(unittest.TestCase):
    """The stop and upgrade hooks run where fchemmc is installed too, read."""

    def test_both_gates_know_the_emmc_controller(self):
        for hook in (STOP, UPGRADE):
            with open(hook, encoding='utf-8') as f:
                text = f.read()
            self.assertIn('chip=0x79061022|vendor=0x1022[[:space:]]+device=0x7906', text)
        with open(STOP, encoding='utf-8') as f:
            self.assertIn('[ ! -f "${MODDIR}/fchemmc.ko" ]', f.read())


# ---------------------------------------------------------------- the image

def tree_of(top):
    found = {}
    for base, dirs, files in os.walk(top):
        for name in files:
            p = os.path.join(base, name)
            with open(p, 'rb') as f:
                found[os.path.relpath(p, top)] = f.read()
        for name in dirs:
            found[os.path.relpath(os.path.join(base, name), top) + '/'] = None
    return found


@unittest.skipIf(SH is None, 'no sh here')
class Image(Tree):
    """image-fchemmc.sh put on an image root, then settle as install.sh runs it at the first boot."""

    def setUp(self):
        super().setUp()
        self.write('img/boot/kernel/kernel', '\0\0' + K0 + '\n    root@builder:/usr/obj/SMP\n\0')
        self.write('img/boot/modules/linker.hints', 'hints')
        os.makedirs(self.path('img/boot/loader.conf.d'))
        self.write('img/usr/local/share/os-xgs-npu/source/.image-build', 'build 1\n')
        self.write('sys/sys/param.h', '')
        os.makedirs(self.path('sys/amd64/include'))
        self.before = tree_of(self.path('img'))

    def make(self, fails=False, machine=None):
        return self.stub('make', 'printf "%%s\\n" "$*" >> "%s"\nln -s "%s" machine\n%s'
                         % (self.path('make.args'), machine or '${1#SYSDIR=}/amd64/include',
                            'exit 1' if fails else 'echo "ELF fchemmc for ${2#FCHEMMC_KERNEL=}" > fchemmc.ko'))

    def put(self, release='15.1-RELEASE-p6', sys=True, **make):
        env = dict(os.environ, MAKE=self.make(**make), TMPDIR=self.dir,
                   UNAME=self.stub('uname', 'echo "%s"' % release),
                   # FreeBSD's tar takes a member pattern as it is; GNU tar here is told to
                   TAR=self.stub('tar', 'exec tar --wildcards "$@"'))
        args = [SH, IMAGE, 'put', self.path('img')] + ([self.path('sys')] if sys else [])
        return subprocess.run(args, env=env, capture_output=True, text=True, timeout=60)

    def carry_sources(self, sha):
        """The codeload archive inject.sh puts into the image's kernel-sources pool."""
        top = self.path('archive/src-' + sha + 'fullsha')
        os.makedirs(os.path.join(top, 'sys', 'sys'))
        os.makedirs(os.path.join(top, 'sys', 'amd64', 'include'))
        os.makedirs(os.path.join(top, 'lib'))
        self.write('archive/src-%sfullsha/sys/sys/param.h' % sha, '')
        self.write('archive/src-%sfullsha/lib/libc.c' % sha, '')
        pool = self.path('img/usr/local/share/os-xgs-npu/kernel-sources')
        os.makedirs(pool, exist_ok=True)
        subprocess.run(['tar', '-czf', os.path.join(pool, 'src-%s.tar.gz' % sha), '-C', self.path('archive'),
                        'src-%sfullsha' % sha], check=True)
        self.before = tree_of(self.path('img'))

    def settle(self, pci='', emmc=False, *args):
        env = dict(os.environ, ROOT=self.path('img'), PCICONF=self.stub('pciconf', "echo '%s'" % pci),
                   SYSTEMDISK=self.stub('system-disk.sh', 'exit %d' % (0 if emmc else 1)))
        return subprocess.run([SH, IMAGE, 'settle'] + list(args), env=env, capture_output=True, text=True,
                              timeout=60)

    def img(self, name):
        return self.read('img/' + name)

    def test_put_builds_for_the_images_own_kernel_and_lays_it_all_down(self):
        done = self.put()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.read('make.args').splitlines(),
                         ['SYSDIR=%s FCHEMMC_KERNEL=%s' % (self.path('sys'), K0)])
        self.assertEqual(self.img('boot/modules/fchemmc.ko'), 'ELF fchemmc for %s\n' % K0)
        self.assertEqual(self.img('boot/modules/fchemmc.ko.kernel'), K0 + '\n')
        self.assertEqual(self.img('boot/loader.conf.d/fchemmc.conf'), 'fchemmc_load="YES"\n')
        self.assertEqual(self.img('usr/local/share/os-xgs-npu/fchemmc.image'), K0 + '\n')
        self.assertEqual(sorted(os.listdir(self.path('img/usr/local/share/os-xgs-npu/fchemmc'))),
                         sorted(n for n in os.listdir(os.path.join(TOP, 'contrib', 'fchemmc'))
                                if n == 'Makefile' or n.endswith(('.c', '.h'))))
        self.assertEqual(self.img('boot/modules/linker.hints'), 'hints', 'linker.hints is not touched')
        # and the build directory is gone
        self.assertEqual([n for n in os.listdir(self.dir) if n.startswith('fchemmc-image.')], [])

    def test_without_sys_it_builds_from_the_sources_the_image_carries(self):
        self.carry_sources('c962f76493b6')
        done = self.put(sys=False)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        args = self.read('make.args').split(' FCHEMMC_KERNEL=')
        self.assertTrue(args[0].startswith('SYSDIR=' + self.dir) and args[0].endswith('/kernel-src/sys'), args[0])
        self.assertEqual(self.img('boot/modules/fchemmc.ko'), 'ELF fchemmc for %s\n' % K0)
        # the extracted sources went with the build directory
        self.assertEqual([n for n in os.listdir(self.dir) if n.startswith('fchemmc-image.')], [])

    def test_without_sys_and_no_sources_for_its_kernel_nothing_goes_in(self):
        self.carry_sources('0f1e2d3c4b5a')
        done = self.put(sys=False)
        self.assertEqual(done.returncode, 1)
        self.assertIn('no kernel sources for c962f76493b6', done.stdout)
        self.assertEqual(tree_of(self.path('img')), self.before)

    def test_a_build_machine_of_another_major_release_puts_nothing_in(self):
        done = self.put(release='14.2-RELEASE-p3')
        self.assertEqual(done.returncode, 1)
        self.assertIn('build on a FreeBSD 15', done.stdout)
        self.assertIsNone(self.read('make.args'))
        self.assertEqual(tree_of(self.path('img')), self.before)

    def test_a_failed_build_puts_nothing_in(self):
        done = self.put(fails=True)
        self.assertEqual(done.returncode, 1)
        self.assertIn('the build failed', done.stdout)
        self.assertEqual(tree_of(self.path('img')), self.before)

    def test_machine_headers_from_another_tree_put_nothing_in(self):
        done = self.put(machine='/usr/include/machine')
        self.assertEqual(done.returncode, 1)
        self.assertIn('not from', done.stdout)
        self.assertEqual(tree_of(self.path('img')), self.before)

    def test_a_kernel_without_a_version_line_puts_nothing_in(self):
        self.write('img/boot/kernel/kernel', '\0\0nothing here\0')
        before = tree_of(self.path('img'))
        done = self.put()
        self.assertEqual(done.returncode, 1)
        self.assertEqual(tree_of(self.path('img')), before)

    def test_a_board_without_the_controller_is_as_before_after_its_first_boot(self):
        self.put()
        done = self.settle(NPUEP_PCI)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn('taken away: no 1022:7906', done.stdout)
        self.assertEqual(tree_of(self.path('img')), self.before)
        # and a second install says nothing and changes nothing
        again = self.settle(NPUEP_PCI)
        self.assertEqual(again.stdout, '')
        self.assertEqual(tree_of(self.path('img')), self.before)

    def test_a_board_this_project_does_not_install_on_is_as_before_even_with_the_controller(self):
        self.put()
        done = self.settle(EMMC_PCI, False, 'unknown-board')
        self.assertIn('taken away: this board is not one this project installs on', done.stdout)
        self.assertEqual(tree_of(self.path('img')), self.before)

    def test_an_xgs_87_keeps_it_and_the_plugin_takes_it_over(self):
        self.put()
        done = self.settle(NPUEP_PCI + "'\necho '" + EMMC_PCI)
        self.assertIn('kept: this machine has the eMMC controller', done.stdout)
        self.assertEqual(self.img('boot/modules/fchemmc.ko'), 'ELF fchemmc for %s\n' % K0)
        self.assertEqual(self.img('boot/loader.conf.d/fchemmc.conf'), 'fchemmc_load="YES"\n')
        self.assertIsNone(self.img('usr/local/share/os-xgs-npu/fchemmc.image'))

    def test_a_system_on_the_emmc_is_never_left_without_it(self):
        self.put()
        done = self.settle(NPUEP_PCI, True, 'unknown-board')
        self.assertIn('kept: the system disk is the eMMC', done.stdout)
        self.assertEqual(self.img('boot/modules/fchemmc.ko'), 'ELF fchemmc for %s\n' % K0)

    def test_a_build_that_is_not_the_images_is_not_taken(self):
        self.put()
        os.unlink(self.path('img/boot/modules/fchemmc.ko.kernel'))
        self.write('img/boot/modules/fchemmc.ko.kernel', K1 + '\n')
        self.settle(NPUEP_PCI)
        self.assertEqual(self.img('boot/modules/fchemmc.ko.kernel'), K1 + '\n')
        self.assertIsNone(self.img('usr/local/share/os-xgs-npu/fchemmc.image'))


class Wiring(unittest.TestCase):
    """install.sh and uninstall.sh, read."""

    def read(self, path):
        with open(path, encoding='utf-8') as f:
            return f.read()

    def test_install_settles_the_image_first_on_every_board(self):
        text = self.read(INSTALL)
        board = text.index('if ! BOARD=$(/bin/sh "${SRC}/src/opnsense/scripts/xgs/board.sh"); then')
        unknown = text.index('/bin/sh "${SRC}/install/image-fchemmc.sh" settle unknown-board || true')
        known = text.index('/bin/sh "${SRC}/install/image-fchemmc.sh" settle || true')
        self.assertLess(board, unknown)
        self.assertLess(unknown, text.index('exit 1', unknown))
        self.assertLess(text.index('exit 1', unknown), known)
        # before anything else is installed
        self.assertLess(known, text.index('install -d -m 0755 "${PREFIX}/opnsense/scripts/xgs"'))
        self.assertIn('install -m 0755 "${SRC}/src/opnsense/scripts/xgs/system-disk.sh" '
                      '"${PREFIX}/opnsense/scripts/xgs/system-disk.sh"', text)

    def test_uninstall_keeps_fchemmc_on_the_emmc_system_disk(self):
        text = self.read(UNINSTALL)
        m = re.search(r'system-disk\.sh" -e > /dev/null 2>&1; then\n(.*?)\nelse\n(.*?)\nfi\n', text, re.S)
        self.assertIsNotNone(m)
        self.assertNotIn('rm ', m.group(1))
        self.assertIn('/boot/modules/fchemmc.ko', m.group(2))
        self.assertIn('/boot/loader.conf.d/fchemmc.conf', m.group(2))
        self.assertIn('if [ -f /var/run/octep-follow.armed ] && [ "${EMMCROOT}" = 1 ]; then', text)
        self.assertIn('rm -f "${PREFIX}/opnsense/scripts/xgs/system-disk.sh"', text)


if __name__ == '__main__':
    unittest.main()
