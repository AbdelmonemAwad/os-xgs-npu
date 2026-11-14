#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
Which front ports the Intrusion Detection inspects in IPS mode at an MTU their netmap adapter cannot carry.

    ids_mtu.py          one line per such port, and nothing when there is none (for a shell and the log)
    ids_mtu.py --json   {"on", "ports": [...], "over": [...]} (what status.py puts on the Overview)

WHY. In IPS mode Suricata reads a port through netmap. A front port of either driver gets netmap's
emulated adapter unless the driver gives it one of its own, and the emulated adapter carries a frame in
one 2,048-byte buffer: its transmit copies each frame into a single mbuf cluster and cuts the rest
(kernel log "nm_os_generic_xmit_frame size 2048 < len 9014"), and with dev.netmap.buf_size at 2048 it
refuses the port outright ("netmap buf size (2048) < device MTU (9000)"). Measured on the XGS 136,
2026-11-11 (experiment item 4): a port at MTU 9000 passed 1,472-byte pings and lost every 8,972-byte
one, and Suricata started as a daemon on such a port exits without a word. Nothing in OPNsense says
any of this, so the Overview and the log at apply do.

THE LIMITS, as an MTU. Emulated: 2,048 bytes less an Ethernet header and one VLAN tag, 2,030.
Native - npuep's own adapter, hw.npuep.netmap=1 at the loader, XGS 136 only (#1154) - one frame a
slot, so netmap's buffer size less the same 18 bytes; with dev.netmap.buf_size at 9216 that is 9,198
and a port at MTU 9000 runs whole (measured 2026-11-11). A port is taken as native only when the
driver published that port's rings (dev.npuep.0.giu.netmap_port.PortN): a port netmap_attach failed
for is left emulated by the driver, and is reported as emulated here. A VLAN on a front port is
always emulated: netmap gives an interface of its own kind the generic adapter.

Read-only: config.xml, sysctl and ifconfig, nothing written.
"""

import json
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

CONFIG = "/conf/config.xml"
HEADERS = 18                 # an Ethernet header and one VLAN tag
EMULATED_BUF = 2048          # the emulated adapter's buffer: one mbuf cluster
FRONT = re.compile(r"^(oxp|npup)(\d+)(.*)$")


def sysctl(name):
    try:
        out = subprocess.run(["/sbin/sysctl", "-n", name], capture_output=True, text=True,
                             errors="replace", timeout=5)
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        return None
    return out.stdout.strip() or None


def sysctl_int(name):
    v = sysctl(name)
    try:
        return int(v.split()[0]) if v is not None else None
    except (ValueError, IndexError):
        return None


def live_mtu(device):
    """The MTU the interface has now, or None when ifconfig does not know it."""
    try:
        out = subprocess.run(["/sbin/ifconfig", device], capture_output=True, text=True,
                             errors="replace", timeout=5)
    except (OSError, subprocess.SubprocessError):
        return None
    m = re.search(r"\bmtu (\d+)", out.stdout) if out.returncode == 0 else None
    return int(m.group(1)) if m else None


def netmap_buf_size(read_int=sysctl_int):
    """netmap's buffer size: the one in use once a netmap client has opened, else the one asked for."""
    for name in ("dev.netmap.buf_curr_size", "dev.netmap.buf_size"):
        v = read_int(name)
        if v:
            return v
    return EMULATED_BUF


def adapter(device, read_int=sysctl_int):
    """'native' or 'emulated' for one front port device, and the largest MTU it carries."""
    m = FRONT.match(device)
    if m and m.group(1) == "npup" and m.group(3) == "" and \
            read_int("dev.npuep.0.giu.netmap_port.Port%s.rings" % m.group(2)) is not None:
        return "native", netmap_buf_size(read_int) - HEADERS
    return "emulated", EMULATED_BUF - HEADERS


def check(root, read_int=sysctl_int, mtu_of=live_mtu):
    """{"on": IPS in netmap mode, "ports": every inspected front port, "over": those above their limit}.
    root is config.xml's root element; read_int and mtu_of are the system's, replaceable for a test."""
    ids = root.find("OPNsense/IDS/general")

    def text(node, key, default):
        value = node.findtext(key) if node is not None else None
        return (value or default).strip()

    on = text(ids, "enabled", "0") == "1" and text(ids, "mode", "pcap") == "netmap"
    out = {"on": on, "ports": [], "over": []}
    if not on:
        return out
    for key in [k.strip() for k in text(ids, "interfaces", "").split(",") if k.strip()]:
        device = (root.findtext("interfaces/%s/if" % key) or "").strip()
        if not FRONT.match(device):
            continue
        mtu = mtu_of(device)
        if mtu is None:
            configured = (root.findtext("interfaces/%s/mtu" % key) or "").strip()
            mtu = int(configured) if configured.isdigit() else 1500
        kind, carries = adapter(device, read_int)
        port = {
            "interface": (root.findtext("interfaces/%s/descr" % key) or key.upper()).strip(),
            "device": device,
            "mtu": mtu,
            "adapter": kind,
            "carries": carries,
        }
        out["ports"].append(port)
        if mtu > carries:
            out["over"].append(port)
    return out


def lines(result):
    """The warning, one line per port, as the log at apply has it."""
    return ["Intrusion Detection in IPS mode on %s (%s): MTU %d is above the %d its %s netmap adapter "
            "carries, so larger frames are cut and Suricata may not start; set the MTU to 1500 on "
            "IPS ports, or on the XGS 136 use the native adapter (hw.npuep.netmap=1 and "
            "dev.netmap.buf_size=9216 in System: Settings: Tunables, then reboot)"
            % (p["interface"], p["device"], p["mtu"], p["carries"], p["adapter"])
            for p in result["over"]]


def read(path=CONFIG):
    try:
        return check(ET.parse(path).getroot())
    except (OSError, ET.ParseError):
        return None


def main(argv):
    result = read()
    if "--json" in argv:
        json.dump(result, sys.stdout)
        sys.stdout.write("\n")
        return 0
    for line in lines(result) if result else []:
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
