#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""bringup.sh away from the appliance, with the driver's answers made up.

The handshake: a coprocessor that an earlier load of the driver handshook with is said to be so at
once, and offered nothing - until 2026-11-06 it was waited out as a handshake in flight, a minute a
second at a time, and said not to answer; and an ordinary handshake is still offered after the
warm-up, the rings armed before it.

The ports: one that NetAgent gives no address in three tries - a request that times out, one it
refuses, an answer of zeros - is said by the name on its socket with the reason, and the last line
names it; until 2026-11-06 it was said by its place in a loop, an answer of zeros was bound, and the
last line counted the port's interface with the rest. A port that answers is bound as before, its
logical interface first and its tag after, and a cage the model does not have is not named.

sysctl, uname, logger, ifconfig, python3 and sleep are stand-ins in a copy of the script, and the
module, its stamp, the log and the script's /var/db are files in a temporary directory.

    python3 tools/test-bringup.py
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'octep', 'bringup.sh')
MODELS = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'models.py')
KERNEL = 'FreeBSD 15.1-RELEASE-p6 stable/26.7-n284176-c962f76493b6 SMP'
EARLIER = 'not handshaken by this instance; register holds 0x0000000000000001 (a port is started)'
IDLE = 'idle (scratch 0x0000000000000000)'
DONE = 'completed (scratch 0x0000000000000000)'
XGS3300, XGS2100 = 'AMDA0202-0004', 'AMDA0202-0001'

# NetAgent made up: each address request answered as the plan has it for the port, the last answer
# repeated, and recorded in the words the driver's nwa.last prints (octep_sysctl_nwa_last). Every
# address is the documentation prefix de:ad:be:ef with the port's tag in its last two bytes.
NETAGENT = r'''
import json, os, sys

d = sys.argv[1]


def staged(leaf):
    with open(os.path.join(d, 'dev.octep.0.nwa.' + leaf)) as f:
        return int(f.read())


op, sub, port = staged('op'), staged('sub'), staged('port')
with open(os.path.join(d, 'plan.json')) as f:
    plan = json.load(f)
answer, error, status, words = 'none', 0, 0, []
if (op, sub) == (4, 3):
    with open(os.path.join(d, 'asked'), 'a+') as f:
        f.seek(0)
        before = f.read().split().count(str(port))
        f.write('%d\n' % port)
    answers = plan.get(str(port), ['mac'])
    answer = answers[min(before, len(answers) - 1)]
if answer == 'mac':
    words = [0xefbeadde, ((port >> 8) & 0xff) | ((port & 0xff) << 8)]
elif answer == 'zeros':
    words = [0, 0]
elif answer == 'junk':
    words = ['0x12g4', 0]
elif answer == 'timeout':
    error = 60
elif answer.startswith('status '):
    status = int(answer.split()[1])
text = '\nop 0x%02x%s  sub 0x%02x%s  port 0x%08x  ' % (
    op, {3: ' PORT_ATTR_SET', 4: ' PORT_ATTR_GET'}.get(op, ''),
    sub, {0: ' STATE - and a GET of it returns the LINK here', 3: ' MAC'}.get(sub, ''), port)
if error:
    text += ('did not complete: error 60 (timed out - nothing was read back, because whatever is in '
             "the window is the other processor's and not a reply)\n")
else:
    text += 'marker 0x00000014 (expected)  status 0x%08x %s  reply %d bytes\n' % (
        status, '(ok)' if status == 0 else '(error)', 14 if words else 8)
    text += 'payload %d word%s\n' % (len(words), '' if len(words) == 1 else 's')
    for i, w in enumerate(words):
        text += ('  [%2d] 0x%08x  %u\n' % (i, w, w)) if isinstance(w, int) else ('  [%2d] %s  ?\n' % (i, w))
with open(os.path.join(d, 'last'), 'w') as f:
    f.write(text)
sys.exit(1 if error else 0)
'''


class Copy(unittest.TestCase):
    """The driver loaded and attached, its module stamped for the kernel that is running."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        with open(self.path('octep.ko'), 'w') as f:
            f.write('ELF')
        with open(self.path('octep.ko.kernel'), 'w') as f:
            f.write(KERNEL + '\n')

    def tearDown(self):
        self.tmp.cleanup()

    def path(self, name):
        return os.path.join(self.dir, name)

    def stub(self, name, body):
        path = self.path(name)
        with open(path, 'w', newline='\n') as f:
            f.write('#!/bin/sh\n%s\n' % body)
        os.chmod(path, 0o755)
        return path

    def read(self, name):
        try:
            with open(self.path(name)) as f:
                return f.read()
        except FileNotFoundError:
            return ''

    def run_copy(self, replacements):
        """The script with each command or path in replacements put in its stand-in's place, run with
        sleep a stand-in that only keeps what it was asked to wait."""
        os.makedirs(self.path('bin'))
        self.stub(os.path.join('bin', 'sleep'), 'echo "$1" >> "%s"' % self.path('sleeps'))
        with open(SCRIPT, encoding='utf-8') as f:
            text = f.read()
        for command, stand_in in replacements:
            self.assertIn(command, text)
            text = text.replace(command, stand_in)
        script = self.path('bringup.sh')
        with open(script, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        env = {name: value for name, value in os.environ.items()
               if name not in ('CHECK', 'FORCE', 'HS_ATTEMPTS', 'COPROC_WARMUP', 'NWA_WAIT', 'RSIZE')}
        env.update(PATH=self.path('bin') + os.pathsep + os.environ.get('PATH', ''),
                   MODULE=self.path('octep.ko'), LOGFILE=self.path('log'), FASTPATH_WAIT='0',
                   KLDSTAT=self.stub('kldstat', 'exit 0'), KLDLOAD=self.stub('kldload', 'exit 1'))
        done = subprocess.run(['/bin/sh', script], env=env, capture_output=True, text=True, timeout=60)
        return done, self.read('writes').splitlines(), self.read('sleeps').splitlines()


class Handshake(Copy):

    def bring_up(self, state, answer=None):
        """bringup.sh with the handshake's state reading state, and answer once an offer is made. What it
        returned, and the writes and sleeps it made."""
        with open(self.path('hs'), 'w') as f:
            f.write(state + '\n')
        offered = ''
        if answer is not None:
            offered = 'dev.octep.0.sdp.handshake=1) echo "%s" > "%s" ;;' % (answer, self.path('hs'))
        # sysctl -n <name> reads; sysctl <name>=<value> ... writes, each kept in the order made
        sysctl = self.stub('sysctl', (
            'if [ "$1" = -n ]; then\n'
            '\tcase "$2" in\n'
            '\tdev.octep.0.sdp.hs_state) cat "{hs}" ;;\n'
            '\t*) exit 1 ;;\n'
            '\tesac\n'
            '\texit 0\n'
            'fi\n'
            'for w in "$@"; do\n'
            '\techo "$w" >> "{writes}"\n'
            '\tcase "$w" in\n'
            '\t{offered}\n'
            '\tesac\n'
            'done').format(hs=self.path('hs'), writes=self.path('writes'), offered=offered))
        return self.run_copy((('/sbin/sysctl', sysctl),
                              ('/usr/bin/uname', self.stub('uname', 'echo "%s"' % KERNEL)),
                              ('/usr/bin/logger', self.stub('logger', 'exit 0'))))

    def test_a_coprocessor_handshaken_by_an_earlier_load_is_said_so_at_once(self):
        done, writes, sleeps = self.bring_up(EARLIER)
        log = self.read('log')
        self.assertEqual(done.returncode, 1)
        self.assertIn('the handshake register holds 0x0000000000000001 (a port is started), which this load '
                      'of the driver did not write: an earlier load handshook with the coprocessor', log)
        self.assertNotIn('not answering', log)
        self.assertEqual(sleeps, [], 'it waited for a handshake that was not coming')
        self.assertNotIn('dev.octep.0.sdp.handshake=1', writes)
        self.assertNotIn('dev.octep.0.dp.start=1', writes)

    def test_an_ordinary_handshake_is_offered_after_the_warm_up_and_taken(self):
        done, writes, sleeps = self.bring_up(IDLE, answer=DONE)
        log = self.read('log')
        self.assertIn('output rings armed', log)
        self.assertIn('handshake completed', log)
        self.assertEqual(writes.count('dev.octep.0.sdp.handshake=1'), 1)
        self.assertLess(writes.index('dev.octep.0.dp.start=1'), writes.index('dev.octep.0.sdp.handshake=1'))
        self.assertEqual(sleeps, ['20', '6'])
        # and on to the fast path, which no stand-in here answers
        self.assertEqual(done.returncode, 1)
        self.assertIn('the rpc facility never configured', log)


def switch(p):
    return 0x8000 | (p << 8)


def address(tag):
    return 'de:ad:be:ef:%02x:%02x' % ((tag >> 8) & 0xff, tag & 0xff)


# The XGS 3300's twelve as the two loops bind them: the cages on interfaces 10 and 11, then the ten
# behind the switch on 0 to 9.
PANEL3300 = [(10, 1), (11, 2)] + [(p - 1, switch(p)) for p in range(1, 11)]


class Binding(Copy):
    """After a handshake already made and a fast path already up: the two loops that ask each front port
    for its address and bind it, and the line the script ends on."""

    def bring_up(self, plan, assembly=XGS3300):
        """bringup.sh with NetAgent answering each port's address requests as plan has them - a list per
        tag, its last answer repeated, an address where the plan has none - and models.py reading the
        panel of assembly, or not knowing the model when it is None. What it returned, the lines it
        logged without their times, and the writes it made."""
        with open(self.path('plan.json'), 'w') as f:
            json.dump({str(tag): answers for tag, answers in plan.items()}, f)
        if assembly is not None:
            panel = subprocess.run([sys.executable, MODELS, '--ports'], capture_output=True, text=True,
                                   env=dict(os.environ, XGS_PLANAR_SERIAL='XYZ' + assembly + '12345678'))
            self.assertEqual(panel.returncode, 0, panel.stderr)
            with open(self.path('panel'), 'w') as f:
                f.write(panel.stdout)
        with open(self.path('netagent.py'), 'w') as f:
            f.write(NETAGENT)
        d = self.dir
        sysctl = self.stub('sysctl', (
            'D="{d}"\n'
            'if [ "$1" = -n ]; then\n'
            '\tcase "$2" in\n'
            '\tdev.octep.0.sdp.hs_state) echo "{done}" ;;\n'
            '\tdev.octep.0.rpc.state) echo "reconfig_done 1" ;;\n'
            '\tdev.octep.0.nwa.last) cat "$D/last" ;;\n'
            '\t*) exit 1 ;;\n'
            '\tesac\n'
            '\texit 0\n'
            'fi\n'
            'for w in "$@"; do\n'
            '\techo "$w" >> "$D/writes"\n'
            '\tcase "$w" in\n'
            '\tdev.octep.0.nwa.op=*|dev.octep.0.nwa.sub=*|dev.octep.0.nwa.port=*) echo "${{w#*=}}" > "$D/${{w%%=*}}" ;;\n'
            '\tdev.octep.0.nwa.request=1) "{py}" "$D/netagent.py" "$D" || exit 1 ;;\n'
            '\tdev.octep.0.dp.if_add=*) echo "oxp$(cat "$D/ifs" 2> /dev/null | wc -l | tr -d " ")" >> "$D/ifs" ;;\n'
            '\tesac\n'
            'done').format(d=d, done=DONE, py=sys.executable))
        ifconfig = self.stub('ifconfig', '[ "$1" = -l ] && echo "lo0 $(cat "%s/ifs" | tr "\\n" " ")"\nexit 0' % d)
        python3 = self.stub('python3', (
            'case "$1" in\n'
            '*/models.py) [ -s "{d}/panel" ] && exec cat "{d}/panel"\n'
            '\techo "not a model this project knows: AMDA0202-0009" >&2; exit 1 ;;\n'
            '*/ports.py) [ "$2" = --count ] && echo 0 ;;\n'
            'esac\n'
            'exit 0').format(d=d))
        done, writes, _ = self.run_copy((('/sbin/sysctl', sysctl), ('/sbin/ifconfig', ifconfig),
                                         ('/usr/local/bin/python3', python3),
                                         ('/usr/local/bin/php', self.path('no-php')),
                                         ('/usr/bin/uname', self.stub('uname', 'echo "%s"' % KERNEL)),
                                         ('/usr/bin/logger', self.stub('logger', 'exit 0')),
                                         ('/var/log/octep-bringup.log', self.path('log')),
                                         ('/var/db/os-xgs-npu', self.path('db'))))
        logged = [line.split(' ', 1)[-1] for line in self.read('log').splitlines()]
        return done, logged, writes

    @staticmethod
    def posted(writes):
        """The rpc commands posted, in order: the command, and the interface, tag and address staged."""
        staged, out = {}, []
        for w in writes:
            name, _, value = w.partition('=')
            if name == 'dev.octep.0.rpc.post':
                out.append((staged.get('cmd'), staged.get('lif_iface'), staged.get('lif_tag'),
                            staged.get('lif_mac')))
            elif name.startswith('dev.octep.0.rpc.'):
                staged[name[len('dev.octep.0.rpc.'):]] = value
        return out

    @staticmethod
    def asked(writes):
        """The NetAgent requests made, in order: op, sub and port."""
        staged, out = {}, []
        for w in writes:
            name, _, value = w.partition('=')
            if name == 'dev.octep.0.nwa.request':
                out.append((int(staged['op']), int(staged['sub']), int(staged['port'])))
            elif name.startswith('dev.octep.0.nwa.'):
                staged[name[len('dev.octep.0.nwa.'):]] = value
        return out

    def assertBound(self, writes, ports):
        """Each (interface, tag) bound with its own address, its logical interface posted and then its
        tag, and nothing else bound."""
        want = []
        for iface, tag in ports:
            want.append(('3', str(iface), str(tag), address(tag)))
            want.append(('5', str(iface), str(tag), address(tag)))
        self.assertEqual([p for p in self.posted(writes) if p[0] in ('3', '5')], want)

    def test_every_port_that_answers_is_bound_as_before(self):
        done, logged, writes = self.bring_up({})
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertBound(writes, PANEL3300)
        self.assertIn('bound 10 of the ten ports behind the switch', logged)
        self.assertNotIn('not bound', ' '.join(logged))
        self.assertEqual(logged[-1], 'up: 12 front-port interfaces')

    def test_a_cage_that_never_answers_is_named_with_the_reason(self):
        done, logged, writes = self.bring_up({1: ['timeout']})
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn('F1 (oxp0): not bound - no address from NetAgent after three tries; the last '
                      'request timed out', logged)
        self.assertEqual(logged[-1], 'up: 12 front-port interfaces, 1 of them not bound: F1 (oxp0)')
        self.assertEqual(self.asked(writes).count((4, 3, 1)), 3, 'asked a bounded number of times')
        self.assertBound(writes, PANEL3300[1:])

    def test_a_switch_port_refused_is_named_with_its_status(self):
        done, logged, writes = self.bring_up({switch(3): ['status 3']})
        self.assertIn('Port3 (oxp4): not bound - no address from NetAgent after three tries; the last '
                      'answer was status 0x00000003', logged)
        self.assertIn('bound 9 of the ten ports behind the switch', logged)
        self.assertEqual(logged[-1], 'up: 12 front-port interfaces, 1 of them not bound: Port3 (oxp4)')
        self.assertEqual(self.asked(writes).count((4, 3, switch(3))), 3)
        self.assertNotIn((3, 3, switch(3)), self.asked(writes), 'an address given to the switch')
        self.assertBound(writes, [p for p in PANEL3300 if p[1] != switch(3)])

    def test_an_answer_of_zeros_is_no_address(self):
        done, logged, writes = self.bring_up({switch(9): ['zeros']})
        self.assertIn('F3 (oxp10): not bound - no address from NetAgent after three tries; the last '
                      'answer held no usable address (zeros, or words that are not one)', logged)
        self.assertNotIn('00:00:00:00:00:00', [p[3] for p in self.posted(writes)])
        self.assertNotIn((3, 3, switch(9)), self.asked(writes))
        self.assertEqual(logged[-1], 'up: 12 front-port interfaces, 1 of them not bound: F3 (oxp10)')

    def test_a_word_that_is_not_a_number_is_no_address_and_ends_nothing(self):
        """The words are read as text before any arithmetic: an arithmetic expansion of one that is not a
        number ends sh, and this is a boot's (the review's B6 met it in switch-jumbo.sh)."""
        done, logged, writes = self.bring_up({switch(9): ['junk']})
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn('F3 (oxp10): not bound - no address from NetAgent after three tries; the last '
                      'answer held no usable address (zeros, or words that are not one)', logged)
        self.assertNotIn((3, 3, switch(9)), self.asked(writes))
        self.assertEqual(logged[-1], 'up: 12 front-port interfaces, 1 of them not bound: F3 (oxp10)')

    def test_a_port_that_answers_on_its_second_try_is_bound(self):
        done, logged, writes = self.bring_up({switch(2): ['none', 'mac']})
        self.assertEqual(self.asked(writes).count((4, 3, switch(2))), 2)
        self.assertBound(writes, PANEL3300)
        self.assertEqual(logged[-1], 'up: 12 front-port interfaces')

    def test_without_the_models_table_the_names_are_the_3300s(self):
        # the cage F2 is the port NetAgent's warm-up asks, so it answers that first
        done, logged, writes = self.bring_up({2: ['mac', 'timeout'], switch(10): ['none']}, assembly=None)
        self.assertIn("this model is not in models.py; the XGS 3300's panel", logged)
        self.assertIn('F4 (oxp11): not bound - no address from NetAgent after three tries; the last '
                      'answer held no address', logged)
        self.assertEqual(logged[-1], 'up: 12 front-port interfaces, 2 of them not bound: F2 (oxp1), F4 (oxp11)')

    def test_a_cage_the_model_does_not_have_is_not_named(self):
        # the XGS 2100's panel has no cages on the coprocessor; its F1 is the switch's port 9
        done, logged, writes = self.bring_up({1: ['status 1'], 2: ['mac', 'status 1'], switch(9): ['timeout']},
                                             assembly=XGS2100)
        self.assertIn('cage 1: no address from NetAgent after three tries, skipping; the last answer was '
                      'status 0x00000001', logged)
        self.assertEqual(logged[-1], 'up: 10 front-port interfaces, 1 of them not bound: F1 (oxp8)')


if __name__ == '__main__':
    unittest.main(verbosity=2)
