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
"""
import http.client
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error

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

failures = 0


def check(what, got, want):
    global failures
    if got != want:
        failures += 1
        print(f'FAIL {what}: got {got!r}, want {want!r}')
    else:
        print(f'ok   {what}')


RELEASE = {'tag_name': 'v0.1.0-alpha.5.3', 'name': 'v0.1.0-alpha.5.3', 'draft': False,
           'published_at': '2026-10-21T18:00:00Z', 'body': '- a change', 'html_url': ''}
REL_SHA = 'a' * 40
BUILD_SHA = 'b' * 40


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


def unreachable(url, direct=False):
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

# Ahead and behind as they were.
up.get = github('ahead')
check('a release ahead: offered', up.run_check().get('update_available'), True)
up.get = github('behind')
check('a release behind: not offered', up.run_check().get('update_available'), False)

# apply() after a check that failed: an earlier answer offered an update, the check made now fails,
# and nothing is downloaded.
up.get = github('ahead')
up.installed = build('2026-10-19T10:00:00+00:00')
check('the earlier check offered it', up.run_check().get('update_available'), True)
up.get = unreachable


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
# 'running' and its reason in /dev/null; it is the update's failure now.
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
check('... and the lock let go', up.take_apply_lock() is not None, True)

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
print('all passed' if not failures else f'{failures} failed')
sys.exit(1 if failures else 0)
