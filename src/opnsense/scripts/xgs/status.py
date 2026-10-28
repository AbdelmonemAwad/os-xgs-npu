#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
Report the coprocessor's state as JSON, for the status page and for anyone at a shell.

WHY A SCRIPT AND NOT PHP. Everything here is a sysctl read, and the sysctl tree is the driver's
own interface - it is what has been used to measure this hardware from the first day. Reading it
here keeps one statement of what a field means instead of two, and leaves the web layer with
nothing to do but draw.

BOTH FAMILIES. The OCTEON TX appliance runs the octep driver (dev.octep.0, SDP rings, oxp ports) and
the ARMADA one runs npuep (dev.npuep.0, GIU rings, npup ports). The two sysctl trees are different,
so this detects which driver is attached and reads the one that is, filling the same JSON shape -
the page draws whichever it gets. Everything a board has that the other does not simply reads as
absent on that board, which is the whole point of a plugin that supports every appliance.

Read-only. It posts no RPC command, sets no sysctl and touches no file; a status page that could
change the state of a firewall's datapath would be a poor trade for a table of numbers.
"""

import json
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

CONFIG = "/conf/config.xml"


def sysctl(name):
    """One sysctl, or None if it is not there. A missing node is normal on the other board."""
    try:
        out = subprocess.run(
            ["/sbin/sysctl", "-n", name],
            capture_output=True, text=True, timeout=5,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        return None
    return out.stdout.strip() or None


def sysctl_int(name):
    v = sysctl(name)
    if v is None:
        return None
    try:
        return int(v.split()[0])
    except (ValueError, IndexError):
        return None


def board():
    """Which appliance this is, read positively from the assembly number."""
    try:
        out = subprocess.run(
            ["/bin/sh", "/usr/local/opnsense/scripts/xgs/board.sh"],
            capture_output=True, text=True, timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return {"assembly": None, "family": None}
    if out.returncode != 0:
        return {"assembly": None, "family": None}
    parts = out.stdout.split()
    model = None
    try:
        import models
        a, _ = models.this()
        model = models.MODELS[a][0] if a in models.MODELS else None
    except Exception:
        pass
    return {
        "assembly": parts[0] if parts else None,
        "family": parts[1] if len(parts) > 1 else None,
        "model": model,
        # research and development: a model this project has not been run on (board.sh's "rnd")
        "rnd": len(parts) > 2 and parts[2] == "rnd",
        "serial": serial(),
    }


def serial():
    """The appliance's serial number, as on its label: SMBIOS's system serial (X330... on the XGS
    3300). For the owner's own page; the support bundle and the problem reports carry status.py's
    output only through reporter.Scrubber, which reads this same value and withholds it. None
    where the firmware left it unset."""
    try:
        out = subprocess.run(["/bin/kenv", "-q", "smbios.system.serial"],
                             capture_output=True, text=True, timeout=5)
    except (OSError, subprocess.SubprocessError):
        return None
    value = out.stdout.strip()
    return value if value and value.lower() not in ("default string", "to be filled by o.e.m.") else None


def ports(prefix, start, count):
    """
    Every front port, with the link state the switch actually reports.

    ethtool-style speed on a front port is fabricated by the far side - it answers 1000M link-up for
    an unplugged cage - so what is reported here is ifconfig's own status and media, which come from
    the driver's link poll and not from the coprocessor's guess.
    """
    rows = []
    for n in range(start, start + count):
        name = "%s%d" % (prefix, n)
        try:
            out = subprocess.run(
                ["/sbin/ifconfig", name], capture_output=True, text=True, timeout=5
            )
        except (OSError, subprocess.SubprocessError):
            continue
        if out.returncode != 0:
            continue
        status, media, descr = None, None, None
        for line in out.stdout.splitlines():
            line = line.strip()
            if line.startswith("status:"):
                status = line.split(":", 1)[1].strip()
            elif line.startswith("media:"):
                media = line.split(":", 1)[1].strip()
            elif line.startswith("description:"):
                descr = line.split(":", 1)[1].strip()
        rows.append({
            "device": name,
            "description": descr,
            "status": status,
            "media": media,
        })
    return rows


def first_line(name):
    """The first non-empty line of a multi-line sysctl, collapsed to single spaces, or None."""
    raw = sysctl(name)
    if raw is None:
        return None
    for line in raw.splitlines():
        line = line.strip()
        if line:
            return " ".join(line.split())
    return None


# ---- the OCTEON TX driver (octep): SDP rings, dp counters, oxp ports -------------------------------

OCTEP = "dev.octep.0"

OCTEP_DP_FIELDS = [
    ("rx_done", "frames taken off the receive rings"),
    ("rx_seen", "frames the ring reported"),
    ("rx_resync", "times the read index was moved past a gap"),
    ("rx_skipped", "descriptors skipped by a resync"),
    ("credit_capped", "times a receive grant was held back at the ring size"),
    ("tx_posted", "frames posted to the coprocessor"),
    ("oq_busy", "output-queue service passes that found work"),
    ("intr_taken", "interrupts delivered"),
    ("intr_drained", "interrupts that found the ring empty"),
    ("rxwd_runs", "receive watchdog runs"),
]


def octep_last_command():
    """The last RPC command and how it ended, without the reply body (up to 182 counter words)."""
    raw = sysctl("%s.rpc.last" % OCTEP)
    if raw is None:
        return None
    keep = []
    for line in raw.strip().splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("["):
            break
        keep.append(line)
        if "payload" in line:
            break
    return " ".join(" ".join(keep).split()) or None


def log_match(pattern):
    """The groups of the newest kernel message matching pattern: the message buffer first, then the
    copy of the boot's messages kept in /var/run/dmesg.boot, for a buffer that has wrapped since."""
    rx = re.compile(pattern)
    texts = []
    try:
        texts.append(subprocess.run(["/sbin/dmesg"], capture_output=True, text=True, timeout=10).stdout)
    except (OSError, subprocess.SubprocessError):
        pass
    try:
        with open("/var/run/dmesg.boot", errors="replace") as f:
            texts.append(f.read())
    except OSError:
        pass
    for text in texts:
        found = rx.findall(text)
        if found:
            return found[-1]
    return None


def octep_report(out):
    out["driver"] = {
        "description": sysctl("%s.%%desc" % OCTEP),
        "ready": sysctl_int("%s.ready" % OCTEP),
        "host_status": sysctl("%s.host_status" % OCTEP),
        "target_status": sysctl("%s.target_status" % OCTEP),
    }
    rings_raw = (sysctl("%s.sdp.rings" % OCTEP) or "").strip().splitlines()
    out["handshake"] = {
        "state": sysctl("%s.sdp.hs_state" % OCTEP),
        "rings": " ".join(rings_raw[0].split()) if rings_raw else None,
        "rings_in_use": " ".join(rings_raw[-1].split()) if len(rings_raw) > 1 else None,
        "vfs": sysctl_int("%s.sdp.nvfs" % OCTEP),
    }
    out["rpc"] = {
        "ready": sysctl_int("%s.rpc.ready" % OCTEP),
        "allow_write": sysctl_int("%s.rpc.allow_write" % OCTEP),
        "last": octep_last_command(),
    }
    out["netagent"] = {
        "ready": sysctl_int("%s.nwa.ready" % OCTEP),
        "commands": sysctl_int("%s.nwa.commands" % OCTEP),
        "timeouts": sysctl_int("%s.nwa.timeouts" % OCTEP),
    }
    fw_cfg = sysctl_int("%s.rpc.fw_cfg" % OCTEP)
    out["offload"] = {
        "fw_cfg_requested": fw_cfg,
        "offload_bit_requested": None if fw_cfg is None else bool(fw_cfg & 0x1),
        "note": (
            "fw_cfg is write-only on this coprocessor: this is the value the host last asked "
            "for, not a reading of the far side"
        ),
        "fw_rev_posted": sysctl_int("%s.rpc.fw_rev" % OCTEP),
        "fw_rev_bumps": sysctl_int("%s.rpc.fw_rev_bumps" % OCTEP),
        "fw_rev_bump_fail": sysctl_int("%s.rpc.fw_rev_bump_fail" % OCTEP),
        "fw_rev_bump_early": sysctl_int("%s.rpc.fw_rev_bump_early" % OCTEP),
    }
    counters = {}
    for key, descr in OCTEP_DP_FIELDS:
        v = sysctl_int("%s.dp.%s" % (OCTEP, key))
        if v is not None:
            counters[key] = {"value": v, "description": descr}
    out["datapath"] = counters
    out["ports"] = ports("oxp", 0, 12)
    # The board's own figures are in the line the driver logs when it reads the platform block;
    # what this host uses of them is the driver's own bounds.
    held = log_match(r"octep0: rpc: the board holds (\d+) connections, (\d+) next hops and (\d+) microflows")
    hi = sysctl_int("%s.rpc.hi_ready" % OCTEP)
    out["limits"] = {
        # from the platform block, as the XGS 136's driver reads it (rpc.platform, rpc.board_sas
        # since 2026-10-20; None on an older build)
        "platform": sysctl("%s.rpc.platform" % OCTEP) or None,
        "board_connections": int(held[0]) if held else None,
        "board_next_hops": int(held[1]) if held else None,
        "board_associations": sysctl_int("%s.rpc.board_sas" % OCTEP) or None,
        "connections_max": sysctl_int("%s.dp.conn_max" % OCTEP),
        "connections_in_use": sysctl_int("%s.dp.flows" % OCTEP),
        "next_hops_max": sysctl_int("%s.dp.nhop_max" % OCTEP),
        "next_hops_in_use": sysctl_int("%s.dp.nhops" % OCTEP),
        "transmit_queues": sysctl_int("%s.dp.ntxq" % OCTEP),
        "flow_writes": None if hi is None else ("posted on the high-priority ring" if hi else "each one waited for"),
        "made_at_most": sysctl_int("%s.dp.fast_make_per_sec" % OCTEP),
        "made_per": "second",
        "ipv6_refused": sysctl_int("%s.dp.flow_v6_refused" % OCTEP),
    }


# ---- the ARMADA driver (npuep): GIU rings, giu/flow/ipsec counters, npup ports ---------------------

NPUEP = "dev.npuep.0"
# The gate word offload.sh last had this coprocessor accept, kept there with the boot it belongs
# to, because the ARMADA cannot be asked for it. Not /var/run: the boot hook writes the gate before
# cleanvar empties that directory, so a file there was gone by the time the page asked.
NPUEP_FW_CFG_STATE = "/var/db/os-xgs-npu/offload-fw_cfg"
NPUEP_BOOT_TOKEN = "/var/run/xgs-npuep-boot"


def npuep_fw_cfg_written():
    """The fw_cfg word the coprocessor accepted since this boot, or None if none was written."""
    # The stamp is the token the start hook leaves in /var/run for this boot (emptied at boot, so
    # it is the boot's). Not kern.boottime, which moves by a second when the clock is stepped, and
    # not kern.boot_id, which this kernel does not have.
    try:
        with open(NPUEP_BOOT_TOKEN) as f:
            boot = f.read().strip()
        with open(NPUEP_FW_CFG_STATE) as f:
            stamp, word = f.read().split()
        if not boot or stamp != boot:
            return None		# written during an earlier boot, or before this boot's start hook
        return int(word, 0)
    except (OSError, ValueError):
        return None

NPUEP_DP_FIELDS = [
    ("giu.rx_packets", "frames taken off the receive rings"),
    ("giu.rx_bytes", "bytes received"),
    ("giu.tx_packets", "frames posted to the coprocessor"),
    ("giu.tx_bytes", "bytes posted"),
    ("giu.rx_dropped", "frames the receive path dropped"),
    ("giu.rx_bad", "malformed frames"),
    ("giu.rx_nobuf", "frames with no receive buffer"),
    ("flow.cand_pushed", "flow candidates seen in punted frames"),
    ("flow.auto_made", "flows the sweep accelerated on its own"),
    ("ipsec.offered", "IPsec associations the kernel offered to a front port"),
]


# The handshake word's bits, as contrib/npuep/npuep.c names them (CTRL_FCLT_*): the coprocessor
# sets 0 and 2, this host 1 and 3. Shown as words, as the XGS 3300's handshake is.
NPUEP_HS_BITS = [
    (0x1, "target initialised"),
    (0x2, "host initialised"),
    (0x4, "doorbells set up"),
    (0x8, "host alive"),
]


def npuep_handshake(word):
    if word is None:
        return None
    named = [text for bit, text in NPUEP_HS_BITS if word & bit]
    state = "completed" if word & 0xF == 0xF else "not complete"
    return "%s; %s (word 0x%x)" % (state, ", ".join(named) if named else "nothing set", word)


def npuep_report(out):
    heartbeats = sysctl_int("%s.heartbeats" % NPUEP)
    out["driver"] = {
        "description": sysctl("%s.%%desc" % NPUEP),
        # its PCIe relaxed ordering and no-snoop, which attach clears (#748)
        "pcie_order": sysctl("%s.pcie_order" % NPUEP),
        # The driver is attached whenever its tree is here; the coprocessor's liveness is the
        # heartbeat count, which this shows where the other family shows a host/target status word.
        "ready": 1,
        "host_status": None if heartbeats is None else "%d heartbeats" % heartbeats,
        "target_status": None,
    }
    out["handshake"] = {
        "state": npuep_handshake(sysctl_int("%s.handshake" % NPUEP)),
        "rings": first_line("%s.giu.rings" % NPUEP),
        "rings_in_use": sysctl("%s.giu.link" % NPUEP) and
            ("link up" if sysctl_int("%s.giu.link" % NPUEP) else "link down"),
        # Not SR-IOV on this family (an AGNIC endpoint), so there are no virtual functions.
        "vfs": None,
    }
    # The control channel is rpc_channel here, not rpc; its state line carries the ring tally.
    out["rpc"] = {
        "ready": sysctl_int("%s.rpc_channel.open" % NPUEP),
        # There is no write gate on this family - commands are posted directly - so nothing to show.
        "allow_write": None,
        "last": first_line("%s.rpc_channel.state" % NPUEP),
    }
    out["netagent"] = {
        "ready": sysctl_int("%s.nwa.ready" % NPUEP),
        "commands": sysctl_int("%s.nwa.commands" % NPUEP),
        "timeouts": sysctl_int("%s.nwa.timeouts" % NPUEP),
    }
    # The gate has no read-back on this family, so what is shown is the word offload.sh last had the
    # coprocessor accept since boot - the same meaning as the OCTEON TX's write-only fw_cfg - and
    # None when nothing has been written since boot. The flow table and the IPsec offers are on
    # their own pages; the revision barrier is here.
    fw_cfg = npuep_fw_cfg_written()
    out["offload"] = {
        "fw_cfg_requested": fw_cfg,
        "offload_bit_requested": None if fw_cfg is None else bool(fw_cfg & 0x1),
        "note": (
            "the ARMADA cannot be asked for the gate word: this is the one the coprocessor last "
            "accepted from offload.sh since boot, or nothing if it has not been written since boot. "
            "The flow table is on the Flow page and the IPsec offers on the IPsec page; the "
            "firewall revision is posted and held by the far side"
        ),
        "fw_rev_posted": sysctl_int("%s.flow.fw_rev" % NPUEP),
        "fw_rev_bumps": sysctl_int("%s.flow.fw_rev_bumps" % NPUEP),
        "fw_rev_bump_fail": sysctl_int("%s.flow.fw_rev_bump_fail" % NPUEP),
    }
    counters = {}
    for key, descr in NPUEP_DP_FIELDS:
        v = sysctl_int("%s.%s" % (NPUEP, key))
        if v is not None:
            counters[key] = {"value": v, "description": descr}
    out["datapath"] = counters
    out["ports"] = ports("npup", 1, 14)
    # The board's own figures, read with PLATFORM_READ, and what this host allocated of them.
    out["limits"] = {
        "platform": sysctl("%s.rpc_channel.platform" % NPUEP),
        "board_connections": sysctl_int("%s.rpc_channel.board_conn" % NPUEP),
        "board_next_hops": sysctl_int("%s.rpc_channel.board_nhop" % NPUEP),
        "board_associations": sysctl_int("%s.rpc_channel.board_sas" % NPUEP),
        "connections_max": sysctl_int("%s.flow.conn_max" % NPUEP),
        "connections_in_use": sysctl_int("%s.flow.conns" % NPUEP),
        "next_hops_max": sysctl_int("%s.flow.nhop_cap" % NPUEP),
        "next_hops_in_use": sysctl_int("%s.flow.nhops" % NPUEP),
        "transmit_queues": sysctl_int("%s.giu.txq" % NPUEP),
        # The board asks for a high-priority ring and this driver declares it, but posts nothing on
        # it yet: every command, the flow writes too, waits for its answer.
        "flow_writes": "each one waited for" if sysctl("%s.rpc_channel.platform" % NPUEP) is not None else None,
        "made_at_most": sysctl_int("%s.flow.make_per_sweep" % NPUEP),
        "made_per": "sweep",
        "ipv6_refused": sysctl_int("%s.flow.v6_refused" % NPUEP),
    }


RESET_REASON = "/var/db/os-xgs-npu/reset-reason.json"


def last_reset():
    """What the host's chipset recorded as the cause of the reset that started this boot, as the
    start hook 06-xgs-reset-reason kept it (scripts/xgs/reset_reason.py), with the boot's own time.
    None when nothing was kept for this boot: no such register on this host, or a hook that has not
    run since the plugin was installed."""
    try:
        with open(RESET_REASON, encoding="utf-8") as handle:
            kept = json.load(handle)
    except (OSError, ValueError):
        return None
    boot = re.search(r"sec = (\d+)", sysctl("kern.boottime") or "")
    if not boot or not isinstance(kept, dict) or abs(int(kept.get("boot") or 0) - int(boot.group(1))) > 5:
        return None
    return {
        "boot": int(boot.group(1)),
        "value": kept.get("value"),
        "bits": [b.get("bit") for b in kept.get("bits", []) if isinstance(b, dict)],
        "reasons": [b.get("reason") for b in kept.get("bits", []) if isinstance(b, dict)],
    }


SENSORS = "/usr/local/sbin/xgs3300-sensors"
STATUSLED = "/usr/local/sbin/xgs-statusled"
FANCTL_STATUS = "/var/db/fanctl/status.json"


def degrees(text):
    """A temperature sysctl as the driver gives it, "51.9C", as a number; None for a node that is
    absent or refused - the coprocessor's copies refuse a reading nobody fed."""
    m = re.match(r"\s*(-?\d+(?:\.\d+)?)C\s*$", text or "")
    return float(m.group(1)) if m else None


def fans_os_fanctl_read():
    """The fans os-fanctl last read, turning ones only, from its status file; [] when it is not
    installed, or its file is older than two minutes. On board 201 that chip is os-fanctl's: this
    reads no Super I/O of its own."""
    try:
        with open(FANCTL_STATUS, encoding="utf-8") as handle:
            doc = json.load(handle)
    except (OSError, ValueError):
        return []
    if time.time() - float(doc.get("at") or 0) > 120:
        return []
    out = []
    for fan in doc.get("fans") or []:
        if fan.get("state") != "turning" or not fan.get("rpm"):
            continue
        duty = fan.get("output") if fan.get("output") is not None else fan.get("duty")
        out.append({"name": fan.get("label") or fan.get("channel"), "rpm": fan.get("rpm"), "duty": duty})
    return out


def appliance(b):
    """The board's own health, for the Status tab's Appliance health card and rows (2026-10-28).

    XGS 3300: the CPLD's bank as tools/sensors/xgs3300-sensors reads it (the reader os-fanctl and
    temperatures.sh use, under 10 ms) - the coprocessor, the inlet, both fans with their under-speed
    flags, both power supplies, the reset flags, the CPLD's version - and the coprocessor's memory
    where its module has a sensor. Board 201 (XGS 126, 136): the coprocessor as its driver holds it,
    its fan as os-fanctl last read it, the board controller's reset flag. The host's processor on
    both. A part a board does not have, or nothing read, is left out."""
    out = {"temperatures": [], "fans": [], "power": []}
    cpu = degrees(sysctl("dev.amdtemp.0.core0.sensor0")) or degrees(sysctl("dev.cpu.0.temperature"))
    if cpu is not None:
        out["temperatures"].append({"name": "cpu", "celsius": cpu})
    if b.get("family") == "octeontx":
        try:
            run = subprocess.run([SENSORS, "json"], capture_output=True, text=True, timeout=5)
            bank = json.loads(run.stdout) if run.returncode == 0 else {}
        except (OSError, subprocess.SubprocessError, ValueError):
            bank = {}
        for key, name in (("npu_c", "coprocessor"), ("inlet_c", "inlet")):
            if bank.get(key) is not None:
                out["temperatures"].append({"name": name, "celsius": bank[key]})
        memory = degrees(sysctl("%s.temperature.memory" % OCTEP))
        if memory is not None:
            out["temperatures"].append({"name": "memory", "celsius": memory})
        for n in (0, 1):
            if bank.get("fan%d_rpm" % n) is not None:
                out["fans"].append({"number": n, "rpm": bank["fan%d_rpm" % n],
                                    "under": bank.get("fan%d_under" % n) == 1})
        if bank.get("psu_internal_good") is not None:
            out["power"] = [{"name": "internal", "good": bank["psu_internal_good"] == 1},
                            {"name": "external", "good": bank.get("psu_external_good") == 1}]
        if bank.get("reset_flag0") is not None:
            out["reset_button"] = bank["reset_flag0"] == 1
            out["reset_flag1"] = bank.get("reset_flag1") == 1
        out["cpld"] = bank.get("cpld")
    elif b.get("family") == "armada":
        npu = degrees(sysctl("%s.temperature" % NPUEP))
        if npu is not None:
            out["temperatures"].append({"name": "coprocessor", "celsius": npu})
        out["fans"] = fans_os_fanctl_read()
        if b.get("assembly") == "201":
            try:
                run = subprocess.run([STATUSLED, "reset-flag"], capture_output=True, text=True, timeout=5)
                word = run.stdout.split()[0] if run.returncode == 0 and run.stdout.split() else ""
            except (OSError, subprocess.SubprocessError):
                word = ""
            if word in ("set", "clear"):
                out["reset_button"] = word == "set"
    return out


def coprocessor_health():
    """What scripts/xgs/health.py last read of the coprocessor's own Linux, every five minutes."""
    try:
        with open("/var/db/os-xgs-npu/health.json", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return None


def heartbeat():
    """The driver's heartbeat with the fast path (docs/measurements/heartbeat-2026-10-22.md), either
    family's, or None on a driver without one. On the XGS 136 an answer is a beat taken off the queue."""
    for node in ("%s.heartbeat" % OCTEP, "%s.heartbeat" % NPUEP):
        state = sysctl(node + ".state")
        if state is None:
            continue
        hb = {"state": state}
        for key in ("on", "since_answer_ms", "sent", "answered", "deaths", "revivals", "rtt_us"):
            hb[key] = sysctl_int("%s.%s" % (node, key))
        return hb
    return None


def intrusion_detection(path=CONFIG):
    """Whether OPNsense's Intrusion Detection sees what the coprocessor carries. Suricata listens on
    this host, and a connection the flow maker hands to the coprocessor after its first data no
    longer crosses it: in IDS mode (pcap), or in IPS mode without "Inspect, then hardware" running -
    which reads oxips0 in Netmap mode only - Suricata sees how each connection starts and nothing
    after it. Until 2026-10-24 no page said so; the Overview and XGS: Settings say it when
    "uninspected" is true. Read from config.xml, as the settings stand; None if it cannot be read."""
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError):
        return None
    ids = root.find("OPNsense/IDS/general")
    xgs = root.find("OPNsense/XGS/general")

    def text(node, key, default):
        value = node.findtext(key) if node is not None else None
        return (value or default).strip()

    capture = text(ids, "mode", "pcap")
    names = []
    for key in [k for k in text(ids, "interfaces", "").split(",") if k]:
        names.append((root.findtext("interfaces/%s/descr" % key) or key.upper()).strip())
    running = any(sysctl_int(node) == 1 for node in ("%s.dp.ips_new" % OCTEP, "%s.flow.ips_new" % NPUEP))
    out = {
        "on": text(ids, "enabled", "0") == "1",
        "mode": "ids" if capture == "pcap" else "ips",
        "capture": capture,
        "interfaces": names,
        "offloading": text(xgs, "offload", "0") == "1" and text(xgs, "flow_auto", "0") == "1",
        "inspect_then_hardware": capture == "netmap" and text(xgs, "inspect_then_hardware", "0") == "1" and running,
    }
    out["uninspected"] = out["on"] and out["offloading"] and not out["inspect_then_hardware"]
    return out


CP_ZONES = "/var/db/os-xgs-npu/cp/zones.json"


def captive_portal(path=CP_ZONES):
    """Where each enabled Captive Portal zone runs, as cp_users.py last decided it (zones.json):
    {"setting", "overlay": {"on", "code"}, "zones": [{"name", "where", "reason", "clients"}], "age"};
    None before its first run."""
    try:
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, ValueError):
        return None
    overlay = data.get("overlay") or {}
    return {
        "setting": bool(data.get("setting")),
        "overlay": {"on": bool(overlay.get("on")), "code": overlay.get("code", "")},
        "zones": [{key: z.get(key) for key in ("name", "where", "reason", "clients")}
                  for z in data.get("zones", []) if isinstance(z, dict)],
        "age": max(0, int(time.time()) - int(data.get("run", 0))),
    }


def main():
    out = {"board": board(), "reset": last_reset()}

    if sysctl("%s.%%desc" % OCTEP) is not None:
        out["present"] = True
        octep_report(out)
    elif sysctl("%s.%%desc" % NPUEP) is not None:
        out["present"] = True
        npuep_report(out)
    else:
        out["present"] = False
        out["message"] = (
            "no coprocessor driver is attached - neither octep (OCTEON TX) nor npuep (ARMADA); "
            "the module did not load, or declined to attach"
        )

    out["health"] = coprocessor_health()
    out["appliance"] = appliance(out["board"])
    out["heartbeat"] = heartbeat()
    out["intrusion_detection"] = intrusion_detection()
    out["captive_portal"] = captive_portal()
    if out["intrusion_detection"] and not out["present"]:
        out["intrusion_detection"]["uninspected"] = False     # nothing is carried without a driver
    json.dump(out, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
