#!/usr/local/bin/python3
"""
Report the coprocessor's state as JSON, for the status page and for anyone at a shell.

WHY A SCRIPT AND NOT PHP. Everything here is a sysctl read, and the sysctl tree is the driver's
own interface - it is what has been used to measure this hardware from the first day. Reading it
here keeps one statement of what a field means instead of two, and leaves the web layer with
nothing to do but draw.

Read-only. It posts no RPC command, sets no sysctl and touches no file; a status page that could
change the state of a firewall's datapath would be a poor trade for a table of numbers.
"""

import json
import subprocess
import sys

DEV = "dev.octep.0"

# The counters worth showing, and what they mean in a sentence. The full 182 are a different tool -
# tools/octep-fpcnt.sh - because reading them costs an RPC round trip to the coprocessor.
DP_FIELDS = [
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


def ports():
    """
    Every front port, with the link state the switch actually reports.

    ethtool-style speed on a pport is fabricated by the far side - it answers 1000M link-up for an
    unplugged cage - so what is reported here is ifconfig's own status and media, which come from
    the driver's link poll and not from the coprocessor's guess.
    """
    rows = []
    for n in range(0, 12):
        name = "oxp%d" % n
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


def last_command():
    """
    The last RPC command and how it ended, without the reply body.

    rpc.last prints the command, the result line, and then the payload - up to 182 words for a
    counter read. Everything from the payload on is dropped, so this stays one line whatever was
    last asked.
    """
    raw = sysctl("%s.rpc.last" % DEV)
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


def main():
    present = sysctl("%s.%%desc" % DEV) is not None

    out = {
        "present": present,
        "board": board(),
    }

    if not present:
        out["message"] = (
            "the octep driver is not attached; on this board that is either a module that did "
            "not load or an appliance of the other family"
        )
        json.dump(out, sys.stdout, indent=2)
        sys.stdout.write("\n")
        return 0

    out["driver"] = {
        "description": sysctl("%s.%%desc" % DEV),
        "ready": sysctl_int("%s.ready" % DEV),
        "host_status": sysctl("%s.host_status" % DEV),
        "target_status": sysctl("%s.target_status" % DEV),
    }

    # The handshake is the gate everything else sits behind: no ring, no command and no frame
    # moves until the target has answered it.
    #
    # sdp.rings prints a row per ring - sixty-four of them - which is a diagnostic and not a
    # status. Only its first line and its own closing summary are kept here; anyone who wants the
    # table reads the sysctl.
    rings_raw = (sysctl("%s.sdp.rings" % DEV) or "").strip().splitlines()
    out["handshake"] = {
        "state": sysctl("%s.sdp.hs_state" % DEV),
        "rings": " ".join(rings_raw[0].split()) if rings_raw else None,
        "rings_in_use": " ".join(rings_raw[-1].split()) if len(rings_raw) > 1 else None,
        "vfs": sysctl_int("%s.sdp.nvfs" % DEV),
    }

    # The two control channels. RPC carries the tables - interfaces, associations, counters - and
    # NetAgent carries the front ports' own attributes. Only NetAgent keeps a command tally; for
    # RPC the useful thing is the last command's result, which is a sentence.
    out["rpc"] = {
        "ready": sysctl_int("%s.rpc.ready" % DEV),
        "allow_write": sysctl_int("%s.rpc.allow_write" % DEV),
        # rpc.last carries the reply's whole payload after the result line, which for a counter
        # read is 182 words. The result is the status; the payload is a different tool's job.
        "last": last_command(),
    }

    out["netagent"] = {
        "ready": sysctl_int("%s.nwa.ready" % DEV),
        "commands": sysctl_int("%s.nwa.commands" % DEV),
        "timeouts": sysctl_int("%s.nwa.timeouts" % DEV),
    }

    # The firewall configuration word. There is no command that reads it back, so what is shown
    # is what this host last asked for - which is stated plainly rather than dressed as a reading.
    fw_cfg = sysctl_int("%s.rpc.fw_cfg" % DEV)
    out["offload"] = {
        "fw_cfg_requested": fw_cfg,
        "offload_bit_requested": None if fw_cfg is None else bool(fw_cfg & 0x1),
        "note": (
            "fw_cfg is write-only on this coprocessor: this is the value the host last asked "
            "for, not a reading of the far side"
        ),
    }

    counters = {}
    for key, descr in DP_FIELDS:
        v = sysctl_int("%s.dp.%s" % (DEV, key))
        if v is not None:
            counters[key] = {"value": v, "description": descr}
    out["datapath"] = counters

    out["ports"] = ports()

    json.dump(out, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
