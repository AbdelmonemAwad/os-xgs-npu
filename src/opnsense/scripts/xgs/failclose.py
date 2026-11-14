#!/usr/local/bin/python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""Drop when the inspection stops - XGS: Settings, "Drop when the inspection stops" (general.failclose).

With "Inspect, then hardware" on, the fast path sends each new connection's frames to the coprocessor's
inspector function, and the driver gives them to oxips0 for Suricata. While nothing reads oxips0 - the
Intrusion Detection stopped, restarting, or exited - each driver does what its knob says: 1, its own
default, hands the frame straight back and sets the connection to forward, so it runs in hardware
uninspected; 0 drops the frame (dev.octep.0.dp.ips_loop, dev.npuep.0.ips.loop). This keeps that knob
in step with the configuration:

    0  failclose on, "Inspect, then hardware" on, and the Intrusion Detection enabled in IPS mode
       (Netmap): a connection meant to be inspected is never passed uninspected.
    1  anything else - so an Intrusion Detection that is off, or in IDS mode, which reads nothing
       from oxips0, never makes the inspected path drop. A new install has the Intrusion Detection
       off, and drops nothing.

Nothing in OPNsense tells a plugin that the Intrusion Detection was turned off, so this is run by XGS:
Settings' apply, at boot, and from cron every minute for 58 seconds, reading config.xml again every two
seconds: the knob follows the Intrusion Detection's page, and a driver loaded again, within two seconds.

    failclose.py            once: set the knob as config.xml says, print JSON
    failclose.py --for 58   the same, again every two seconds (cron)
"""
import json
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

CONFIG = '/conf/config.xml'
OIDS = ('dev.octep.0.dp.ips_loop', 'dev.npuep.0.ips.loop')
TAG = 'xgs-failclose'
STEP = 2


def log(message):
    try:
        subprocess.run(['/usr/bin/logger', '-t', TAG, '-p', 'daemon.notice', message], timeout=10)
    except (OSError, subprocess.SubprocessError):
        pass


def wanted(root):
    """'0' to drop while nothing reads oxips0, '1' to pass; and why, in words."""
    def text(path, default):
        value = root.findtext(path)
        return (value if value is not None and value.strip() != '' else default).strip()

    fc = text('OPNsense/XGS/general/failclose', '1') == '1'
    inspect = text('OPNsense/XGS/general/inspect_then_hardware', '0') == '1'
    ids_on = text('OPNsense/IDS/general/enabled', '0') == '1'
    ids_ips = text('OPNsense/IDS/general/mode', 'pcap') == 'netmap'
    if not fc:
        return '1', 'drop when the inspection stops: off'
    if not inspect:
        return '1', 'drop when the inspection stops: nothing to do while "Inspect, then hardware" is off'
    if not (ids_on and ids_ips):
        return '1', 'drop when the inspection stops: nothing to do while the Intrusion Detection is not on in IPS mode'
    return '0', 'drop when the inspection stops: on - a frame meant for the Intrusion Detection is dropped while it does not read oxips0'


def sysctl_get(oid):
    try:
        r = subprocess.run(['/sbin/sysctl', '-n', oid], capture_output=True, text=True, timeout=5)
    except (OSError, subprocess.SubprocessError):
        return None
    return r.stdout.strip() if r.returncode == 0 else None


def sysctl_set(oid, value):
    try:
        return subprocess.run(['/sbin/sysctl', '%s=%s' % (oid, value)], capture_output=True,
                              timeout=5).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def once():
    try:
        root = ET.parse(CONFIG).getroot()
    except (OSError, ET.ParseError):
        return {'applied': False, 'message': 'drop when the inspection stops: config.xml unreadable, left as it is'}
    want, why = wanted(root)
    for oid in OIDS:
        now = sysctl_get(oid)
        if now is None:
            continue
        if now == want:
            return {'applied': True, 'oid': oid, 'loop': want, 'message': why}
        if sysctl_set(oid, want):
            log('%s=%s (was %s): %s' % (oid, want, now, why))
            return {'applied': True, 'oid': oid, 'loop': want, 'message': why}
        log('the driver refused %s=%s' % (oid, want))
        return {'applied': False, 'oid': oid, 'message': 'drop when the inspection stops: the driver refused %s=%s' % (oid, want)}
    return {'applied': True, 'oid': None, 'loop': want,
            'message': why if want == '1' else why + "; the coprocessor's inspector function is not there yet"}


def main(argv):
    seconds = 0
    if '--for' in argv:
        try:
            seconds = max(0, min(58, int(argv[argv.index('--for') + 1])))
        except (IndexError, ValueError):
            print(json.dumps({'applied': False, 'message': 'usage: failclose.py [--for SECONDS]'}))
            return 2
    # Every step, not only when config.xml changed: a driver that attaches again - a reload, a
    # recovery in place (recover.sh) - starts its knob at 1, and is put back within a step.
    result = once()
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        time.sleep(max(0.0, min(STEP, end - time.monotonic())))
        result = once()
    print(json.dumps(result))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
