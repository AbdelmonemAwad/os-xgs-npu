#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The firewall hook in src/etc/inc/plugins.inc.d/xgs.inc against the rule-registering object of every
OPNsense it runs on, with PHP and no OPNsense: 26.7 to 26.7.2, which name the port-forward method
registerForwardRule() alone - the images carry 26.7 (issue 969); 26.7.3 to 26.7.8, which have
registerDestinationNatRule() and keep the old name as glue; and 27.1, from which their source says the
glue goes. Each gets the two filter rules and the one redirect, through a name it has; an object that
refuses a rule leaves the hook returning, not stopping the reload; and the hook calls by name only
methods every one of them has.

A refused rule is logged with syslog() as the hook does at run time, so a run of this on an appliance
leaves "xgs: the management link's rules were left out: refused" in its system log, from php.

    python3 tools/test-firewall-hook.py"""
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOOK = os.path.join(ROOT, 'src', 'etc', 'inc', 'plugins.inc.d', 'xgs.inc')

# What OPNsense\Firewall\Plugin offers for registering rules, by release: read from opnsense/core's own
# Plugin.php at the tags 26.7, 26.7.3 and 26.7.8 on 2026-11-02. 27.1 is 26.7.8 without the methods its
# source marks "backwards glue to be removed in 27.1".
CORE_26_7 = ['registerAnchor', 'registerFilterRule', 'registerForwardRule', 'registerDNatRule',
             'registerSNatRule', 'registerNptRule', 'registerTable']
CORE_26_7_3 = CORE_26_7 + ['registerDestinationNatRule', 'registerStaticNatRule', 'registerSourceNatRule']
CORE_27_1 = [m for m in CORE_26_7_3 if m not in ('registerForwardRule', 'registerDNatRule', 'registerSNatRule')]

HARNESS = r'''
$c = json_decode($argv[2], true);
function config_read_array($key) { global $c; return isset($c['config'][$key]) ? $c['config'][$key] : []; }
function mwexecf($format, $args = [], $mute = false) { return 0; }
$body = '';
foreach ($c['methods'] as $m) {
    $body .= "public function $m(...\$a) { " . ($c['refuse'] === $m ? "throw new RuntimeException('refused'); " : '')
        . "\$this->calls[] = ['$m', \$a]; }\n";
}
eval("class FakePlugin { public \$calls = [];\n$body}");
include $argv[1];
$fw = new FakePlugin();
$out = xgs_firewall($fw);
echo json_encode(['same' => $out === $fw, 'calls' => $fw->calls]);
'''

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


LINK = {'interfaces': {'lan': {'if': 'igb0', 'enable': '1'}, 'opt3': {'if': 'oxmgmt0', 'enable': '1'}}}


def hook(methods, config=LINK, refuse=None):
    case = json.dumps({'methods': methods, 'config': config, 'refuse': refuse})
    out = subprocess.run(['php', '-r', HARNESS, '--', HOOK, case], capture_output=True, text=True)
    if out.returncode != 0 or not out.stdout.startswith('{'):
        return 'php failed: ' + (out.stderr or out.stdout).strip()[:300]
    return json.loads(out.stdout)


def names(result):
    return [c[0] for c in result['calls']] if isinstance(result, dict) else result


if shutil.which('php') is None:
    print('FAIL php is not installed, so the firewall hook cannot be checked')
    sys.exit(1)

REDIRECT = {'interface': 'opt3', 'ipprotocol': 'inet', 'protocol': 'tcp/udp', 'from': '169.254.254.2',
            'to': '169.254.254.1', 'to_port': '514', 'target': '127.0.0.1', 'local-port': '5140',
            'pass': 'pass', 'descr': "the coprocessor's log to syslog-ng"}

for release, methods, rdr in (('26.7', CORE_26_7, 'registerForwardRule'),
                              ('26.7.3 to 26.7.8', CORE_26_7_3, 'registerDestinationNatRule'),
                              ('27.1', CORE_27_1, 'registerDestinationNatRule')):
    got = hook(methods)
    check('OPNsense %s: two filter rules and the redirect, through %s' % (release, rdr), names(got),
          ['registerFilterRule', 'registerFilterRule', rdr])
    if isinstance(got, dict):
        check('OPNsense %s: the redirect is the coprocessor\'s log to 127.0.0.1:5140' % release,
              got['calls'][2][1], [1, REDIRECT])
        check('OPNsense %s: the hook hands back the object it was given' % release, got['same'], True)

# The rules are on whichever interface key carries the link; the ICMP one, as an example.
got = hook(CORE_26_7)
if isinstance(got, dict):
    check('the filter rules are on the link\'s interface, from the coprocessor alone',
          {k: got['calls'][0][1][2][k] for k in ('interface', 'from', 'to')},
          {'interface': 'opt3', 'from': '169.254.254.2', 'to': '(self)'})

# An object that refuses the redirect, or any rule: the hook returns, and the reload goes on without
# the link's rules - it must never be what stops the firewall from loading (issue 969).
for refuse in ('registerForwardRule', 'registerFilterRule'):
    got = hook(CORE_26_7, refuse=refuse)
    check('a refused %s does not stop the reload' % refuse,
          got['same'] if isinstance(got, dict) else got, True)

# No link assigned, or the link disabled: no rule at all.
check('no management link assigned: no rule',
      names(hook(CORE_26_7_3, {'interfaces': {'lan': {'if': 'igb0', 'enable': '1'}}})), [])
check('the link assigned but disabled: no rule',
      names(hook(CORE_26_7_3, {'interfaces': {'opt3': {'if': 'mvmgmt0'}}})), [])
check('mvmgmt0, the ARMADA link, gets the same rules',
      names(hook(CORE_26_7, {'interfaces': {'opt1': {'if': 'mvmgmt0', 'enable': '1'}}})),
      ['registerFilterRule', 'registerFilterRule', 'registerForwardRule'])

# Read from the source: every method the hook calls by its name is one every release above has, and a
# name chosen at run time is chosen by method_exists().
with open(HOOK, encoding='utf-8') as f:
    source = f.read()
named = sorted(set(re.findall(r'\$fw->(\w+)\s*\(', source)))
everywhere = sorted(set(CORE_26_7) & set(CORE_26_7_3) & set(CORE_27_1))
check('the hook calls by name only methods every OPNsense has', [m for m in named if m not in everywhere], [])
check('a name chosen at run time is chosen by method_exists()',
      bool(re.search(r'\$fw->\$\w+\s*\(', source)) <= ('method_exists($fw' in source), True)

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
