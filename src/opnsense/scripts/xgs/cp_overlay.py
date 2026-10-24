#!/usr/local/bin/python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The Captive Portal's accounting, told what the coprocessor carried - an overlay on OPNsense's own
file, put only on the versions of it it was written for.

The Captive Portal counts each client's traffic in ipfw (scripts/captiveportal/lib/ipfw.py,
IPFW.list_accounting_info: a count rule per address and direction), and its background process takes
the idle timeout, a session's limits and the RADIUS accounting from those counts. A connection the
coprocessor forwards never reaches ipfw, so a client whose traffic runs in hardware would look idle
and uncounted. The overlay adds, to what ipfw counted for each client, what the coprocessor carried for
it in its current session - the file cp_users.py writes (COUNTS) - before the background process reads
the counts. Nothing else in the file changes.

    cp_overlay.py sync      on while it is wanted, off when it is not (below); print JSON
    cp_overlay.py apply     put the overlay on if the file is a version it knows; print JSON
    cp_overlay.py remove    put the original back; print JSON
    cp_overlay.py status    print JSON: whether it is on, and why not

WHEN. The file is OPNsense's, and the firmware's health audit names it as changed while the overlay is
on, so it is on only while it is wanted: while XGS: Settings runs the portal's clients in hardware,
and after that until no session still has counts from the coprocessor - taken away from a session,
they would make the portal take its smaller total for a reset and count all of it again. cp_users.py
syncs it every two seconds; the plugin's removal takes it off.

THE GUARD. The file is changed only when its SHA-256 is one KNOWN lists - the original as an OPNsense
release ships it, read and checked by hand - and the change is the same lines at the same place each
time, so the result has a hash of its own, which is how "on" is told. Any other file - an OPNsense
update that changed it, an edit by hand - is left as it is, and status says so: the zones that need
the counts then stay with the host (cp_users.py), and XGS: Settings says why. An update that ships the
known file again overwrites the overlay; the pkg trigger (os-xgs-npu-cp.ucl) and the boot hook
(67-xgs-cp-overlay) sync it at once, and a new version waits until KNOWN lists it.

Applied or removed, the Captive Portal is restarted when it runs, so its background process reads the
file as it now is. Never fails its caller.
"""
import hashlib
import json
import os
import subprocess
import sys
import xml.etree.ElementTree as ET

TARGET = '/usr/local/opnsense/scripts/captiveportal/lib/ipfw.py'
CONFIG = '/conf/config.xml'
# Written out, not joined: COUNTS goes into the overlaid file, whose hash must not depend on anything.
STATE_DIR = '/var/db/os-xgs-npu/cp'
COUNTS = '/var/db/os-xgs-npu/cp/hw-counts.json'
ORIGINALS = '/var/db/os-xgs-npu/cp/originals'
STATUS = '/var/db/os-xgs-npu/cp/overlay.json'

# The original file, by its SHA-256, as each release ships it. Read on the appliances and in
# opnsense/core's stable/26.7 on 2026-10-24: the same file in 26.7 to 26.7.7.
KNOWN = {
    'b948aa4baca65391868a6b3c566ba5502caae7a68b0833ad9420515095715d16': 'OPNsense 26.7 to 26.7.7',
}

# Where the lines go: the end of list_accounting_info, before add_accounting. Found once, or nothing
# is changed.
ANCHOR = '        return result\n\n    @staticmethod\n    def add_accounting('
MARK = '# os-xgs-npu overlay'
BLOCK = '''        ''' + MARK + ''': what the coprocessor carried for each client in its session, which ipfw
        # never sees (os-xgs-npu, scripts/xgs/cp_overlay.py; removed with it).
        try:
            import json
            with open(''' + repr(COUNTS) + ''') as handle:
                carried = json.load(handle)
            for address, hw in carried.items():
                if address in result:
                    for key in ('in_pkts', 'in_bytes', 'out_pkts', 'out_bytes'):
                        result[address][key] += int(hw.get(key, 0))
                    result[address]['last_accessed'] = max(result[address]['last_accessed'],
                                                           int(hw.get('last_accessed', 0)))
        except Exception:
            pass
'''


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def overlaid(original):
    """The known original with the overlay on, or None if the anchor is not where it should be."""
    text = original.decode('utf-8')
    if text.count(ANCHOR) != 1 or MARK in text:
        return None
    return text.replace(ANCHOR, BLOCK + ANCHOR).encode('utf-8')


def patched_hashes():
    """The hash each known original has with the overlay on, from the originals kept here."""
    out = {}
    for digest in KNOWN:
        try:
            with open(ORIGINALS + '/' + digest, 'rb') as handle:
                data = handle.read()
        except OSError:
            continue
        if sha256(data) == digest:
            made = overlaid(data)
            if made is not None:
                out[sha256(made)] = digest
    return out


def read_target():
    try:
        with open(TARGET, 'rb') as handle:
            return handle.read()
    except OSError:
        return None


def status():
    """{on, code, why, sha256, version}: code is what XGS: Settings words - on, not_applied,
    unknown_version, no_file."""
    data = read_target()
    if data is None:
        return {'on': False, 'code': 'no_file', 'why': 'no Captive Portal accounting file at ' + TARGET,
                'sha256': None, 'version': None}
    digest = sha256(data)
    patched = patched_hashes()
    if digest in patched:
        return {'on': True, 'code': 'on', 'why': '', 'sha256': digest, 'version': KNOWN[patched[digest]]}
    if digest in KNOWN:
        return {'on': False, 'code': 'not_applied', 'why': 'not applied', 'sha256': digest,
                'version': KNOWN[digest]}
    if MARK.encode('utf-8') in data:
        why = 'the file carries the overlay, but not on a version this knows'
    else:
        why = 'the Captive Portal\'s accounting file is a version this was not written for'
    return {'on': False, 'code': 'unknown_version', 'why': why, 'sha256': digest, 'version': None}


def restart_portal():
    """The background process reads the file when it starts: restarted if the portal runs."""
    try:
        running = subprocess.run(['/usr/local/sbin/configctl', 'captiveportal', 'status'], capture_output=True,
                                 text=True, timeout=30).stdout
        if 'is running' in running:
            subprocess.run(['/usr/local/sbin/configctl', 'captiveportal', 'restart'], capture_output=True,
                           timeout=60)
            return True
    except (OSError, subprocess.SubprocessError):
        pass
    return False


def write_atomic(path, data, mode):
    tmp = path + '.xgs-tmp'
    with open(tmp, 'wb') as handle:
        handle.write(data)
    os.chmod(tmp, mode)
    os.rename(tmp, path)


def apply():
    data = read_target()
    if data is None:
        return dict(status(), changed=False)
    digest = sha256(data)
    if digest not in KNOWN:
        return dict(status(), changed=False)
    made = overlaid(data)
    if made is None:
        return dict(status(), changed=False)
    os.makedirs(ORIGINALS, exist_ok=True)
    keep = ORIGINALS + '/' + digest
    if not os.path.exists(keep):
        write_atomic(keep, data, 0o444)
    write_atomic(TARGET, made, os.stat(TARGET).st_mode & 0o777)
    restarted = restart_portal()
    return dict(status(), changed=True, restarted=restarted)


def remove():
    data = read_target()
    if data is None:
        return dict(status(), changed=False)
    patched = patched_hashes()
    digest = sha256(data)
    if digest not in patched:
        return dict(status(), changed=False)
    with open(ORIGINALS + '/' + patched[digest], 'rb') as handle:
        original = handle.read()
    write_atomic(TARGET, original, os.stat(TARGET).st_mode & 0o777)
    restarted = restart_portal()
    return dict(status(), changed=True, restarted=restarted)


def wanted():
    """True while the portal's clients run in hardware or a session still has the coprocessor's
    counts; None when config.xml cannot be read, and nothing is changed."""
    try:
        root = ET.parse(CONFIG).getroot()
    except (OSError, ET.ParseError):
        return None
    if (root.findtext('OPNsense/XGS/general/cp_hardware') or '0') == '1':
        return True
    try:
        with open(COUNTS) as handle:
            return bool(json.load(handle))
    except (OSError, ValueError):
        return False


def sync():
    want = wanted()
    if want is None:
        return dict(status(), changed=False)
    return apply() if want else remove()


def main(argv):
    what = argv[1] if len(argv) > 1 else 'status'
    try:
        os.makedirs(STATE_DIR, exist_ok=True)
        result = {'apply': apply, 'remove': remove, 'sync': sync}.get(what, status)()
    except OSError as err:
        result = dict(status(), changed=False, error=str(err))
    try:
        write_atomic(STATUS, json.dumps(result).encode('utf-8'), 0o644)
    except OSError:
        pass
    print(json.dumps(result))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
