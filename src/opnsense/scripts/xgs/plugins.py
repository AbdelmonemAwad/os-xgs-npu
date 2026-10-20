#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The project's appliance plugins and their releases - for the Plugins tab of XGS: Updates.

    plugins.py list            what is installed of each, and what was last found and done
    plugins.py check           ask GitHub for each installed plugin's latest release, keep it, print the list
    plugins.py update <name>   install that plugin's latest release, detached; the list shows how it went

EACH PLUGIN UPDATES ITSELF. Every one of them carries its own self-update.py - it reads the
plugin's latest release on GitHub, takes it only when it is ahead of what is installed, and runs the
installer the release carries - and this only calls it: `self-update.py check` to ask, and
`self-update.py` to install. Nothing of a plugin is in this repository. Until 2026-10-20 each of them
ran itself once a day from cron and installed a new release on its own; the owner asked for that to
stop and for the updates to be taken here, by hand (each plugin's own release drops its cron entry).

What a plugin has installed is its /usr/local/share/<name>/installed.json, what its last update did
its update.json, and the output of that update its update.log - all written by its own scripts.

THE CHECK ASKS NPUFORGE.ORG FIRST, for the reason update.py gives: three requests of GitHub's API a
plugin, six plugins, sixty requests an hour from one address. Every plugin's self-update.py takes the
API's address from SELF_UPDATE_API, so the check hands it npuforge.org's copy of the API and nothing
of a plugin changes; when the copy cannot answer, the check is asked again of GitHub itself. An
update is never given the copy: the plugin asks GitHub which commit its release is, and installs
that.
"""

import json
import os
import subprocess
import sys
import time

SHARE = '/usr/local/share'
MIRROR = os.environ.get('XGS_UPDATE_MIRROR', 'https://npuforge.org/api/github')
STATE = '/var/db/os-xgs-npu/plugins-check.json'
# name, where its self-update.py is installed, what it is
PLUGINS = [
    ('opnsense-arabic', '/usr/local/share/opnsense-arabic/self-update.py', 'the Arabic translation'),
    ('opnsense-rtl', '/root/opnsense-rtl/self-update.py', 'right-to-left layout'),
    ('os-fanctl', '/usr/local/opnsense/scripts/fanctl/self-update.py', 'fan control'),
    ('os-frontpanel', '/usr/local/opnsense/scripts/frontpanel/self-update.py', 'the front panel'),
    ('os-linkhealth', '/usr/local/opnsense/scripts/linkhealth/self-update.py', 'link health'),
    ('os-netreport', '/usr/local/opnsense/scripts/netreport/self-update.py', 'the network report'),
]


def read_json(path):
    try:
        with open(path, encoding='utf-8') as handle:
            return json.load(handle) or {}
    except (OSError, ValueError):
        return {}


def running(script):
    """Is this plugin's self-update running now? Its lock file is held while it runs."""
    try:
        done = subprocess.run(['/bin/pgrep', '-f', script], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return False
    return done.returncode == 0 and bool(done.stdout.strip())


def listing():
    found = read_json(STATE)
    rows = []
    for name, script, what in PLUGINS:
        if not os.path.isfile(script):
            continue
        have = read_json(os.path.join(SHARE, name, 'installed.json'))
        last = read_json(os.path.join(SHARE, name, 'update.json'))
        seen = (found.get('plugins') or {}).get(name) or {}
        rows.append({
            'name': name,
            'what': what,
            'installed': (have.get('commit') or '')[:12],
            'installed_date': have.get('date') or '',
            'installed_at': have.get('installed_at'),
            'latest': seen.get('tag') or '',
            'latest_sha': (seen.get('sha') or '')[:12],
            'compare': seen.get('compare') or '',
            'take': bool(seen.get('take')),
            'check_status': seen.get('status') or ('found' if seen.get('tag') else ''),
            'check_message': seen.get('message') or '',
            'last_status': last.get('status') or '',
            'last_message': last.get('message') or '',
            'last_at': last.get('at'),
            'running': running(script),
        })
    return {'checked_at': found.get('at'), 'plugins': rows}


def ask(script, api):
    """One plugin's `self-update.py check`, against the API at api ('' for GitHub's own)."""
    env = dict(os.environ)
    if api:
        env['SELF_UPDATE_API'] = api
    try:
        done = subprocess.run(['/usr/local/bin/python3', script, 'check'], capture_output=True,
                              text=True, timeout=120, env=env)
        return json.loads(done.stdout.strip().splitlines()[-1]) if done.stdout.strip() else {}
    except (OSError, subprocess.SubprocessError, ValueError, IndexError) as failure:
        return {'status': 'failed', 'message': str(failure)}


def check():
    seen = {}
    for name, script, _ in PLUGINS:
        if not os.path.isfile(script):
            continue
        answer = {'status': 'failed'}
        if MIRROR:
            # A check that fails writes its failure where the plugin keeps what its last update
            # did; one the copy could not answer, and GitHub then did, is not that plugin's news.
            last = os.path.join(SHARE, name, 'update.json')
            try:
                with open(last, 'rb') as handle:
                    kept = handle.read()
            except OSError:
                kept = None
            answer = ask(script, MIRROR)
            if answer.get('status') == 'failed':
                try:
                    if kept is None:
                        os.unlink(last)
                    else:
                        with open(last, 'wb') as handle:
                            handle.write(kept)
                except OSError:
                    pass
        if answer.get('status') == 'failed':
            answer = ask(script, '')
        seen[name] = answer
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    with open(STATE + '.tmp', 'w', encoding='utf-8') as handle:
        json.dump({'at': int(time.time()), 'plugins': seen}, handle)
    os.replace(STATE + '.tmp', STATE)
    return listing()


def update(name):
    script = next((s for n, s, _ in PLUGINS if n == name), None)
    if script is None or not os.path.isfile(script):
        return {'status': 'failed', 'message': '%s is not installed here' % name}
    if running(script):
        return {'status': 'busy', 'message': 'an update of %s is running' % name}
    # detached: the installer restarts configd and the web GUI, which would cut this call off
    subprocess.Popen(['/usr/sbin/daemon', '-f', '/usr/local/bin/python3', script],
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return {'status': 'started', 'message': 'updating %s' % name}


def main(argv):
    command = argv[1] if len(argv) > 1 else 'list'
    if command == 'list':
        print(json.dumps(listing()))
    elif command == 'check':
        print(json.dumps(check()))
    elif command == 'update' and len(argv) > 2:
        print(json.dumps(update(argv[2])))
    else:
        print(json.dumps({'status': 'failed', 'message': 'usage: plugins.py list|check|update <name>'}))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
