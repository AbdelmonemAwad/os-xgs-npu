#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""plugins.py's install of a plugin that is not here (its SIGNED RELEASES): a plugin's releases made
here and signed with throwaway keys - what the Plugins tab lists for one that is signed, one that is
not, one signed with another key and one whose manifest is of another repository; what is taken out of
a signed archive into the installer directory - the plugin's self-update.py and the release keys beside
it, nothing else and nowhere else - and that nothing is taken, and nothing left, when the archive is not
the one signed or lacks either. Nothing here asks GitHub: the releases are files in a temporary
directory, named by RELEASES_FROM, and OpenSSL makes and checks the signatures as on the appliance.

    python3 tools/test-plugin-install.py"""
import hashlib
import importlib.util
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    'plugins', os.path.join(os.path.dirname(HERE), 'src', 'opnsense', 'scripts', 'xgs', 'plugins.py'))
pl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pl)
OPENSSL = shutil.which('openssl')
failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


if not OPENSSL:
    print('FAIL this needs openssl')
    sys.exit(1)

TOP = tempfile.mkdtemp(prefix='test-plugin-install.')
KEYS, RELEASES, WORK = (os.path.join(TOP, d) for d in ('keys', 'releases', 'work'))
for d in (KEYS, RELEASES, WORK):
    os.makedirs(d)
pl.RELEASE_KEYS, pl.WORK, pl.OPENSSL = KEYS, WORK, OPENSSL
pl.INSTALLER_DIR = os.path.join(WORK, '%s-installer')
pl.RELEASES_FROM = os.path.join(TOP, 'releases-from')
with open(pl.RELEASES_FROM, 'w', encoding='utf-8') as f:
    f.write('file://%s/\n' % RELEASES)

NAME = 'os-portguard'
SCRIPT = '/usr/local/opnsense/scripts/portguard/self-update.py'
SOURCE = 'src/opnsense/scripts/portguard/self-update.py'
KEYDIR = 'src/opnsense/scripts/portguard/release-keys/'
TAG, COMMIT = 'v1.0.2', '2' * 40
SELF_UPDATE = "#!/usr/local/bin/python3\nNAME = 'os-portguard'\n"


def openssl(*args):
    subprocess.run([OPENSSL] + list(args), check=True, capture_output=True)


def make_key(name):
    pem = os.path.join(TOP, name + '.pem')
    openssl('genpkey', '-algorithm', 'ed25519', '-out', pem)
    openssl('pkey', '-in', pem, '-pubout', '-out', os.path.join(TOP, name + '.pub'))
    return pem


def sign(pem, data):
    path = os.path.join(TOP, 'to-sign')
    with open(path, 'wb') as f:
        f.write(data)
    openssl('pkeyutl', '-sign', '-inkey', pem, '-rawin', '-in', path, '-out', path + '.sig')
    with open(path + '.sig', 'rb') as f:
        return f.read()


def archive_of(files):
    """An archive as git archive makes one: every file under one top directory, and a name given
    with ../ left as it is."""
    buf = io.BytesIO()
    top = '%s-%s' % (NAME, TAG)
    with tarfile.open(fileobj=buf, mode='w:gz') as tar:
        for name, text in files.items():
            body = text.encode()
            entry = tarfile.TarInfo(name if name.startswith('../') else top + '/' + name)
            entry.size, entry.mode = len(body), 0o644
            tar.addfile(entry, io.BytesIO(body))
    return buf.getvalue()


def files(**more):
    with open(os.path.join(TOP, 'owner.pub'), encoding='utf-8') as f:
        pub = f.read()
    found = {SOURCE: SELF_UPDATE, KEYDIR + 'xgs-release-key-test.pub': pub, 'install/install.sh': '#!/bin/sh\n',
             'src/opnsense/scripts/portguard/portguard.py': 'x = 1\n'}
    found.update(more)
    return found


def publish(pem, archive=None, serve=None, fields=None, signature=True):
    """The plugin's latest release in the test's place: its archive, the manifest - fields put in or
    taken out (None) - and the manifest's signature with pem; serve, an archive in place of the one signed."""
    archive = archive if archive is not None else archive_of(files())
    manifest = {'format': 1, 'repository': '%s/%s' % (pl.OWNER, NAME), 'tag': TAG, 'commit': COMMIT,
                'date': '2026-11-07T10:00:00+00:00', 'archive': '%s-%s.tar.gz' % (NAME, TAG),
                'sha256': hashlib.sha256(archive).hexdigest(), 'size': len(archive)}
    for name, value in (fields or {}).items():
        if value is None:
            manifest.pop(name, None)
        else:
            manifest[name] = value
    body = (json.dumps(manifest, sort_keys=True, indent=1) + '\n').encode()
    out = {'%s-%s.tar.gz' % (NAME, TAG): serve if serve is not None else archive, 'release.json': body}
    if signature:
        out['release.json.sig'] = sign(pem, body)
    base = os.path.join(RELEASES, NAME)
    shutil.rmtree(base, ignore_errors=True)
    for place in (os.path.join(base, 'latest', 'download'), os.path.join(base, 'download', TAG)):
        os.makedirs(place)
        for name, data in out.items():
            with open(os.path.join(place, name), 'wb') as f:
                f.write(data)


def trust(*names):
    shutil.rmtree(KEYS)
    os.makedirs(KEYS)
    for name in names:
        shutil.copy(os.path.join(TOP, name + '.pub'), os.path.join(KEYS, 'xgs-release-key-%s.pub' % name))


def installer_dir():
    """What the installer directory holds: its files, by their path in it."""
    root = pl.INSTALLER_DIR % NAME
    found = []
    for path, _, names in os.walk(root):
        found += [os.path.relpath(os.path.join(path, n), root).replace(os.sep, '/') for n in names]
    return sorted(found)


def fetch():
    try:
        return pl.fetch_installer(NAME, SCRIPT), ''
    except (pl.Unsigned, ValueError, OSError) as error:
        return None, str(error)


owner, stranger = make_key('owner'), make_key('stranger')
trust('owner')

# -- what the Plugins tab lists for a plugin that is not here
publish(owner)
found = pl.latest_release(NAME)
check('a signed release: offered, by its signed tag and commit',
      (found.get('status'), found.get('tag'), found.get('sha'), found.get('signed_with')),
      ('found', TAG, COMMIT, 'xgs-release-key-owner.pub'))
publish(owner, signature=False)
check('no signature: not offered, and said', pl.latest_release(NAME),
      {'status': 'failed', 'message': 'its latest release is not signed yet: it has no release.json.sig'})
shutil.rmtree(os.path.join(RELEASES, NAME))
check('no manifest - every plugin release before 2026-11-07: not offered, and said',
      pl.latest_release(NAME).get('message'), 'its latest release is not signed yet: it has no release.json')
publish(stranger)
check('signed with another key: not offered', pl.latest_release(NAME).get('message'),
      'the signature of its latest release does not verify with any release key of this appliance')
trust()
check('no key on the appliance: not offered', pl.latest_release(NAME).get('message'),
      "the signature of its latest release does not verify with this appliance's release keys - it has none")
trust('owner')
# K9 (the release check's review, 2026-11-07): the manifest and its signature read through 'latest' twice paired
# one release's manifest with another's signature when a release was published between the two reads. The
# signature is read by the tag the manifest names now: 'latest' holding another signature, the manifest's own
# under its tag, the release is offered.
publish(owner)
with open(os.path.join(RELEASES, NAME, 'latest', 'download', 'release.json.sig'), 'wb') as f:
    f.write(sign(owner, b'the manifest of the release before\n'))
check('K9: latest holding the signature of another manifest: the one under its tag read, offered',
      pl.latest_release(NAME).get('status'), 'found')
os.remove(os.path.join(RELEASES, NAME, 'download', TAG, 'release.json.sig'))
check('K9: ... and none under its tag: not signed yet, as before', pl.latest_release(NAME).get('message'),
      'its latest release is not signed yet: it has no release.json.sig')
for what, fields, said in (('another repository', {'repository': 'AbdelmonemAwad/os-fanctl'}, 'of another repository'),
                           ('no commit', {'commit': None}, 'has no commit'),
                           ('an archive name with a path', {'archive': '../x.tar.gz'}, 'has no archive'),
                           ('a size of true', {'size': True}, 'has no size'),
                           ('a format it does not read', {'format': 2}, 'of a format')):
    publish(owner, fields=fields)
    check('a signed manifest with %s: not offered' % what, said in pl.latest_release(NAME).get('message', ''), True)

# -- what Install takes out of the signed archive
publish(owner)
path, error = fetch()
with open(path or os.devnull, encoding='utf-8') as f:
    body = f.read() if path else ''
check("Install takes the plugin's self-update.py and the release key beside it, and nothing else",
      (error, installer_dir(), body), ('', ['release-keys/xgs-release-key-test.pub', 'self-update.py'], SELF_UPDATE))

signed = archive_of(files())
for what, served, said in (('another of the same size', signed[:-1] + bytes([signed[-1] ^ 1]), 'not the one signed'),
                           ('a longer one', signed + b'more', 'not the one signed'),
                           ('a shorter one', signed[:-10], 'not the one signed')):
    publish(owner, archive=signed, serve=served)
    path, error = fetch()
    check('an archive not the one signed (%s): nothing taken, nothing left' % what,
          (path, said in error, os.path.exists(pl.INSTALLER_DIR % NAME)), (None, True, False))

publish(owner, archive=archive_of({SOURCE: SELF_UPDATE, 'install/install.sh': '#!/bin/sh\n'}))
path, error = fetch()
check('a signed archive with no release key: nothing taken - its self-update.py would install nothing',
      (path, 'no release key' in error, os.path.exists(pl.INSTALLER_DIR % NAME)), (None, True, False))
publish(owner, archive=archive_of({KEYDIR + 'k.pub': 'k\n', 'install/install.sh': '#!/bin/sh\n'}))
path, error = fetch()
check('a signed archive with no self-update.py: nothing taken', (path, 'has no %s' % SOURCE in error), (None, True))

ESCAPE = os.path.join(WORK, 'escape.pub')
publish(owner, archive=archive_of(files(**{'../../escape.pub': 'x\n', KEYDIR + '../../../escape.pub': 'x\n',
                                           KEYDIR + 'deeper/k.pub': 'x\n', KEYDIR + '.hidden.pub': 'x\n',
                                           KEYDIR + 'notes.txt': 'x\n'})))
path, error = fetch()
check('nothing of the archive lands outside the installer directory, nor beside the keys it holds',
      (error, os.path.exists(ESCAPE), installer_dir()),
      ('', False, ['release-keys/xgs-release-key-test.pub', 'self-update.py']))

try:
    pl.fetch_installer('opnsense-rtl', '/root/opnsense-rtl/self-update.py')
    said = 'taken'
except ValueError as refused:
    said = str(refused)
check('a plugin kept anywhere but under scripts/ is not one Install takes', said,
      'opnsense-rtl is not one this page can install')

# -- start(): what the page's Install answers
pl.suits = lambda name: True
pl.spawn = lambda names: None
pl.PROGRESS = os.path.join(TOP, 'plugins-update.json')
pl.PLUGINS = [(NAME, os.path.join(TOP, 'not-here', 'self-update.py'), 'port guard')]
pl.source_path = lambda script: SOURCE
shutil.rmtree(os.path.join(RELEASES, NAME))
answer = pl.start([NAME], install=True)
check("Install of a plugin whose latest release is not signed: refused, and why",
      answer, {'status': 'failed', 'message': '%s: its latest release is not signed yet: it has no release.json; '
                                              'nothing was installed' % NAME})
publish(owner)
answer = pl.start([NAME], install=True)
with open(pl.PROGRESS, encoding='utf-8') as f:
    queued = json.load(f)
check('Install of a signed one: started, the self-update.py taken out of its archive the one the runner starts',
      (answer.get('status'), queued['scripts'].get(NAME)),
      ('started', os.path.join(pl.INSTALLER_DIR % NAME, 'self-update.py')))

shutil.rmtree(TOP, ignore_errors=True)
print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
