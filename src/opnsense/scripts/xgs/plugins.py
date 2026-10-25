#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The project's appliance plugins and their releases - for the Plugins tab of XGS: Updates.

    plugins.py list               what is installed of each, what was last found and done, and the
                                  progress of the updates running now
    plugins.py check              ask GitHub for each installed plugin's latest release, keep it, print the list
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
plugin's latest release on GitHub, takes it only when it is ahead of what is installed, and runs the
installer the release carries - and this only calls it: `self-update.py check` to ask, and
`self-update.py` to install. Nothing of a plugin is in this repository. Until 2026-10-20 each of them
ran itself once a day from cron and installed a new release on its own; since then an update is
taken here, where the owner sees it (each plugin's own release drops its cron entry).

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
of a plugin changes; when the copy cannot answer, the check is asked again of GitHub itself.

AN UPDATE ASKS THE COPY TOO, ONCE GITHUB HAS CONFIRMED IT (since 0.1.0-alpha.8.4). Until then every
update and install asked GitHub's API itself - two or three requests a plugin - and once the
project's update took the plugins along, every update of an appliance asked that for each of them: on
2026-10-25 an XGS 136 updated several times in an hour was refused ("GitHub refused the request
(403)") and one plugin was not installed. Now, before a plugin's self-update runs, the runner asks the copy for the plugin's latest
release and its commit, and GitHub's git endpoint - the reference list `git ls-remote` reads, which
is not the API and not its hourly allowance - for the commit that release's tag names. Only when the
two agree is the self-update handed the copy; otherwise it asks GitHub's API itself, as before. What
it installed is compared with the confirmed commit afterwards, and a difference is said.
"""

import glob
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

SHARE = '/usr/local/share'
MIRROR = os.environ.get('XGS_UPDATE_MIRROR', 'https://npuforge.org/api/github')
STATE = '/var/db/os-xgs-npu/plugins-check.json'
PROGRESS = '/var/db/os-xgs-npu/plugins-update.json'
SEEN = '/var/db/os-xgs-npu/plugins-seen.json'
AUTO = '/var/db/os-xgs-npu/plugins-auto'
WORK = '/var/tmp'
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
RAW = 'https://raw.githubusercontent.com'
# INSTALLING ONE THAT IS NOT HERE (2026-10-25). A plugin of the family that is not installed is listed
# too, with its latest release, and Install installs it the way an update does: its own self-update.py,
# taken from that release's tag on GitHub into a work directory, run by the same runner - which finds
# nothing installed and installs the release with the installer it carries. Only the family's os-*
# plugins, whose script sits under /usr/local/opnsense/scripts as in their repositories' src/; the
# Arabic catalogue and the right-to-left layout come with the image. Until then a plugin added to the
# family after an appliance was installed could only be installed from a shell.
INSTALLER_DIR = os.path.join(WORK, '%s-installer')
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
        done = subprocess.run(['/bin/pgrep', '-f', script], capture_output=True, text=True, timeout=10)
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


def latest_release(name):
    """A plugin's latest release, asked of the site's copy of GitHub's API and then of GitHub's own,
    for one that is not installed and so has no self-update.py to ask."""
    for api in ([MIRROR] if MIRROR else []) + [GITHUB]:
        url = '%s/repos/%s/%s/releases/latest' % (api, OWNER, name)
        request = urllib.request.Request(url, headers={'Accept': 'application/vnd.github+json',
                                                       'User-Agent': 'os-xgs-npu'})
        try:
            with urllib.request.urlopen(request, timeout=20) as answer:
                found = json.loads(answer.read().decode('utf-8'))
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return {'status': 'none', 'message': 'no release is published yet'}
            continue
        except (urllib.error.URLError, OSError, ValueError):
            continue
        if found.get('tag_name'):
            return {'status': 'found', 'tag': found['tag_name'], 'installed': '', 'take': False}
    return {'status': 'failed', 'message': 'could not reach GitHub'}


def fetch_installer(name, script, tag):
    """The plugin's own self-update.py of release tag, into its installer directory; its path."""
    source = source_path(script)
    work = INSTALLER_DIR % name
    os.makedirs(work, exist_ok=True)
    target = os.path.join(work, 'self-update.py')
    url = '%s/%s/%s/%s/%s' % (RAW, OWNER, name, urllib.parse.quote(tag), source)
    with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'os-xgs-npu'}),
                                timeout=30) as answer:
        body = answer.read()
    if b'REPO = ' not in body:
        raise ValueError('%s does not look like a self-update script' % url)
    with open(target + '.tmp', 'wb') as handle:
        handle.write(body)
    os.replace(target + '.tmp', target)
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
    except (urllib.error.URLError, OSError, ValueError):
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

    try:
        tag = ask_copy('/repos/%s/%s/releases/latest' % (OWNER, name))['tag_name']
        sha = ask_copy('/repos/%s/%s/commits/%s' % (OWNER, name, urllib.parse.quote(tag, safe='')))['sha']
    except (urllib.error.URLError, OSError, ValueError, KeyError, TypeError):
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
        'now': int(time.time()),
    }


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
            if source_path(script) is not None:
                seen[name] = latest_release(name)
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
    write_json(STATE, {'at': int(time.time()), 'plugins': seen})
    return listing()


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
            found = latest_release(name)
            if found.get('status') != 'found':
                return {'status': 'failed', 'message': '%s: %s' % (name, found.get('message'))}
            try:
                scripts[name] = fetch_installer(name, script, found['tag'])
            except (urllib.error.URLError, OSError, ValueError) as error:
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
    subprocess.Popen(['/usr/sbin/daemon', '-f', '/usr/local/bin/python3', os.path.abspath(__file__), 'run'] + names,
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return {'status': 'started', 'message': '%s %s' % ('installing' if install else 'updating', ', '.join(names))}


def update_all():
    offered = [row['name'] for row in listing()['plugins'] if row['take']]
    return start(offered)


def sync():
    """The plugins with the project's own update (install/install.sh calls it): the updates the check
    offers for the installed plugins, then the installs of every plugin never seen here that suits
    this appliance - run here, in the foreground, one at a time, as the Plugins tab runs them, so the
    update ends after them and its reboot is not offered while one is still being installed. Returns
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
    found = read_json(STATE).get('plugins') or {}
    updates = [row['name'] for row in rows if row.get('take')]
    installs, scripts, lines = [], {}, []
    for row in rows:
        name = row['name']
        if not row.get('installable') or name in seen or not suits(name):
            continue
        script = next(s for n, s, _ in PLUGINS if n == name)
        try:
            scripts[name] = fetch_installer(name, script, (found.get(name) or {}).get('tag') or '')
            installs.append(name)
        except (urllib.error.URLError, OSError, ValueError) as error:
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
    run(names)
    jobs = read_json(PROGRESS).get('jobs') or {}
    for name in names:
        job = jobs.get(name) or {}
        verb = 'installed' if name in installs else 'updated'
        if job.get('status') == 'done':
            lines.append('%s: %s %s' % (name, verb, job.get('tag') or ''))
        elif job.get('status') == 'current':
            lines.append('%s: at its latest release already' % name)
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
                          'message': 'usage: plugins.py list|check|update <name>|update-all|install <name>|sync'}))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
