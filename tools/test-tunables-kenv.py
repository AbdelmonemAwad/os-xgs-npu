#!/usr/bin/env python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""scripts/xgs/tunables-kenv.sh away from the appliance: the kernel's environment is a file behind a
stand-in kenv, the configuration's rows what a stand-in PHP prints, and /boot/loader.conf a file of the
test's. What the configuration has is set (issue 669); since 2026-11-06 what it no longer has - still
in the old loader.conf, which the loader read - is taken out, and nothing else is: the owner's own
loader files, a value set at the loader's prompt, a variable the hooks set, a row whose value is not a
number, another driver's prefix, and everything when the configuration could not be read. With PHP
installed, the script's own PHP reads a made-up config.xml as well. /bin/sh, so on Linux.

    python3 tools/test-tunables-kenv.py
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'tunables-kenv.sh')
POSIX = os.path.exists('/bin/sh') and sys.platform != 'win32'

# kenv(1) as the script uses it: no argument lists name="value"; -q name prints it; -u name unsets it;
# name=value sets it.
KENV = r'''
import json, os, sys
path = os.environ['FAKE_KENV']
with open(path) as f:
    env = json.load(f)
args = sys.argv[1:]
if not args:
    for k, v in env.items():
        print('%s="%s"' % (k, v))
elif args[0] == '-q':
    if args[1] not in env:
        sys.exit(1)
    print(env[args[1]])
elif args[0] == '-u':
    if env.pop(args[1], None) is None:
        sys.exit(1)
elif '=' in args[0]:
    k, v = args[0].split('=', 1)
    env[k] = v
with open(path, 'w') as f:
    json.dump(env, f)
'''

# config.inc for the script's own PHP: the configuration's tunables from the test.
CONFIG_INC = '''<?php
$config = json_decode(file_get_contents(getenv('FAKE_CONFIG')), true);
'''


@unittest.skipUnless(POSIX, 'tunables-kenv.sh runs under /bin/sh')
class TunablesKenv(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.kenv_state = os.path.join(self.dir, 'kenv.json')
        self.loader = os.path.join(self.dir, 'loader.conf')
        with open(os.path.join(self.dir, 'kenv.py'), 'w') as f:
            f.write(KENV)
        self.kenv = self.stub('kenv', 'exec "%s" "%s" "$@"' % (sys.executable, os.path.join(self.dir, 'kenv.py')))

    def tearDown(self):
        self.tmp.cleanup()

    def stub(self, name, body):
        path = os.path.join(self.dir, name)
        with open(path, 'w', newline='\n') as f:
            f.write('#!/bin/sh\n%s\n' % body)
        os.chmod(path, 0o755)
        return path

    def run_script(self, env, loader, rows, prefix='hw.npuep.', local=None, confd=None, php=None):
        """rows: the stand-in PHP's lines, or None for a PHP that fails before it reads anything."""
        with open(self.kenv_state, 'w') as f:
            json.dump(env, f)
        with open(self.loader, 'w') as f:
            f.write(loader)
        if local is not None:
            with open(self.loader + '.local', 'w') as f:
                f.write(local)
        if confd is not None:
            os.makedirs(self.loader + '.d', exist_ok=True)
            with open(os.path.join(self.loader + '.d', 'owner.conf'), 'w') as f:
                f.write(confd)
        if php is None:
            out = os.path.join(self.dir, 'php.out')
            with open(out, 'w') as f:
                f.write(''.join(r + '\n' for r in rows or []))
            php = self.stub('php', 'cat "%s"%s' % (out, '' if rows is not None else '; exit 255'))
        done = subprocess.run(['/bin/sh', SCRIPT, prefix], capture_output=True, text=True, errors='replace',
                              timeout=60, env=dict(os.environ, KENV=self.kenv, PHP=php, LOADER_CONF=self.loader,
                                                   FAKE_KENV=self.kenv_state))
        self.assertEqual(done.returncode, 0, done.stderr)
        with open(self.kenv_state) as f:
            return json.load(f), done.stdout.split('\n')[:-1]

    LOADER = ('# from the configuration, as OPNsense writes it\n'
              'hw.npuep.ipsec_on="1"\nhw.npuep.flow_max="65536"\nhw.npuep.giu.txq="4"\n')

    def test_a_row_the_configuration_dropped_is_taken_out(self):
        env = {'hw.npuep.ipsec_on': '1', 'hw.npuep.flow_max': '65536', 'hw.npuep.giu.txq': '4',
               'kernelname': '/boot/kernel/kernel'}
        env, said = self.run_script(env, self.LOADER, ['#read', 'hw.npuep.ipsec_on=1', 'hw.npuep.giu.txq=4'])
        self.assertEqual(env, {'hw.npuep.ipsec_on': '1', 'hw.npuep.giu.txq': '4', 'kernelname': '/boot/kernel/kernel'})
        self.assertEqual(said, ['hw.npuep.flow_max removed'])

    def test_what_the_configuration_has_is_set_as_before(self):
        env, said = self.run_script({'hw.npuep.ipsec_on': '0'}, 'hw.npuep.ipsec_on="0"\n',
                                    ['#read', 'hw.npuep.ipsec_on=1', 'hw.npuep.flow_max=131072'])
        self.assertEqual(env, {'hw.npuep.ipsec_on': '1', 'hw.npuep.flow_max': '131072'})
        self.assertEqual(said, ['hw.npuep.ipsec_on=1', 'hw.npuep.flow_max=131072'])

    def test_the_owners_own_files_keep_theirs(self):
        env = {'hw.npuep.flow_max': '65536', 'hw.npuep.giu.txq': '4'}
        env, said = self.run_script(env, self.LOADER, ['#read'], local='hw.npuep.flow_max=65536\n',
                                    confd=' hw.npuep.giu.txq = "4"\n')
        self.assertEqual(env, {'hw.npuep.flow_max': '65536', 'hw.npuep.giu.txq': '4'})
        self.assertEqual(said, [])

    def test_what_the_loader_did_not_take_from_loader_conf_stays(self):
        # set at the loader's prompt over loader.conf's, and set by the hooks themselves
        env = {'hw.npuep.flow_max': '32768', 'hw.npuep.ips_vf': '1'}
        env, said = self.run_script(env, self.LOADER, ['#read'])
        self.assertEqual(env, {'hw.npuep.flow_max': '32768', 'hw.npuep.ips_vf': '1'})
        self.assertEqual(said, [])

    def test_a_row_whose_value_is_not_a_number_is_not_taken_out(self):
        # the configuration names it - with a value the script does not set - and loader.conf has a number
        env, said = self.run_script({'hw.npuep.giu.txq': '4'}, 'hw.npuep.giu.txq="4"\n',
                                    ['#read', '#named hw.npuep.giu.txq'])
        self.assertEqual(env, {'hw.npuep.giu.txq': '4'})
        self.assertEqual(said, [])
        # and a value loader.conf gives that is not a number is not one the configuration's rows gave
        env, said = self.run_script({'hw.npuep.debug': '0x3'}, 'hw.npuep.debug="0x3"\n', ['#read'])
        self.assertEqual(env, {'hw.npuep.debug': '0x3'})
        self.assertEqual(said, [])

    def test_another_prefix_is_not_looked_at(self):
        env = {'hw.octep.flow_max': '65536'}
        env, said = self.run_script(env, 'hw.octep.flow_max="65536"\n', ['#read'], prefix='hw.npuep.')
        self.assertEqual(env, {'hw.octep.flow_max': '65536'})
        self.assertEqual(said, [])

    def test_nothing_is_taken_out_when_the_configuration_was_not_read(self):
        env = {'hw.npuep.flow_max': '65536'}
        for rows in (None, []):
            got, said = self.run_script(dict(env), self.LOADER, rows)
            self.assertEqual(got, env)
            self.assertEqual(said, [])

    @unittest.skipUnless(shutil.which('php'), 'php is not installed: the script\'s own PHP is not read here')
    def test_the_scripts_own_php_reads_the_configuration(self):
        with open(os.path.join(self.dir, 'config.inc'), 'w') as f:
            f.write(CONFIG_INC)
        config = os.path.join(self.dir, 'config.json')
        with open(config, 'w') as f:
            json.dump({'system': {'hostname': 'fw'}, 'sysctl': {'item': [
                {'tunable': 'hw.npuep.ipsec_on', 'value': '1'},
                {'tunable': 'hw.npuep.debug', 'value': '0x3'},
                {'tunable': 'net.inet.ip.forwarding', 'value': '1'}]}}, f)
        php = self.stub('php', 'FAKE_CONFIG="%s" exec "%s" -d include_path="%s" "$@"' % (config, shutil.which('php'), self.dir))
        env = {'hw.npuep.ipsec_on': '0', 'hw.npuep.debug': '0x3', 'hw.npuep.flow_max': '65536'}
        env, said = self.run_script(env, self.LOADER + 'hw.npuep.debug="0x3"\n', [], php=php)
        self.assertEqual(env, {'hw.npuep.ipsec_on': '1', 'hw.npuep.debug': '0x3'})
        self.assertEqual(said, ['hw.npuep.ipsec_on=1', 'hw.npuep.flow_max removed'])


if __name__ == '__main__':
    unittest.main(verbosity=1)
