<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/readme/banner-dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="docs/images/readme/banner-light.svg">
  <img src="docs/images/readme/banner-light.svg" alt="os-xgs-npu - Sophos XGS hardware, back as OPNsense firewalls" width="100%">
</picture>

> **⚠ WORK IN PROGRESS, 2026-10-14: the XGS 136's driver, hooks and hardware support are being integrated into this repository and its plugins right now. Nothing here is advised for any use or any test on either appliance - the XGS 136 or the XGS 3300 - until the integration and its testing are finished and this notice is removed.**

<p align="center">
  <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/checks.yml"><img alt="checks: syntax, nothing private, docs" src="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/checks.yml/badge.svg"></a>
  <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/upstream.yml"><img alt="upstream: watches for a new OPNsense release" src="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/upstream.yml/badge.svg"></a>
  <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/docs-review.yml"><img alt="docs review: pages past their review date" src="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/docs-review.yml/badge.svg"></a>
  <br>
  <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/releases"><img alt="the newest release, a pre-release while in alpha" src="https://img.shields.io/github/v/release/AbdelmonemAwad/os-xgs-npu?include_prereleases&amp;sort=semver&amp;style=flat-square&amp;label=release&amp;color=8250df"></a>
  <a href="CHANGELOG.md"><img alt="changelog: written from the commits" src="https://img.shields.io/badge/changelog-from%20the%20commits-8250df.svg?style=flat-square"></a>
  <a href="LICENSE"><img alt="licence: BSD-2-Clause" src="https://img.shields.io/badge/licence-BSD--2--Clause-blue.svg?style=flat-square"></a>
  <a href="#-experimental"><img alt="status: experimental" src="https://img.shields.io/badge/status-EXPERIMENTAL-critical.svg?style=flat-square"></a>
  <a href="docs/guide/families-and-hardware.md#-families"><img alt="families: 2 of 6 with hardware" src="https://img.shields.io/badge/families-2%20of%206%20with%20hardware-555555.svg?style=flat-square"></a>
  <a href="docs/families/README.md"><img alt="platforms: U-Boot for all 9" src="https://img.shields.io/badge/platforms-U--Boot%20for%20all%209-555555.svg?style=flat-square"></a>
  <a href="contrib/"><img alt="drivers: npuep and octep" src="https://img.shields.io/badge/drivers-npuep%20%2B%20octep-555555.svg?style=flat-square"></a>
  <a href="CONTRIBUTING.md"><img alt="kernel modules are built on the appliance" src="https://img.shields.io/badge/kernel%20modules-built%20on%20the%20appliance-important.svg?style=flat-square"></a>
</p>

Install OPNsense on a Sophos XGS and it boots to a working firewall with **no network interfaces at
all**. The appliance looks like one computer and is two: an x86 host, and a coprocessor behind a PCIe
endpoint that owns every front port. Sophos ships drivers for its own operating system and for Linux
only, so when support lapses the usual verdict on the hardware is e-waste.

**os-xgs-npu** is a pair of FreeBSD kernel modules, written against registers no vendor documents, and
an OPNsense plugin around them. They hand the front ports back - and then hand the coprocessor the
firewall's own connections and IPsec associations, so its fast path forwards them without the host.
Two of the six coprocessor families answer now, on silicon that shares nothing.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/readme/status-dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="docs/images/readme/status-light.svg">
  <img src="docs/images/readme/status-light.svg" alt="os-xgs-npu status: the stage it is at, one card per appliance with what has been tested and measured on it and its weighted completion, the families without hardware, and the figures that keep it honest" width="100%">
</picture>

## At a glance

- **Two families, every front port.** The XGS 3300 (OCTEON TX) has all twelve and its expansion
  module's; the XGS 136 (ARMADA) has all fourteen - each an ordinary FreeBSD interface, named as the
  chassis names it.
- **pf decides, the coprocessor forwards.** A connection the firewall has passed goes into the
  coprocessor's flow table, IPv4 with its translation and IPv6 as it is, and comes back out the
  moment pf lets go of it. Each driver sizes its table by what the board itself says it holds -
  2,000,000 connections on the XGS 3300, 1,500,000 on the XGS 136
  ([board-limits.md](docs/families/board-limits.md)).
- **IPsec in the coprocessor's engine.** An association strongSwan installs through OPNsense lands on
  the engine with nothing typed: AES-GCM, AES-CBC with an HMAC, NAT traversal, IPv6 inside and out.
- **It installs like part of OPNsense.** One section of the menu, every page in Arabic and English, the
  front ports assigned at the first boot, updates from this repository, and
  [xgs-image-builder](https://github.com/AbdelmonemAwad/xgs-image-builder) for an image with all of it
  inside.
- **Kept honest.** Every figure is on a page with how it was taken and a photograph of the run; a
  claim that turned out wrong stays on its page, struck through, beside what replaced it
  ([the-road.md](docs/the-road.md)).

## Measured

Every badge links to the page that holds its number, and the page names the module build.

<table>
<tr>
<th width="14%"></th>
<th width="43%">XGS 136 &nbsp;&middot;&nbsp; ARMADA CN913x &nbsp;&middot;&nbsp; <code>contrib/npuep</code></th>
<th width="43%">XGS 3300 &nbsp;&middot;&nbsp; OCTEON TX CN83xx &nbsp;&middot;&nbsp; <code>contrib/octep</code></th>
</tr>
<tr>
<td valign="top"><b>platform</b></td>
<td valign="top">

<a href="docs/guide/what-works.md"><img alt="XGS 136: 14 of 14 front ports" src="https://img.shields.io/badge/XGS%20136%20(AMDA0201)-14%2F14%20front%20ports-brightgreen.svg?style=flat-square"></a><br>
<a href="compat.json"><img alt="OPNsense 26.7.7" src="https://img.shields.io/badge/OPNsense-26.7.7-d94f00.svg?style=flat-square"></a><br>
<a href="compat.json"><img alt="FreeBSD 15.1-RELEASE-p4" src="https://img.shields.io/badge/FreeBSD-15.1--RELEASE--p4-ab2b28.svg?style=flat-square"></a><br>
<a href="docs/families/armada.md#building-and-running-it"><img alt="kernel 26.7-n284159, followed across the update" src="https://img.shields.io/badge/kernel-26.7--n284159%20followed-6e5494.svg?style=flat-square"></a><br>
<a href="docs/families/armada.md#the-coprocessor-from-the-host"><img alt="coprocessor: console, tools, switch read from the host" src="https://img.shields.io/badge/coprocessor-console%20%C2%B7%20tools%20%C2%B7%20switch%20read-brightgreen.svg?style=flat-square"></a>

</td>
<td valign="top">

<a href="docs/families/octeon-tx.md"><img alt="XGS 3300: 12 of 12 front ports" src="https://img.shields.io/badge/XGS%203300%20(AMDA0202)-12%2F12%20front%20ports-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/measurements/xgs3300.md#the-machine"><img alt="OPNsense 26.7.7" src="https://img.shields.io/badge/OPNsense-26.7.7-d94f00.svg?style=flat-square"></a><br>
<a href="docs/measurements/xgs3300.md#the-machine"><img alt="FreeBSD 15.1-RELEASE-p4" src="https://img.shields.io/badge/FreeBSD-15.1--RELEASE--p4-ab2b28.svg?style=flat-square"></a><br>
<a href="docs/measurements/xgs3300.md#a-kernel-update-must-not-cost-the-front-ports"><img alt="kernel 26.7-n284159, its module built for it" src="https://img.shields.io/badge/kernel-26.7--n284159%20module%20built%20for%20it-6e5494.svg?style=flat-square"></a>

</td>
</tr>
<tr>
<td valign="top"><b>data path</b></td>
<td valign="top">

<a href="docs/measurements/mtu-2026-10-17.md#the-xgs-136-over-the-gigabit-link"><img alt="MTU 9000 through both switch chips: one stream 989 Mbit/s clear, 982 through the IPsec tunnel" src="https://img.shields.io/badge/MTU%209000-989%20clear%20%C2%B7%20982%20IPsec%20Mbit%2Fs-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/findings/a-hundred-polls-a-second-on-the-armada.md"><img alt="host receive: one TCP stream 773 Mbit/s in and 934 out, was 18.3 and 347 with a 10 ms poll; issue 415" src="https://img.shields.io/badge/host%20receive-773%20in%20%C2%B7%20934%20out%20Mbit%2Fs%20(was%2018)%20%C2%B7%20%23415-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/findings/one-next-hop-for-many-flows-on-the-armada.md"><img alt="flow offload: the driver's sweep put 11 connections in hardware over 2 next-hop entries" src="https://img.shields.io/badge/flow%20offload-11%20connections%20%C2%B7%202%20next%20hops%2C%20by%20the%20sweep-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/findings/the-revision-barrier-on-the-armada.md"><img alt="ruleset reload: every offloaded flow discarded by the revision barrier" src="https://img.shields.io/badge/ruleset%20reload-every%20flow%20discarded%20at%20once-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/families/board-limits.md"><img alt="flow table: 1,500,000 connections, the size the board itself states" src="https://img.shields.io/badge/flow%20table-1%2C500%2C000%20connections%2C%20the%20board%27s-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/measurements/ipv6-and-queues-2026-10-18.md#the-xgs-136s-four-transmit-queues"><img alt="transmit: four queues in use, 941 Mbit/s, the link full" src="https://img.shields.io/badge/transmit-4%20queues%20in%20use%20%C2%B7%20941%20Mbit%2Fs-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/families/board-limits.md"><img alt="IPv6 flows: built, not yet measured through the XGS 136" src="https://img.shields.io/badge/IPv6%20flows-built%2C%20not%20yet%20measured-yellow.svg?style=flat-square"></a>

</td>
<td valign="top">

<a href="docs/measurements/xgs3300.md#the-clear-text-path-at-a-gigabit-through-the-host-and-with-the-flow-table"><img alt="host path: 957 / 964 Mbit/s plain, four streams" src="https://img.shields.io/badge/host%20path-957%20%2F%20964%20Mbit%2Fs%20plain%2C%20four%20streams-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/measurements/xgs3300.md#the-clear-text-path-at-a-gigabit-through-the-host-and-with-the-flow-table"><img alt="flow offload: 99.6 percent of frames in hardware" src="https://img.shields.io/badge/flow%20offload-99.6%20%25%20of%20frames%20in%20hardware-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/measurements/mtu-2026-10-17.md"><img alt="jumbo frames through the switch chip: 10,240 bytes on all ten ports, written at every boot" src="https://img.shields.io/badge/switch%20ports-jumbo%2010%2C240%20at%20every%20boot-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/measurements/xgs3300.md#the-mtu-from-72-to-9000-on-the-spot"><img alt="MTU: 72 to 9,000 per port, on the spot" src="https://img.shields.io/badge/MTU-72%20to%209%2C000%20per%20port-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/findings/the-sensors-answer-on-the-host-bus.md"><img alt="sensors: temperatures and fans, read from the host" src="https://img.shields.io/badge/sensors-CPU%20%C2%B7%20NPU%20%C2%B7%20inlet%20%C2%B7%20fans%2C%20from%20the%20host-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/measurements/ipv6-and-queues-2026-10-18.md#ipv6-through-the-xgs-3300"><img alt="IPv6 flows: 9.24 and 9.27 Gbit/s, 18 of 18 directions in hardware" src="https://img.shields.io/badge/IPv6%20flows-9.24%20%C2%B7%209.27%20Gbit%2Fs%2C%2018%2F18%20in%20hardware-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/families/board-limits.md"><img alt="flow table: 2,000,000 connections, the size the board itself states" src="https://img.shields.io/badge/flow%20table-2%2C000%2C000%20connections%2C%20the%20board%27s-brightgreen.svg?style=flat-square"></a>

</td>
</tr>
<tr>
<td valign="top"><b>IPsec</b></td>
<td valign="top">

<a href="docs/findings/the-engine-encrypts-outbound-esp-on-the-armada.md"><img alt="IPsec offload: AES-GCM both ways in hardware, 20 of 20 in, 10 of 10 out, 10 of 10 forwarded" src="https://img.shields.io/badge/IPsec%20offload-AES--GCM%20both%20ways%20%C2%B7%2020%2F20%20in%20%C2%B7%2010%2F10%20out%20%C2%B7%2010%2F10%20forwarded-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/findings/the-host-answers-through-the-tunnel-on-the-armada.md"><img alt="receive termination: every decrypted frame terminated, none refused" src="https://img.shields.io/badge/receive%20termination-70%20of%2070%20frames%2C%20none%20refused-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/findings/fragmentation-before-the-envelope-on-the-armada.md"><img alt="fragmentation before the envelope: 1,500-byte packets, 5 of 5 from the appliance and 10 of 10 forwarded, DF answered" src="https://img.shields.io/badge/fragmentation-1%2C500--byte%20packets%20%C2%B7%205%2F5%20%C2%B7%2010%2F10%20forwarded%20%C2%B7%20DF%20answered-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/findings/a-window-on-the-armada.md"><img alt="replay window: the engine's check, a replayed frame dropped as a duplicate and as out of window" src="https://img.shields.io/badge/replay%20window-32%20to%204%2C096%20on%20the%20engine%20%C2%B7%20replayed%20frame%20dropped-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/findings/esp-behind-udp-on-the-armada.md"><img alt="NAT-T: ESP inside UDP 4500 both ways in hardware, 10 of 10 from the peer, 10 of 10 and 5 of 5 fragmented forwarded" src="https://img.shields.io/badge/NAT--T-ESP%20in%20UDP%204500%20both%20ways%20%C2%B7%2010%2F10%20%C2%B7%205%2F5%20fragmented-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/findings/a-block-cipher-on-the-armada.md"><img alt="AES-CBC with HMAC SHA-1/256/384/512: taken both ways in hardware, 10 of 10 from the peer each" src="https://img.shields.io/badge/AES--CBC%20%2B%20HMAC-SHA--1%2F256%2F384%2F512%20both%20ways%20%C2%B7%2010%2F10%20each-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/findings/amd-ccp-breaks-ipsec-gcm-on-the-xgs-136-host.md#the-fix-2026-10-17"><img alt="ccp: the host's AMD crypto engine failed software AES-GCM; IPsec now held to aesni at boot, 903 Mbit/s in software with ccp loaded; issue 399" src="https://img.shields.io/badge/ccp-IPsec%20held%20to%20aesni%20%C2%B7%20903%20Mbit%2Fs%20%C2%B7%20%23399-brightgreen.svg?style=flat-square"></a>

</td>
<td valign="top">

<a href="docs/measurements/mtu-2026-10-17.md"><img alt="IPsec, the engine at both ends: one stream 7.41 Gbit/s at MTU 9000 into an XGS 3300" src="https://img.shields.io/badge/IPsec%2C%20engine%20at%20both%20ends-7.41%20Gbit%2Fs%20one%20stream%20at%20MTU%209000-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/measurements/xgs3300.md#a-ring-behind-the-block-build-by-build"><img alt="IPsec offload: AES-GCM both ways, 951 down / 946 up Mbit/s" src="https://img.shields.io/badge/IPsec%20offload-AES--GCM%20both%20ways%20%C2%B7%20951%20%2F%20946%20Mbit%2Fs-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/findings/the-flow-carries-the-association.md"><img alt="flows with their association: one stream 680 to 750 Mbit/s with the host out of its data path" src="https://img.shields.io/badge/flows%20%2B%20IPsec-one%20stream%20680%E2%80%93750%20Mbit%2Fs%20in%20hardware-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/measurements/xgs3300.md#an-association-behind-nat-on-the-coprocessor"><img alt="NAT-T: at the plain tunnel's rates" src="https://img.shields.io/badge/NAT--T-at%20the%20plain%20tunnel%27s%20rates-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/measurements/xgs3300.md#aes-cbc-with-an-hmac-on-the-coprocessor"><img alt="AES-CBC with HMAC SHA-1/256/384/512: two thirds of GCM's rate" src="https://img.shields.io/badge/AES--CBC%20%2B%20HMAC-SHA--1%2F256%2F384%2F512%20%C2%B7%20%E2%85%94%20of%20GCM-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/measurements/xgs3300.md#an-ipv6-outer-header"><img alt="IPv6: outer header and inner packets" src="https://img.shields.io/badge/IPv6-outer%20and%20inner-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/measurements/xgs3300.md#replay-windows-other-than-32"><img alt="replay window: up to 4,096" src="https://img.shields.io/badge/replay%20window-up%20to%204%2C096-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/measurements/xgs3300.md#an-association-with-extended-sequence-numbers"><img alt="ESN: outbound only; inbound stays with the kernel" src="https://img.shields.io/badge/ESN-outbound%20only-yellow.svg?style=flat-square"></a><br>
<a href="docs/findings/gcm-12-and-transport-are-not-ours.md"><img alt="not offloaded: GCM-8 and GCM-12, transport mode, ESN inbound" src="https://img.shields.io/badge/not%20offloaded-GCM--8%2F12%20%C2%B7%20transport%20%C2%B7%20ESN%20in-lightgrey.svg?style=flat-square"></a>

</td>
</tr>
<tr>
<td valign="top"><b>reliability</b></td>
<td valign="top">

<sub>no long soak under load on this board yet - <a href="docs/families/armada.md#where-this-stands">row 7</a></sub>

</td>
<td valign="top">

<a href="docs/measurements/xgs3300.md#two-hours-of-tls-on-the-failures-own-path-translated-through-the-host"><img alt="integrity soaks: 2,391 GiB over TLS, 0 bytes changed" src="https://img.shields.io/badge/integrity%20soaks-2%2C391%20GiB%20over%20TLS%20%C2%B7%200%20bytes%20changed-brightgreen.svg?style=flat-square"></a><br>
<a href="docs/measurements/xgs3300.md#three-hours-of-tls-on-the-clear-text-path-with-the-trigger-on-and-then-off"><img alt="give-backs: 6,100 revived in place, 0 refused" src="https://img.shields.io/badge/give--backs-6%2C100%20revived%20%C2%B7%200%20refused-brightgreen.svg?style=flat-square"></a><br>
<a href="https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361"><img alt="open: an idle ring a burst finds, issue 361" src="https://img.shields.io/badge/open-an%20idle%20ring%20a%20burst%20finds%20%28%23361%29-orange.svg?style=flat-square"></a>

</td>
</tr>
</table>

<p align="center"><sub>colours: <strong>green</strong> measured and working &middot; <strong>yellow</strong> partial, the link says which half &middot; <strong>light grey</strong> not offloaded, by whose refusal the link says &middot; <strong>orange</strong> an open fault &middot; <strong>dark grey</strong> a fact about the repository &middot; the version badges in their projects' colours. A badge is corrected the day a measurement contradicts it, the old figure struck beside the new in the section it links to, like everything else here.</sub></p>

## Families

| family | coprocessor | boards | driver | hardware here | state |
|---|---|---|---|---|---|
| [ARMADA](docs/families/armada.md) | Marvell CN913x, `11ab:7080` | AMDA0200 XGS 87/107, AMDA0201 XGS 126/136, AMDA0208 XGS 116, AMDA0224 XGS 138 | `npuep` | **XGS 136** | all 14 front ports, flow and IPsec offload |
| [OCTEON TX](docs/families/octeon-tx.md) | Cavium CN83xx, `177d:a300` | AMDA0202 XGS 2100/2300/3100/3300 | `octep` | **XGS 3300** | all 12 front ports and the module's, flow and IPsec offload |
| [OCTEON TX2](docs/families/octeon-tx2.md) | `177d:b200` | the larger boards | none | no | described from the vendor's binaries |
| [OCTEON TX2 98XX](docs/families/octeon-tx2-98xx.md) | `177d:b100` | the largest boards | none | no | described from the vendor's binaries |
| [TOPAZ](docs/families/topaz.md), [GR](docs/families/gr.md) | none | Atom boards | not needed | no | no coprocessor exists |

What has run on which board, and why there are two drivers and not one with a switch in it, is in
[the families page](docs/guide/families-and-hardware.md). **What one appliance has and the other does
not** - the same features listed for both, so every gap shows - is [parity.md](docs/families/parity.md).

## ⚠ Experimental

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

## Installing

One installer reads the board's assembly number and installs only that board's pieces; any other
board gets nothing. The kernel module is built on the appliance against the running kernel's own
sources and stamped with it.

```sh
git clone https://github.com/AbdelmonemAwad/os-xgs-npu
cd os-xgs-npu
./install/install.sh
```

Then **XGS Coprocessor** in the menu: Overview, Settings, Ports, Transceivers, IPsec, Offloaded Flows,
Updates, Problem Reports and the log. Updates come from the [releases](https://github.com/AbdelmonemAwad/os-xgs-npu/releases),
not from every change merged. Everything about installing, the boot hooks, the pages, updating
and the first boot is in [docs/guide/installing.md](docs/guide/installing.md) - read its note on DNS
before blaming the driver for a network with no internet.

## Documentation

| | |
|---|---|
| [**docs/README.md**](docs/README.md) | **Start here**: the documentation by what you want to do, and every folder by what it holds |
| [docs/INDEX.md](docs/INDEX.md) | Every page, one line each, with the date it was last reviewed and when it is due again ([how pages are reviewed](docs/REVIEW.md)) |
| [docs/guide/](docs/guide/) | What used to be this file, at full length: [what works](docs/guide/what-works.md), [how it works](docs/guide/how-it-works.md), [what it cannot do](docs/guide/limitations.md), [families and hardware](docs/guide/families-and-hardware.md), [installing](docs/guide/installing.md), [background](docs/guide/background.md), [every badge](docs/guide/measured.md) |
| [DESIGN.md](DESIGN.md) | The contract: the stages, the limits that are properties of the hardware, and every claim that turned out to be wrong |
| [docs/measurements/](docs/measurements/) | The numbers, with how each was taken and a photograph of the run |
| [docs/findings/](docs/findings/) | What was found, dated, one page per fault or discovery |
| [CHANGELOG.md](CHANGELOG.md) | What changed in each release, written from the commits; [RELEASING.md](RELEASING.md) says when a release is made - at once for a fix, gathered for improvements |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Building, testing on an appliance, and what a change has to carry |

## ⚖️ Licence

**BSD-2-Clause.** Every file carries the identifier; [LICENSE](LICENSE) is the whole of it.

The protocol it speaks was recovered from Marvell's GPL-2.0-only sources, published by Sophos in
its SFOS_OSS drop, and from binaries the appliance ships. **No vendor code is included or
redistributed here.** Facts — offsets, command numbers, field widths — are transcribed; expression
is not copied, and `contrib/npuep/npugiu.h` is where that decision is visible and argued.

[**docs/provenance.md**](docs/provenance.md) sets out exactly what was read, from where, which four
sentences are quoted verbatim and why, and what is deliberately absent. Read it before reusing
this, and form your own view.

### Copyright, trademarks and credit

Copyright &copy; 2026 Abdelmonem Awad. **OPNsense&reg;** is a registered trademark of Deciso B.V.;
**Sophos&reg;** and **XGS** are trademarks of Sophos Ltd. This project is independent: it is not
affiliated with, endorsed by or sponsored by either, and their names are used only to say what this
software runs on.

Each page the plugin adds ends with a one-line credit - the project, its author, its licence, and that
it is independent. OPNsense's own footer, with Deciso's copyright, is left exactly as it is.
