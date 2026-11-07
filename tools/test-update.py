#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""What scripts/xgs/update.py offers and what apply() does with it, without GitHub (issue 677).

    python3 tools/test-update.py

On 2026-10-22 a check that could not reach GitHub was followed by an update that installed the
target of an earlier check, and that target - a release - was older than the build it replaced. So:
apply() checks again itself and stops with the check's error; and a build that has diverged from the
newest release is not taken back to it when it is dated after it. GitHub's answers are stood in for
here, and the state directory is a temporary one.

And, since 2026-11-06 (the review's U11, U14 and U15): an update whose build changed one module
while another, older one sat beside it unchanged asks for its reboot; a download is tried again,
three times at most, and nothing a failed update made is left behind, nor anything it did not make
taken away; and the releases are read past the first twenty when the installed one is further back.
The update runs end to end there: codeload stood in for, a tarball made here, and the install.sh in
it a stand-in.
"""
import datetime
import hashlib
import http.client
import importlib.util
import io
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
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    'update', os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'update.py'))
up = importlib.util.module_from_spec(spec)
spec.loader.exec_module(up)

STATE = tempfile.mkdtemp(prefix='test-update-')
up.STATE = STATE
up.CHECK = os.path.join(STATE, 'check.json')
up.APPLY = os.path.join(STATE, 'apply.json')
up.LOG = os.path.join(STATE, 'apply.log')
up.BRANCH = ''
up.out = lambda obj: obj          # apply() and check() hand back what they would print
RUN_CHECK = up.run_check          # stood in for below, and taken back for the releases' pages
URLOPEN = urllib.request.urlopen  # likewise, and taken back to read a release's files through file://

failures = 0


def check(what, got, want):
    global failures
    if got != want:
        failures += 1
        print(f'FAIL {what}: got {got!r}, want {want!r}')
    else:
        print(f'ok   {what}')


# Signed releases (since 2026-11-07): a key made here for these tests alone - its public half the one
# release key the update trusts - and the release's files stood in for: what publish() puts out is
# what release_bytes() reads, a 404 for anything not put out.
KEYS = tempfile.mkdtemp(prefix='test-update-keys-')
TRUSTED = os.path.join(KEYS, 'trusted')
os.makedirs(TRUSTED)
up.RELEASE_KEYS = TRUSTED
up.OPENSSL = shutil.which('openssl') or '/usr/bin/openssl'


def new_key(name, where):
    """An Ed25519 key: its private half in KEYS, its public half in where (as name.pub); its path."""
    private = os.path.join(KEYS, name + '.key')
    subprocess.run([up.OPENSSL, 'genpkey', '-algorithm', 'ed25519', '-out', private], check=True,
                   capture_output=True)
    subprocess.run([up.OPENSSL, 'pkey', '-in', private, '-pubout', '-out', os.path.join(where, name + '.pub')],
                   check=True, capture_output=True)
    return private


OWNER_KEY = new_key('test-release-key', TRUSTED)
OTHER_KEY = new_key('other-key', KEYS)        # a key the appliance does not hold


def sign(data, key=OWNER_KEY):
    with tempfile.NamedTemporaryFile(dir=KEYS, delete=False) as f:
        f.write(data)
    try:
        return subprocess.run([up.OPENSSL, 'pkeyutl', '-sign', '-inkey', key, '-rawin', '-in', f.name],
                              check=True, capture_output=True).stdout
    finally:
        os.unlink(f.name)


PUBLISHED = {}


def manifest(tag, commit, date, archive=b'an archive'):
    return {'format': 1, 'repository': up.REPO, 'tag': tag, 'commit': commit, 'date': date,
            'archive': 'os-xgs-npu-%s.tar.gz' % tag, 'sha256': hashlib.sha256(archive).hexdigest(),
            'size': len(archive)}


def publish(fields, key=OWNER_KEY, newest=True, archive=None, change=None):
    """A release put out: its manifest, signed with key; change, when given, applied to the manifest's
    bytes after they were signed. Read as the newest release too, unless said otherwise."""
    data = (json.dumps(fields, sort_keys=True, indent=1) + '\n').encode()
    signature = sign(data, key)
    if change:
        data = change(data)
    files = {up.MANIFEST: data, up.MANIFEST + '.sig': signature}
    if archive is not None:
        files[fields['archive']] = archive
    PUBLISHED[fields['tag']] = files
    if newest:
        PUBLISHED[''] = files


def release_bytes(tag, name, limit):
    files = PUBLISHED.get(tag) or {}
    if name not in files:
        raise urllib.error.HTTPError(up.release_url(tag, name), 404, 'Not Found', {}, None)
    if len(files[name]) > limit:
        raise up.Unsigned('%s is larger than a release file of its kind can be' % name)
    return files[name]


REAL_RELEASE_BYTES = up.release_bytes
up.release_bytes = release_bytes

RELEASE = {'tag_name': 'v0.1.0-alpha.5.3', 'name': 'v0.1.0-alpha.5.3', 'draft': False,
           'published_at': '2026-10-21T18:00:00Z', 'body': '- a change', 'html_url': ''}
REL_SHA = 'a' * 40
BUILD_SHA = 'b' * 40
publish(manifest('v0.1.0-alpha.5.3', REL_SHA, '2026-10-21T18:00:00+00:00'))


def github(compare_status):
    """GitHub's three answers to a check: the releases, the newest one's commit, the comparison."""
    def get(url, direct=False):
        if '/releases' in url:
            return [RELEASE]
        if '/compare/' in url:
            return {'status': compare_status, 'ahead_by': 3, 'behind_by': 5}
        if '/commits/' in url:
            return {'sha': REL_SHA}
        raise AssertionError('unexpected request ' + url)
    return get


def unreachable(*args, **kwargs):
    """GitHub's API, or a release's files, out of reach."""
    raise urllib.error.URLError('Name could not be resolved')


def build(date):
    return lambda: {'commit': BUILD_SHA, 'date': date, 'source': 'update', 'release': ''}


# The dates.
check('a git date read', up.when('2026-10-22T01:00:00+00:00') > 0, True)
check('a GitHub date read', up.when('2026-10-21T18:00:00Z'), up.when('2026-10-21T18:00:00+00:00'))
check('no date', up.when(''), 0)
check('a date that is not one', up.when('yesterday'), 0)
check('unknown either way is not newer', up.newer_than('', '2026-10-21T18:00:00Z'), False)

# A diverged build dated after the release: not offered, and said to be newer (issue 677).
up.get = github('diverged')
up.installed = build('2026-10-22T01:00:00+00:00')
r = up.run_check()
check('diverged, built after the release: not offered', r.get('update_available'), False)
check('diverged, built after the release: said to be newer', r.get('newer_than_target'), True)

# A diverged build dated before it - a test build left behind: offered, as since 2026-10-19.
up.installed = build('2026-10-19T10:00:00+00:00')
r = up.run_check()
check('diverged, built before the release: offered', r.get('update_available'), True)
check('diverged, built before the release: not said to be newer', r.get('newer_than_target'), None)

# Ahead as it was; behind - what is installed is after the release - judged by the dates signed. The
# API's word decides nothing since 2026-11-07: a copy that calls an older build ahead of the release
# cannot keep the release from it.
up.get = github('ahead')
check('a release ahead: offered', up.run_check().get('update_available'), True)
up.get = github('behind')
up.installed = build('2026-10-22T01:00:00+00:00')
r = up.run_check()
check('a release behind, built after it: not offered, said to be newer',
      (r.get('update_available'), r.get('newer_than_target')), (False, True))
up.installed = build('2026-10-19T10:00:00+00:00')
check('the API says behind of a build dated before the release: offered all the same',
      up.run_check().get('update_available'), True)

# apply() after a check that failed: an earlier answer offered an update, the check made now fails,
# and nothing is downloaded.
up.get = github('ahead')
up.installed = build('2026-10-19T10:00:00+00:00')
check('the earlier check offered it', up.run_check().get('update_available'), True)
up.get = unreachable
up.release_bytes = unreachable


def no_download(*args, **kwargs):
    raise AssertionError('apply() downloaded after a failed check')


up.urllib.request.urlopen = no_download
up.tag_commit = no_download
r = up.apply()
check('apply() after a failed check: an error', r.get('status'), 'error')
check('apply() after a failed check: the check\'s own reason in it',
      'Name could not be resolved' in r.get('message', ''), True)
check('apply() after a failed check: no update started', os.path.exists(up.APPLY), False)

# apply() when the check made now finds nothing newer.
up.get = github('behind')
up.release_bytes = release_bytes
up.installed = build('2026-10-22T01:00:00+00:00')
r = up.apply()
check('apply() with nothing newer: an error', r.get('status'), 'error')
check('apply() with nothing newer: no update started', os.path.exists(up.APPLY), False)

# #661: an update the appliance restarted under left its record 'running' for good, and the page kept
# Check and Update off. A 'running' record from before this boot, whose lock nobody holds, is an update
# that stopped; one of this boot whose process is there still runs.
now = int(time.time())
up.boottime = lambda: now - 100
up.write_json(up.APPLY, {'status': 'running', 'pid': 99999999, 'sha': 'c' * 40, 'started_at': now - 86400})
st = up.settled(up.read_json(up.APPLY, {}))
check('a running record from before this boot: failed', st.get('status'), 'failed')
check('... said to have stopped before it finished', 'stopped before it finished' in st.get('message', ''), True)
check('... and written so', up.read_json(up.APPLY, {}).get('status'), 'failed')
up.write_json(up.APPLY, {'status': 'running', 'pid': os.getpid(), 'sha': 'c' * 40, 'started_at': now - 10})
check('a running record of this boot whose process is there: still running',
      up.settled(up.read_json(up.APPLY, {})).get('status'), 'running')

if up.fcntl is not None:
    held = up.take_apply_lock()
    check('apply() while another update holds the lock: refused',
          up.apply().get('message'), 'an update is already running')
    up.let_go(held)

# U17: install.sh inherited the lock under whatever number it had, and devd and cron, which it
# restarts, held it after the update - the next one was refused as already running. The lock is held
# on descriptor 9, and install.sh starts every daemon with 9 closed.
if up.fcntl is not None:
    nine_free = not up.fd_open(up.LOCK_FD)
    held = up.take_apply_lock()
    check('the lock held on descriptor 9 where 9 is free', held == up.LOCK_FD, nine_free)
    check('... still held there: another take refused', up.take_apply_lock(), None)
    up.let_go(held)
    again = up.take_apply_lock()
    check('... and free once let go', again is not None, True)
    up.let_go(again)
STARTS = re.compile(r'service \S+ restart|daemon -f|(console_log|feeder)\.py" start|plugins\.py" sync|'
                    r'system_cron_configure|npu-(clock|log)\.sh"|mgmt-link\.sh" assign')
with open(os.path.join(HERE, '..', 'install', 'install.sh'), encoding='utf-8') as handle:
    starts = [line.strip() for line in handle if not line.lstrip().startswith('#') and STARTS.search(line)]
check('install.sh: the daemons it starts found (11 on 2026-11-06)', len(starts) >= 11, True)
check('install.sh: each started with descriptor 9 closed', [s for s in starts if '9>&-' not in s], [])

# An error none of the steps expected - a download cut short - ended the update with its record still
# 'running' and its reason in /dev/null; it is the update's failure now. Since 2026-11-06 the download
# takes it for its own failure and tries again; the pauses are stood in for, here and below.
slept = []
real_sleep = up.time.sleep
up.time.sleep = slept.append
os.unlink(up.APPLY)
up.run_check = lambda: {'status': 'ok', 'update_available': True,
                        'target': {'sha': 'c' * 40, 'date': '2026-11-06T00:00:00Z', 'tag': ''}}


def cut_short(*args, **kwargs):
    raise http.client.IncompleteRead(b'partial', 1000)


up.urllib.request.urlopen = cut_short
r = up.apply()
check('a download cut short: an error', r.get('status'), 'error')
record = up.read_json(up.APPLY, {})
check('... recorded as failed', record.get('status'), 'failed')
check('... with its reason', 'IncompleteRead' in record.get('message', ''), True)
held = up.take_apply_lock()
check('... and the lock let go', held is not None, True)
up.let_go(held)

# #661: the progress bar from this update's own traces only. The driver's build has an account every
# run of the installer writes and a directory every run of the kernel follower compiles in; an XGS
# 126's update that had stopped during the build showed 95%, "Building the driver", taken from what
# later runs had left there. install.sh starts the account with a line naming its run and prints that
# line in the update's log as well; the follower says "compiling" once its copy is in place.
ACCOUNT = '/var/db/os-xgs-npu/install-build.log'
check('the account is the one install.sh writes', up.BUILD_LOG, ACCOUNT)
up.BUILD_LOG = os.path.join(STATE, 'install-build.log')
up.BUILDROOT = os.path.join(STATE, 'build')


def run_line(pid, at):
    return "the driver's build is logged in %s (installer %d, %s)" % (ACCOUNT, pid, at)


with open(os.path.join(HERE, '..', 'install', 'install.sh'), encoding='utf-8') as handle:
    installer = handle.read().splitlines()
made = [line for line in installer if line.startswith('BUILD_RUN=')]
check('install.sh names its build\'s run once', len(made), 1)
if made and os.name == 'posix' and shutil.which('sh'):
    said = subprocess.run(['sh', '-c', made[0] + '; printf "%s\\n" "${BUILD_RUN}"'], capture_output=True,
                          text=True, errors='replace', timeout=10).stdout.strip()
    check('... in the words update.py looks for',
          re.fullmatch(re.escape(getattr(up, 'BUILD_RUN', '?') + ACCOUNT) +
                       r' \(installer \d+, \d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ\)', said) is not None, True)
afresh = [line for line in installer if re.search(r'[^>]> /var/db/os-xgs-npu/install-build\.log', line)]
check('both families\' builds begin the account afresh', len(afresh), 2)
check('... with the run\'s line first', all('{ echo "${BUILD_RUN}";' in line for line in afresh), True)
with open(os.path.join(HERE, '..', 'install', 'kernel-follow.sh'), encoding='utf-8') as handle:
    follower = handle.read().splitlines()


def where(start):
    return next((n for n, line in enumerate(follower) if line.startswith(start)), -1)


copied, compiling, made_it = (where('/bin/cp "${SRCTREE}"'), where('echo "compiling '),
                              where('if ! ( cd "${BUILDROOT}" && /usr/bin/make'))
check('the follower says "compiling" after its copy, before the build', 0 < copied < compiling < made_it, True)
if compiling >= 0 and os.name == 'posix' and shutil.which('sh'):
    tree = os.path.join(STATE, 'tree')
    os.makedirs(tree)
    for name in ('a.c', 'b.c', 'c.c', 'a.h', 'Makefile'):
        open(os.path.join(tree, name), 'w').close()
    said = subprocess.run(['sh', '-c', follower[compiling]], env=dict(os.environ, BUILDROOT=tree),
                          capture_output=True, text=True, errors='replace', timeout=10).stdout.strip()
    check('... with the number of its sources first', said, 'compiling 3 sources in ' + tree)

THIS_RUN = run_line(4242, '2026-11-06T10:00:00Z')
TO_DRIVER = ['v0.1.0-alpha.15 is commit cccccccccccc on GitHub',
             'downloading AbdelmonemAwad/os-xgs-npu v0.1.0-alpha.15, commit cccccccccccc', 'unpacking',
             'running its install/install.sh - this builds the driver and takes a few minutes', '',
             '== board 201, ARMADA: installing the ARMADA pieces ==', '== OPNsense integration ==',
             '== GUI translations ==', '== problem reports ==', '== appliance settings ==', '== boot hooks ==',
             '== driver sources ==', '== path cost timer ==', '== kernel module ==', '   ' + THIS_RUN]
KERNEL = 'FreeBSD 15.1-RELEASE-p6 stable/26.7-n284176-c962f76493b6 SMP'
FOLLOWED = ['kernel to follow: ' + KERNEL, '  read from     : /boot/kernel/kernel', 'kernel running : ' + KERNEL,
            'module         : /boot/modules/npuep.ko', 'module stamp   : ' + KERNEL,
            'FORCE=1 - rebuilding for the kernel the module already matches',
            'building against /usr/src-26.7-c962f76493b6/sys']
COMPILING = 'compiling 10 sources in /var/db/os-xgs-npu/build'
BUILT = ['built 912384 bytes from 10 sources, machine headers from /usr/src-26.7-c962f76493b6/sys/amd64/include',
         'installed /boot/modules/npuep.ko, stamped for the kernel the next boot will use',
         'the front ports will come up on that boot without anything else being done']
# what earlier installs left: one of this version, and one of an installer before 2026-11-06, which
# began the account with no run line and whose follower said no "compiling"
OLDER = [run_line(1111, '2026-11-01T08:00:00Z')] + FOLLOWED + [COMPILING] + BUILT
OLDER_INSTALLER = FOLLOWED + BUILT


def write(path, lines):
    with open(path, 'w', encoding='utf-8') as handle:
        handle.write(''.join(line + '\n' for line in lines))


def build_dir(sources, objects):
    """The follower's build directory: SOURCES files copied in, OBJECTS of them compiled."""
    shutil.rmtree(up.BUILDROOT, ignore_errors=True)
    os.makedirs(up.BUILDROOT)
    for n in range(sources):
        for ext in ('.c', '.o') if n < objects else ('.c',):
            open(os.path.join(up.BUILDROOT, 'f%d%s' % (n, ext)), 'w').close()


def bar(state):
    p = up.progress(state) or {}
    return p.get('step'), p.get('percent'), p.get('detail')


running = {'status': 'running', 'pid': os.getpid(), 'sha': 'c' * 40, 'started_at': int(time.time())}
stopped = {'status': 'failed', 'sha': 'c' * 40, 'started_at': running['started_at'],
           'finished_at': running['started_at'] + 600, 'message': 'the update stopped before it finished'}

# A leftover account from an older run, the update at its driver.
write(up.LOG, TO_DRIVER)
build_dir(10, 10)
write(up.BUILD_LOG, OLDER)
check('an older run\'s account: not its 95%, no number', bar(running), ('sources', None, ''))
write(up.BUILD_LOG, OLDER_INSTALLER)
check('an older installer\'s account, no run line: not its 95% either', bar(running), ('sources', None, ''))
check('#661: a stopped update, the account a later install\'s: no number', bar(stopped), ('sources', None, ''))

# A run of the follower beside the update: the cron entry's, after a kernel update, compiling while
# the update's own follower waits for the lock; or an install started by hand, writing the account.
write(up.BUILD_LOG, [THIS_RUN])
build_dir(10, 6)
check('the update\'s follower waiting for its lock, another run compiling: the sources still',
      bar(running), ('sources', 35, ''))
write(up.BUILD_LOG, [THIS_RUN] + FOLLOWED)
build_dir(10, 10)
check('... then building, the directory still the other run\'s: not its 10/10', bar(running), ('build', 40, ''))
write(up.BUILD_LOG, [THIS_RUN] + FOLLOWED + [COMPILING])
build_dir(10, 0)
check('... its own copy in place: its own 0/10', bar(running), ('build', 40, '0/10'))
write(up.BUILD_LOG, [run_line(5555, '2026-11-06T10:03:00Z')] + FOLLOWED + [COMPILING])
build_dir(10, 3)
check('an install started by hand writing the account beside the update: not its 3/10',
      bar(running), ('sources', None, ''))
write(up.BUILD_LOG, [THIS_RUN] + FOLLOWED + [COMPILING])
build_dir(10, 7)
check('the update stopped during its build, another run compiling there since: no number',
      bar(stopped), ('build', None, ''))
write(up.BUILD_LOG, [THIS_RUN] + FOLLOWED + [COMPILING] + BUILT)
check('the update stopped after its own build was installed: that 95% is its own', bar(stopped), ('build', 95, ''))

# A normal update, poll by poll, the last install's account and build still there when it starts.
ACCOUNT_BUILT = [THIS_RUN] + FOLLOWED + [COMPILING] + BUILT
polls = [
    ('the download', TO_DRIVER[:2], OLDER, (10, 10), ('download', 2, '')),
    ('the unpacking', TO_DRIVER[:3], OLDER, (10, 10), ('unpack', 8, '')),
    ('the installer started', TO_DRIVER[:5], OLDER, (10, 10), ('install', 12, '')),
    ('its sections', TO_DRIVER[:13], OLDER, (10, 10), ('install', 34, '')),
    ('the driver\'s section, its run line not said yet', TO_DRIVER[:14], OLDER, (10, 10), ('sources', None, '')),
    ('the run line said, the account not begun yet', TO_DRIVER, OLDER, (10, 10), ('sources', None, '')),
    ('the account begun, the follower waiting for its lock', TO_DRIVER, [THIS_RUN], (10, 10), ('sources', 35, '')),
    ('the follower reading the kernels', TO_DRIVER, [THIS_RUN] + FOLLOWED[:-1], (10, 10), ('sources', 35, '')),
    ('building, the last build still in the directory', TO_DRIVER, [THIS_RUN] + FOLLOWED, (10, 10),
     ('build', 40, '')),
    ('its copy in place', TO_DRIVER, [THIS_RUN] + FOLLOWED + [COMPILING], (10, 0), ('build', 40, '0/10')),
    ('half compiled', TO_DRIVER, [THIS_RUN] + FOLLOWED + [COMPILING], (10, 5), ('build', 65, '5/10')),
    ('all compiled', TO_DRIVER, [THIS_RUN] + FOLLOWED + [COMPILING], (10, 10), ('build', 90, '10/10')),
    ('built', TO_DRIVER, ACCOUNT_BUILT[:-2], (10, 10), ('build', 92, '')),
    ('installed', TO_DRIVER, ACCOUNT_BUILT, (10, 10), ('build', 95, '')),
    ('the next section', TO_DRIVER + ['   built and installed through the kernel follower:', '     ' + BUILT[0],
                                      '     ' + BUILT[1], '', '== loader =='], ACCOUNT_BUILT, (10, 10),
     ('finish', 96, '')),
]
shown = []
for what, log_lines, account, (sources, objects), want in polls:
    write(up.LOG, log_lines)
    write(up.BUILD_LOG, account)
    build_dir(sources, objects)
    got = bar(running)
    check('a normal update: ' + what, got, want)
    shown.append(got[1])
check('a normal update: done', bar(dict(running, status='done', finished_at=running['started_at'] + 300)),
      ('finish', 100, ''))
numbers = [n for n in shown if n is not None]
check('a normal update: the bar never goes back', numbers, sorted(numbers))

shutil.rmtree(STATE, ignore_errors=True)


# ---------------------------------------------------------------- the update, end to end

SHA = 'd' * 40
WORK = tempfile.mkdtemp(prefix='test-update-work-')
NPUEP, OCTEP = os.path.join(WORK, 'npuep.ko'), os.path.join(WORK, 'octep.ko')
up.MODULES = (NPUEP, OCTEP)
up.HOOK_DIRS = (os.path.join(WORK, 'early'), os.path.join(WORK, 'start'))
up.LOADED = os.path.join(STATE, 'loaded.json')
up.SOURCE = os.path.join(WORK, 'source')
BOOT = int(time.time()) - 1000
up.boottime = lambda: BOOT
up.run_check = lambda: {'status': 'ok', 'update_available': True,
                        'target': {'sha': SHA, 'date': '2026-11-06T00:00:00Z', 'tag': ''}}
ARCHIVE = os.path.join(STATE, SHA[:12] + '.tar.gz')
TREE = os.path.join(STATE, 'src-' + SHA[:12])


def module(path, data):
    """A module as the boot found it: written before the boot, so the boot's snapshot takes it."""
    with open(path, 'w') as f:
        f.write(data)
    os.utime(path, (BOOT - 100, BOOT - 100))


def tarball(script):
    """GitHub's tarball of the commit: one top directory, and install/install.sh in it."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode='w:gz') as tar:
        data = ('#!/bin/sh\n' + script + '\n').encode()
        info = tarfile.TarInfo('os-xgs-npu-%s/install/install.sh' % SHA)
        info.size, info.mode = len(data), 0o755
        tar.addfile(info, io.BytesIO(data))
    return buf.getvalue()


class Answer(io.BytesIO):
    """codeload's answer: the archive, or the first bytes of it and then the connection cut."""

    def __init__(self, data, cut=False):
        super().__init__(data)
        self.cut = cut

    def read(self, size=-1):
        if self.cut and self.tell() > 0:
            raise http.client.IncompleteRead(b'', 1000)
        return super().read(100 if self.cut else size)


def codeload(*answers):
    """urlopen as codeload answers it, one answer a try: an exception is raised, bytes are the archive,
    ('cut', bytes) the archive cut short. The tries are counted in asked."""
    asked = []

    def urlopen(request, timeout=None):
        asked.append(request.full_url)
        answer = answers[min(len(asked), len(answers)) - 1]
        if isinstance(answer, BaseException):
            raise answer
        if isinstance(answer, tuple):
            return Answer(answer[1], cut=True)
        return Answer(answer)
    up.urllib.request.urlopen = urlopen
    return asked


def update(*answers):
    """One update with codeload's answers, its tries and pauses counted; the record it left."""
    for path in (up.APPLY, up.LOADED):
        if os.path.exists(path):
            os.unlink(path)
    del slept[:]
    asked = codeload(*answers)
    r = up.apply()
    return r, up.read_json(up.APPLY, {}), asked, up.read_text(up.LOG)


DOWN = urllib.error.URLError(OSError(51, 'Network is unreachable'))
GOOD = tarball('exit 0')

# The review's U11: an octep.ko left on an ARMADA appliance, which nothing there rebuilds, the same as
# the boot found it; npuep.ko changed by the build. The driver changed, and the reboot is asked for.
module(NPUEP, 'npuep as the boot loaded it')
module(OCTEP, 'an octep.ko from an old install, never rebuilt here')
r, record, asked, _ = update(tarball("printf 'npuep as this build made it' > '%s'" % NPUEP))
check('U11: an update with an octep.ko left unchanged beside it: done', record.get('status'), 'done')
check('U11: ... npuep.ko changed, so the driver changed', record.get('driver_changed'), True)
check('U11: ... and the reboot is asked for', record.get('reboot'), True)
module(NPUEP, 'npuep as the boot loaded it')
r, record, asked, _ = update(tarball('exit 0'))
check('U11: both the same as the boot found them: nothing waits for a reboot',
      (record.get('driver_changed'), record.get('reboot')), (False, False))
os.unlink(OCTEP)
r, record, asked, _ = update(tarball("printf 'npuep rebuilt' > '%s'" % NPUEP))
check('U11: one module, changed: the reboot is asked for, as before', record.get('reboot'), True)
module(NPUEP, 'npuep as the boot loaded it')
r, record, asked, _ = update(tarball('exit 0'))
check('U11: one module, the same: nothing waits for a reboot, as before', record.get('reboot'), False)

# The review's U14: a download that fails is tried again - three tries, 30 and 90 s apart - and goes on
# once one gets through.
r, record, asked, log = update(DOWN, ('cut', GOOD), GOOD)
check('U14: a link down, then a cut, then the archive: the update done', record.get('status'), 'done')
check('U14: ... three tries', len(asked), 3)
check('U14: ... 30 and then 90 s apart', slept, [30, 90])
check('U14: ... each said in the log',
      ('trying again in 30 s, try 2 of 3' in log, 'trying again in 90 s, try 3 of 3' in log), (True, True))
check('U14: ... and the archive gone once unpacked', os.path.exists(ARCHIVE), False)

r, record, asked, log = update(('cut', GOOD), ('cut', GOOD), ('cut', GOOD))
check('U14: cut short every time: failed after three tries',
      (record.get('status'), len(asked)), ('failed', 3))
check('U14: ... with the reason and the tries',
      ('IncompleteRead' in record.get('message', ''), record.get('message', '').endswith('(tried 3 times)')),
      (True, True))
check('U14: ... and no archive left behind, cut short', os.path.exists(ARCHIVE), False)

r, record, asked, log = update(urllib.error.HTTPError(up.TARBALL + SHA, 404, 'Not Found', {}, None))
check('U14: codeload has no such archive: not tried again', (len(asked), slept), (1, []))
check('U14: ... and said as before', record.get('message'), 'the download failed: HTTP Error 404: Not Found')

r, record, asked, log = update(GOOD[:len(GOOD) // 2])
check('U14: an archive that ends early: failed', record.get('status'), 'failed')
check('U14: ... the archive and the tree begun from it taken away',
      (os.path.exists(ARCHIVE), os.path.exists(TREE)), (False, False))

os.makedirs(up.SOURCE, exist_ok=True)
with open(os.path.join(up.SOURCE, 'kept'), 'w') as f:
    f.write('the tree of what runs')
r, record, asked, log = update(tarball('exit 1'))
check('U14: install.sh failed: failed', record.get('status'), 'failed')
check('U14: ... the tree it ran from taken away', os.path.exists(TREE), False)
check('U14: ... and the tree an image keeps as it was', up.read_text(os.path.join(up.SOURCE, 'kept')),
      'the tree of what runs')

# Nothing the update did not make: what another update left, and an archive of this commit already
# there when no try got far enough to write one.
OTHER_TREE = os.path.join(STATE, 'src-0123456789ab')
OTHER_ARCHIVE = os.path.join(STATE, '0123456789ab.tar.gz')
os.makedirs(OTHER_TREE, exist_ok=True)
for path in (OTHER_ARCHIVE, ARCHIVE):
    with open(path, 'w') as f:
        f.write('not this update\'s')
r, record, asked, log = update(DOWN, DOWN, DOWN)
check('U14: a link down every time: failed after three tries', (record.get('status'), len(asked)),
      ('failed', 3))
check('U14: ... what it did not make left as it was',
      (os.path.isdir(OTHER_TREE), up.read_text(OTHER_ARCHIVE), up.read_text(ARCHIVE)),
      (True, 'not this update\'s', 'not this update\'s'))
os.unlink(ARCHIVE)
up.time.sleep = real_sleep


# ---------------------------------------------------------------- the releases, page by page

up.run_check = RUN_CHECK


def published(n):
    return (datetime.datetime(2026, 1, 1) + datetime.timedelta(hours=n)).strftime('%Y-%m-%dT%H:%M:%SZ')


def releases(total, refused_from=0):
    """GitHub's releases, newest first, alpha.<total> down to alpha.1, each with one line of notes:
    its answer to a page of the list - and, from page refused_from, a refusal. The pages asked for
    are counted in asked, with whether the site's copy could have been asked (not direct)."""
    listed = [{'tag_name': 'v0.1.0-alpha.%d' % n, 'name': 'v0.1.0-alpha.%d' % n, 'draft': False,
               'published_at': published(n), 'body': '- change %d' % n, 'html_url': ''}
              for n in range(total, 0, -1)]
    publish(manifest('v0.1.0-alpha.%d' % total, REL_SHA, published(total)))
    asked = []

    def get(url, direct=False):
        if '/releases?' in url:
            q = urllib.parse.parse_qs(urllib.parse.urlparse(url).query)
            size, page = int(q['per_page'][0]), int(q.get('page', ['1'])[0])
            asked.append((size, page, direct))
            if refused_from and page >= refused_from:
                raise urllib.error.HTTPError(url, 403, 'rate limit exceeded', {}, None)
            return listed[(page - 1) * size:page * size]
        if '/compare/' in url:
            return {'status': 'ahead', 'ahead_by': 3, 'behind_by': 0}
        if '/commits/' in url:
            return {'sha': REL_SHA}
        raise AssertionError('unexpected request ' + url)
    up.get = get
    return asked


def installed_release(n):
    tag = 'v0.1.0-alpha.%d' % n
    up.installed = lambda: {'commit': 'c' * 40, 'date': published(n), 'source': 'update', 'release': tag}


asked = releases(150)
installed_release(145)
r = up.run_check()
check('U15: 5 releases behind: 5 since', r.get('releases_since'), 5)
check('U15: ... read from the first twenty alone, through the site', asked, [(20, 1, False)])

asked = releases(150)
installed_release(125)
r = up.run_check()
check('U15: 25 releases behind: 25 since', r.get('releases_since'), 25)
check('U15: ... each one\'s notes listed', [c['release'] for c in r.get('changes', [])],
      ['v0.1.0-alpha.%d' % n for n in range(150, 125, -1)])
check('U15: ... the first hundred asked for through the site', asked, [(20, 1, False), (100, 1, False)])

asked = releases(150)
installed_release(20)
r = up.run_check()
check('U15: 130 releases behind: 130 since', r.get('releases_since'), 130)
check('U15: ... the list at its fifty lines', len(r.get('changes', [])), 50)
check('U15: ... the second hundred asked of GitHub itself',
      asked, [(20, 1, False), (100, 1, False), (100, 2, True)])

asked = releases(150)
up.installed = lambda: {'commit': 'c' * 40, 'date': published(0), 'source': 'update', 'release': 'v0.0.9'}
r = up.run_check()
check('U15: an installed release not on the list: the newest alone, as before',
      (r.get('status'), r.get('releases_since')), ('ok', 1))
check('U15: ... read to the list\'s last page and no further',
      asked, [(20, 1, False), (100, 1, False), (100, 2, True)])

asked = releases(1000)
r = up.run_check()
check('U15: a thousand releases, the installed one not among them: five pages of a hundred at most',
      asked, [(20, 1, False)] + [(100, p, p > 1) for p in range(1, 6)])

asked = releases(150, refused_from=2)
installed_release(20)
r = up.run_check()
check('U15: a page past the first hundred refused: the check answers with what it read',
      (r.get('status'), r.get('releases_since'), r.get('target', {}).get('tag')),
      ('ok', 1, 'v0.1.0-alpha.150'))

# ---------------------------------------------------------------- signed releases (2026-11-07)
# What is installed is what the release's owner signed: the check reads the newest release's manifest
# and offers it only when its signature verifies with a key the appliance holds; the update reads the
# manifest again, and installs the archive it names only when its SHA-256 and size are the signed ones.
# GitHub's API - through npuforge.org's copy - is asked for what the page shows beside it, and decides
# nothing (the review's G3 and U5).

TAG, SIGNED_SHA, SIGNED_DATE = 'v0.1.0-alpha.200', 'e' * 40, '2026-11-07T08:00:00+00:00'
up.get = github('ahead')
up.installed = build('2026-11-01T00:00:00+00:00')


def checked(fields, **kwargs):
    PUBLISHED.clear()
    publish(fields, **kwargs)
    return up.run_check()


r = checked(manifest(TAG, SIGNED_SHA, SIGNED_DATE))
check('signed: the newest release offered, by the commit and the date it was signed with',
      (r.get('update_available'), r['target']['sha'], r['target']['date'], r.get('signed_with')),
      (True, SIGNED_SHA, SIGNED_DATE, 'test-release-key.pub'))

PUBLISHED.clear()
r = up.run_check()
check('the newest release not signed yet: not offered, and said so',
      (r.get('status'), r.get('message')), ('error', 'the newest release is not signed yet: it has no release.json'))

r = checked(manifest(TAG, SIGNED_SHA, SIGNED_DATE), key=OTHER_KEY)
check('signed with a key the appliance does not hold: not offered',
      (r.get('status'), r.get('message')),
      ('error', 'the signature of the newest release does not verify with any release key of this appliance'))

r = checked(manifest(TAG, SIGNED_SHA, SIGNED_DATE), change=lambda d: d.replace(b'e' * 40, b'f' * 40))
check('the manifest changed after it was signed - another commit named: not offered',
      (r.get('status'), 'does not verify' in r.get('message', '')), ('error', True))

r = checked(manifest(TAG, SIGNED_SHA, SIGNED_DATE), change=lambda d: d + b' ')
check('... one byte added to it: not offered', (r.get('status'), 'does not verify' in r.get('message', '')),
      ('error', True))

up.RELEASE_KEYS = KEYS + '/none'
r = checked(manifest(TAG, SIGNED_SHA, SIGNED_DATE))
check('an appliance with no release key: nothing offered, and said why',
      (r.get('status'), r.get('message', '').endswith("this appliance's release keys - it has none")), ('error', True))
up.RELEASE_KEYS = TRUSTED

for name, fields in (
        ('another repository', dict(manifest(TAG, SIGNED_SHA, SIGNED_DATE), repository='someone/os-xgs-npu')),
        ('a commit of another form', manifest(TAG, 'e' * 39, SIGNED_DATE)),
        ('an archive with a path in its name', dict(manifest(TAG, SIGNED_SHA, SIGNED_DATE), archive='../x.tar.gz')),
        ('no date', manifest(TAG, SIGNED_SHA, '')),
        ('a size of none', dict(manifest(TAG, SIGNED_SHA, SIGNED_DATE), size=0)),
        ('another format', dict(manifest(TAG, SIGNED_SHA, SIGNED_DATE), format=2))):
    r = checked(fields)
    check('signed, but %s: not offered' % name, r.get('status'), 'error')

publish(manifest(TAG, SIGNED_SHA, SIGNED_DATE), newest=False)
PUBLISHED['v0.1.0-alpha.199'] = PUBLISHED[TAG]
try:
    up.signed_manifest('v0.1.0-alpha.199')
    taken = 'taken'
except up.Unsigned as reason:
    taken = str(reason)
check('a release whose files are another release\'s: refused',
      taken, 'the manifest published with v0.1.0-alpha.199 is the one of v0.1.0-alpha.200')

up.get = unreachable
r = checked(manifest(TAG, SIGNED_SHA, SIGNED_DATE))
check('GitHub\'s API and its copy out of reach: the signed release offered all the same, with less to show',
      (r.get('status'), r.get('update_available'), r.get('changes'), r.get('compare')), ('ok', True, [], 'unknown'))

up.get = github('behind')
up.installed = build('2026-11-08T00:00:00+00:00')
r = checked(manifest(TAG, SIGNED_SHA, SIGNED_DATE))
check('a build dated after the signed release: not offered - nothing is taken back',
      (r.get('update_available'), r.get('newer_than_target')), (False, True))
up.installed = lambda: {'commit': SIGNED_SHA, 'date': SIGNED_DATE, 'source': 'update', 'release': TAG}
r = checked(manifest(TAG, SIGNED_SHA, SIGNED_DATE))
check('the signed release installed: nothing offered', (r.get('update_available'), r.get('compare')),
      (False, 'identical'))

# The real reading of a release's files, from a place a test appliance names (file://), and its limit.
up.release_bytes = REAL_RELEASE_BYTES
up.urllib.request.urlopen = URLOPEN
FILES = tempfile.mkdtemp(prefix='test-update-releases-')
data = (json.dumps(manifest(TAG, SIGNED_SHA, SIGNED_DATE), sort_keys=True) + '\n').encode()
for where in ('latest/download', 'download/' + TAG):
    os.makedirs(os.path.join(FILES, where))
    with open(os.path.join(FILES, where, up.MANIFEST), 'wb') as f:
        f.write(data)
    with open(os.path.join(FILES, where, up.MANIFEST + '.sig'), 'wb') as f:
        f.write(sign(data))
with open(os.path.join(STATE, 'releases-from'), 'w') as f:
    f.write('file://' + FILES + '\n')
up.installed = build('2026-11-01T00:00:00+00:00')
up.get = github('ahead')
r = up.run_check()
check('the files read from the place a test appliance names: the newest release signed and offered',
      (r.get('status'), r.get('message'), r.get('update_available'), r.get('target', {}).get('tag')),
      ('ok', None, True, TAG))
check('... the release by its tag too', up.signed_manifest(TAG)['commit'], SIGNED_SHA)
os.rename(os.path.join(FILES, 'latest/download', up.MANIFEST), os.path.join(FILES, 'kept.json'))
r = up.run_check()
check('a manifest missing from that place: said not signed yet, as GitHub\'s 404 is',
      (r.get('status'), r.get('message')), ('error', 'the newest release is not signed yet: it has no release.json'))
os.rename(os.path.join(FILES, 'kept.json'), os.path.join(FILES, 'latest/download', up.MANIFEST))
with open(os.path.join(FILES, 'latest/download', up.MANIFEST), 'wb') as f:
    f.write(b' ' * (up.MANIFEST_MAX + 1))
r = up.run_check()
check('a manifest larger than one can be: not read whole, not offered',
      (r.get('status'), 'larger than a release file' in r.get('message', '')), ('error', True))
os.unlink(os.path.join(STATE, 'releases-from'))
shutil.rmtree(FILES, ignore_errors=True)
up.release_bytes = release_bytes

# The update, end to end, of a signed release: the archive its manifest names, published with it.
SIGNED_ARCHIVE = os.path.join(STATE, SIGNED_SHA[:12] + '.tar.gz')
SIGNED_TREE = os.path.join(STATE, 'src-' + SIGNED_SHA[:12])
up.run_check = lambda: {'status': 'ok', 'update_available': True,
                        'target': {'sha': SIGNED_SHA, 'date': SIGNED_DATE, 'tag': TAG}}
up.tag_commit = lambda tag: SIGNED_SHA
real_sleep, up.time.sleep = up.time.sleep, lambda seconds: slept.append(seconds)
GOOD_SIGNED = tarball('exit 0')
PUBLISHED.clear()
publish(manifest(TAG, SIGNED_SHA, SIGNED_DATE, GOOD_SIGNED), archive=GOOD_SIGNED)
r, record, asked, log = update(GOOD_SIGNED)
check('a signed release updated: done', record.get('status'), 'done')
check('... downloaded as the file published with it, by the name its manifest gives',
      asked, [up.release_url(TAG, 'os-xgs-npu-%s.tar.gz' % TAG)])
check('... its log says the key and that the archive was the one signed',
      ('signed with test-release-key.pub' in log, 'the archive is the one signed: %d bytes' % len(GOOD_SIGNED) in log),
      (True, True))

r, record, asked, log = update(GOOD_SIGNED[:-1] + bytes([GOOD_SIGNED[-1] ^ 1]))
check('an archive one bit from the signed one: failed, nothing installed',
      (record.get('status'), 'is not the one signed' in record.get('message', ''), os.path.exists(SIGNED_TREE)),
      ('failed', True, False))
r, record, asked, log = update(GOOD_SIGNED + b'more')
check('an archive longer than the signed one: failed, read no further than its signed size',
      (record.get('status'), 'more than %d' % len(GOOD_SIGNED) in record.get('message', '')), ('failed', True))
check('... and no archive left behind', os.path.exists(SIGNED_ARCHIVE), False)

PUBLISHED.clear()
r, record, asked, log = update(GOOD_SIGNED)
check('the release not signed when the update reads it again: failed, nothing downloaded',
      (record.get('status'), asked), ('failed', []))

publish(dict(manifest(TAG, 'f' * 40, SIGNED_DATE, GOOD_SIGNED)), archive=GOOD_SIGNED)
r, record, asked, log = update(GOOD_SIGNED)
check('signed as another commit than the check found: failed, nothing downloaded',
      (record.get('status'), asked, 'names commit ffffffffffff' in record.get('message', '')), ('failed', [], True))

publish(manifest(TAG, SIGNED_SHA, SIGNED_DATE, GOOD_SIGNED), archive=GOOD_SIGNED)
up.installed = build('2026-11-08T00:00:00+00:00')
r, record, asked, log = update(GOOD_SIGNED)
check('an update to a release dated before what is installed: refused, nothing downloaded',
      (record.get('status'), asked, 'dated before what is installed' in record.get('message', '')),
      ('failed', [], True))
up.installed = build('2026-11-01T00:00:00+00:00')

up.tag_commit = lambda tag: 'f' * 40
r, record, asked, log = update(GOOD_SIGNED)
check('GitHub saying the tag is another commit than the one signed: failed, nothing downloaded',
      (record.get('status'), asked), ('failed', []))
up.time.sleep = real_sleep

for path in (STATE, WORK, KEYS):
    shutil.rmtree(path, ignore_errors=True)
print('all passed' if not failures else f'{failures} failed')
sys.exit(1 if failures else 0)
