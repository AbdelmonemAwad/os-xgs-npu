#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""Updates of this project from its GitHub repository, for XGS: Updates.

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

WHICH VERSION. The newest release on GitHub, pre-releases included - while the project is in alpha
every release's number ends in -alpha.N. A release is made when a fix is ready, or when improvements
have been gathered (RELEASING.md), never for every change merged: until 2026-10-18 this offered the
newest commit on main whose checks passed, which mixed a day's merges with what had been released.
Only forward: an appliance running something newer than the newest release is not taken back - and
that holds for a build from a branch the release does not contain ("diverged") too, judged by date:
on 2026-10-22 the XGS 136, running a build of the branch that became v0.1.0-alpha.5.4, was offered
and given v0.1.0-alpha.5.3, released before that build (issue 677).

AN UPDATE CHECKS AGAIN. apply() makes the check itself, then, and stops with the check's own error
when it fails; it never acts on a stored answer. On that same morning a check that could not reach
GitHub was followed by an update that installed the target of an earlier, successful one (issue
677): an answer acted on must be the one just given.

A test machine can still follow a branch: XGS_UPDATE_BRANCH, or a one-line file STATE/branch, names
it, and then the newest commit on it whose checks all passed is offered, as before.

THROUGH NPUFORGE.ORG FIRST. Without a token GitHub's API answers sixty requests an hour from one
address, and a check is three of them - the releases, the newest one's commit, and the comparison
with the installed one - and three more for each plugin on the Plugins tab. On 2026-10-20 the
three appliances of one lab, behind one address, used the hour up: "GitHub refused the request
(403): the hourly allowance for this address is used up". An address a provider shares among many
customers would be used up before any of them asked. So the check asks the same questions of
npuforge.org's copy of the API (website/worker/index.js, githubMirror), which asks GitHub once for
everybody every two minutes, with a token; when the site cannot answer, GitHub is asked itself, as
before. XGS_UPDATE_MIRROR='' asks GitHub alone. A test machine following a branch always does.

What is installed is never the site's word alone: before an update downloads anything, the
release's commit is confirmed with GitHub (tag_commit), and an update whose commit GitHub does not
confirm stops there.
"""
import datetime
import hashlib
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
GITHUB_API = 'https://api.github.com'
API = GITHUB_API + '/repos/' + REPO
MIRROR = os.environ.get('XGS_UPDATE_MIRROR', 'https://npuforge.org/api/github')
REFS = 'https://github.com/' + REPO + '.git/info/refs?service=git-upload-pack'
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
LOADED = os.path.join(STATE, 'loaded.json')
APPLY = os.path.join(STATE, 'apply.json')
LOG = os.path.join(STATE, 'apply.log')
# What install.sh's driver build leaves behind while it runs: the kernel follower's own account, and
# the directory it compiles in. The progress bar reads both; install/install.sh and
# install/kernel-follow.sh name the same two paths.
BUILD_LOG = '/var/db/os-xgs-npu/install-build.log'
BUILDROOT = '/var/db/os-xgs-npu/build'
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


def fetch(url, seconds):
    request = urllib.request.Request(url, headers={
        'Accept': 'application/vnd.github+json',
        'User-Agent': 'os-xgs-npu-updates',
    })
    with urllib.request.urlopen(request, timeout=seconds) as response:
        return json.load(response)


def get(url, direct=False):
    """GitHub's answer to a request of its API: through npuforge.org's copy first, and GitHub itself
    when the copy cannot answer. A 404 or a 422 the copy marks as GitHub's own (x-mirror-of) is
    GitHub's answer - a commit it does not know - and is not asked again."""
    if MIRROR and not direct and not BRANCH and url.startswith(GITHUB_API + '/'):
        try:
            return fetch(MIRROR + url[len(GITHUB_API):], 20)
        except urllib.error.HTTPError as error:
            if error.code in (404, 422) and error.headers.get('x-mirror-of') == 'api.github.com':
                raise
        except (urllib.error.URLError, OSError, ValueError):
            pass
    return fetch(url, 30)


def tag_commit(tag):
    """The commit a release's tag names, as GitHub says it: from its git endpoint, the list of
    references `git ls-remote` reads - not the API, so not its hourly allowance, and not the site's
    copy of it. An annotated tag's own line is followed by the commit it points to, "^{}"; that is
    the one taken. When the endpoint cannot be read, GitHub's API is asked directly."""
    try:
        request = urllib.request.Request(REFS, headers={'User-Agent': 'git/2 os-xgs-npu-updates'})
        with urllib.request.urlopen(request, timeout=30) as response:
            data = response.read(8 << 20)
        refs = {}
        at = 0
        while at + 4 <= len(data):
            size = int(data[at:at + 4], 16)
            if size < 4:
                at += 4
                continue
            line = data[at + 4:at + size].split(b'\0')[0].strip().decode('ascii', 'replace')
            at += size
            if len(line) > 41 and line[40] == ' ':
                refs[line[41:]] = line[:40]
        found = refs.get('refs/tags/%s^{}' % tag) or refs.get('refs/tags/%s' % tag)
        if found:
            return found
    except (urllib.error.URLError, OSError, ValueError):
        pass
    return get('%s/commits/%s' % (API, urllib.request.quote(tag, safe='')), direct=True)['sha']


# The driver modules an install writes; whichever this family uses is there.
MODULES = ('/boot/modules/npuep.ko', '/boot/modules/octep.ko')


def md5(path):
    try:
        with open(path, 'rb') as handle:
            return hashlib.md5(handle.read()).hexdigest()
    except OSError:
        return ''


def mtime(path):
    try:
        return os.stat(path).st_mtime
    except OSError:
        return float('inf')


def boottime():
    try:
        text = subprocess.run(['/sbin/sysctl', '-n', 'kern.boottime'], capture_output=True,
                              text=True, timeout=10).stdout
        return int(text.split('sec =')[1].split(',')[0])
    except (IndexError, ValueError, OSError, subprocess.SubprocessError):
        return 0


def installed():
    return read_json(INSTALLED, {}) or {}


def when(stamp):
    """Seconds since the epoch of an ISO 8601 time as git (%cI) and GitHub write them; 0 when there
    is none or it cannot be read."""
    try:
        return int(datetime.datetime.fromisoformat((stamp or '').replace('Z', '+00:00')).timestamp())
    except ValueError:
        return 0


def newer_than(installed_date, target_date):
    """True when what is installed is dated after the target: a diverged build is then not taken back
    to it (issue 677). Unknown either way, it is not - the target is offered, as before."""
    have, want = when(installed_date), when(target_date)
    return bool(have and want and have > want)


def loaded_modules():
    """{module path: md5} of the driver this boot loaded, taken while the file is still older than
    the boot and kept for the rest of it in LOADED.

    An update rebuilds the module and writes it again even when the bytes come out the same, and
    from then on the file's age no longer says whether it is the one running. Until 2026-10-20 the
    age was all an update went by: 0.1.0-alpha.4.2 rebuilt both XGS 3300s' octep.ko byte for byte,
    rightly said nothing waited for a reboot, and left the file newer than the boot - so the next
    update, 0.1.0-alpha.4.3, which rebuilt it byte for byte again, asked for a reboot, and both
    appliances were rebooted for a driver that had not changed. Now the first look in a boot - the
    Updates page opening, or an update starting - keeps what the boot loaded, and every update in
    that boot compares with it."""
    bt = boottime()
    kept = read_json(LOADED, {}) or {}
    if kept.get('boottime') != bt:
        kept = {'boottime': bt, 'modules': {}}
    for path in MODULES:
        if path not in kept['modules'] and os.path.exists(path) and mtime(path) < bt:
            kept['modules'][path] = md5(path)
    try:
        write_json(LOADED, kept)
    except OSError:
        pass
    return kept['modules']


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


def run_check():
    """The check, made now: its answer kept in CHECK for the page, and returned for apply()."""
    return check_branch() if BRANCH else check_releases()


def check():
    return out(run_check())


def check_releases():
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
            return result
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
        # Diverged is offered too: a build from a branch outside the release's history - a test
        # build, or one whose change was merged as a single commit - holds nothing the release can
        # be compared with. The second XGS 3300, installed from a probe branch on 2026-10-19, was
        # shown "Up to date" with its Update button off, a release ahead of it. But not when the build
        # is dated after the release: then the release would take it back (issue 677), and the page
        # says it is newer, as for one that is behind.
        if result['compare'] == 'diverged' and newer_than(inst.get('date', ''), newest.get('published_at')):
            result['newer_than_target'] = True
        result['update_available'] = result['compare'] in ('ahead', 'unknown', 'diverged') and \
            sha != have and not result.get('newer_than_target')
        # The newest release's own notes, always: with nothing to update to, the page still shows
        # what the release it compares with carries, instead of an empty tab.
        result['notes'] = {'release': tag, 'date': newest.get('published_at'), 'lines': release_lines(newest)}
    except urllib.error.HTTPError as error:
        reason = 'GitHub refused the request (%s)' % error.code
        if error.code in (403, 429):
            reason += ': the hourly allowance for this address is used up; try again later'
        result = {'status': 'error', 'checked_at': now, 'source': 'releases', 'message': reason}
    except (urllib.error.URLError, OSError, ValueError, KeyError) as error:
        result = {'status': 'error', 'checked_at': now, 'source': 'releases',
                  'message': 'could not reach GitHub: %s' % error}
    write_json(CHECK, result)
    return result


def check_branch():
    """A test machine following a branch: the newest commit on it whose checks all passed."""
    now = int(time.time())
    inst = installed()
    have = inst.get('commit', '')
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
            return result
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
        # newest passing one (a branch under test, a hand install) is not taken back - by date when
        # the two have diverged (issue 677).
        if result['compare'] == 'diverged' and newer_than(inst.get('date', ''), result['target']['date']):
            result['newer_than_target'] = True
        result['update_available'] = result['compare'] in ('ahead', 'unknown', 'diverged') and \
            target['sha'] != have and not result.get('newer_than_target')
    except urllib.error.HTTPError as error:
        reason = 'GitHub refused the request (%s)' % error.code
        if error.code in (403, 429):
            reason += ': the hourly allowance for this address is used up; try again later'
        result = {'status': 'error', 'checked_at': now, 'branch': BRANCH, 'message': reason}
    except (urllib.error.URLError, OSError, ValueError, KeyError) as error:
        result = {'status': 'error', 'checked_at': now, 'branch': BRANCH,
                  'message': 'could not reach GitHub: %s' % error}
    write_json(CHECK, result)
    return result


def read_text(path):
    try:
        with open(path, encoding='utf-8', errors='replace') as handle:
            return handle.read()
    except OSError:
        return ''


def progress(state):
    """How far an update has got, read from what it leaves behind - never estimated from the clock.

    Six steps, each with its share of the bar: the download, the unpacking, the installer's own
    sections (each "== ... ==" header it prints is a step forward), the kernel sources made ready,
    the driver's build, and the finish. The build is most of the wait, and its share moves with the
    object files appearing beside the sources in the follower's build directory: five of nine
    compiled is five ninths of the way. Before 2026-10-18 the page showed one fixed line for all of
    it, and the log sat at "== kernel module ==" for minutes, because the build writes to a log of
    its own."""
    if state.get('status') not in ('running', 'done', 'failed'):
        return None
    lines = read_text(LOG).splitlines()
    headers = [line for line in lines if line.startswith('== ')]
    step, percent, detail = 'download', 2, ''
    if any(line.startswith('unpacking') for line in lines):
        step, percent = 'unpack', 8
    if any(line.startswith('running its install') for line in lines):
        step, percent = 'install', 12
        before = 0
        for header in headers:
            if header.startswith('== kernel module'):
                break
            before += 1
        percent = min(34, 12 + 3 * before)
    if any(header.startswith('== kernel module') for header in headers):
        step, percent = 'sources', 35
        build = read_text(BUILD_LOG)
        if 'building against' in build:
            step, percent = 'build', 40
            try:
                names = os.listdir(BUILDROOT)
            except OSError:
                names = []
            sources = sum(1 for n in names if n.endswith('.c'))
            objects = min(sources, sum(1 for n in names if n.endswith('.o')))
            if sources:
                percent = 40 + int(50 * objects / sources)
                detail = '%d/%d' % (objects, sources)
        for line in build.splitlines():
            if line.startswith('built '):
                percent, detail = max(percent, 92), ''
            elif line.startswith('installed '):
                percent = max(percent, 95)
    if any(header.startswith(('== loader', '== installed')) for header in headers):
        step, percent, detail = 'finish', max(percent, 96), ''
    if state.get('status') == 'done':
        step, percent, detail = 'finish', 100, ''
    return {'step': step, 'percent': percent, 'detail': detail,
            'started_at': state.get('started_at'), 'finished_at': state.get('finished_at')}


def status():
    # Take what this boot loaded while the file still shows it, before any update rewrites it.
    loaded_modules()
    state = read_json(APPLY, {}) or {}
    since_boot = bool(state.get('status') == 'done' and state.get('finished_at', 0) > boottime())
    pending = since_boot and state.get('driver_changed', True) is not False
    return out({
        'status': 'ok',
        'channel': BRANCH or 'releases',
        'installed': installed(),
        'check': read_json(CHECK, {}),
        'apply': state,
        'progress': progress(state),
        'pending_reboot': pending,
        'driver_unchanged': since_boot and not pending,
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
    # Checked again, now, never taken from the stored answer (issue 677): a check that failed since,
    # or an answer hours old, must not choose what is installed.
    found = run_check()
    if found.get('status') != 'ok':
        return out({'status': 'error', 'message': 'the check made before updating failed, so nothing '
                    'was downloaded: %s' % found.get('message', 'no reason given')})
    if not found.get('update_available'):
        return out({'status': 'error', 'message': 'nothing to update to: what is installed is the '
                    'newest, or newer'})
    sha = found['target']['sha']
    os.makedirs(STATE, exist_ok=True)
    started = int(time.time())
    write_json(APPLY, {'status': 'running', 'pid': os.getpid(), 'sha': sha, 'started_at': started})
    logf = open(LOG, 'w', encoding='utf-8')

    def say(text):
        logf.write(text + '\n')
        logf.flush()

    def fail(message):
        say(message)
        write_json(APPLY, {'status': 'failed', 'sha': sha, 'message': message,
                           'started_at': started, 'finished_at': int(time.time())})
        logf.close()
        return out({'status': 'error', 'message': message})

    tree = os.path.join(STATE, 'src-' + sha[:12])
    archive = os.path.join(STATE, sha[:12] + '.tar.gz')
    tag = found['target'].get('tag', '')
    if tag and not BRANCH:
        # The check may have read the release from npuforge.org's copy of GitHub's API; what is
        # downloaded is what GitHub itself says the release is.
        try:
            confirmed = tag_commit(tag)
        except (urllib.error.URLError, OSError, ValueError, KeyError) as error:
            return fail('%s could not be confirmed with GitHub (%s); nothing was downloaded - try '
                        'again later' % (tag, error))
        if confirmed != sha:
            return fail('GitHub says %s is commit %s, not the %s the check found; nothing was '
                        'downloaded - check for updates again' % (tag, confirmed[:12], sha[:12]))
        say('%s is commit %s on GitHub' % (tag, sha[:12]))
    say('downloading %s %s' % (REPO, (tag + ', commit ' + sha[:12]) if tag else 'at ' + sha))
    try:
        # By the commit the check found, never by the tag: a tag can be moved between the check and
        # this, and then what is installed would not be the commit recorded as installed. Until
        # 2026-10-19 a release was downloaded by its tag.
        request = urllib.request.Request(TARBALL + sha, headers={'User-Agent': 'os-xgs-npu-updates'})
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

    # The driver this boot loaded, to tell afterwards whether the build changed it (loaded_modules).
    loaded = loaded_modules()

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
    # A reboot loads a driver that is not the one running. When the build gave back, byte for
    # byte, the module the boot loaded, there is nothing for a reboot to load: the XGS 136 was
    # asked for one on 2026-10-19 after an update that rebuilt the driver it was running.
    unchanged = any(path in loaded and md5(path) == loaded[path] for path in MODULES)
    say('')
    if unchanged:
        say('installed %s. The driver it built is the one running, so nothing waits for a reboot.'
            % (tag or sha[:12]))
    else:
        say('installed %s. The coprocessor driver takes effect at the next boot.' % (tag or sha[:12]))
    # The last check found this commit; it is installed now, so the page stops offering it.
    found.update({'installed': sha, 'compare': 'identical', 'ahead_by': 0, 'behind_by': 0,
                  'changes': [], 'update_available': False})
    write_json(CHECK, found)
    write_json(APPLY, {'status': 'done', 'sha': sha, 'started_at': started,
                       'finished_at': int(time.time()), 'driver_changed': not unchanged})
    logf.close()
    return out({'status': 'ok', 'sha': sha})


def channel(want):
    """What this appliance follows - 'releases', the default and the one for an appliance in service,
    or 'main': the newest commit on main whose checks all passed, for an owner who wants what is
    merged before it is released. Kept as the one-line file STATE/branch that branch() reads, so a
    shell and the Updates page set the same thing. The last check belonged to the other channel and
    is dropped, so the next one is made for this one. With nothing asked, says which it is."""
    path = os.path.join(STATE, 'branch')
    if want not in ('', 'releases', 'main'):
        return out({'status': 'error', 'message': 'a channel is releases or main, not %s' % want}) or 1
    if want:
        os.makedirs(STATE, exist_ok=True)
        if want == 'main':
            with open(path, 'w', encoding='utf-8') as handle:
                handle.write('main\n')
        elif os.path.exists(path):
            os.unlink(path)
        if os.path.exists(CHECK):
            os.unlink(CHECK)
    current = branch()
    return out({'status': 'ok', 'channel': current or 'releases'})


def main(argv):
    command = argv[1] if len(argv) > 1 else 'status'
    if command == 'channel':
        return channel(argv[2] if len(argv) > 2 else '')
    handlers = {'status': status, 'check': check, 'apply': apply, 'log': log}
    if command not in handlers:
        return out({'status': 'error', 'message': 'unknown command: %s' % command}) or 1
    return handlers[command]()


if __name__ == '__main__':
    sys.exit(main(sys.argv))
