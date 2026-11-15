#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The TACACS+ server type (src/opnsense/mvc/app/library/OPNsense/Auth/TacacsPlus.php), with PHP and no
OPNsense: its fields as System: Access: Servers draws and checks them, and an authentication through a
stand-in for its tool (tools/tacacs/xgs-tacauth.c, which needs FreeBSD's libtacplus) - the eight lines it
writes on the tool's standard input, in their order and nothing on its command line; a PASS taken, a FAIL
and an ERROR refused with the server's words; a user or a password with a line break, a missing tool and
an empty server refused without asking anyone.

    python3 tools/test-tacacs.py"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONNECTOR = os.path.join(ROOT, 'src', 'opnsense', 'mvc', 'app', 'library', 'OPNsense', 'Auth', 'TacacsPlus.php')

# The stand-in tool: the lines it is given go to a file beside it, its command line to another, and it
# answers by the password, as the real one would by the server's word.
TOOL = r'''#!/usr/bin/env python3
import json, os, sys
here = os.path.dirname(os.path.abspath(__file__))
lines = sys.stdin.read().split('\n')
json.dump({'lines': lines, 'argv': sys.argv[1:]}, open(os.path.join(here, 'seen.json'), 'w'))
password = lines[7] if len(lines) > 7 else ''
if password == 'right':
    print('PASS welcome'); sys.exit(0)
if password == 'down':
    print('ERROR connect: Connection refused'); sys.exit(2)
print('FAIL wrong user or password'); sys.exit(1)
'''

HARNESS = r'''<?php
namespace OPNsense\Auth {
    interface IAuthConnector {}
    class Base { protected $lastAuthErrors = []; public function getLastAuthErrors() { return $this->lastAuthErrors; } }
    // in the connector's own namespace, so PHP's gettext and syslog are neither needed nor redeclared
    function gettext($s) { return $s; }
    function syslog($p, $m) { $GLOBALS['logged'][] = $m; return true; }
}
namespace {
    $logged = [];
    require $argv[1];
    $c = json_decode($argv[2], true);
    $t = new \OPNsense\Auth\TacacsPlus();
    $r = new ReflectionProperty('\OPNsense\Auth\TacacsPlus', 'tool');
    $r->setAccessible(true);
    $r->setValue(null, $c['tool']);
    $out = ['type' => \OPNsense\Auth\TacacsPlus::getType(), 'description' => $t->getDescription()];
    $opts = $t->getConfigurationOptions();
    $out['fields'] = [];
    foreach ($opts as $k => $o) {
        $out['fields'][$k] = ['type' => $o['type'], 'default' => $o['default'] ?? null];
    }
    $out['checks'] = [];
    foreach ($c['checks'] as $k => $values) {
        foreach ($values as $v) {
            $out['checks'][$k][$v] = count($opts[$k]['validate']($v)) === 0;
        }
    }
    $t->setProperties($c['server']);
    $_SERVER['REMOTE_ADDR'] = '192.0.2.10';
    $m = new ReflectionMethod('\OPNsense\Auth\TacacsPlus', '_authenticate');
    $m->setAccessible(true);
    $out['result'] = $m->invoke($t, $c['user'], $c['password']);
    $out['errors'] = $t->getLastAuthErrors();
    $out['properties'] = $t->getLastAuthProperties();
    $out['logged'] = $logged;
    echo json_encode($out);
}
'''

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


php = shutil.which('php')
if php is None:
    print('FAIL php is not installed here, and this test needs it')
    sys.exit(1)

tmp = tempfile.mkdtemp(prefix='tacacs-test-')
tool = os.path.join(tmp, 'xgs-tacauth')
with open(tool, 'w') as f:
    f.write(TOOL)
os.chmod(tool, 0o755)
harness = os.path.join(tmp, 'harness.php')
with open(harness, 'w') as f:
    f.write(HARNESS)

SERVER = {'host': 'tacacs.example.net', 'tacplus_port': '4949', 'tacplus_secret': 'a "quoted" secret',
          'tacplus_timeout': '3', 'tacplus_method': 'chap'}
CHECKS = {'host': ['192.0.2.1', 'tacacs.example.net', 'fe80::1', 'two words', ''],
          'tacplus_port': ['49', '65535', '0', '70000', 'x'],
          'tacplus_timeout': ['1', '60', '0', '61'],
          'tacplus_method': ['pap', 'ascii', 'chap', 'mschap'],
          'tacplus_secret': ['one line', 'two\nlines']}


def run(user, password, server=None, tool_path=None):
    seen = os.path.join(tmp, 'seen.json')
    if os.path.exists(seen):
        os.remove(seen)
    conf = {'tool': tool_path or tool, 'server': server or SERVER, 'user': user, 'password': password, 'checks': CHECKS}
    p = subprocess.run([php, harness, CONNECTOR, json.dumps(conf)], capture_output=True, text=True, errors='replace')
    try:
        out = json.loads(p.stdout)
    except ValueError:
        print(p.stdout[-800:], p.stderr[-800:])
        raise
    out['seen'] = json.load(open(seen)) if os.path.exists(seen) else None
    return out


r = run('lab-admin', 'right')
check('the type and its description', [r['type'], r['description']], ['tacplus', 'TACACS+'])
check('its fields as the servers page draws them',
      r['fields'], {'host': {'type': 'text', 'default': ''}, 'tacplus_port': {'type': 'text', 'default': '49'},
                    'tacplus_secret': {'type': 'text', 'default': ''}, 'tacplus_timeout': {'type': 'text', 'default': '5'},
                    'tacplus_method': {'type': 'dropdown', 'default': 'pap'}})
check('the server: an IPv4 address or a host name; IPv6, spaces and nothing refused',
      r['checks']['host'], {'192.0.2.1': True, 'tacacs.example.net': True, 'fe80::1': False, 'two words': False, '': False})
check('the port: 1 to 65535', r['checks']['tacplus_port'], {'49': True, '65535': True, '0': False, '70000': False, 'x': False})
check('the timeout: 1 to 60 seconds', r['checks']['tacplus_timeout'], {'1': True, '60': True, '0': False, '61': False})
check('the method: PAP, ASCII or CHAP', r['checks']['tacplus_method'], {'pap': True, 'ascii': True, 'chap': True, 'mschap': False})
check('the secret: one line', r['checks']['tacplus_secret'], {'one line': True, 'two\nlines': False})
check('the eight lines on the tool\'s standard input, in their order, and nothing on its command line',
      r['seen'], {'lines': ['tacacs.example.net', '4949', 'a "quoted" secret', '3', 'chap', '192.0.2.10', 'lab-admin', 'right', ''],
                  'argv': []})
check('a PASS: accepted, the server\'s words kept', [r['result'], r['errors'], r['properties']],
      [True, [], {'tacacs_message': 'welcome'}])

r = run('lab-admin', 'wrong')
check('a FAIL: refused with the server\'s words, nothing in the system log',
      [r['result'], r['errors'], r['logged']], [False, {'error': 'TACACS+ refused: wrong user or password'}, []])
r = run('lab-admin', 'down')
check('an ERROR: refused, and said in the system log',
      [r['result'], r['errors'], r['logged']],
      [False, {'error': 'TACACS+ error: connect: Connection refused'},
       ['TACACS+ server tacacs.example.net:4949: connect: Connection refused']])
r = run('lab-admin', 'right\nextra')
check('a password with a line break: refused, the tool not asked', [r['result'], r['seen']], [False, None])
r = run('lab-admin', 'right', server=dict(SERVER, host=''))
check('no server set: refused, the tool not asked', [r['result'], r['seen']], [False, None])
r = run('lab-admin', 'right', tool_path=os.path.join(tmp, 'missing'))
check('the tool not built: refused, said in the system log',
      [r['result'], r['seen'], len(r['logged'])], [False, None, 1])
r = run('lab-admin', 'right', server=dict(SERVER, tacplus_method='mschap', tacplus_port='', tacplus_timeout=''))
check('an unknown method, an empty port and timeout: PAP, 49 and 5 seconds',
      r['seen']['lines'][1:5], ['49', 'a "quoted" secret', '5', 'pap'])

shutil.rmtree(tmp, ignore_errors=True)
print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
