#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""scripts/xgs/failclose.py's choice of the inspector's knob (dp.ips_loop, ips.loop): 0 - drop while
nothing reads oxips0 - only with "Drop when the inspection stops" and "Inspect, then hardware" on and
the Intrusion Detection on in IPS mode (Netmap); 1 - the drivers' own, pass - any other time, so a new
install, whose Intrusion Detection is off, drops nothing.

    python3 tools/test-failclose.py"""
import importlib.util
import os
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    'failclose', os.path.join(HERE, '..', 'src', 'opnsense', 'scripts', 'xgs', 'failclose.py'))
fc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fc)

failures = 0


def check(what, got, want):
    global failures
    if got != want:
        failures += 1
        print(f'FAIL {what}: got {got!r}, want {want!r}')
    else:
        print(f'ok   {what}')


def loop(xgs='', ids=''):
    root = ET.fromstring('<opnsense><OPNsense><XGS><general>%s</general></XGS><IDS><general>%s</general></IDS>'
                         '</OPNsense></opnsense>' % (xgs, ids))
    return fc.wanted(root)[0]


IPS = '<enabled>1</enabled><mode>netmap</mode>'
INSPECT = '<inspect_then_hardware>1</inspect_then_hardware>'
check('a new install: nothing saved, the Intrusion Detection off - pass', loop(), '1')
check('everything on, IPS mode - drop', loop(INSPECT, IPS), '0')
check('the setting saved on - drop', loop(INSPECT + '<failclose>1</failclose>', IPS), '0')
check('the setting off - pass', loop(INSPECT + '<failclose>0</failclose>', IPS), '1')
check('Inspect, then hardware off - pass', loop('<inspect_then_hardware>0</inspect_then_hardware>', IPS), '1')
check('the Intrusion Detection off - pass', loop(INSPECT, '<enabled>0</enabled><mode>netmap</mode>'), '1')
check('the Intrusion Detection in IDS mode - pass', loop(INSPECT, '<enabled>1</enabled><mode>pcap</mode>'), '1')

print('all passed' if not failures else f'{failures} failed')
sys.exit(1 if failures else 0)
