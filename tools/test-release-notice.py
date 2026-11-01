#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""tools/release-notice.py against made-up release notes and a stand-in GitHub: which issues are told
that their fix is out, once, and which are not.

    python3 tools/test-release-notice.py"""
import importlib.util
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('release_notice', os.path.join(here, 'release-notice.py'))
rn = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rn)

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


R = rn.REPO
NOTES = '''## 0.1.0-alpha.11 - 2026-11-02

### Fixes

- npuep: as many queues as there are CPUs ([#946](https://github.com/%s/pull/946))
- Marker in each buffer ([#943](https://github.com/%s/pull/943))

### Documentation

- A page ([#947](https://github.com/%s/pull/947))
''' % (R, R, R)


class GitHub:
    def __init__(self, bodies, issues, told=()):
        self.bodies, self.issues, self.told = bodies, issues, set(told)
        self.written = []

    def __call__(self, method, path, body=None):
        parts = path.split('?')[0].split('/')
        if method == 'GET' and parts[-2] == 'pulls':
            n = int(parts[-1])
            return {'number': n, 'title': 'Change %d' % n, 'body': self.bodies.get(n, '')}
        if method == 'GET' and parts[-1] == 'comments':
            n = int(parts[-2])
            return [{'body': 'old <!-- release-notice v0.1.0-alpha.11 -->'}] if n in self.told else []
        if method == 'GET' and parts[-2] == 'issues':
            n = int(parts[-1])
            return dict({'number': n}, **self.issues.get(n, {}))
        self.written.append((path, body))
        return None


def go(gh, dry_run=False):
    said = []
    rn.run('0.1.0-alpha.11', NOTES, gh, dry_run=dry_run, say=said.append)
    return said


check('the notes name their pull requests', rn.pulls(NOTES), [946, 943, 947])
check('Fix for one issue', rn.fixed('Fix for #890\n\nmore'), [890])
check('Fix for several', rn.fixed('Some text.\n\nFix for #890, #748 and #736\n'), [890, 748, 736])
check("GitHub's own word", rn.fixed('This fixes #12 and closes: #13.'), [12, 13])
check('a mention in passing is not a fix', rn.fixed('For the XGS 107w of #890. See #748.'), [])
check('a fix for further down a line is not one', rn.fixed('This is a fix for #890 in passing'), [])

gh = GitHub({946: 'For the 107w.\n\nFix for #890', 943: 'Fix for #890, #816', 947: 'See #900'}, {})
go(gh)
told = [w[0].split('/')[-2] for w in gh.written]
check('each issue is told once', told, ['890', '816'])
body = gh.written[0][1]['body']
check('the notice links the release', 'releases/tag/v0.1.0-alpha.11' in body, True)
check('the notice names the change', 'Change 946 (#946)' in body, True)
check('the notice says how an appliance takes it', 'XGS: Updates' in body, True)
check('the notice carries its mark', rn.mark('0.1.0-alpha.11') in body, True)

gh = GitHub({946: 'Fix for #890'}, {}, told={890})
go(gh)
check('an issue told before is not told again', gh.written, [])

gh = GitHub({946: 'Fix for #900'}, {900: {'pull_request': {}}})
go(gh)
check('a pull request named as fixed is not told', gh.written, [])

gh = GitHub({946: 'Fix for #890'}, {})
said = go(gh, dry_run=True)
check('a dry run writes nothing', gh.written, [])
check('a dry run says what it would write', said[0].startswith('would write on #890:'), True)

gh = GitHub({}, {})
said = go(gh)
check('nothing named, nothing written', (gh.written, said[-1].startswith('no pull request')), ([], True))

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
