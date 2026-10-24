<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/readme/banner-dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="docs/images/readme/banner-light.svg">
  <img src="docs/images/readme/banner-light.svg" alt="os-xgs-npu - Sophos XGS hardware, back as OPNsense firewalls" width="100%">
</picture>

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/readme/notice-dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="docs/images/readme/notice-light.svg">
  <img src="docs/images/readme/notice-light.svg" alt="Update OPNsense now - PHP2Shell: update to PHP 8.5.12 or later, test appliances included" width="100%">
</picture>

> [!CAUTION]
> **Update OPNsense now, on every appliance - a test one too. PHP2Shell, 2026-10-21.** PHP fixed two
> critical flaws, CVE-2026-109618 and CVE-2026-109619, which [php.net](https://www.php.net/archive/2026.php)
> says can allow remote code execution without a login, and OPNsense's web interface runs on PHP.
> OPNsense delivers the fix, **PHP 8.5.12**, to 26.7.7 as an out-of-band package update, so **the
> version still reads 26.7.7 afterwards** ([its announcement](https://forum.opnsense.org/index.php?topic=53191.0)):
> on 2026-10-25 one of this project's own test appliances still ran PHP 8.5.11 under "OPNsense 26.7.7".
>
> - **Update:** System: Firmware: Status - *Check for updates*, then *Update*.
> - **Check:** in a shell (the console's option 8, or SSH), `php -v` reads **8.5.12** or later.
> - **The captive portal**, if you run it: restart it after the update, as OPNsense advises.
> - **The [ready-made images](https://github.com/AbdelmonemAwad/xgs-image-builder/releases)** start from
>   OPNsense 26.7's own image and carry its older PHP: update right after installing, before the web
>   interface can be reached from a network you do not trust.

> [!NOTE]
> **Still being completed.** The main part of the work is done. Some functions are being improved, and
> the XGS-specific integration is being completed where something is still missing - each gap is on
> [docs/families/parity.md](docs/families/parity.md) - and it all arrives as updates, through the
> [releases](https://github.com/AbdelmonemAwad/os-xgs-npu/releases).

> [!IMPORTANT]
> **New discoveries, 2026-10-22 - and why the completion figures went down.** A sweep of the vendor's
> own sources and the coprocessor's own log found capabilities of this hardware that were not known: a
> deep packet inspection and threat detection engine on the XGS 3300's coprocessor, a transmit rate
> limit for each front port, the TCP window checked in hardware - which also showed that a connection
> in hardware has its TCP window checked nowhere ~~today~~ - and more. They matter a great deal, so the
> figures now count them: the XGS 3300 ~~97%~~ ~~89%~~ **90%**, the XGS 136 ~~96%~~ ~~91%~~ ~~92%~~ **93%** - up
> again the same evening, once the first of them was done: **the coprocessor checks the TCP window of a
> connection it holds** ([measured](docs/measurements/tcp-window-2026-10-22.md)) - and the second:
> **the host knows within 30 seconds when its fast path stops** - on the XGS 3300 by a heartbeat it
> answers every time, on the XGS 136 by the heartbeat taken off its queue - reports it, and restarts
> the appliance if its owner chose that
> ([measured](docs/measurements/heartbeat-2026-10-22.md)) - and on 2026-10-23 the third: **the XGS
> 3300's coprocessor is reached over its PCIe management link instead of its console**, one command
> in 0.77 seconds instead of 11 ([measured](docs/measurements/mgmt-link-2026-10-23.md)). **They will be
> worked on gradually and arrive as updates.** Every one of them, with a drawing of where it lives:
> [the discoveries of 2026-10-22](docs/findings/discoveries-2026-10-22.md), ticked off in
> [680](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/680).

<p align="center">
  <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/checks.yml"><img alt="checks: syntax, nothing private, docs" src="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/checks.yml/badge.svg"></a>
  <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/upstream.yml"><img alt="upstream: watches for a new OPNsense release" src="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/upstream.yml/badge.svg"></a>
  <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/docs-review.yml"><img alt="docs review: pages past their review date" src="https://github.com/AbdelmonemAwad/os-xgs-npu/actions/workflows/docs-review.yml/badge.svg"></a>
  <br>
  <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/releases"><img alt="the newest release; alpha while the number says so" src="https://img.shields.io/github/v/release/AbdelmonemAwad/os-xgs-npu?include_prereleases&amp;sort=semver&amp;style=flat-square&amp;label=release&amp;color=8250df"></a>
  <a href="CHANGELOG.md"><img alt="changelog: written from the commits" src="https://img.shields.io/badge/changelog-from%20the%20commits-8250df.svg?style=flat-square"></a>
  <a href="LICENSE"><img alt="licence: BSD-2-Clause" src="https://img.shields.io/badge/licence-BSD--2--Clause-blue.svg?style=flat-square"></a>
  <a href="#getting-it-onto-an-appliance"><img alt="status: alpha, being completed" src="https://img.shields.io/badge/status-alpha%2C%20being%20completed-orange.svg?style=flat-square"></a>
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
  <source media="(prefers-color-scheme: dark)" srcset="https://npuforge.org/readme/status-dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="https://npuforge.org/readme/status-light.svg">
  <img src="https://npuforge.org/readme/status-light.svg" alt="os-xgs-npu status: the stage it is at, one card per appliance with what has been tested and measured on it and its weighted completion, the families without hardware, and the figures that keep it honest" width="100%">
</picture>

<p align="center"><b>Contents:</b> <a href="#at-a-glance">At a glance</a> &middot; <a href="#the-pages">The pages</a> &middot; <a href="#measured">Measured</a> &middot; <a href="#families">Families</a> &middot; <a href="#getting-it-onto-an-appliance">Getting it onto an appliance</a> &middot; <a href="#documentation">Documentation</a> &middot; <a href="#licence">Licence</a><br>
<b>Also:</b> <a href="https://npuforge.org"><b>npuforge.org</b>, the site</a> &middot; <a href="docs/README.md">the documentation</a> &middot; <a href="docs/images/README.md">every image</a> &middot; <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/wiki">the wiki</a> &middot; <a href="CHANGELOG.md">what changed</a></p>

## At a glance

- **Two families, every front port.** The XGS 3300 (OCTEON TX) has all twelve and its expansion
  module's; the XGS 136 (ARMADA) has all fourteen - each an ordinary FreeBSD interface, named as the
  chassis names it.
- **pf decides, the coprocessor forwards.** A connection the firewall has passed goes into the
  coprocessor's flow table, IPv4 with its translation and IPv6 as it is, and comes back out the
  moment pf lets go of it - or a block list gains its address, which cuts it
  ([measured](docs/measurements/lists-cut-2026-10-19.md)), or the Intrusion Detection logs it with a
  listed TLS fingerprint (JA4), which cuts it too ([measured](docs/measurements/ja4-cut-2026-10-19.md)) -
  dropped on the coprocessor itself first, by its verdict, and kept there while its sender goes on,
  so a cut flood never reaches the host ([measured](docs/measurements/verdict-2026-10-23.md)).
  Both coprocessors count each connection they forward, and the drivers read those counts:
  with pf's, they are what crossed the wire, and given to pf they put the whole connection in its
  state and on its rule, where OPNsense's own pages show it ([XGS 3300](docs/measurements/hw-counters-2026-10-19.md),
  [XGS 136](docs/measurements/xgs136-counts-2026-10-19.md#the-reader-measured)). Each driver sizes its table by what the board itself says it holds -
  2,000,000 connections on the XGS 3300, 1,500,000 on the XGS 136
  ([board-limits.md](docs/families/board-limits.md)). A DSCP a normalization rule sets goes into
  hardware with the connection: the coprocessor writes pf's mark into every frame
  ([measured on both](docs/measurements/flow-dscp-2026-10-24.md)). A connection a bridge carries
  between two of an appliance's ports goes into hardware as a bridged flow, its frames' MACs and TTL
  left as they are ([measured on both](docs/measurements/bridged-2026-10-24.md)). A connection routed
  by a VLAN subinterface goes into hardware too, the coprocessor writing its tag
  ([measured on both](docs/measurements/vlan-2026-10-24.md)).
- ~~**An IPS or the offload, for now.** Suricata sees how a connection in hardware starts and nothing
  after, so its inline IPS and the offload are one or the other until the two are measured together:
  what it must see in full goes in **Never offloaded** on XGS: Settings, or acceleration stays off.
  Whether they can work together is under research~~
  **An IPS and the offload together, as an experiment.** With **Inspect, then hardware** on XGS:
  Settings, OPNsense's Intrusion Detection in IPS mode sees every frame of a new connection the
  coprocessor carries until it has let its first megabyte through, drops what its rules drop, and the
  hardware carries the rest - 941 Mbit/s on the XGS 136, the link full, and 3.98 Gbit/s on the XGS
  3300 ([measured](docs/measurements/inspect-2026-10-23.md#the-setting-and-the-intrusion-detection-service)).
  Off by default; without it, what the IPS must see in full goes in **Never offloaded**, or
  acceleration stays off ([the FAQ](docs/guide/faq.md#what-runs-in-the-coprocessors-hardware)).
- **Captive Portal clients in hardware.** With the setting on XGS: Settings, each portal session is a
  live user of the coprocessor: its connections run in hardware, and a logout, a hard or an idle
  timeout takes them out at once. What the coprocessor carries is added to the portal's own counts,
  so its idle timeout and RADIUS accounting see it - by an overlay on one of the portal's files,
  changed only on the versions it knows ([measured on both](docs/measurements/cp-hardware-2026-10-24.md)).
  Off by default.
- **Nothing leaves an appliance unless its owner chooses.** Problem reports wait for their Send and
  the appliance is not counted until its owner says so - XGS: Overview asks once
  ([problem-reports.md](docs/problem-reports.md#the-owner-decides)).
- **IPsec in the coprocessor's engine.** An association strongSwan installs through OPNsense lands on
  the engine with nothing typed: AES-GCM, AES-CBC with an HMAC, NAT traversal, IPv6 inside and out.
- **It installs like part of OPNsense.** One section of the menu, every page in Arabic and English, the
  front ports assigned at the first boot, updates from this repository, and
  [xgs-image-builder](https://github.com/AbdelmonemAwad/xgs-image-builder) for an image with all of it
  inside.
- **Kept honest.** Every figure is on a page with how it was taken and a photograph of the run; a
  claim that turned out wrong stays on its page, struck through, beside what replaced it
  ([the-road.md](docs/the-road.md)).

## The pages

<a href="docs/guide/the-pages.md"><img src="docs/images/screens/en/01-xgs-overview.png" alt="XGS: Overview on an XGS 3300: the front panel drawn with each port's link, cards for the coprocessor, hardware offloading, the IPsec engine, the front ports, the switch chip and the last restart, and the board's limits" width="100%"></a>

**XGS: Overview** on the second XGS 3300, photographed on the appliance itself on 2026-10-19, full
screen in the dark theme. Every page - ~~the nine~~ the ten of the **XGS** section (**About** since
2026-10-20, with the project, its author, its addresses and the disclaimer; the plugins' updates a tab
of **Updates**), the dashboard and the four plugins' - is photographed the same way, each with what it
is for, in
**[docs/guide/the-pages.md](docs/guide/the-pages.md)**; every image in the repository, the appliances'
photographs and the measurements' among them, is indexed in
**[docs/images/README.md](docs/images/README.md)**.

## Measured

Every badge links to the page that holds its number, and the page names the module build. One row is one feature, the same on both appliances, so what one has and the other does not shows in the row - the rows follow [parity.md](docs/families/parity.md).

<table>
<tr>
<th width="14%"></th>
<th width="43%">XGS 136 &nbsp;&middot;&nbsp; ARMADA CN913x &nbsp;&middot;&nbsp; <code>contrib/npuep</code></th>
<th width="43%">XGS 3300 &nbsp;&middot;&nbsp; OCTEON TX CN83xx &nbsp;&middot;&nbsp; <code>contrib/octep</code></th>
</tr>
<tr>
<td rowspan="9" valign="top"><b>platform</b></td>
<td><a href="docs/guide/what-works.md"><img alt="XGS 136: 14 of 14 front ports" src="https://img.shields.io/badge/XGS%20136%20(AMDA0201)-14%2F14%20front%20ports-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/families/octeon-tx.md"><img alt="XGS 3300: 12 of 12 front ports" src="https://img.shields.io/badge/XGS%203300%20(AMDA0202)-12%2F12%20front%20ports-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="compat.json"><img alt="OPNsense 26.7.7" src="https://img.shields.io/badge/OPNsense-26.7.7-d94f00.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/xgs3300.md#the-machine"><img alt="OPNsense 26.7.7" src="https://img.shields.io/badge/OPNsense-26.7.7-d94f00.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="compat.json"><img alt="FreeBSD 15.1-RELEASE-p4" src="https://img.shields.io/badge/FreeBSD-15.1--RELEASE--p4-ab2b28.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/xgs3300.md#the-machine"><img alt="FreeBSD 15.1-RELEASE-p4" src="https://img.shields.io/badge/FreeBSD-15.1--RELEASE--p4-ab2b28.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/families/armada.md#building-and-running-it"><img alt="kernel 26.7-n284159, followed across the update" src="https://img.shields.io/badge/kernel-26.7--n284159%20followed-6e5494.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/xgs3300.md#a-kernel-update-must-not-cost-the-front-ports"><img alt="kernel 26.7-n284159, its module built for it" src="https://img.shields.io/badge/kernel-26.7--n284159%20module%20built%20for%20it-6e5494.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/families/armada.md#the-coprocessor-from-the-host"><img alt="coprocessor: console, tools, switch read from the host" src="https://img.shields.io/badge/coprocessor-console%20%C2%B7%20tools%20%C2%B7%20switch%20read-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/families/octeon-tx-peripherals.md#the-cpld-is-driven-now-and-it-was-holding-two-ports-dark"><img alt="coprocessor: its CPLD driven and its sensors read from the host" src="https://img.shields.io/badge/coprocessor-CPLD%20and%20sensors%20from%20the%20host-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/families/parity.md#the-table"><img alt="first boot: the front ports assigned by the names on the chassis" src="https://img.shields.io/badge/first%20boot-ports%20assigned%20by%20the%20chassis%20names-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/families/parity.md#the-table"><img alt="first boot: the front ports assigned by the names on the chassis" src="https://img.shields.io/badge/first%20boot-ports%20assigned%20by%20the%20chassis%20names-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/guide/updating.md"><img alt="updated to v0.1.0-alpha.2 from its own Updates page in 16 seconds; the driver it built was the one running" src="https://img.shields.io/badge/updates-from%20the%20page%2C%2016%20s%2C%20no%20reboot%20needed-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/guide/updating.md"><img alt="updated to v0.1.0-alpha.2 from its own Updates page in 17 seconds, then rebooted onto the new driver" src="https://img.shields.io/badge/updates-from%20the%20page%2C%2017%20s%2C%20then%20a%20reboot-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/problem-reports.md"><img alt="problem reports: sent to GitHub only when the owner chooses, without addresses or serials" src="https://img.shields.io/badge/problem%20reports-sent%20only%20when%20you%20choose%2C%20without%20addresses-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/problem-reports.md"><img alt="problem reports: sent to GitHub only when the owner chooses, without addresses or serials" src="https://img.shields.io/badge/problem%20reports-sent%20only%20when%20you%20choose%2C%20without%20addresses-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/image-136-2026-10-21.md"><img alt="an image from the builder: built, installed and booted on an XGS 136, then updated and its configuration restored" src="https://img.shields.io/badge/image-built%20and%20booted-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/families/parity.md#the-table"><img alt="an image from the builder: built, installed and booted on an XGS 3300" src="https://img.shields.io/badge/image-built%20and%20booted-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td rowspan="4" valign="top"><b>ports</b></td>
<td><a href="docs/measurements/mtu-2026-10-17.md#the-xgs-136-over-the-gigabit-link"><img alt="MTU 9000 through both switch chips: one stream 989 Mbit/s clear, 982 through the IPsec tunnel" src="https://img.shields.io/badge/MTU%209000-989%20clear%20%C2%B7%20982%20IPsec%20Mbit%2Fs-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/xgs3300.md#the-mtu-from-72-to-9000-on-the-spot"><img alt="MTU: 72 to 9,000 per port, on the spot" src="https://img.shields.io/badge/MTU-72%20to%209%2C000%20per%20port-brightgreen.svg?style=flat-square"></a><br><a href="docs/measurements/mtu-2026-10-17.md"><img alt="jumbo frames through the switch chip: 10,240 bytes, written at every boot on the XGS 3300, set from each port's MTU by the network agent on the XGS 136" src="https://img.shields.io/badge/switch%20ports-jumbo%2010%2C240%3A%203300%20at%20boot%2C%20136%20by%20MTU-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/cage-medium-2026-10-19.md"><img alt="transceivers: two copper SFP-GE-T modules read on the coprocessor; the port reports 1000baseT, from the module" src="https://img.shields.io/badge/transceivers-copper%20SFP--GE--T%20x2%20%C2%B7%201000baseT-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/cage-medium-2026-10-19.md"><img alt="transceivers: seven modules read on the coprocessor with their diagnostics; each port reports its module's medium" src="https://img.shields.io/badge/transceivers-7%20modules%20%C2%B7%20diagnostics%20%C2%B7%2010Gbase--T%20or%20--SR-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/families/parity.md#the-table"><img alt="expansion bay: the host's, for a modem or Wi-Fi - no coprocessor ports" src="https://img.shields.io/badge/expansion%20bay-the%20host%27s%3A%20modem%20or%20Wi--Fi-inactive.svg?style=flat-square"></a></td>
<td><a href="docs/octep/expansion-modules.md"><img alt="expansion module: 4x10G, ports A1 to A4" src="https://img.shields.io/badge/expansion%20module-4x10G%2C%20ports%20A1%20to%20A4-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/families/parity.md#the-table"><img alt="port identify light: blinks" src="https://img.shields.io/badge/identify%20light-blinks-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/families/parity.md#the-table"><img alt="port identify light: blinks" src="https://img.shields.io/badge/identify%20light-blinks-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td rowspan="4" valign="top"><b>data path</b></td>
<td><a href="docs/findings/a-hundred-polls-a-second-on-the-armada.md"><img alt="host receive: one TCP stream 773 Mbit/s in and 934 out, was 18.3 and 347 with a 10 ms poll; issue 415" src="https://img.shields.io/badge/host%20receive-773%20in%20%C2%B7%20934%20out%20Mbit%2Fs%20(was%2018)%20%C2%B7%20%23415-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/xgs3300.md#the-clear-text-path-at-a-gigabit-through-the-host-and-with-the-flow-table"><img alt="host path: 957 / 964 Mbit/s plain, four streams" src="https://img.shields.io/badge/host%20path-957%20%2F%20964%20Mbit%2Fs%20plain%2C%20four%20streams-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/families/parity.md#the-table"><img alt="receive: polled every millisecond; the coprocessor has not been seen to raise an interrupt for a received frame" src="https://img.shields.io/badge/receive-polled%20every%20millisecond-yellow.svg?style=flat-square"></a></td>
<td><a href="docs/families/parity.md#the-table"><img alt="receive: an interrupt per ring" src="https://img.shields.io/badge/receive-an%20interrupt%20per%20ring-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/ipv6-and-queues-2026-10-18.md#the-xgs-136s-four-transmit-queues"><img alt="transmit: four queues in use, 941 Mbit/s, the link full" src="https://img.shields.io/badge/transmit-4%20queues%20in%20use%20%C2%B7%20941%20Mbit%2Fs-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/families/parity.md#the-table"><img alt="transmit: seven rings beside the first" src="https://img.shields.io/badge/transmit-seven%20rings%20beside%20the%20first-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/families/parity.md#the-table"><img alt="without a copy per frame: not yet, a copy each way" src="https://img.shields.io/badge/a%20copy%20per%20frame-still%20made%20each%20way-lightgrey.svg?style=flat-square"></a></td>
<td><a href="docs/families/parity.md#the-table"><img alt="without a copy per frame: not yet, a copy each way" src="https://img.shields.io/badge/a%20copy%20per%20frame-still%20made%20each%20way-lightgrey.svg?style=flat-square"></a></td>
</tr>
<tr>
<td rowspan="12" valign="top"><b>flow offload</b></td>
<td><a href="docs/measurements/xgs136-hardware-2026-10-18.md"><img alt="flow offload: a connection in hardware at 941 Mbit/s with the host's CPUs at 0.4 per cent, translated LAN to WAN too" src="https://img.shields.io/badge/flow%20offload-941%20Mbit%2Fs%20in%20hardware%20%C2%B7%20CPU%200.4%20%25%20%C2%B7%20NAT-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/xgs3300.md#the-clear-text-path-at-a-gigabit-through-the-host-and-with-the-flow-table"><img alt="flow offload: 99.6 percent of frames in hardware" src="https://img.shields.io/badge/flow%20offload-99.6%20%25%20of%20frames%20in%20hardware-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/families/board-limits.md"><img alt="flow table: 1,500,000 connections, the size the board itself states" src="https://img.shields.io/badge/flow%20table-1%2C500%2C000%20connections%2C%20the%20board%27s-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/families/board-limits.md"><img alt="flow table: 2,000,000 connections, the size the board itself states" src="https://img.shields.io/badge/flow%20table-2%2C000%2C000%20connections%2C%20the%20board%27s-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/xgs136-hardware-2026-10-18.md#a-thousand-connections-at-once"><img alt="flow writes: posted on the high-priority ring, not waited for" src="https://img.shields.io/badge/flow%20writes-posted%2C%20no%20wait-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/families/parity.md#the-table"><img alt="flow writes: posted on the high-priority ring, not waited for" src="https://img.shields.io/badge/flow%20writes-posted%2C%20no%20wait-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/xgs136-hardware-2026-10-18.md#ipv6"><img alt="IPv6 flows: 929 and 919 Mbit/s in hardware through the XGS 136" src="https://img.shields.io/badge/IPv6%20flows-929%20%C2%B7%20919%20Mbit%2Fs%20in%20hardware-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/ipv6-and-queues-2026-10-18.md#ipv6-through-the-xgs-3300"><img alt="IPv6 flows: 9.24 and 9.27 Gbit/s, 18 of 18 directions in hardware" src="https://img.shields.io/badge/IPv6%20flows-9.24%20%C2%B7%209.27%20Gbit%2Fs%2C%2018%2F18%20in%20hardware-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/xgs136-giveback-2026-10-19.md"><img alt="a connection given back: revived in place, and held with the host when it keeps coming back - 1,521 retransmissions a minute, was 6,073" src="https://img.shields.io/badge/given%20back-revived%3B%201%2C521%20retransmits%2C%20was%206%2C073-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/xgs3300.md#three-hours-of-tls-on-the-clear-text-path-with-the-trigger-on-and-then-off"><img alt="give-backs: 6,100 revived in place, 0 refused" src="https://img.shields.io/badge/give--backs-6%2C100%20revived%20%C2%B7%200%20refused-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/findings/the-revision-barrier-on-the-armada.md"><img alt="ruleset reload: every offloaded flow discarded by the revision barrier" src="https://img.shields.io/badge/ruleset%20reload-every%20flow%20discarded%20at%20once-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/octep/ruleset-reload.md"><img alt="ruleset reload: every offloaded flow voided" src="https://img.shields.io/badge/ruleset%20reload-every%20flow%20voided-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/xgs136-hardware-2026-10-18.md#a-thousand-connections-at-once"><img alt="a thousand connections at once: 953 of 1,024 in hardware, the link full at 996 Mbit/s" src="https://img.shields.io/badge/1%2C024%20connections-953%20in%20hardware%20%C2%B7%20996%20Mbit%2Fs-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/xgs3300-many-2026-10-19.md"><img alt="a thousand connections at once: 1,032 in hardware, the 10 Gbit/s link full at 9,837 Mbit/s, the CPUs 7.8 per cent busy" src="https://img.shields.io/badge/1%2C024%20connections-1%2C032%20in%20hardware%20%C2%B7%209%2C837%20Mbit%2Fs%20%C2%B7%20CPU%207.8%20%25-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/xgs136-counts-2026-10-19.md#the-reader-measured"><img alt="connection counts from the coprocessor: pf's state equal to the wire, 0 packets apart in 1.37 million either way" src="https://img.shields.io/badge/connection%20counts-pf%20%3D%20the%20wire%2C%200%20in%201.37M%20both%20ways-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/hw-counters-2026-10-19.md"><img alt="connection counts from the coprocessor: with pf's, the wire to 14 packets in 2.74 million" src="https://img.shields.io/badge/connection%20counts-coprocessor%20%2B%20pf%20%3D%20the%20wire%2C%2014%20in%202.74M-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/xgs136-counts-2026-10-19.md#the-reader-measured"><img alt="interface counters: hardware traffic added, every frame both ways, the four bytes of FCS taken off what comes in" src="https://img.shields.io/badge/interface%20counters-every%20frame%20both%20ways%2C%20FCS%20off-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/hw-counters-2026-10-19.md#added-to-the-interfaces-counters"><img alt="interface counters: hardware traffic counted, the whole stream to 327 packets in 5.1 million" src="https://img.shields.io/badge/interface%20counters-hardware%20traffic%20counted%2C%20327%20in%205.1M-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/flow-dscp-2026-10-24.md#on-the-xgs-136"><img alt="pf's DSCP mark: kept on a connection in hardware, EF arriving as 0xb8 where it arrived as 0x0 before" src="https://img.shields.io/badge/DSCP%20mark-kept%20in%20hardware%2C%20EF%200xb8%20where%20it%20was%200x0-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/flow-dscp-2026-10-24.md"><img alt="pf's DSCP mark: kept on a connection in hardware both ways, EF, AF41 and CS1, the ECN bits as they came" src="https://img.shields.io/badge/DSCP%20mark-kept%20in%20hardware%20both%20ways%2C%20EF%20AF41%20CS1%2C%20ECN%20kept-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/bridged-2026-10-24.md#on-the-xgs-136"><img alt="bridged connections: made in hardware, the host's bridge taking 145,973 frames where it carried 1,933,880" src="https://img.shields.io/badge/bridged%20connections-in%20hardware%2C%20146k%20frames%20through%20the%20host%20for%201.93M-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/bridged-2026-10-24.md"><img alt="bridged connections: made in hardware with their MACs and TTL kept, where they had been sent out of the WAN port" src="https://img.shields.io/badge/bridged%20connections-in%20hardware%2C%20MACs%20and%20TTL%20kept%2C%20not%20out%20of%20the%20WAN-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/vlan-2026-10-24.md#on-the-xgs-136"><img alt="VLAN connections: made in hardware, the XGS 136's processors 0.9 per cent busy where they were 33.1" src="https://img.shields.io/badge/VLAN%20connections-in%20hardware%2C%20CPUs%200.9%20%25%20busy%20where%2033.1%20%25-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/vlan-2026-10-24.md"><img alt="VLAN connections: made in hardware with the tag written, the interrupts at 0.0 per cent where they were 2.9" src="https://img.shields.io/badge/VLAN%20connections-in%20hardware%2C%20tag%20written%2C%20interrupts%200.0%20%25%20where%202.9%20%25-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td rowspan="6" valign="top"><b>security</b></td>
<td><a href="docs/measurements/lists-cut-2026-10-19.md"><img alt="block list: a stream in hardware cut within two seconds of its address being listed" src="https://img.shields.io/badge/block%20list-stream%20cut%20within%202%20s%20of%20listing-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/lists-cut-2026-10-19.md"><img alt="block list: a stream in hardware cut within two seconds of its address being listed" src="https://img.shields.io/badge/block%20list-stream%20cut%20within%202%20s%20of%20listing-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/ja4-cut-2026-10-19.md#on-the-xgs-136"><img alt="TLS fingerprint (JA4): the listed stream cut in hardware at once, the one beside it ran on" src="https://img.shields.io/badge/TLS%20fingerprint%20%28JA4%29-listed%20stream%20cut%2C%20the%20other%20ran%20on-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/ja4-cut-2026-10-19.md"><img alt="TLS fingerprint (JA4): the listed stream cut, the one beside it ran on" src="https://img.shields.io/badge/TLS%20fingerprint%20(JA4)-listed%20stream%20cut%2C%20the%20other%20ran%20on-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/anomaly-2026-10-19.md#on-the-xgs-136"><img alt="unusual traffic: a port scan, an address sweep and a stream in hardware beyond its own normal, each reported within its minute; nothing blocked" src="https://img.shields.io/badge/unusual%20traffic-scan%2C%20sweep%2C%20volume%20reported-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/anomaly-2026-10-19.md"><img alt="unusual traffic: a port scan, an address sweep and a host beyond its own normal, each reported within its minute; nothing blocked" src="https://img.shields.io/badge/unusual%20traffic-scan%2C%20sweep%2C%20volume%20reported-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/dos-probe-2026-10-19.md#on-the-xgs-136"><img alt="an address dropped by the coprocessor's DoS table: not in the XGS 136's firmware either, the three commands not dispatched" src="https://img.shields.io/badge/address%20drop-not%20in%20firmware-lightgrey.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/dos-probe-2026-10-19.md"><img alt="an address dropped by the coprocessor's DoS table: not in its firmware, the three commands not dispatched" src="https://img.shields.io/badge/address%20drop-not%20in%20firmware-lightgrey.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/policy-guard-136-2026-10-20.md"><img alt="a connection an IPsec policy covers: kept out of hardware, and taken out when a tunnel comes to cover it - none of it in the clear, the connection on through the tunnel at 916 Mbit/s" src="https://img.shields.io/badge/IPsec%20policy-covered%20connections%20out%20of%20hardware%2C%20none%20in%20the%20clear-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/policy-guard-136-2026-10-20.md#on-the-xgs-3300"><img alt="a connection an IPsec policy covers: taken out of hardware when a tunnel comes to cover it, none of it in the clear after" src="https://img.shields.io/badge/IPsec%20policy-covered%20connections%20out%20of%20hardware%2C%20none%20in%20the%20clear-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/cp-hardware-2026-10-24.md#the-cases-on-the-xgs-136"><img alt="Captive Portal clients in hardware: a logout takes them out within two seconds; the RADIUS stop 8.87 GB, the driver's count 8.93" src="https://img.shields.io/badge/Captive%20Portal-logout%20out%20of%20hardware%20in%202%20s%2C%20RADIUS%208.87%20GB-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/cp-hardware-2026-10-24.md"><img alt="Captive Portal clients in hardware: the RADIUS stop 38.3 GB, of which ipfw alone counted 87 KB" src="https://img.shields.io/badge/Captive%20Portal-RADIUS%20stop%2038.3%20GB%2C%20ipfw%20alone%2087%20KB-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td rowspan="9" valign="top"><b>IPsec</b></td>
<td><a href="docs/findings/the-engine-encrypts-outbound-esp-on-the-armada.md"><img alt="IPsec offload: AES-GCM both ways in hardware, 20 of 20 in, 10 of 10 out, 10 of 10 forwarded" src="https://img.shields.io/badge/IPsec%20offload-AES--GCM%20both%20ways%20%C2%B7%2020%2F20%20in%20%C2%B7%2010%2F10%20out%20%C2%B7%2010%2F10%20forwarded-brightgreen.svg?style=flat-square"></a><br><a href="docs/findings/the-host-answers-through-the-tunnel-on-the-armada.md"><img alt="receive termination: every decrypted frame terminated, none refused" src="https://img.shields.io/badge/receive%20termination-70%20of%2070%20frames%2C%20none%20refused-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/mtu-2026-10-17.md"><img alt="IPsec, the engine at both ends: one stream 7.41 Gbit/s at MTU 9000 into an XGS 3300" src="https://img.shields.io/badge/IPsec%2C%20engine%20at%20both%20ends-7.41%20Gbit%2Fs%20one%20stream%20at%20MTU%209000-brightgreen.svg?style=flat-square"></a><br><a href="docs/measurements/xgs3300.md#a-ring-behind-the-block-build-by-build"><img alt="IPsec offload: AES-GCM both ways, 951 down / 946 up Mbit/s" src="https://img.shields.io/badge/IPsec%20offload-AES--GCM%20both%20ways%20%C2%B7%20951%20%2F%20946%20Mbit%2Fs-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/findings/a-block-cipher-on-the-armada.md"><img alt="AES-CBC with HMAC SHA-1/256/384/512: taken both ways in hardware, 10 of 10 from the peer each" src="https://img.shields.io/badge/AES--CBC%20%2B%20HMAC-SHA--1%2F256%2F384%2F512%20both%20ways%20%C2%B7%2010%2F10%20each-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/xgs3300.md#aes-cbc-with-an-hmac-on-the-coprocessor"><img alt="AES-CBC with HMAC SHA-1/256/384/512: two thirds of GCM's rate" src="https://img.shields.io/badge/AES--CBC%20%2B%20HMAC-SHA--1%2F256%2F384%2F512%20%C2%B7%20%E2%85%94%20of%20GCM-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/findings/esp-behind-udp-on-the-armada.md"><img alt="NAT-T: ESP inside UDP 4500 both ways in hardware, 10 of 10 from the peer, 10 of 10 and 5 of 5 fragmented forwarded" src="https://img.shields.io/badge/NAT--T-ESP%20in%20UDP%204500%20both%20ways%20%C2%B7%2010%2F10%20%C2%B7%205%2F5%20fragmented-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/xgs3300.md#an-association-behind-nat-on-the-coprocessor"><img alt="NAT-T: at the plain tunnel's rates" src="https://img.shields.io/badge/NAT--T-at%20the%20plain%20tunnel%27s%20rates-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/findings/a-window-on-the-armada.md"><img alt="replay window: the engine's check, a replayed frame dropped as a duplicate and as out of window" src="https://img.shields.io/badge/replay%20window-32%20to%204%2C096%20on%20the%20engine%20%C2%B7%20replayed%20frame%20dropped-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/xgs3300.md#replay-windows-other-than-32"><img alt="replay window: up to 4,096" src="https://img.shields.io/badge/replay%20window-up%20to%204%2C096-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/findings/fragmentation-before-the-envelope-on-the-armada.md"><img alt="fragmentation before the envelope: 1,500-byte packets, 5 of 5 from the appliance and 10 of 10 forwarded, DF answered" src="https://img.shields.io/badge/fragmentation-1%2C500--byte%20packets%20%C2%B7%205%2F5%20%C2%B7%2010%2F10%20forwarded%20%C2%B7%20DF%20answered-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/families/parity.md#the-table"><img alt="fragmentation before the envelope" src="https://img.shields.io/badge/fragmentation-before%20the%20envelope-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/ipv6-136-2026-10-20.md"><img alt="IPsec over IPv6, outside and inside: both on the engine, both ways, 979 Mbit/s with IPv6 inside" src="https://img.shields.io/badge/IPv6-outer%20and%20inner%20%C2%B7%20979%20Mbit%2Fs%20both%20ways-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/xgs3300.md#an-ipv6-outer-header"><img alt="IPv6: outer header and inner packets" src="https://img.shields.io/badge/IPv6-outer%20and%20inner-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/esn-136-2026-10-20.md"><img alt="extended sequence numbers: both ways on the engine, 4.3 million frames decrypted on one association, none failed" src="https://img.shields.io/badge/ESN-both%20ways%20%C2%B7%204.3M%20frames%2C%20none%20failed-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/esn-3300-2026-10-20.md"><img alt="extended sequence numbers: both ways on the engine, 20.7 million frames decrypted on one association, none failed" src="https://img.shields.io/badge/ESN-both%20ways-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/tunflows-136-2026-10-20.md"><img alt="tunnel connections in the flow table: the encrypting direction in hardware, the host handing it no frame, CPUs 11 % busy against 23 %, none in the clear" src="https://img.shields.io/badge/tunnel%20flows-outbound%20in%20hardware%2C%20CPU%2011%20%25%20vs%2023%20%25-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/findings/the-flow-carries-the-association.md"><img alt="flows with their association: one stream 680 to 750 Mbit/s with the host out of its data path" src="https://img.shields.io/badge/flows%20%2B%20IPsec-one%20stream%20680%E2%80%93750%20Mbit%2Fs%20in%20hardware-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/findings/amd-ccp-breaks-ipsec-gcm-on-the-xgs-136-host.md#the-fix-2026-10-17"><img alt="ccp: the host's AMD crypto engine failed software AES-GCM; IPsec now held to aesni at boot, 903 Mbit/s in software with ccp loaded; issue 399" src="https://img.shields.io/badge/ccp-IPsec%20held%20to%20aesni%20%C2%B7%20903%20Mbit%2Fs%20%C2%B7%20%23399-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/findings/gcm-12-and-transport-are-not-ours.md"><img alt="not offloaded: GCM-8 and GCM-12, transport mode" src="https://img.shields.io/badge/not%20offloaded-GCM--8%2F12%20%C2%B7%20transport-lightgrey.svg?style=flat-square"></a></td>
</tr>
<tr>
<td rowspan="5" valign="top"><b>the appliance</b></td>
<td><a href="docs/families/parity.md#the-table"><img alt="temperatures: the coprocessor's and the PHYs', in OPNsense's sensor list" src="https://img.shields.io/badge/temperatures-coprocessor%20and%20PHYs%2C%20in%20OPNsense-brightgreen.svg?style=flat-square"></a><br><a href="docs/families/parity.md#the-table"><img alt="fans: read over the management link" src="https://img.shields.io/badge/fans-read%20over%20the%20management%20link-yellow.svg?style=flat-square"></a></td>
<td><a href="docs/findings/the-sensors-answer-on-the-host-bus.md"><img alt="sensors: temperatures and fans, read from the host" src="https://img.shields.io/badge/sensors-CPU%20%C2%B7%20NPU%20%C2%B7%20inlet%20%C2%B7%20fans%2C%20from%20the%20host-brightgreen.svg?style=flat-square"></a><br><a href="docs/families/parity.md#the-table"><img alt="fans: read; the board gives the host no register to set them" src="https://img.shields.io/badge/fans-read%3B%20no%20register%20to%20set%20them-yellow.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/families/parity.md#the-table"><img alt="front panel: none on this model" src="https://img.shields.io/badge/front%20panel-none%20on%20this%20model-inactive.svg?style=flat-square"></a></td>
<td><a href="docs/families/parity.md#the-table"><img alt="front panel: the LCD and its keys" src="https://img.shields.io/badge/front%20panel-LCD%20and%20keys-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/families/parity.md#the-table"><img alt="hardware watchdog: armed" src="https://img.shields.io/badge/watchdog-armed-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/families/parity.md#the-table"><img alt="hardware watchdog: armed" src="https://img.shields.io/badge/watchdog-armed-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/poe-136-2026-10-20.md"><img alt="PoE: read, and switched off, on or off-and-on from XGS: Ports; no powered device tried yet" src="https://img.shields.io/badge/PoE-switched%20from%20XGS%3A%20Ports%2C%20no%20device%20tried-yellow.svg?style=flat-square"></a></td>
<td><a href="docs/families/parity.md#the-table"><img alt="PoE: none on this model" src="https://img.shields.io/badge/PoE-none%20on%20this%20model-inactive.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/families/parity.md#the-table"><img alt="fail-to-wire relay: none on this model" src="https://img.shields.io/badge/fail--to--wire-none%20on%20this%20model-inactive.svg?style=flat-square"></a></td>
<td><a href="docs/families/parity.md#the-table"><img alt="fail-to-wire relay: read, deliberately not driven" src="https://img.shields.io/badge/fail--to--wire-read%2C%20deliberately%20not%20driven-yellow.svg?style=flat-square"></a></td>
</tr>
<tr>
<td rowspan="3" valign="top"><b>reliability</b></td>
<td><a href="docs/measurements/xgs136-soak-2026-10-19.md"><img alt="integrity soak: 795 GiB over TLS in two hours, 0 bytes changed" src="https://img.shields.io/badge/integrity%20soak-795%20GiB%20over%20TLS%20in%20two%20hours%20%C2%B7%200%20bytes%20changed-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/xgs3300.md#two-hours-of-tls-on-the-failures-own-path-translated-through-the-host"><img alt="integrity soaks: 2,391 GiB over TLS, 0 bytes changed" src="https://img.shields.io/badge/integrity%20soaks-2%2C391%20GiB%20over%20TLS%20%C2%B7%200%20bytes%20changed-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="docs/measurements/rpc-bounds-2026-10-19.md#xgs-136"><img alt="posted commands: each kept in its slot" src="https://img.shields.io/badge/posted%20commands-kept%20in%20their%20slot-brightgreen.svg?style=flat-square"></a></td>
<td><a href="docs/measurements/rpc-bounds-2026-10-19.md#xgs-3300"><img alt="posted commands: each kept in its slot, an oversize one refused" src="https://img.shields.io/badge/posted%20commands-kept%20in%20their%20slot%2C%20an%20oversize%20one%20refused-brightgreen.svg?style=flat-square"></a></td>
</tr>
<tr>
<td><a href="https://github.com/AbdelmonemAwad/os-xgs-npu/issues/531"><img alt="open: a stream at a third of its speed for minutes after some boots, issue 531" src="https://img.shields.io/badge/open-slow%20minutes%20after%20a%20boot%2C%20issue%20531-orange.svg?style=flat-square"></a></td>
<td><a href="https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361"><img alt="open: an idle ring a burst finds, issue 361" src="https://img.shields.io/badge/open-an%20idle%20ring%20a%20burst%20finds%20%28%23361%29-orange.svg?style=flat-square"></a></td>
</tr>
</table>

<p align="center"><sub>colours: <strong>green</strong> measured and working &middot; <strong>yellow</strong> partial, the link says which half &middot; <strong>light grey</strong> not built yet, or not offloaded, by whose refusal the link says &middot; <strong>grey</strong> the hardware has none &middot; <strong>orange</strong> an open fault &middot; <strong>dark grey</strong> a fact about the repository &middot; the version badges in their projects' colours. A badge is corrected the day a measurement contradicts it, the old figure struck beside the new in the section it links to, like everything else here.</sub></p>

## Families

| family | coprocessor | boards | driver | hardware here | state |
|---|---|---|---|---|---|
| [ARMADA](docs/families/armada.md) | Marvell CN913x, `11ab:7080` | AMDA0200 XGS 87/107, AMDA0201 XGS 126/136, AMDA0208 XGS 116, AMDA0224 XGS 138 | `npuep` | **XGS 136** | all 14 front ports, flow and IPsec offload |
| [OCTEON TX](docs/families/octeon-tx.md) | Cavium CN83xx, `177d:a300` | AMDA0202 XGS 2100/2300/3100/3300 | `octep` | **XGS 3300** | all 12 front ports and the module's, flow and IPsec offload |
| [OCTEON TX2](docs/families/octeon-tx2.md) | `177d:b200` | the larger boards | none | no | described from the vendor's binaries |
| [OCTEON TX2 98XX](docs/families/octeon-tx2-98xx.md) | `177d:b100` | the largest boards | none | no | described from the vendor's binaries |
| [TOPAZ](docs/families/topaz.md), [GR](docs/families/gr.md) | none | Atom boards | not needed | no | no coprocessor exists |

<!-- devices: written by tools/devices-table.py -->

### Every model

The two families with a driver, model by model - the table the drivers and the first boot read ([`models.py`](src/opnsense/scripts/xgs/models.py), [front-ports-by-model.md](docs/families/front-ports-by-model.md)). **Run here** is an appliance this project has been run on; every other model is **added, untested** - from the vendor's board files and device trees, research and development, because there is no such appliance here to test on. It installs, its status page says so, and its first boot checks its ports against what its coprocessor reports ([portcheck.py](src/opnsense/scripts/xgs/portcheck.py)). **Each model has a page on the site** - its ports drawn where they sit, the board drawn, every port's path, the vendor's figures - [every model on npuforge.org](https://npuforge.org/devices/).

| model | family | board | coprocessor | front ports | state |
|---|---|---|---|---|---|
| [XGS 87, 87w](https://npuforge.org/devices/xgs-87/) | ARMADA | `AMDA0200-0001, 0002` | CN9130 | 5: 4 RJ45, 1 SFP | added, untested |
| [XGS 107, 107w](https://npuforge.org/devices/xgs-107/) | ARMADA | `AMDA0200-0003, 0004` | CN9130 | 9: 8 RJ45, 1 SFP | added, untested |
| [XGS 116, 116w](https://npuforge.org/devices/xgs-116/) | ARMADA | `AMDA0208-0001, 0002` | CN9131 | 9: 8 RJ45, 1 SFP | added, untested |
| [XGS 126, 126w](https://npuforge.org/devices/xgs-126/) | ARMADA | `AMDA0201-0001, 0002` | CN9131 | 14: 12 RJ45, 2 SFP | added, untested |
| [**XGS 136**, 136w](https://npuforge.org/devices/xgs-136/) | ARMADA | `AMDA0201-0003, 0004` | CN9131 | 14: 12 RJ45, 2 SFP | **run here** (136w: added, untested) |
| [XGS 138](https://npuforge.org/devices/xgs-138/) | ARMADA | `AMDA0224-0001` | CN9131 | 8: 6 RJ45, 2 SFP+ | added, untested |
| [XGS 2100](https://npuforge.org/devices/xgs-2100/) | OCTEON TX | `AMDA0202-0001` | CN8360 | 10: 8 RJ45, 2 SFP | added, untested |
| [XGS 2300](https://npuforge.org/devices/xgs-2300/) | OCTEON TX | `AMDA0202-0002` | CN8360 | 10: 8 RJ45, 2 SFP | added, untested |
| [XGS 3100](https://npuforge.org/devices/xgs-3100/) | OCTEON TX | `AMDA0202-0003` | CN8365 | 12: 8 RJ45, 2 SFP, 2 SFP+ | added, untested |
| [**XGS 3300**](https://npuforge.org/devices/xgs-3300/) | OCTEON TX | `AMDA0202-0004` | CN8365 | 12: 8 RJ45, 2 SFP, 2 SFP+ | **run here** |

> [!NOTE]
> **Appliances of these models will be bought to test on.** Until then, each model marked untested is confirmed or corrected by its first owner's report.

The families without a driver, from the vendor's board files:

| family | model | board | coprocessor | front ports | module bays |
|---|---|---|---|---|:-:|
| OCTEON TX2 | XGS 4300 | `AMDA0203-0001` | CN9265 | 8 RJ45, 4 SFP+ | A, B |
| OCTEON TX2 | XGS 4500 | `AMDA0203-0002` | CN9265 | 8 RJ45, 4 SFP+ | A, B |
| OCTEON TX2 | XGS 4310 | `AMDA0228` | CN9670 | 8 RJ45, 4 SFP28 | A, B |
| OCTEON TX2 | XGS 5500 | `AMDA0204-0001` | CN9670 | 8 RJ45, 8 SFP+ | A-D |
| OCTEON TX2 | XGS 6500 | `AMDA0204-0002` | CN9670 | 8 RJ45, 12 SFP+ | A-D |
| OCTEON TX2 | XGS 6580 | `AMDA0225-0001` | CN9670 | 4 RJ45, 6 SFP+, 4 SFP28 | A-D |
| OCTEON TX2 98XX | XGS 7500 | `AMDA0205-0001` | CN9880 | 8 RJ45, 12 SFP+, 2 QSFP28 | A-D |
| OCTEON TX2 98XX | XGS 8500 | `AMDA0205-0001` | CN9880 | 8 RJ45, 12 SFP+, 2 QSFP28 | A-D |
| OCTEON TX2 98XX | XGS 7580 | `AMDA0226-0001` | CN9880 | 6 RJ45, 4 SFP+, 8 SFP28, 2 QSFP28 | A-D |
| no coprocessor in the board file | XGS 88, 88w | `AMDA0220` | - | 4 RJ45 | - |
| no coprocessor in the board file | XGS 108, 108w | `AMDA0221` | - | 6 RJ45, 1 SFP | - |
| no coprocessor in the board file | XGS 118, 118w | `AMDA0222` | - | 9 RJ45, 1 SFP | - |
| no coprocessor in the board file | XGS 128, 128w | `AMDA0223` | - | 9 RJ45, 1 SFP | - |

<!-- /devices -->

What has run on which board, and why there are two drivers and not one with a switch in it, is in
[the families page](docs/guide/families-and-hardware.md). **What one appliance has and the other does
not** - the same features listed for both, so every gap shows - is [parity.md](docs/families/parity.md).

## Getting it onto an appliance

Two roads, for two kinds of reader. Most people want a firewall: take the image. Those who want to
read, build and change the drivers themselves take the files.

### 💿 The image - for most people

<table>
<tr>
<td width="62%" valign="top">

**[Download a ready image](https://build.npuforge.org/)** - four are published with ~~each release~~ each main release,
the console at 38400 baud (the Sophos firmware's speed) or 115200 (OPNsense's), each with the front
panel plugin and without it - or, on the same page, choose the OPNsense release, the image type and
the console speed, and the build hands you one of your own. Either is the official OPNsense image,
verified, with this project's **newest release**, its plugins, the Arabic translation and the
right-to-left layout already inside.

Write it to a USB stick, boot the appliance from it and install to the disk. At the first boot from
the disk a thirty-second countdown on the console assigns the front ports by the names on the chassis:
**WAN** on Port2 (XGS 3300) or Port1 (XGS 136), every other front port in one bridge as **LAN** on
192.168.1.1, and the management port, where there is one, as **MGMT** on 192.168.100.1.

From then on, **XGS - Updates** brings each new release.

**Never done this before?** [Installing, step by step](docs/guide/step-by-step.md) goes from nothing to
the first sign-in in order, with a picture for each step: the ready image, or the copy of the builder
and the build, then the USB stick, the console, the installer and the web interface.

</td>
<td width="38%" valign="top">

**The builder:** [xgs-image-builder](https://github.com/AbdelmonemAwad/xgs-image-builder) - its own
repository, its own [build page](https://build.npuforge.org/), its
[ready images](https://github.com/AbdelmonemAwad/xgs-image-builder/releases/latest), and the
install steps, one by one.

**Tested:** an image built there installed on an XGS 3300 on 2026-10-16, which assigned its own front
ports at its first boot; and on an XGS 136 on 2026-10-21, its front ports up at the first boot, then
updated to OPNsense 26.7.7 from the GUI and its earlier configuration restored
([measured](docs/measurements/image-136-2026-10-21.md)). Burn an image and boot it before you rely on it.

</td>
</tr>
</table>

<a name="-experimental"></a>

### 🛠 For experts - the files, directly

This road is for those who want to work on the drivers themselves. They are FreeBSD kernel modules
that drive an undocumented coprocessor, written from the outside with no vendor support on any
operating system, and a mistake in one can stop an appliance until it is power-cycled by hand - which
has happened here, more than once, during development. So it asks a little more than the image does:
**a serial console, an appliance you can reach, the source read before it is loaded, and a way to
reinstall.**

One installer reads the board's assembly number and installs only that board's pieces; any other
board gets nothing. The kernel module is built on the appliance against the running kernel's own
sources and stamped with it.

```sh
git clone https://github.com/AbdelmonemAwad/os-xgs-npu
cd os-xgs-npu
./install/install.sh
```

Then **XGS** in the menu: Overview, Settings, Ports, Transceivers, IPsec, Offloaded Flows,
Updates, Problem Reports and the log. Updates come from the [releases](https://github.com/AbdelmonemAwad/os-xgs-npu/releases),
not from every change merged. Everything about installing, the boot hooks, the pages, updating
and the first boot is in [docs/guide/installing.md](docs/guide/installing.md) - read its note on DNS
before blaming the driver for a network with no internet.

> [!IMPORTANT]
> **Your appliance, your decision.** Whichever road you take, nothing here carries a warranty of any
> kind, and no one else is answerable for what it does to your hardware, your network or your data.
> If you load it, that decision and its consequences are yours. Try a release on an appliance you can
> reach before you trust it with a network you depend on.

## Documentation

| | |
|---|---|
| [**docs/README.md**](docs/README.md) | **Start here**: the documentation by what you want to do, and every folder by what it holds |
| [docs/INDEX.md](docs/INDEX.md) | Every page, one line each, with the date it was last reviewed and when it is due again ([how pages are reviewed](docs/REVIEW.md)) |
| [docs/guide/the-pages.md](docs/guide/the-pages.md) | Every page of the web interface, photographed on an appliance in English and in Arabic, with what each one is for |
| [docs/images/README.md](docs/images/README.md) | Every image in the repository by kind - the appliances, the measurements, the web interface - with what it shows and the pages that use it |
| [The wiki](https://github.com/AbdelmonemAwad/os-xgs-npu/wiki) | How the project came to be, installing it step by step, and its photographs |
| [docs/guide/](docs/guide/) | What used to be this file, at full length: [what works](docs/guide/what-works.md), [how it works](docs/guide/how-it-works.md), [what it cannot do](docs/guide/limitations.md), [families and hardware](docs/guide/families-and-hardware.md), [installing](docs/guide/installing.md), [background](docs/guide/background.md), [every badge](docs/guide/measured.md) |
| [DESIGN.md](DESIGN.md) | The contract: the stages, the limits that are properties of the hardware, and every claim that turned out to be wrong |
| [docs/measurements/](docs/measurements/) | The numbers, with how each was taken and a photograph of the run |
| [docs/findings/](docs/findings/) | What was found, dated, one page per fault or discovery |
| [CHANGELOG.md](CHANGELOG.md) | What changed in each release, written from the commits; [RELEASING.md](RELEASING.md) says when a release is made - at once for a fix, gathered for improvements |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Building, testing on an appliance, and what a change has to carry |

<a name="licence"></a>

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

Copyright &copy; 2026 Abdelmonem Awad. The photographs and screenshots in
[docs/images](docs/images/README.md) are under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/):
use them freely, naming the author and linking here; each carries that notice in a strip added below
it, never over the picture. **OPNsense&reg;** is a registered trademark of Deciso B.V.;
**Sophos&reg;** and **XGS** are trademarks of Sophos Ltd. This project is independent: it is not
affiliated with, endorsed by or sponsored by either, and their names are used only to say what this
software runs on.

No Sophos software or firmware is distributed here. The drivers are this project's own; where a
vendor's code was read to learn how the hardware behaves - Marvell's, published under the GPL - or a
fact was read from software on the appliance, [the provenance pages](docs/provenance.md) say what
was read and why. If you hold rights in anything here and think it should not be, write to
[contact@npuforge.org](mailto:contact@npuforge.org): it will be looked at promptly, and corrected or
taken down if it should be.

Each page the plugin adds ends with a one-line credit - the project, its author, its licence, and that
it is independent. OPNsense's own footer, with Deciso's copyright, is left exactly as it is.
