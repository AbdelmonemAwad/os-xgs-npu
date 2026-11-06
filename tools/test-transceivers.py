#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/transceivers.py against made-up cages, the coprocessor and sysctl(8) stood in for.

collect-due: a read keeps each cage's link as it found it, and the next one is due when five minutes
have passed or a link is not what it was - a far end that restarted no longer leaves "no light" on
the page for five minutes.

A read cut short (the review of 2026-11-06, D3): a cage whose lines did not end keeps what the last
read found, its driver is told nothing, and the error and stderr name it, the exit still 0 for
configd; a reply with no cage whole keeps the last read as no reply does, and is asked again at boot;
a module whose diagnostics did not end gets none. Until then the cages after a cut were taken as
empty and told "0 0".
    python3 tools/test-transceivers.py"""
import atexit
import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import time
import types

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'opnsense',
                                'scripts', 'xgs'))
import transceivers  # noqa: E402

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s' % ('ok  ' if ok else 'FAIL', name) + ('' if ok else ': got %r, want %r' % (got, want)))


folder = tempfile.mkdtemp(prefix='transceivers-test-')
atexit.register(shutil.rmtree, folder, True)
transceivers.STATE = os.path.join(folder, 'transceivers.json')
real_tell_driver = transceivers.tell_driver
transceivers.tell_driver = lambda answer, devices=None, only=None: answer
transceivers.cage_ports = lambda: {'F1': {'label': 'F1', 'device': 'oxp0', 'status': 'no carrier'}}
transceivers.coprocessor = lambda seconds, command: ('@@F1\nF1 is not present\n@@F1 done\n', '')

answer = transceivers.collect()
check('a read keeps the cages\' interfaces', answer.get('devices'), {'F1': 'oxp0'})
check('a read keeps each cage\'s link as it found it', answer.get('links'), {'F1': 'no carrier'})

link = {'F1': 'no carrier'}
transceivers.links_now = lambda devices: dict(link)
check('nothing changed, read a moment ago: not due', transceivers.due(), False)
link['F1'] = 'active'
check('the far end came back: due', transceivers.due(), True)
link['F1'] = 'no carrier'
state = transceivers.show()
state['at'] = int(time.time()) - transceivers.READ_EVERY
transceivers.keep(state)
check('five minutes since the last read: due', transceivers.due(), True)
state = transceivers.show()
state['at'] = int(time.time())
del state['devices']
transceivers.keep(state)
check('a read from before the links were kept: due', transceivers.due(), True)

# --- a read cut short ------------------------------------------------------------------------------

transceivers.tell_driver = real_tell_driver
told = []


def sysctl(argv, **kwargs):
    """sysctl(8) as tell_driver() uses it: the XGS 3300's node is there, and every write is kept."""
    if argv[:2] == ['/sbin/sysctl', '-N']:
        return types.SimpleNamespace(returncode=0 if argv[2] == 'dev.octep.0.dp.module_media' else 1)
    told.append(argv[2])
    return types.SimpleNamespace(returncode=0)


transceivers.subprocess = types.SimpleNamespace(run=sysctl, SubprocessError=subprocess.SubprocessError)
transceivers.cage_ports = lambda: {label: {'label': label, 'device': 'oxp%d' % n, 'status': 'active'}
                                   for n, label in enumerate(('F1', 'F2', 'F3'))}

# F1 an Intel 10GBASE-SR module with diagnostics (byte 92 0x68), F2 a copper module, F3 empty
DECODE = {
    'F1': ['F1 phy10 byte 0 0x03', 'F1 phy10 byte 2 0x07 LC', '  byte 3 0x10', 'F1 phy10 byte 12 0x67 10300 MBd',
           'F1 phy10 byte 92 0x68', 'Vendor Name: Intel Corp', 'Vendor PN: FTLX8571D3BCV-IT'],
    'F2': ['F2 phy11 byte 0 0x03', 'F2 phy11 byte 2 0x22 RJ45', '  byte 6 0x08', 'F2 phy11 byte 12 0x0d 1300 MBd',
           'Vendor PN: SFP-GE-T'],
    'F3': ['F3 is not present'],
}
DIAGNOSTICS = ['Temperature (0x2200) 34.2 C', 'Vcc (0x8390) 3.368 V', 'TX Bias (0x0c83) 6.406 mA',
               'TX Power (0x1838) 0.620 mW', 'RX Power (0x11c6) 0.455 mW']


def reply(cages, cut_in=None):
    """The decode's reply for these cages, each closed - and with cut_in, that one's first line and
    nothing after it, as the runner prints a session that ended there."""
    lines = []
    for label in cages:
        lines += ['@@' + label] + DECODE[label] + ['@@%s done' % label]
    if cut_in:
        lines += ['@@' + cut_in, DECODE[cut_in][0]]
    return '\n'.join(lines) + '\n'


asked = []


def coprocessor(answers):
    """The coprocessor, answering the decode and the diagnostics from answers, keeping each command."""
    def run(seconds, command):
        asked.append(command)
        return answers['diagnostics' if 'xgs-sff -p' in command else 'decode']
    return run


def last_read():
    """A whole read of the three, kept: the state a cut read starts from."""
    transceivers.coprocessor = coprocessor({'decode': (reply(['F1', 'F2', 'F3']), ''),
                                            'diagnostics': ('@@10\n' + '\n'.join(DIAGNOSTICS) + '\n@@10 done\n', '')})
    with contextlib.redirect_stderr(io.StringIO()) as err:
        before = transceivers.collect()
    del told[:], asked[:]
    return before, err.getvalue()


before, said = last_read()
media = {m['port']: m.get('medium') for m in before['modules']}
check('a whole read: every cage, no error, nothing on stderr',
      ([m['port'] for m in before['modules']], before['error'], said), (['F1', 'F2', 'F3'], '', ''))
check('a whole read: F1 10GBASE-SR, F2 copper by its connector, F3 empty',
      (media['F1'], media['F2'], before['modules'][2]['present']),
      ({'1g': None, '10g': '10GBASE-SR', 'by': 'codes'}, {'1g': '1000BASE-T', '10g': None, 'by': 'connector'}, False))
check('a whole read: F1\'s diagnostics', before['modules'][0].get('dom', {}).get('rx_mw'), 0.455)

# The decode cut short in F2: F1 whole, F2's first line, nothing of F3.
CUT = 'the coprocessor\'s console did not answer within 10s'
transceivers.coprocessor = coprocessor({'decode': (reply(['F1'], cut_in='F2'), CUT),
                                        'diagnostics': ('@@10\n' + '\n'.join(DIAGNOSTICS) + '\n@@10 done\n', '')})
with contextlib.redirect_stderr(io.StringIO()) as err:
    answer = transceivers.collect()
check('each cage\'s lines asked to end with a line of their own', 'echo "@@$l done"' in asked[0], True)
check('cut: every cage still on the page, in order', [m['port'] for m in answer['modules']], ['F1', 'F2', 'F3'])
check('cut: F2 keeps what the last read found, with when',
      answer['modules'][1], dict(before['modules'][1], read_at=before['at']))
check('cut: F3 keeps what the last read found, with when',
      answer['modules'][2], dict(before['modules'][2], read_at=before['at']))
check('cut: F1 is this read\'s', ('read_at' in answer['modules'][0], answer['modules'][0].get('medium')),
      (False, media['F1']))
check('cut: the driver told F1 alone', told, ['dev.octep.0.dp.module_media=oxp0 0 19'])
check('cut: the error names the cages not read and why', answer['error'],
      'F2, F3 not read: the reply was cut short (%s)' % CUT)
check('cut: stderr says it', err.getvalue(), 'transceivers.py: F2, F3 not read: the reply was cut short (%s)\n' % CUT)
check('cut: kept so for the page', transceivers.show()['modules'], answer['modules'])

# A second cut, before F2 again: F2 keeps the time it was last read, not the cut read's.
del told[:]
with contextlib.redirect_stderr(io.StringIO()):
    again = transceivers.collect()
check('cut twice: F2 keeps the time it was read', again['modules'][1].get('read_at'), before['at'])

# The command line's exit for configd, which answers a failed command with "Execute error".
with contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()) as out:
    code = transceivers.main(['transceivers.py', 'collect'])
check('cut: collect still exits 0, its answer on stdout', (code, '"F2, F3 not read' in out.getvalue()), (0, True))

# Nothing whole: the decode cut short in F1. The last read is kept as no reply keeps it, the driver
# told what it was last told and nothing from the cut, and the boot's tries ask again.
before, _ = last_read()
transceivers.coprocessor = coprocessor({'decode': (reply([], cut_in='F1'), CUT), 'diagnostics': ('', '')})
transceivers.time = types.SimpleNamespace(time=time.time, sleep=lambda seconds: None)
with contextlib.redirect_stderr(io.StringIO()) as err:
    answer = transceivers.collect(tries=2)
check('nothing whole: the last read kept', answer['modules'], before['modules'])
check('nothing whole: the error names every cage', answer['error'],
      'F1, F2, F3 not read: the reply was cut short (%s)' % CUT)
check('nothing whole: stderr says it', err.getvalue().count('F1, F2, F3 not read'), 2)
check('nothing whole: the driver told the last read again, not the cut',
      told, ['dev.octep.0.dp.module_media=oxp0 0 19', 'dev.octep.0.dp.module_media=oxp1 16 0',
             'dev.octep.0.dp.module_media=oxp2 0 0'] * 2)
check('nothing whole: asked again at boot', len([c for c in asked if 'xgs-sff-decode' in c]), 2)

# The diagnostics cut short: F1 read whole, its readings not; it gets none rather than half.
before, _ = last_read()
transceivers.coprocessor = coprocessor({'decode': (reply(['F1', 'F2', 'F3']), ''),
                                        'diagnostics': ('@@10\n' + DIAGNOSTICS[0] + '\n', 'exit 124')})
with contextlib.redirect_stderr(io.StringIO()) as err:
    answer = transceivers.collect()
check('diagnostics cut: F1 gets none', 'dom' in answer['modules'][0], False)
check('diagnostics cut: the error says so', answer['error'],
      'diagnostics of F1 not read: the reply was cut short (exit 124)')
check('diagnostics cut: the driver told every cage', len(told), 3)

print('all passed' if failures == 0 else '%d failed' % failures)
sys.exit(1 if failures else 0)
