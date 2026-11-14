#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The Syslog templates' overlay that sends the coprocessor's log to OPNsense's remote destinations
(svc-3-30): templates/OPNsense/Syslog/xgs-coprocessor-remote.conf rendered with jinja2, as OPNsense's
template engine renders it, against made-up destinations - a log path from the coprocessor's source to
each enabled network destination, with that destination's filters, and nothing for the others. And
+TARGETS.D/xgs.TARGET writes it into syslog-ng's include directory. Needs jinja2.

    python3 tools/test-syslog-remote.py
"""

import os
import re
import sys

import jinja2

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATES = os.path.join(HERE, '..', 'src', 'opnsense', 'service', 'templates', 'OPNsense', 'Syslog')

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


class Helpers:
    """The two of OPNsense's template helpers the overlay uses, over a dict of the configuration."""

    def __init__(self, config):
        self.config = config

    def _get(self, path):
        node = self.config
        for part in path.split('.'):
            if not isinstance(node, dict) or part not in node:
                return None
            node = node[part]
        return node

    def empty(self, path):
        return not self._get(path)

    def toList(self, path):
        node = self._get(path)
        return node if isinstance(node, list) else ([] if node is None else [node])


def render(destinations):
    env = jinja2.Environment(loader=jinja2.FileSystemLoader(TEMPLATES), extensions=['jinja2.ext.do'])
    config = {'OPNsense': {'Syslog': {'destinations': {'destination': destinations}}}}
    return env.get_template('xgs-coprocessor-remote.conf').render(helpers=Helpers(config))


def paths(text):
    return re.findall(r'log \{(.*?)\};', text, re.S)


UDP = {'@uuid': '11111111-2222-3333-4444-555555555555', 'enabled': '1', 'transport': 'udp4',
       'hostname': '192.0.2.10', 'port': '514', 'description': 'collector'}
TLS = {'@uuid': 'aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee', 'enabled': '1', 'transport': 'tls4',
       'hostname': 'logs.example.org', 'port': '6514', 'level': 'warn,err,crit,alert,emerg',
       'program': 'kernel,sshd'}
OFF = dict(UDP, **{'@uuid': '99999999-2222-3333-4444-555555555555', 'enabled': '0'})

with open(os.path.join(TEMPLATES, '+TARGETS.D', 'xgs.TARGET'), encoding='utf-8') as handle:
    check('written into syslog-ng\'s include directory, beside OPNsense\'s destinations',
          handle.read().strip(),
          'xgs-coprocessor-remote.conf:/usr/local/etc/syslog-ng.conf.d/xgs-coprocessor-remote.conf')

check('no remote destination: nothing', paths(render(None)), [])
text = render([UDP, TLS, OFF])
found = paths(text)
check('a log path for each enabled destination, none for one switched off', len(found), 2)
check('from the coprocessor\'s source, its host named by the rewrite',
      all('source(s_xgs_coprocessor);' in p and 'rewrite(r_xgs_coprocessor);' in p for p in found), True)
check('to OPNsense\'s own destinations, not written again',
      [re.search(r'destination\((d_\w+)\)', p).group(1) for p in found],
      ['d_11111111222233334444555555555555', 'd_aaaaaaaabbbbccccddddeeeeeeeeeeee'])
check('no destination of its own', 'network(' in text or re.search(r'^destination ', text, re.M) is not None, False)
check('a destination\'s choices are its filters here',
      re.findall(r'filter\((f_xgs_\w+)\);', found[1]),
      ['f_xgs_aaaaaaaabbbbccccddddeeeeeeeeeeee_program', 'f_xgs_aaaaaaaabbbbccccddddeeeeeeeeeeee_level'])
check('a program list as OPNsense writes it', 'program("kernel") or program("sshd")' in text, True)
check('a level list as OPNsense writes it', 'level(warn,err,crit,alert,emerg)' in text, True)
check('every brace closed', text.count('{'), text.count('}'))

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
