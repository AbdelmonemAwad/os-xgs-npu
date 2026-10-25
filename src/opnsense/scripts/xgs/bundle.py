#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""A support bundle: what somebody helping with this appliance needs, cleaned, in one file.

    bundle.py      write the bundle and print {"file", "size", "items", "data"} - data is the
                   .tar.gz in base64, which the Problem Reports page hands to the browser to save

WHAT IS IN IT. The coprocessor's state as the pages read it (status, ports, transceivers, IPsec,
the updater), the driver's sysctls and the kernel's modules, the boot messages, the plugin's own
lines from the system log, what the coprocessor printed on its console, the network interfaces and their counters, the switch's frame limit,
OPNsense's version and the reports already held - each a text file, every one put through the
Problem Reports cleaner first (reporter.Scrubber): addresses, MACs, serial numbers, the host and
domain names, e-mail addresses, keys and fingerprints are replaced before anything is written. The
modules' own serial numbers are taken out of the transceivers file before that, because they are
not shaped like anything the cleaner looks for.

WHAT IS NOT. The configuration, its backups, certificates and keys, any password: nothing is read
from /conf except the host and domain names the cleaner needs in order to remove them. The bundle
is made on request only, kept nowhere on the appliance, and sent nowhere - the owner saves it and
decides whom to give it to.
"""

import base64
import io
import json
import os
import subprocess
import sys
import tarfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import reporter  # noqa: E402

XGS = '/usr/local/opnsense/scripts/xgs'
LOG = '/var/log/system/latest.log'
CONSOLE_LOG = '/var/log/xgs/npu-console.log'
TAGS = ('octep', 'npuep', 'npuctl', 'xgs-', 'linkhealth', 'fanctl', 'frontpanel', 'netreport')


def command(argv, timeout=60):
    try:
        done = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        return done.stdout + (('\n[stderr]\n' + done.stderr) if done.stderr.strip() else '')
    except (OSError, subprocess.SubprocessError) as failure:
        return '[could not run %s: %s]\n' % (' '.join(argv), failure)


def pretty(text):
    try:
        return json.dumps(json.loads(text), indent=2) + '\n'
    except ValueError:
        return text


def transceivers():
    """The cages as last read, without the modules' serial numbers."""
    try:
        with open('/var/db/os-xgs-npu/transceivers.json', 'r') as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return '[not read yet]\n'
    for module in data.get('modules') or []:
        module.pop('serial', None)
    return json.dumps(data, indent=2) + '\n'


def our_log_lines(limit=3000):
    lines = []
    try:
        with open(LOG, 'r', errors='replace') as handle:
            for line in handle:
                if any(tag in line for tag in TAGS):
                    lines.append(line)
    except OSError:
        return '[no system log at %s]\n' % LOG
    return ''.join(lines[-limit:])


def console_log_tail(limit=262144):
    """The last LIMIT bytes of the coprocessor's console log, the older file first if it is short."""
    text = b''
    for path in (CONSOLE_LOG + '.1', CONSOLE_LOG):
        try:
            with open(path, 'rb') as handle:
                text += handle.read()
        except OSError:
            pass
    if not text:
        return '[no console log at %s]\n' % CONSOLE_LOG
    return text[-limit:].decode('utf-8', errors='replace').replace('\r', '').replace('\x1b', '')


def items():
    """(name, text) for every file in the bundle, in the order they are listed."""
    return [
        ('versions.txt', command(['/usr/local/sbin/opnsense-version']) + command(['/usr/bin/uname', '-a']) +
         command(['/bin/cat', '/usr/local/share/os-xgs-npu/installed.json'])),
        ('system.json', json.dumps(reporter.system_block(), indent=2) + '\n'),
        ('status.json', pretty(command([XGS + '/status.py']))),
        ('ports.json', pretty(command([XGS + '/portmap.py']))),
        ('transceivers.json', transceivers()),
        ('ipsec.json', pretty(command([XGS + '/ipsec.py']))),
        ('updates.json', pretty(command([XGS + '/update.py', 'status']))),
        ('reports.json', pretty(command([XGS + '/reporter.py', 'list']))),
        ('watchdog.txt', command(['/bin/sh', XGS + '/watchdog.sh', 'status'])),
        ('reset-reason.json', pretty(command([XGS + '/reset_reason.py', 'show']))),
        ('sysctl-driver.txt', command(['/sbin/sysctl', '-e', 'dev.octep', 'dev.npuep', 'hw.octep', 'hw.npuep'])),
        ('kldstat.txt', command(['/sbin/kldstat', '-v'])),
        ('dmesg.txt', command(['/sbin/dmesg'])),
        ('ifconfig.txt', command(['/sbin/ifconfig', '-a'])),
        # Each interface's packets, bytes, errors and drops, in and out: where frames go missing,
        # which ifconfig does not show (an XGS 107 losing most of its traffic, 2026-10-24).
        ('netstat-interfaces.txt', command(['/usr/bin/netstat', '-i', '-b', '-d', '-n', '-W'])),
        # What the switch's frame limit was set to at this boot (scripts/xgs/switch-jumbo.sh).
        ('switch-jumbo.txt', command(['/bin/cat', '/var/db/os-xgs-npu/switch-jumbo.state'])),
        ('log-plugin-lines.txt', our_log_lines()),
        # What the coprocessor printed on its console (scripts/xgs/console_log.py): its last words
        # when it failed or restarted on its own (#454, #768).
        ('coprocessor-console.txt', console_log_tail()),
    ]


def main():
    clean = reporter.Scrubber()
    stamp = time.strftime('%Y%m%d-%H%M%S')
    name = 'xgs-support-%s.tar.gz' % stamp
    folder = 'xgs-support-%s' % stamp
    buffer = io.BytesIO()
    listed = []
    with tarfile.open(fileobj=buffer, mode='w:gz') as tar:
        for filename, text in items():
            data = clean(text).encode('utf-8', 'replace')
            info = tarfile.TarInfo('%s/%s' % (folder, filename))
            info.size = len(data)
            info.mtime = int(time.time())
            info.mode = 0o644
            tar.addfile(info, io.BytesIO(data))
            listed.append({'name': filename, 'bytes': len(data)})
        readme = ('A support bundle from an appliance running os-xgs-npu, made %s.\n'
                  'Addresses, MACs, serial numbers, host and domain names, e-mail addresses, keys and\n'
                  'fingerprints were replaced before anything was written. No configuration, password,\n'
                  'certificate or key is in it.\n' % time.strftime('%Y-%m-%d %H:%M:%S %Z')).encode('utf-8')
        info = tarfile.TarInfo('%s/README.txt' % folder)
        info.size = len(readme)
        info.mtime = int(time.time())
        info.mode = 0o644
        tar.addfile(info, io.BytesIO(readme))
    blob = buffer.getvalue()
    print(json.dumps({'file': name, 'size': len(blob), 'items': listed,
                      'data': base64.b64encode(blob).decode('ascii')}))
    subprocess.run(['/usr/bin/logger', '-t', 'xgs-bundle',
                    'support bundle made on request (%d bytes, %d files)' % (len(blob), len(listed) + 1)],
                   capture_output=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
