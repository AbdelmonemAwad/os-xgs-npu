#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""mgmt-link.sh coprocessor away from the appliance: how often it tells the coprocessor its address,
through what, and when it stops (#1010); and mgmt-link.sh trust, which gives the coprocessor this
host's key the same way. npu-run.sh, ping, ifconfig, logger, board.sh, sleep and ssh are stand-ins
that keep what they were asked, in a copy of the script whose paths point at them.

And mgmt-link.sh early, from the ARMADA's early boot hook (the review's A4): the link assigned before
OPNsense configures its interfaces where it is there and nothing carries it, and nothing done anywhere
else - nothing reconfigured or reloaded, nothing on the XGS 3300. mgmt-link.php is a stand-in that
keeps the assignment in a file, so that a second run is the boot after.

    python3 tools/test-mgmt-link.py
"""
import json
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'npuctl', 'mgmt-link.sh')
ADDRESS = 'ip addr replace 169.254.254.2/30 dev mvmgmt0; ip link set mvmgmt0 up'


class Coprocessor(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.calls = os.path.join(self.dir, 'calls')
        open(self.calls, 'w').close()

    def tearDown(self):
        self.tmp.cleanup()

    def stub(self, name, body):
        path = os.path.join(self.dir, name)
        with open(path, 'w', newline='\n') as f:
            f.write('#!/bin/sh\necho "%s $*" >> "%s"\n%s\n' % (name, self.calls, body))
        os.chmod(path, 0o755)
        return path

    def nth(self, name):
        return '$(grep -c "^%s " "%s")' % (name, self.calls)

    def run_script(self, ping_from=None, busy=0, cmd='coprocessor'):
        """ping answers from its ping_from-th call (None: never); npu-run.sh refuses (75) its first
        `busy` calls."""
        ping = 'exit 1' if ping_from is None else '[ %s -ge %d ] && exit 0; exit 1' % (self.nth('ping'), ping_from)
        stubs = {'/usr/local/opnsense/scripts/xgs/board.sh': self.stub('board', 'echo "201 armada"'),
                 '/usr/bin/logger': self.stub('logger', 'exit 0'),
                 '/sbin/ifconfig': self.stub('ifconfig', 'exit 0'),
                 '/sbin/ping': self.stub('ping', ping)}
        npu = self.stub('npu-run', 'echo "npu-run-env NO_LINK=${NPU_RUN_NO_LINK:-} WAIT=${NPU_RUN_WAIT:-}" >> "%s"\n'
                                   '[ %s -le %d ] && exit 75; exit 0' % (self.calls, self.nth('npu-run'), busy))
        self.stub('sleep', 'exit 0')
        self.stub('ssh', 'exit 0')
        with open(SCRIPT, encoding='utf-8') as f:
            text = f.read()
        for real, fake in stubs.items():
            text = text.replace(real, fake)
        script = os.path.join(self.dir, 'mgmt-link.sh')
        with open(script, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        # a key already made, so that trust has one to give
        key = os.path.join(self.dir, 'id_ed25519_npu')
        with open(key, 'w', newline='\n') as f:
            f.write('not a key\n')
        with open(key + '.pub', 'w', newline='\n') as f:
            f.write('ssh-ed25519 TESTKEYBODY xgs-host-to-npu\n')
        env = dict(os.environ, PATH=self.dir + os.pathsep + os.environ.get('PATH', ''),
                   MGMT_LINK_HELD='1', CONSOLE='/dev/null', NPU_RUN=npu, START_WAIT='300', KEY=key)
        done = subprocess.run(['/bin/sh', script, cmd], env=env, capture_output=True,
                              text=True, timeout=60)
        with open(self.calls) as f:
            return done, f.read().splitlines()

    @staticmethod
    def tells(calls):
        return [c for c in calls if c.startswith('npu-run ')]

    def test_a_link_that_answers_is_not_told(self):
        done, calls = self.run_script(ping_from=1)
        self.assertEqual(done.returncode, 0)
        self.assertEqual(self.tells(calls), [])
        self.assertIn('the coprocessor answers at 169.254.254.2', done.stdout)

    def test_the_address_goes_through_npu_run_never_over_the_link(self):
        done, calls = self.run_script(ping_from=2)
        self.assertEqual(self.tells(calls), ['npu-run 5 ' + ADDRESS])
        self.assertIn('npu-run-env NO_LINK=1 WAIT=30', calls)
        self.assertIn('the coprocessor answers at 169.254.254.2', done.stdout)

    def test_the_address_waits_for_the_console_behind_a_look(self):
        """#1018: a tell refused after npu-run.sh's 5 s came back 15 s later, to the next look of
        feeder.py; it waits up to 30 s for the console's lock now."""
        done, calls = self.run_script(ping_from=2)
        waits = [c.split('WAIT=')[1] for c in calls if c.startswith('npu-run-env ')]
        self.assertEqual(waits, ['30'])

    def test_a_console_held_by_another_caller_is_asked_again(self):
        done, calls = self.run_script(ping_from=5, busy=3)
        self.assertEqual(len(self.tells(calls)), 4)
        self.assertIn('answers at', done.stdout)

    def test_a_link_that_never_answers_is_told_for_about_five_minutes_then_left(self):
        done, calls = self.run_script(ping_from=None)
        self.assertEqual(done.returncode, 0)
        self.assertEqual(len(self.tells(calls)), 300 // 30 + 1)
        self.assertLessEqual(sum(int(c.split()[1]) for c in calls if c.startswith('sleep ')), 300)
        self.assertIn('does not answer at 169.254.254.2 after about 300 s', done.stdout)

    def test_the_key_waits_for_the_console_behind_a_look(self):
        """trust runs when the link's ssh is refused, and every look of feeder.py goes over the console
        then as well: it waits for the console's lock as long as the address does, not npu-run.sh's
        5 s (A11)."""
        done, calls = self.run_script(cmd='trust')
        self.assertEqual(done.returncode, 0)
        tells = self.tells(calls)
        self.assertEqual(len(tells), 1)
        self.assertTrue(tells[0].startswith("npu-run 15 xgs-remount.sh rw; grep -qF 'TESTKEYBODY' "))
        self.assertIn('npu-run-env NO_LINK=1 WAIT=30', calls)
        self.assertIn('the coprocessor trusts', done.stdout)

    def test_the_key_is_asked_again_while_the_console_is_held(self):
        done, calls = self.run_script(cmd='trust', busy=2)
        self.assertEqual(done.returncode, 0)
        waits = [c.split('WAIT=')[1] for c in calls if c.startswith('npu-run-env ')]
        self.assertEqual(waits, ['30', '30', '30'])
        self.assertIn('the coprocessor trusts', done.stdout)

    def test_nothing_is_written_to_the_console_by_the_script_itself(self):
        with open(SCRIPT, encoding='utf-8') as f:
            self.assertNotIn('> "${CONSOLE}"', f.read())


ASSIGN = '/usr/local/opnsense/scripts/npuctl/mgmt-link.php'
SHOW = 'php %s show mvmgmt0' % ASSIGN
WRITE = 'php %s assign 169.254.254.1 30 mvmgmt0' % ASSIGN


class Early(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.calls = os.path.join(self.dir, 'calls')
        open(self.calls, 'w').close()
        # what the stand-in configuration carries for the link: the line `show` prints, or no file
        self.config = os.path.join(self.dir, 'assigned')
        # what the script last saw of NPUMGMT (THE OWNER'S CHOICE KEPT, #1072): config.xml's, here a file
        self.state = os.path.join(self.dir, 'npumgmt.state')

    def tearDown(self):
        self.tmp.cleanup()

    def stub(self, name, body):
        path = os.path.join(self.dir, name)
        with open(path, 'w', newline='\n') as f:
            f.write('#!/bin/sh\necho "%s $*" >> "%s"\n%s\n' % (name, self.calls, body))
        os.chmod(path, 0o755)
        return path

    def run_early(self, board='201 armada', link=True, show=None, assign=None):
        """board: what board.sh prints; link: whether the driver made the link; show and assign: the
        stand-in mgmt-link.php's answer to each, in place of reading and writing its file."""
        show = show or '[ -f "%s" ] && cat "%s" || echo none' % (self.config, self.config)
        assign = assign or 'echo "opt15 NPUMGMT $3/$4" > "%s"; echo opt15' % self.config
        state = '[ -f "%s" ] && cat "%s" || echo none' % (self.state, self.state)
        mark = 'shift 2; echo "$*" > "%s"' % self.state
        php = self.stub('php', 'case "$2" in\nshow) %s ;;\nassign) %s ;;\nstate) %s ;;\nmark) %s ;;\nesac'
                        % (show, assign, state, mark))
        stubs = {'/usr/local/opnsense/scripts/xgs/board.sh': self.stub('board', 'echo "%s"' % board),
                 '/usr/bin/logger': self.stub('logger', 'exit 0'),
                 '/sbin/ifconfig': self.stub('ifconfig', 'exit %d' % (0 if link else 1)),
                 '/usr/local/sbin/configctl': self.stub('configctl', 'exit 0'),
                 '/sbin/ping': self.stub('ping', 'exit 0')}
        with open(SCRIPT, encoding='utf-8') as f:
            text = f.read()
        for real, fake in stubs.items():
            text = text.replace(real, fake)
        script = os.path.join(self.dir, 'mgmt-link.sh')
        with open(script, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        env = dict(os.environ, PHP=php, CONSOLE='/dev/null')
        done = subprocess.run(['/bin/sh', script, 'early'], env=env, capture_output=True, text=True,
                              timeout=60)
        with open(self.calls) as f:
            calls = f.read().splitlines()
        open(self.calls, 'w').close()
        return done, calls

    @staticmethod
    def of(calls, name):
        # the record's own state and mark calls are not counted where a test counts the assignment's
        return [c for c in calls if c.split()[0] == name and not (name == 'php' and len(c.split()) > 2
                                                                    and c.split()[2] in ('state', 'mark'))]

    def test_a_link_with_no_assignment_is_assigned_and_nothing_else_is_done(self):
        done, calls = self.run_early()
        self.assertEqual(done.returncode, 0, done.stderr)
        # read, written once, and read again to say what was written
        self.assertEqual(self.of(calls, 'php'), [SHOW, WRITE, SHOW])
        # the link is only looked for: 07-npuep brought it up, and rc.bootup configures it and the rules
        self.assertEqual(self.of(calls, 'ifconfig'), ['ifconfig mvmgmt0'])
        self.assertEqual(self.of(calls, 'configctl'), [])
        self.assertEqual(self.of(calls, 'ping'), [])
        self.assertIn('mgmt-link: mvmgmt0 is interface opt15 (NPUMGMT), 169.254.254.1/30', done.stdout)

    def test_the_boot_after_reads_and_writes_nothing(self):
        self.run_early()
        done, calls = self.run_early()
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.of(calls, 'php'), [SHOW])
        self.assertEqual(done.stdout, '')

    def test_an_assignment_there_already_is_kept_as_it_is(self):
        """An owner's own key and address for it: kept, not made right - only install.sh's assign does that."""
        with open(self.config, 'w', newline='\n') as f:
            f.write('opt3 PCIE 10.0.0.1/30\n')
        done, calls = self.run_early()
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.of(calls, 'php'), [SHOW])
        self.assertEqual(self.of(calls, 'configctl') + self.of(calls, 'logger'), [])
        self.assertEqual(done.stdout, '')

    def read_state(self):
        try:
            with open(self.state) as f:
                return f.read().strip()
        except OSError:
            return None

    def test_an_assignment_made_is_recorded(self):
        done, calls = self.run_early()
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.read_state(), 'assigned opt15')

    def test_an_assignment_its_owner_removed_is_not_made_again_and_said_once(self):
        # #1072: the owner removed NPUMGMT in Interfaces: Assignments after this script had assigned it
        with open(self.state, 'w', newline='\n') as f:
            f.write('assigned opt15\n')
        done, calls = self.run_early()
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.of(calls, 'php'), [SHOW])
        self.assertTrue(self.read_state().startswith('removed '))
        self.assertIn('was removed by its owner', done.stdout)
        self.assertIn('Interfaces: Assignments brings it back', done.stdout)
        done, calls = self.run_early()
        self.assertEqual(self.of(calls, 'php'), [SHOW])
        self.assertEqual(done.stdout, '')
        self.assertTrue(self.read_state().startswith('removed '))

    def test_an_owner_who_assigns_it_again_has_it_back_as_this_makes_it(self):
        # Interfaces: Assignments adds it disabled and without an address: made right once, and said
        with open(self.state, 'w', newline='\n') as f:
            f.write('removed 2026-11-21 12:00 UTC\n')
        with open(self.config, 'w', newline='\n') as f:
            f.write('opt15 OPT15 /\n')
        done, calls = self.run_early()
        self.assertEqual(done.returncode, 0, done.stderr)
        php = [c for c in self.of(calls, 'php') if ' state' not in c and ' mark ' not in c]
        self.assertEqual(php, [SHOW, WRITE])
        self.assertEqual(self.read_state(), 'assigned opt15')
        self.assertIn('assigned again by its owner', done.stdout)
        done, calls = self.run_early()
        php = [c for c in self.of(calls, 'php') if ' state' not in c and ' mark ' not in c]
        self.assertEqual(php, [SHOW])
        self.assertEqual(done.stdout, '')

    def test_a_removal_holds_when_the_assignment_cannot_be_read(self):
        with open(self.state, 'w', newline='\n') as f:
            f.write('removed 2026-11-21 12:00 UTC\n')
        done, calls = self.run_early(show='echo "PHP Fatal error" >&2; exit 255')
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertNotIn(WRITE, self.of(calls, 'php'))
        self.assertTrue(self.read_state().startswith('removed '))

    def test_a_boot_without_the_link_reads_and_writes_nothing(self):
        done, calls = self.run_early(link=False)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.of(calls, 'php'), [])
        self.assertEqual(done.stdout, '')

    def test_the_xgs_3300_is_left_to_its_bootup_hook(self):
        for board in ('202 octeontx', '202 octeontx rnd'):
            done, calls = self.run_early(board=board)
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertEqual(self.of(calls, 'php') + self.of(calls, 'ifconfig'), [], board)

    def test_a_board_this_project_does_not_know_gets_nothing(self):
        done, calls = self.run_early(board='none untested')
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.of(calls, 'php'), [])

    def test_every_armada_board_is_assigned(self):
        done, calls = self.run_early(board='208 armada rnd')
        self.assertEqual(self.of(calls, 'php'), [SHOW, WRITE, SHOW])

    def test_an_answer_that_cannot_be_read_writes_nothing(self):
        for show in ('exit 1', 'echo "PHP Warning:  something"; echo none', 'echo'):
            done, calls = self.run_early(show=show)
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertEqual(self.of(calls, 'php'), [SHOW], show)
            self.assertIn('the assignment of mvmgmt0 could not be read - nothing written', done.stdout)

    def test_an_assignment_that_was_not_written_is_said_and_the_boot_goes_on(self):
        done, calls = self.run_early(assign='echo "no free interface slot" >&2; exit 1')
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.of(calls, 'php'), [SHOW, WRITE, SHOW])
        self.assertIn('the assignment of mvmgmt0 was not written', done.stdout)
        self.assertNotIn('is interface', done.stdout)
        self.assertEqual(self.of(calls, 'configctl'), [])


class Ping(unittest.TestCase):
    """mgmt-link.sh ping (svc-1-10): a ping sent by the coprocessor over the link. The address and the
    count become part of a command line on the coprocessor, so anything else is refused before ssh is
    run; ssh and timeout are stand-ins that keep what they were given."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.calls = os.path.join(self.dir, 'calls')
        open(self.calls, 'w').close()

    def tearDown(self):
        self.tmp.cleanup()

    def stub(self, name, body):
        path = os.path.join(self.dir, name)
        with open(path, 'w', newline='\n') as f:
            f.write('#!/bin/sh\necho "%s $*" >> "%s"\n%s\n' % (name, self.calls, body))
        os.chmod(path, 0o755)
        return path

    def ping(self, *args, ssh='echo "64 bytes from x"; exit 0', link=True):
        stubs = {'/usr/local/opnsense/scripts/xgs/board.sh': self.stub('board', 'echo "201 armada"'),
                 '/usr/bin/logger': self.stub('logger', 'exit 0'),
                 '/sbin/ifconfig': self.stub('ifconfig', 'exit %d' % (0 if link else 1)),
                 '/usr/bin/timeout': self.stub('timeout', 'shift; exec "$@"')}
        self.stub('ssh', ssh)
        with open(SCRIPT, encoding='utf-8') as f:
            text = f.read()
        for real, fake in stubs.items():
            text = text.replace(real, fake)
        script = os.path.join(self.dir, 'mgmt-link.sh')
        with open(script, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        env = dict(os.environ, PATH=self.dir + os.pathsep + os.environ.get('PATH', ''))
        done = subprocess.run(['/bin/sh', script, 'ping'] + list(args), env=env, capture_output=True,
                              text=True, timeout=60)
        with open(self.calls) as f:
            return done, [c for c in f.read().splitlines() if c.startswith('ssh ')]

    def test_an_address_is_pinged_by_the_coprocessor(self):
        done, sent = self.ping('192.0.2.1', '3')
        self.assertEqual(done.returncode, 0)
        self.assertIn('64 bytes from x', done.stdout)
        self.assertEqual(done.stdout.splitlines()[-1], 'ping: answered')
        self.assertEqual(len(sent), 1)
        self.assertTrue(sent[0].endswith('root@169.254.254.2 ping -c 3 -w 8 192.0.2.1'), sent[0])

    def test_what_did_not_answer_is_said_and_the_exit_is_0(self):
        # configd shows a script's output only when it exits 0: until 2026-11-14 an address that did
        # not answer read "Execute error" on the page, and what ping printed was lost
        for body, last in (('echo "2 packets transmitted, 0 received"; exit 1', 'ping: no answer'),
                           ('echo "connect: Network is unreachable"; exit 2', 'ping: not sent'),
                           ('exit 255', 'ping: the coprocessor could not be reached over mvmgmt0')):
            done, sent = self.ping('192.0.2.1', '1', ssh=body)
            self.assertEqual(done.returncode, 0, body)
            self.assertEqual(done.stdout.splitlines()[-1], last, body)
        self.assertIn('Network is unreachable', self.ping('192.0.2.1', '1', ssh=(
            'echo "connect: Network is unreachable"; exit 2'))[0].stdout)

    def test_no_link_is_said_without_ssh(self):
        done, sent = self.ping('169.254.254.1', '1', link=False)
        self.assertEqual((done.returncode, done.stdout.strip(), sent), (0, 'ping: no mvmgmt0 on this machine', []))

    def test_an_ipv6_address_is_pinged_with_ping6(self):
        done, sent = self.ping('2001:db8::1', '1')
        self.assertIn('ping6 -c 1 -w 6 2001:db8::1', sent[0])

    def test_a_name_is_taken_and_the_count_defaults_to_four(self):
        done, sent = self.ping('gateway.example.org')
        self.assertIn('ping -c 4 -w 9 gateway.example.org', sent[0])

    def test_anything_else_is_refused_before_ssh(self):
        for bad in (['1.2.3.4;reboot'], ['$(reboot)'], ['-f', '1'], [''], ['a b'], ['1.2.3.4', '0'],
                    ['1.2.3.4', '11'], ['1.2.3.4', '3x'], ['x' * 254]):
            done, sent = self.ping(*bad)
            self.assertEqual(done.returncode, 2, bad)
            self.assertEqual(sent, [], bad)


class Status(unittest.TestCase):
    """mgmt-link.sh status says a bridge the link is a member of: an XGS 87's support bundle had mvmgmt0
    in a bridge with its WAN port (2026-11-21), and nothing said so. ifconfig, ping and ssh are
    stand-ins; nothing is sent anywhere."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def stub(self, name, body):
        path = os.path.join(self.dir, name)
        with open(path, 'w', newline='\n') as f:
            f.write('#!/bin/sh\n%s\n' % body)
        os.chmod(path, 0o755)
        return path

    def status(self, bridge0):
        ifconfig = self.stub('ifconfig', 'case "$1" in\n'
                                         '-g) echo bridge0; echo bridge1 ;;\n'
                                         'bridge0) printf "%%s\\n" %s ;;\n'
                                         'bridge1) echo "member: npup3 flags=143<LEARNING>" ;;\n'
                                         'esac\nexit 0' % bridge0)
        stubs = {'/usr/local/opnsense/scripts/xgs/board.sh': self.stub('board', 'echo "201 armada"'),
                 '/sbin/ifconfig': ifconfig,
                 '/sbin/ping': self.stub('ping', 'exit 1')}
        self.stub('ssh', 'exit 255')
        with open(SCRIPT, encoding='utf-8') as f:
            text = f.read()
        for real, fake in stubs.items():
            text = text.replace(real, fake)
        script = os.path.join(self.dir, 'mgmt-link.sh')
        with open(script, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        env = dict(os.environ, PATH=self.dir + os.pathsep + os.environ.get('PATH', ''), CONSOLE='/dev/null')
        done = subprocess.run(['/bin/sh', script, 'status'], env=env, capture_output=True, text=True, timeout=60)
        return [l for l in done.stdout.splitlines() if l.startswith('bridge: ')]

    def test_a_bridged_link_is_said(self):
        self.assertEqual(self.status('"member: mvmgmt0 flags=143<LEARNING>" "member: npup2 flags=143<LEARNING>"'),
                         ['bridge: mvmgmt0 is a member of bridge0 - it is never bridged; take it out in '
                          'Interfaces: Devices: Bridge'])

    def test_a_link_in_no_bridge_is_said_too(self):
        self.assertEqual(self.status('"member: npup2 flags=143<LEARNING>" "member: mvmgmt01 flags=143<LEARNING>"'),
                         ['bridge: none'])


# config.inc and util.inc for mgmt-link.php's own PHP: the configuration a JSON file of the test's,
# written back as it is at write_config().
CONFIG_INC = '''<?php
$config = json_decode(file_get_contents(getenv('FAKE_CONFIG')), true);
function config_read_array($key) { global $config; return $config[$key] ?? []; }
function write_config($why) { global $config; file_put_contents(getenv('FAKE_CONFIG'), json_encode($config)); }
'''


@unittest.skipUnless(shutil.which('php'), 'php is not installed: mgmt-link.php is not run here')
class AssignKey(unittest.TestCase):
    """mgmt-link.php assign: the first optN that has no interface and that nothing else in the
    configuration names. A key left behind - still a bridge's member, or a rule's interface - gave the
    link that bridge or that rule until 2026-11-22."""

    PHP_SCRIPT = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'npuctl', 'mgmt-link.php')

    def assign(self, config):
        with tempfile.TemporaryDirectory() as d:
            for name, text in (('config.inc', CONFIG_INC), ('util.inc', '<?php\n')):
                with open(os.path.join(d, name), 'w', newline='\n') as f:
                    f.write(text)
            path = os.path.join(d, 'config.json')
            with open(path, 'w') as f:
                json.dump(config, f)
            done = subprocess.run([shutil.which('php'), '-d', 'include_path=' + d, self.PHP_SCRIPT, 'assign',
                                   '169.254.254.1', '30', 'mvmgmt0'], env=dict(os.environ, FAKE_CONFIG=path),
                                  capture_output=True, text=True, timeout=60)
            with open(path) as f:
                return done, json.load(f)

    INTERFACES = {'lan': {'if': 'npup1'}, 'opt1': {'if': 'npup2', 'descr': 'WAN'}}

    def test_a_key_still_named_elsewhere_is_not_taken(self):
        done, config = self.assign({'interfaces': self.INTERFACES,
                                    'bridges': {'bridged': [{'bridgeif': 'bridge0', 'members': 'opt1,opt2'}]},
                                    'filter': {'rule': [{'interface': 'opt3', 'descr': 'opt10 kept'}]}})
        self.assertEqual((done.returncode, done.stdout, done.stderr), (0, 'opt4\n', ''))
        self.assertEqual(config['interfaces']['opt4']['if'], 'mvmgmt0')
        self.assertNotIn('opt2', config['interfaces'])

    def test_with_nothing_left_behind_the_first_free_key_as_before(self):
        done, config = self.assign({'interfaces': self.INTERFACES, 'filter': {'rule': [{'interface': 'opt12'}]}})
        self.assertEqual((done.returncode, done.stdout), (0, 'opt2\n'))
        self.assertEqual(config['interfaces']['opt2']['descr'], 'NPUMGMT')


if __name__ == '__main__':
    unittest.main(verbosity=1)
