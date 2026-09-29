# Capturing the XGS 3300 while it runs its own firmware

**Read this before switching the XGS 3300 back to its own firmware.** Switching is expensive, so this
is written so that one trip is enough. There is a script that does the whole thing:

    tools/sfos-capture.sh

It only reads. It never passes `C` to `usfp_table_print.sh`, which clears a counter array; it does not
run `xgs-ftw`, `xgs-led*`, `xgs-eeprom -w`, `xgs-cpld <reg> <value>` or `fw_setenv`; it loads and
unloads nothing and brings no interface up or down. The single exception is section 9, a traffic
generator that adds an address and a neighbour entry to PortF1 and removes both afterwards - it is
skipped unless `RUN_TRAFFIC=1` is set, and it is the reason section 2a is worth anything.

    sh sfos-capture.sh 2>&1 | tee /tmp/sfos-capture.txt
    RUN_TRAFFIC=1 sh sfos-capture.sh 2>&1 | tee /tmp/sfos-capture-load.txt   # second pass, with load

Get the files off with `xgs-scp-from.sh`, or let the box `ftpput` them out - SFOS key auth is
structurally impossible, so do not try to scp *in*.

---

## The one thing that matters most

**A packet capture on `oct0` while a front port receives traffic.**

    tcpdump -i oct0 -s 0 -c 20 -xx -nn
    tcpdump -i oct0 -s 0 -c 20 -w /tmp/oct0.pcap

Every byte of the host-to-coprocessor direction was worked out from binaries: the two-byte port tag,
the sixty-four metadata bytes, `meta[0] = 1`, the EtherType `0xEFEF` on tag 254, the sixty-six byte
private header. **Not one byte of the coprocessor-to-host direction has ever been observed.** It is
entirely inferred, and a driver has been written against that inference for weeks.

Twenty frames with `-xx` settles it. Run the traffic generator in another shell at the same time, or
the capture has nothing in it.

If only one command from this whole document can be run, run that one.

---

## Why the trip is needed at all

The datapath works in every direction except the last hop. Frames leave PortF1 and arrive on PortF2,
counted on the fast path's own per-port counters in both directions; the frame matches a LIF; the
fast path raises the counter for handing it to the host. **Nothing is ever written into the host's
output ring.** All 256 buffers and all 256 info blocks are still the poison byte after fifty frames -
410,112 bytes compared, not one changed.

Eight things have been tried against it and all eight are negative: the post-IOQ scratch announce, the
five PF interrupt enables, the ring number, the published VF topology, publishing addresses the way
the management facility does, the LIF's representor field, the metadata, and the connection tuple the
far side hashes. Each was grounded in the vendor's own binary and each produced nothing.

What is left cannot be settled from the binaries, because the binaries describe intent and the
question is about behaviour. That is what this capture is for.

---

## The list, in order of what it settles

### 1. The last hop

| what | why it earns the trip |
|---|---|
| `tcpdump -i oct0 -xx` and a `.pcap`, under load | above. The only unobserved direction |
| `tcpdump -i Port1 -xx` under the same load | shows what `mv_pport` strips, so the two can be differenced |
| every file under `/sys/module/{octeon_drv,slipf,octnic,mv_pport,usfp_firewall}/parameters/` | `pci_port` is what the fast path reads to decide its host port count; the rest have never been listed |
| `/proc/interrupts`, before and after load | whether the SDP path raises an interrupt at all, and how many |
| `ethtool -S oct0`, `ethtool -g oct0`, `ethtool -l oct0` | the host NIC's own receive counters and ring geometry |
| `ps` and the `usfp` command line from `/proc/*/cmdline` | confirms `-t 8`, `nr_port`, `pci_port` as actually run on this board |
| anything under `/sys/kernel/debug/octeon*` or `/proc/octeon*` | the vendor driver may expose a register dump; a working OQ's `RSIZE`, `DBELL`, `CONTROL`, `CNTS` is worth a great deal |

### 2. The counters, which every previous capture truncated

**Every `worker_sys_cnt` capture in the inventory was cut with `head`.** The whole
`FPCNTR_FROM_WIRE_DROP_LIF_*` family - the five counters that gate the wire-to-host direction - has
never been on screen. And the `D` flag, which prints the vendor's own description of every counter,
has never been used once.

    usfp_table_print.sh worker_sys_cnt D        # descriptions. Never run.
    usfp_table_print.sh worker_sys_cnt I        # including zeros, so the shape shows
    usfp_table_print.sh worker_port_cnt D
    usfp_table_print.sh worker_port_cnt I       # confirms the 256 stride worked out from the binary
    usfp_table_print.sh worker_dbg_cnt D
    usfp_table_print.sh worker_dbg_cnt I
    usfp_table_print.sh worker_dragonfly_cnt I
    usfp_table_print.sh platform_info

**Never pass `C`.** It clears the array.

### 3. The state tables, complete

    usfp_table_print.sh lif I
    usfp_table_print.sh nhop I
    usfp_table_print.sh conn I
    usfp_table_print.sh mflow I
    usfp_table_print.sh luid I
    usfp_table_print.sh sa I

The LIF dump already answered what a LIF looks like; `nhop`, `conn` and `mflow` have only ever been
seen empty or with one entry, and `luid` and `sa` never at all.

### 4. The mappings nobody dumped

    xgs-ports -b       # MAC with label - the only one ever run
    xgs-ports -f       # MAC with LIFPORT - never run, and it is the port-to-interface map
    xgs-ports -d       # devnames with labels
    xgs-ports -g       # grand ridge index with lifport and PCIe id

plus every file under `/sys/kernel/usfp_firewall/control/`, `/sys/kernel/nwa_ports_info/` and
`/sys/kernel/nwa_pports/`. The first of those carries `set_netdev_lif_port`, which the boot script
feeds from `xgs-ports -f` - that is the host telling the fast path which netdev is which interface,
and it has never been seen.

### 5. The peripheral channel

The CPLD's bus is named in the key store as `spi:0:1:3`, in the coprocessor's namespace, and it
carries the sensors, the SFP cage pins and the fail-to-wire relay. Five roadmap items are one channel.

- **the CPLD swept to `0x7f`**, not `0x5f`. Registers `0x30`, `0x31` and `0x38`-`0x3c` were found by
  a second sweep nobody read; nothing has looked above `0x5f` at all.
- **is there an `spi` device, and on which side** - `/dev/spidev*`, `/sys/bus/spi/devices/`,
  `/sys/class/spi_master/`, on the host **and** on the coprocessor. This settles whether the CPLD is
  reachable from a non-vendor host at all.
- `xgs-1us-sensors -a`, `xgs-dt-sensors`, `xgs-phy-temperature`, and `xgs-nct` with no arguments so
  its refusal is on the record.
- **`xgs-sff-event` and `xgs-sff-mq`**, which exist only on the coprocessor and have never been run.
  Ideally across a module reseat, which is what would settle what `cpld:0x38`'s bits mean: read,
  pull a module, read, replace, read.
- **the switch's registers beyond 0 and 3** - `21`, `22`, `26`, `27` per port, which carry the
  per-port control and status.
- **the 88X5113 at `mdio45:0:7`**, slices 0 and 2, more registers than the eight already read.

### 6. Structure that earlier captures cut off

- `lspci -vvv -s 01:00.0` **in full** - the SR-IOV capability body was cut by `head -45`, so First VF
  Offset, VF Stride, TotalVFs and NumVFs are all unrecorded.
- `/proc/iomem` in full on both sides - the coprocessor's was cut at `head -24`, and the eleven
  vfio-bound functions it showed were a floor, not a total.
- `lsmod` and `dmesg` in full on both sides.

---

## What to do with the result

Keep it beside the existing capture as a sibling, not merged into it: the two are different runs,
and the value of the first is that it is what the appliance looked like on one particular day. The
captures themselves are held outside this repository, because they identify one physical unit.

The rule that goes with them applies to the new one from the moment it lands: **read it before every
step, and again when stuck.** The existing capture has three times answered a question nobody asked
it, and twice shown that a page in this repository was wrong.

## Two habits that cost real time, so the new capture should avoid them

- **`head` truncated something valuable in at least four files.** Print tables whole and let the file
  be large.
- **A command echo is not output.** Several captures record a tool's usage message because the
  invocation was wrong, and the usage message was then read as data. Check each section has real
  output before leaving the box.
