#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""Updates of this project from its GitHub repository, for System: Firmware: XGS Coprocessor.

    update.py status    what is installed, what the last check found, what the last update did
    update.py check     the newest release, and what changed in every release since the one
                        installed
    update.py apply     download that commit and run its own installer (configd starts this
                        detached; the page follows the log)
    update.py log       the last update's log

WHY NOT THE FIRMWARE PAGE. This project is not in OPNsense's package repository, so System:
Firmware: Updates never offers it: a change merged on GitHub reaches an installed appliance only
when somebody pulls it by hand. This is that pull, behind a button.

WHAT AN UPDATE IS. The commit's tarball, unpacked and installed by the install/install.sh it
carries - the same script a hand install runs, so an update is never a different procedure from
an install. It rebuilds the driver module through the kernel follower and stamps it for the
running kernel; the module that is LOADED stays the old one until the next boot, because
reloading the driver takes the front ports away, and on an XGS 3300 the WAN is one of them. So
an update says "takes effect at the next boot" and the page offers the reboot; nothing here
reboots on its own.

WHICH VERSION. The newest release on GitHub - while the project is in alpha every release is a
pre-release, and those are offered too. A release is made when a fix is ready, or when improvements
have been gathered (RELEASING.md), never for every change merged: until 2026-10-18 this offered the
newest commit on main whose checks passed, which mixed a day's merges with what had been released.
Only forward: an appliance running something newer than the newest release is not taken back.

A test machine can still follow a branch: XGS_UPDATE_BRANCH, or a one-line file STATE/branch, names
it, and then the newest commit on it whose checks all passed is offered, as before.
"""
import json
import os
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.error
import urllib.request

REPO = 'AbdelmonemAwad/os-xgs-npu'
API = 'https://api.github.com/repos/' + REPO
TARBALL = 'https://codeload.github.com/' + REPO + '/tar.gz/'
STATE = '/var/db/os-xgs-npu/update'


def branch():
    """'' - follow releases - unless XGS_UPDATE_BRANCH or a one-line file STATE/branch names a branch, for trying a
    change on a test machine before it is merged. Never set on an appliance in service."""
    name = os.environ.get('XGS_UPDATE_BRANCH', '')
    if not name:
        try:
            with open(os.path.join(STATE, 'branch'), encoding='utf-8') as handle:
                name = handle.read().strip()
        except OSError:
            name = ''
    return name if name and all(ch.isalnum() or ch in '-._/' for ch in name) else ''


BRANCH = branch()		# '' follows releases
INSTALLED = '/usr/local/share/os-xgs-npu/installed.json'
SOURCE = '/usr/local/share/os-xgs-npu/source'
CHECK = os.path.join(STATE, 'check.json')
APPLY = os.path.join(STATE, 'apply.json')
LOG = os.path.join(STATE, 'apply.log')
# How far back to look for a commit whose checks passed. Unauthenticated, GitHub allows sixty
# requests an hour from one address, and each commit looked at is one request.
LOOK_BACK = 5


def out(obj):
    print(json.dumps(obj))
    return 0


def read_json(path, default=None):
    try:
        with open(path, encoding='utf-8') as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return default


def write_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as handle:
        json.dump(obj, handle)
    os.replace(tmp, path)


def get(url):
    request = urllib.request.Request(url, headers={
        'Accept': 'application/vnd.github+json',
        'User-Agent': 'os-xgs-npu-updates',
    })
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def boottime():
    try:
        text = subprocess.run(['/sbin/sysctl', '-n', 'kern.boottime'], capture_output=True,
                              text=True, timeout=10).stdout
        return int(text.split('sec =')[1].split(',')[0])
    except (IndexError, ValueError, OSError, subprocess.SubprocessError):
        return 0


def installed():
    return read_json(INSTALLED, {}) or {}


def green(sha):
    """True when every check run on the commit has finished and none failed."""
    runs = get('%s/commits/%s/check-runs?per_page=100' % (API, sha)).get('check_runs', [])
    if not runs:
        return False
    return all(run.get('status') == 'completed' and
               run.get('conclusion') in ('success', 'neutral', 'skipped') for run in runs)


def subject(message):
    return (message or '').split('\n', 1)[0][:200]


def release_lines(release):
    """The changes a release's notes list, one bullet a line - the notes are its CHANGELOG section."""
    lines = []
    for raw in (release.get('body') or '').splitlines():
        line = raw.strip()
        if line.startswith(('- ', '* ')):
            lines.append(line[2:].strip())
    return lines or [release.get('name') or release.get('tag_name', '')]


def check():
    if BRANCH:
        return check_branch()
    now = int(time.time())
    inst = installed()
    have, have_rel = inst.get('commit', ''), inst.get('release', '')
    try:
        releases = [r for r in get('%s/releases?per_page=20' % API) if not r.get('draft')]
        releases.sort(key=lambda r: r.get('published_at') or '', reverse=True)
        if not releases:
            result = {'status': 'error', 'checked_at': now, 'source': 'releases',
                      'message': 'no release has been published yet'}
            write_json(CHECK, result)
            return out(result)
        newest = releases[0]
        tag = newest['tag_name']
        sha = get('%s/commits/%s' % (API, tag))['sha']
        result = {
            'status': 'ok', 'checked_at': now, 'source': 'releases', 'installed': have,
            'installed_release': have_rel,
            'target': {'sha': sha, 'tag': tag, 'name': newest.get('name') or tag,
                       'date': newest.get('published_at'), 'prerelease': bool(newest.get('prerelease')),
                       'url': newest.get('html_url', ''), 'subject': newest.get('name') or tag},
        }
        cmp = None
        if have and have != sha and have_rel != tag:
            try:
                cmp = get('%s/compare/%s...%s' % (API, have, sha))
            except urllib.error.HTTPError as error:
                if error.code not in (404, 422):
                    raise
                result['installed_unknown'] = True
        if have == sha or have_rel == tag:
            result.update({'compare': 'identical', 'ahead_by': 0, 'behind_by': 0})
        elif cmp is not None:
            result.update({'compare': cmp.get('status'), 'ahead_by': cmp.get('ahead_by', 0),
                           'behind_by': cmp.get('behind_by', 0)})
        else:
            result.update({'compare': 'unknown', 'ahead_by': None})
        # Every release newer than the installed one, newest first, each by the lines of its notes.
        tags = [r['tag_name'] for r in releases]
        newer = releases[:tags.index(have_rel)] if have_rel in tags else releases[:1]
        result['changes'] = [{'sha': r['tag_name'], 'release': r['tag_name'], 'date': r.get('published_at'),
                              'subject': line} for r in newer for line in release_lines(r)][:50]
        result['releases_since'] = len(newer)
        result['update_available'] = result['compare'] in ('ahead', 'unknown') and sha != have
    except urllib.error.HTTPError as error:
        reason = 'GitHub refused the request (%s)' % error.code
        if error.code in (403, 429):
            reason += ': the hourly allowance for this address is used up; try again later'
        result = {'status': 'error', 'checked_at': now, 'source': 'releases', 'message': reason}
    except (urllib.error.URLError, OSError, ValueError, KeyError) as error:
        result = {'status': 'error', 'checked_at': now, 'source': 'releases',
                  'message': 'could not reach GitHub: %s' % error}
    write_json(CHECK, result)
    return out(result)


def check_branch():
    """A test machine following a branch: the newest commit on it whose checks all passed."""
    now = int(time.time())
    have = installed().get('commit', '')
    try:
        commits = get('%s/commits?sha=%s&per_page=%d' % (API, BRANCH, LOOK_BACK))
        target = None
        for c in commits:
            if green(c['sha']):
                target = c
                break
        if target is None:
            result = {'status': 'error', 'checked_at': now, 'branch': BRANCH,
                      'message': 'none of the newest %d commits on %s has passed its checks yet'
                                 % (len(commits), BRANCH)}
            write_json(CHECK, result)
            return out(result)
        result = {
            'status': 'ok', 'checked_at': now, 'branch': BRANCH, 'installed': have,
            'target': {'sha': target['sha'], 'date': target['commit']['committer']['date'],
                       'subject': subject(target['commit']['message'])},
        }
        cmp = None
        if have:
            try:
                cmp = get('%s/compare/%s...%s' % (API, have, target['sha']))
            except urllib.error.HTTPError as error:
                # The installed commit is not on GitHub: a build from a local branch, or from a
                # branch deleted after its merge. Nothing can be compared with it, so the newest
                # passing commit is offered as for an installation that recorded none, and the page
                # says why - measured on 2026-10-17 on the XGS 3300, whose test build's commit was
                # never pushed, as "GitHub refused the request (404)" with nothing offered.
                if error.code not in (404, 422):
                    raise
                result['installed_unknown'] = True
        if cmp is not None:
            result['compare'] = cmp.get('status')          # ahead, behind, identical, diverged
            result['ahead_by'] = cmp.get('ahead_by', 0)
            result['behind_by'] = cmp.get('behind_by', 0)
            changes = cmp.get('commits', [])
        else:
            result['compare'] = 'unknown'
            result['ahead_by'] = None
            changes = commits[commits.index(target):]
        result['changes'] = [{
            'sha': c['sha'], 'date': c['commit']['committer']['date'],
            'subject': subject(c['commit']['message']), 'url': c.get('html_url', ''),
        } for c in reversed(changes)][:50]
        # An update is offered only forward: an appliance running a newer commit than main's
        # newest passing one (a branch under test, a hand install) is not taken back.
        result['update_available'] = result['compare'] in ('ahead', 'unknown') and \
            target['sha'] != have
    except urllib.error.HTTPError as error:
        reason = 'GitHub refused the request (%s)' % error.code
        if error.code in (403, 429):
            reason += ': the hourly allowance for this address is used up; try again later'
        result = {'status': 'error', 'checked_at': now, 'branch': BRANCH, 'message': reason}
    except (urllib.error.URLError, OSError, ValueError, KeyError) as error:
        result = {'status': 'error', 'checked_at': now, 'branch': BRANCH,
                  'message': 'could not reach GitHub: %s' % error}
    write_json(CHECK, result)
    return out(result)


def status():
    state = read_json(APPLY, {}) or {}
    pending = bool(state.get('status') == 'done' and state.get('finished_at', 0) > boottime())
    return out({
        'status': 'ok',
        'installed': installed(),
        'check': read_json(CHECK, {}),
        'apply': state,
        'pending_reboot': pending,
    })


def log():
    try:
        with open(LOG, encoding='utf-8', errors='replace') as handle:
            text = handle.read()
    except OSError:
        text = ''
    return out({'status': 'ok', 'log': text[-20000:]})


def apply():
    state = read_json(APPLY, {}) or {}
    if state.get('status') == 'running' and state.get('pid'):
        try:
            os.kill(int(state['pid']), 0)
            return out({'status': 'error', 'message': 'an update is already running'})
        except (OSError, ValueError):
            pass
    found = read_json(CHECK, {}) or {}
    if not found.get('update_available'):
        return out({'status': 'error', 'message': 'nothing to update to: check for updates first'})
    sha = found['target']['sha']
    os.makedirs(STATE, exist_ok=True)
    write_json(APPLY, {'status': 'running', 'pid': os.getpid(), 'sha': sha,
                       'started_at': int(time.time())})
    logf = open(LOG, 'w', encoding='utf-8')

    def say(text):
        logf.write(text + '\n')
        logf.flush()

    def fail(message):
        say(message)
        write_json(APPLY, {'status': 'failed', 'sha': sha, 'message': message,
                           'finished_at': int(time.time())})
        logf.close()
        return out({'status': 'error', 'message': message})

    tree = os.path.join(STATE, 'src-' + sha[:12])
    archive = os.path.join(STATE, sha[:12] + '.tar.gz')
    tag = found['target'].get('tag', '')
    say('downloading %s %s' % (REPO, tag or 'at ' + sha))
    try:
        request = urllib.request.Request(TARBALL + (tag or sha), headers={'User-Agent': 'os-xgs-npu-updates'})
        with urllib.request.urlopen(request, timeout=120) as response, open(archive, 'wb') as handle:
            shutil.copyfileobj(response, handle)
    except (urllib.error.URLError, OSError) as error:
        return fail('the download failed: %s' % error)

    say('unpacking')
    shutil.rmtree(tree, ignore_errors=True)
    os.makedirs(tree)
    try:
        with tarfile.open(archive) as tar:
            members = tar.getmembers()
            top = members[0].name.split('/')[0] if members else ''
            for m in members:
                # Everything under the one top directory GitHub puts in a tarball, and nothing
                # that would land outside the tree.
                if not m.name.startswith(top + '/') or '..' in m.name.split('/'):
                    continue
                m.name = m.name[len(top) + 1:]
                if m.name:
                    tar.extract(m, tree, filter='tar')
    except (tarfile.TarError, OSError) as error:
        return fail('the archive could not be unpacked: %s' % error)
    os.unlink(archive)
    if not os.path.isfile(os.path.join(tree, 'install', 'install.sh')):
        return fail('the downloaded tree has no install/install.sh')
    # install.sh records what it installed from this, as it does from a git checkout or from an
    # image's stamp.
    with open(os.path.join(tree, '.update-commit'), 'w', encoding='utf-8') as handle:
        handle.write('%s %s %s\n' % (sha, found['target']['date'], found['target'].get('tag', '')))

    say('running its install/install.sh - this builds the driver and takes a few minutes')
    say('')
    proc = subprocess.Popen(['/bin/sh', 'install/install.sh'], cwd=tree, stdout=logf,
                            stderr=subprocess.STDOUT)
    rc = proc.wait()
    if rc != 0:
        return fail('install/install.sh failed with status %d; nothing was rebooted, and the '
                    'driver that is running stays as it was' % rc)

    # The source tree an image keeps, replaced by the one just installed, so the appliance
    # carries the sources of what it runs.
    if os.path.isdir(SOURCE):
        shutil.rmtree(SOURCE + '.prev', ignore_errors=True)
        os.rename(SOURCE, SOURCE + '.prev')
    shutil.move(tree, SOURCE)
    say('')
    say('installed %s. The coprocessor driver takes effect at the next boot.' % (tag or sha[:12]))
    # The last check found this commit; it is installed now, so the page stops offering it.
    found.update({'installed': sha, 'compare': 'identical', 'ahead_by': 0, 'behind_by': 0,
                  'changes': [], 'update_available': False})
    write_json(CHECK, found)
    write_json(APPLY, {'status': 'done', 'sha': sha, 'finished_at': int(time.time())})
    logf.close()
    return out({'status': 'ok', 'sha': sha})


def main(argv):
    command = argv[1] if len(argv) > 1 else 'status'
    handlers = {'status': status, 'check': check, 'apply': apply, 'log': log}
    if command not in handlers:
        return out({'status': 'error', 'message': 'unknown command: %s' % command}) or 1
    return handlers[command]()


if __name__ == '__main__':
    sys.exit(main(sys.argv))
