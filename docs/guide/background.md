<!-- reviewed: 2026-10-17 -->
# Background, and what the two appliances are

*Moved here from the [README](../../README.md) on 2026-10-18, word for word, when the README was
made shorter. Struck claims stay struck, with what replaced them beside them; links were only
re-pointed to work from this folder.*

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
quietly dropped &mdash; see [docs/the-road.md](../the-road.md) for the shape of it, and
[docs/measurements/](../measurements/) for every number with how it was taken.

**Several related projects belong to the same effort** and are not here yet. They will be published
in full, on the same terms: measured first, documented as they are, with what did not work kept
beside what did.

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
> LEDs are lit. ~~What is **not** there is performance work - no zero copy, no batching, no offload -
> and persistence: nothing survives a reboot without being brought up again by hand.~~ That was
> 2026-10-01. Since then the driver brings the front ports up at boot itself
> ([08-octep](../../src/etc/rc.syshook.d/early/08-octep)); a connection `pf` has passed is forwarded by
> the coprocessor without the host, and IPsec runs through the coprocessor's engine under the
> kernel's own offload contract, both below; and the path through the host carries one stream at
> 755 to 797 Mbit/s and four at 947 to 967. What is still not there: every frame that crosses the
> host is copied once each way, into an mbuf on receive and into the ring's buffer on transmit. Four further
> families are described from the vendor's own tables with **no hardware at all**; see
> [Families](families-and-hardware.md#-families), where every row says which is
> which. Values for untested assemblies are carried in the tree and marked as untested wherever
> they appear.
