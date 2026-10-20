#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""Count this appliance among the ones running os-xgs-npu, on npuforge.org's home page.

    count.py           send, once a day from cron.d/xgs-count and once at the end of install.sh,
                       unless XGS: Problem Reports says not to
    count.py --show    print what would be sent, and send nothing

WHAT IS SENT. Three things and no others:

    id        32 hex digits this appliance made for itself the first time, at random, and kept in
              /var/db/os-xgs-npu/appliance-id - nothing read from the hardware, so it says nothing
              about the unit, and deleting the file makes the appliance a new one
    model     the board's own name for itself, e.g. "xgs3300r1" (SMBIOS product and version)
    release   the os-xgs-npu release installed, or its commit when it is not a release

The site keeps each identifier with its model, its release and the first and last day it was heard
from, and counts an appliance once however often it sends; it does not keep the address the
request came from (website/worker/index.js, /api/appliance; npuforge.org/privacy/). Turned off on
XGS: Problem Reports ("Count this appliance"), nothing is sent and the identifier is left alone.

Never fatal and never in the way: a failure is one line in the log and nothing else.
"""
import json
import os
import re
import secrets
import subprocess
import sys
import urllib.request
import xml.etree.ElementTree as ET

URL = os.environ.get('XGS_COUNT_URL', 'https://npuforge.org/api/appliance')
CONFIG = '/conf/config.xml'
ID_FILE = '/var/db/os-xgs-npu/appliance-id'
INSTALLED = '/usr/local/share/os-xgs-npu/installed.json'


def enabled():
    """On unless XGS: Problem Reports has it off."""
    try:
        node = ET.parse(CONFIG).getroot().find('OPNsense/XGS/reporting/count')
    except (OSError, ET.ParseError):
        return True
    return node is None or (node.text or '').strip() != '0'


def appliance_id(make=True):
    try:
        with open(ID_FILE, encoding='ascii') as handle:
            value = handle.read().strip()
        if re.fullmatch(r'[0-9a-f]{32}', value):
            return value
    except (OSError, UnicodeDecodeError):
        pass
    if not make:
        return '(made at random at the first send)'
    value = secrets.token_hex(16)
    os.makedirs(os.path.dirname(ID_FILE), exist_ok=True)
    with open(ID_FILE + '.tmp', 'w', encoding='ascii') as handle:
        handle.write(value + '\n')
    os.replace(ID_FILE + '.tmp', ID_FILE)
    return value


def kenv(key):
    try:
        return subprocess.run(['/bin/kenv', '-q', key], capture_output=True, text=True,
                              timeout=10).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ''


def model():
    name = re.sub(r'[^a-z0-9-]', '', (kenv('smbios.system.product') + kenv('smbios.system.version')).lower())
    return name[:24] if len(name) >= 2 else 'unknown'


def release():
    try:
        with open(INSTALLED, encoding='utf-8') as handle:
            inst = json.load(handle)
    except (OSError, ValueError):
        inst = {}
    tag = inst.get('release') or ''
    if re.fullmatch(r'v\d[0-9a-z.+-]{0,30}', tag):
        return tag
    commit = inst.get('commit') or ''
    return commit[:12] if re.fullmatch(r'[0-9a-f]{7,40}', commit) else 'unknown'


def log(message):
    subprocess.run(['/usr/bin/logger', '-t', 'xgs-count', '-p', 'daemon.notice', message], check=False)


def main():
    show = '--show' in sys.argv[1:]
    if not enabled():
        if show:
            print('off on XGS: Problem Reports - nothing is sent')
        return 0
    if show:
        print(json.dumps({'id': appliance_id(make=False), 'model': model(), 'release': release()}))
        return 0
    body = {'id': appliance_id(), 'model': model(), 'release': release()}
    request = urllib.request.Request(URL, data=json.dumps(body).encode('ascii'), method='POST', headers={
        'Content-Type': 'application/json', 'User-Agent': 'os-xgs-npu-count'})
    try:
        with urllib.request.urlopen(request, timeout=20):
            pass
    except OSError as error:
        log('not counted this time: %s' % error)
    return 0


if __name__ == '__main__':
    sys.exit(main())
