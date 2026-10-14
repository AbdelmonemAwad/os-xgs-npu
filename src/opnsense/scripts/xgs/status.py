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
import subprocess
import sys


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
    return {
        "assembly": parts[0] if parts else None,
        "family": parts[1] if len(parts) > 1 else None,
    }


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


# ---- the ARMADA driver (npuep): GIU rings, giu/flow/ipsec counters, npup ports ---------------------

NPUEP = "dev.npuep.0"

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


def npuep_report(out):
    heartbeats = sysctl_int("%s.heartbeats" % NPUEP)
    out["driver"] = {
        "description": sysctl("%s.%%desc" % NPUEP),
        # The driver is attached whenever its tree is here; the coprocessor's liveness is the
        # heartbeat count, which this shows where the other family shows a host/target status word.
        "ready": 1,
        "host_status": None if heartbeats is None else "%d heartbeats" % heartbeats,
        "target_status": None,
    }
    out["handshake"] = {
        "state": sysctl("%s.handshake" % NPUEP),
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
    out["offload"] = {
        # The offload gate is set by hand on this family and has no read-back, so it is not shown
        # here; the flow table and the IPsec offers are on their own pages. The revision barrier is.
        "fw_cfg_requested": None,
        "offload_bit_requested": None,
        "note": (
            "on the ARMADA the offload gate is set per flow and has no read-back; the flow table "
            "is on the Flow page and the IPsec offers on the IPsec page. The firewall revision is "
            "posted and held by the far side"
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


def main():
    out = {"board": board()}

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

    json.dump(out, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
