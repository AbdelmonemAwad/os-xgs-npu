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
  <a href="docs/families/octeon-tx-reference.md"><img alt="XGS 3300: front ports link at 10G, no return traffic" src="https://img.shields.io/badge/XGS%203300%20(AMDA0202)-front%20ports%20link%20at%2010G%20%7C%20no%20return%20traffic-orange.svg?style=flat-square"></a>
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
> the **XGS 3300** (AMDA0202, Cavium OCTEON TX CN83XX) the management link is up and pings, and the
> handshake that gates its front ports completes, the host programs an SDP datapath ring, and frames
> posted on it **leave a front port and cross a fibre** to the other cage - but nothing comes back, so
> **no front port carries host traffic yet**. Four further families are described from the vendor's
> own tables with **no hardware at all**; see [Families](#-families), where every row says which is
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

Two things work there, and they are worth separating from each other.

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

**What still does not work is the datapath's other direction.** Frames posted on an SDP ring are
consumed by the coprocessor and nothing returns.

Egress is no longer in question. The per-port counters are dead on this path, so it was settled by
watching the cages: streaming 5,567 frames in two windows separated by five seconds of silence makes
the activity LED on both cages blink during the windows and stop together during the silence. A frame
posted on the ring reaches a front port, crosses the fibre and arrives at the other one.

Four defects on this side have been found and fixed since. The frame was missing its 66-byte
private header - a 2-byte port tag in network order then 64 metadata bytes running `0xc0` to
`0xff`, which the far side validates. Two derived constants were wrong with it: `pki_ih3.sl` had
to become 94 rather than 28, and the checksum offset 81 rather than 15. And the receive buffer was
sized 1536 where the vendor uses 1602, because the same private header counts against it. None of
them alone changed the outcome. What remains is not a header field: nothing yet tells the
coprocessor's fast path to hand a received frame to the host - and Marvell's own host modules, read
as source, never send such an instruction either. See
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

Receive needs the coprocessor's own forwarding tables filled in, which is what the control channel
is for: a logical interface per port, and a binding from the port tag to it. Two commands each,
and nothing else.

## ⚠️ What it cannot do

*Also ARMADA. The OCTEON TX limits are different and are listed on
[its own page](docs/families/octeon-tx.md) - most of all that frames leave a front port there
but nothing is ever received back, so no front port is usable as an interface yet.*

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

**The interfaces are not assigned in OPNsense yet.** They exist, they carry traffic, and they can
be bridged — but until they are assigned they are outside the firewall's own configuration and pf
has no rules for them.

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
| [OCTEON TX](docs/families/octeon-tx.md) | `177d:a300` | `xgs1us` | `octep` | yes | **XGS 3300** | **management link, ping 0% loss. Handshake completes and gates NetAgent, which transacts and answers. A front port is raised and its link read back; a 10G fibre between the two SFP+ cages trains. Frames posted on an SDP ring leave a front port and cross that fibre - 5,567 of them, 8.2 MB, confirmed by watching both cage activity LEDs blink while streaming and stop together during a five-second silence. Nothing returns on the output queue** |
| [OCTEON TX2](docs/families/octeon-tx2.md) | `177d:b200` | `xgs1ul`, `xgs1ul_4x80`, `xgs2u`, `xgs2ub` | none | no | no | nothing - documented only |
| [OCTEON TX2 98XX](docs/families/octeon-tx2-98xx.md) | `177d:b100` | shares the TX2 platforms | none | no | no | nothing - documented only |
| [TOPAZ](docs/families/topaz.md) | `Atom C11` in `/proc/cpuinfo` | - | not needed | - | no | no coprocessor exists |
| [GR](docs/families/gr.md) | `Atom` **and** `P69` | `AMDA0004-*` | not needed | - | no | no coprocessor exists |

**The two drivers are not at the same stage, and the table says so.** `npuep` carries a datapath;
`octep` brings up a management link, completes the SDP handshake, drives NetAgent, raises a front
port and reads its link back, and gets frames out of a front port - but nothing is received back,
so it has no usable front-port interface. Both are built on the appliance against the running
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
  host drives an SDP datapath ring, and frames posted on it leave a front port and cross a fibre -
  but nothing is ever received back, so no front port is usable as an interface yet.

Everything below in this section is about the XGS 136 and the ARMADA reset tables.

The per-board reset values are a table, not a constant — the polarity is inverted between board
generations — so the module reads the assembly number out of the bridge's own EEPROM and looks it
up. Values are carried for AMDA0200, AMDA0201 (XGS 126/136), AMDA0202-0205 - AMDA0202 being the
XGS 3300's own OCTEON TX assembly, carried here because the reset table is per assembly and not
per family - AMDA0208 (XGS 116) and AMDA0224 (XGS 138). **Only AMDA0201 has been tested on real
hardware.** The others come from the vendor tool and should be treated as unverified.

## 📦 Installing

> [!IMPORTANT]
> **The installer is ARMADA only.** `install/install.sh` installs `npuep` and its boot hooks, and
> does nothing at all for an OCTEON TX board. `octep` is **not packaged and not installed by
> anything** - it is built on the appliance and loaded by hand, on purpose, because its handshake
> has a consequence that should not happen at boot without somebody choosing it. See
> [docs/families/octeon-tx.md](docs/families/octeon-tx.md).

```sh
git clone https://github.com/AbdelmonemAwad/os-xgs-npu
cd os-xgs-npu
./install/install.sh
```

The installer puts in place two early boot hooks and, if a module has been built, installs it:

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
| [hardware.md](docs/hardware.md) | What is actually on the board, measured |
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
