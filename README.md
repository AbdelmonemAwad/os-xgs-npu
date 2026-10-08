# os-xgs-npu

<p align="center">
  <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/checks.yml"><img alt="checks" src="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/checks.yml/badge.svg"></a>
  <a href="#"><img alt="status: experimental" src="https://img.shields.io/badge/status-EXPERIMENTAL-critical.svg?style=flat-square"></a>
  <a href="LICENSE"><img alt="licence: BSD-2-Clause" src="https://img.shields.io/badge/licence-BSD--2--Clause-blue.svg?style=flat-square"></a>
</p>

<p align="center">
  <a href="https://opnsense.org/"><img alt="OPNsense 26.7" src="https://img.shields.io/badge/OPNsense-26.7-d94f00.svg?style=flat-square"></a>
  <a href="https://www.freebsd.org/"><img alt="FreeBSD 15.1-RELEASE-p1" src="https://img.shields.io/badge/FreeBSD-15.1--RELEASE--p1-ab2b28.svg?style=flat-square"></a>
  <a href="compat.json"><img alt="kernel 26.7-n283674" src="https://img.shields.io/badge/kernel-26.7--n283674-6e5494.svg?style=flat-square"></a>
</p>

<p align="center">
  <a href="#-what-works"><img alt="XGS 136: 14 of 14 front ports" src="https://img.shields.io/badge/XGS%20136%20(AMDA0201)-14%2F14%20front%20ports-brightgreen.svg?style=flat-square"></a>
  <a href="docs/families/octeon-tx.md"><img alt="XGS 3300: 12 of 12 front ports" src="https://img.shields.io/badge/XGS%203300%20(AMDA0202)-12%2F12%20front%20ports-brightgreen.svg?style=flat-square"></a>
</p>

<p align="center">
  <a href="#-families"><img alt="families: 2 of 6 with hardware" src="https://img.shields.io/badge/families-2%20of%206%20with%20hardware-lightgrey.svg?style=flat-square"></a>
  <a href="docs/families/README.md"><img alt="platforms: U-Boot for 9" src="https://img.shields.io/badge/platforms-U--Boot%20for%20all%209-lightgrey.svg?style=flat-square"></a>
  <a href="contrib/"><img alt="drivers: npuep and octep" src="https://img.shields.io/badge/drivers-npuep%20%2B%20octep-lightgrey.svg?style=flat-square"></a>
  <a href="CONTRIBUTING.md"><img alt="kernel modules are built on the appliance" src="https://img.shields.io/badge/kernel%20modules-built%20on%20the%20appliance-important.svg?style=flat-square"></a>
</p>

<table>
<tr><td>

<p align="center">
  <img alt="Background - months of work, recently opened" src="https://img.shields.io/badge/%E2%97%86-B%20A%20C%20K%20G%20R%20O%20U%20N%20D-informational?style=for-the-badge&labelColor=1f3a5f">
  <br>
  <strong>MONTHS OF WORK &nbsp;&mdash;&nbsp; RECENTLY OPENED &nbsp;&mdash;&nbsp; MORE TO FOLLOW</strong>
</p>

**This repository is younger than the work in it.** The reverse engineering, the measurements and
the failed attempts behind these drivers ran for months on real appliances before any of it was
written in public. One of the faults it closes had been open for eighteen of them.

**Publishing and documenting began recently, and deliberately.** Nothing here is claimed that has
not been run on the hardware, which is why the dead ends are recorded beside the results rather than
quietly dropped &mdash; see [docs/the-road.md](docs/the-road.md) for the shape of it, and
[docs/measurements/](docs/measurements/) for every number with how it was taken.

**Several related projects belong to the same effort** and are not here yet. They will be published
in full, on the same terms: measured first, documented as they are, with what did not work kept
beside what did.

</td></tr>
</table>

<table>
<tr><td>

<p align="center">
  <img alt="EXPERIMENTAL - under active development - experts only" src="https://img.shields.io/badge/%E2%9A%A0-E%20X%20P%20E%20R%20I%20M%20E%20N%20T%20A%20L-critical?style=for-the-badge&labelColor=8b0000">
  <br>
  <strong>EXPERIMENTAL &nbsp;&mdash;&nbsp; UNDER ACTIVE DEVELOPMENT &nbsp;&mdash;&nbsp; EXPERTS ONLY</strong>
</p>

This project drives an undocumented PCIe coprocessor by writing to its registers from a kernel
module of our own making. There is no vendor support for any of it, on any operating system.

It is not a product. It is not supported. It can wedge the appliance hard enough to need a
power cycle by hand &mdash; which has already happened here, more than once, during development.

<p align="center">
  <img alt="Do not run this on anything you rely on" src="https://img.shields.io/badge/DO%20NOT%20RUN%20THIS%20ON%20ANYTHING%20YOU%20RELY%20ON-critical?style=for-the-badge&labelColor=8b0000">
  <br>
  <strong>DO NOT RUN THIS ON ANYTHING YOU RELY ON.</strong>
</p>

Not on a firewall carrying real traffic. Not on hardware whose power switch you cannot reach.
Assume any commit can change behaviour, and assume the first thing you lose is the network you
manage it over.

You will need a serial console, the willingness to read the source before you load it, and a way
to reinstall if it goes wrong.

**USE IT AT YOUR OWN RISK AND ON YOUR OWN RESPONSIBILITY.** Nothing here carries a warranty of any
kind, and no one else is answerable for what it does to your hardware, your network or your data.
If you load it, that decision and its consequences are yours.

</td></tr>
</table>

**All fourteen front ports of a Sophos XGS 136, working under OPNsense.**

**And on a Sophos XGS 3300 - a different coprocessor family entirely - the management link between
the host and its coprocessor, carrying IP traffic.**

This appliance looks like one computer and is two: an x86 host, and a Marvell CN9131 coprocessor
behind a PCIe endpoint that **owns every front port**. Install a stock OPNsense on one and it
boots to a working firewall with no network interfaces at all. The vendor's drivers are
Linux-only, so the usual answer is that the hardware is e-waste.

It is not. The coprocessor is a whole computer that boots its own Linux from its own eMMC, and it
is sitting there waiting to be told a host is present.

> **Scope.** Two appliances have been on the bench, and they are not the same silicon. On the
> **XGS 136** (AMDA0201, Marvell CN9131, ARMADA family) all fourteen front ports carry traffic. On
> the **XGS 3300** (AMDA0202, Cavium OCTEON TX CN83XX) the management link is up and pings, the
> handshake completes, the host programs SDP datapath rings, and traffic now crosses **in both
> directions**: frames leave a front port and reach a machine off the appliance, and frames entering
> a copper panel port arrive on a FreeBSD interface carrying that panel port's own address. The
> switch behind the panel ports is programmed, all eight copper ports run at a gigabit and the panel
> LEDs are lit. What is **not** there is performance work - no zero copy, no batching, no offload -
> and persistence: nothing survives a reboot without being brought up again by hand. Four further
> families are described from the vendor's own tables with **no hardware at all**; see
> [Families](#-families), where every row says which is
> which. Values for untested assemblies are carried in the tree and marked as untested wherever
> they appear.

## ✅ What works

*This section is the **ARMADA** family - the XGS 136. For OCTEON TX and the XGS 3300, see
[Families](#-families) and [docs/families/octeon-tx.md](docs/families/octeon-tx.md).*

**All fourteen front ports carry traffic, in both directions, as fourteen ordinary FreeBSD
interfaces.**

```
npuep0: giu: datapath enabled - 14 interfaces, 256 descriptors each way
npuep0:   Port1   npup1 tag 0x8100      Port2   npup2 tag 0x8200
npuep0:   Port7   npup7 tag 0x8700      Port8   npup8 tag 0x8800
npuep0:   Port9   npup9 tag 0x0001      Port10  npup10 tag 0x0003
npuep0:   PortF1  npup13 tag 0x8900     PortF2  npup14 tag 0x8a00
npuep0: rpc: all 14 front ports have an interface and a binding, read back and confirmed
npuep0: nwa: 14 of 14 ports up
```

Measured with three cables in, frames arriving on three ports at once and each landing on its own
interface:

```
dev.npuep.0.giu.rx_port.Port9: 34      dev.npuep.0.giu.rx_port.Port7: 27
dev.npuep.0.giu.rx_port.Port8: 10
```

Port 8 is behind the coprocessor's internal switch and Port 9 is a separate MAC on the SoC, so
both port families are proven together. A full ARP exchange completes over either — request in,
reply out — which is the smallest thing that requires both directions to work.

### And on OCTEON TX — the XGS 3300, which is a different and earlier story

A frame now goes in at a panel port and comes out on a FreeBSD interface carrying that port's own
address. **All twelve front ports are interfaces**, each with its own address read from the
coprocessor rather than invented:

```
oxp0   PortF1   the two 10G SFP+ cages, direct to BGX2
oxp1   PortF2
oxp2   Port1    the eight copper panel ports, behind the 88E6193X
...             every one at a gigabit, with its PHY powered and its LED lit
oxp9   Port8
oxp10  PortF3   the two 1G SFP cages, also behind the switch
oxp11  PortF4
```

Twelve interfaces, twelve distinct addresses, in the board file's own `lifport` order. **What is done and what is not** is a weighted table in [octeon-tx-reference.md](docs/families/octeon-tx-reference.md#where-this-stands) - the short version is 85% - and every number behind it, with how it was taken, is in [docs/measurements/xgs3300.md](docs/measurements/xgs3300.md). **Traffic is
proven on the three that have cables in them** - a 10G cage and two copper panel ports - and the
other nine are bound, addressed and presented the same way.

**IPsec, driven by OPNsense.** The kernel's own offload contract, `if_ipsec_accel_methods`, is
answered on every front port: an association strongSwan installs reaches the coprocessor's crypto
engine through the kernel, with no command typed, and both directions of a tunnel then run through
that engine - the inbound frames it decrypts are terminated by the driver, and for the outbound
association the driver stands where the kernel's own cipher would be called, so the coprocessor is
the **only** encryptor on it: a packet too big for the tunnel is answered or fragmented first, never
handed back to the kernel. Measured on a real tunnel between two OPNsense appliances joined at
10 Gbit/s, four TCP streams from a machine on a 1 Gbit/s LAN port: **951 Mbit/s down and 912 up with
the coprocessor's cipher, 149 and 256 with the kernel's**, a rekey under load without a lost
sequence number. And a connection that runs through the tunnel can be put in the coprocessor's flow
table **with its association**, so the host leaves its data path too: one TCP stream went from 75 to
195 Mbit/s across the host to **680 to 750 with both directions in hardware**, the host a few
hundredths of a core busy. That is `dev.octep.0.ipsec.flows`, off by default, and its top value is
a decision rather than a default: the fast path forwards a frame that arrives *in the clear* on the
tunnel's port if it matches a connection that is in hardware - read in its code, then measured,
15 of 20 - while the value below it keeps the kernel's check on everything that arrives and takes
only the direction that leaves encrypted. Honest limits: IPv4, AES-GCM-16, no ESN, no NAT-T
(issue 294); once an association has been mirrored the module cannot be unloaded without a
reboot. Behind `dev.octep.0.ipsec.on` and the loader tunable `hw.octep.ipsec_on`, off by default.
The pages are [the kernel drives the coprocessor](docs/the-kernel-drives-the-coprocessor.md),
[one encryptor per association](docs/one-encryptor-per-association.md) and
[the flow carries the association](docs/the-flow-carries-the-association.md).

**A connection the fast path gives back is answered at once.** The coprocessor hands a TCP
connection back to the host at the twelfth frame in a row with the same acknowledgement - which is
what every lost segment of a fast download produces - and the driver used to notice at its next
one-second poll. The receive path now kicks a task the moment a punted frame belongs to a
connection it holds, the connection is put back in service with one command, and a command's
answer is waited for in steps of twenty microseconds instead of a thousand. Four streams
downloading through the tunnel went from 23 to 42 % forwarded by the coprocessor to **95 to 97 %**,
the host from half a core to under a tenth. It is in
[a give-back is answered at once](docs/a-give-back-is-answered-at-once.md), for plain connections
as for tunnelled ones.

**And `pf`'s state no longer runs out under a connection that is in hardware.** `pf` sees none of
an accelerated connection's packets, so nothing restamped its state: a UDP flow through NAT was
taken out about once a minute and came back with a different source port, and a TCP connection
whose state went was cut. A connection is now made only when `pf`'s states for it are on their long
timer, and every five seconds the coprocessor is asked which connections it is still forwarding and
`pf`'s states for those are restamped. Found by reading for something else; measured before and
after in [a state that sees nothing runs out](docs/a-state-that-sees-nothing-runs-out.md).

**And a connection is made when its frames ask for it, not at the next poll** - with two older
faults found on the way that were worth more than the poll's second. The poll's eight attempts a
second were being spent on flows that can never be accelerated, and a connection behind them was
never made; and the table a connection's second direction is read from took the wrong bits of its
hash, so connections from one machine to one server shared a slot and were made one a second.
Four streams down through the lab tunnel for twelve seconds: 88 % forwarded by the coprocessor
before, 99 % now; eight streams up, 83 % and 100 %. Four streams that last two seconds: 87 % with
the poll alone, 100 % made between polls.
[A connection made between polls](docs/made-between-polls.md).

**And one connection in sixty-four was never made at all**: the one whose two directions land in
the same slot of the table its second direction is read from. Found by a counter, shown on demand
by opening a few hundred flows and computing which of them collide, and fixed with one more entry
that the missing direction is kept in.
[A connection that collides with itself](docs/a-connection-that-collides-with-itself.md).

**And a rekey under load no longer drops what is sent while the new association installs.** The
driver used to take the kernel's cipher away, wait, read the kernel's sequence counter and only
then give the coprocessor the association - and for those milliseconds every packet was dropped:
308, 368 and 540 of them in three of six rekeys. Now the coprocessor is given the association
first, started a million numbers past the kernel's counter, with the kernel's IVs moved to a half
of their space the coprocessor cannot reach; and the cipher is taken second. Twenty-three rekeys
under a four-stream upload: nothing dropped by the appliance, one packet dropped by the peer in two
of them. [Installed first, taken second](docs/installed-first-taken-second.md).

**And the host carries one stream as fast as four.** One TCP stream through the tunnel ran at
75 Mbit/s where four filled the gigabit, and it was not the tunnel. The receive watchdog - a timer
that looks at every ring twenty times a second in case an interrupt is lost - made one pass over a
ring its handler was working; the handler found the ring held and left; and the ring then waited
for the timer, fifty milliseconds at a time. A servicer now holds a ring for its whole visit and
goes round until it is empty, and one that is turned away leaves a note the holder reads. One
stream across the host: 755 to 797 Mbit/s up and 769 to 793 down, where it was 74 to 76 and 88 to
362 - the same as with the flow table carrying it.
[A visit is not a pass](docs/a-visit-is-not-a-pass.md).

**And one of them is the appliance's WAN.** Panel port 2, assigned in OPNsense and asked for a
lease, gets one from the upstream router and installs the default route through itself:

```
DHCPDISCOVER on oxp3 ... DHCPOFFER ... DHCPACK ... bound
default            <upstream>         UGS            oxp3
```

That is the whole path working as a firewall would use it: a DHCP exchange is broadcast out, a
unicast reply back, through the switch, the coprocessor's fast path and the PCIe ring, and into the
host's own network stack. Everything below is how, and each step was a separate fault.

**The management link carries IP.** `octep0` is an ordinary FreeBSD interface and ping across PCIe
runs at 0% loss. That is one interface, not the front ports.

**The host programs an SDP datapath ring and frames cross it.** The ring pair is allocated and
programmed, the coprocessor's own fast path runs beside it, and frames posted by the host arrive:

```
IN_CNTS 8   IN_PKT_CNT 8   IN_BYTE_CNT 2144
```

`2144` is twice `(64+28)+(128+28)+(256+28)+(512+28)` — the hardware's own byte counter agreeing with
every `tlen` the driver wrote, at four frame sizes, which is what confirms the instruction format
rather than an inspection of it. The fast path survives the traffic: no core dumped.

**And NetAgent answers.** That is the front ports' control plane rather than their datapath - the
same handshake gates both, and before it the NetAgent window is blank and every request returns
`ENXIO` - but once it is up NetAgent answers with nothing plugged in:

```
op 0x01  marker 0x00000014 (expected)  status 0x00000000 (ok)  reply 2020 bytes
payload 503 words        commands 2   timeouts 0
```

The protocol was already in this repository, described in [docs/netagent.md](docs/netagent.md) and
implemented for ARMADA in `contrib/npuep/npunwa.c` — and the header this coprocessor publishes is
identical to the ARMADA one word for word, which is the first evidence from silicon that the
NetAgent **framing** is family-independent rather than merely looking it. The operation set is
not: from the host only four opcodes have a handler here, and every other one comes back with
status 1. The transport is proven and the reply is decoded: fourteen ports, thirteen of them
populated, each a 20-byte record whose first word is `tag | flags`.

A front port can be raised from here and the link read back, and a 10G fibre between the two SFP+
cages trains under OPNsense - proven by taking one end down and watching the other end's link follow.

**The whole loop now completes, and a frame has been read out of host memory at the end of it.**
The fast path keeps 182 named counters, and reading them around a controlled burst accounts for
every frame. With a fibre between the two SFP+ cages, six hundred frames posted on the host's ring:

| the fast path's own name | what it means | delta |
|---|---|---|
| `FPCNTR_RX_KN` | taken off the host's ring | **+600** |
| `FPCNTR_FROM_KN_TO_WIRE` | and routed to the wire, not to encryption | **+600** |
| `FPCNTR_TX_WIRE` | and transmitted | **+600** |
| `FPCNTR_RX_WIRE` | it arrives back on the other cage | **+600** |
| `FPCNTR_FROM_WIRE_TO_KN_FORCED` | no offloaded connection matches, so it is forced to the host | **+600** |
| `FPCNTR_TX_KN` | and the decision to hand it over is taken - this counts the decision, not the delivery | **+600** |
| `FPCNTR_TX_DROP`, `FPCNTR_TX_DROP_QUEUE_FULL` | nothing dropped anywhere, at this burst size | **0** |

and one of them was then found sitting in a host receive buffer, decoded:

```
ring 12 buf   0  len 134  tag 2  meta 0xb44399a2 - the vendor's
  ff:ff:ff:ff:ff:ff <- 02:00:00:00:00:01  type 0800
```

That is the driver's own test frame, back from PortF2, in memory this host owns.

**What it took, and each of these was a separate fault.**

*The metadata chooses the destination.* The 64 bytes in front of every frame are not filler. The
fast path reads one of them and, when it is non-zero, takes the four bytes after it as an egress
security-association handle and routes the frame to encryption instead of to the wire. The walking
pattern from `0xc0` makes that byte `0xdf`, so **every frame asked to be encrypted**, none could be,
and all of them were charged to `FPCNTR_TX_DROP`. Sending what the vendor's own hook writes - byte 0
set to 1, the other 63 zero - is what first put a frame on a wire.

*The host has to state the ring split, and build it.* `RINFO` is not only a description of how the
endpoint is carved up; it is how the host **states** the carving, and this driver only ever read it.
The vendor writes `RINFO |= (rpvf << 32) | (nvfs << 48)`, which on this appliance's own firmware
gives `0x0008000100400000` - eight VFs at one ring each, so the PF's rings begin at 8. Declaring
that is half of it: the functions have to exist too, in the SR-IOV capability, or the far side is
told about ring sets nothing on the bus backs and it goes down. Create, declare, then hand over -
and the far side's transmit queue drains for the first time.

*The rings must be programmed before the handshake, and never restarted.* The target latches the
host's ring addresses when its port opens and does not look again. Programmed first and left alone,
600 frames drop nothing; after two `dp.stop` / `dp.start` cycles, 400 frames give 192 queue-full
drops and it never recovers. Every earlier burst in this project was measured after a restart, which
is why the queue always looked permanently stuck.

*And the return prefix is 82 bytes, not 66.* Eight bytes of SDP info carrying the length, eight more
holding `0x8003000000000000`, then the 2-byte port tag and 64 metadata bytes, and only then the
Ethernet header. The 66 is the target's own 2+64; it is not what lands in a host buffer.

*The doorbell counts bytes of the scatter list, not entries.* This is the one that had held the
project for eighteen months, and it was never a missing mechanism. `R_OUT_SLIST_DBELL` counts
**sixteen per buffer** - the size of one scatter-list entry - so a grant of one credit per entry
granted a sixteenth of the ring, and the block's fetch pointer sat inside a descriptor rather than
on one. Granted in the block's own unit, 300 paced frames give `rx_done +308` and the far side's
`TX_DROP_QUEUE_FULL` stops moving. Nineteen negatives had been recorded before this, every one of
them a plausible missing mechanism; three were genuinely missing, were implemented, and changed
nothing. **When a number is off by a constant factor, that is the finding.**

*The panel ports are behind a switch, and the switch had never been programmed.* Ten of the twelve
hang off a Marvell 88E6193X reachable only from the coprocessor, and every one of its panel ports
was left disabled by the vendor's own init. `contrib/mvsw` reaches it over `/dev/mvmdio-uio` and
brings them up; all eight copper ports run at a gigabit with their PHYs powered and the panel LEDs
lit to the board file's own scheme.

*The two SFP cages were held dark by one bit each on the CPLD.* `tx_disable` for the 1G cages lives
in CPLD register `0x25`, bits 4 and 10, and both read set while the two 10G cages' equivalents read
clear - which is why those two had always worked. Clearing them brings a cage up at a gigabit. A
module swap needs the SERDES woken again; the CPLD bit itself survives both that and a reboot.

*And the port tag is what the switch's DSA tag says it is.* Frames from a panel port reached the
coprocessor and died at `FROM_WIRE_DROP_LIF_INDEX_INVALID`, and the index was never the problem. The
switch's uplink runs in **DSA frame mode**, so every frame it sends the coprocessor carries a 4-byte
tag at offset 12 naming the source port; the fast path turns that into
`0x8000 | (src_port << 8) | (src_dev << 5)` and looks the result up. This driver had been binding
tags 1, 2 and 3 - values the far side never produces. Binding the tags it does produce, after naming
each port's own address to the switch so its TCAM entry stops being "Never Hit", empties both drop
counters:

```
RX_WIRE +10   FROM_WIRE_TO_KN_FORCED +10   TX_KN +10
FROM_WIRE_DROP_LIF_INDEX_INVALID +0   RX_BAD_PORT_TYPE +0
```

**And the frames land.** `oxp3` is panel port 1, with panel port 1's own MAC read from NetAgent
through that same tag, and it counts the packets that arrive on it. See
[docs/families/octeon-tx.md](docs/families/octeon-tx.md) for the measurements and the order the
bring-up has to happen in, which turns out to matter a great deal.


### Back on ARMADA - the XGS 136, port by port

**Twelve of the fourteen are verified port by port, with loopback cables.** An ARP exchange with
an outside device proves one path; it says nothing about the other eleven, and nothing at all when
the far end declines to answer. `contrib/npuep/portmap.sh` removes the far end from the question:
it transmits out of each port in turn and records which port hears it.

```
  sent on   heard on
  ...
  npup3      npup4(+1)        0x8300 → 0x8400   switch
  npup4      npup3(+1)
  npup5      npup6(+2)        0x8500 → 0x8600   switch
  npup7      npup8(+1)        0x8700 → 0x8800   switch
  npup9      npup10(+1)       0x0001 → 0x0003   SoC
  npup11     npup12(+2)       0x0004 → 0x0002   SoC
  npup13    - nothing -       SFP cage, no fibre to hand
```

Every pair symmetric, both tag families, and **the sending port's own counter never moved** — so
the coprocessor's switch does not forward between front ports behind the host's back. That last
one is not a detail: if it did, traffic would pass between two ports without `pf` ever seeing it.
The host is the only forwarder here, which is what a firewall needs.

It also settles the one part of the port table that was inferred rather than read. The four SoC
ports are tagged `0x0001, 0x0003, 0x0004, 0x0002` in connector order — not sequentially — and that
ordering came out of a disassembly. If two of those were swapped, a frame leaving `npup10` would
have arrived on `npup11` instead of `npup9`. It arrived on `npup9`.

`PortF1` and `PortF2` are the SFP cages and are **untested**: a copper patch lead cannot loop them.

The driver loads itself at boot, creates the interfaces, programs the coprocessor and verifies the
programming by reading it back. Nothing is typed.

## 🧭 How it works, briefly

The coprocessor publishes a map of five *facilities* in a PCIe BAR, each one a different
conversation:

| Facility | What it is | Where |
|---|---|---|
| `ctrl` | the handshake and the doorbells | [docs/facility-protocol.md](docs/facility-protocol.md) |
| `mvmgmt` | a management NIC between host and coprocessor | [docs/mvmgmt.md](docs/mvmgmt.md) |
| `giu` | the datapath: a full NIC with queues, buffer pools and offloads | [docs/giu.md](docs/giu.md) |
| `nwa` | a mailbox for per-port state — link, speed, media, admin up | [docs/netagent.md](docs/netagent.md) |
| `rpc` | the control channel that fills in the forwarding tables | [docs/rpc.md](docs/rpc.md) |

The datapath carries every front port on **one** pair of DMA queues. What separates them is a
two-byte tag in front of each frame: the driver writes it on transmit to choose the egress port,
and reads it on receive to decide which interface a frame belongs to. Ports come in two families
and they do not follow one rule — ten behind an internal switch carry `0x8000 + n*0x100`, four
that are separate MACs on the SoC carry `0x0001`..`0x0004`.

**OCTEON TX differs on the receive side and it matters.** There the host owns eight output rings,
the fast path chooses between them by hashing the frame, and the host does not get to pick - so all
eight have to be read, not just the one transmit uses. The tag is in the same place, but two more
qwords sit in front of it: the prefix a host buffer receives is 82 bytes, not 66.

Receive needs the coprocessor's own forwarding tables filled in, which is what the control channel
is for: a logical interface per port, and a binding from the port tag to it. Two commands each,
and nothing else.

## ⚠️ What it cannot do

*Also ARMADA. The OCTEON TX limits are different and are listed on
[its own page](docs/families/octeon-tx.md). There both directions now work and the panel ports are
interfaces, but nothing is optimised - every frame is copied, there is one queue per direction per
interface and no offload - and the whole bring-up is a sequence of sysctls that has to be repeated
after every reboot.*

**The datapath attaches once per coprocessor boot.** The device waits for `HOST_MGMT_READY`
once, answers once, and then spends the rest of its life in its command loop. **A module reload on
its own cannot be answered** — three remedies were tried and measured not to help: closing the
datapath down with `PF_DISABLE`/`PF_CLOSE`, which the device accepts and which changes nothing;
retracting the stale handshake; and waiting thirty seconds instead of four.

What restores it is a coprocessor reboot, and the host can cause one itself. `06-npuctl` pulses the
reset line, and that pulse is a real reset rather than a release — so **a pulse followed by a
reload brings everything back with no power cycle.** Verified, twice in a row: fourteen
interfaces, the forwarding tables read back and confirmed, `nwa: 14 of 14 ports up`, and a front
port brought up with no address counted forty frames off the wire in twelve seconds. A cold power
cycle works too. A warm reboot runs the same hook, so it very probably does as well — still
untested end to end, but no longer resting on an untested mechanism.

This file used to say a power cycle was the only way. That was wrong, and both reasons looked like
hardware rather than like bugs: `reload.sh` parsed the PCI selector with an `awk` field separator
that left out the tab `pciconf` prints, so it waited its whole timeout and reported a dead
endpoint that had been answering all along; and the barmap parser read an entry the coprocessor
had not written yet as a real one — every field zero, and zero is the control facility's id — so a
half-published table failed attach outright. Both fixed.

**The facility table takes about fourteen seconds to appear after a reset, and is not published
atomically.** The cookie lands before the entries do, so validating the cookie and reading on gets
a partial table. The driver now waits for the facilities it actually needs. Measured at exactly
fourteen seconds on two consecutive cycles.

Loading the driver too soon after a reset pulse **hangs the host**: a PCIe read to an endpoint
still in reset neither returns nor times out, so there is no panic and no log — the machine simply
stops. Both the driver and `reload.sh` now ask **config space** first, which is the safe question:
a configuration read to a device that is not answering comes back as all-ones instead of being
left outstanding. Measured after the fix, the endpoint answers config space the instant the pulse
ends, so the guard costs nothing and only the facility table needs waiting for.

**The interfaces are assigned in OPNsense, and they have to be.** All twelve carry traffic and can
be bridged; one of them is this appliance's WAN. Until a front port is assigned it is outside the
firewall's own configuration and pf has no rules for it, so an unassigned port that looks up and
carries nothing is usually that and not the driver.

**A bridged front port needs promiscuous mode, and gets it by itself.** The port's hardware filter
admits unicast only for the address it owns, and a bridge member receives replies addressed to the
**bridge** — so without it the port forwards broadcast and nothing else, which looks like a working
port right up to the point where something tries to talk through it. The driver follows
`IFF_PROMISC`: on when `if_bridge` adds the port, off when it leaves, nothing to configure.

**The device's own packet counters are unavailable.** `GET_STATISTICS` is answered, at full
length, with zeros: Marvell's header labels that member `CC_PF_PP2_STATISTICS`, the physical
packet processor's counters, and a function with no physical port has none. `GET_GP_STATS`, which
is the GIU port's own, is not implemented by this firmware at all. The driver reports its own
counts and says so plainly, because an instrument that prints a zero reading like a measurement is
worse than one that admits it cannot see.

## 🧩 Families

Sophos XGS appliances are not one machine with a range of speeds. The coprocessor behind the front
ports belongs to one of **six** families, and the firmware picks the family by probing for a single
PCI id or a CPU model string - `xgs-host-startup.sh` in the vendor's own tooling is where that table
lives. Two of the six have no coprocessor at all.

The table is what has been **run**, not what has been read. A row with no hardware means exactly
that: the constants are carried because the vendor's tables give them, and nothing has been powered
on.

| family | probed by | platforms | driver | binds? | hardware here? | what works |
|---|---|---|---|---|---|---|
| [ARMADA](docs/families/armada.md) | `11ab:7080` | `xgsdt1`, `xgsdt2-116`, `xgsdt2-126136`, `xgsdt2-138` | `npuep` | yes | **XGS 136** | **all 14 front ports** |
| [OCTEON TX](docs/families/octeon-tx.md) | `177d:a300` | `xgs1us` | `octep` | yes | **XGS 3300** | **all 12 front ports** |
| [OCTEON TX2](docs/families/octeon-tx2.md) | `177d:b200` | `xgs1ul`, `xgs1ul_4x80`, `xgs2u`, `xgs2ub` | none | no | no | nothing - documented only |
| [OCTEON TX2 98XX](docs/families/octeon-tx2-98xx.md) | `177d:b100` | shares the TX2 platforms | none | no | no | nothing - documented only |
| [TOPAZ](docs/families/topaz.md) | `Atom C11` in `/proc/cpuinfo` | - | not needed | - | no | no coprocessor exists |
| [GR](docs/families/gr.md) | `Atom` **and** `P69` | `AMDA0004-*` | not needed | - | no | no coprocessor exists |

**The two drivers are not at the same stage, and the table says so.** `npuep` carries a datapath;
`octep` brings up a management link, completes the SDP handshake, drives NetAgent, raises a front
port and reads its link back, and posts frames on SDP rings that the coprocessor consumes - they
leave PortF1, arrive on PortF2 and are written back into host memory, but each ring delivers one
packet and then stops, so it has no usable front-port interface. Both are built on the appliance against the running
kernel's own sources and neither is packaged - see
[docs/families/octeon-tx.md](docs/families/octeon-tx.md) for how to build and start `octep`,
including why its handshake is a separate step you have to ask for.

**`177d:b100` is its own family and not a variant of TX2.** It has a separate branch in the vendor's
startup script, and that branch counts how many times the id appears, because on those boards it
appears more than once. Calling it OCTEON TX2 loses that.

**The two families are genuinely different protocols, not one protocol with two PCI ids.** ARMADA
keeps its facility table at a fixed offset in a BAR and guards it with a cookie; OCTEON publishes a
pointer to its table in a CSR and guards it with a different magic word. Their facility records have
different fields in a different order. ARMADA raises target-to-host doorbells as MSI-X vectors; on
OCTEON every facility reports that it has none. That is why there are two drivers and not one with a
switch in it - see [docs/families/](docs/families/) for each one.

## 🖥️ Hardware

Two appliances, running OPNsense 26.7 on FreeBSD 15.1:

- **Sophos XGS 136** - assembly AMDA0201, Marvell CN9131, ARMADA family, 14 ports. All fourteen
  carry traffic. - **Sophos XGS 3300** - assembly AMDA0202-0004, Cavium OCTEON TX CN83XX, 12 panel
  ports - of which ten are ports of an on-board 88E6193X switch and only two attach to the
  coprocessor directly, see [the family
  page](docs/families/octeon-tx.md#how-the-ports-are-actually-wired) - plus a host-side Intel
  management NIC. Its management link to the coprocessor is up, the SDP handshake completes, the
  host drives SDP datapath rings, traffic crosses in both directions, and the front ports are
  ordinary FreeBSD interfaces - up to twelve of them, one per front port, each with that port's own
  address.

Everything below in this section is about the XGS 136 and the ARMADA reset tables.

The per-board reset values are a table, not a constant — the polarity is inverted between board
generations — and the board is read from its own assembly number, which sits inside the SMBIOS
type 2 serial (`src/opnsense/scripts/xgs/board.sh`). Values are carried for AMDA0200, AMDA0201
(XGS 126/136), AMDA0202-0205 - AMDA0202 being the XGS 3300's own OCTEON TX assembly, carried here
because the reset table is per assembly and not per family - AMDA0208 (XGS 116) and AMDA0224
(XGS 138). **Only AMDA0201 has been driven on real hardware, and the tool refuses to drive the
pins on any other board.** The others come from the vendor tool and should be treated as
unverified.

An earlier version of this section said the board was already read and looked up. It was not: the
tool carried AMDA0201 as a constant, and on an XGS 3300 that sent the 136's values to a bridge
wired to the coprocessor's reset and boot flash. The coprocessor dropped off the bus until the
bridge was put back to its power-up state with `mcp2210.py restore`.

## 📦 Installing

> [!IMPORTANT]
> **One installer, and it decides the appliance before it installs anything.** It reads the
> board's assembly number (`src/opnsense/scripts/xgs/board.sh`) and installs only that board's
> pieces: **AMDA0201** (XGS 126/136) gets the ARMADA set below, **AMDA0202** (run on the XGS 3300)
> gets the OCTEON TX set - `08-octep`, the driver's sources, and the module built through the kernel
> follower. **Any other board gets nothing**, and so does an ARMADA board with an OCTEON TX
> endpoint on its bus. Each set removes the other's pieces if an older version put them there.
> The OCTEON TX module build needs the running kernel's sources already in `/usr/src-<series>-<sha>`
> (`contrib/npuep/fetch-sources.sh`); see [docs/families/octeon-tx.md](docs/families/octeon-tx.md).

```sh
git clone https://github.com/AbdelmonemAwad/os-xgs-npu
cd os-xgs-npu
./install/install.sh
```

> [!WARNING]
> **Before blaming anything here for a network that has no internet, check that the firewall can
> resolve a name.** A fresh OPNsense leaves the resolver recursing to the root servers, and an
> upstream that does not allow that makes every query fail. It cost most of a day on this
> appliance.
>
> ```sh
> host example.com 127.0.0.1
> ```
>
> `SERVFAIL` means the resolver, not the datapath. The fix is to give the system nameservers and
> let the resolver forward to them instead of recursing: **System - Settings - General - DNS
> servers**, then **Services - Unbound DNS - Query Forwarding - Use System Nameservers**.
>
> It hides well. A machine with more than one network keeps every adapter's nameservers and
> resolves through whichever works, so the port you administer the box from looks fine while a
> network whose only resolver is the firewall has no internet at all - and the firewall still
> answers `ping` by address, because an address needs no name. The first network that depends on
> this box alone is where it surfaces, which may be long after the install.

On an ARMADA board the installer puts in place two early boot hooks and, if a module has been
built, installs it:

- `06-npuctl` pulses the coprocessor out of reset. It comes out of power-on **held**, and nothing
  in OPNsense releases it — which is why the appliance boots with a silent coprocessor and no
  ports.
- `07-npuep` loads the driver and waits for the interfaces to appear, because every early hook
  runs before OPNsense configures its interfaces and one that returns too soon leaves them out of
  that pass.

The kernel module is **built on the appliance**, not packaged: it is C against that kernel's
headers. `contrib/npuep/fetch-sources.sh` fetches the sources that match the running kernel,
pinned to the commit the kernel names in `uname -v`; `contrib/npuep/build.sh` builds against them
and stamps the result with the kernel it was built for; the installer copies both to
`/boot/modules`.

That stamp is not bookkeeping. A module's kernel dependency is a range running to the end of its
branch, so a module built for the wrong 15.x kernel **loads without complaint** rather than being
refused — the loud failure this project was designed around does not arrive. `install/verify.sh`
compares the stamp instead, and `compat.json` records `kern_version` rather than the version
labels, which do not move when OPNsense ships a kernel set inside a series.

Both hooks are numbered above OPNsense's own `05-upgrade`, which finalises a pending firmware set
and reboots from inside the early boot sequence. Numbered below it, as `01` and `02`, this code
brought the coprocessor up and then had the machine rebooted underneath it.

It cannot be preloaded from `loader.conf`, and not for one reason but two — the reset pulse has
not happened at that point, so there is no live device to attach to, and the BAR restore this
hardware needs only happens on the `kldload` path. See
[docs/porting-notes.md](docs/porting-notes.md).

Both hooks are written so that they can never be the reason a firewall fails to boot: unfamiliar
hardware, a missing module or a coprocessor that never answers each log the reason and exit 0.

## 📚 Documentation

[**DESIGN.md**](DESIGN.md) is the contract — the stages, the limits that are properties of the
hardware, and every claim that turned out to be wrong, with what replaced it.

| | |
|---|---|
| [families/](docs/families/) | The six coprocessor families, the assembly map, and what has run on which. [octeon-tx.md](docs/families/octeon-tx.md) is the long one: the XGS 3300's wiring, the SDP handshake and ring, and the order bring-up has to happen in |
| [the-road.md](docs/the-road.md) | **How it was found**, drawn: the phases, the nineteen dead ends and what each one cost, and the eleven things that have to be right at once for one packet |
| [hardware.md](docs/hardware.md) | What is actually on the board, measured |
| [measurements/](docs/measurements/) | **The numbers, one page per appliance**: what was measured, how it was taken, and what each figure does not mean. [xgs3300.md](docs/measurements/xgs3300.md) has the round trip, the transmit ceiling and the two counters the measuring found to be wrong |
| [npu-bring-up.md](docs/npu-bring-up.md) | Getting the coprocessor out of reset, over a USB-to-SPI bridge |
| [facility-protocol.md](docs/facility-protocol.md) | The five facilities, the barmap, the handshake |
| [mvmgmt.md](docs/mvmgmt.md) | `mvmgmt0`, the management interface |
| [giu.md](docs/giu.md) | The datapath that carries all fourteen ports, and the 66-byte header |
| [rpc.md](docs/rpc.md) | The control channel and the forwarding tables |
| [netagent.md](docs/netagent.md) | Per-port state, link, media and address |
| [porting-notes.md](docs/porting-notes.md) | What a port to another OS would hit |
| [provenance.md](docs/provenance.md) | ARMADA: what was read, from where, and what was deliberately not copied |
| [octeontx/provenance.md](docs/octeontx/provenance.md) | OCTEON TX: the same, kept separate because it is a different upstream |

Per family, under [docs/families/](docs/families/): [armada.md](docs/families/armada.md),
[octeon-tx.md](docs/families/octeon-tx.md), [octeon-tx2.md](docs/families/octeon-tx2.md),
[octeon-tx2-98xx.md](docs/families/octeon-tx2-98xx.md), [topaz.md](docs/families/topaz.md),
[gr.md](docs/families/gr.md).

Much of it was recovered from Sophos's own shipped binaries, which carry full debug information,
and from Marvell's GPL source drop. Where a claim comes from a disassembly it says so; where it
comes from the vendor's own boot log it quotes the line. **Where something is inferred rather than
measured, it says that too** — several things in this file were once stated with more confidence
than the evidence carried, and the corrections are in the history.

## ⚖️ Licence

**BSD-2-Clause.** Every file carries the identifier; [LICENSE](LICENSE) is the whole of it.

The protocol it speaks was recovered from Marvell's GPL-2.0-only sources, published by Sophos in
its SFOS_OSS drop, and from binaries the appliance ships. **No vendor code is included or
redistributed here.** Facts — offsets, command numbers, field widths — are transcribed; expression
is not copied, and `contrib/npuep/npugiu.h` is where that decision is visible and argued.

[**docs/provenance.md**](docs/provenance.md) sets out exactly what was read, from where, which four
sentences are quoted verbatim and why, and what is deliberately absent. Read it before reusing
this, and form your own view.
