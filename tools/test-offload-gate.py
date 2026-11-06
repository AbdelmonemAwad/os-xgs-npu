#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The XGS 136's offload gate after its coprocessor is reset under the running host - taken up again
in place by scripts/xgs/recover.sh, or by contrib/npuep/reload.sh (the review of 2026-11-06, A6). The
coprocessor forgets the gate at each of its boots, and until then:

  - the gate's write after the reset, the boot hook's settings (early/07-npuep settings), waited the
    boot's twenty seconds for the rpc channel, and no start hook came after it to write the gate
    again: a later channel left offloading off with the setting on. The driver itself gives its
    channel 120 seconds from its attach;
  - the word offload.sh kept from before the reset still read as this boot's to status.py: the page
    said the gate was on while the coprocessor had it off.

Each script runs as a copy whose paths point at stand-ins - the driver's sysctls, ifconfig, pciconf,
the loader, the reset bridge - under /bin/sh, so on Linux. sleep is a stand-in that does not sleep,
so a second of the scripts' is one read of the channel here. What the page would show is status.py's
own reader's answer. Nothing here comes from an appliance.

    python3 tools/test-offload-gate.py
"""
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EARLY = os.path.join(ROOT, 'src', 'etc', 'rc.syshook.d', 'early', '07-npuep')
OFFLOAD = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'offload.sh')
RECOVER = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'recover.sh')
RELOAD = os.path.join(ROOT, 'contrib', 'npuep', 'reload.sh')
STATUS = os.path.join(ROOT, 'src', 'opnsense', 'scripts', 'xgs', 'status.py')
POSIX = os.path.exists('/bin/sh') and sys.platform != 'win32'
BEFORE = '0123456789abcdef'	# the token of the coprocessor's boot before the reset
NEVER = 1000			# a channel that does not open while anything here waits


@unittest.skipUnless(POSIX, 'the scripts run under /bin/sh')
class Appliance(unittest.TestCase):
    """A made-up XGS 136 in a temporary directory, its coprocessor having accepted fw_cfg 0x3 - the gate
    on, Cut in hardware off - before the reset, kept as offload.sh keeps it with that boot's token. The
    settings page now says the gate on and Cut in hardware on: 0x7 is what a write after the reset
    gives it."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = self.dir = self.tmp.name
        self.bin = os.path.join(d, 'bin')
        os.makedirs(self.bin)
        os.makedirs(os.path.join(d, 'scripts', 'xgs'))
        self.token = os.path.join(d, 'xgs-npuep-boot')
        self.state = os.path.join(d, 'offload-fw_cfg')
        self.write('xgs-npuep-boot', BEFORE)
        self.write('offload-fw_cfg', BEFORE + ' 3')
        self.write('polls', '0')
        self.channel_opens_after(0)
        self.write('pulse', '0')
        self.write('config-space', '708011ab')
        for name in ('calls', 'logged', 'commands'):
            self.write(name, None)

        # The driver's sysctls: the rpc channel's state says "not open" for open-after reads, then
        # "open"; a command written by hand is answered rc 0, as the driver words it.
        self.script(os.path.join(self.bin, 'sysctl'), r'''D="%s"
if [ "$1" = -n ]; then
	case $2 in
	dev.npuep.0.rpc_channel.state)
		n=$(($(cat "$D/polls") + 1))
		echo $n > "$D/polls"
		if [ $n -gt "$(cat "$D/open-after")" ]; then word=open; else word='not open'; fi
		printf '%%s  magic 0x00000000  revision 351  hi_rings 1  reconfig_done 1\n' "$word"
		printf '  low ring:  posted 0 done 0 (this driver has posted 0)\n' ;;
	dev.npuep.0.rpc_channel.command) cat "$D/answer" 2>/dev/null || echo 'nothing sent yet' ;;
	dev.npuep.0.nwa.macs_from_agent) echo 14 ;;
	dev.npuep.0.gone) echo 1 ;;
	dev.npuep.0.heartbeat.state) echo alive ;;
	*) exit 1 ;;
	esac
	exit 0
fi
for a; do
	case $a in
	dev.npuep.0.rpc_channel.command=*)
		echo "${a#*=}" >> "$D/commands"
		echo 'command 2, 4 payload bytes -> answered, rc 0, 0 bytes back:' > "$D/answer" ;;
	esac
done
exit 0''' % d)
        self.script(os.path.join(self.bin, 'ifconfig'),
                    "case $1 in\nnpup1|mvmgmt0) exit 0 ;;\n-l) echo 'npup1 npup2 mvmgmt0' ;;\n-g) ;;\n"
                    "*) exit 1 ;;\nesac")
        self.script(os.path.join(self.bin, 'pciconf'),
                    "case $1 in\n-l) printf 'npuep0@pci0:1:0:0:\\tclass=0x020000 card=0x00000000 "
                    "chip=0x708011ab rev=0x00 hdr=0x00\\n' ;;\n-r) cat \"%s\" ;;\nesac"
                    % os.path.join(d, 'config-space'))
        self.script(os.path.join(self.bin, 'logger'),
                    'for m in "$@"; do :; done\necho "$m" >> "%s"' % os.path.join(d, 'logged'))
        self.script(os.path.join(self.bin, 'sleep'), 'exit 0')
        self.script(os.path.join(self.bin, 'php'), 'echo 0')
        self.script(os.path.join(self.bin, 'config'), 'echo 1')	# offload.sh's reader: the gate on
        self.script(os.path.join(self.bin, 'uname'), 'echo "FreeBSD 15.0 a made-up kernel"')
        self.script(os.path.join(d, 'scripts', 'xgs', 'board.sh'), "echo '201 armada'")
        # the reset bridge, mcp2210.py run by python3: its answer is the pulse file's
        self.stub('python3', 'exit $(cat "%s")' % os.path.join(d, 'pulse'))
        self.stub('kldstat', 'case "$*" in *npuep.ko.prev*) exit 1 ;; esac')
        self.stub('kldstat-none', 'exit 1')
        for name in ('kldunload', 'kldload', 'configctl', 'daemon', 'dmesg', 'kenv'):
            self.stub(name)
        self.stub('rc.reload_all', where=d)
        self.env = dict(os.environ, PATH=self.bin + os.pathsep + '/usr/bin:/bin', XGS_RECOVER_LOCKED='1',
                        STATE=self.state, BOOT_TOKEN=self.token, CONFIG_READER=os.path.join(self.bin, 'config'),
                        XGS_CUT_HW='1', XGS_COPIES='0')

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, name, text):
        with open(os.path.join(self.dir, name), 'w', encoding='utf-8', newline='\n') as f:
            if text is not None:
                f.write(text + '\n')

    def read(self, name):
        with open(os.path.join(self.dir, name), encoding='utf-8') as f:
            return f.read().strip()

    def lines(self, name):
        return self.read(name).splitlines()

    def channel_opens_after(self, reads):
        self.write('open-after', str(reads))

    def script(self, path, body):
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('#!/bin/sh\n%s\n' % body)
        os.chmod(path, 0o755)
        return path

    def stub(self, name, body='exit 0', where=None):
        """A command that writes down each call, with the token as it reads at that moment."""
        return self.script(os.path.join(where or self.bin, name),
                           'echo "%s $* [$(cat "%s" 2>/dev/null)]" >> "%s"\n%s'
                           % (name, self.token, os.path.join(self.dir, 'calls'), body))

    def token_at(self, name):
        """The token as it read when name was first called, or None if it was not."""
        for line in self.lines('calls'):
            if line.split(' ', 1)[0] == name:
                return re.search(r'\[([^\]]*)\]$', line).group(1)
        return None

    def copy(self, src, name, stands, optional=()):
        """src with each path in stands pointing at its stand-in; those in optional need not be there."""
        with open(src, encoding='utf-8') as f:
            text = f.read()
        for real, fake in stands:
            self.assertIn(real, text, 'nothing in %s to stand in for: %s' % (src, real))
            text = text.replace(real, fake)
        for real, fake in optional:
            text = text.replace(real, fake)
        return self.script(os.path.join(self.dir, name), text.split('\n', 1)[1])

    def offload(self):
        """offload.sh itself: the gate written by hand on the channel, its word kept with the token."""
        return self.copy(OFFLOAD, 'offload.sh', [('/sbin/sysctl', os.path.join(self.bin, 'sysctl')),
                                                 ('/usr/bin/logger', os.path.join(self.bin, 'logger'))])

    def early(self, offload):
        """The boot hook, early/07-npuep, writing the gate with offload."""
        d, b = self.dir, self.bin
        return self.copy(EARLY, '07-npuep', [
            ('/sbin/sysctl', os.path.join(b, 'sysctl')), ('/sbin/ifconfig', os.path.join(b, 'ifconfig')),
            ('/usr/bin/logger', os.path.join(b, 'logger')), ('/usr/local/bin/php', os.path.join(b, 'php')),
            ('/usr/bin/uname', os.path.join(b, 'uname')), ('/bin/kenv', os.path.join(b, 'kenv')),
            ('/usr/local/opnsense/scripts/xgs/offload.sh', offload),
            ('/usr/local/opnsense/scripts/xgs/flow_exclude.php', os.path.join(d, 'not-there.php')),
            ('/usr/local/opnsense/scripts/xgs/tunables-kenv.sh', os.path.join(d, 'not-there.sh')),
            ('/var/run/os-xgs-npu.oxips0', os.path.join(d, 'oxips0')),
            ('/var/db/os-xgs-npu', os.path.join(d, 'db'))])

    def run_sh(self, script, *args, **env):
        done = subprocess.run(['/bin/sh', script] + list(args), capture_output=True, text=True,
                              errors='replace', timeout=120, env=dict(self.env, **env))
        return done

    def shown(self):
        """What status.py's own reader makes of the word kept and the token: the word, or None."""
        spec = importlib.util.spec_from_file_location('xgs_status', STATUS)
        status = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(status)
        status.NPUEP_BOOT_TOKEN, status.NPUEP_FW_CFG_STATE = self.token, self.state
        return status.npuep_fw_cfg_written()


class GateWait(Appliance):
    """early/07-npuep's wait for the rpc channel before it writes the gate, offload.sh a stand-in that
    says whether the channel was open when it was asked."""

    def setUp(self):
        super().setUp()
        self.answer = self.stub('offload', 'if [ "$(cat "%s")" -gt "$(cat "%s")" ]; then echo \'{"applied":true}\'; '
                                'else echo \'{"applied":false}\'; fi'
                                % (os.path.join(self.dir, 'polls'), os.path.join(self.dir, 'open-after')))

    def gate(self, *args, **env):
        done = self.run_sh(self.early(self.answer), *args, **env)
        self.assertEqual(done.returncode, 0, done.stderr)
        return [line for line in self.lines('logged') if line.startswith('offload gate after')]

    def test_after_a_reset_a_late_channel_is_waited_for(self):
        """Until 2026-11-06 the settings wrote the gate after twenty seconds, the channel still shut."""
        self.channel_opens_after(30)
        self.assertEqual(self.gate('settings'), ['offload gate after 30s: {"applied":true}'])

    def test_after_a_reset_the_wait_is_the_drivers_own(self):
        """The driver leaves its channel shut 120 seconds after its attach: no longer than that."""
        self.channel_opens_after(NEVER)
        self.assertEqual(self.gate('settings'), ['offload gate after 120s: {"applied":false}'])
        self.assertEqual(self.read('polls'), '120')

    def test_at_boot_the_wait_stays_twenty_seconds(self):
        """The boot hook holds up the interfaces' configuration, and the start hook writes the gate again."""
        module = os.path.join(self.dir, 'npuep.ko')
        self.write('npuep.ko', '')
        self.write('npuep.ko.kernel', 'FreeBSD 15.0 a made-up kernel')
        self.channel_opens_after(NEVER)
        self.assertEqual(self.gate(MODULE=module, KLDSTAT=os.path.join(self.bin, 'kldstat-none'),
                                   KLDLOAD=os.path.join(self.bin, 'kldload')),
                         ['offload gate after 20s: {"applied":false}'])
        self.assertEqual(self.read('polls'), '20')


class Recovery(Appliance):
    """scripts/xgs/recover.sh taking the XGS 136's coprocessor up again in place."""

    def recover(self, hook):
        d = self.dir
        script = self.copy(RECOVER, 'recover.sh', [
            ('PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin',
             'PATH=%s:/usr/bin:/bin' % self.bin),
            ('LIB=/usr/local/opnsense/scripts', 'LIB=%s' % os.path.join(d, 'scripts')),
            ('CON=/var/log/xgs/npu-console.log', 'CON=%s' % os.path.join(d, 'npu-console.log')),
            ('LOCK=/var/run/xgs-recover.lock', 'LOCK=%s' % os.path.join(d, 'recover.lock')),
            ('/var/db/os-xgs-npu/switch-jumbo.state', os.path.join(d, 'switch-jumbo.state')),
            ('/usr/local/etc/rc.syshook.d/early/07-npuep', hook),
            ('/usr/local/etc/rc.reload_all', os.path.join(d, 'rc.reload_all'))],
            # the token's path is not asked for: a recover.sh without one is what these are against
            optional=[('/var/run/xgs-npuep-boot', self.token)])
        return self.run_sh(script)

    def test_the_gate_after_a_late_channel_is_what_is_shown(self):
        """The whole recovery, the boot hook and offload.sh themselves, the channel open thirty seconds
        after the ports: the gate written again, kept with the coprocessor's new boot, and shown."""
        self.channel_opens_after(30)
        done = self.recover(self.early(self.offload()))
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        token = self.read('xgs-npuep-boot')
        self.assertRegex(token, '^[0-9a-f]{16}$')
        self.assertNotEqual(token, BEFORE)
        self.assertEqual(self.token_at('kldload'), token, 'made again before the driver was loaded again')
        self.assertEqual(self.lines('commands'), ['02 07 00 00 00'])
        self.assertEqual(self.shown(), 7)

    def test_a_channel_that_never_opens_shows_no_word(self):
        """The gate not written after the reset: the page shows none, not the one from before it."""
        self.channel_opens_after(NEVER)
        done = self.recover(self.early(self.offload()))
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.lines('commands'), [])
        self.assertIn('gate not written: the rpc channel to the coprocessor is not open yet, so the gate '
                      'cannot be written', self.lines('logged'))
        self.assertIsNone(self.shown())

    def test_a_recovery_that_fails_after_the_reset_shows_no_word(self):
        self.write('config-space', 'ffffffff')
        done = self.recover(self.early(self.offload()))
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn('never answered its config space', done.stdout)
        self.assertIsNone(self.shown())

    def test_a_reset_refused_keeps_the_word(self):
        """Nothing was reset (#1050): the coprocessor still holds what it accepted."""
        self.write('pulse', '2')
        done = self.recover(self.early(self.offload()))
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn('not reset', done.stdout)
        self.assertEqual(self.read('xgs-npuep-boot'), BEFORE)
        self.assertEqual(self.shown(), 3)


class Reload(Appliance):
    """contrib/npuep/reload.sh: the module unloaded, the coprocessor reset, the module loaded again."""

    def reload(self):
        """A copy of reload.sh in the test's directory, its installed module there too, under boot/."""
        d = self.dir
        os.makedirs(os.path.join(d, 'boot'), exist_ok=True)
        return self.copy(RELOAD, 'reload.sh', [
            ('/usr/local/opnsense/scripts/npuctl', os.path.join(d, 'scripts', 'npuctl')),
            ('/usr/local/etc/rc.syshook.d/early/07-npuep', self.early(self.offload())),
            ('/usr/local/bin/php', os.path.join(self.bin, 'php')),
            ('/usr/local/opnsense/scripts/xgs', os.path.join(d, 'scripts', 'xgs')),
            ('/boot/modules/npuep.ko', os.path.join(d, 'boot', 'npuep.ko')),
            ('/usr/bin/uname', os.path.join(self.bin, 'uname'))])

    def loaded(self):
        """What kldload was given, each call."""
        return [line.split()[1] for line in self.lines('calls') if line.startswith('kldload ')]

    def test_the_gate_after_a_late_channel_is_what_is_shown(self):
        self.channel_opens_after(30)
        script = self.reload()
        self.write('npuep.ko', '')		# beside the script, as in a build directory
        done = self.run_sh(script)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        token = self.read('xgs-npuep-boot')
        self.assertNotEqual(token, BEFORE)
        self.assertEqual(self.token_at('kldload'), token, 'made again before the module was loaded again')
        self.assertEqual(self.lines('commands'), ['02 07 00 00 00'])
        self.assertEqual(self.shown(), 7)
        self.assertEqual(self.loaded(), [os.path.join(self.dir, 'npuep.ko')])

    def test_with_no_module_to_load_again_nothing_is_unloaded_or_reset(self):
        """#1072, A15 (2026-11-06): run from the plugin's source tree, as verify.sh told an owner to, it
        unloaded the driver and reset the coprocessor, found no module beside it, and left the XGS 136
        without its front ports until a restart."""
        script = self.reload()
        done = self.run_sh(script)
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn('nothing is unloaded and the coprocessor is not reset', done.stdout)
        called = [line.split(' ', 1)[0] for line in self.lines('calls')] if os.path.exists(
            os.path.join(self.dir, 'calls')) else []
        self.assertNotIn('kldunload', called)
        self.assertNotIn('python3', called, 'the reset asked for')

    def test_from_the_source_tree_the_installed_module_is_loaded_again(self):
        script = self.reload()
        self.write('boot/npuep.ko', '')
        self.write('boot/npuep.ko.kernel', 'FreeBSD 15.0 a made-up kernel')
        done = self.run_sh(script)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.loaded(), [os.path.join(self.dir, 'boot', 'npuep.ko')])

    def test_the_installed_prev_where_it_is_the_one_for_this_kernel(self):
        """As the boot hook chooses: npuep.ko built for another kernel, npuep.ko.prev for this one."""
        script = self.reload()
        self.write('boot/npuep.ko', '')
        self.write('boot/npuep.ko.kernel', 'FreeBSD 15.1 the kernel an update brought')
        self.write('boot/npuep.ko.prev', '')
        self.write('boot/npuep.ko.prev.kernel', 'FreeBSD 15.0 a made-up kernel')
        done = self.run_sh(script)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.loaded(), [os.path.join(self.dir, 'boot', 'npuep.ko.prev')])


if __name__ == '__main__':
    sys.exit(0 if unittest.main(exit=False).result.wasSuccessful() else 1)
