#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The project's appliance plugins and their releases - for the Plugins tab of XGS: Updates.

    plugins.py list               what is installed of each, what was last found and done, and the
                                  progress of the updates running now
    plugins.py check              ask GitHub for each installed plugin's latest release, keep it, print the list
    plugins.py check-start        what the Plugins tab's Check runs: the check started in the background,
                                  and the list printed at once, "checking" saying it is under way
    plugins.py update <name>      install that plugin's latest release, detached
    plugins.py update-all         install every plugin's latest release that the last check offered, one
                                  after the other, detached
    plugins.py install <name>     install a plugin of the family that is not here, from its latest
                                  release, detached
    plugins.py run <name> ...     what the two above start: the updates themselves, one at a time
    plugins.py sync               what the project's own update does after installing itself: every
                                  installed plugin to its latest release, and every one this appliance
                                  never had and that suits it installed - in the foreground, one at a time

THE PLUGINS COME ALONG WITH THE PROJECT'S UPDATE (since 2026-10-25), so that an owner does not have
to install each by hand. install/install.sh, when XGS: Updates runs it, ends with `plugins.py sync`.
A plugin this appliance has had and no longer has was taken away by its owner and is not brought
back: SEEN keeps every plugin found installed here, and only one never seen is installed - and
os-frontpanel only where there is a front panel, on the OCTEON TX family's rack models, as the images
have it. Installed means its self-update.py or its installed.json is here; an install that failed
leaves only an update.json in its share directory, and is tried again at the next update (until
0.1.0-alpha.8.4 that directory alone counted, and an install GitHub refused was never tried again).
A file AUTO holding "off" stops all of it; the Plugins tab is as it was.

EACH PLUGIN UPDATES ITSELF. Every one of them carries its own self-update.py - it reads the
plugin's latest release on GitHub, takes it only when it is ahead of what is installed and, since
2026-11-07, only as its owner signed it (each one's SIGNED RELEASES), and runs the installer the
release carries - and this only calls it: `self-update.py check` to ask, and `self-update.py` to
install. Nothing of a plugin is in this repository. Until 2026-10-20 each of them ran itself once a
day from cron and installed a new release on its own; since then an update is taken here, where the
owner sees it (each plugin's own release drops its cron entry). A plugin that is not here is installed
from its latest release only as signed too (SIGNED RELEASES below).

What a plugin has installed is its /usr/local/share/<name>/installed.json, what its last update did
its update.json, and the output of that update its update.log - all written by its own scripts.

THE PROGRESS IS READ, NOT REPORTED. A self-update says nothing until it ends, so the runner watches
what it leaves while it works, twice a second, and writes what it sees to PROGRESS for the page:
asking GitHub for the release, then the release's archive growing in its work directory under
/var/tmp (<name>-update.*), then the archive unpacked (tree/), then the installer's output in
update.log, then update.json written. How far along a step is comes from the plugin's last update -
its archive's size and its installer's lines, kept in PROGRESS - and from time where there was none.
The installers restart configd and the web GUI, so the runner is detached from both, and the page
keeps asking while the web GUI is away.

THE CHECK ASKS NPUFORGE.ORG FIRST, for the reason update.py gives: three requests of GitHub's API a
plugin, eleven plugins since 2026-10-25 (six before), sixty requests an hour from one address. Every plugin's self-update.py takes the
API's address from SELF_UPDATE_API, so the check hands it npuforge.org's copy of the API and nothing
of a plugin changes; when the copy cannot answer, the check is asked again of GitHub itself. A
self-update.py that checks signatures (each plugin's releases after 2026-11-07) asks neither: it reads
its release's signed manifest from GitHub's download of it, not the API, and is handed the copy's
address for nothing.

AN UPDATE ASKS THE COPY TOO, ONCE GITHUB HAS CONFIRMED IT (since 0.1.0-alpha.8.4). Until then every
update and install asked GitHub's API itself - two or three requests a plugin - and once the
project's update took the plugins along, every update of an appliance asked that for each of them: on
2026-10-25 an XGS 136 updated several times in an hour was refused ("GitHub refused the request
(403)") and one plugin was not installed. Now, before a plugin's self-update runs, the runner asks the copy for the plugin's latest
release and its commit, and GitHub's git endpoint - the reference list `git ls-remote` reads, which
is not the API and not its hourly allowance - for the commit that release's tag names. Only when the
two agree is the self-update handed the copy; otherwise it asks GitHub's API itself, as before. What
it installed is compared with the confirmed commit afterwards, and a difference is said.

ONE CHECK AT A TIME, IN THE BACKGROUND (since 2026-11-06). A check asks each plugin's self-update, up
to two minutes each, and configd answers the page for two minutes at most: a slow check came back to
the page as no answer, and the next Check ran beside the one still running - each putting back the
other's failure where a plugin keeps its last update's record (put_back), as the update's own check
(sync) could too. So a check holds CHECK_LOCK for as long as it runs, and one asked meanwhile waits
for it and takes its answer; and the page's Check starts it in the background (check-start), its
answer the list with "checking", which the page follows until it is gone.

THE UPDATE WAITS FOR A PLUGIN A WHILE, NOT FOR EVER (since 2026-11-07). sync ran the updates itself
and waited for each as long as it took: a plugin whose self-update never ended - its installer
waiting on pkg's lock, or on a configd that did not answer - kept the project's update "running" for
as long, and the next one could not start (the review's U8). A self-update is never stopped - pkg cut
in the middle of an install can leave the plugin half installed and pkg's database locked - so it is
the wait that ends: sync starts the runner as the Plugins tab does, detached, and waits for it until
one plugin has run SYNC_WAIT seconds. Then it says which plugin is still at it and which are queued
after it, and the update ends; the runner goes on with them, and the Plugins tab shows when they are
done.
"""

import fcntl
import glob
import hashlib
import http.client
import json
import os
import posixpath
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

SHARE = '/usr/local/share'
MIRROR = os.environ.get('XGS_UPDATE_MIRROR', 'https://npuforge.org/api/github')
STATE = '/var/db/os-xgs-npu/plugins-check.json'
PROGRESS = '/var/db/os-xgs-npu/plugins-update.json'
# What a release's read can end in, said as the reason rather than a traceback: a connection that failed,
# a download cut short (IncompleteRead), and OpenSSL that did not answer in time - as update.py's check
# catches them since 0.1.0-alpha.15.1 (the release check's review, which found these three escaping
# here: an update's plugins.py sync then ended in a traceback, and took no plugin along).
READ_FAILED = (urllib.error.URLError, OSError, ValueError, http.client.HTTPException, subprocess.SubprocessError)
SEEN = '/var/db/os-xgs-npu/plugins-seen.json'
AUTO = '/var/db/os-xgs-npu/plugins-auto'
# a check under way (ONE CHECK AT A TIME above): CHECK_LOCK held by it, and CHECKING written by
# check-start a moment before the check it starts has the lock - counted for CHECK_STARTING seconds
CHECK_LOCK = '/var/run/xgs-plugins-check.lock'
CHECKING = '/var/run/xgs-plugins-check.json'
CHECK_STARTING = 15
WORK = '/var/tmp'
# a check whose connection failed within RETRY_FAST seconds is asked again after RETRY_PAUSE (ask_again)
RETRY_FAST = 15
RETRY_PAUSE = 3
# The project's update waits this long at most for any one plugin (sync, THE UPDATE WAITS FOR A PLUGIN
# A WHILE above); the runner it started says it runs within RUNNER_START; and the wait reads the
# runner's progress every WAIT_STEP seconds.
SYNC_WAIT = 15 * 60
RUNNER_START = 60
WAIT_STEP = 1
# name, where its self-update.py is installed, what it is
PLUGINS = [
    ('opnsense-arabic', '/usr/local/share/opnsense-arabic/self-update.py', 'the Arabic translation'),
    ('opnsense-rtl', '/root/opnsense-rtl/self-update.py', 'right-to-left layout'),
    ('os-fanctl', '/usr/local/opnsense/scripts/fanctl/self-update.py', 'fan control'),
    ('os-frontpanel', '/usr/local/opnsense/scripts/frontpanel/self-update.py', 'the front panel'),
    ('os-linkhealth', '/usr/local/opnsense/scripts/linkhealth/self-update.py', 'link health'),
    ('os-netreport', '/usr/local/opnsense/scripts/netreport/self-update.py', 'the network report'),
    ('os-watchdog', '/usr/local/opnsense/scripts/watchdog/self-update.py', 'the hardware watchdog'),
    ('os-kernelfollow', '/usr/local/opnsense/scripts/kernelfollow/self-update.py', 'the kernel follower'),
    ('os-blocklists', '/usr/local/opnsense/scripts/blocklists/self-update.py', 'block lists'),
    ('os-portguard', '/usr/local/opnsense/scripts/portguard/self-update.py', 'port guard'),
    ('os-fallbackcrypto', '/usr/local/opnsense/scripts/fallbackcrypto/self-update.py', 'fallback crypto'),
]
OWNER = 'AbdelmonemAwad'
GITHUB = 'https://api.github.com'
# INSTALLING ONE THAT IS NOT HERE (2026-10-25). A plugin of the family that is not installed is listed
# too, with its latest release, and Install installs it the way an update does: its own self-update.py,
# taken out of that release's signed archive into a work directory (SIGNED RELEASES below), run by the
# same runner - which finds nothing installed and installs the release with the installer it carries.
# Only the family's os-* plugins, whose script sits under /usr/local/opnsense/scripts as in their
# repositories' src/; the Arabic catalogue and the right-to-left layout come with the image. Until then
# a plugin added to the family after an appliance was installed could only be installed from a shell.
INSTALLER_DIR = os.path.join(WORK, '%s-installer')
# SIGNED RELEASES (since 2026-11-07, the review's G3). A plugin that is not here is installed from its
# latest release only as signed, as an installed one has updated itself since then: the release's
# manifest and its signature are read from the release's own downloads - not GitHub's API, nor
# npuforge.org's copy of it - and the signature verified with this project's own release keys
# (RELEASE_KEYS: one key signs the whole family); the archive the manifest names is downloaded and its
# SHA-256 and size checked against the signed ones; and the plugin's self-update.py and its release keys
# are taken out of that archive into its installer directory, where the runner starts it and it checks
# the release again with those keys. Until then the self-update.py was fetched from
# raw.githubusercontent.com at the release's tag, as it came, and run as root. A test appliance may read
# the plugins' releases from another place, named in the one-line file RELEASES_FROM (a URL, file://
# too; each plugin's under its name): what is installed must still be signed with a key in RELEASE_KEYS.
RELEASE_KEYS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'release-keys')
RELEASES_FROM = '/var/db/os-xgs-npu/plugins-releases-from'
OPENSSL = '/usr/bin/openssl'
MANIFEST = 'release.json'
MANIFEST_MAX = 64 << 10
ARCHIVE_MAX = 256 << 20
FIELDS = {
    'tag': re.compile(r'v[0-9][0-9A-Za-z.+-]{0,62}\Z'),
    'commit': re.compile(r'[0-9a-f]{40}\Z'),
    'archive': re.compile(r'[A-Za-z0-9][A-Za-z0-9._+-]{0,126}\.tar\.gz\Z'),
    'sha256': re.compile(r'[0-9a-f]{64}\Z'),
}
# Where each step starts on the bar, in percent; a step moves on toward the next one's start.
STEPS = [('queued', 0), ('check', 3), ('download', 12), ('unpack', 45), ('install', 52), ('done', 100)]
STEP_AT = dict(STEPS)
# A step that has no history to go by fills as if it took this long, in seconds.
GUESS = {'check': 3, 'download': 5, 'unpack': 2, 'install': 20}


def read_json(path):
    try:
        with open(path, encoding='utf-8') as handle:
            return json.load(handle) or {}
    except (OSError, ValueError):
        return {}


def write_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path + '.tmp', 'w', encoding='utf-8') as handle:
        json.dump(data, handle)
    os.replace(path + '.tmp', path)


def running(script):
    """Is this plugin's self-update running now? Its lock file is held while it runs."""
    try:
        done = subprocess.run(['/bin/pgrep', '-f', script], capture_output=True, text=True, errors='replace', timeout=10)
    except (OSError, subprocess.SubprocessError):
        return False
    return done.returncode == 0 and bool(done.stdout.strip())


def alive(pid):
    try:
        os.kill(int(pid), 0)
        return True
    except (OSError, ValueError, TypeError):
        return False


def runner_alive(prog):
    return bool(prog.get('runner')) and alive(prog.get('runner'))


def source_path(script):
    """Where an installed self-update.py sits in its repository: src/ maps to /usr/local. None for a
    plugin installed anywhere else, which is not one Install can install."""
    prefix = '/usr/local/opnsense/scripts/'
    return 'src/opnsense/scripts/' + script[len(prefix):] if script.startswith(prefix) else None


class Unsigned(ValueError):
    """A plugin's release not taken as signed (SIGNED RELEASES above) - the reason, said as it is."""


def releases_of(name):
    """Where a plugin's release files are read: its releases on GitHub, or the place a test appliance
    names in RELEASES_FROM, the plugin's under its name."""
    try:
        with open(RELEASES_FROM, encoding='utf-8') as handle:
            place = handle.read().strip()
        if place:
            return '%s/%s' % (place.rstrip('/'), name)
    except OSError:
        pass
    return 'https://github.com/%s/%s/releases' % (OWNER, name)


def release_url(name, tag, file):
    """A file published with a plugin's release tag, or with its latest release when tag is ''."""
    if not tag:
        return '%s/latest/download/%s' % (releases_of(name), file)
    return '%s/download/%s/%s' % (releases_of(name), urllib.parse.quote(tag, safe=''), file)


def release_file(name, file, limit, tag=''):
    """A file of a plugin's latest release - or of release tag - read whole; one larger than limit is
    not a file of a release. One missing from a test appliance's place (file://) is GitHub's 404 for a
    file a release does not have."""
    url = release_url(name, tag, file)
    try:
        answer = urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'os-xgs-npu'}),
                                        timeout=60)
    except urllib.error.URLError as error:
        if isinstance(getattr(error, 'reason', None), FileNotFoundError):
            raise urllib.error.HTTPError(url, 404, 'Not Found', {}, None) from error
        raise
    with answer:
        data = answer.read(limit + 1)
    if len(data) > limit:
        raise Unsigned('%s of its latest release is larger than a release file of its kind can be' % file)
    return data


def signature_key(data, signature):
    """The name of the public key in RELEASE_KEYS whose Ed25519 signature over data signature is -
    OpenSSL's own check - or '' when none is."""
    keys = sorted(glob.glob(os.path.join(RELEASE_KEYS, '*.pub')))
    if not keys or len(signature) != 64:
        return ''
    paths = []
    try:
        for content, suffix in ((data, '.manifest'), (signature, '.sig')):
            fd, path = tempfile.mkstemp(dir=WORK, prefix='xgs-plugin-', suffix=suffix)
            paths.append(path)
            with os.fdopen(fd, 'wb') as handle:
                handle.write(content)
        for key in keys:
            done = subprocess.run([OPENSSL, 'pkeyutl', '-verify', '-pubin', '-inkey', key, '-rawin', '-in',
                                   paths[0], '-sigfile', paths[1]], stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL, timeout=30, check=False)
            if done.returncode == 0:
                return os.path.basename(key)
    finally:
        for path in paths:
            try:
                os.unlink(path)
            except OSError:
                pass
    return ''


def named_tag(data):
    """The tag a manifest not yet verified names, as only where to read its signature; '' when it
    names none of the form a tag has, and the latest release's signature is read as before."""
    try:
        found = json.loads(data.decode('utf-8'))
    except (ValueError, RecursionError):
        return ''
    tag = found.get('tag') if isinstance(found, dict) else None
    return tag if isinstance(tag, str) and FIELDS['tag'].match(tag) else ''


def plugin_manifest(name):
    """The manifest of a plugin's latest release: read, its signature verified with a key in
    RELEASE_KEYS, and every field it must have checked. Its fields, and 'key', the key's name; Unsigned
    when it is not to be taken. A network failure is the caller's to say."""
    missing = MANIFEST
    try:
        data = release_file(name, MANIFEST, MANIFEST_MAX)
        missing = MANIFEST + '.sig'
        # its signature by the tag the manifest names, not through 'latest' a second time: a release
        # published between the two reads paired one release's manifest with another's signature (the
        # release check's review, K9). The tag only says where to look; the signature still decides.
        signature = release_file(name, missing, 64, tag=named_tag(data))
    except urllib.error.HTTPError as error:
        if error.code == 404:
            raise Unsigned('its latest release is not signed yet: it has no %s' % missing) from error
        raise
    key = signature_key(data, signature)
    if not key:
        raise Unsigned('the signature of its latest release does not verify with %s' % (
            'any release key of this appliance' if glob.glob(os.path.join(RELEASE_KEYS, '*.pub'))
            else "this appliance's release keys - it has none"))
    try:
        found = json.loads(data.decode('utf-8'))
    except ValueError as error:
        raise Unsigned('the manifest of its latest release, signed, is not JSON') from error
    if not isinstance(found, dict) or found.get('format') != 1:
        raise Unsigned('the manifest of its latest release is of a format this plugins.py does not read')
    if found.get('repository') != '%s/%s' % (OWNER, name):
        raise Unsigned('the manifest of its latest release is of another repository')
    for field, pattern in FIELDS.items():
        if not isinstance(found.get(field), str) or not pattern.match(found[field]):
            raise Unsigned('the manifest of its latest release has no %s, or not one of the form it must have'
                           % field)
    size = found.get('size')
    if not isinstance(size, int) or isinstance(size, bool) or not 0 < size <= ARCHIVE_MAX:
        raise Unsigned('the manifest of its latest release has no size, or one no archive of it can have')
    found['key'] = key
    return found


def latest_release(name):
    """A plugin's latest release, for one that is not installed and so has no self-update.py to ask: as
    its signed manifest names it (SIGNED RELEASES above). A release that is not signed is not offered,
    and the reason is the row's."""
    try:
        found = plugin_manifest(name)
    except Unsigned as reason:
        return {'status': 'failed', 'message': str(reason)}
    except urllib.error.HTTPError as error:
        return {'status': 'failed', 'message': "GitHub refused its latest release's manifest (%s)" % error.code}
    except READ_FAILED as error:
        return {'status': 'failed', 'message': 'could not reach GitHub: %s' % error}
    return {'status': 'found', 'tag': found['tag'], 'sha': found['commit'], 'installed': '', 'take': False,
            'signed_with': found['key']}


def fetch_installer(name, script):
    """The plugin's own self-update.py and the release keys beside it, out of its latest release's
    signed archive (SIGNED RELEASES above), into its installer directory; the self-update.py's path.
    Unsigned when the release is not to be taken - nothing is left then - and a network failure the
    caller's to say."""
    source = source_path(script)
    if source is None:
        raise ValueError('%s is not one this page can install' % name)
    keys = posixpath.dirname(source) + '/release-keys/'
    found = plugin_manifest(name)
    work = INSTALLER_DIR % name
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(work, 'release-keys'))
    archive = os.path.join(work, 'release.tar.gz')
    target = os.path.join(work, 'self-update.py')
    try:
        # the archive the manifest names, read as far as its signed size and hashed on the way
        request = urllib.request.Request(release_url(name, found['tag'], found['archive']),
                                         headers={'User-Agent': 'os-xgs-npu'})
        digest, size = hashlib.sha256(), 0
        with urllib.request.urlopen(request, timeout=120) as answer, open(archive, 'wb') as handle:
            while True:
                chunk = answer.read(1 << 16)
                if not chunk:
                    break
                size += len(chunk)
                if size > found['size']:
                    break
                digest.update(chunk)
                handle.write(chunk)
        if size != found['size'] or digest.hexdigest() != found['sha256']:
            raise Unsigned('the archive of its latest release, %s, is not the one signed; nothing was taken '
                           'from it' % found['tag'])
        # self-update.py where the repository has it, and the public keys of the directory beside it -
        # each by its own name, so nothing of the archive is written anywhere but here
        keys_taken = 0
        try:
            tar = tarfile.open(archive)
        except tarfile.TarError as error:
            raise ValueError('the signed archive of %s cannot be read: %s' % (found['tag'], error)) from error
        with tar:
            for member in tar.getmembers():
                inner = member.name.split('/', 1)[1] if '/' in member.name else ''
                if not member.isfile() or not inner:
                    continue
                if inner == source:
                    dest = target
                elif (inner.startswith(keys) and inner.endswith('.pub') and '/' not in inner[len(keys):]
                      and not inner[len(keys):].startswith('.')):
                    dest = os.path.join(work, 'release-keys', inner[len(keys):])
                    keys_taken += 1
                else:
                    continue
                with tar.extractfile(member) as part, open(dest + '.tmp', 'wb') as handle:
                    shutil.copyfileobj(part, handle)
                os.replace(dest + '.tmp', dest)
        os.unlink(archive)
        if not os.path.isfile(target) or not keys_taken:
            raise Unsigned('the signed archive of %s has no %s, or no release key beside it' % (found['tag'], source))
    except BaseException:
        shutil.rmtree(work, ignore_errors=True)
        raise
    return target


def failed_install(name, script):
    """Whether all that is here of name is an install that failed: no self-update.py and no
    installed.json, only the share directory its self-update wrote its update.json in. A plugin's
    uninstall.sh takes that whole directory away, so this is not one its owner removed."""
    share = os.path.join(SHARE, name)
    return (not os.path.isfile(script) and os.path.isdir(share)
            and not os.path.isfile(os.path.join(share, 'installed.json')))


def remember_installed():
    """Every plugin found installed here - its self-update.py or its installed.json - kept in SEEN with
    those found before. One whose install failed was never installed: it is taken off SEEN, where the
    share directory alone put it until 0.1.0-alpha.8.4, and the next update tries it again."""
    kept = read_json(SEEN)
    seen = set(kept.get('installed') or [])
    now = {name for name, script, _ in PLUGINS
           if os.path.isfile(script) or os.path.isfile(os.path.join(SHARE, name, 'installed.json'))}
    failed = {name for name, script, _ in PLUGINS if failed_install(name, script)}
    keep = (seen - failed) | now
    if keep != seen:
        try:
            write_json(SEEN, {'installed': sorted(keep), 'at': int(time.time())})
        except OSError:
            pass
    return keep


def refs_commit(name, tag):
    """The commit tag names in name's repository, as GitHub's git endpoint lists it - the references
    `git ls-remote` reads, not the API, so not its hourly allowance and not the site's copy of it. An
    annotated tag's "^{}" line, the commit it points to, is the one taken. '' when the list cannot be
    read or has no such tag."""
    url = 'https://github.com/%s/%s.git/info/refs?service=git-upload-pack' % (OWNER, name)
    try:
        request = urllib.request.Request(url, headers={'User-Agent': 'git/2 os-xgs-npu'})
        with urllib.request.urlopen(request, timeout=30) as answer:
            data = answer.read(8 << 20)
        refs, at = {}, 0
        while at + 4 <= len(data):
            size = int(data[at:at + 4], 16)
            if size < 4:
                at += 4
                continue
            line = data[at + 4:at + size].split(b'\0')[0].strip().decode('ascii', 'replace')
            at += size
            if len(line) > 41 and line[40] == ' ':
                refs[line[41:]] = line[:40]
    except READ_FAILED:
        return ''
    return refs.get('refs/tags/%s^{}' % tag) or refs.get('refs/tags/%s' % tag) or ''


def confirmed_copy(name):
    """The commit of name's latest release when npuforge.org's copy of GitHub's API and GitHub's git
    endpoint agree on it: the copy may then answer that plugin's update. '' when they differ or either
    cannot be asked - the update then asks GitHub's API itself."""
    if not MIRROR:
        return ''

    def ask_copy(path):
        request = urllib.request.Request(MIRROR + path, headers={'Accept': 'application/vnd.github+json',
                                                                 'User-Agent': 'os-xgs-npu'})
        with urllib.request.urlopen(request, timeout=20) as answer:
            return json.loads(answer.read().decode('utf-8'))

    # Whatever the copy answers, never the runner ended by it: an answer cut short (IncompleteRead, an
    # HTTPException) or nested deeper than the parser goes (RecursionError) ended run() after it had written
    # the job as running - that plugin and the rest of the queue not updated (the release check before
    # 0.1.0-alpha.15.1, its third review: K32).
    try:
        tag = ask_copy('/repos/%s/%s/releases/latest' % (OWNER, name))['tag_name']
        sha = ask_copy('/repos/%s/%s/commits/%s' % (OWNER, name, urllib.parse.quote(tag, safe='')))['sha']
    except (*READ_FAILED, KeyError, TypeError, RecursionError):
        return ''
    return sha if sha and refs_commit(name, tag) == sha else ''


_SUITS = {}


def suits(name):
    """Whether a plugin belongs on this appliance: os-frontpanel only on the OCTEON TX family's rack
    models, which have a front panel - the images put it only there too. A desktop is not offered it,
    here or by the update (an XGS 136 was, on 2026-10-25, and its installer stopped for want of LCDproc)."""
    if name != 'os-frontpanel':
        return True
    if name in _SUITS:
        return _SUITS[name]
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import models
        assembly, _ = models.this()
        _SUITS[name] = models.MODELS.get(assembly, ('', ''))[1] == 'octeontx'
    except Exception:
        _SUITS[name] = False
    return _SUITS[name]


def listing():
    remember_installed()
    found = read_json(STATE)
    prog = read_json(PROGRESS)
    busy = runner_alive(prog)
    jobs = prog.get('jobs') or {}
    rows = []
    for name, script, what in PLUGINS:
        absent = not os.path.isfile(script)
        if absent and (source_path(script) is None or not suits(name)):
            continue
        have = read_json(os.path.join(SHARE, name, 'installed.json')) if not absent else {}
        last = read_json(os.path.join(SHARE, name, 'update.json'))
        seen = (found.get('plugins') or {}).get(name) or {}
        commit = have.get('commit') or ''
        # The release installed, by name: the last update's when it installed this commit, else the
        # last check's when it found it.
        tag = ''
        if last.get('sha') and last.get('sha') == commit:
            tag = last.get('tag') or ''
        elif seen.get('sha') and seen.get('sha') == commit:
            tag = seen.get('tag') or ''
        # What the last check offered, against what is installed NOW: an update since the check
        # installed it, and it is offered no more (until 2026-10-23 it stayed offered, the row
        # unchanged, until the next check).
        take = (not absent and bool(seen.get('take')) and bool(seen.get('sha'))
                and seen.get('sha') != commit)
        # A failure older than a check that has since found the release, with nothing left to take,
        # is no news: what a check before 2026-10-31 wrote there stays until the next update.
        if (last.get('status') == 'failed' and seen.get('status') != 'failed' and seen.get('tag')
                and (found.get('at') or 0) > (last.get('at') or 0) and not take):
            last = {}
        job = dict(jobs.get(name) or {})
        # a job queued a moment ago waits for its runner to start; one older with no runner died
        fresh = job.get('status') == 'queued' and time.time() - (job.get('queued_at') or 0) < 15
        if job.get('status') in ('queued', 'running') and not busy and not fresh:
            job['status'] = 'failed'
            job['message'] = 'the update stopped before it finished'
        rows.append({
            'name': name,
            'what': what,
            'installed': commit[:12],
            'installed_tag': tag,
            'installed_date': have.get('date') or '',
            'installed_at': have.get('installed_at'),
            'latest': seen.get('tag') or '',
            'latest_sha': (seen.get('sha') or '')[:12],
            'compare': seen.get('compare') or '',
            'take': take,
            'check_status': seen.get('status') or ('found' if seen.get('tag') else ''),
            'check_message': seen.get('message') or '',
            'last_status': last.get('status') or '',
            'last_message': last.get('message') or '',
            'last_at': last.get('at'),
            'running': (not absent and running(script)) or job.get('status') in ('queued', 'running'),
            'job': job,
            'not_installed': absent,
            'installable': absent and seen.get('status') == 'found',
        })
    queue = prog.get('queue') or []
    finished = sum(1 for n in queue if (jobs.get(n) or {}).get('status') in ('done', 'current', 'failed'))
    pending = busy or any(row['job'].get('status') in ('queued', 'running') for row in rows)
    return {
        'checked_at': found.get('at'),
        'plugins': rows,
        'queue': {'names': queue, 'finished': finished, 'running': pending,
                  'started_at': prog.get('started_at'), 'finished_at': prog.get('finished_at')},
        # the check under way, {'since': ...}, or None (ONE CHECK AT A TIME above)
        'checking': checking(),
        'now': int(time.time()),
    }


def check_locked():
    """Whether a check holds CHECK_LOCK now. Asked with a shared lock, given back at once: a check that
    comes for the lock meanwhile waits that moment, and is never refused for it."""
    try:
        fd = os.open(CHECK_LOCK, os.O_RDONLY)
    except OSError:
        return False
    try:
        fcntl.flock(fd, fcntl.LOCK_SH | fcntl.LOCK_NB)
    except OSError:
        return True
    finally:
        os.close(fd)
    return False


def checking():
    """The check under way, {'since': when it was started}, or None: one that holds CHECK_LOCK, or one
    check-start started less than CHECK_STARTING seconds ago that has not taken it yet."""
    mark = read_json(CHECKING)
    since = mark.get('since') if isinstance(mark, dict) and isinstance(mark.get('since'), int) else None
    if check_locked() or (since is not None and 0 <= time.time() - since < CHECK_STARTING):
        return {'since': since}
    return None


def check_start():
    """The Plugins tab's Check, through configd ([plugins.check]): the check started in the background -
    detached by daemon(8), as an update is - unless one is under way, and the list answered at once."""
    if checking() is None:
        try:
            write_json(CHECKING, {'since': int(time.time())})
            subprocess.Popen(['/usr/sbin/daemon', '-f', '/usr/local/bin/python3', os.path.abspath(__file__), 'check'],
                             stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            try:
                os.unlink(CHECKING)
            except OSError:
                pass
    return listing()


def ask(script, api):
    """One plugin's `self-update.py check`, against the API at api ('' for GitHub's own)."""
    env = dict(os.environ)
    if api:
        env['SELF_UPDATE_API'] = api
    try:
        done = subprocess.run(['/usr/local/bin/python3', script, 'check'], capture_output=True,
                              text=True, errors='replace', timeout=120, env=env)
        return json.loads(done.stdout.strip().splitlines()[-1]) if done.stdout.strip() else {}
    except (OSError, subprocess.SubprocessError, ValueError, IndexError) as failure:
        return {'status': 'failed', 'message': str(failure)}


def ask_again(script, api):
    """ask(), and once more a few seconds later when the connection itself failed fast - a reset, an
    end of the TLS stream. Those came in bursts of a minute or two (2026-10-30 on the first XGS 3300,
    2026-10-31 on the XGS 136), and there the next ask got through for two plugins of three. A failure
    that took long, a timeout, is not asked again: the page's check waits for every plugin."""
    started = time.monotonic()
    answer = ask(script, api)
    if (answer.get('status') == 'failed' and time.monotonic() - started < RETRY_FAST
            and str(answer.get('message') or '').startswith('could not reach GitHub')):
        time.sleep(RETRY_PAUSE)
        answer = ask(script, api)
    return answer


def put_back(path, kept, since):
    """A check's failure taken out of where the plugin keeps what its last update did, and what was
    there put back - only a failure written since the check began, so an update that finished
    meanwhile keeps its own record."""
    try:
        if os.path.getmtime(path) < since:
            return
        with open(path, encoding='utf-8') as handle:
            if json.load(handle).get('status') != 'failed':
                return
    except (OSError, ValueError, AttributeError):
        return
    try:
        if kept is None:
            os.unlink(path)
        else:
            with open(path, 'wb') as handle:
                handle.write(kept)
    except OSError:
        pass


def check():
    """Each plugin asked for its latest release (ask_each), and the list - one check at a time
    (CHECK_LOCK): one asked while another runs waits for it, and takes its answer rather than asking
    GitHub again. CHECKING, the mark of the check-start that asked for it, goes once it is done."""
    asked = int(time.time())
    try:
        fd = os.open(CHECK_LOCK, os.O_RDWR | os.O_CREAT, 0o644)
    except OSError:
        fd = None		# no lock to be had: checked as before 2026-11-06 rather than not at all
    try:
        waited = False
        if fd is not None:
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError:
                fcntl.flock(fd, fcntl.LOCK_EX)
                waited = True
        if not waited or (read_json(STATE).get('at') or 0) < asked:
            ask_each()
        mark = read_json(CHECKING)
        # a mark made after this check began asks for a later one: left for it
        if isinstance(mark, dict) and isinstance(mark.get('since'), int) and mark['since'] <= asked:
            try:
                os.unlink(CHECKING)
            except OSError:
                pass
    finally:
        if fd is not None:
            os.close(fd)
    return listing()


def ask_each():
    """Each installed plugin's self-update asked for its latest release, and GitHub for the latest
    release of each one not here; the answers kept in STATE."""
    seen = {}
    for name, script, _ in PLUGINS:
        if not os.path.isfile(script):
            if source_path(script) is not None:
                seen[name] = latest_release(name)
            continue
        # A check that fails writes its failure where the plugin keeps what its last update did. A
        # check is not an update: its failure is the check's answer, shown as that until the next
        # check, and the last update's record is put back - until 2026-10-31 a reset stood there as
        # the last update a day after later checks had found the release.
        last = os.path.join(SHARE, name, 'update.json')
        since = int(time.time())
        try:
            with open(last, 'rb') as handle:
                kept = handle.read()
        except OSError:
            kept = None
        answer = ask_again(script, MIRROR) if MIRROR else {'status': 'failed'}
        if answer.get('status') == 'failed':
            answer = ask_again(script, '')
        put_back(last, kept, since)
        seen[name] = answer
    write_json(STATE, {'at': int(time.time()), 'plugins': seen})


def start(names, install=False):
    """The updates of names, one after the other, by a runner detached from configd and the web GUI;
    with install, the installs of plugins that are not here, each by its release's own self-update."""
    prog = read_json(PROGRESS)
    if runner_alive(prog):
        return {'status': 'busy', 'message': 'updates are running: %s' % ', '.join(prog.get('queue') or [])}
    scripts = {}
    for name in names:
        script = next((s for n, s, _ in PLUGINS if n == name), None)
        if script is None:
            return {'status': 'failed', 'message': '%s is not a plugin of the family' % name}
        if install:
            if os.path.isfile(script):
                return {'status': 'failed', 'message': '%s is installed already: Update takes its releases' % name}
            if source_path(script) is None:
                return {'status': 'failed', 'message': '%s is not one this page can install' % name}
            if not suits(name):
                return {'status': 'failed', 'message': '%s does not belong on this appliance' % name}
            # its latest release read again now, its signature verified (SIGNED RELEASES above): the
            # check's stored answer chooses nothing
            try:
                scripts[name] = fetch_installer(name, script)
            except Unsigned as reason:
                return {'status': 'failed', 'message': '%s: %s; nothing was installed' % (name, reason)}
            except urllib.error.HTTPError as error:
                return {'status': 'failed', 'message': "%s: GitHub refused its latest release's files (%s); "
                        'nothing was installed' % (name, error.code)}
            except READ_FAILED as error:
                return {'status': 'failed', 'message': '%s: its installer could not be fetched: %s' % (name, error)}
            continue
        if not os.path.isfile(script):
            return {'status': 'failed', 'message': '%s is not installed here' % name}
        if running(script):
            return {'status': 'busy', 'message': 'an update of %s is running' % name}
    if not names:
        return {'status': 'current', 'message': 'the last check offered no update'}
    now = int(time.time())
    jobs = prog.get('jobs') or {}
    for name in names:
        jobs[name] = {'status': 'queued', 'queued_at': now, 'step': 'queued', 'percent': 0}
    write_json(PROGRESS, {'queue': names, 'started_at': now, 'jobs': jobs, 'scripts': scripts,
                          'history': prog.get('history') or {}})
    spawn(names)
    return {'status': 'started', 'message': '%s %s' % ('installing' if install else 'updating', ', '.join(names))}


def spawn(names):
    """The runner for names, detached by daemon(8) from what started it: configd and the web GUI,
    which the installers restart, or the project's update, which may end before the runner does."""
    subprocess.Popen(['/usr/sbin/daemon', '-f', '/usr/local/bin/python3', os.path.abspath(__file__), 'run'] + names,
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def wait_for(names, since):
    """sync's wait for the runner it started at since: [] once the runner is through names - or gone,
    or never started, which the jobs it left say - and the names from the one that has run SYNC_WAIT
    seconds on, which the runner goes on with, when one has."""
    asked = time.time()
    while True:
        prog = read_json(PROGRESS)
        if (prog.get('finished_at') or 0) >= since:
            return []
        if runner_alive(prog):
            jobs = prog.get('jobs') or {}
            for i, name in enumerate(names):
                job = jobs.get(name) or {}
                if job.get('status') == 'running' and time.time() - (job.get('started_at') or time.time()) >= SYNC_WAIT:
                    return names[i:]
        elif prog.get('runner') or time.time() - asked >= RUNNER_START:
            return []
        time.sleep(WAIT_STEP)


def update_all():
    offered = [row['name'] for row in listing()['plugins'] if row['take']]
    return start(offered)


def sync():
    """The plugins with the project's own update (install/install.sh calls it): the updates the check
    offers for the installed plugins, then the installs of every plugin never seen here that suits
    this appliance - one at a time, by the runner the Plugins tab starts, and waited for, so that the
    update ends after them and its reboot is not offered while one is still being installed; but for
    SYNC_WAIT at most a plugin, never stopping one (THE UPDATE WAITS FOR A PLUGIN A WHILE). Returns
    what became of each, a line apiece, for the update's log."""
    try:
        with open(AUTO, encoding='utf-8') as handle:
            off = handle.read().strip() == 'off'
    except OSError:
        off = False
    if off:
        return ['not taken along: %s says off - the Plugins tab of XGS: Updates takes them by hand' % AUTO]
    if runner_alive(read_json(PROGRESS)):
        return ['the Plugins tab is updating them already; it is left to finish']
    seen = remember_installed()
    rows = check().get('plugins') or []
    updates = [row['name'] for row in rows if row.get('take')]
    installs, scripts, lines = [], {}, []
    for row in rows:
        name = row['name']
        if not row.get('installable') or name in seen or not suits(name):
            continue
        script = next(s for n, s, _ in PLUGINS if n == name)
        try:
            scripts[name] = fetch_installer(name, script)
            installs.append(name)
        except Unsigned as reason:
            lines.append('%s: not installed - %s' % (name, reason))
        except READ_FAILED as error:
            lines.append('%s: not installed - its installer could not be fetched: %s' % (name, error))
    names = updates + installs
    if not names:
        return lines + ['every installed plugin is at its latest release, and none is missing']
    now = int(time.time())
    prog = read_json(PROGRESS)
    jobs = prog.get('jobs') or {}
    for name in names:
        jobs[name] = {'status': 'queued', 'queued_at': now, 'step': 'queued', 'percent': 0}
    write_json(PROGRESS, {'queue': names, 'started_at': now, 'jobs': jobs, 'scripts': scripts,
                          'history': prog.get('history') or {}})
    spawn(names)
    left = wait_for(names, now)
    jobs = read_json(PROGRESS).get('jobs') or {}
    for name in names:
        job = jobs.get(name) or {}
        verb = 'installed' if name in installs else 'updated'
        if name in left[:1]:
            lines.append('%s: still being %s after %d minutes, and not stopped - it goes on after this '
                         'update, and the Plugins tab of XGS: Updates says when it is done' %
                         (name, verb, round(SYNC_WAIT / 60)))
        elif name in left:
            lines.append('%s: queued after %s, and %s after it' % (name, left[0], verb))
        elif job.get('status') == 'done':
            lines.append('%s: %s %s' % (name, verb, job.get('tag') or ''))
        elif job.get('status') == 'current':
            lines.append('%s: at its latest release already' % name)
        elif job.get('status') in ('queued', 'running'):
            lines.append('%s: not %s - the runner of the updates %s' % (
                name, verb, 'stopped before it ended' if job.get('status') == 'running' else
                'did not come to it'))
        else:
            lines.append('%s: not %s - %s' % (name, verb, job.get('message') or 'no reason given'))
    remember_installed()
    return lines


def lines_of(path):
    try:
        with open(path, encoding='utf-8', errors='replace') as handle:
            text = handle.read().splitlines()
    except OSError:
        return 0, ''
    tail = next((line.strip() for line in reversed(text) if line.strip()), '')
    return len(text), tail[:160]


def watch(name, started, hist):
    """What the self-update of name, started at started, has left so far: (step, fraction, detail, bytes, lines)."""
    log = os.path.join(SHARE, name, 'update.log')
    try:
        log_new = os.path.getmtime(log) >= started - 1
    except OSError:
        log_new = False
    if log_new:
        count, tail = lines_of(log)
        # the first two lines are the update's own header; the rest is the installer's
        lines = max(0, count - 2)
        want = hist.get('lines') or 0
        return 'install', (min(1.0, lines / want) if want else None), tail, None, lines
    dirs = [d for d in glob.glob(os.path.join(WORK, name + '-update.*')) if os.path.isdir(d)]
    dirs = [d for d in dirs if os.path.getmtime(d) >= started - 1]
    if dirs:
        work = max(dirs, key=os.path.getmtime)
        if os.path.isdir(os.path.join(work, 'tree')):
            return 'unpack', None, '', None, None
        try:
            size = os.path.getsize(os.path.join(work, 'release.tar.gz'))
        except OSError:
            size = 0
        want = hist.get('bytes') or 0
        return 'download', (min(1.0, size / want) if want else None), size, size, None
    return 'check', None, '', None, None


# The step a failed update stopped at, as its self-update's message names it: the runner sees the files
# an update leaves twice a second, and a step over in less - a small archive downloaded, checked against
# its signature and removed - is not seen. Until 2026-11-07 the bar of an update refused for an archive not
# the one signed said it stopped at Check for updates (issue 1122, the review's U20).
STOPPED_AT = (
    ('is not the one signed', 'download'),
    ('could not be installed', 'download'),
    ('has no install', 'unpack'),
    ('failed with status', 'install'),
)


def stopped_at(message, seen):
    """The step the message says the update stopped at, or the one seen if that is later; never one
    before what was seen."""
    names = [s for s, _ in STEPS]
    said = next((step for words, step in STOPPED_AT if words in (message or '')), None)
    if said is None or seen not in names:
        return seen if seen in names else (said or seen)
    return said if names.index(said) > names.index(seen) else seen


def percent(step, fraction, since, hist):
    """Where on the bar: the step's start, moved toward the next one's by fraction or by time."""
    names = [s for s, _ in STEPS]
    lo = STEP_AT[step]
    hi = STEP_AT[names[names.index(step) + 1]] if step != 'done' else 100
    if fraction is None:
        took = (hist.get('seconds') or {}).get(step) or GUESS.get(step, 5)
        # never quite reaches the next step on time alone
        fraction = 1 - 0.5 ** (since / max(1.0, took))
        fraction = min(fraction, 0.95)
    return int(lo + (hi - lo) * fraction)


def run(names):
    """The runner: each update in turn, watched twice a second, its progress written to PROGRESS."""
    prog = read_json(PROGRESS)
    prog['runner'] = os.getpid()
    prog['queue'] = names
    prog.setdefault('jobs', {})
    history = prog.setdefault('history', {})
    write_json(PROGRESS, prog)
    for name in names:
        # an install's self-update is the one start() fetched; an update's, the installed one
        script = (prog.get('scripts') or {}).get(name) or next((s for n, s, _ in PLUGINS if n == name), None)
        started = time.time()
        job = {'status': 'running', 'queued_at': prog['jobs'].get(name, {}).get('queued_at'),
               'started_at': int(started), 'step': 'check', 'percent': STEP_AT['check'], 'detail': ''}
        prog['jobs'][name] = job
        write_json(PROGRESS, prog)
        hist = history.get(name) or {}
        seen_bytes, seen_lines, step_since, last_step, took = 0, 0, started, 'check', {}
        # the site's copy answers the self-update only when GitHub's git endpoint confirms its commit
        env = dict(os.environ)
        confirmed = confirmed_copy(name)
        if confirmed:
            env['SELF_UPDATE_API'] = MIRROR
        job['asked'] = 'npuforge.org' if confirmed else 'GitHub'
        try:
            child = subprocess.Popen(['/usr/local/bin/python3', script], stdin=subprocess.DEVNULL,
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
        except OSError as failure:
            child = None
            job.update({'status': 'failed', 'message': str(failure)})
        while child is not None and child.poll() is None:
            step, fraction, detail, size, lines = watch(name, started, hist)
            now = time.time()
            if step != last_step:
                took[last_step] = round(now - step_since, 1)
                step_since, last_step = now, step
            seen_bytes = max(seen_bytes, size or 0)
            seen_lines = max(seen_lines, lines or 0)
            job.update({'step': step, 'detail': detail,
                        'percent': max(job.get('percent', 0), percent(step, fraction, now - step_since, hist))})
            write_json(PROGRESS, prog)
            time.sleep(0.5)
        took[last_step] = round(time.time() - step_since, 1)
        result = read_json(os.path.join(SHARE, name, 'update.json'))
        if child is not None:
            if (result.get('at') or 0) >= int(started) - 1:
                status = {'updated': 'done', 'current': 'current'}.get(result.get('status'), 'failed')
                job.update({'status': status, 'message': result.get('message') or '',
                            'tag': result.get('tag') or ''})
                if status == 'failed':
                    job['step'] = stopped_at(job['message'], job.get('step'))
                    job['percent'] = max(job.get('percent', 0), STEP_AT.get(job['step'], 0))
                if confirmed and status == 'done' and result.get('sha') != confirmed:
                    job['message'] += ('; it installed commit %s, and GitHub named %s for that release'
                                       % ((result.get('sha') or '?')[:12], confirmed[:12]))
            else:
                job.update({'status': 'failed', 'message': 'the update ended without saying how it went'})
        job.update({'finished_at': int(time.time()),
                    'step': 'done' if job['status'] in ('done', 'current') else job.get('step'),
                    'percent': 100 if job['status'] in ('done', 'current') else job.get('percent', 0)})
        if job['status'] == 'done':
            # the next update of this plugin fills its bar by what this one took
            history[name] = {'bytes': seen_bytes or hist.get('bytes'), 'lines': seen_lines or hist.get('lines'),
                             'seconds': took}
        write_json(PROGRESS, prog)
    prog['finished_at'] = int(time.time())
    prog['runner'] = None
    write_json(PROGRESS, prog)
    return 0


def main(argv):
    command = argv[1] if len(argv) > 1 else 'list'
    if command == 'list':
        print(json.dumps(listing()))
    elif command == 'check':
        print(json.dumps(check()))
    elif command == 'check-start':
        print(json.dumps(check_start()))
    elif command == 'update' and len(argv) > 2:
        print(json.dumps(start([argv[2]])))
    elif command == 'update-all':
        print(json.dumps(update_all()))
    elif command == 'install' and len(argv) > 2:
        print(json.dumps(start([argv[2]], install=True)))
    elif command == 'run' and len(argv) > 2:
        return run(argv[2:])
    elif command == 'sync':
        for line in sync():
            print(line)
    else:
        print(json.dumps({'status': 'failed',
                          'message': 'usage: plugins.py list|check|check-start|update <name>|update-all|install <name>|sync'}))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
