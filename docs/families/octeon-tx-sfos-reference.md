# OCTEON TX: what the vendor's own system does

Everything on this page was read off an XGS 3300 (AMDA0202-0004) running **SFOS 22.0.2
MR-2-Build546** on 2026-09-29, while the vendor's datapath was carrying live traffic. It is the
reference measurement the driver work had been missing: until now every fact about the
coprocessor-to-host direction came from disassembly, and none of it had been observed.

MAC addresses, serial numbers and the unit hostname are replaced with placeholders throughout.

## How the appliance was reached

Public-key SSH **does work**, contradicting what this project previously recorded. An `ssh` to the
`admin` account with a key lands in the SFOS menu, not a shell; feeding the menu `5` (Device
Management) then `3` (Advanced Shell) gives a root shell, so a whole command list can be piped in:

    { printf '5\n3\n'; cat commands.sh; printf '\nexit\n'; } | ssh -tt -i <key> admin@<ip>

`internal-sftp` is enabled for the same key, so bulk files come off with plain `sftp` - no FTP
receiver, no console. Both channels are far faster than the serial console and neither needs a
password.

When cutting a marker out of the output, never wait on a string that appears in the command you
sent: the shell echoes it back. Split the marker with an empty quoted string so that only the real
output carries the literal.

## The host-side topology

One PCI device, two functions: `177d:a300` (PF) and `177d:a303` (VF).

| netdev | driver | what it is |
|---|---|---|
| `oct0` | `OCTNIC` | the SDP transport itself. MTU 10000, promiscuous, **8 tx and 8 rx queues** |
| `Port1`-`Port8`, `PortF1`-`PortF4`, `pport_l0`, `pport_l0s0p0` | `mv_pport`, link kind **`pport`** | children of `oct0`, one per front port |
| `mvmgmt0` | `mgmt_net`, virtual | a **separate** host-coprocessor network, MTU 9600, IPv6 link-local only |
| `PortMGMT` | `igb` | the host's own management NIC, nothing to do with the coprocessor |

Host modules: `octeon_drv`, `octnic`, `mv_pport`, `mv_nwa_host`, `mgmt_net`, `usfp_firewall`.
Coprocessor modules: `pcie_ep`, `usfp_rh`, `mv_nwa_target`, `mgmt_net`, `dpi_dma`, `octeontx2_npa`,
`mvmdio_uio`, `octeontx`, `octeontx_zip`.

### `mvmgmt0` is a second, working coprocessor-to-host path

`xgs-ssh.sh` is four lines: it runs `ssh` to an IPv6 link-local address on `mvmgmt0` with a key from
`/opt/sophos/keys/`. So the coprocessor reaches the host and the host reaches the coprocessor over an
ordinary IPv6 link-local network with **no console involved at all**. Any claim that a silent
`uart2` blocks coprocessor-side measurement is wrong while this link is up.

## The private header, observed rather than inferred

A raw capture on `oct0` shows the header this project reconstructed from binaries, and confirms it
byte for byte:

    82 00                                       2-byte pport tag
    01 <63 more bytes>                          64 bytes of metadata, byte 0 = 1
    <dst mac> <src mac> <ethertype> ...         the real frame, at offset 66

**Metadata byte 0 is 1** in every frame, confirming what `mrvl_cst_set_tx_meta` said. The remaining
63 bytes are **not cleared** - they carry stale content from whatever used the buffer before, which
is why their value never mattered.

### The tag is the port number in the FIRST byte

The `nhop` table prints the same field, and settles the encoding:

    PPort_tag: 0x8100   IFACE_ID: 0
    PPort_tag: 0x8200   IFACE_ID: 1

so **`tag = 0x8000 | ((iface_id + 1) << 8)`**, and on the wire the port byte comes first. A frame for
the first front port begins `81 00`, not `00 81`. Code that writes a 16-bit tag as
`d[0] = tag >> 8; d[1] = tag & 0xff` is correct **only if it is handed 0x8100**, not 1 and not 0x0081.

### Inbound frames never appear on `oct0`

A capture restricted to the inbound direction took **0 packets while 110 passed the filter
outbound**, on an interface whose `/proc/net/dev` row shows tens of thousands of received packets.
The receive path strips the header and re-delivers on the `PortN` child, so no tap on `oct0` ever
sees an inbound frame. A capture on `oct0` that shows nothing arriving is not evidence that nothing
arrives.

## The ring configuration, which is the real difference

`/proc/Octeon0/status` reports **Input Queues: 8, Output Queues: 8**, and `/proc/Octeon0/stats`
shows the coprocessor spreading received packets across every one of them:

    DROQ    0      1      2      3      4       5       6      7
    pkts  1358   2325   4788   1652   2484   16507   32043   2693

DROQ 0 takes about 2% of the traffic. A host that publishes one output ring is not a host with a
small share of the traffic - it is a host that has not published what the coprocessor was told to
expect. The fast path picks its host queue as `crc32c(tuple) % num_sp_txqs`, and `num_sp_txqs` is
**8** for this assembly.

The live hardware rings are **8 through 15**, not 0 through 7. Rings 0-7 are programmed but idle -
their packet and byte counters are zero - while rings 8-15 carry every frame. Ring 8's output block
reads:

| offset | register | value |
|---|---|---|
| `+0x100` | `OUT_CNTS` | live |
| `+0x110` | `OUT_INT_LEVELS` | `0x0000005600000008` - packet threshold **8** |
| `+0x120` | `OUT_SLIST_BADDR` | a distinct address per ring |
| `+0x130` | `OUT_SLIST_RSIZE` | `0x1000` = **4096** |
| `+0x140` | `OUT_SLIST_DBELL` | live |
| `+0x150` | `OUT_CONTROL` | **`0x0000001004000642`** |
| `+0x160` | `OUT_ENABLE` | `1` |

and the input block uses `IN_INSTR_RSIZE` `0x800` = 2048. The idle rings 0-7 differ: they share one
base address, their `OUT_INT_LEVELS` is `0x003fffff00000000` and their doorbell sits at `0x800`.

## The working LIF table

Twelve LIFs, `LIF ID = iface_id << 12`, one per front port, in `xgs-ports -f` order:

    iface_id  0..7  -> Port1..Port8
    iface_id  8,9   -> PortF3, PortF4
    iface_id 10,11  -> PortF1, PortF2

Every one is `FWD mode: L3 FWD`, `Offload disabled: 0`, `DF enabled: 0`, MTU 1500. `Admin disabled`
tracks the link exactly: 0 for the ports that are up, 1 for the ports that are down.

**`Representor PID MLB` is 0 on eleven of the twelve** and 4095 only on `iface_id 0`. Both a LIF with
0 and a LIF with 4095 were passing traffic at the time, so this field is not the gate - which
independently confirms the negative result already published for it.

## Where the tables actually live

They are on the **host**, at `/sys/kernel/debug/usfp/table/`: `conn`, `lif`, `luid`, `mflow`, `nhop`,
`platform_info`, `sa`, `worker_dbg_cnt`, `worker_dragonfly_cnt`, `worker_port_cnt`, `worker_sys_cnt`.

**`usfp_table_print.sh` does not exist anywhere in this v22 build**, and the coprocessor has no
`/sys/kernel/debug/usfp` at all. Plans that depend on running that script on the coprocessor cannot
work. The debugfs files are read-only and print **only non-zero counters**, so there is no
description mode and no way to clear an array by accident.

`platform_info` reads:

    Platform Name : XGS_1US        Num PFs      : 1       Num VFs  : 8
    Proc_cores    : 20             Num workers  : 17      RPC rings: 1
    Max interfaces: 128            Max conn     : 2000000 Max nhop : 65536
    HW capabilities (bitmap) : 0x1      (= CKSUM_VF)

## A control surface nobody had found

`/sys/kernel/usfp_firewall/control/` is writable and holds the knobs the host uses to configure the
datapath directly, beside the RPC facility:

    set_netdev_lif_port     bind a netdev to a LIF and a port
    setup_netdev_vlans      the VLAN side of the same
    table_debug_access      reach the tables
    fp_pkt_dump             make the fast path dump packets
    offload  ips  fintrack  tcp_seq_chk  verbose_log  inject_recovery
    set_fw_state_rev (7)    set_l3_fwd_state_rev (25)    clear_dbg_cntrs

## The RPC facility is wider than we had mapped

`/sys/kernel/debug/rpc_provider/stats` counts every command as it is used. Commands seen live:

    0, 1, 2, 3, 5, 6, 36, 37, 39, 41, 43, 44, 45

Seven of those - **0, 1, 2, 6, 39, 41, 43** - had never been identified. `cmd-37` (LIF_READ)
dominates at several thousand calls. The provider has **five doorbell rings**, of which 0 and 1 are
in use (5885 and 77 doorbells), with `ring_full`, `ring_timeout` and `hol_wait` all zero.

`state` reads `cfg_magic=0xd7d3ab00 cfg_revision=9211 active_hi_rings=1 reconfig_done=0`.

## The counters, with everything punting to the host

    FPCNTR_RX_WIRE                          63700
    FPCNTR_TX_KN                            63700      <- every wire frame goes to the host
    FPCNTR_RX_KN                            23749
    FPCNTR_TX_WIRE                          23398
    FPCNTR_FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE 53492
    FPCNTR_FROM_WIRE_TO_KN_NON_ACCEL        10208      <- 53492 + 10208 = 63700
    FPCNTR_FROM_KN_PROC_CMSG                  351
    FPCNTR_FROM_KN_TO_WIRE                  23398

Per-port counters exist only for **port 000** - the index is the coprocessor's own DPDK ethdev, not
the front panel port, so `PORT_000_PORT_CNT_RX` is the whole wire side.

### The control channel is not one-way

`worker_dbg_cnt` carries **`WORKER_DEBUG_CNT_CMSG_SENT : 351`**, exactly matching
`FROM_KN_PROC_CMSG : 351`. The fast path does send control messages, one for each it processes.
This project published the opposite, and that page is corrected in the same change as this one.

## The coprocessor's own published addresses

`/sys/kernel/nwa_pcie_addr/` on the coprocessor holds two values, 16 KB apart:

    addr       0x1cca00000
    cntr_addr  0x1cc9fc000

and `/sys/kernel/debug/rpc_handler/stats` is the far end of the host's `rpc_provider`.

## The SoC port and switch inventory, from the coprocessor

`/sys/kernel/nwa_ports_info/` reports **3** SoC ports as `<qlm> <lane> <num_lanes> <type> <switch>`:

    port 0 : qlm 4, lane 0, 1 lane, type 2, switch 0     -> the 88E6193X
    port 1 : qlm 5, lane 0, 1 lane, type 1, switch 255   -> a direct SFP+ cage
    port 2 : qlm 6, lane 0, 1 lane, type 1, switch 255   -> the other cage

`/sys/kernel/nwa_switch_info/` reports **1** switch with **10** ports. That is the whole twelve-port
front panel: ten through the switch, two direct.

## What this changes

1. The host must publish **eight** output rings, on hardware rings 8-15, with `RSIZE` 4096 and
   `OUT_CONTROL 0x1004000642` - not one ring on ring 0.
2. The pport tag is `0x8000 | ((iface_id + 1) << 8)`, port byte first.
3. `usfp_table_print.sh` is not part of any plan; the host's debugfs is.
4. Public-key SSH and `sftp` work, so the console is not the channel for any of this.
