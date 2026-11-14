#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/ids_mtu.py on made-up config.xml, sysctl and MTU: which front ports the Intrusion Detection
inspects in IPS mode at an MTU their netmap adapter cuts.

    python3 tools/test-ids-mtu.py

A port missed is Suricata cutting jumbo frames, or not running, with no page saying so; a port reported
wrongly is a warning about a setup that works (the XGS 136's native adapter at MTU 9000).
"""
import importlib.util
import os
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    'ids_mtu', os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'ids_mtu.py'))
im = importlib.util.module_from_spec(spec)
spec.loader.exec_module(im)

failures = 0


def check(what, got, want):
    global failures
    if got != want:
        failures += 1
        print('FAIL %s: got %r, want %r' % (what, got, want))
    else:
        print('ok   %s' % what)


def config(enabled='1', mode='netmap', interfaces='opt1,opt2,lan'):
    return ET.fromstring(
        '<opnsense><interfaces>'
        '<lan><if>igc0</if><descr>LAN</descr></lan>'
        '<opt1><if>npup8</if><descr>Port8</descr><mtu>9000</mtu></opt1>'
        '<opt2><if>npup3</if><descr>Port3</descr></opt2>'
        '<opt3><if>oxp4</if><descr>P4</descr></opt3>'
        '<opt4><if>oxp4_vlan10</if></opt4>'
        '</interfaces><OPNsense><IDS><general><enabled>%s</enabled><mode>%s</mode>'
        '<interfaces>%s</interfaces></general></IDS></OPNsense></opnsense>' % (enabled, mode, interfaces))


def sysctls(table):
    return lambda name: table.get(name)


mtus = {'npup8': 9000, 'npup3': 1500, 'oxp4': 2030, 'oxp4_vlan10': 2031, 'igc0': 9000}

# emulated everywhere: npup8 at 9000 is over, npup3 at 1500 is not, igc0 is not a front port
r = im.check(config(), sysctls({}), mtus.get)
check('IPS on', r['on'], True)
check('front ports only', [p['device'] for p in r['ports']], ['npup8', 'npup3'])
check('emulated, 9000 over', [(p['device'], p['adapter'], p['carries']) for p in r['over']],
      [('npup8', 'emulated', 2030)])
check('one line in the log', len(im.lines(r)), 1)
check('the line names both ways out', all(s in im.lines(r)[0] for s in ('1500', 'hw.npuep.netmap=1')), True)

# native adapter with 9216-byte buffers on Port8: carries 9198, so nothing over
native = {'dev.npuep.0.giu.netmap_port.Port8.rings': 4, 'dev.netmap.buf_size': 9216}
r = im.check(config(), sysctls(native), mtus.get)
check('native 9216 carries 9000', r['over'], [])
check('native named', r['ports'][0]['adapter'], 'native')

# native but buffers still 2048: over, and the buffer in use wins over the one asked for
r = im.check(config(), sysctls({'dev.npuep.0.giu.netmap_port.Port8.rings': 4, 'dev.netmap.buf_size': 2048}), mtus.get)
check('native 2048 over', [(p['device'], p['carries']) for p in r['over']], [('npup8', 2030)])
r = im.check(config(), sysctls({'dev.npuep.0.giu.netmap_port.Port8.rings': 4, 'dev.netmap.buf_size': 2048,
                                'dev.netmap.buf_curr_size': 9216}), mtus.get)
check('buffer in use counts', r['over'], [])

# a port netmap_attach failed for (no rings published) stays emulated
r = im.check(config(), sysctls({'dev.npuep.0.giu.netmap_port.Port3.rings': 4, 'dev.netmap.buf_size': 9216}), mtus.get)
check('no rings, emulated', [p['adapter'] for p in r['over']], ['emulated'])

# the 3300: octep has no native adapter; 2030 is the edge, and a VLAN is emulated too
r = im.check(config(interfaces='opt3,opt4'), sysctls({'dev.netmap.buf_size': 9216}), mtus.get)
check('2030 carried, 2031 on a VLAN not', [p['device'] for p in r['over']], ['oxp4_vlan10'])

# off, or IDS mode (pcap): nothing said
check('disabled', im.check(config(enabled='0'), sysctls({}), mtus.get), {'on': False, 'ports': [], 'over': []})
check('pcap mode', im.check(config(mode='pcap'), sysctls({}), mtus.get)['over'], [])

# ifconfig does not answer: the configured MTU, else 1500
r = im.check(config(), sysctls({}), lambda d: None)
check('configured MTU when ifconfig is silent', [(p['device'], p['mtu']) for p in r['ports']],
      [('npup8', 9000), ('npup3', 1500)])

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
