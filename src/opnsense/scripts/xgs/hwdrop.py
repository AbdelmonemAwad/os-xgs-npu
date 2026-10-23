#!/usr/local/bin/python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""Drop the connections a cut names on the coprocessor itself, by their verdict, before their pf
states are killed - XGS: Settings "Cut in hardware" (discovery 3, 2026-10-23).

lists_cut.py and ja4_cut.py kill pf states by id, and each driver then takes the connection out of
hardware - within a second or two, and every frame after that crosses to this host for pf to drop.
With "Cut in hardware" on, every plain connection is made with the microflow action IPS and the
fast path's IPS switch is on (dp.mflow_action / flow.mflow_action, offload.sh), so a connection's
verdict decides each of its frames: DROP, written here, stops them on the coprocessor at once, both
ways, and the driver keeps the connection there while its sender goes on sending. A connection made
before the setting was turned on, and a tunnel's, is made to forward without asking: DROP changes
nothing for it, and the pf kill cuts it as before.

    import hwdrop
    hwdrop.drop_addresses(contains)  connections with either end in contains (an `in` test)
    hwdrop.drop_ends(ends)           connections both of whose ends, (address, port), are in ends

Each returns how many connections the coprocessor took DROP for. Never raises.
"""
import ipaddress
import os
import subprocess
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flows  # noqa: E402

CONFIG = '/conf/config.xml'
SYSCTL = '/sbin/sysctl'


def enabled(path=CONFIG):
    """On unless XGS: Settings has "Cut in hardware" off."""
    try:
        g = ET.parse(path).getroot().find('OPNsense/XGS/general')
    except (OSError, ET.ParseError):
        return True
    return g is None or (g.findtext('cut_in_hardware') or '1').strip() != '0'


def table():
    """(the driver's verdict sysctl, [connections as flows.py reads them]), or (None, [])."""
    text = flows.sysctl('dev.octep.0.dp.flow_table')
    if text is not None:
        return 'dev.octep.0.dp.flow_verdict', flows.octep(text)
    text = flows.sysctl('dev.npuep.0.flow.flows')
    if text is not None:
        return 'dev.npuep.0.flow.verdict', flows.npuep(text)
    return None, []


def address(text):
    try:
        return str(ipaddress.ip_address(text))
    except ValueError:
        return None


def drop(match, path=CONFIG):
    if not enabled(path):
        return 0
    oid, conns = table()
    if oid is None:
        return 0
    dropped = 0
    for f in conns:
        try:
            if not match(f):
                continue
            subprocess.run([SYSCTL, '%s=%d 1' % (oid, f['id'])], capture_output=True, timeout=10)
            said = subprocess.run([SYSCTL, '-n', oid], capture_output=True, text=True, timeout=10).stdout
        except (KeyError, TypeError, ValueError, OSError, subprocess.SubprocessError):
            continue
        if 'on the coprocessor' in said:
            dropped += 1
    return dropped


def drop_addresses(contains, path=CONFIG):
    return drop(lambda f: any(a is not None and a in contains
                              for a in (address(f.get('src', '')), address(f.get('dst', '')))), path)


def drop_ends(ends, path=CONFIG):
    def both(f):
        a, b = address(f.get('src', '')), address(f.get('dst', ''))
        return (a, f.get('sport')) in ends and (b, f.get('dport')) in ends
    return drop(both, path)
