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
with the installed one, and for an appliance more than twenty releases behind the pages of releases
before them (release_list) - and three more for each plugin on the Plugins tab. On 2026-10-20 the
three appliances of one lab, behind one address, used the hour up: "GitHub refused the request
(403): the hourly allowance for this address is used up". An address a provider shares among many
customers would be used up before any of them asked. So the check asks the same questions of
npuforge.org's copy of the API (website/worker/index.js, githubMirror), which asks GitHub once for
everybody every two minutes, with a token; when the site cannot answer, GitHub is asked itself, as
before. XGS_UPDATE_MIRROR='' asks GitHub alone. A test machine following a branch always does.

What is installed is never the site's word alone: before an update downloads anything, the
release's commit is confirmed with GitHub (tag_commit), and an update whose commit GitHub does not
confirm stops there.

SIGNED RELEASES (since 2026-11-07). A release is installed only as its owner signed it. Three files
are published with each release on GitHub: os-xgs-npu-<tag>.tar.gz, the tag's tree made by `git
archive` on the owner's machine; release.json, the manifest, naming the tag, its commit and its date
and the archive's SHA-256 and size; and release.json.sig, the manifest's Ed25519 signature, made with
a key only the owner holds - never in CI, never on GitHub. The appliance holds the public keys
(RELEASE_KEYS). The check reads the newest release's manifest - GitHub's download of the newest
release's file, not its API, so not the hourly allowance either - and offers it only when its
signature verifies with one of them, and it is dated after what is installed; the update reads the
manifest of that release again, downloads the archive it names, and installs it only when its SHA-256
and size are the ones signed. A release not signed yet is not offered. So neither npuforge.org's copy
of the API nor GitHub's API chooses what is installed any more: a copy that is down, wrong or in
other hands can no longer pick a commit, take an appliance back to an older release, or keep a newer
one from it. They are still asked for what the page shows beside the release - its notes, the
releases since the installed one, how far the installed commit is from it - and an answer they cannot
give leaves the page with less to show, the update as the manifest says (review of 2026-11-06, G3 and
U5). A test appliance may read a release's files from another place, named in the one-line file
STATE/releases-from (a URL, file:// too): what is installed must still be signed by a key in
RELEASE_KEYS, so the place decides nothing. Following a branch is as before: GitHub's API, directly.
"""
import datetime
import errno
import glob
import hashlib
import http.client
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.request
try:
    import fcntl
except ImportError:		# not on the appliance: the tests, run elsewhere
    fcntl = None

REPO = 'AbdelmonemAwad/os-xgs-npu'
GITHUB_API = 'https://api.github.com'
API = GITHUB_API + '/repos/' + REPO
MIRROR = os.environ.get('XGS_UPDATE_MIRROR', 'https://npuforge.org/api/github')
REFS = 'https://github.com/' + REPO + '.git/info/refs?service=git-upload-pack'
TARBALL = 'https://codeload.github.com/' + REPO + '/tar.gz/'
# A release's own files (SIGNED RELEASES above), and the public keys its manifest must be signed with:
# installed beside this script, each named by the start of its fingerprint.
RELEASES = 'https://github.com/' + REPO + '/releases'
RELEASE_KEYS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'release-keys')
OPENSSL = '/usr/bin/openssl'
MANIFEST = 'release.json'
MANIFEST_MAX = 64 << 10
ARCHIVE_MAX = 256 << 20
STATE = '/var/db/os-xgs-npu/update'
# The descriptor the update's lock is held on (take_apply_lock), and handed to install.sh on: it
# closes 9 for every daemon it starts, so that none of them holds the lock after the update.
LOCK_FD = 9


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
# install/kernel-follow.sh name the same two paths. Neither is an update's alone - every run of the
# follower compiles in that directory, and an install started by hand writes that account too - so
# install.sh starts the account with a line that names its run, BUILD_RUN and the rest, and prints
# the same line in its own output, which is the update's log (this_build).
BUILD_LOG = '/var/db/os-xgs-npu/install-build.log'
BUILDROOT = '/var/db/os-xgs-npu/build'
BUILD_RUN = "the driver's build is logged in "
# How far back to look for a commit whose checks passed. Unauthenticated, GitHub allows sixty
# requests an hour from one address, and each commit looked at is one request.
LOOK_BACK = 5
# How far back a check reads the releases (release_list): the twenty newest, as every check reads
# them, and only when the installed release is not among them, pages of a hundred - the first through
# npuforge.org's copy and the rest from GitHub itself, because the copy answers a list's first page
# and no other: it passes per_page alone (website/worker/index.js, mirrorRequest), so asked for a
# second page it would answer the first again. Five pages, the five hundred newest releases: at most
# four more of GitHub's sixty an hour while npuforge.org answers, and only on an appliance that far
# behind.
RELEASE_PAGES = 5
# The pauses before a download is tried again (apply_locked), so three tries in all. Issue 971's update
# ended at its download's first failure, the one try it had, and the connection failures the plugins'
# checks met came in bursts of a minute or two (plugins.py, ask_again): the third try starts two minutes
# after the first failed, past such a burst. No more than three: a link down for longer is an outage,
# which an update would wait out with the page showing its download step all the while; it says why it
# stopped instead, and the Update button is there again. Codeload is not the API, so the tries spend
# none of the sixty requests an hour.
DOWNLOAD_PAUSES = (30, 90)


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


class Unsigned(Exception):
    """A release not taken as signed: its manifest missing, its signature not verifying, or what the
    manifest says not a release of this project - the reason, said as it is."""


def releases_from():
    """Where a release's files are read: GitHub's releases, or the place a test appliance names."""
    try:
        with open(os.path.join(STATE, 'releases-from'), encoding='utf-8') as handle:
            place = handle.read().strip()
        if place:
            return place.rstrip('/')
    except OSError:
        pass
    return RELEASES


def release_url(tag, name):
    """A file published with release tag, or with the newest release when tag is ''."""
    if not tag:
        return '%s/latest/download/%s' % (releases_from(), name)
    return '%s/download/%s/%s' % (releases_from(), urllib.request.quote(tag, safe=''), name)


def release_bytes(tag, name, limit):
    """The file, read whole; one larger than limit is not a file of a release. A file missing from a
    place a test appliance names (file://) is GitHub's 404 for a file a release does not have."""
    url = release_url(tag, name)
    request = urllib.request.Request(url, headers={'User-Agent': 'os-xgs-npu-updates'})
    try:
        response = urllib.request.urlopen(request, timeout=60)
    except urllib.error.URLError as error:
        if isinstance(getattr(error, 'reason', None), FileNotFoundError):
            raise urllib.error.HTTPError(url, 404, 'Not Found', {}, None)
        raise
    with response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise Unsigned('%s of %s is larger than a release file of its kind can be'
                       % (name, tag or 'the newest release'))
    return data


def signature_key(data, signature):
    """The name of the public key in RELEASE_KEYS whose signature over data signature is - OpenSSL's
    own Ed25519 check - or '' when none is."""
    keys = sorted(glob.glob(os.path.join(RELEASE_KEYS, '*.pub')))
    if not keys or len(signature) != 64:
        return ''
    os.makedirs(STATE, exist_ok=True)
    paths = []
    try:
        for content, prefix in ((data, '.manifest-'), (signature, '.signature-')):
            fd, path = tempfile.mkstemp(dir=STATE, prefix=prefix)
            paths.append(path)
            with os.fdopen(fd, 'wb') as handle:
                handle.write(content)
        for key in keys:
            done = subprocess.run([OPENSSL, 'pkeyutl', '-verify', '-pubin', '-inkey', key, '-rawin',
                                   '-in', paths[0], '-sigfile', paths[1]],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
            if done.returncode == 0:
                return os.path.basename(key)
    finally:
        for path in paths:
            try:
                os.unlink(path)
            except OSError:
                pass
    return ''


FIELDS = {
    'repository': re.compile(re.escape(REPO) + r'\Z'),
    'tag': re.compile(r'v[0-9][0-9A-Za-z.+-]{0,62}\Z'),
    'commit': re.compile(r'[0-9a-f]{40}\Z'),
    'archive': re.compile(r'[A-Za-z0-9][A-Za-z0-9._+-]{0,126}\.tar\.gz\Z'),
    'sha256': re.compile(r'[0-9a-f]{64}\Z'),
}


def signed_manifest(tag=''):
    """The manifest of release tag, or of the newest release: read, its signature verified with a key
    in RELEASE_KEYS, and every field it must have checked. Its fields, and 'key', the key's name;
    Unsigned when it is not to be taken. A network failure is the caller's to say."""
    what = tag or 'the newest release'
    try:
        data = release_bytes(tag, MANIFEST, MANIFEST_MAX)
        signature = release_bytes(tag, MANIFEST + '.sig', 64)
    except urllib.error.HTTPError as error:
        if error.code == 404:
            raise Unsigned('%s is not signed yet: it has no %s' % (what, MANIFEST))
        raise
    key = signature_key(data, signature)
    if not key:
        raise Unsigned('the signature of %s does not verify with %s' % (
            what, 'any release key of this appliance' if glob.glob(os.path.join(RELEASE_KEYS, '*.pub'))
            else 'this appliance\'s release keys - it has none'))
    try:
        found = json.loads(data.decode('utf-8'))
    except ValueError:
        raise Unsigned('the manifest of %s, signed, is not JSON' % what)
    if not isinstance(found, dict) or found.get('format') != 1:
        raise Unsigned('the manifest of %s is of a format this update.py does not read' % what)
    for name, pattern in FIELDS.items():
        if not isinstance(found.get(name), str) or not pattern.match(found[name]):
            raise Unsigned('the manifest of %s has no %s, or not one of the form it must have' % (what, name))
    if not when(found.get('date')):
        raise Unsigned('the manifest of %s has no date' % what)
    if not isinstance(found.get('size'), int) or not 0 < found['size'] <= ARCHIVE_MAX:
        raise Unsigned('the manifest of %s has no size, or one no archive of it can have' % what)
    if tag and found['tag'] != tag:
        raise Unsigned('the manifest published with %s is the one of %s' % (tag, found['tag']))
    found['key'] = key
    return found


# The driver modules an install writes; whichever this family uses is there.
MODULES = ('/boot/modules/npuep.ko', '/boot/modules/octep.ko')
# The boot steps an install writes - the hooks under these, named for this project, which run once a
# boot: a change in one takes effect at the next boot, as a new driver does. Until 2026-10-25 only the
# driver was compared, and 0.1.0-alpha.8 asked the lab's three boards for no reboot - their driver was
# the release's already - while its boot steps had changed.
HOOK_DIRS = ('/usr/local/etc/rc.syshook.d/early', '/usr/local/etc/rc.syshook.d/start')
HOOK_NAMES = ('xgs', 'npu', 'octep')


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
                              text=True, errors='replace', timeout=10).stdout
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
    return boot_snapshot()['modules']


def boot_hooks():
    """The boot hooks of this project present now: their paths, sorted."""
    found = []
    for directory in HOOK_DIRS:
        try:
            names = sorted(os.listdir(directory))
        except OSError:
            continue
        found += [os.path.join(directory, name) for name in names
                  if any(key in name for key in HOOK_NAMES)]
    return found


def boot_snapshot():
    """What this boot loaded - {'modules': {path: md5}, 'hooks': {path: md5}} - taken, file by file,
    while a file is still older than the boot (loaded_modules says why), and kept in LOADED for the
    rest of the boot. 'hooks' is missing from a snapshot an older update.py kept: it knew only the
    driver."""
    bt = boottime()
    kept = read_json(LOADED, {}) or {}
    if kept.get('boottime') != bt:
        kept = {'boottime': bt, 'modules': {}, 'hooks': {}}
    kept.setdefault('modules', {})
    kept.setdefault('hooks', {})
    for path in MODULES:
        if path not in kept['modules'] and os.path.exists(path) and mtime(path) < bt:
            kept['modules'][path] = md5(path)
    for path in boot_hooks():
        if path not in kept['hooks'] and mtime(path) < bt:
            kept['hooks'][path] = md5(path)
    try:
        write_json(LOADED, kept)
    except OSError:
        pass
    return kept


def hooks_changed(snapshot):
    """The boot hooks an install changed against what this boot ran: a hook whose bytes differ, one
    added, one taken away. A hook written since the boot and not in the snapshot counts as changed - the
    boot did not run it as it is now."""
    before = snapshot.get('hooks') or {}
    now = boot_hooks()
    changed = [path for path in now if before.get(path) != md5(path)]
    changed += [path for path in before if path not in now]
    return sorted(set(changed))


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


def release_list(have_rel):
    """The releases published, newest first and drafts left out, read as far back as the installed
    one (RELEASE_PAGES): which of them are newer is where it stands among them. Until 2026-11-06 the
    twenty newest were all a check read, and an appliance whose release was further back was offered
    the newest with that one's notes alone, as one release since the installed one (the review's U15).
    A page beyond the first twenty that cannot be read ends the reading there, and the check answers
    with what it has, as it did before."""
    found = get('%s/releases?per_page=20' % API)
    if have_rel and len(found) >= 20 and not any(r.get('tag_name') == have_rel for r in found):
        try:
            for page in range(1, RELEASE_PAGES + 1):
                more = get('%s/releases?per_page=100%s' % (API, '&page=%d' % page if page > 1 else ''),
                           direct=page > 1)
                if not isinstance(more, list):
                    break
                found = found + more
                if len(more) < 100 or any(r.get('tag_name') == have_rel for r in more):
                    break
        except (urllib.error.URLError, OSError, ValueError):
            pass
    releases, seen = [], set()
    for r in found:
        if r.get('draft') or r.get('tag_name') in seen:
            continue
        seen.add(r.get('tag_name'))
        releases.append(r)
    releases.sort(key=lambda r: r.get('published_at') or '', reverse=True)
    return releases


def run_check():
    """The check, made now: its answer kept in CHECK for the page, and returned for apply()."""
    return check_branch() if BRANCH else check_releases()


def check():
    return out(run_check())


def check_releases():
    """The newest release, as its signed manifest says (SIGNED RELEASES), and whether it is to be
    installed here: offered when it is not what is installed and is not dated before it - only
    forward, as since issue 677, judged now by the date signed. What the page shows beside it is read
    after (describe) and decides nothing."""
    now = int(time.time())
    inst = installed()
    have, have_rel = inst.get('commit', ''), inst.get('release', '')
    try:
        signed = signed_manifest()
    except Unsigned as reason:
        result = {'status': 'error', 'checked_at': now, 'source': 'releases', 'message': str(reason)}
        write_json(CHECK, result)
        return result
    except urllib.error.HTTPError as error:
        result = {'status': 'error', 'checked_at': now, 'source': 'releases',
                  'message': "GitHub refused the newest release's manifest (%s)" % error.code}
        write_json(CHECK, result)
        return result
    except (urllib.error.URLError, OSError, ValueError) as error:
        result = {'status': 'error', 'checked_at': now, 'source': 'releases',
                  'message': 'could not reach GitHub: %s' % error}
        write_json(CHECK, result)
        return result
    tag, sha = signed['tag'], signed['commit']
    result = {
        'status': 'ok', 'checked_at': now, 'source': 'releases', 'installed': have,
        'installed_release': have_rel, 'signed_with': signed['key'],
        'target': {'sha': sha, 'tag': tag, 'name': tag, 'date': signed['date'], 'prerelease': False,
                   'url': '%s/tag/%s' % (RELEASES, tag), 'subject': tag},
    }
    same = have == sha or have_rel == tag
    if same:
        result.update({'compare': 'identical', 'ahead_by': 0, 'behind_by': 0})
    elif newer_than(inst.get('date', ''), signed['date']):
        # What is installed is dated after the newest release - a build from main, or from a branch
        # (issue 677): it is not taken back; the next release is offered.
        result['newer_than_target'] = True
    result['update_available'] = not same and not result.get('newer_than_target')
    try:
        describe(result, have, have_rel)
    except (urllib.error.URLError, OSError, ValueError, KeyError, TypeError):
        result.setdefault('compare', 'unknown')
        result.setdefault('ahead_by', None)
        result['changes'], result['releases_since'] = [], None
        result['notes'] = {'release': tag, 'date': signed['date'], 'lines': []}
    write_json(CHECK, result)
    return result


def describe(result, have, have_rel):
    """What the page shows beside the newest release, from GitHub's API through npuforge.org's copy:
    how far the installed commit is from it, every release since the installed one up to it, newest
    first, each by the lines of its notes, and its own notes. Read into result; it changes neither the
    release offered nor whether it is."""
    tag, sha = result['target']['tag'], result['target']['sha']
    releases = release_list(have_rel)
    tags = [r['tag_name'] for r in releases]
    upto = releases[tags.index(tag):] if tag in tags else []
    if 'compare' not in result:
        cmp = None
        if have:
            try:
                cmp = get('%s/compare/%s...%s' % (API, have, sha))
            except urllib.error.HTTPError as error:
                if error.code not in (404, 422):
                    raise
                result['installed_unknown'] = True
        if cmp is not None:
            result.update({'compare': cmp.get('status'), 'ahead_by': cmp.get('ahead_by', 0),
                           'behind_by': cmp.get('behind_by', 0)})
        else:
            result.update({'compare': 'unknown', 'ahead_by': None})
    utags = [r['tag_name'] for r in upto]
    newer = upto[:utags.index(have_rel)] if have_rel in utags else upto[:1]
    result['changes'] = [{'sha': r['tag_name'], 'release': r['tag_name'], 'date': r.get('published_at'),
                          'subject': line} for r in newer for line in release_lines(r)][:50]
    result['releases_since'] = len(newer)
    if upto:
        # The newest release's own notes, always: with nothing to update to, the page still shows
        # what the release it compares with carries, instead of an empty tab.
        result['target'].update({'name': upto[0].get('name') or tag, 'url': upto[0].get('html_url') or
                                 result['target']['url'], 'subject': upto[0].get('name') or tag})
        result['notes'] = {'release': tag, 'date': upto[0].get('published_at'), 'lines': release_lines(upto[0])}
    else:
        result['notes'] = {'release': tag, 'date': result['target']['date'], 'lines': []}


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


def this_build(log_lines):
    """The lines of the driver's build this update ran, out of BUILD_LOG - None when the account there
    is another run's. install.sh starts the account with a line naming its run - its process and the
    time - and prints that line in its own output too, which is this update's log (log_lines); an
    account whose first line is not among them is an earlier install's, one an install started by
    hand is writing, or not begun yet."""
    build = read_text(BUILD_LOG).splitlines()
    first = build[0].strip() if build else ''
    if not first.startswith(BUILD_RUN) or not any(line.strip() == first for line in log_lines):
        return None
    return build[1:]


def progress(state):
    """How far an update has got, read from what it leaves behind - never estimated from the clock.

    Six steps, each with its share of the bar: the download, the unpacking, the installer's own
    sections (each "== ... ==" header it prints is a step forward), the kernel sources made ready,
    the driver's build, and the finish. The build is most of the wait, and its share moves with the
    object files appearing beside the sources in the follower's build directory: five of nine
    compiled is five ninths of the way. Before 2026-10-18 the page showed one fixed line for all of
    it, and the log sat at "== kernel module ==" for minutes, because the build writes to a log of
    its own.

    Only what THIS update left counts. The build's account is read when it is this update's
    (this_build), and the build directory from the follower's "compiling" line on, while that is the
    account's last line and the update still runs: the follower holds its lock from before that line
    until it ends, so no other run of it compiles there meanwhile - before the line the directory
    holds the build before this one, and once the update has stopped, anybody's. Until 2026-11-06
    both were read whoever had written them, and an XGS 126's update that had stopped during the build
    showed 95%, "Building the driver", taken from what later runs had left in the account and the
    directory (#661). When this update's own traces cannot say how far it has got, the percentage is
    None, and the page shows the steps without a number."""
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
        build = this_build(lines)
        if build is None:
            percent = None
        elif any(line.startswith('building against') for line in build):
            step, percent = 'build', 40
            said = [line.rstrip() for line in build if line.strip()]
            if said and said[-1].startswith('compiling '):
                if state.get('status') != 'running':
                    percent = None
                else:
                    try:
                        sources = int(said[-1].split()[1])
                    except (IndexError, ValueError):
                        sources = 0
                    try:
                        names = os.listdir(BUILDROOT)
                    except OSError:
                        names = []
                    objects = min(sources, sum(1 for n in names if n.endswith('.o')))
                    if sources > 0:
                        percent = 40 + int(50 * objects / sources)
                        detail = '%d/%d' % (objects, sources)
            for line in build:
                if line.startswith('built '):
                    percent, detail = max(percent or 0, 92), ''
                elif line.startswith('installed '):
                    percent = max(percent or 0, 95)
    if any(header.startswith(('== loader', '== installed')) for header in headers):
        step, percent, detail = 'finish', max(percent or 0, 96), ''
    if state.get('status') == 'done':
        step, percent, detail = 'finish', 100, ''
    return {'step': step, 'percent': percent, 'detail': detail,
            'started_at': state.get('started_at'), 'finished_at': state.get('finished_at')}


def take_apply_lock():
    """The lock an update holds from its first step to its last - install.sh, which it runs, inherits
    it - so a 'running' record whose lock nobody holds is of an update that stopped: the appliance
    restarted under it, or it was killed. The descriptor, or None while another update holds it.

    It is held on descriptor 9 (LOCK_FD), which install.sh closes for every daemon it starts or
    restarts. Until 2026-11-06 install.sh got it under whatever number it had here, and devd and
    cron, which install.sh restarts, held it on after the update had ended - the next update was
    refused as already running until they were restarted (the review's U17). Where 9 is taken
    already - nothing in this script opens it - the lock stays on the descriptor it was opened on."""
    if fcntl is None:
        return -1
    os.makedirs(STATE, exist_ok=True)
    fd = os.open(os.path.join(STATE, 'apply.lock'), os.O_RDWR | os.O_CREAT, 0o600)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        os.close(fd)
        return None
    if fd != LOCK_FD and not fd_open(LOCK_FD):
        # a flock belongs to the open file, which the duplicate shares: closing the first keeps it
        os.dup2(fd, LOCK_FD, inheritable=False)
        os.close(fd)
        fd = LOCK_FD
    return fd


def fd_open(fd):
    try:
        os.fstat(fd)
        return True
    except OSError:
        return False


def let_go(fd):
    if fd is not None and fd >= 0:
        os.close(fd)


def runs_this_boot(state):
    """An update started by a version before the lock - one taking this version in - holds none: its
    own process, still there in this boot, is what says it runs. A process number from before this
    boot names nobody, or somebody else."""
    try:
        started = int(state.get('started_at') or 0)
        if started and started < boottime():
            return False
        os.kill(int(state.get('pid') or 0), 0)
        return True
    except (OSError, ValueError, TypeError):
        return False


def settled(state):
    """A 'running' record nobody runs any more, said for what it is: an update that stopped and
    was not completed. Until 2026-11-06 such a record stayed 'running' across every reboot - an XGS
    126's from 2026-10-31, whose update the appliance restarted under (#661) - and the page kept
    Check and Update off for good, so no update was started from it again."""
    if state.get('status') != 'running':
        return state
    lock = take_apply_lock()
    if lock is None:
        return state
    let_go(lock)
    if runs_this_boot(state):
        return state
    state = dict(state, status='failed', finished_at=int(time.time()),
                 message='the update stopped before it finished - the appliance restarted under it, '
                         'or it was stopped - and nothing after that step was done; check for updates '
                         'and update again')
    state.pop('pid', None)
    write_json(APPLY, state)
    return state


def status():
    # Take what this boot loaded while the files still show it, before any update rewrites them.
    snapshot = boot_snapshot()
    state = settled(read_json(APPLY, {}) or {})
    since_boot = bool(state.get('status') == 'done' and state.get('finished_at', 0) > boottime())
    driver_changed = state.get('driver_changed', True) is not False
    if 'reboot' in state:
        steps = state.get('hooks_changed') or []
        pending = since_boot and bool(state['reboot'])
    else:
        # An update made by an older update.py, which compared the driver only: the boot hooks are
        # compared here, as it would have. Updating from 0.1.0-alpha.8 is such an update, and it asks
        # for the reboot that 0.1.0-alpha.8's own boot steps were not given.
        steps = [os.path.basename(path) for path in hooks_changed(snapshot)] if since_boot else []
        pending = since_boot and (driver_changed or bool(steps))
    return out({
        'status': 'ok',
        'channel': BRANCH or 'releases',
        'installed': installed(),
        'check': read_json(CHECK, {}),
        'apply': state,
        'progress': progress(state),
        'pending_reboot': pending,
        'driver_unchanged': since_boot and not driver_changed,
        'boot_steps_changed': steps,
    })


def log():
    try:
        with open(LOG, encoding='utf-8', errors='replace') as handle:
            text = handle.read()
    except OSError:
        text = ''
    return out({'status': 'ok', 'log': text[-20000:]})


def apply():
    """One update, under the lock from its first step to its last (take_apply_lock). An error nobody
    expected - a download cut short (IncompleteRead), an archive that ends early (EOFError), a full
    disk - ended it with its record still 'running' and its reason nowhere, since daemon(8) sends this
    script's own output to /dev/null; it is recorded as the update's failure now. A download cut short
    is the download's own failure since 2026-11-06, and tried again (DOWNLOAD_PAUSES). However the
    update ends, what it made and did not move into place is taken away before the lock is let go
    (tidy)."""
    lock = take_apply_lock()
    if lock is None:
        return out({'status': 'error', 'message': 'an update is already running'})
    made = []
    try:
        return apply_locked(lock, made)
    except Exception as error:		# anything at all: the record must not stay 'running'
        message = 'the update stopped on an error: %s: %s' % (type(error).__name__, error)
        state = read_json(APPLY, {}) or {}
        if state.get('status') == 'running' and state.get('pid') == os.getpid():
            write_json(APPLY, dict(state, status='failed', message=message, finished_at=int(time.time())))
            try:
                with open(LOG, 'a', encoding='utf-8') as handle:
                    handle.write(message + '\n')
            except OSError:
                pass
        return out({'status': 'error', 'message': message})
    finally:
        tidy(made)
        let_go(lock)


def tidy(made):
    """What an update made in STATE - the archive it downloaded, the tree it unpacked from it - and
    did not move into place, taken away when it ends. Until 2026-11-06 a failed download left its
    archive behind, cut short, and a failed unpacking or install its tree, one more of each for every
    commit tried (issue 971; the review's U14). Only the paths apply_locked() put in made, each once it
    had made it: nothing the update did not create."""
    for path in reversed(made):
        if os.path.isdir(path) and not os.path.islink(path):
            shutil.rmtree(path, ignore_errors=True)
        elif os.path.lexists(path):
            try:
                os.unlink(path)
            except OSError:
                pass


def worth_again(error):
    """Whether a download that failed with error is tried again: not when codeload answered that it
    has no such archive - a 4xx, but for 408 and 429, which say to come back - and not when the disk
    here is full, where the next try fails as this one did."""
    if isinstance(error, urllib.error.HTTPError):
        return error.code >= 500 or error.code in (408, 429)
    return getattr(error, 'errno', None) not in (errno.ENOSPC, errno.EDQUOT)


def apply_locked(lock, made):
    state = read_json(APPLY, {}) or {}
    if state.get('status') == 'running' and runs_this_boot(state):
        return out({'status': 'error', 'message': 'an update is already running'})
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
    signed = None
    if tag and not BRANCH:
        # What is installed is what the release's owner signed (SIGNED RELEASES): its manifest read
        # again now, its signature verified, the commit it names the one the check found, and its
        # date not before what is installed.
        try:
            signed = signed_manifest(tag)
        except Unsigned as reason:
            return fail('%s; nothing was downloaded' % reason)
        except (urllib.error.URLError, OSError, ValueError) as error:
            return fail('the manifest of %s could not be read (%s); nothing was downloaded - try again '
                        'later' % (tag, error))
        if signed['commit'] != sha:
            return fail('the signed manifest of %s names commit %s, not the %s the check found; nothing '
                        'was downloaded - check for updates again' % (tag, signed['commit'][:12], sha[:12]))
        if newer_than(installed().get('date', ''), signed['date']):
            return fail('%s is dated before what is installed, and nothing is taken back; nothing was '
                        'downloaded' % tag)
        say('%s is commit %s, signed with %s' % (tag, sha[:12], signed['key']))
        # And GitHub's own word for the tag, as before signed releases: a tag moved since it was signed
        # stops the update too.
        try:
            confirmed = tag_commit(tag)
        except (urllib.error.URLError, OSError, ValueError, KeyError) as error:
            return fail('%s could not be confirmed with GitHub (%s); nothing was downloaded - try '
                        'again later' % (tag, error))
        if confirmed != sha:
            return fail('GitHub says %s is commit %s, not the %s it was signed as; nothing was '
                        'downloaded - check for updates again' % (tag, confirmed[:12], sha[:12]))
        say('%s is commit %s on GitHub' % (tag, sha[:12]))
    say('downloading %s %s' % (REPO, (tag + ', commit ' + sha[:12] + (', ' + signed['archive'] if signed else ''))
                               if tag else 'at ' + sha))
    # Tried up to three times, after a pause each time it fails (DOWNLOAD_PAUSES says why): until
    # 2026-11-06 one failure ended the update (the review's U14), and a download cut short
    # (IncompleteRead) was not even taken for the download's failure.
    tries = len(DOWNLOAD_PAUSES) + 1
    for attempt in range(1, tries + 1):
        try:
            # A release: the archive its manifest names, published with it, read as far as its signed
            # size and hashed on the way. A branch: by the commit the check found, never by a tag - a
            # tag can be moved between the check and this, and then what is installed would not be the
            # commit recorded as installed (until 2026-10-19 a release was downloaded by its tag).
            url = release_url(tag, signed['archive']) if signed else TARBALL + sha
            request = urllib.request.Request(url, headers={'User-Agent': 'os-xgs-npu-updates'})
            digest, size = hashlib.sha256(), 0
            with urllib.request.urlopen(request, timeout=120) as response, open(archive, 'wb') as handle:
                if archive not in made:
                    made.append(archive)
                while True:
                    chunk = response.read(1 << 16)
                    if not chunk:
                        break
                    size += len(chunk)
                    if signed and size > signed['size']:
                        break
                    digest.update(chunk)
                    handle.write(chunk)
            break
        except (urllib.error.URLError, OSError, http.client.HTTPException) as error:
            if attempt == tries or not worth_again(error):
                return fail('the download failed: %s%s' % (error, ' (tried %d times)' % attempt
                                                           if attempt > 1 else ''))
            pause = DOWNLOAD_PAUSES[attempt - 1]
            say('the download failed (%s); trying again in %d s, try %d of %d'
                % (error, pause, attempt + 1, tries))
            time.sleep(pause)

    if signed and (size != signed['size'] or digest.hexdigest() != signed['sha256']):
        return fail('the archive of %s is not the one signed: %s bytes, SHA-256 %s, where %d bytes, '
                    'SHA-256 %s were signed; nothing was installed' % (
                        tag, size if size <= signed['size'] else 'more than %d' % signed['size'],
                        digest.hexdigest()[:16] if size <= signed['size'] else '-', signed['size'],
                        signed['sha256'][:16]))
    if signed:
        say('the archive is the one signed: %d bytes, SHA-256 %s' % (size, signed['sha256'][:16]))
    say('unpacking')
    shutil.rmtree(tree, ignore_errors=True)
    os.makedirs(tree)
    made.append(tree)
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

    # The driver and the boot hooks this boot loaded, to tell afterwards whether the install changed
    # them (loaded_modules, boot_snapshot).
    snapshot = boot_snapshot()
    loaded = snapshot['modules']

    say('running its install/install.sh - this builds the driver and takes a few minutes')
    say('')
    # The installer inherits the lock: should this script be stopped while it runs, the update is
    # still running for as long as the installer is. On descriptor 9, which it keeps from the daemons
    # it starts (take_apply_lock).
    proc = subprocess.Popen(['/bin/sh', 'install/install.sh'], cwd=tree, stdout=logf,
                            stderr=subprocess.STDOUT, pass_fds=(lock,) if lock >= 0 else ())
    rc = proc.wait()
    if rc == 3:
        return fail('install/install.sh installed everything but the driver: its build failed (%s '
                    'says why), so the driver that runs is the one from before, and the next boot '
                    'loads it too. Nothing was rebooted; update again once the cause is fixed' % BUILD_LOG)
    if rc == 2:
        return fail('install/install.sh installed everything, but there is no driver for the kernel '
                    'the next boot will use: DO NOT REBOOT until one is built - the end of this log says '
                    'how. Nothing was rebooted')
    if rc != 0:
        return fail('install/install.sh failed with status %d; nothing was rebooted, and the '
                    'driver that is running stays as it was' % rc)

    # The source tree an image keeps, replaced by the one just installed, so the appliance
    # carries the sources of what it runs.
    if os.path.isdir(SOURCE):
        shutil.rmtree(SOURCE + '.prev', ignore_errors=True)
        os.rename(SOURCE, SOURCE + '.prev')
    shutil.move(tree, SOURCE)
    made.remove(tree)
    # A reboot loads a driver that is not the one running. When the build gave back, byte for
    # byte, the module the boot loaded, there is nothing for a reboot to load: the XGS 136 was
    # asked for one on 2026-10-19 after an update that rebuilt the driver it was running. Every
    # module there is counts, and only when each is the one the boot loaded is there nothing to load:
    # until 2026-11-06 one that was the same sufficed, and an octep.ko left on an ARMADA appliance,
    # which nothing there rebuilds, had a build that changed npuep.ko told that nothing waited for a
    # reboot (the review's U11; install.sh takes such a module off now).
    present = [path for path in MODULES if os.path.exists(path)]
    unchanged = bool(present) and all(path in loaded and md5(path) == loaded[path] for path in present)
    steps = [os.path.basename(path) for path in hooks_changed(snapshot)]
    say('')
    if not unchanged:
        say('installed %s. The coprocessor driver takes effect at the next boot.' % (tag or sha[:12]))
    elif steps:
        say('installed %s. The driver it built is the one running, but %d of its boot steps changed '
            '(%s): they take effect at the next boot.' % (tag or sha[:12], len(steps), ', '.join(steps)))
    else:
        say('installed %s. The driver it built is the one running and no boot step changed, so '
            'nothing waits for a reboot.' % (tag or sha[:12]))
    # The last check found this commit; it is installed now, so the page stops offering it.
    found.update({'installed': sha, 'compare': 'identical', 'ahead_by': 0, 'behind_by': 0,
                  'changes': [], 'update_available': False})
    write_json(CHECK, found)
    write_json(APPLY, {'status': 'done', 'sha': sha, 'started_at': started,
                       'finished_at': int(time.time()), 'driver_changed': not unchanged,
                       'hooks_changed': steps, 'reboot': (not unchanged) or bool(steps)})
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
