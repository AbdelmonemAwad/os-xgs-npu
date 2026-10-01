# OCTEON TX - the reference

What is true now, with no history. Every number here has been read from the appliance or from
the vendor's own source, and the reasoning behind each one - including what was tried and
withdrawn - is in [octeon-tx.md](octeon-tx.md), which is a log rather than a reference.

If you only read one page about this family, read this one.

## The board

| | |
|---|---|
| coprocessor | Cavium OCTEON TX CN8365, PCI `177d:a300`, eight VFs at `177d:a303` |
| host | AMD Ryzen Embedded V1780B, 8 threads, 16 GB |
| cores | 20, with `isolcpus=1-19`; 17 of them are fast-path workers |
| assembly | `AMDA0202-0004`, platform `xgs1us` |
| front ports | 12: `Port1`-`Port8` and `PortF3`/`PortF4` behind an 88E6193X, `PortF1`/`PortF2` direct to BGX2 |

## Bring-up, in order

1. **Load the driver.** It finds the barmap at `BAR1 + 0x02000000` and reports the four
   facilities: control, mgmt_netdev, nw_agent and rpc, at 32/33/34/35 MB with doorbells
   152/153/154/155.
2. **Complete the endpoint handshake.** Until it does, the NetAgent window is entirely blank and
   every request returns `ENXIO`. The handshake is also where the coprocessor reports its tick
   rate - 800 per microsecond on this board - and it runs once per coprocessor boot and must
   never be re-armed.
3. **Read the port table.** NetAgent operation `0x01` returns 13 records of 20 bytes, each
   beginning with `tag | flags`.
4. **Raise a port.** Operation `0x03`, attribute `0x00`, payload 1.
5. **Read its link.** Operation `0x04`, attribute `0x00`. Allow about two seconds - a 10G
   bring-up is not instantaneous and an immediate read-back reports failure when it means
6. **Name each panel port's own address to the switch.** Operation `0x03`, attribute `0x03`,
   addressed by the port's switch tag - `0x8100` for panel label 1 - carrying the six bytes
   operation `0x04` answers for the same tag. Until this is sent the switch's TCAM entry for that
   port has its octet mask at `0x00`, "Never Hit", and the port passes broadcast and nothing else.
7. **Bind the tag to a logical interface.** `rpc` command 5, `PPORT_UPDATE`, mapping the same tag
   to `switch port - 1`, which is the board file's `lifport`; then command 3, `LIF_ADD_UPDATE`,
   with that address, MTU 1500, forwarding mode 2 and mask 255.
8. **Give it a host interface.** `dp.if_port` then `dp.if_add`, both carrying the tag, and the
   interface comes up with the panel port's own address. A tag with no interface is counted in
   `dp.rx_untagged`.
   unfinished.

## The port tags

| tag | what | filter table |
|---|---|---|
| `0x0001`, `0x0002` | the two coprocessor 10G MACs - `PortF1` and `PortF2` | 65535 |
| `0x8000` | the switch's uplink to the coprocessor | 0 |
| `0x8100` .. `0x8a00` | switch ports 1 to 10 | 12 |

Bit 15 marks a switch port and bits 13:8 carry its number. The filter-table size is the quickest
way to tell the two kinds apart. Note that `0x0003` answers requests without appearing in the
published table, so an answer is not evidence that a port exists.

**This table is also the answer to the receive path, and it was published here before the question
was asked.** The switch's uplink runs in DSA frame mode, so every frame it hands the coprocessor
carries the source port in a four-byte tag, and the fast path turns that into exactly the value in
the third row: `0x8000 | (switch port << 8)`. This driver bound tags 1, 2 and 3 instead, for
months, while its own reference page said what the tags were. **Read the reference before probing
for what it already holds.**


## The NetAgent operations that work from the host

Exactly four. Every other opcode returns an ACK with status 1, because the target registers no
handler for it - that includes MDIO and GPIO, which the target implements internally but does
not expose on this hop.

| op | what |
|---|---|
| `0x01` | switch init - returns the port table |
| `0x03` | set a port attribute |
| `0x04` | get a port attribute |
| `0x45` | combined all-port info |

### Attributes worth knowing

| attribute | what it returns |
|---|---|
| `0x00` | **the link** on a coprocessor MAC tag, and it is what a SET writes |
| `0x04` | the **nominal** speed - 10000 even for a port never brought up |
| `0x0d` | duplex. Static; it does not follow the link |
| `0x0e` | 264 bytes of 64 counters, **none of which are populated on this path** |
| `0x01`, `0x09`, `0x50` | refused with status 1 |

## The datapath frame

The 64-byte instruction: `dptr@0 ih3@8 pki_ih3@16 rptr@24 irh@32 exhdr[3]@40`. `rptr` and `irh`
are written byte-swapped; the rest are not. The three `exhdr` words are never written.

| field | value |
|---|---|
| `ih3.pkind` | 40 |
| `ih3.fsz` | 28, which is `16 + 4 + 8` |
| `pki_ih3.sl` | **94**, which is `fsz + TOTAL_TAG_LEN` |
| `pki_ih3` | `w = 1`, `utt = 1`, `tagtype` ordered, `pm = 0`; `qpg` and `uqpg` left unset |
| `irh.opcode` | `0x1220` |
| `irh.rlenssz` | **81**, the checksum offset, which is `TOTAL_TAG_LEN + sizeof(ethhdr) + 1` |
| `irh.param` | the interface index, and 0 on a usfp appliance |
| `irh.dport` | never written |

And the frame at `dptr` is not a bare Ethernet frame. It carries a **66-byte private header**:

    [ 2B port tag, network order ][ 64B metadata ][ dst MAC ][ src MAC ] ...

The metadata is **not** a walking pattern from `0xc0`. Filling it that way sets byte 3, which the
fast path reads as an egress security-association handle, so every frame was routed to IPsec
encryption and dropped before the wire. The vendor's form is what this driver sends now.

`TOTAL_TAG_LEN` is 66 - `PPORT_HLEN` 2 plus `CUSTOM_META_TAG_LEN` 64 - and both ends name the
same split. The metadata is validated: the fast path counts what fails in
`FPCNTR_FROM_KN_DROP_MISMATCH_METADATA_FIELDS`.

## The output queue

Each descriptor is **two** 64-bit words: a buffer pointer and an info pointer. Both must be real
memory - the device DMAs the packet to one and a 16-byte response header and length to the other.

## Where this stands

Frames go out and frames come back, and the panel ports are FreeBSD interfaces. What is left is
engineering rather than discovery, which is a recent state and worth stating plainly.

The table is weighted so that a number can be argued with rather than taken on trust. Each row says
what it covers; the last column is how many of the hundred points the row is still holding back,
which is a more useful ordering than the percentage itself.

| | component | done | weight | still missing |
|---|---|---|---|---|
| 1 | the receive rate - works; the limit is now this driver's own servicing | 85% | 20 | **2.91** |
| 2 | network interfaces - twelve, real addresses, assigned in OPNsense | 80% | 12 | **2.33** |
| 3 | all twelve front ports - bound, addressed, presented | 100% | 8 | - |
| 4 | interrupts - MSI-X, one vector per ring | 100% | 6 | - |
| 5 | performance - sufficient on the ten 1G ports, 65% of line rate on the two 10G | 40% | 7 | **4.08** |
| 6 | persistence - boot hook, bring-up, and a kernel stamp that is enforced | 85% | 5 | 0.73 |
| 7 | resilience - recovery without a host reboot, soak testing | 30% | 4 | **2.72** |
| 8 | protocol and hardware understanding | 99% | 15 | 0.15 |
| 9 | the control plane | 100% | 10 | - |
| 10 | the outbound datapath - confirmed against a machine off the appliance | 100% | 10 | - |
| 11 | the rest of the appliance - see below | 67% | 6 | **1.92** |

Weights sum to 103. **Weighted: 85%.**

**Four rows are finished** and they carry 34 of the weight between them. Row five used to read 0%
and hold back nearly seven points; measuring it is what changed that, because **for ten of the
twelve front ports the driver has six and a half times the headroom it needs** and only the two 10G
cages are capped. What is left of it is real but it is now bounded, and it is the same work as row
one: a copy per frame and one queue per direction.

Row 11 is itemised, because an average over six unequal things is the kind of number that deserves
to be shown rather than asserted:

| the rest of the appliance | |
|---|---|
| the CPLD, reached and driven | 100% |
| the SFP cages - `tx_disable` at the CPLD, and the SERDES after every insertion | 100% |
| the panel LEDs behind the switch | 100% |
| the front panel - protocol read out of the vendor's daemon, tool written, **not yet seen** | 50% |
| the thermal sensors and fans - mapped, and three roads to them closed | 0% |
| the fail-to-wire relay - readable and read, deliberately not driven | 50% |

**And one thing on this appliance cannot be done at all**: PortF1 and PortF2 have **no LED key in
the board file**, so they have no indicator by design rather than by omission. It is not counted
against anything.

## What it carries, in one table

Taken on one XGS 3300 - **the readings, the method and what each one does not mean are in
[docs/measurements/xgs3300.md](../measurements/xgs3300.md)**, because a measurement belongs to a
board rather than to a family.

| | |
|---|---|
| round trip over a copper panel port | 0.193 ms average, **0% loss** over 30,000 full-size frames |
| what the wire takes | **1,023 Mbit/s**, which is line rate for a gigabit port |
| what the driver accepts | **535,142 pps** |
| what a 1G port needs | 81,486 pps - **6.5x headroom** |
| what a 10G port needs | 814,863 pps - **65% of line rate** |

**So for ten of the twelve front ports this driver is nowhere near the bottleneck**, and only the
two 10G SFP+ cages are capped. That is what row five above is now about, and it is a bounded
problem rather than an open one.

**Two cautions carried here because they change how a reading is read.** `Opkts` counts what the
driver accepted and posted, not what left the port - in the ceiling run it claimed 6,432 Mbit/s out
of a gigabit port, which was the 84% the coprocessor dropped being counted as sent. And the
received byte counter was doubled until #171, because the stack adds it too unless a driver claims
`IFCAP_HWSTATS`.


## What does not work

**Performance work has not been started.** Every frame is copied, there is one queue per direction
per interface, and there is no offload of any kind - no checksum, no TSO, no LRO, no distribution
across queues. This is the largest single gap and it is not a fault; it is work nobody has done.

**Recovery needs a host reboot.** The datapath attaches once per coprocessor boot, so unloading the
module takes the ports with it and a reload cannot be answered - a host reboot is what restarts the
coprocessor. Nothing has been soak tested for hours of continuous traffic either.

**The module is built on the appliance** against the running kernel's headers, because OPNsense
ships no kernel sources. That is forced rather than chosen. A stale one is no longer loaded
silently: `build.sh` stamps it with the kernel it was built against, and the bring-up refuses to
load a mismatched module when none is running and warns when one is.

A working **vendor** host side is three modules: `octnic` creates `oct0`, `mv_nwa_host` calls
`register_pport_device` once per tag from the NetAgent port list, and `pport` creates a virtual
netdev per front port over `oct0`. **None of that reaches the coprocessor** - all three register
with the host's own pport layer and send nothing - so the gap was never a registration handshake.
What tells the coprocessor's fast path to hand a received frame to the host is the `rpc` facility:
`PPORT_UPDATE` maps a port tag to an interface and `LIF_ADD_UPDATE` installs the LIF. Promiscuous
mode was tried, is accepted with status 0, and is not the gate.


## The channel that programs the fast path

The wire format of the `rpc` facility - the rings, the descriptors, the command numbers and the LIF
entry the wire-to-host gate consults - is in [octeon-tx-rpc.md](octeon-tx-rpc.md), read out of the
coprocessor's own `usfp_rh.ko` at the release this board runs.

## Registers whose reads are not what they look like

| register | what it does |
|---|---|
| `R_OUT_CNTS` `0x10100` | reads `0x2000000000000000` on an idle ring - flags in the upper half, not a count |
| `R_OUT_SLIST_RSIZE` | reads back 16 after being written 0; 256 sticks |
| `R_IN_INSTR_DBELL` | low half is the outstanding count; a field at bit 38 accumulates |

`R_OUT_PKT_CNT` `0x10180` and `R_OUT_BYTE_CNT` `0x10190` were checked against the vendor's own
`cn83xx_pf_regs.h` and are correct, so a zero there is a true zero.

**Never read BAR1 entry 15.** It is the GICD window and reading it hangs the appliance hard.
