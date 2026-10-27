<!-- reviewed: 2026-10-19 -->
# Images

Every image in the repository, in one place and by kind. The pages use them from here, and so
does the [wiki](https://github.com/AbdelmonemAwad/os-xgs-npu/wiki), which keeps no images of its
own. Each photograph was taken on the appliances themselves; what each one shows is the alt text
its page gives it, and the pages that use it are named beside it.

This page is written by `tools/images-index.py`, and the checks fail when it is out of date: add an
image to its folder, use it on a page, and run the tool.

**Licence.** The photographs and screenshots are &copy; 2026 Abdelmonem Awad, under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/): use them freely, naming the author and
linking here. Each carries that notice in a strip added below it - never over the picture, so a
measurement is still what the screen showed - and in its metadata: `tools/mark-images.py` adds both
to a new image, and the checks fail on one without them.

| folder | what it holds | images |
|---|---|---|
| [screens/](screens/) | The web interface, page by page, in English and in Arabic | 79 |
| [hardware/](hardware/) | Photographs and drawings of the appliances, one folder each | 41 |
| [measurements/](measurements/) | Each measurement's photographs, in a folder named after its page | 482 |
| [readme/](readme/) | The README's banner and status board, drawn by tools/readme-art.py | 6 |

634 images in all.

## The web interface

Full screen, dark theme, photographed on 2026-10-19 on the second XGS 3300. Each page is described
in [docs/guide/the-pages.md](../guide/the-pages.md), English first and Arabic under it.

<table>
<tr><th width="20%">page</th><th width="40%">English</th><th width="40%">Arabic</th></tr>
<tr><td valign="top"><sub>Lobby: Dashboard on an XGS 3300</sub></td><td valign="top"><a href="screens/en/00-lobby-dashboard.png"><img src="screens/en/00-lobby-dashboard.png" alt="Lobby: Dashboard on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/00-lobby-dashboard.png"><img src="screens/ar/00-lobby-dashboard.png" alt="Lobby: Dashboard on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>The question on XGS: Overview, on the second XGS 3300 as a new install shows it: problem reports waiting for their Send, the appliance not counted, the model's tested status, "Help it get better", and the two answers</sub></td><td valign="top"><a href="screens/en/01-xgs-overview-question.png"><img src="screens/en/01-xgs-overview-question.png" alt="The question on XGS: Overview, on the second XGS 3300 as a new install shows it: problem reports waiting for their Send, the appliance not counted, the model's tested status, "Help it get better", and the two answers - English" width="100%"></a></td><td valign="top"><a href="screens/ar/01-xgs-overview-question.png"><img src="screens/ar/01-xgs-overview-question.png" alt="The question on XGS: Overview, on the second XGS 3300 as a new install shows it: problem reports waiting for their Send, the appliance not counted, the model's tested status, "Help it get better", and the two answers - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Overview on an XGS 3300: the front panel drawn with each port's link, cards for the coprocessor, hardware offloading, the IPsec engine, the front ports, the switch chip and the last restart, and the board's limits</sub></td><td valign="top"><a href="screens/en/01-xgs-overview.png"><img src="screens/en/01-xgs-overview.png" alt="XGS: Overview on an XGS 3300: the front panel drawn with each port's link, cards for the coprocessor, hardware offloading, the IPsec engine, the front ports, the switch chip and the last restart, and the board's limits - English" width="100%"></a></td><td valign="top"><a href="screens/ar/01-xgs-overview.png"><img src="screens/ar/01-xgs-overview.png" alt="XGS: Overview on an XGS 3300: the front panel drawn with each port's link, cards for the coprocessor, hardware offloading, the IPsec engine, the front ports, the switch chip and the last restart, and the board's limits - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Overview's Coprocessor tab on the first XGS 3300: eight cards - the vendor's build 23.0.0.329, the CN8365 with 20 cores, copy A running with copy B holding 22.0.2.546, the Kingston eMMC, the persistent partition, the boot firmware, the watchdog fed every 5 s, no memory errors - then the software and the chips</sub></td><td valign="top"><a href="screens/en/01b-xgs-overview-coprocessor.png"><img src="screens/en/01b-xgs-overview-coprocessor.png" alt="XGS: Overview's Coprocessor tab on the first XGS 3300: eight cards - the vendor's build 23.0.0.329, the CN8365 with 20 cores, copy A running with copy B holding 22.0.2.546, the Kingston eMMC, the persistent partition, the boot firmware, the watchdog fed every 5 s, no memory errors - then the software and the chips - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: Overview's Datapath counters tab on the first XGS 3300: the driver's counters, then the coprocessor's fast-path counters named for vendor version 23, with Read now and Clear</sub></td><td valign="top"><a href="screens/en/01c-xgs-overview-fast-path.png"><img src="screens/en/01c-xgs-overview-fast-path.png" alt="XGS: Overview's Datapath counters tab on the first XGS 3300: the driver's counters, then the coprocessor's fast-path counters named for vendor version 23, with Read now and Clear - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: Settings on an XGS 3300: the Acceleration tab</sub></td><td valign="top"><a href="screens/en/02-xgs-settings.png"><img src="screens/en/02-xgs-settings.png" alt="XGS: Settings on an XGS 3300: the Acceleration tab - English" width="100%"></a></td><td valign="top"><a href="screens/ar/02-xgs-settings.png"><img src="screens/ar/02-xgs-settings.png" alt="XGS: Settings on an XGS 3300: the Acceleration tab - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Settings on the XGS 136, the Security tab with full help: Cut in hardware, Inspect, then hardware, Watch for unusual traffic</sub></td><td valign="top"><a href="screens/en/02b-xgs-settings-security.png"><img src="screens/en/02b-xgs-settings-security.png" alt="XGS: Settings on the XGS 136, the Security tab with full help: Cut in hardware, Inspect, then hardware, Watch for unusual traffic - English" width="100%"></a></td><td valign="top"><a href="screens/ar/02b-xgs-settings-security.png"><img src="screens/ar/02b-xgs-settings-security.png" alt="XGS: Settings on the XGS 136, the Security tab with full help: Cut in hardware, Inspect, then hardware, Watch for unusual traffic - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Settings on an XGS 3300, the Hardware tab: the flow table, the switch chip, the fast path, the ports</sub></td><td valign="top"><a href="screens/en/02c-xgs-settings-hardware.png"><img src="screens/en/02c-xgs-settings-hardware.png" alt="XGS: Settings on an XGS 3300, the Hardware tab: the flow table, the switch chip, the fast path, the ports - English" width="100%"></a></td><td valign="top"><a href="screens/ar/02c-xgs-settings-hardware.png"><img src="screens/ar/02c-xgs-settings-hardware.png" alt="XGS: Settings on an XGS 3300, the Hardware tab: the flow table, the switch chip, the fast path, the ports - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Ports on an XGS 3300</sub></td><td valign="top"><a href="screens/en/03-xgs-ports.png"><img src="screens/en/03-xgs-ports.png" alt="XGS: Ports on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/03-xgs-ports.png"><img src="screens/ar/03-xgs-ports.png" alt="XGS: Ports on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Ports on the XGS 136, its Power over Ethernet section: port 11 on with no device, port 12 switched off, each with Switch off or on and Power cycle</sub></td><td valign="top"><a href="screens/en/03b-xgs-ports-poe.png"><img src="screens/en/03b-xgs-ports-poe.png" alt="XGS: Ports on the XGS 136, its Power over Ethernet section: port 11 on with no device, port 12 switched off, each with Switch off or on and Power cycle - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: Transceivers on an XGS 3300</sub></td><td valign="top"><a href="screens/en/04-xgs-transceivers.png"><img src="screens/en/04-xgs-transceivers.png" alt="XGS: Transceivers on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/04-xgs-transceivers.png"><img src="screens/ar/04-xgs-transceivers.png" alt="XGS: Transceivers on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: IPsec on an XGS 3300</sub></td><td valign="top"><a href="screens/en/05-xgs-ipsec.png"><img src="screens/en/05-xgs-ipsec.png" alt="XGS: IPsec on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/05-xgs-ipsec.png"><img src="screens/ar/05-xgs-ipsec.png" alt="XGS: IPsec on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: IPsec, Associations in the engine: one association each way through the tunnel to the XGS 136</sub></td><td valign="top"><a href="screens/en/05b-xgs-ipsec-associations.png"><img src="screens/en/05b-xgs-ipsec-associations.png" alt="XGS: IPsec, Associations in the engine: one association each way through the tunnel to the XGS 136 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/05b-xgs-ipsec-associations.png"><img src="screens/ar/05b-xgs-ipsec-associations.png" alt="XGS: IPsec, Associations in the engine: one association each way through the tunnel to the XGS 136 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: IPsec, Counters: frames decrypted and encrypted by the engine</sub></td><td valign="top"><a href="screens/en/05c-xgs-ipsec-counters.png"><img src="screens/en/05c-xgs-ipsec-counters.png" alt="XGS: IPsec, Counters: frames decrypted and encrypted by the engine - English" width="100%"></a></td><td valign="top"><a href="screens/ar/05c-xgs-ipsec-counters.png"><img src="screens/ar/05c-xgs-ipsec-counters.png" alt="XGS: IPsec, Counters: frames decrypted and encrypted by the engine - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Offloaded Flows on an XGS 3300</sub></td><td valign="top"><a href="screens/en/06-xgs-offloaded-flows.png"><img src="screens/en/06-xgs-offloaded-flows.png" alt="XGS: Offloaded Flows on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/06-xgs-offloaded-flows.png"><img src="screens/ar/06-xgs-offloaded-flows.png" alt="XGS: Offloaded Flows on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS, Updates on an appliance</sub></td><td valign="top"><a href="screens/en/07-xgs-updates.png"><img src="screens/en/07-xgs-updates.png" alt="XGS, Updates on an appliance - English" width="100%"></a></td><td valign="top"><a href="screens/ar/07-xgs-updates.png"><img src="screens/ar/07-xgs-updates.png" alt="XGS, Updates on an appliance - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Updates' Plugins tab on the first XGS 3300 on 2026-10-25: eleven plugins, each installed at its newest release - the four older ones just updated with Update all, the five new ones at v1.0.0 - with what each last update did</sub></td><td valign="top"><a href="screens/en/07b-xgs-updates-plugins.png"><img src="screens/en/07b-xgs-updates-plugins.png" alt="XGS: Updates' Plugins tab on the first XGS 3300 on 2026-10-25: eleven plugins, each installed at its newest release - the four older ones just updated with Update all, the five new ones at v1.0.0 - with what each last update did - English" width="100%"></a></td><td valign="top"><a href="screens/ar/07b-xgs-updates-plugins.png"><img src="screens/ar/07b-xgs-updates-plugins.png" alt="XGS: Updates' Plugins tab on the first XGS 3300 on 2026-10-25: eleven plugins, each installed at its newest release - the four older ones just updated with Update all, the five new ones at v1.0.0 - with what each last update did - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Updates' Plugins tab on the first XGS 3300 with Port Guard removed: its row reads not installed, its latest release v1.0.0, and Install</sub></td><td valign="top"><a href="screens/en/07c-xgs-updates-plugins-install.png"><img src="screens/en/07c-xgs-updates-plugins-install.png" alt="XGS: Updates' Plugins tab on the first XGS 3300 with Port Guard removed: its row reads not installed, its latest release v1.0.0, and Install - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: Problem Reports on an XGS 3300</sub></td><td valign="top"><a href="screens/en/08-xgs-problem-reports.png"><img src="screens/en/08-xgs-problem-reports.png" alt="XGS: Problem Reports on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/08-xgs-problem-reports.png"><img src="screens/ar/08-xgs-problem-reports.png" alt="XGS: Problem Reports on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Log File on an XGS 3300</sub></td><td valign="top"><a href="screens/en/09-xgs-log-file.png"><img src="screens/en/09-xgs-log-file.png" alt="XGS: Log File on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/09-xgs-log-file.png"><img src="screens/ar/09-xgs-log-file.png" alt="XGS: Log File on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: About on the XGS 136: the project, its status and the release installed</sub></td><td valign="top"><a href="screens/en/09b-xgs-about.png"><img src="screens/en/09b-xgs-about.png" alt="XGS: About on the XGS 136: the project, its status and the release installed - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: About, its Contact tab</sub></td><td valign="top"><a href="screens/en/09c-xgs-about-contact.png"><img src="screens/en/09c-xgs-about-contact.png" alt="XGS: About, its Contact tab - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: About, its Disclaimer tab</sub></td><td valign="top"><a href="screens/en/09d-xgs-about-disclaimer.png"><img src="screens/en/09d-xgs-about-disclaimer.png" alt="XGS: About, its Disclaimer tab - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: Coprocessor Console on the first XGS 3300: the coprocessor's lines kept from its boots, each boot's start marked by the logger</sub></td><td valign="top"><a href="screens/en/09e-xgs-coprocessor-console.png"><img src="screens/en/09e-xgs-coprocessor-console.png" alt="XGS: Coprocessor Console on the first XGS 3300: the coprocessor's lines kept from its boots, each boot's start marked by the logger - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: Network Report on the first XGS 3300</sub></td><td valign="top"><a href="screens/en/10-network-report.png"><img src="screens/en/10-network-report.png" alt="XGS: Network Report on the first XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/10-network-report.png"><img src="screens/ar/10-network-report.png" alt="XGS: Network Report on the first XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Fan Control on the first XGS 3300</sub></td><td valign="top"><a href="screens/en/11-fan-control.png"><img src="screens/en/11-fan-control.png" alt="XGS: Fan Control on the first XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/11-fan-control.png"><img src="screens/ar/11-fan-control.png" alt="XGS: Fan Control on the first XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Front Panel on the first XGS 3300</sub></td><td valign="top"><a href="screens/en/12-front-panel.png"><img src="screens/en/12-front-panel.png" alt="XGS: Front Panel on the first XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/12-front-panel.png"><img src="screens/ar/12-front-panel.png" alt="XGS: Front Panel on the first XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Link Health on the first XGS 3300</sub></td><td valign="top"><a href="screens/en/13-link-health.png"><img src="screens/en/13-link-health.png" alt="XGS: Link Health on the first XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/13-link-health.png"><img src="screens/ar/13-link-health.png" alt="XGS: Link Health on the first XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Hardware Watchdog on the first XGS 3300: armed by os-xgs-npu with 128 s, the AMD FCH timer, the last restart a reboot, the chipset's reset record read by os-xgs-npu</sub></td><td valign="top"><a href="screens/en/14-hardware-watchdog.png"><img src="screens/en/14-hardware-watchdog.png" alt="XGS: Hardware Watchdog on the first XGS 3300: armed by os-xgs-npu with 128 s, the AMD FCH timer, the last restart a reboot, the chipset's reset record read by os-xgs-npu - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: Kernel Follower on the first XGS 3300: the kernel running and the one the next boot takes, no module registered, one tree of kernel sources; os-xgs-npu's own module listed as followed by os-xgs-npu</sub></td><td valign="top"><a href="screens/en/15-kernel-follower.png"><img src="screens/en/15-kernel-follower.png" alt="XGS: Kernel Follower on the first XGS 3300: the kernel running and the one the next boot takes, no module registered, one tree of kernel sources; os-xgs-npu's own module listed as followed by os-xgs-npu - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: Block Lists on the first XGS 3300: no list chosen yet - the lists, the packets blocked, the DNS lists and the JA4 fingerprints at 0</sub></td><td valign="top"><a href="screens/en/16-block-lists.png"><img src="screens/en/16-block-lists.png" alt="XGS: Block Lists on the first XGS 3300: no list chosen yet - the lists, the packets blocked, the DNS lists and the JA4 fingerprints at 0 - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: Port Guard on the first XGS 3300: no device listed, 91 devices seen from the host's discovery</sub></td><td valign="top"><a href="screens/en/17-port-guard.png"><img src="screens/en/17-port-guard.png" alt="XGS: Port Guard on the first XGS 3300: no device listed, 91 devices seen from the host's discovery - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: Fallback Crypto on the first XGS 3300: the Ryzen V1780B, aesni as the provider, two IPsec SAs in the kernel with AES-GCM, aesni only at the next boot, and the processor's features</sub></td><td valign="top"><a href="screens/en/18-fallback-crypto.png"><img src="screens/en/18-fallback-crypto.png" alt="XGS: Fallback Crypto on the first XGS 3300: the Ryzen V1780B, aesni as the provider, two IPsec SAs in the kernel with AES-GCM, aesni only at the next boot, and the processor's features - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>The image builder's page</sub></td><td valign="top"><a href="screens/en/20-builder-page.png"><img src="screens/en/20-builder-page.png" alt="The image builder's page - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>The image builder's form: the release, the image type and the console</sub></td><td valign="top"><a href="screens/en/21-builder-form.png"><img src="screens/en/21-builder-form.png" alt="The image builder's form: the release, the image type and the console - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
</table>

## Hardware

### hardware/bench/ - The bench the appliances are measured on (2)

<table>
<tr>
<td width="25%" valign="top"><a href="hardware/bench/desk-2026-10-22.jpg"><img src="hardware/bench/desk-2026-10-22.jpg" alt="The bench where they are taken, 2026-10-22: the driver's code on one screen, a run of the measurements on the other, and below them the XGS 136, opened, and the XGS 3300" width="100%"></a><br><sub>The bench where they are taken, 2026-10-22: the driver's code on one screen, a run of the measurements on the other, and below them the XGS 136, opened, and the XGS 3300</sub></td>
<td width="25%" valign="top"><a href="hardware/bench/three-appliances-2026-10-17.jpg"><img src="hardware/bench/three-appliances-2026-10-17.jpg" alt="The bench: the XGS 136 at the top, the second XGS 3300 in the middle with its 4x10G module, the first XGS 3300 at the bottom" width="100%"></a><br><sub>The bench: the XGS 136 at the top, the second XGS 3300 in the middle with its 4x10G module, the first XGS 3300 at the bottom</sub></td>
</tr>
</table>

### hardware/xg330/ - The XG 330, where the story starts (1)

<table>
<tr>
<td width="25%" valign="top"><a href="hardware/xg330/xg330-front.jpg"><img src="hardware/xg330/xg330-front.jpg" alt="xg330-front.jpg" width="100%"></a><br><sub>xg330-front.jpg</sub></td>
</tr>
</table>

### hardware/xgs136/ - The XGS 136: the board, its parts, and the USB adapter that kept it reachable (22)

<table>
<tr>
<td width="25%" valign="top"><a href="hardware/xgs136/88e6193x.jpg"><img src="hardware/xgs136/88e6193x.jpg" alt="The 88E6193X" width="100%"></a><br><sub>The 88E6193X</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/88f8215.jpg"><img src="hardware/xgs136/88f8215.jpg" alt="The second Marvell part" width="100%"></a><br><sub>The second Marvell part</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/aqr412c.jpg"><img src="hardware/xgs136/aqr412c.jpg" alt="The AQR412C" width="100%"></a><br><sub>The AQR412C</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/bios.jpg"><img src="hardware/xgs136/bios.jpg" alt="The BIOS flash and the serial label" width="100%"></a><br><sub>The BIOS flash and the serial label</sub></td>
</tr>
<tr>
<td width="25%" valign="top"><a href="hardware/xgs136/board-map.svg"><img src="hardware/xgs136/board-map.svg" alt="The XGS 136, part by part" width="100%"></a><br><sub>The XGS 136, part by part</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/bottom.jpg"><img src="hardware/xgs136/bottom.jpg" alt="The XGS 136 from below" width="100%"></a><br><sub>The XGS 136 from below</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/channels.svg"><img src="hardware/xgs136/channels.svg" alt="What reaches the XGS 136's coprocessor, and from where" width="100%"></a><br><sub>What reaches the XGS 136's coprocessor, and from where</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/cn8-jp11.jpg"><img src="hardware/xgs136/cn8-jp11.jpg" alt="CN8, JP11 and a flash" width="100%"></a><br><sub>CN8, JP11 and a flash</sub></td>
</tr>
<tr>
<td width="25%" valign="top"><a href="hardware/xgs136/cn9130.jpg"><img src="hardware/xgs136/cn9130.jpg" alt="The CN9130" width="100%"></a><br><sub>The CN9130</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/ddr4-jp12.jpg"><img src="hardware/xgs136/ddr4-jp12.jpg" alt="DDR4 and JP12" width="100%"></a><br><sub>DDR4 and JP12</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/emmc.jpg"><img src="hardware/xgs136/emmc.jpg" alt="The eMMC" width="100%"></a><br><sub>The eMMC</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/j1-leds.jpg"><img src="hardware/xgs136/j1-leds.jpg" alt="J1, J2 and the LEDs" width="100%"></a><br><sub>J1, J2 and the LEDs</sub></td>
</tr>
<tr>
<td width="25%" valign="top"><a href="hardware/xgs136/label.jpg"><img src="hardware/xgs136/label.jpg" alt="The assembly label" width="100%"></a><br><sub>The assembly label</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/nct6779d.jpg"><img src="hardware/xgs136/nct6779d.jpg" alt="The Nuvoton NCT6779D" width="100%"></a><br><sub>The Nuvoton NCT6779D</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/pd69104.jpg"><img src="hardware/xgs136/pd69104.jpg" alt="The PoE controller" width="100%"></a><br><sub>The PoE controller</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/port-chain.svg"><img src="hardware/xgs136/port-chain.svg" alt="The XGS 136's fourteen front ports, end to end" width="100%"></a><br><sub>The XGS 136's fourteen front ports, end to end</sub></td>
</tr>
<tr>
<td width="25%" valign="top"><a href="hardware/xgs136/ryzen.jpg"><img src="hardware/xgs136/ryzen.jpg" alt="The host CPU" width="100%"></a><br><sub>The host CPU</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/top-annotated.jpg"><img src="hardware/xgs136/top-annotated.jpg" alt="The XGS 136 from above, with its parts numbered" width="100%"></a><br><sub>The XGS 136 from above, with its parts numbered</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/top.jpg"><img src="hardware/xgs136/top.jpg" alt="The XGS 136 from above" width="100%"></a><br><sub>The XGS 136 from above</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/trs213.jpg"><img src="hardware/xgs136/trs213.jpg" alt="The RS-232 transceiver" width="100%"></a><br><sub>The RS-232 transceiver</sub></td>
</tr>
<tr>
<td width="25%" valign="top"><a href="hardware/xgs136/usb-adapter-label.jpg"><img src="hardware/xgs136/usb-adapter-label.jpg" alt="Its label: WL-NWU330GCA, Rev.a, the serial number pixelated" width="100%"></a><br><sub>Its label: WL-NWU330GCA, Rev.a, the serial number pixelated</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs136/usb-adapter-top.jpg"><img src="hardware/xgs136/usb-adapter-top.jpg" alt="The WAVLINK WL-NWU330GCA USB to 2.5G Ethernet adapter" width="100%"></a><br><sub>The WAVLINK WL-NWU330GCA USB to 2.5G Ethernet adapter</sub></td>
</tr>
</table>

### hardware/xgs3300/ - The XGS 3300: the board, its parts and its front panel (16)

<table>
<tr>
<td width="25%" valign="top"><a href="hardware/xgs3300/amd-v1780b.jpg"><img src="hardware/xgs3300/amd-v1780b.jpg" alt="The host CPU" width="100%"></a><br><sub>The host CPU</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs3300/board-map.svg"><img src="hardware/xgs3300/board-map.svg" alt="The XGS 3300, part by part" width="100%"></a><br><sub>The XGS 3300, part by part</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs3300/fail-to-wire-relays.jpg"><img src="hardware/xgs3300/fail-to-wire-relays.jpg" alt="The bypass relays" width="100%"></a><br><sub>The bypass relays</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs3300/front-panel-chain.svg"><img src="hardware/xgs3300/front-panel-chain.svg" alt="The front panel, end to end" width="100%"></a><br><sub>The front panel, end to end</sub></td>
</tr>
<tr>
<td width="25%" valign="top"><a href="hardware/xgs3300/front-panel-controller.jpg"><img src="hardware/xgs3300/front-panel-controller.jpg" alt="The GIFAR bridge on the front panel board" width="100%"></a><br><sub>The GIFAR bridge on the front panel board</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs3300/i210-management-nic.jpg"><img src="hardware/xgs3300/i210-management-nic.jpg" alt="The Intel i210" width="100%"></a><br><sub>The Intel i210</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs3300/inside-numbered.jpg"><img src="hardware/xgs3300/inside-numbered.jpg" alt="The XGS 3300 opened, with its parts numbered" width="100%"></a><br><sub>The XGS 3300 opened, with its parts numbered</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs3300/juart1-rs232.jpg"><img src="hardware/xgs3300/juart1-rs232.jpg" alt="JUART1 and its transceiver" width="100%"></a><br><sub>JUART1 and its transceiver</sub></td>
</tr>
<tr>
<td width="25%" valign="top"><a href="hardware/xgs3300/lattice-cpld.jpg"><img src="hardware/xgs3300/lattice-cpld.jpg" alt="The Lattice device" width="100%"></a><br><sub>The Lattice device</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs3300/marvell-88x5113.jpg"><img src="hardware/xgs3300/marvell-88x5113.jpg" alt="The 88X5113 PHY" width="100%"></a><br><sub>The 88X5113 PHY</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs3300/ncb-xg330d.jpg"><img src="hardware/xgs3300/ncb-xg330d.jpg" alt="NCB-XG330D" width="100%"></a><br><sub>NCB-XG330D</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs3300/octeon-tx-cn8365.jpg"><img src="hardware/xgs3300/octeon-tx-cn8365.jpg" alt="The OCTEON TX coprocessor" width="100%"></a><br><sub>The OCTEON TX coprocessor</sub></td>
</tr>
<tr>
<td width="25%" valign="top"><a href="hardware/xgs3300/service-headers.jpg"><img src="hardware/xgs3300/service-headers.jpg" alt="The service headers" width="100%"></a><br><sub>The service headers</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs3300/service-headers.svg"><img src="hardware/xgs3300/service-headers.svg" alt="What each header is and what it takes" width="100%"></a><br><sub>What each header is and what it takes</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs3300/xgs3300-inside-annotated.jpg"><img src="hardware/xgs3300/xgs3300-inside-annotated.jpg" alt="The XGS 3300 opened" width="100%"></a><br><sub>The XGS 3300 opened</sub></td>
<td width="25%" valign="top"><a href="hardware/xgs3300/xgs3300-inside.jpg"><img src="hardware/xgs3300/xgs3300-inside.jpg" alt="Inside the appliance" width="100%"></a><br><sub>Inside the appliance</sub></td>
</tr>
</table>

## Measurements

Each folder holds one measurement's photographs - the windows the runs were taken in, as they
finished - and is named after the page that tables them.

### measurements/anomaly-136-2026-10-19/ (1)

| image | what it shows | used on |
|---|---|---|
| [01-xgs136-unusual-traffic.png](measurements/anomaly-136-2026-10-19/01-xgs136-unusual-traffic.png) | The XGS 136: the port scan, the sweep and a stream in hardware, all reported | [anomaly-2026-10-19.md](../measurements/anomaly-2026-10-19.md) |

### measurements/anomaly-2026-10-19/ (3)

The page: [anomaly-2026-10-19.md](../measurements/anomaly-2026-10-19.md).

| image | what it shows | used on |
|---|---|---|
| [01-xgs3300-scan-sweep-volume.png](measurements/anomaly-2026-10-19/01-xgs3300-scan-sweep-volume.png) | Run 1: the port scan and the volume reported, the sweep missed | [anomaly-2026-10-19.md](../measurements/anomaly-2026-10-19.md) |
| [02-xgs3300-readings-every-ten-seconds.png](measurements/anomaly-2026-10-19/02-xgs3300-readings-every-ten-seconds.png) | Run 2: readings every ten seconds - the port scan, the sweep and the volume, all reported | [anomaly-2026-10-19.md](../measurements/anomaly-2026-10-19.md) |
| [03-xgs3300-log-page.png](measurements/anomaly-2026-10-19/03-xgs3300-log-page.png) | XGS: Log File filtered on xgs-anomaly: the scans, the sweep and the volumes of both runs | [anomaly-2026-10-19.md](../measurements/anomaly-2026-10-19.md) |

### measurements/bench-2026-10-17/ (16)

The page: [bench-2026-10-17.md](../measurements/bench-2026-10-17.md).

| image | what it shows | used on |
|---|---|---|
| [01-clear-pc-to-old3300.png](measurements/bench-2026-10-17/01-clear-pc-to-old3300.png) | clear text, PC to the first XGS 3300, one stream: 1.85 Gbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |
| [02-clear-old3300-to-pc.png](measurements/bench-2026-10-17/02-clear-old3300-to-pc.png) | clear text, the first XGS 3300 to the PC, one stream: 3.00 Gbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |
| [03-clear-pc-to-old3300-p4.png](measurements/bench-2026-10-17/03-clear-pc-to-old3300-p4.png) | clear text, PC to the first XGS 3300, four streams: 2.68 Gbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |
| [04-clear-old3300-to-pc-p4.png](measurements/bench-2026-10-17/04-clear-old3300-to-pc-p4.png) | clear text, the first XGS 3300 to the PC, four streams: 4.60 Gbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |
| [05-clear-pc-to-136.png](measurements/bench-2026-10-17/05-clear-pc-to-136.png) | clear text, PC to the XGS 136 before issue 415: 18.3 Mbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |
| [06-clear-136-to-pc.png](measurements/bench-2026-10-17/06-clear-136-to-pc.png) | clear text, the XGS 136 to the PC before issue 415: 347 Mbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |
| [20-ipsec-sw-pc-to-old3300.png](measurements/bench-2026-10-17/20-ipsec-sw-pc-to-old3300.png) | IPsec, the second 3300 in software, PC to the first XGS 3300: 1.25 Gbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |
| [21-ipsec-sw-old3300-to-pc.png](measurements/bench-2026-10-17/21-ipsec-sw-old3300-to-pc.png) | IPsec, the second 3300 in software, the first XGS 3300 to the PC: 2.73 Gbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |
| [22-ipsec-new-sw-136-hw-pc-to-136.png](measurements/bench-2026-10-17/22-ipsec-new-sw-136-hw-pc-to-136.png) | IPsec, the second 3300 in software and the 136 in hardware, PC to the XGS 136: 668 Mbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |
| [23-ipsec-new-sw-136-hw-136-to-pc.png](measurements/bench-2026-10-17/23-ipsec-new-sw-136-hw-136-to-pc.png) | IPsec, the second 3300 in software and the 136 in hardware, the XGS 136 to the PC: 893 Mbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |
| [30-ipsec-hw-pc-to-old3300.png](measurements/bench-2026-10-17/30-ipsec-hw-pc-to-old3300.png) | IPsec in hardware, PC to the first XGS 3300: 1.85 Gbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |
| [31-ipsec-hw-old3300-to-pc.png](measurements/bench-2026-10-17/31-ipsec-hw-old3300-to-pc.png) | IPsec in hardware, the first XGS 3300 to the PC: 2.42 Gbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |
| [32-ipsec-hw-pc-to-old3300-p4.png](measurements/bench-2026-10-17/32-ipsec-hw-pc-to-old3300-p4.png) | IPsec in hardware, PC to the first XGS 3300, four streams: 2.01 Gbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |
| [33-ipsec-hw-old3300-to-pc-p4.png](measurements/bench-2026-10-17/33-ipsec-hw-old3300-to-pc-p4.png) | IPsec in hardware, the first XGS 3300 to the PC, four streams: 2.58 Gbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |
| [34-ipsec-hw-pc-to-136.png](measurements/bench-2026-10-17/34-ipsec-hw-pc-to-136.png) | IPsec in hardware, PC to the XGS 136: 902 Mbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |
| [35-ipsec-hw-136-to-pc.png](measurements/bench-2026-10-17/35-ipsec-hw-136-to-pc.png) | IPsec in hardware, the XGS 136 to the PC: 893 Mbit/s | [bench-2026-10-17.md](../measurements/bench-2026-10-17.md) |

### measurements/boot-series-676-2026-10-25/ (2)

The page: [boot-series-676-2026-10-25.md](../measurements/boot-series-676-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-sfos21-20-boots.png](measurements/boot-series-676-2026-10-25/01-sfos21-20-boots.png) | Twenty boots of the XGS 136 from 12:45 to 13:41 UTC: each with Port1's link active, one address, the gateway answering five of five pings, five to seven frames more in, and the network agent's failures at 0; twenty of twenty received on Port1 | [boot-series-676-2026-10-25.md](../measurements/boot-series-676-2026-10-25.md) |
| [02-sfos21-every-boot.png](measurements/boot-series-676-2026-10-25/02-sfos21-every-boot.png) | The whole log: the ten boots from 12:14 to 12:41 UTC, run before the photographed twenty and not photographed while they ran, then the twenty - thirty boots, every one receiving on Port1 | [boot-series-676-2026-10-25.md](../measurements/boot-series-676-2026-10-25.md) |

### measurements/bridged-2026-10-24/ (14)

The page: [bridged-2026-10-24.md](../measurements/bridged-2026-10-24.md).

| image | what it shows | used on |
|---|---|---|
| [01-3300-before-bridge-no-address.png](measurements/bridged-2026-10-24/01-3300-before-bridge-no-address.png) | Before: the bridge with no address - both directions given the WAN's gateway as their next hop, nothing reached the XGS 136 | [bridged-2026-10-24.md](../measurements/bridged-2026-10-24.md) |
| [02-3300-before-bridge-with-address.png](measurements/bridged-2026-10-24/02-3300-before-bridge-with-address.png) | Before: the bridge with an address - delivered, but every frame from the bridge's MAC with its TTL lowered, both ways | [bridged-2026-10-24.md](../measurements/bridged-2026-10-24.md) |
| [03-3300-after-bridge-no-address.png](measurements/bridged-2026-10-24/03-3300-after-bridge-no-address.png) | After: the bridge with no address - made bridged, each direction out of the other's port, the frames from the sender's MAC with TTL 64 both ways | [bridged-2026-10-24.md](../measurements/bridged-2026-10-24.md) |
| [04-3300-after-bridge-with-address.png](measurements/bridged-2026-10-24/04-3300-after-bridge-with-address.png) | After: the bridge with an address - bridged the same way | [bridged-2026-10-24.md](../measurements/bridged-2026-10-24.md) |
| [05-3300-after-ipv6.png](measurements/bridged-2026-10-24/05-3300-after-ipv6.png) | After: IPv6 across the bridge - bridged, the hop limit 64 both ways | [bridged-2026-10-24.md](../measurements/bridged-2026-10-24.md) |
| [06-3300-after-routed-through-the-members.png](measurements/bridged-2026-10-24/06-3300-after-routed-through-the-members.png) | After: a connection the XGS 3300 routes through the same two member ports - still routed, its frames from the ports' own MACs with the TTL lowered | [bridged-2026-10-24.md](../measurements/bridged-2026-10-24.md) |
| [07-3300-after-bridge-flows-off.png](measurements/bridged-2026-10-24/07-3300-after-bridge-flows-off.png) | After, dp.bridge_flows 0: nothing in hardware, the host's bridge carries the connection | [bridged-2026-10-24.md](../measurements/bridged-2026-10-24.md) |
| [08-136-before.png](measurements/bridged-2026-10-24/08-136-before.png) | Before: the XGS 136's bridge carried every frame of the connection itself | [bridged-2026-10-24.md](../measurements/bridged-2026-10-24.md) |
| [09-136-after.png](measurements/bridged-2026-10-24/09-136-after.png) | After: both of iperf3's connections made bridged, the frames from the PC's own MAC with its TTL, the host's bridge taking a fraction of them | [bridged-2026-10-24.md](../measurements/bridged-2026-10-24.md) |
| [10-136-after-bridge-flows-off.png](measurements/bridged-2026-10-24/10-136-after-bridge-flows-off.png) | After, flow.bridge_flows 0: the host's bridge carries the connection, as before | [bridged-2026-10-24.md](../measurements/bridged-2026-10-24.md) |
| [11-136-after-state-killed.png](measurements/bridged-2026-10-24/11-136-after-state-killed.png) | The XGS 136: two connections in the table, their four states killed at second 6, none three seconds later, the stream at 0 | [bridged-2026-10-24.md](../measurements/bridged-2026-10-24.md) |
| [12-3300-after-state-killed.png](measurements/bridged-2026-10-24/12-3300-after-state-killed.png) | The XGS 3300: the connection in hardware, its four states killed at second 6, out of hardware three seconds later, the stream at 0 | [bridged-2026-10-24.md](../measurements/bridged-2026-10-24.md) |
| [13-3300-give-backs-bridged-and-routed.png](measurements/bridged-2026-10-24/13-3300-give-backs-bridged-and-routed.png) | Give-backs at 1,500-byte frames: a bridged stream and a routed one, each revived 22 times in ten seconds | [bridged-2026-10-24.md](../measurements/bridged-2026-10-24.md) |
| [14-136-half-bridged-half-routed.png](measurements/bridged-2026-10-24/14-136-half-bridged-half-routed.png) | Half bridged, half routed: the replies addressed to the XGS 136 itself; the driver keeps the connection with the host and says why | [bridged-2026-10-24.md](../measurements/bridged-2026-10-24.md) |

### measurements/cage-medium-2026-10-19/ (3)

The page: [cage-medium-2026-10-19.md](../measurements/cage-medium-2026-10-19.md).

| image | what it shows | used on |
|---|---|---|
| [01-cage-medium-before.png](measurements/cage-medium-2026-10-19/01-cage-medium-before.png) | Before: the 136's F1 says 1000baseSX with a 1000BASE-T module in it; the XGS 3300's F1 and F2 say 10Gbase-SR with copper modules | [cage-medium-2026-10-19.md](../measurements/cage-medium-2026-10-19.md) |
| [02-cage-medium-after.png](measurements/cage-medium-2026-10-19/02-cage-medium-after.png) | After: every cage's port reports its module's medium | [cage-medium-2026-10-19.md](../measurements/cage-medium-2026-10-19.md) |
| [03-xgs136-module-media.png](measurements/cage-medium-2026-10-19/03-xgs136-module-media.png) | The XGS 136's module_media: what it refuses, a cage told nothing, and put back | [cage-medium-2026-10-19.md](../measurements/cage-medium-2026-10-19.md) |

### measurements/channel-claim-wait-2026-10-24/ (3)

The page: [channel-claim-wait-2026-10-24.md](../measurements/channel-claim-wait-2026-10-24.md).

| image | what it shows | used on |
|---|---|---|
| [03-136-before-the-change-claimed-in-time.png](measurements/channel-claim-wait-2026-10-24/03-136-before-the-change-claimed-in-time.png) | The module before the change, reloaded with the inspection function off: the data path at second 132, the channel open and the table confirmed at 132, the heartbeat at 138, a ping across Port8 answered | [channel-claim-wait-2026-10-24.md](../measurements/channel-claim-wait-2026-10-24.md) |
| [04-136-the-wait-20-seconds.png](measurements/channel-claim-wait-2026-10-24/04-136-the-wait-20-seconds.png) | The wait: unclaimed at second 128, the heartbeat at 134, claimed after 20 seconds at 148, the channel open and the table confirmed at 148; the line every 3 s - not reached yet, not yet, confirmed; a ping answered | [channel-claim-wait-2026-10-24.md](../measurements/channel-claim-wait-2026-10-24.md) |
| [05-136-the-wait-given-up.png](measurements/channel-claim-wait-2026-10-24/05-136-the-wait-given-up.png) | Given up: unclaimed at 387, nothing claimed in 120 seconds at 507; the line naming the channel as not opened and why; three pings lost | [channel-claim-wait-2026-10-24.md](../measurements/channel-claim-wait-2026-10-24.md) |

### measurements/cipher-2026-10-25/ (6)

The page: [cipher-2026-10-25.md](../measurements/cipher-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-sha384-sfos22-engine.png](measurements/cipher-2026-10-25/01-sha384-sfos22-engine.png) | The second XGS 3300, vendor version 22, its engine against the first board's kernel with AES-256-CBC and HMAC-SHA-384: 40 pings answered, 932 Mbit/s one way and 886 the other | [cipher-2026-10-25.md](../measurements/cipher-2026-10-25.md) |
| [02-sha384-sfos23-engine.png](measurements/cipher-2026-10-25/02-sha384-sfos23-engine.png) | The first XGS 3300, vendor version 23, its engine against the second board's kernel: 40 pings answered, 850 and 990 Mbit/s | [cipher-2026-10-25.md](../measurements/cipher-2026-10-25.md) |
| [03-sha384-sfos21-engine.png](measurements/cipher-2026-10-25/03-sha384-sfos21-engine.png) | The XGS 136, vendor version 21, its engine against the second XGS 3300's kernel: 40 pings answered, the engine decrypting and encrypting all of them | [cipher-2026-10-25.md](../measurements/cipher-2026-10-25.md) |
| [04-chacha-sfos22-engine.png](measurements/cipher-2026-10-25/04-chacha-sfos22-engine.png) | The second XGS 3300's engine with ChaCha20-Poly1305: no ping answered; its fast path's CRYPTO_DROP_SADB_PRE_ERR up by 50 | [cipher-2026-10-25.md](../measurements/cipher-2026-10-25.md) |
| [05-chacha-sfos23-engine.png](measurements/cipher-2026-10-25/05-chacha-sfos23-engine.png) | The first XGS 3300's engine: no ping answered; its CRYPTO_DROP_SADB_PRE_ERR up by 50 | [cipher-2026-10-25.md](../measurements/cipher-2026-10-25.md) |
| [06-chacha-sfos21-engine.png](measurements/cipher-2026-10-25/06-chacha-sfos21-engine.png) | The XGS 136's engine: no ping answered; its CRYPTO_DROP_SADB_PRE_ERR up by 40 | [cipher-2026-10-25.md](../measurements/cipher-2026-10-25.md) |

### measurements/closing-check-2026-10-24/ (5)

The page: [closing-check-2026-10-24.md](../measurements/closing-check-2026-10-24.md).

| image | what it shows | used on |
|---|---|---|
| [01-136-blind-reset-check-off.png](measurements/closing-check-2026-10-24/01-136-blind-reset-check-off.png) | XGS 136, the check off: the blind RST taken, both states TIME_WAIT, the line after the idle time never arrives | [closing-check-2026-10-24.md](../measurements/closing-check-2026-10-24.md) |
| [02-136-blind-reset-check-on.png](measurements/closing-check-2026-10-24/02-136-blind-reset-check-on.png) | XGS 136, the check on: the RST refused, state-mismatch +1, the states ESTABLISHED through the idle time, the line arrives | [closing-check-2026-10-24.md](../measurements/closing-check-2026-10-24.md) |
| [03-136-twenty-connections-ending.png](measurements/closing-check-2026-10-24/03-136-twenty-connections-ending.png) | Twenty connections ending at once each way through the XGS 136: 897 and 39 FINs and RSTs judged with the fast path's numbers, none handed up without them, state-mismatch +0 | [closing-check-2026-10-24.md](../measurements/closing-check-2026-10-24.md) |
| [04-3300-blind-reset-check-off.png](measurements/closing-check-2026-10-24/04-3300-blind-reset-check-off.png) | XGS 3300, the check off: the blind RST taken, the states gone after the idle time, the line never arrives | [closing-check-2026-10-24.md](../measurements/closing-check-2026-10-24.md) |
| [05-3300-blind-reset-check-on.png](measurements/closing-check-2026-10-24/05-3300-blind-reset-check-on.png) | XGS 3300, the check on: the RST refused, the states ESTABLISHED, the line arrives | [closing-check-2026-10-24.md](../measurements/closing-check-2026-10-24.md) |

### measurements/console-logger-2026-10-25/ (7)

The page: [console-logger-2026-10-25.md](../measurements/console-logger-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-sfos22-sfos21-a-minute-of-silence.png](measurements/console-logger-2026-10-25/01-sfos22-sfos21-a-minute-of-silence.png) | The second XGS 3300 and the XGS 136: a minute of each coprocessor's console, read only, with nothing happening - 0 bytes on both | [console-logger-2026-10-25.md](../measurements/console-logger-2026-10-25.md) |
| [02-sfos23-the-console-logged-through-a-watchdog-reset.png](measurements/console-logger-2026-10-25/02-sfos23-the-console-logged-through-a-watchdog-reset.png) | The vendor's way, dd reading the console with O_SYNC, os-xgs-npu's logger stopped for it: the coprocessor's feeder stopped, the host silent at 65 s and back after 222 s, 0 bytes kept this time | [console-logger-2026-10-25.md](../measurements/console-logger-2026-10-25.md) |
| [03-three-boards-console_log.png](measurements/console-logger-2026-10-25/03-three-boards-console_log.png) | console_log.py on the three boards, vendor version 22, 23 and 21: started; a line written to the coprocessor's console and one from its kernel, both kept; npu-run.sh over the console with the logger running, answered in 26-27 s, the pause gone after; the logger reading again | [console-logger-2026-10-25.md](../measurements/console-logger-2026-10-25.md) |
| [04-sfos23-console_log-through-a-watchdog-reset.png](measurements/console-logger-2026-10-25/04-sfos23-console_log-through-a-watchdog-reset.png) | console_log.py through the same reset: nothing of the coprocessor's boot kept, its next start at the boot | [console-logger-2026-10-25.md](../measurements/console-logger-2026-10-25.md) |
| [05-sfos23-console_log-through-a-watchdog-reset-again.png](measurements/console-logger-2026-10-25/05-sfos23-console_log-through-a-watchdog-reset-again.png) | console_log.py through the same reset again: nothing kept | [console-logger-2026-10-25.md](../measurements/console-logger-2026-10-25.md) |
| [06-sfos23-console_log-through-a-watchdog-reset-a-third-time.png](measurements/console-logger-2026-10-25/06-sfos23-console_log-through-a-watchdog-reset-a-third-time.png) | console_log.py through the reset a third time: the coprocessor's boot stub kept up to its fifth line - OcteonTX SOC, Locking L2 cache, PASS: CRC32 verification, Transferring to thread scheduler - before the host froze | [console-logger-2026-10-25.md](../measurements/console-logger-2026-10-25.md) |
| [07-sfos22-what-console_log-kept-across-a-reboot.png](measurements/console-logger-2026-10-25/07-sfos22-what-console_log-kept-across-a-reboot.png) | What console_log.py kept on the second XGS 3300 across its own reboot: its coprocessor's kernel reporting one of its 10 Gbit/s ports down and up, five times, each when the first XGS 3300 at the cable's other end restarted, then the logger started again at the boot | [console-logger-2026-10-25.md](../measurements/console-logger-2026-10-25.md) |

### measurements/copro-health-2026-10-25/ (2)

The page: [copro-health-2026-10-25.md](../measurements/copro-health-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-three-boards-the-watchdog-and-edac.png](measurements/copro-health-2026-10-25/01-three-boards-the-watchdog-and-edac.png) | The three coprocessors: an sbsa-gwdt watchdog in each, kept fed by busybox "watchdog -t 5"; on both XGS 3300s the memory controller's EDAC and nine cache controllers, 19 corrected-error and 19 uncorrected-error counters, all 0; on the XGS 136 no EDAC controller at all | [copro-health-2026-10-25.md](../measurements/copro-health-2026-10-25.md) |
| [02-sfos23-the-feeder-stopped.png](measurements/copro-health-2026-10-25/02-sfos23-the-feeder-stopped.png) | The first XGS 3300, vendor version 23: the coprocessor's feeder stopped at 05:35:43; the host answering, the heartbeat alive and the coprocessor answering until 56 s; at 63 s the host no longer answers; it booted 220 s after the stop, and its chipset recorded why: the hardware watchdog expired | [copro-health-2026-10-25.md](../measurements/copro-health-2026-10-25.md) |

### measurements/coprocessor-recovery-2026-10-26/ (4)

The page: [coprocessor-recovery-2026-10-26.md](../measurements/coprocessor-recovery-2026-10-26.md).

| image | what it shows | used on |
|---|---|---|
| [01-vendor23-first-3300-console-reboot.png](measurements/coprocessor-recovery-2026-10-26/01-vendor23-first-3300-console-reboot.png) | The first XGS 3300 on 0.1.0-alpha.8.7: its fast path stopped at 20:20:13; taken as gone at 30 s; fastpath.py's recovery from 20:21:00 - its console asked at 20:23:00, the new boot waiting for the host at 20:23:17, the driver up at 20:24:35, the fast path answering again 250 s into the recovery; the host's boot time unchanged, not restarted; the management link up; a ping from the PC through its bridge and one from the second XGS 3300 to oxp0, none lost | [coprocessor-recovery-2026-10-26.md](../measurements/coprocessor-recovery-2026-10-26.md) |
| [02-vendor21-136-reset-before-unload.png](measurements/coprocessor-recovery-2026-10-26/02-vendor21-136-reset-before-unload.png) | The XGS 136 on 0.1.0-alpha.8.7: its fast path and watchdog feeder stopped at 20:16:33; the host unreachable over its LAN from 7 s; fastpath.py's recovery from 20:18:01 - the coprocessor reset from the host, its config space answering at once, the driver unloaded and loaded, up at 20:18:51, the fast path answering again 86 s into the recovery; the host's boot time unchanged, not restarted; the management link answering; the receive path with no frame refused | [coprocessor-recovery-2026-10-26.md](../measurements/coprocessor-recovery-2026-10-26.md) |
| [03-vendor23-first-3300-ipsec-device-reattached.png](measurements/coprocessor-recovery-2026-10-26/03-vendor23-first-3300-ipsec-device-reattached.png) | The first XGS 3300 on octep.ko 54a32be8: the tunnel's two associations mirrored on the coprocessor, five pings into it encrypted there; its fast path stopped at 20:45:36, taken as gone at 30 s; fastpath.py's recovery - the console asked, the new boot waiting for the host, the driver staying loaded with IPsec associations pointing into it, its device detached and attached again, the fast path answering 279 s into the recovery; not restarted; the management link up; five pings into the tunnel encrypted by the kernel, the second XGS 3300's inbound association 420 bytes more; both pings through the appliance answered | [coprocessor-recovery-2026-10-26.md](../measurements/coprocessor-recovery-2026-10-26.md) |
| [04-vendor22-second-3300-ipsec-device-reattached.png](measurements/coprocessor-recovery-2026-10-26/04-vendor22-second-3300-ipsec-device-reattached.png) | The second XGS 3300 on octep.ko 54a32be8, its own record: usfp and the watchdog feeder stopped; the heartbeat dead and the coprocessor gone at 28 s, alive again at 209 s; recover.sh - the coprocessor booted again by its own watchdog and waiting for the host, the driver staying loaded with IPsec associations pointing into it, its device detached and attached again, the fast path answering 202 s into the recovery; two associations mirrored on its coprocessor again; the management link up; the host's boot the same as the run's | [coprocessor-recovery-2026-10-26.md](../measurements/coprocessor-recovery-2026-10-26.md) |

### measurements/counters-clear-2026-10-25/ (1)

The page: [counters-clear-2026-10-25.md](../measurements/counters-clear-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-three-boards.png](measurements/counters-clear-2026-10-25/01-three-boards.png) | The three boards: on the second XGS 3300 (vendor version 22) the fast-path clear went out as 48 and the counters fell from a sum of 4,443 to 312 two seconds later; on the first (vendor version 23) as 49, from 12,349 to 2; on the XGS 136 (vendor version 21) as 47, from 43,920 to 21; the other clears 47, 49, 50 and 48, 50, 51; on the XGS 136 the direct-forwarding clear not sent, vendor version 21 having none | [counters-clear-2026-10-25.md](../measurements/counters-clear-2026-10-25.md) |

### measurements/cp-hardware-2026-10-24/ (10)

The page: [cp-hardware-2026-10-24.md](../measurements/cp-hardware-2026-10-24.md).

| image | what it shows | used on |
|---|---|---|
| [01-3300-hardware.png](measurements/cp-hardware-2026-10-24/01-3300-hardware.png) | XGS 3300: a zone in hardware; logged off at second 11, the client out of the portal's table at 13 and out of the driver's at 14, the stream at nothing from second 13 | [cp-hardware-2026-10-24.md](../measurements/cp-hardware-2026-10-24.md) |
| [02-3300-hard.png](measurements/cp-hardware-2026-10-24/02-3300-hard.png) | XGS 3300: a two-minute hard timeout; the session ended 124 s after the login, the stream in hardware until then | [cp-hardware-2026-10-24.md](../measurements/cp-hardware-2026-10-24.md) |
| [03-3300-idle.png](measurements/cp-hardware-2026-10-24/03-3300-idle.png) | XGS 3300: a one-minute idle timeout through a 100-second stream in hardware: ipfw alone last saw the client up to 41 s before, the portal 3 to 6 s before, and the session stayed | [cp-hardware-2026-10-24.md](../measurements/cp-hardware-2026-10-24.md) |
| [04-3300-radius.png](measurements/cp-hardware-2026-10-24/04-3300-radius.png) | XGS 3300: RADIUS accounting; the interim and stop messages carry 36.1 and 38.3 GB from the client, nearly all of it carried by the coprocessor | [cp-hardware-2026-10-24.md](../measurements/cp-hardware-2026-10-24.md) |
| [05-3300-guard.png](measurements/cp-hardware-2026-10-24/05-3300-guard.png) | XGS 3300: the guard; the portal's file replaced by an unknown version - the zone back to the host and the stream through it - then the release's file back and the zone in hardware again | [cp-hardware-2026-10-24.md](../measurements/cp-hardware-2026-10-24.md) |
| [06-136-hardware.png](measurements/cp-hardware-2026-10-24/06-136-hardware.png) | XGS 136: a zone in hardware; logged off at second 11, the client out of both tables by second 13, the user deleted and its connections taken out | [cp-hardware-2026-10-24.md](../measurements/cp-hardware-2026-10-24.md) |
| [07-136-hard.png](measurements/cp-hardware-2026-10-24/07-136-hard.png) | XGS 136: a two-minute hard timeout, the session ended 123 s after the login; the portal's count of 13 GB is the coprocessor's | [cp-hardware-2026-10-24.md](../measurements/cp-hardware-2026-10-24.md) |
| [08-136-idle.png](measurements/cp-hardware-2026-10-24/08-136-idle.png) | XGS 136: a one-minute idle timeout; the session stayed through a 100-second stream in hardware, which ipfw alone last saw up to 95 s before, and ended 81 s after the stream did | [cp-hardware-2026-10-24.md](../measurements/cp-hardware-2026-10-24.md) |
| [09-136-radius.png](measurements/cp-hardware-2026-10-24/09-136-radius.png) | XGS 136: RADIUS accounting; the interim and stop messages carry 7.5 and 8.9 GB to the client, the driver's count 8.9 GB | [cp-hardware-2026-10-24.md](../measurements/cp-hardware-2026-10-24.md) |
| [10-136-guard.png](measurements/cp-hardware-2026-10-24/10-136-guard.png) | XGS 136: the guard; an unknown version of the portal's file sends the zone to the host, the release's file brings it back | [cp-hardware-2026-10-24.md](../measurements/cp-hardware-2026-10-24.md) |

### measurements/discoveries-2026-10-22/ (4)

| image | what it shows | used on |
|---|---|---|
| [01-xgs3300-switch-tcam.png](measurements/discoveries-2026-10-22/01-xgs3300-switch-tcam.png) | The XGS 3300's switch read through the coprocessor: the TCAM on for ports 1 to 10, 254 of 256 entries in use, entries 0, 1 and 254 decoded, every port's queues weighted | [discoveries-2026-10-22.md](../findings/discoveries-2026-10-22.md) |
| [02-xgs3300-coprocessor-log.png](measurements/discoveries-2026-10-22/02-xgs3300-coprocessor-log.png) | The XGS 3300's coprocessor log: Dragonfly starting with nDPI and its rules, its settings directory, its flow settings, and the transmit rate limit set for each front port | [discoveries-2026-10-22.md](../findings/discoveries-2026-10-22.md) |
| [03-xgs136-coprocessor-log.png](measurements/discoveries-2026-10-22/03-xgs136-coprocessor-log.png) | The XGS 136's coprocessor log: no Dragonfly and no /tmp/df_etc, the rate limit disabled, a random number generator and a watchdog | [discoveries-2026-10-22.md](../findings/discoveries-2026-10-22.md) |
| [where-each-capability-lives.svg](measurements/discoveries-2026-10-22/where-each-capability-lives.svg) | Where each capability found on 2026-10-22 lives: the host, the coprocessor's fast path and its own software, the switch and the front ports, with what is on both appliances and what on the XGS 3300 only | [discoveries-2026-10-22.md](../findings/discoveries-2026-10-22.md) |

### measurements/dos-probe-2026-10-19/ (2)

The page: [dos-probe-2026-10-19.md](../measurements/dos-probe-2026-10-19.md).

| image | what it shows | used on |
|---|---|---|
| [02-xgs3300-the-answers.png](measurements/dos-probe-2026-10-19/02-xgs3300-the-answers.png) | The DoS commands, with the coprocessor's answers: each not dispatched, and the stream untouched | [dos-probe-2026-10-19.md](../measurements/dos-probe-2026-10-19.md) |
| [03-xgs136-the-answers.png](measurements/dos-probe-2026-10-19/03-xgs136-the-answers.png) | The XGS 136's answers to the DoS commands: each refused with rc 0x8006, the stream untouched; the PC's address blurred | [dos-probe-2026-10-19.md](../measurements/dos-probe-2026-10-19.md) |

### measurements/dragonfly-2026-10-25/ (4)

The page: [dragonfly-2026-10-25.md](../measurements/dragonfly-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-sfos22-on-for-the-pcs-port.png](measurements/dragonfly-2026-10-25/01-sfos22-on-for-the-pcs-port.png) | The second XGS 3300, vendor version 22: Dragonfly turned on for oxp0, the PC's port - pings, DNS and HTTPS to the board answered, a stream through it at 9.67 Gbit/s; 1,621,926 more packets inspected, 6 flows exported in 2 messages, none failed; nothing on any of the host's 24 interfaces, its rings or the coprocessor's own interfaces | [dragonfly-2026-10-25.md](../measurements/dragonfly-2026-10-25.md) |
| [02-sfos22-the-tunnels-port.png](measurements/dragonfly-2026-10-25/02-sfos22-the-tunnels-port.png) | The second XGS 3300, vendor version 22: Dragonfly on for oxp12, where the tunnel's ESP from the first board arrives - plain traffic on the link answered and streamed at 9.02 Gbit/s, the tunnel's 10 pings none; the 10 replies received, sent for decryption, through Dragonfly, and counted nowhere after; off again, the tunnel answered | [dragonfly-2026-10-25.md](../measurements/dragonfly-2026-10-25.md) |
| [03-sfos23-the-tunnels-port.png](measurements/dragonfly-2026-10-25/03-sfos23-the-tunnels-port.png) | The first XGS 3300, vendor version 23: the same on oxp0, the tunnel's other end - plain traffic 5 of 5 and 8.89 Gbit/s, the tunnel 0 of 10; the 10 pings received, sent for decryption, through Dragonfly, and nothing after; off again, 5 of 5 | [dragonfly-2026-10-25.md](../measurements/dragonfly-2026-10-25.md) |
| [04-sfos21-xgs136-no-dragonfly.png](measurements/dragonfly-2026-10-25/04-sfos21-xgs136-no-dragonfly.png) | The XGS 136, vendor version 21: no line naming Dragonfly in its coprocessor's log, no /tmp/df_etc, no /opt/sophos/dragonfly; 176 fast-path counters, none of them Dragonfly's; no command to read its counters in vendor version 21's numbering | [dragonfly-2026-10-25.md](../measurements/dragonfly-2026-10-25.md) |

### measurements/esn-136-2026-10-20/ (9)

The page: [esn-136-2026-10-20.md](../measurements/esn-136-2026-10-20.md).

| image | what it shows | used on |
|---|---|---|
| [01-plain.png](measurements/esn-136-2026-10-20/01-plain.png) | The plain tunnel, for comparison | [esn-136-2026-10-20.md](../measurements/esn-136-2026-10-20.md) |
| [02-mode-esn.png](measurements/esn-136-2026-10-20/02-mode-esn.png) | The child made again with extended sequence numbers: the 3300 selects AES_GCM_16_256/EXT_SEQ; the 136's outbound association on the engine, its inbound one left to the kernel | [esn-136-2026-10-20.md](../measurements/esn-136-2026-10-20.md) |
| [03-esn-outbound.png](measurements/esn-136-2026-10-20/03-esn-outbound.png) | ESN outbound only: 866 Mbit/s from the 136, the 3300's kernel taking every frame the 136 encrypted | [esn-136-2026-10-20.md](../measurements/esn-136-2026-10-20.md) |
| [04-mode-esn-inbound.png](measurements/esn-136-2026-10-20/04-mode-esn-inbound.png) | The child made again with esn_decrypt on: the inbound association accepted by the engine | [esn-136-2026-10-20.md](../measurements/esn-136-2026-10-20.md) |
| [05-esn-both-ways.png](measurements/esn-136-2026-10-20/05-esn-both-ways.png) | ESN both ways for 90 seconds: 984 and 979 Mbit/s | [esn-136-2026-10-20.md](../measurements/esn-136-2026-10-20.md) |
| [06-esn-inbound-long.png](measurements/esn-136-2026-10-20/06-esn-inbound-long.png) | ESN inbound, four streams for three minutes: 985 Mbit/s, rx_bad and rx_nosa 0 | [esn-136-2026-10-20.md](../measurements/esn-136-2026-10-20.md) |
| [07-default-mode.png](measurements/esn-136-2026-10-20/07-default-mode.png) | The build that ships, after a reboot: both ESN associations accepted by the engine | [esn-136-2026-10-20.md](../measurements/esn-136-2026-10-20.md) |
| [08-default-run.png](measurements/esn-136-2026-10-20/08-default-run.png) | The build that ships: 387 and 983 Mbit/s | [esn-136-2026-10-20.md](../measurements/esn-136-2026-10-20.md) |
| [09-3300-kernel.png](measurements/esn-136-2026-10-20/09-3300-kernel.png) | The 3300's kernel ESP at the end: 5,796,289 in, no authentication failure, no replay | [esn-136-2026-10-20.md](../measurements/esn-136-2026-10-20.md) |

### measurements/esn-3300-2026-10-20/ (1)

The page: [esn-3300-2026-10-20.md](../measurements/esn-3300-2026-10-20.md).

| image | what it shows | used on |
|---|---|---|
| [01-esn-arriving-on-the-engine.png](measurements/esn-3300-2026-10-20/01-esn-arriving-on-the-engine.png) | The second XGS 3300 on module f7eede10, nothing set: an ESN association arriving on its engine through the 2026-10-12 sequence at 983 to 985 Mbit/s, then three minutes of 1,500-byte frames at 905 Mbit/s, 15,169,983 frames decrypted, no drop or authentication counter moved | [esn-3300-2026-10-20.md](../measurements/esn-3300-2026-10-20.md) |

### measurements/flow-dscp-2026-10-24/ (6)

The page: [flow-dscp-2026-10-24.md](../measurements/flow-dscp-2026-10-24.md).

| image | what it shows | used on |
|---|---|---|
| [01-3300-pf-mark-host-before-after.png](measurements/flow-dscp-2026-10-24/01-3300-pf-mark-host-before-after.png) | EF on the host path, then in hardware as before today - the mark lost both ways - and in hardware with pf's mark | [flow-dscp-2026-10-24.md](../measurements/flow-dscp-2026-10-24.md) |
| [02-3300-pf-mark-values.png](measurements/flow-dscp-2026-10-24/02-3300-pf-mark-values.png) | AF41 and CS1 carried both ways; 0xB9 kept with the host by the driver; no rule, no mark | [flow-dscp-2026-10-24.md](../measurements/flow-dscp-2026-10-24.md) |
| [03-3300-pf-mark-ecn.png](measurements/flow-dscp-2026-10-24/03-3300-pf-mark-ecn.png) | The sender asking for ECN: the data frames 0xba on the host path and in hardware, the acknowledgements 0xb8 | [flow-dscp-2026-10-24.md](../measurements/flow-dscp-2026-10-24.md) |
| [04-3300-override-instrument.png](measurements/flow-dscp-2026-10-24/04-3300-override-instrument.png) | The override by hand: off 0x0; 46, 184 and 10 written as 0xb8, 0xe0 and 0x28 | [flow-dscp-2026-10-24.md](../measurements/flow-dscp-2026-10-24.md) |
| [05-3300-final-build.png](measurements/flow-dscp-2026-10-24/05-3300-final-build.png) | The module as merged, 0fec828d: EF lost in hardware as before, and carried both ways with pf's mark | [flow-dscp-2026-10-24.md](../measurements/flow-dscp-2026-10-24.md) |
| [06-136-pf-mark.png](measurements/flow-dscp-2026-10-24/06-136-pf-mark.png) | The XGS 136: EF on the host path, lost in hardware as before, carried with pf's mark; AF41 carried; 0xB9 kept with the host | [flow-dscp-2026-10-24.md](../measurements/flow-dscp-2026-10-24.md) |

### measurements/flow-dscp6-2026-10-24/ (4)

The page: [flow-dscp6-2026-10-24.md](../measurements/flow-dscp6-2026-10-24.md).

| image | what it shows | used on |
|---|---|---|
| [01-3300-ipv6-mark-kept-with-the-host.png](measurements/flow-dscp6-2026-10-24/01-3300-ipv6-mark-kept-with-the-host.png) | IPv6 through the second XGS 3300: no rule, no mark; EF on the host path, written by pf both ways; EF as the driver keeps it, with the host | [flow-dscp6-2026-10-24.md](../measurements/flow-dscp6-2026-10-24.md) |
| [02-3300-override-ipv4-then-ipv6.png](measurements/flow-dscp6-2026-10-24/02-3300-override-ipv4-then-ipv6.png) | The override by hand on one path and one build: IPv4 0xb8 both ways, IPv6 0x00 both ways at 46 and at 10 | [flow-dscp6-2026-10-24.md](../measurements/flow-dscp6-2026-10-24.md) |
| [03-136-ipv6-mark-in-hardware.png](measurements/flow-dscp6-2026-10-24/03-136-ipv6-mark-in-hardware.png) | IPv6 through the XGS 136: no rule, no mark; EF on the host path; EF kept with the host as the driver has it; EF and AF41 made in hardware with the mark, and lost | [flow-dscp6-2026-10-24.md](../measurements/flow-dscp6-2026-10-24.md) |
| [04-both-final-builds.png](measurements/flow-dscp6-2026-10-24/04-both-final-builds.png) | The modules as merged: IPv6 with EF kept with the host on both, 0xb8; the XGS 136's instrument, in hardware, 0x00 | [flow-dscp6-2026-10-24.md](../measurements/flow-dscp6-2026-10-24.md) |

### measurements/frontports-2026-10-25/ (5)

The page: [frontports-2026-10-25.md](../measurements/frontports-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-sfos22-a-frame-for-the-other-port.png](measurements/frontports-2026-10-25/01-sfos22-a-frame-for-the-other-port.png) | The second XGS 3300, vendor version 22: ten frames into its Port6 addressed to the MAC of the device on its Port8 - the switch took in 10 at port 6 and sent out none at port 8, none arrived, none on the host's captures, and the fast path dropped all ten as not addressed to the port | [frontports-2026-10-25.md](../measurements/frontports-2026-10-25.md) |
| [02-sfos22-routed-a-rule-the-states-killed.png](measurements/frontports-2026-10-25/02-sfos22-routed-a-rule-the-states-killed.png) | The second XGS 3300: routed between Port6 and Port8 - pings and a connect through, a stream at 710 Mbit/s in hardware; the block rule: the stream on at 712 and 713, new pings none and the connect refused, the rule's count 7; the states killed: the stream at 0 and out of hardware; the rule removed: through again | [frontports-2026-10-25.md](../measurements/frontports-2026-10-25.md) |
| [03-sfos21-xgs136-a-frame-for-the-other-port.png](measurements/frontports-2026-10-25/03-sfos21-xgs136-a-frame-for-the-other-port.png) | The XGS 136, vendor version 21: the same into its Port7 - the switch took in 10 at port 7 and sent out none at port 8, none arrived, and the host saw the ten on Port7 | [frontports-2026-10-25.md](../measurements/frontports-2026-10-25.md) |
| [04-sfos21-xgs136-routed-a-rule-the-states-killed.png](measurements/frontports-2026-10-25/04-sfos21-xgs136-routed-a-rule-the-states-killed.png) | The XGS 136: the same between its Port7 and Port8 - the stream at 912 Mbit/s in hardware; with the rule, on at 899 and 893 while new pings and the connect were refused; the states killed: 0, out of hardware; the rule removed: through again | [frontports-2026-10-25.md](../measurements/frontports-2026-10-25.md) |
| [05-three-boards-the-tcam.png](measurements/frontports-2026-10-25/05-three-boards-the-tcam.png) | Each board's TCAM as its firmware leaves it: on for ports 1 to 10 and off for port 0 on all three; 254 of 256 entries in use on both XGS 3300s, vendor version 22 and 23, and on the XGS 136 a walk of the entries that did not end | [frontports-2026-10-25.md](../measurements/frontports-2026-10-25.md) |

### measurements/giu-goodbye-2026-10-25/ (1)

The page: [giu-goodbye-2026-10-25.md](../measurements/giu-goodbye-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [02-sfos21-six-reloads-goodbye-first.png](measurements/giu-goodbye-2026-10-25/02-sfos21-six-reloads-goodbye-first.png) | Six reloads of the XGS 136 with npuep.ko a20cdaf6 in /root/giu741: the first unloads the previous build and its PF_DISABLE fails, the command never taken; the other five say "giu: datapath disabled"; each new load has all fourteen ports confirmed and a ping to the second XGS 3300 answered | [giu-goodbye-2026-10-25.md](../measurements/giu-goodbye-2026-10-25.md) |

### measurements/heartbeat-2026-10-22/ (12)

The page: [heartbeat-2026-10-22.md](../measurements/heartbeat-2026-10-22.md).

| image | what it shows | used on |
|---|---|---|
| [01-3300-state.png](measurements/heartbeat-2026-10-22/01-3300-state.png) | XGS 3300, octep.ko 7937346d: 1,092 beats sent, 1,084 answered and 8 answered too late (unmatched), the fast path's own count of control messages from the host 1,092; one late, one taken as stopped and one revival, from the run below; the first answer of the boot in 139 microseconds | [heartbeat-2026-10-22.md](../measurements/heartbeat-2026-10-22.md) |
| [02-3300-a-minute-of-beats.png](measurements/heartbeat-2026-10-22/02-3300-a-minute-of-beats.png) | XGS 3300, a minute of beats: every one answered, 129 to 139 microseconds | [heartbeat-2026-10-22.md](../measurements/heartbeat-2026-10-22.md) |
| [03-3300-four-streams-through-it.png](measurements/heartbeat-2026-10-22/03-3300-four-streams-through-it.png) | XGS 3300, four streams down through it at 6.13 Gbit/s: every beat answered, 20 to 131 microseconds | [heartbeat-2026-10-22.md](../measurements/heartbeat-2026-10-22.md) |
| [04-3300-fast-path-stopped-45-s.png](measurements/heartbeat-2026-10-22/04-3300-fast-path-stopped-45-s.png) | XGS 3300, usfp stopped on the coprocessor for 45 seconds: late after 19 s, taken as stopped at 30 s, alive again after 48 s without an answer, revivals 1; the eight beats sent while it was stopped answered when it came back, as unmatched; the longest answer 3.85 s, the beat sent just before it went on | [heartbeat-2026-10-22.md](../measurements/heartbeat-2026-10-22.md) |
| [05-136-state.png](measurements/heartbeat-2026-10-22/05-136-state.png) | XGS 136, npuep.ko 925b6378, as installed: the heartbeat off, nothing sent, the fast path counting no control message from the host | [heartbeat-2026-10-22.md](../measurements/heartbeat-2026-10-22.md) |
| [06-136-heartbeat-turned-on-30-s.png](measurements/heartbeat-2026-10-22/06-136-heartbeat-turned-on-30-s.png) | XGS 136, the heartbeat turned on for 30 seconds: 7 beats sent, none answered, the fast path's FROM_KN_DROP_CMSG 7 - waiting, late at 15 s, taken as stopped at 30 s - then off again | [heartbeat-2026-10-22.md](../measurements/heartbeat-2026-10-22.md) |
| [07-3300-four-streams-heartbeat-off.png](measurements/heartbeat-2026-10-22/07-3300-four-streams-heartbeat-off.png) | XGS 3300, the same four streams with the heartbeat off: 5.90 Gbit/s | [heartbeat-2026-10-22.md](../measurements/heartbeat-2026-10-22.md) |
| [08-3300-stopped-restart-after-2-min.png](measurements/heartbeat-2026-10-22/08-3300-stopped-restart-after-2-min.png) | XGS 3300, set to restart after 2 minutes: usfp stopped for 200 seconds - late, then taken as stopped at 30 s, the appliance gone at 158 s without an answer, and back after the boot with the heartbeat answering | [heartbeat-2026-10-22.md](../measurements/heartbeat-2026-10-22.md) |
| [09-3300-after-the-restart.png](measurements/heartbeat-2026-10-22/09-3300-after-the-restart.png) | XGS 3300 after it: the watcher's line in the log - not answered for 158 s, restarting as XGS: Settings says - two restarts in 24 hours (this and a first test), the fast-path-stopped report in the queue, and both shutdowns clean | [heartbeat-2026-10-22.md](../measurements/heartbeat-2026-10-22.md) |
| [10-136-state-beats-taken.png](measurements/heartbeat-2026-10-22/10-136-state-beats-taken.png) | XGS 136, npuep.ko a38947c1: 46 beats sent, 46 taken and 46 counted by the fast path; two stops of 40 seconds, each late, then taken as stopped, then answering again after 41 and 44 s | [heartbeat-2026-10-22.md](../measurements/heartbeat-2026-10-22.md) |
| [11-136-fast-path-stopped-40-s.png](measurements/heartbeat-2026-10-22/11-136-fast-path-stopped-40-s.png) | XGS 136, usfp stopped for 40 seconds, sampled on the appliance itself - this PC reaches it through the fast path: the beat sent at the stop not taken, late at 15 s, taken as stopped at 30 s, and taken, answering again, once usfp went on | [heartbeat-2026-10-22.md](../measurements/heartbeat-2026-10-22.md) |
| [12-136-what-answers-with-usfp-stopped.png](measurements/heartbeat-2026-10-22/12-136-what-answers-with-usfp-stopped.png) | XGS 136, usfp stopped for 25 seconds and everything asked: the ping through the front ports lost, the RPC channel's two reads answered at once, the heartbeat's beat waiting to be taken - late at 15 s - and taken once usfp went on | [heartbeat-2026-10-22.md](../measurements/heartbeat-2026-10-22.md) |

### measurements/hw-counters-2026-10-19/ (8)

The page: [hw-counters-2026-10-19.md](../measurements/hw-counters-2026-10-19.md).

| image | what it shows | used on |
|---|---|---|
| [01-xgs3300-4gb.png](measurements/hw-counters-2026-10-19/01-xgs3300-4gb.png) | Run 1: 4,000,000,000 bytes, counted by the coprocessor and by pf, against what the VM received | [hw-counters-2026-10-19.md](../measurements/hw-counters-2026-10-19.md) |
| [02-xgs3300-4gb-retransmissions.png](measurements/hw-counters-2026-10-19/02-xgs3300-4gb-retransmissions.png) | Run 2: the same, with the PC's retransmission count | [hw-counters-2026-10-19.md](../measurements/hw-counters-2026-10-19.md) |
| [03-xgs3300-reused-slot.png](measurements/hw-counters-2026-10-19/03-xgs3300-reused-slot.png) | Run 3: a connection on a slot that had just carried a stream, counted from its own first byte | [hw-counters-2026-10-19.md](../measurements/hw-counters-2026-10-19.md) |
| [04-xgs3300-given-to-pf.png](measurements/hw-counters-2026-10-19/04-xgs3300-given-to-pf.png) | Run 4: the coprocessor's counts given to pf - its states, and the rules that passed them | [hw-counters-2026-10-19.md](../measurements/hw-counters-2026-10-19.md) |
| [05-xgs3300-port-counts.png](measurements/hw-counters-2026-10-19/05-xgs3300-port-counts.png) | The port counts, summed over every message: a stream in hardware, and the same through the host | [hw-counters-2026-10-19.md](../measurements/hw-counters-2026-10-19.md) |
| [06-xgs3300-sessions-page.png](measurements/hw-counters-2026-10-19/06-xgs3300-sessions-page.png) | Firewall: Diagnostics: Sessions with the coprocessor's counts given to pf: each stream at 80 GB and more | [hw-counters-2026-10-19.md](../measurements/hw-counters-2026-10-19.md) |
| [07-xgs3300-port-counts-applied.png](measurements/hw-counters-2026-10-19/07-xgs3300-port-counts-applied.png) | The port counts added to the interfaces' counters: without, the host's interfaces saw 45 packets of a 6.65 GB stream; with, the whole of it | [hw-counters-2026-10-19.md](../measurements/hw-counters-2026-10-19.md) |
| [08-xgs3300-traffic-graph.png](measurements/hw-counters-2026-10-19/08-xgs3300-traffic-graph.png) | Reporting: Traffic for F1 and F2 with the port counts added: two streams in hardware at about 4.5 Gbit/s | [hw-counters-2026-10-19.md](../measurements/hw-counters-2026-10-19.md) |

### measurements/ids-note-2026-10-24/ (6)

The page: [ids-note-2026-10-24.md](../measurements/ids-note-2026-10-24.md).

| image | what it shows | used on |
|---|---|---|
| [01-3300-ids-mode-and-the-coprocessor.png](measurements/ids-note-2026-10-24/01-3300-ids-mode-and-the-coprocessor.png) | XGS 3300: the Intrusion Detection in IDS mode on F1; with the flow maker on, a 10 s stream of 4.24 GB in hardware and 63 packets captured by Suricata; with it off, 2,148,522 | [ids-note-2026-10-24.md](../measurements/ids-note-2026-10-24.md) |
| [02-136-ids-mode-and-the-coprocessor.png](measurements/ids-note-2026-10-24/02-136-ids-mode-and-the-coprocessor.png) | XGS 136: the same on Port8; with the flow maker on, 92,123 packets captured - about the first second of the stream; with it off, 1,613,702 | [ids-note-2026-10-24.md](../measurements/ids-note-2026-10-24.md) |
| [03-3300-overview-the-note.png](measurements/ids-note-2026-10-24/03-3300-overview-the-note.png) | XGS 3300, XGS: Overview: the Intrusion Detection listens on F1 in IDS mode, and the warning above the page says what Suricata does not see and what to do | [ids-note-2026-10-24.md](../measurements/ids-note-2026-10-24.md) |
| [04-3300-overview-the-note-arabic.png](measurements/ids-note-2026-10-24/04-3300-overview-the-note-arabic.png) | The same in Arabic | [ids-note-2026-10-24.md](../measurements/ids-note-2026-10-24.md) |
| [05-3300-settings-the-note.png](measurements/ids-note-2026-10-24/05-3300-settings-the-note.png) | XGS 3300, XGS: Settings: the same warning above the tabs, over the settings it names | [ids-note-2026-10-24.md](../measurements/ids-note-2026-10-24.md) |
| [06-136-overview-the-note.png](measurements/ids-note-2026-10-24/06-136-overview-the-note.png) | XGS 136, XGS: Overview: the warning for its Port8 | [ids-note-2026-10-24.md](../measurements/ids-note-2026-10-24.md) |

### measurements/iftable-why-2026-10-24/ (2)

The page: [iftable-why-2026-10-24.md](../measurements/iftable-why-2026-10-24.md).

| image | what it shows | used on |
|---|---|---|
| [01-136-the-line-and-verify.png](measurements/iftable-why-2026-10-24/01-136-the-line-and-verify.png) | The XGS 136: dev.npuep.0.iftable reading confirmed, and verify.sh passing with the model, assembly AMDA0201-0003 and board revision 08 at the top | [iftable-why-2026-10-24.md](../measurements/iftable-why-2026-10-24.md) |
| [02-136-verify-printing-a-table-not-confirmed.png](measurements/iftable-why-2026-10-24/02-136-verify-printing-a-table-not-confirmed.png) | verify.sh with the sample line: FAIL, the reason under it, the driver's messages from the boot under that | [iftable-why-2026-10-24.md](../measurements/iftable-why-2026-10-24.md) |

### measurements/image-136-2026-10-21/ (1)

The page: [image-136-2026-10-21.md](../measurements/image-136-2026-10-21.md).

| image | what it shows | used on |
|---|---|---|
| [01-installed-updated-restored.png](measurements/image-136-2026-10-21/01-installed-updated-restored.png) | The XGS 136's own record: the release the image installed, the driver built at the first boot from
the sources the image carries, the update to 26.7.7 with the reboot held until the driver was built
for the new kernel, and the IPsec offload, ports and route after the restored configuration | [image-136-2026-10-21.md](../measurements/image-136-2026-10-21.md) |

### measurements/inspect-2026-10-23/ (13)

The page: [inspect-2026-10-23.md](../measurements/inspect-2026-10-23.md).

| image | what it shows | used on |
|---|---|---|
| [01-3300-tcp-to-the-inspector.png](measurements/inspect-2026-10-23/01-3300-tcp-to-the-inspector.png) | XGS 3300, TCP, sent to the inspector: 30 frames counted as sent, no error, none back, nothing delivered; the stream at 3,720 Mbit/s again once forwarded | [inspect-2026-10-23.md](../measurements/inspect-2026-10-23.md) |
| [02-3300-udp-to-the-inspector.png](measurements/inspect-2026-10-23/02-3300-udp-to-the-inspector.png) | XGS 3300, UDP at 300 Mbit/s: 167,677 frames sent to the inspector in six seconds, no error, none back, the last frame with an unclaimed tag a control message | [inspect-2026-10-23.md](../measurements/inspect-2026-10-23.md) |
| [03-136-tcp-to-the-inspector.png](measurements/inspect-2026-10-23/03-136-tcp-to-the-inspector.png) | XGS 136, TCP: 48 frames sent to the inspector, none back, nothing delivered; 1,006 Mbit/s again once forwarded | [inspect-2026-10-23.md](../measurements/inspect-2026-10-23.md) |
| [04-136-udp-to-the-inspector.png](measurements/inspect-2026-10-23/04-136-udp-to-the-inspector.png) | XGS 136, UDP at 300 Mbit/s: 151,867 frames sent to the inspector, no error, none back | [inspect-2026-10-23.md](../measurements/inspect-2026-10-23.md) |
| [05-3300-tcp-inspected-and-back.png](measurements/inspect-2026-10-23/05-3300-tcp-inspected-and-back.png) | XGS 3300, a TCP stream set to inspection and handed straight back by the driver: 4,856 Mbit/s, every frame through this host and back, none dropped | [inspect-2026-10-23.md](../measurements/inspect-2026-10-23.md) |
| [06-3300-udp-inspected-and-back.png](measurements/inspect-2026-10-23/06-3300-udp-inspected-and-back.png) | XGS 3300, a UDP stream at 300 Mbit/s, the same: all of it delivered | [inspect-2026-10-23.md](../measurements/inspect-2026-10-23.md) |
| [07-3300-suricata-blocks-udp-passes-tcp.png](measurements/inspect-2026-10-23/07-3300-suricata-blocks-udp-passes-tcp.png) | XGS 3300, Suricata in IPS mode on oxips0: the UDP stream to port 5204 blocked by its rule, a TCP stream through it at 3,230 Mbit/s | [inspect-2026-10-23.md](../measurements/inspect-2026-10-23.md) |
| [08-3300-inspected-then-hardware.png](measurements/inspect-2026-10-23/08-3300-inspected-then-hardware.png) | XGS 3300, inspection then hardware: a new TCP stream inspected by Suricata for its first megabyte, then forwarded in hardware at 3.82 Gbit/s; a new UDP stream to the blocked port dropped by Suricata | [inspect-2026-10-23.md](../measurements/inspect-2026-10-23.md) |
| [09-136-tcp-inspected-and-back.png](measurements/inspect-2026-10-23/09-136-tcp-inspected-and-back.png) | XGS 136, a TCP stream set to inspection and handed straight back by the driver through the inspector's function: the link full, 401,868 frames there and back, none dropped | [inspect-2026-10-23.md](../measurements/inspect-2026-10-23.md) |
| [10-136-udp-inspected-and-back.png](measurements/inspect-2026-10-23/10-136-udp-inspected-and-back.png) | XGS 136, a UDP stream at 300 Mbit/s, the same: 326 Mbit/s at the far end, all of it | [inspect-2026-10-23.md](../measurements/inspect-2026-10-23.md) |
| [11-136-inspected-then-hardware.png](measurements/inspect-2026-10-23/11-136-inspected-then-hardware.png) | XGS 136, inspection then hardware: a new TCP stream through Suricata, then forwarded in hardware at 930 Mbit/s; a new UDP stream to the blocked port dropped by Suricata, 199,507 drop events | [inspect-2026-10-23.md](../measurements/inspect-2026-10-23.md) |
| [12-136-the-ids-service-on-oxips0.png](measurements/inspect-2026-10-23/12-136-the-ids-service-on-oxips0.png) | XGS 136, Inspect, then hardware through OPNsense's Intrusion Detection service: a new TCP stream inspected, alerted on, then carried in hardware at 941 Mbit/s; a new UDP stream blocked by a drop rule | [inspect-2026-10-23.md](../measurements/inspect-2026-10-23.md) |
| [13-3300-the-ids-service-on-oxips0.png](measurements/inspect-2026-10-23/13-3300-the-ids-service-on-oxips0.png) | XGS 3300, Inspect, then hardware through OPNsense's Intrusion Detection service: a new TCP stream inspected, alerted on, then carried in hardware at 3.98 Gbit/s; a new UDP stream blocked by a drop rule | [inspect-2026-10-23.md](../measurements/inspect-2026-10-23.md) |

### measurements/ipsec-step-2026-10-25/ (3)

The page: [ipsec-step-2026-10-25.md](../measurements/ipsec-step-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-3300-old-sfos23.png](measurements/ipsec-step-2026-10-25/01-3300-old-sfos23.png) | The first XGS 3300, vendor version 23: the outbound association's counts read 0 bytes and 0 packets while the engine's sequence was 4,023,430, then 4,023,441 after ten pings; streams out at 2.24 to 3.11 Gbit/s before the first ask and 1.97 to 3.04 after; SA_REPLAY_UPDATE with sequence 1 answered 0x13; 40 pings after, decrypted by the kernel | [ipsec-step-2026-10-25.md](../measurements/ipsec-step-2026-10-25.md) |
| [02-3300-new-sfos22.png](measurements/ipsec-step-2026-10-25/02-3300-new-sfos22.png) | The second XGS 3300, vendor version 22: counts of 4,353,708 packets while the engine's sequence was 5,538,588, then 5,538,599 after ten pings; streams out at 8.16 to 8.17 Gbit/s before and 7.79 to 8.19 after; SA_REPLAY_UPDATE 0x13; 40 pings after, decrypted by the kernel | [ipsec-step-2026-10-25.md](../measurements/ipsec-step-2026-10-25.md) |
| [03-136-sfos21.png](measurements/ipsec-step-2026-10-25/03-136-sfos21.png) | The XGS 136, vendor version 21: both associations' counts 0 bytes and 0 packets; the outbound one's sequence 0x100017, then 0x100022 after ten pings; SA_HOST_STAT_SYNC answered rc 0 | [ipsec-step-2026-10-25.md](../measurements/ipsec-step-2026-10-25.md) |

### measurements/ipv6-and-queues-2026-10-18/ (3)

The page: [ipv6-and-queues-2026-10-18.md](../measurements/ipv6-and-queues-2026-10-18.md).

| image | what it shows | used on |
|---|---|---|
| [xgs136-txq-2026-10-18-four-queues.png](measurements/ipv6-and-queues-2026-10-18/xgs136-txq-2026-10-18-four-queues.png) | Four queues: 937, 941 and 941 Mbit/s, one stream's frames all on one queue and eight streams' spread over all four, nothing falling back | [ipv6-and-queues-2026-10-18.md](../measurements/ipv6-and-queues-2026-10-18.md) |
| [xgs136-txq-2026-10-18-one-queue.png](measurements/ipv6-and-queues-2026-10-18/xgs136-txq-2026-10-18-one-queue.png) | One queue: 936, 941 and 941 Mbit/s, every frame on the first queue | [ipv6-and-queues-2026-10-18.md](../measurements/ipv6-and-queues-2026-10-18.md) |
| [xgs3300-ipv6.png](measurements/ipv6-and-queues-2026-10-18/xgs3300-ipv6.png) | The run: 9.24 Gbit/s from the PC to the server and 9.27 back, eighteen IPv6 directions listed and all eighteen active in hardware each time | [ipv6-and-queues-2026-10-18.md](../measurements/ipv6-and-queues-2026-10-18.md) |

### measurements/ja4-cut-136-2026-10-19/ (1)

| image | what it shows | used on |
|---|---|---|
| [01-xgs136-ja4-cut.png](measurements/ja4-cut-136-2026-10-19/01-xgs136-ja4-cut.png) | The XGS 136: the listed stream cut at second 10, the one beside it ran on | [ja4-cut-2026-10-19.md](../measurements/ja4-cut-2026-10-19.md) |

### measurements/ja4-cut-2026-10-19/ (2)

The page: [ja4-cut-2026-10-19.md](../measurements/ja4-cut-2026-10-19.md).

| image | what it shows | used on |
|---|---|---|
| [01-xgs3300-now.png](measurements/ja4-cut-2026-10-19/01-xgs3300-now.png) | XGS 3300, the fingerprint listed at second 10 | [ja4-cut-2026-10-19.md](../measurements/ja4-cut-2026-10-19.md) |
| [02-xgs3300-listed.png](measurements/ja4-cut-2026-10-19/02-xgs3300-listed.png) | XGS 3300, the fingerprint listed before the streams | [ja4-cut-2026-10-19.md](../measurements/ja4-cut-2026-10-19.md) |

### measurements/lif-delete-2026-10-24/ (1)

The page: [lif-delete-2026-10-24.md](../measurements/lif-delete-2026-10-24.md).

| image | what it shows | used on |
|---|---|---|
| [01-136-an-interface-entry-removed.png](measurements/lif-delete-2026-10-24/01-136-an-interface-entry-removed.png) | On the XGS 136: an entry created (rc 0, it reads back flags 0x0009), removed (rc 0, it reads back with its forwarding mode 0 - flags 0x0008), removed again (refused, rc 0x9), created again over it (rc 0) and removed; the front ports' table confirmed throughout | [lif-delete-2026-10-24.md](../measurements/lif-delete-2026-10-24.md) |

### measurements/lists-cut-2026-10-19/ (4)

The page: [lists-cut-2026-10-19.md](../measurements/lists-cut-2026-10-19.md).

| image | what it shows | used on |
|---|---|---|
| [01-xgs136-now.png](measurements/lists-cut-2026-10-19/01-xgs136-now.png) | XGS 136, cut at once | [lists-cut-2026-10-19.md](../measurements/lists-cut-2026-10-19.md) |
| [02-xgs136-cron.png](measurements/lists-cut-2026-10-19/02-xgs136-cron.png) | XGS 136, cut by the minute's cron | [lists-cut-2026-10-19.md](../measurements/lists-cut-2026-10-19.md) |
| [03-xgs3300-now.png](measurements/lists-cut-2026-10-19/03-xgs3300-now.png) | XGS 3300, cut at once | [lists-cut-2026-10-19.md](../measurements/lists-cut-2026-10-19.md) |
| [04-xgs3300-cron.png](measurements/lists-cut-2026-10-19/04-xgs3300-cron.png) | XGS 3300, cut by the minute's cron | [lists-cut-2026-10-19.md](../measurements/lists-cut-2026-10-19.md) |

### measurements/lost-interrupt-2026-10-21/ (4)

The page: [lost-interrupt-2026-10-21.md](../measurements/lost-interrupt-2026-10-21.md).

| image | what it shows | used on |
|---|---|---|
| [01-ten-stalls-each-ring-left-clean.png](measurements/lost-interrupt-2026-10-21/01-ten-stalls-each-ring-left-clean.png) | The first soak's report: 456 passes, rxwd_stalls +10, and eight stall records - each ring left by the visit before with int_status 0x0 and cnts 0x0, then holding 44 to 166 packets with its status bit and the count's interrupt bit 62 set, the vector unmasked, nothing pending, and the handler's entry count unchanged | [lost-interrupt-2026-10-21.md](../measurements/lost-interrupt-2026-10-21.md) |
| [03-the-look-four-stalls-548-found.png](measurements/lost-interrupt-2026-10-21/03-the-look-four-stalls-548-found.png) | The soak with the look: 456 passes, rxwd_stalls +4, rxpeek_found +548 taking 7,310 packets, istat_others +37,967,104, and the look's records - ring 4 found with 6 and 11 packets, its status bit and bit 62 set and the handler not run, and lone packets with a count of 1 and bit 62 clear | [lost-interrupt-2026-10-21.md](../measurements/lost-interrupt-2026-10-21.md) |
| [04-the-final-build.png](measurements/lost-interrupt-2026-10-21/04-the-final-build.png) | The soak on the final build: 456 passes, rxwd_stalls +7, rxpeek_found +544 taking 14,189 packets, the watchdog's longest gap 213 ticks, two stall records, and the look's own log - ring 1 found four times in a row, 19, 25, 29 and 37 packets, its status bit and bit 62 set, the handler's entry count 3,468,184 each time | [lost-interrupt-2026-10-21.md](../measurements/lost-interrupt-2026-10-21.md) |
| [05-resend-on.png](measurements/lost-interrupt-2026-10-21/05-resend-on.png) | The soak with RESEND written at every leave: 456 passes, rxwd_stalls +6, rxpeek_found +526, and the look's log - ring 7 found four times in a row with 8 to 11 packets, its status bit and bit 62 set, the handler's entry count 6,985,379 each time | [lost-interrupt-2026-10-21.md](../measurements/lost-interrupt-2026-10-21.md) |

### measurements/luid-2026-10-23/ (2)

The page: [luid-2026-10-23.md](../measurements/luid-2026-10-23.md).

| image | what it shows | used on |
|---|---|---|
| [01-3300-a-user-logged-out.png](measurements/luid-2026-10-23/01-3300-a-user-logged-out.png) | XGS 3300: user 5 logged in, set on a connection carrying 4.5 to 6.1 Gbit/s, then logged out - the connection given back, 144 frames to the host, then made again by the driver at once | [luid-2026-10-23.md](../measurements/luid-2026-10-23.md) |
| [02-136-a-user-logged-out.png](measurements/luid-2026-10-23/02-136-a-user-logged-out.png) | XGS 136: the same at a gigabit - at the logout 50,452 frames went to the host before the driver made the connection again | [luid-2026-10-23.md](../measurements/luid-2026-10-23.md) |

### measurements/mac-counters-3300-2026-10-20/ (1)

The page: [mac-counters-3300-2026-10-20.md](../measurements/mac-counters-3300-2026-10-20.md).

| image | what it shows | used on |
|---|---|---|
| [01-the-macs-and-the-host.png](measurements/mac-counters-3300-2026-10-20/01-the-macs-and-the-host.png) | The XGS 3300's F1, F2 and module ports counted by the coprocessor's MACs beside the host's own counts: F2's 1,956 frames and 125,726 bytes in and 1,546 out on both sides | [mac-counters-3300-2026-10-20.md](../measurements/mac-counters-3300-2026-10-20.md) |

### measurements/many-connections-time-wait-2026-10-27/ (5)

The page: [many-connections-time-wait-2026-10-27.md](../measurements/many-connections-time-wait-2026-10-27.md).

| image | what it shows | used on |
|---|---|---|
| [01-the-pc-sent-every-syn-and-all-arrived.png](measurements/many-connections-time-wait-2026-10-27/01-the-pc-sent-every-syn-and-all-arrived.png) | The PC's capture against the second XGS 3300's oxp0, nine runs of 2026-10-27: three with lost clients (2, 6 and 5); for each of the thirteen lost connections the PC sent five SYNs at about 0, 1, 3, 7 and 15 s, all five arrived on the second 3300's oxp0, and no SYN-ACK came back | [many-connections-time-wait-2026-10-27.md](../measurements/many-connections-time-wait-2026-10-27.md) |
| [02-pf-on-both-hosts-five-in-nothing-back.png](measurements/many-connections-time-wait-2026-10-27/02-pf-on-both-hosts-five-in-nothing-back.png) | pf's state of each lost connection 25 s into the run, on both XGS 3300s: CLOSED:SYN_SENT, 5:0 packets on each; the first 3300's TCP in those runs: syncache entries the same as connections accepted, fifteen drop counters all +0 | [many-connections-time-wait-2026-10-27.md](../measurements/many-connections-time-wait-2026-10-27.md) |
| [03-freebsd-tcp-twcheck-and-the-pc-syn-options.png](measurements/many-connections-time-wait-2026-10-27/03-freebsd-tcp-twcheck-and-the-pc-syn-options.png) | FreeBSD 15.1's tcp_twcheck from the kernel source OPNsense 26.7 builds from: a SYN for a connection in TIME_WAIT starts a new one only with a newer timestamp or a sequence number above rcv_nxt, otherwise goto drop; the first XGS 3300's net.inet.tcp.msl 30000; the PC's Windows TCP timestamps "allowed"; the 981 SYNs the PC sent in one run carried options 2, 3 and 4 - no timestamp | [many-connections-time-wait-2026-10-27.md](../measurements/many-connections-time-wait-2026-10-27.md) |
| [04-every-lost-connection-met-a-time-wait.png](measurements/many-connections-time-wait-2026-10-27/04-every-lost-connection-met-a-time-wait.png) | The first XGS 3300's TIME_WAIT just before the clients started, eleven runs: three connections met one, in the runs of 03:29 and 03:40 UTC, and they were the three lost; the lost connections' four-tuples were in TIME_WAIT on the first 3300 before their run | [many-connections-time-wait-2026-10-27.md](../measurements/many-connections-time-wait-2026-10-27.md) |
| [05-runs-after-the-time-wait-drained.png](measurements/many-connections-time-wait-2026-10-27/05-runs-after-the-time-wait-drained.png) | Fifteen runs, each after the first XGS 3300's TIME_WAIT to the servers' ports had drained (0 to 25 s): 1024 of 1024 streams in each, no client lost | [many-connections-time-wait-2026-10-27.md](../measurements/many-connections-time-wait-2026-10-27.md) |

### measurements/many-tunnels-136-2026-10-20/ (1)

The page: [many-tunnels-136-2026-10-20.md](../measurements/many-tunnels-136-2026-10-20.md).

| image | what it shows | used on |
|---|---|---|
| [01-six-tunnels.png](measurements/many-tunnels-136-2026-10-20/01-six-tunnels.png) | Six IPsec tunnels at once through the XGS 136: 6 of 6 established, 12 associations in its engine, 915 Mbit/s together | [many-tunnels-136-2026-10-20.md](../measurements/many-tunnels-136-2026-10-20.md) |

### measurements/many-tunnels-3300-2026-10-20/ (1)

The page: [many-tunnels-3300-2026-10-20.md](../measurements/many-tunnels-3300-2026-10-20.md).

| image | what it shows | used on |
|---|---|---|
| [01-sixteen-tunnels.png](measurements/many-tunnels-3300-2026-10-20/01-sixteen-tunnels.png) | Sixteen IPsec tunnels at once between the two XGS 3300s: 16 of 16 established, 34 and 32 associations in the engines, 9,019 Mbit/s together | [many-tunnels-3300-2026-10-20.md](../measurements/many-tunnels-3300-2026-10-20.md) |

### measurements/mgmt-link-2026-10-23/ (6)

The page: [mgmt-link-2026-10-23.md](../measurements/mgmt-link-2026-10-23.md).

| image | what it shows | used on |
|---|---|---|
| [01-3300-state.png](measurements/mgmt-link-2026-10-23/01-3300-state.png) | XGS 3300, octep.ko 12f1ea6d: oxmgmt0 up with 169.254.254.1/30, the driver's link up (mgmt_up 1, host and target status 2), 21 interfaces in the configuration with lan on bridge0 and the link as opt17 (NPUMGMT); the two rules, the coprocessor answering ping and ssh | [mgmt-link-2026-10-23.md](../measurements/mgmt-link-2026-10-23.md) |
| [02-3300-the-boot.png](measurements/mgmt-link-2026-10-23/02-3300-the-boot.png) | XGS 3300, the third boot of three: oxmgmt0 made at second 6 and down; the link started at second 125, the boot hook at 124; the switch's jumbo frames written and confirmed at 134 and 136; the coprocessor answering at 169.254.254.2 at second 149 | [mgmt-link-2026-10-23.md](../measurements/mgmt-link-2026-10-23.md) |
| [03-3300-one-command-link-and-console.png](measurements/mgmt-link-2026-10-23/03-3300-one-command-link-and-console.png) | XGS 3300, one command on the coprocessor: 0.73 to 0.77 seconds over the link, 11.01 over the console, five times each | [mgmt-link-2026-10-23.md](../measurements/mgmt-link-2026-10-23.md) |
| [04-3300-the-transceivers-read.png](measurements/mgmt-link-2026-10-23/04-3300-the-transceivers-read.png) | XGS 3300, the Transceivers page's read: 1.80 seconds over the link, 36.19 over the console; seven modules read, in F1, F2, F3 and A1 to A4 | [mgmt-link-2026-10-23.md](../measurements/mgmt-link-2026-10-23.md) |
| [05-136-state.png](measurements/mgmt-link-2026-10-23/05-136-state.png) | XGS 136, npuep.ko a38947c1, with the scripts that now serve both families: mvmgmt0 with 169.254.254.1/30, 18 interfaces with lan on bridge0 and the link as opt15 (NPUMGMT), the two rules, ping and ssh answered | [mgmt-link-2026-10-23.md](../measurements/mgmt-link-2026-10-23.md) |
| [06-136-one-command-link-and-console.png](measurements/mgmt-link-2026-10-23/06-136-one-command-link-and-console.png) | XGS 136, one command on the coprocessor: 0.49 to 0.53 seconds over the link, 11.01 to 11.02 over the console | [mgmt-link-2026-10-23.md](../measurements/mgmt-link-2026-10-23.md) |

### measurements/mirror-2026-10-25/ (5)

The page: [mirror-2026-10-25.md](../measurements/mirror-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-3300-new-sfos22.png](measurements/mirror-2026-10-25/01-3300-new-sfos22.png) | The second XGS 3300 over vendor version 22, after a boot with the setting on: tcpdump counted 22,928 replies but only 1,560 of the stream's data frames; 1,254,606 copies a direction at 8.96 Gbit/s, 8.94 Gbit/s without | [mirror-2026-10-25.md](../measurements/mirror-2026-10-25.md) |
| [02-3300-old-sfos23.png](measurements/mirror-2026-10-25/02-3300-old-sfos23.png) | The first XGS 3300 over vendor version 23, octep.ko 7b22d527: tcpdump on F1 and F2 counted 138,349 data frames arriving and 138,939 replies, the coprocessor sent 277,790 copies a direction; 1.03 Gbit/s at full rate with the copies, 1.02 without | [mirror-2026-10-25.md](../measurements/mirror-2026-10-25.md) |
| [03-136-sfos21.png](measurements/mirror-2026-10-25/03-136-sfos21.png) | The XGS 136, npuep.ko 68c476f1: tcpdump on Port13 and Port8 counted 137,365 data frames and 137,369 replies; the host itself received 9,968 and 9,753 of them, the coprocessor sent 255,193 copies a direction; at full rate 914 Mbit/s with the copies, 809 without | [mirror-2026-10-25.md](../measurements/mirror-2026-10-25.md) |
| [04-setting-saved-and-booted.png](measurements/mirror-2026-10-25/04-setting-saved-and-booted.png) | The second XGS 3300 and the XGS 136 after a boot with the setting on: the word 23 written by the boot on both, and 23,328 copies a direction over a 3-second stream through the XGS 136; then the setting saved off, on and off on the three boards, the word 7, 23, 7 | [mirror-2026-10-25.md](../measurements/mirror-2026-10-25.md) |
| [05-setting-saved-136.png](measurements/mirror-2026-10-25/05-setting-saved-136.png) | The XGS 136's saves, after two of its connections did not answer in the run above: off, on, off - the word 7, 23, 7 - and the save's report | [mirror-2026-10-25.md](../measurements/mirror-2026-10-25.md) |

### measurements/mtu-2026-10-17/ (65)

The page: [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md).

| image | what it shows | used on |
|---|---|---|
| [drops-old3300-to-pc.png](measurements/mtu-2026-10-17/drops-old3300-to-pc.png) | sender retransmits against the PC NIC's discards, one stream at each MTU, and four streams at 9000 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1000-136-clear-in.png](measurements/mtu-2026-10-17/mtu1000-136-clear-in.png) | MTU 1000, clear text, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1000-136-clear-out.png](measurements/mtu-2026-10-17/mtu1000-136-clear-out.png) | MTU 1000, clear text, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1000-136-ipsec-in.png](measurements/mtu-2026-10-17/mtu1000-136-ipsec-in.png) | MTU 1000, IPsec, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1000-136-ipsec-out.png](measurements/mtu-2026-10-17/mtu1000-136-ipsec-out.png) | MTU 1000, IPsec, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1000-old3300-clear-in.png](measurements/mtu-2026-10-17/mtu1000-old3300-clear-in.png) | MTU 1000, clear text, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1000-old3300-clear-out.png](measurements/mtu-2026-10-17/mtu1000-old3300-clear-out.png) | MTU 1000, clear text, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1000-old3300-ipsec-in.png](measurements/mtu-2026-10-17/mtu1000-old3300-ipsec-in.png) | MTU 1000, IPsec, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1000-old3300-ipsec-out.png](measurements/mtu-2026-10-17/mtu1000-old3300-ipsec-out.png) | MTU 1000, IPsec, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1280-136-clear-in.png](measurements/mtu-2026-10-17/mtu1280-136-clear-in.png) | MTU 1280, clear text, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1280-136-clear-out.png](measurements/mtu-2026-10-17/mtu1280-136-clear-out.png) | MTU 1280, clear text, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1280-136-ipsec-in.png](measurements/mtu-2026-10-17/mtu1280-136-ipsec-in.png) | MTU 1280, IPsec, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1280-136-ipsec-out.png](measurements/mtu-2026-10-17/mtu1280-136-ipsec-out.png) | MTU 1280, IPsec, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1280-old3300-clear-in.png](measurements/mtu-2026-10-17/mtu1280-old3300-clear-in.png) | MTU 1280, clear text, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1280-old3300-clear-out.png](measurements/mtu-2026-10-17/mtu1280-old3300-clear-out.png) | MTU 1280, clear text, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1280-old3300-ipsec-in.png](measurements/mtu-2026-10-17/mtu1280-old3300-ipsec-in.png) | MTU 1280, IPsec, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1280-old3300-ipsec-out.png](measurements/mtu-2026-10-17/mtu1280-old3300-ipsec-out.png) | MTU 1280, IPsec, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1500-136-clear-in.png](measurements/mtu-2026-10-17/mtu1500-136-clear-in.png) | MTU 1500, clear text, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1500-136-clear-out.png](measurements/mtu-2026-10-17/mtu1500-136-clear-out.png) | MTU 1500, clear text, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1500-136-ipsec-in.png](measurements/mtu-2026-10-17/mtu1500-136-ipsec-in.png) | MTU 1500, IPsec, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1500-136-ipsec-out.png](measurements/mtu-2026-10-17/mtu1500-136-ipsec-out.png) | MTU 1500, IPsec, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1500-old3300-clear-in.png](measurements/mtu-2026-10-17/mtu1500-old3300-clear-in.png) | MTU 1500, clear text, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1500-old3300-clear-out.png](measurements/mtu-2026-10-17/mtu1500-old3300-clear-out.png) | MTU 1500, clear text, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1500-old3300-ipsec-in.png](measurements/mtu-2026-10-17/mtu1500-old3300-ipsec-in.png) | MTU 1500, IPsec, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu1500-old3300-ipsec-out.png](measurements/mtu-2026-10-17/mtu1500-old3300-ipsec-out.png) | MTU 1500, IPsec, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu2000-136-clear-in.png](measurements/mtu-2026-10-17/mtu2000-136-clear-in.png) | MTU 2000, clear text, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu2000-136-clear-out.png](measurements/mtu-2026-10-17/mtu2000-136-clear-out.png) | MTU 2000, clear text, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu2000-136-ipsec-in.png](measurements/mtu-2026-10-17/mtu2000-136-ipsec-in.png) | MTU 2000, IPsec, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu2000-136-ipsec-out.png](measurements/mtu-2026-10-17/mtu2000-136-ipsec-out.png) | MTU 2000, IPsec, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu2000-old3300-clear-in.png](measurements/mtu-2026-10-17/mtu2000-old3300-clear-in.png) | MTU 2000, clear text, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu2000-old3300-clear-out.png](measurements/mtu-2026-10-17/mtu2000-old3300-clear-out.png) | MTU 2000, clear text, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu2000-old3300-ipsec-in.png](measurements/mtu-2026-10-17/mtu2000-old3300-ipsec-in.png) | MTU 2000, IPsec, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu2000-old3300-ipsec-out.png](measurements/mtu-2026-10-17/mtu2000-old3300-ipsec-out.png) | MTU 2000, IPsec, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu4000-136-clear-in.png](measurements/mtu-2026-10-17/mtu4000-136-clear-in.png) | MTU 4000, clear text, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu4000-136-clear-out.png](measurements/mtu-2026-10-17/mtu4000-136-clear-out.png) | MTU 4000, clear text, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu4000-136-ipsec-in.png](measurements/mtu-2026-10-17/mtu4000-136-ipsec-in.png) | MTU 4000, IPsec, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu4000-136-ipsec-out.png](measurements/mtu-2026-10-17/mtu4000-136-ipsec-out.png) | MTU 4000, IPsec, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu4000-old3300-clear-in.png](measurements/mtu-2026-10-17/mtu4000-old3300-clear-in.png) | MTU 4000, clear text, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu4000-old3300-clear-out.png](measurements/mtu-2026-10-17/mtu4000-old3300-clear-out.png) | MTU 4000, clear text, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu4000-old3300-ipsec-in.png](measurements/mtu-2026-10-17/mtu4000-old3300-ipsec-in.png) | MTU 4000, IPsec, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu4000-old3300-ipsec-out.png](measurements/mtu-2026-10-17/mtu4000-old3300-ipsec-out.png) | MTU 4000, IPsec, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu576-136-clear-in.png](measurements/mtu-2026-10-17/mtu576-136-clear-in.png) | MTU 576, clear text, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu576-136-clear-out.png](measurements/mtu-2026-10-17/mtu576-136-clear-out.png) | MTU 576, clear text, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu576-136-ipsec-in.png](measurements/mtu-2026-10-17/mtu576-136-ipsec-in.png) | MTU 576, IPsec, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu576-136-ipsec-out.png](measurements/mtu-2026-10-17/mtu576-136-ipsec-out.png) | MTU 576, IPsec, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu576-old3300-clear-in.png](measurements/mtu-2026-10-17/mtu576-old3300-clear-in.png) | MTU 576, clear text, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu576-old3300-clear-out.png](measurements/mtu-2026-10-17/mtu576-old3300-clear-out.png) | MTU 576, clear text, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu576-old3300-ipsec-in.png](measurements/mtu-2026-10-17/mtu576-old3300-ipsec-in.png) | MTU 576, IPsec, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu576-old3300-ipsec-out.png](measurements/mtu-2026-10-17/mtu576-old3300-ipsec-out.png) | MTU 576, IPsec, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu6000-136-clear-in.png](measurements/mtu-2026-10-17/mtu6000-136-clear-in.png) | MTU 6000, clear text, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu6000-136-clear-out.png](measurements/mtu-2026-10-17/mtu6000-136-clear-out.png) | MTU 6000, clear text, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu6000-136-ipsec-in.png](measurements/mtu-2026-10-17/mtu6000-136-ipsec-in.png) | MTU 6000, IPsec, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu6000-136-ipsec-out.png](measurements/mtu-2026-10-17/mtu6000-136-ipsec-out.png) | MTU 6000, IPsec, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu6000-old3300-clear-in.png](measurements/mtu-2026-10-17/mtu6000-old3300-clear-in.png) | MTU 6000, clear text, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu6000-old3300-clear-out.png](measurements/mtu-2026-10-17/mtu6000-old3300-clear-out.png) | MTU 6000, clear text, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu6000-old3300-ipsec-in.png](measurements/mtu-2026-10-17/mtu6000-old3300-ipsec-in.png) | MTU 6000, IPsec, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu6000-old3300-ipsec-out.png](measurements/mtu-2026-10-17/mtu6000-old3300-ipsec-out.png) | MTU 6000, IPsec, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu9000-136-clear-in.png](measurements/mtu-2026-10-17/mtu9000-136-clear-in.png) | MTU 9000, clear text, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu9000-136-clear-out.png](measurements/mtu-2026-10-17/mtu9000-136-clear-out.png) | MTU 9000, clear text, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu9000-136-ipsec-in.png](measurements/mtu-2026-10-17/mtu9000-136-ipsec-in.png) | MTU 9000, IPsec, PC to the XGS 136 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu9000-136-ipsec-out.png](measurements/mtu-2026-10-17/mtu9000-136-ipsec-out.png) | MTU 9000, IPsec, the XGS 136 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu9000-old3300-clear-in.png](measurements/mtu-2026-10-17/mtu9000-old3300-clear-in.png) | MTU 9000, clear text, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu9000-old3300-clear-out.png](measurements/mtu-2026-10-17/mtu9000-old3300-clear-out.png) | MTU 9000, clear text, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu9000-old3300-ipsec-in.png](measurements/mtu-2026-10-17/mtu9000-old3300-ipsec-in.png) | MTU 9000, IPsec, PC to the first XGS 3300 | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |
| [mtu9000-old3300-ipsec-out.png](measurements/mtu-2026-10-17/mtu9000-old3300-ipsec-out.png) | MTU 9000, IPsec, the first XGS 3300 to the PC | [mtu-2026-10-17.md](../measurements/mtu-2026-10-17.md) |

### measurements/narrow-path-2026-10-21/ (4)

The page: [narrow-path-2026-10-21.md](../measurements/narrow-path-2026-10-21.md).

| image | what it shows | used on |
|---|---|---|
| [01-the-driver-before.png](measurements/narrow-path-2026-10-21/01-the-driver-before.png) | The XGS 136 on its driver before, npuep.ko 8e18c257: the tunnel moved to 10.99.67.x on Port7, nothing arrives, tx_needfrag 7,142,536 and 294,324 more a second later, the sender at 91.7 % of a CPU and still there after kill -9 | [narrow-path-2026-10-21.md](../measurements/narrow-path-2026-10-21.md) |
| [02-the-fixed-driver.png](measurements/narrow-path-2026-10-21/02-the-fixed-driver.png) | The XGS 136 on the fixed driver, npuep.ko 29b665c7: the same move, 915 Mbit/s through the narrow path, tx_own_df 258,592, tx_needfrag 0, the sender ended | [narrow-path-2026-10-21.md](../measurements/narrow-path-2026-10-21.md) |
| [03-the-3300-before.png](measurements/narrow-path-2026-10-21/03-the-3300-before.png) | The second XGS 3300 on its driver before, octep.ko f7eede10: the tunnel moved to 10.99.67.x, nothing arrives, out_needfrag 15,873,474 and 819,077 more a second later, the sender at 93.8 % of a CPU and still there after kill -9 | [narrow-path-2026-10-21.md](../measurements/narrow-path-2026-10-21.md) |
| [04-the-3300-fixed.png](measurements/narrow-path-2026-10-21/04-the-3300-fixed.png) | The second XGS 3300 on the fixed driver, octep.ko 4c8b4162: the same move, 914 Mbit/s, out_own_df 129,436, out_needfrag 0, the sender ended | [narrow-path-2026-10-21.md](../measurements/narrow-path-2026-10-21.md) |

### measurements/own-traffic-2026-10-25/ (6)

The page: [own-traffic-2026-10-25.md](../measurements/own-traffic-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-sfos21-before.png](measurements/own-traffic-2026-10-25/01-sfos21-before.png) | The XGS 136 on npuep.ko c79fa33b: 2,000 pings of 1,428 bytes sent through its tunnel, the kernel's outbound count 0 bytes 0 packets before and after | [own-traffic-2026-10-25.md](../measurements/own-traffic-2026-10-25.md) |
| [02-sfos22-before.png](measurements/own-traffic-2026-10-25/02-sfos22-before.png) | The second XGS 3300 on octep.ko 80925b28: 7.17 GBytes sent from its own host through the tunnel, the kernel's outbound count 9,425,673,494 bytes before and after | [own-traffic-2026-10-25.md](../measurements/own-traffic-2026-10-25.md) |
| [03-sfos23-before.png](measurements/own-traffic-2026-10-25/03-sfos23-before.png) | The first XGS 3300 on octep.ko 80925b28: 2.61 GBytes sent from its own host through the tunnel, the kernel's outbound count 0 bytes 0 packets before and after | [own-traffic-2026-10-25.md](../measurements/own-traffic-2026-10-25.md) |
| [04-sfos21-after.png](measurements/own-traffic-2026-10-25/04-sfos21-after.png) | The XGS 136 on npuep.ko 873a6662: 2,000 pings of 1,428 bytes, the kernel's outbound count 2,856,000 bytes 2,000 packets after | [own-traffic-2026-10-25.md](../measurements/own-traffic-2026-10-25.md) |
| [05-sfos22-after.png](measurements/own-traffic-2026-10-25/05-sfos22-after.png) | The second XGS 3300 on octep.ko 536e3602: 8.69 GBytes sent from its own host, the kernel's outbound count 9,391,103,681 bytes 1,057,224 packets after | [own-traffic-2026-10-25.md](../measurements/own-traffic-2026-10-25.md) |
| [06-sfos23-after.png](measurements/own-traffic-2026-10-25/06-sfos23-after.png) | The first XGS 3300 on octep.ko 536e3602: its inbound count 9,391,103,681 bytes 1,057,224 packets - the second board's outbound to the byte; 3.35 GBytes sent from its own host, its outbound count from 54,975,996 to 3,681,369,280 bytes | [own-traffic-2026-10-25.md](../measurements/own-traffic-2026-10-25.md) |

### measurements/pcie-order-2026-10-25/ (3)

The page: [pcie-order-2026-10-25.md](../measurements/pcie-order-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-sfos21-before-the-change.png](measurements/pcie-order-2026-10-25/01-sfos21-before-the-change.png) | The XGS 136 on npuep.ko 590205fb: no line in the kernel's log, no pcie_order sysctl, and pciconf's PCI-Express line ending in RO NS - the two attributes on, as the firmware left them | [pcie-order-2026-10-25.md](../measurements/pcie-order-2026-10-25.md) |
| [02-sfos21-cleared-at-attach-and-measured.png](measurements/pcie-order-2026-10-25/02-sfos21-cleared-at-attach-and-measured.png) | The XGS 136 on npuep.ko c79fa33b: the driver's line "PCIe relaxed ordering and no-snoop cleared (device control 0x2040, was 0x2850)", RO NS gone from pciconf's line; as the driver leaves them it received 1000 MB at 959 Mbit/s and sent at 991; with the two set back on, 990 and 991; malformed 0 and unknown port 0 throughout | [pcie-order-2026-10-25.md](../measurements/pcie-order-2026-10-25.md) |
| [03-sfos21-ten-receives-alternated.png](measurements/pcie-order-2026-10-25/03-sfos21-ten-receives-alternated.png) | Ten 1 GB receives by the XGS 136 each way, alternated: cleared 990 once and 991 nine times; on 991 eight times, 950 and 943 once each; the receive path's malformed and unknown port counts 0 at the end | [pcie-order-2026-10-25.md](../measurements/pcie-order-2026-10-25.md) |

### measurements/poe-136-2026-10-20/ (1)

The page: [poe-136-2026-10-20.md](../measurements/poe-136-2026-10-20.md).

| image | what it shows | used on |
|---|---|---|
| [01-switched-from-xgs-ports.png](measurements/poe-136-2026-10-20/01-switched-from-xgs-ports.png) | PoE switched from XGS: Ports on the XGS 136: port 12 off from the page, switched on behind its back and off again by the start hook, a power cycle of port 11, the refusals, and what the vendor's tool refuses on this module | [poe-136-2026-10-20.md](../measurements/poe-136-2026-10-20.md) |

### measurements/policy-guard-136-2026-10-20/ (4)

The page: [policy-guard-136-2026-10-20.md](../measurements/policy-guard-136-2026-10-20.md).

| image | what it shows | used on |
|---|---|---|
| [01-before-the-guard.png](measurements/policy-guard-136-2026-10-20/01-before-the-guard.png) | The build before: the stream stays in the flow table after the policy, and stops | [policy-guard-136-2026-10-20.md](../measurements/policy-guard-136-2026-10-20.md) |
| [02-with-the-guard.png](measurements/policy-guard-136-2026-10-20/02-with-the-guard.png) | With the guard: taken out of the flow table, no frame in the clear, the stream goes on at 916 Mbit/s | [policy-guard-136-2026-10-20.md](../measurements/policy-guard-136-2026-10-20.md) |
| [03-the-counts.png](measurements/policy-guard-136-2026-10-20/03-the-counts.png) | The counts after the run: 5 taken out, 40 refused, every frame of it through the engine | [policy-guard-136-2026-10-20.md](../measurements/policy-guard-136-2026-10-20.md) |
| [04-on-the-xgs-3300.png](measurements/policy-guard-136-2026-10-20/04-on-the-xgs-3300.png) | The XGS 3300: the stream in the clear on the 136's Port7 until the tunnel covers it, then none, the 3300's flow_reval at 2 | [policy-guard-136-2026-10-20.md](../measurements/policy-guard-136-2026-10-20.md) |

### measurements/pollmode-2026-10-25/ (1)

The page: [pollmode-2026-10-25.md](../measurements/pollmode-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-three-boards-set-polling-mode.png](measurements/pollmode-2026-10-25/01-three-boards-set-polling-mode.png) | SET_POLLING_MODE asked of all three coprocessors, payload 0 and 1, beside a link query: the link query answered with status 0 and twelve bytes on each; SET_POLLING_MODE answered with status 1, an error, and eight bytes, every time, on vendor version 22, 23 and 21 | [pollmode-2026-10-25.md](../measurements/pollmode-2026-10-25.md) |

### measurements/port-errors-136-2026-10-20/ (2)

The page: [port-errors-136-2026-10-20.md](../measurements/port-errors-136-2026-10-20.md).

| image | what it shows | used on |
|---|---|---|
| [01-too-long-counted.png](measurements/port-errors-136-2026-10-20/01-too-long-counted.png) | An error only the switch sees: port 8's frame limit at 1,522 bytes, ten frames of 3,042 bytes lost and counted as too long, the limit put back | [port-errors-136-2026-10-20.md](../measurements/port-errors-136-2026-10-20.md) |
| [02-carrier-against-the-reads.png](measurements/port-errors-136-2026-10-20/02-carrier-against-the-reads.png) | The XGS 136's carrier changes against the cron that reads the switch's counters: every change outside a reload and a boot in the first seconds of the cron's minutes, and four ports still "no carrier" at 18:58 with their traffic crossing them | [port-errors-136-2026-10-20.md](../measurements/port-errors-136-2026-10-20.md) |

### measurements/port-qos-2026-10-24/ (4)

The page: [port-qos-2026-10-24.md](../measurements/port-qos-2026-10-24.md).

| image | what it shows | used on |
|---|---|---|
| [01-3300-port-speed-message.png](measurements/port-qos-2026-10-24/01-3300-port-speed-message.png) | The port-speed message for Port8 at 100: taken as a control message, and the stream through Port8 at 1,005 Mbit/s before and after it | [port-qos-2026-10-24.md](../measurements/port-qos-2026-10-24.md) |
| [02-3300-priority-bit.png](measurements/port-qos-2026-10-24/02-3300-priority-bit.png) | Two connections sharing Port8: the priority bit on the one to 5001 at second 14, read back from the fast path's entry, and the two at 304-310 and 690-697 Mbit/s before and after | [port-qos-2026-10-24.md](../measurements/port-qos-2026-10-24.md) |
| [03-3300-qos-commands.png](measurements/port-qos-2026-10-24/03-3300-qos-commands.png) | The seven QoS commands, each answered rc 0x8006, not dispatched | [port-qos-2026-10-24.md](../measurements/port-qos-2026-10-24.md) |
| [04-3300-coprocessor-rate-limit-log.png](measurements/port-qos-2026-10-24/04-3300-coprocessor-rate-limit-log.png) | The coprocessor's own log: the fast path started with -r 0, each queue's limit refused at boot with "bigger than link speed= 0", and no rate set since | [port-qos-2026-10-24.md](../measurements/port-qos-2026-10-24.md) |

### measurements/ports-own-addresses-2026-10-24/ (4)

The page: [ports-own-addresses-2026-10-24.md](../measurements/ports-own-addresses-2026-10-24.md).

| image | what it shows | used on |
|---|---|---|
| [01-136-before-every-port-on-the-placeholder.png](measurements/ports-own-addresses-2026-10-24/01-136-before-every-port-on-the-placeholder.png) | Before: all fourteen ports on the placeholder 02:01:02:03:04:06 onwards, the agent's own addresses different, a stream in hardware leaving with the placeholder | [ports-own-addresses-2026-10-24.md](../measurements/ports-own-addresses-2026-10-24.md) |
| [02-136-after-every-port-on-its-own-address.png](measurements/ports-own-addresses-2026-10-24/02-136-after-every-port-on-its-own-address.png) | After: all fourteen ports on their own addresses, the same as the agent's; link-local addresses made from them; a new DHCP lease on the WAN; a stream in hardware leaving with Port8's own address | [ports-own-addresses-2026-10-24.md](../measurements/ports-own-addresses-2026-10-24.md) |
| [03-136-before-an-address-set-by-hand.png](measurements/ports-own-addresses-2026-10-24/03-136-before-an-address-set-by-hand.png) | Before: Port8 given an address by hand - no ping across it, no stream; back on its hardware address, the ping again | [ports-own-addresses-2026-10-24.md](../measurements/ports-own-addresses-2026-10-24.md) |
| [04-136-after-an-address-set-by-hand.png](measurements/ports-own-addresses-2026-10-24/04-136-after-an-address-set-by-hand.png) | After: Port8 given an address by hand - the switch told within a sweep, the ping crosses, a stream in hardware leaves with the address set by hand; back on its own address, told again | [ports-own-addresses-2026-10-24.md](../measurements/ports-own-addresses-2026-10-24.md) |

### measurements/powerd-2026-10-17/ (8)

| image | what it shows | used on |
|---|---|---|
| [01-off-pc-to-3300.png](measurements/powerd-2026-10-17/01-off-pc-to-3300.png) |  | [appliance-defaults.md](../octep/appliance-defaults.md) |
| [02-off-3300-to-pc.png](measurements/powerd-2026-10-17/02-off-3300-to-pc.png) |  | [appliance-defaults.md](../octep/appliance-defaults.md) |
| [05-off-pc-to-3300-mtu1500.png](measurements/powerd-2026-10-17/05-off-pc-to-3300-mtu1500.png) |  | [appliance-defaults.md](../octep/appliance-defaults.md) |
| [06-off-3300-to-pc-mtu1500.png](measurements/powerd-2026-10-17/06-off-3300-to-pc-mtu1500.png) |  | [appliance-defaults.md](../octep/appliance-defaults.md) |
| [07-on-pc-to-3300-mtu1500.png](measurements/powerd-2026-10-17/07-on-pc-to-3300-mtu1500.png) |  | [appliance-defaults.md](../octep/appliance-defaults.md) |
| [08-on-3300-to-pc-mtu1500.png](measurements/powerd-2026-10-17/08-on-3300-to-pc-mtu1500.png) |  | [appliance-defaults.md](../octep/appliance-defaults.md) |
| [09-on-pc-to-3300-mtu9000.png](measurements/powerd-2026-10-17/09-on-pc-to-3300-mtu9000.png) |  | [appliance-defaults.md](../octep/appliance-defaults.md) |
| [10-on-3300-to-pc-mtu9000.png](measurements/powerd-2026-10-17/10-on-3300-to-pc-mtu9000.png) |  | [appliance-defaults.md](../octep/appliance-defaults.md) |

### measurements/qsched-2026-10-25/ (4)

The page: [qsched-2026-10-25.md](../measurements/qsched-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-sfos22-the-queue-the-frames-wait-in.png](measurements/qsched-2026-10-25/01-sfos22-the-queue-the-frames-wait-in.png) | The second XGS 3300, vendor version 22: switch port 8's eight egress queues - empty idle; with the two connections in hardware crossing it, 457 to 511 frames in queue 0 and none in the other seven; with the host's EF and unmarked streams, the same; with port 0's ingress map sending priority 0 to queue 3, still queue 0 | [qsched-2026-10-25.md](../measurements/qsched-2026-10-25.md) |
| [02-sfos22-ef-and-unmarked-weighted-and-strict.png](measurements/qsched-2026-10-25/02-sfos22-ef-and-unmarked-weighted-and-strict.png) | The second XGS 3300: the host's EF and unmarked streams sharing port 8 - weighted, 438 to 472 against 463 to 502 Mbit/s; strict for all eight, 456 to 548 against 388 to 476; strict with port 0 taking a frame's priority from its IP header first, 407 to 441 against 499 to 535 | [qsched-2026-10-25.md](../measurements/qsched-2026-10-25.md) |
| [03-sfos22-the-priority-bit-strict.png](measurements/qsched-2026-10-25/03-sfos22-the-priority-bit-strict.png) | The second XGS 3300: strict for all eight, the fast path's priority bit set on the connection to 5001 at second 8 - read back from the entry, and the two at 304 to 310 and 693 to 699 Mbit/s after it as before, three seconds before it apart | [qsched-2026-10-25.md](../measurements/qsched-2026-10-25.md) |
| [04-three-boards-the-switch-as-left.png](measurements/qsched-2026-10-25/04-three-boards-the-switch-as-left.png) | Each board's switch as its coprocessor's firmware leaves it: on the XGS 3300s, vendor version 22 and 23, and the XGS 136, vendor version 21 - port 0 in DSA mode with its priority from the tag then the IP header, port 8 the same in normal mode, default priority 0, and every port's scheduling weighted | [qsched-2026-10-25.md](../measurements/qsched-2026-10-25.md) |

### measurements/receive-poll-2026-10-17/ (5)

| image | what it shows | used on |
|---|---|---|
| [10ms-64.png](measurements/receive-poll-2026-10-17/10ms-64.png) | 10 ms and 64 frames: 18.2 Mbit/s | [a-hundred-polls-a-second-on-the-armada.md](../findings/a-hundred-polls-a-second-on-the-armada.md) |
| [1ms-1024-reverse.png](measurements/receive-poll-2026-10-17/1ms-1024-reverse.png) | 1 ms and 1024 frames, the other way: 934 Mbit/s | [a-hundred-polls-a-second-on-the-armada.md](../findings/a-hundred-polls-a-second-on-the-armada.md) |
| [1ms-1024.png](measurements/receive-poll-2026-10-17/1ms-1024.png) | 1 ms and 1024 frames: 773 Mbit/s | [a-hundred-polls-a-second-on-the-armada.md](../findings/a-hundred-polls-a-second-on-the-armada.md) |
| [1ms-256.png](measurements/receive-poll-2026-10-17/1ms-256.png) | 1 ms and 256 frames: 704 Mbit/s | [a-hundred-polls-a-second-on-the-armada.md](../findings/a-hundred-polls-a-second-on-the-armada.md) |
| [1ms-64.png](measurements/receive-poll-2026-10-17/1ms-64.png) | 1 ms and 64 frames: 185 Mbit/s | [a-hundred-polls-a-second-on-the-armada.md](../findings/a-hundred-polls-a-second-on-the-armada.md) |

### measurements/reload-136-2026-10-20/ (2)

The page: [reload-136-2026-10-20.md](../measurements/reload-136-2026-10-20.md).

| image | what it shows | used on |
|---|---|---|
| [01-the-interfaces-come-back.png](measurements/reload-136-2026-10-20/01-the-interfaces-come-back.png) | A reload of npuep.ko on the XGS 136: the script's own log, then the bridge with its 10 members and address, every interface's address and the management link back | [reload-136-2026-10-20.md](../measurements/reload-136-2026-10-20.md) |
| [02-the-transform-put-back.png](measurements/reload-136-2026-10-20/02-the-transform-put-back.png) | A reload with the tunnel's association on the engine: the panic before the fix, then three reloads with the fix that lived, and the associations freed after them | [reload-136-2026-10-20.md](../measurements/reload-136-2026-10-20.md) |

### measurements/revive-hold-136-2026-10-24/ (2)

The page: [revive-hold-136-2026-10-24.md](../measurements/revive-hold-136-2026-10-24.md).

| image | what it shows | used on |
|---|---|---|
| [01-136-far-side-sends-hold-in-turn.png](measurements/revive-hold-136-2026-10-24/01-136-far-side-sends-hold-in-turn.png) | Twelve runs, flow.revive_hold 0 and 1 in turn: with 1, four of six runs nearly all through the host and taken out at a streak; with 0, none | [revive-hold-136-2026-10-24.md](../measurements/revive-hold-136-2026-10-24.md) |
| [02-136-pc-sends-hold-off.png](measurements/revive-hold-136-2026-10-24/02-136-pc-sends-hold-off.png) | The PC's own way with flow.revive_hold 0: in hardware, as before | [revive-hold-136-2026-10-24.md](../measurements/revive-hold-136-2026-10-24.md) |

### measurements/rpc-bounds-2026-10-19/ (1)

The page: [rpc-bounds-2026-10-19.md](../measurements/rpc-bounds-2026-10-19.md).

| image | what it shows | used on |
|---|---|---|
| [01-xgs3300-the-posted-bound.png](measurements/rpc-bounds-2026-10-19/01-xgs3300-the-posted-bound.png) | The posted commands with flow_len 0, 600 and 5000: posted, sent on the low ring, refused | [rpc-bounds-2026-10-19.md](../measurements/rpc-bounds-2026-10-19.md) |

### measurements/rx-msix-136-2026-10-20/ (3)

The page: [rx-msix-136-2026-10-20.md](../measurements/rx-msix-136-2026-10-20.md).

| image | what it shows | used on |
|---|---|---|
| [01-doorbells-and-the-poll.png](measurements/rx-msix-136-2026-10-20/01-doorbells-and-the-poll.png) | The XGS 136's receive on the coprocessor's doorbells against the poll alone: with the doorbells the rate and the ping hold at 10 and 100 ms; without them they fall at 10 ms | [rx-msix-136-2026-10-20.md](../measurements/rx-msix-136-2026-10-20.md) |
| [02-an-hour-on-the-doorbells.png](measurements/rx-msix-136-2026-10-20/02-an-hour-on-the-doorbells.png) | An hour of TLS through the XGS 136 with its receive on the coprocessor's doorbells: 393 passes of 1 GiB, none failed or differed, 932 Mbit/s on average | [rx-msix-136-2026-10-20.md](../measurements/rx-msix-136-2026-10-20.md) |
| [03-the-defaults.png](measurements/rx-msix-136-2026-10-20/03-the-defaults.png) | Fifteen minutes of TLS through the XGS 136 on the driver's new defaults: the doorbells on and the poll at 1 ms, no pass failed or differed | [rx-msix-136-2026-10-20.md](../measurements/rx-msix-136-2026-10-20.md) |

### measurements/sa-counts-2026-10-25/ (3)

The page: [sa-counts-2026-10-25.md](../measurements/sa-counts-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-sfos22-before-from-sa-get-stats.png](measurements/sa-counts-2026-10-25/01-sfos22-before-from-sa-get-stats.png) | The second XGS 3300 on main's octep.ko 6cde3fde: a stream from the PC through the tunnel, 758 MBytes; the kernel's outbound count 794,211,781 bytes 88,806 packets; by flows since install 798,999,634 bytes 89,394 packets, pushed 10 times, SA_GET_STATS asked 22 times | [sa-counts-2026-10-25.md](../measurements/sa-counts-2026-10-25.md) |
| [02-sfos22-from-the-statistics-messages.png](measurements/sa-counts-2026-10-25/02-sfos22-from-the-statistics-messages.png) | The second XGS 3300 on octep.ko 80925b28: a stream of 9.17 GBytes at 9.82 Gbit/s; by flows since install 9,890,905,575 bytes 1,106,887 packets, the kernel's outbound count 9,892,943,604 bytes 1,136,460 packets, pushed 9 times, SA_GET_STATS asked 0 times | [sa-counts-2026-10-25.md](../measurements/sa-counts-2026-10-25.md) |
| [03-sfos23-the-tunnel-it-ends-and-its-messages.png](measurements/sa-counts-2026-10-25/03-sfos23-the-tunnel-it-ends-and-its-messages.png) | The first XGS 3300 on octep.ko 80925b28, vendor version 23: a stream from the PC, 7.99 GBytes, decrypted and counted by its host (in 9,423,935,398 bytes); a stream back, 1.36 GBytes, and the kernel's outbound count still 0 bytes 0 packets; then a flow test through it: dp.hw_counted 163 before, 325 after | [sa-counts-2026-10-25.md](../measurements/sa-counts-2026-10-25.md) |

### measurements/sa23-2026-10-25/ (2)

The page: [sa23-2026-10-25.md](../measurements/sa23-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-before.png](measurements/sa23-2026-10-25/01-before.png) | Before: the first XGS 3300 over vendor version 23, octep.ko fdc4e281 - no association mirrored; the coprocessor refused the inbound and the outbound association, rc 0x0001; 40 pings through the tunnel answered, the kernel's ESP counts rising, the engine's not | [sa23-2026-10-25.md](../measurements/sa23-2026-10-25.md) |
| [02-after.png](measurements/sa23-2026-10-25/02-after.png) | After: octep.ko 4c844f00 - the inbound association at coprocessor index 1 and the outbound at index 2, the outbound started a million numbers past the kernel's counter; 40 pings through the tunnel, decrypted and encrypted by the engine, 40 and 40, and none by the kernel | [sa23-2026-10-25.md](../measurements/sa23-2026-10-25.md) |

### measurements/sfos-versions-2026-10-25/ (8)

The page: [sfos-versions-2026-10-25.md](../measurements/sfos-versions-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-136-sfos21.png](measurements/sfos-versions-2026-10-25/01-136-sfos21.png) | On the XGS 136, npuep.ko d949a608: the platform block at command 36, 228 bytes, version 9a31cacb7d1 - vendor version 21; all fourteen front ports confirmed; a ping across Port8 to the second XGS 3300 answered | [sfos-versions-2026-10-25.md](../measurements/sfos-versions-2026-10-25.md) |
| [02-136-counters.png](measurements/sfos-versions-2026-10-25/02-136-counters.png) | On the XGS 136: tools/npuep-fpcnt.sh picks the vendor version 21 list by itself and reads 176 counters; command 44 sent by hand answers as before | [sfos-versions-2026-10-25.md](../measurements/sfos-versions-2026-10-25.md) |
| [03-136-retry.png](measurements/sfos-versions-2026-10-25/03-136-retry.png) | On the XGS 136 with hw.npuep.rpc_dbell_drop=30: the empty configuration not acknowledged, the channel opened on attempt 3, 15 seconds after attach; vendor version 21 found, all fourteen ports confirmed | [sfos-versions-2026-10-25.md](../measurements/sfos-versions-2026-10-25.md) |
| [04-136-unknown.png](measurements/sfos-versions-2026-10-25/04-136-unknown.png) | On the XGS 136 with hw.npuep.rpc_platform_test=1: no platform block at commands 37 to 40, cmd_shift -1, sfos 0; the table written but not read back; a ping across Port8 to the second XGS 3300 still answered | [sfos-versions-2026-10-25.md](../measurements/sfos-versions-2026-10-25.md) |
| [05-3300-sfos22.png](measurements/sfos-versions-2026-10-25/05-3300-sfos22.png) | On the second XGS 3300, octep.ko 8564b25d: the platform block at command 36, 232 bytes, UNKNOWN_VERSION - vendor version 22; num_mflows 4,001,450 read from offset 224; tools/octep-fpcnt.sh read 182 counters by the vendor version 22 list | [sfos-versions-2026-10-25.md](../measurements/sfos-versions-2026-10-25.md) |
| [06-3300-old-sfos23.png](measurements/sfos-versions-2026-10-25/06-3300-old-sfos23.png) | On the first XGS 3300, octep.ko 62e2f6b3: the platform block at command 37, 232 bytes, UNKNOWN_VERSION - vendor version 23, every command from 36 on sent one higher; num_mflows 4,001,450; the high ring configured; tools/octep-fpcnt.sh read 184 counters by the vendor version 23 list | [sfos-versions-2026-10-25.md](../measurements/sfos-versions-2026-10-25.md) |
| [07-3300-old-sfos23-flow.png](measurements/sfos-versions-2026-10-25/07-3300-old-sfos23-flow.png) | On the first XGS 3300, vendor version 23, octep.ko 8564b25d: one stream, 1.03 Gbit/s; two flows made by themselves, both directions active, orig in oxp0 and reply in oxp1; 3,556,621 frames cut through by the coprocessor; both flows taken back when the connections closed | [sfos-versions-2026-10-25.md](../measurements/sfos-versions-2026-10-25.md) |
| [08-3300-old-sfos23-flow-four.png](measurements/sfos-versions-2026-10-25/08-3300-old-sfos23-flow-four.png) | The same with four streams: 2.20 Gbit/s; five flows ten seconds in, the four streams and iperf3's own connection; 7,567,414 frames cut through; every flow taken back | [sfos-versions-2026-10-25.md](../measurements/sfos-versions-2026-10-25.md) |

### measurements/shaper-3300-2026-10-20/ (2)

The page: [shaper-3300-2026-10-20.md](../measurements/shaper-3300-2026-10-20.md).

| image | what it shows | used on |
|---|---|---|
| [01-before.png](measurements/shaper-3300-2026-10-20/01-before.png) | Before: a 100 Mbit/s shaper, the stream in hardware at 8,816 Mbit/s | [shaper-3300-2026-10-20.md](../measurements/shaper-3300-2026-10-20.md) |
| [02-after.png](measurements/shaper-3300-2026-10-20/02-after.png) | After: the same shaper, the stream with the host at 99.5 Mbit/s | [shaper-3300-2026-10-20.md](../measurements/shaper-3300-2026-10-20.md) |

### measurements/soak-ipsec-136-2026-10-20/ (2)

The page: [soak-ipsec-136-2026-10-20.md](../measurements/soak-ipsec-136-2026-10-20.md).

| image | what it shows | used on |
|---|---|---|
| [01-an-hour-through-the-tunnel.png](measurements/soak-ipsec-136-2026-10-20/01-an-hour-through-the-tunnel.png) | An hour of TLS through the XGS 136's IPsec engine: 378 passes of 1 GiB, none failed and none changed, 895 Mbit/s on average | [soak-ipsec-136-2026-10-20.md](../measurements/soak-ipsec-136-2026-10-20.md) |
| [02-two-hours-through-the-tunnel.png](measurements/soak-ipsec-136-2026-10-20/02-two-hours-through-the-tunnel.png) | Two hours of TLS through the XGS 136's IPsec engine: 756 passes of 1 GiB, none failed and none changed, 899 Mbit/s on average | [soak-ipsec-136-2026-10-20.md](../measurements/soak-ipsec-136-2026-10-20.md) |

### measurements/status-light-2026-10-27/ (2)

The page: [status-light-2026-10-27.md](../measurements/status-light-2026-10-27.md).

| image | what it shows | used on |
|---|---|---|
| [01-136-lit-green-3300-refused.png](measurements/status-light-2026-10-27/01-136-lit-green-3300-refused.png) | The XGS 136, board 201, vendor version 21: the green blinking (0x29) and the red dark before; lit green; read back lit two seconds later. The first XGS 3300, board 202: no controller at 0x40 on that bus, nothing written | [status-light-2026-10-27.md](../measurements/status-light-2026-10-27.md) |
| [02-136-after-a-restart-the-hook-lit-it.png](measurements/status-light-2026-10-27/02-136-after-a-restart-the-hook-lit-it.png) | The XGS 136 restarted at 17:51:30 after its light was set blinking: the start hook xgs-statusled run 79 s into the boot, the light read lit green (0x01), the tool and the hook installed | [status-light-2026-10-27.md](../measurements/status-light-2026-10-27.md) |

### measurements/switch-frame-limit-2026-10-24/ (3)

The page: [switch-frame-limit-2026-10-24.md](../measurements/switch-frame-limit-2026-10-24.md).

| image | what it shows | used on |
|---|---|---|
| [01-136-boot-frame-limit-by-the-agent.png](measurements/switch-frame-limit-2026-10-24/01-136-boot-frame-limit-by-the-agent.png) | A clean boot: the agent sets the frame limit from each port's MTU, carrier changes only while the links came up, 8,000 bytes across Port8 | [switch-frame-limit-2026-10-24.md](../measurements/switch-frame-limit-2026-10-24.md) |
| [02-136-the-old-hooks-reads-alone.png](measurements/switch-frame-limit-2026-10-24/02-136-the-old-hooks-reads-alone.png) | The old hook's reads alone: 300 reads, Port7 reported down for two seconds while its far end stayed up | [switch-frame-limit-2026-10-24.md](../measurements/switch-frame-limit-2026-10-24.md) |
| [03-136-a-read-and-a-port-taken-down.png](measurements/switch-frame-limit-2026-10-24/03-136-a-read-and-a-port-taken-down.png) | The boot before: a read of the switch at second 130, Port7 reported down at second 131 | [switch-frame-limit-2026-10-24.md](../measurements/switch-frame-limit-2026-10-24.md) |

### measurements/tcp-window-2026-10-22/ (21)

The page: [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md).

| image | what it shows | used on |
|---|---|---|
| [01-3300-segments-outside-the-window-check-on.png](measurements/tcp-window-2026-10-22/01-3300-segments-outside-the-window-check-on.png) | XGS 3300, octep.ko 7cd98513, the check on: five segments 64 MiB ahead and five 2 GiB ahead each counted FROM_WIRE_DROP_TCP_SEQ_CHK_INVALID +5 and TX_DROP +5; the RST handed to the host, TCP_FIN_SYN_RST +1, and the connection given back | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [02-3300-segments-outside-the-window-check-off.png](measurements/tcp-window-2026-10-22/02-3300-segments-outside-the-window-check-off.png) | XGS 3300, the check off (tcp_window 0, as before the fix): none of the eleven dropped | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [03-3300-take-back-upload.png](measurements/tcp-window-2026-10-22/03-3300-take-back-upload.png) | XGS 3300, the PC uploading: the stream's connection handed back at second 8; pf's two states for it lose their sloppy mark and take the fast path's numbers, and ten seconds later have moved on, state-mismatch +0; 1.76 Gbit/s | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [04-3300-take-back-download.png](measurements/tcp-window-2026-10-22/04-3300-take-back-download.png) | XGS 3300, the PC downloading: the connection had been given back and revived unchecked before second 8 (TCP_MAX_RETRANS), so pf's states stay sloppy; state-mismatch +0 | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [05-3300-take-back-both-ways.png](measurements/tcp-window-2026-10-22/05-3300-take-back-both-ways.png) | XGS 3300, both ways at once: handed back, two pf states took the fast path's numbers, state-mismatch +0; 7.28 and 6.24 Gbit/s | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [06-136-segments-outside-the-window-check-on.png](measurements/tcp-window-2026-10-22/06-136-segments-outside-the-window-check-on.png) | XGS 136, npuep.ko 0806653b, the check on: 5 and 5 dropped, FROM_WIRE_DROP_TCP_SEQ_CHK_INVALID +5 each time; the RST handed to the host; 938 Mbit/s | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [07-136-segments-outside-the-window-check-off.png](measurements/tcp-window-2026-10-22/07-136-segments-outside-the-window-check-off.png) | XGS 136, the check off: none dropped, 934 Mbit/s | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [08-136-take-back-upload.png](measurements/tcp-window-2026-10-22/08-136-take-back-upload.png) | XGS 136, the PC uploading: handed back, two pf states took the fast path's numbers, the sloppy mark gone, state-mismatch +0 over ten seconds with the host; 929 Mbit/s | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [09-136-take-back-download.png](measurements/tcp-window-2026-10-22/09-136-take-back-download.png) | XGS 136, the PC downloading: handed back, two states took the numbers, state-mismatch +0; 937 Mbit/s | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [10-136-take-back-both-ways.png](measurements/tcp-window-2026-10-22/10-136-take-back-both-ways.png) | XGS 136, both ways: handed back, two states took the numbers, state-mismatch +0 | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [11-3300-four-streams-down-check-on.png](measurements/tcp-window-2026-10-22/11-3300-four-streams-down-check-on.png) | XGS 3300, four streams down, the check on: 9.91 Gbit/s, no segment dropped by the check | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [12-3300-four-streams-down-check-off.png](measurements/tcp-window-2026-10-22/12-3300-four-streams-down-check-off.png) | XGS 3300, four streams down, the check off: 9.78 Gbit/s | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [13-136-four-streams-down-check-on.png](measurements/tcp-window-2026-10-22/13-136-four-streams-down-check-on.png) | XGS 136, four streams down, the check on: 941 Mbit/s, no segment dropped by the check | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [14-136-four-streams-down-check-off.png](measurements/tcp-window-2026-10-22/14-136-four-streams-down-check-off.png) | XGS 136, four streams down, the check off: 933 Mbit/s | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [15-3300-four-streams-down-revival-from-segments.png](measurements/tcp-window-2026-10-22/15-3300-four-streams-down-revival-from-segments.png) | XGS 3300, tcp_revive 2, four streams down: 9.79 Gbit/s, 100 windows made from segments, and 11 ordinary segments dropped by the check | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [16-3300-segments-outside-the-window-revival-from-segments.png](measurements/tcp-window-2026-10-22/16-3300-segments-outside-the-window-revival-from-segments.png) | XGS 3300, tcp_revive 2, segments outside the window: 5 and 5 dropped - in a run in which the connection was not given back before them | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [17-136-four-streams-down-revival-from-segments.png](measurements/tcp-window-2026-10-22/17-136-four-streams-down-revival-from-segments.png) | XGS 136, tcp_revive 2, four streams down: 949 Mbit/s, four connections made with a window from segments, none dropped | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [18-3300-take-back-upload-at-second-3.png](measurements/tcp-window-2026-10-22/18-3300-take-back-upload-at-second-3.png) | XGS 3300, uploading, the take-back at second 3: the connection had been revived unchecked already, left sloppy, state-mismatch +0 | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [19-3300-take-back-download-at-second-3.png](measurements/tcp-window-2026-10-22/19-3300-take-back-download-at-second-3.png) | XGS 3300, downloading, at second 3: given back already, left sloppy, state-mismatch +0 | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [20-3300-take-back-both-ways-at-second-3.png](measurements/tcp-window-2026-10-22/20-3300-take-back-both-ways-at-second-3.png) | XGS 3300, both ways, at second 3: handed back, two pf states took the numbers, state-mismatch +0 | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |
| [21-3300-a-connection-through-the-tunnel.png](measurements/tcp-window-2026-10-22/21-3300-a-connection-through-the-tunnel.png) | XGS 3300, a connection through the lab tunnel with ipsec.flows 1: made unchecked - tcp_unchecked +2, the stream's and iperf3's own - and encrypted on the coprocessor, 1,719,926 frames, 8.15 Gbit/s | [tcp-window-2026-10-22.md](../measurements/tcp-window-2026-10-22.md) |

### measurements/tunflows-136-2026-10-20/ (6)

The page: [tunflows-136-2026-10-20.md](../measurements/tunflows-136-2026-10-20.md).

| image | what it shows | used on |
|---|---|---|
| [01-through-the-host.png](measurements/tunflows-136-2026-10-20/01-through-the-host.png) | Through the host: some 333,000 frames handed to the engine every look, the CPUs 23 % busy | [tunflows-136-2026-10-20.md](../measurements/tunflows-136-2026-10-20.md) |
| [02-in-hardware.png](measurements/tunflows-136-2026-10-20/02-in-hardware.png) | In hardware: no frame through the host, the CPUs 11 % busy, the rekey taking the connections out and making them again | [tunflows-136-2026-10-20.md](../measurements/tunflows-136-2026-10-20.md) |
| [03-nothing-in-the-clear.png](measurements/tunflows-136-2026-10-20/03-nothing-in-the-clear.png) | The same, with what reached the 3300 in the clear counted: none | [tunflows-136-2026-10-20.md](../measurements/tunflows-136-2026-10-20.md) |
| [04-set-in-xgs-settings.png](measurements/tunflows-136-2026-10-20/04-set-in-xgs-settings.png) | The build that ships, the setting made in XGS: Settings and taken at boot: the same | [tunflows-136-2026-10-20.md](../measurements/tunflows-136-2026-10-20.md) |
| [05-lifetime-before.png](measurements/tunflows-136-2026-10-20/05-lifetime-before.png) | Before: the stream in hardware, the kernel's associations counting nothing | [tunflows-136-2026-10-20.md](../measurements/tunflows-136-2026-10-20.md) |
| [06-lifetime-after.png](measurements/tunflows-136-2026-10-20/06-lifetime-after.png) | After: the kernel's associations counting what the engine encrypted, 57 hand-overs | [tunflows-136-2026-10-20.md](../measurements/tunflows-136-2026-10-20.md) |

### measurements/unload-780-2026-10-25/ (3)

The page: [unload-780-2026-10-25.md](../measurements/unload-780-2026-10-25.md).

| image | what it shows | used on |
|---|---|---|
| [01-sfos21-flow-takeouts-old-place.png](measurements/unload-780-2026-10-25/01-sfos21-flow-takeouts-old-place.png) | Three reloads of the XGS 136 with the comparison build npuep.ko 89d6b526: five connections in the flow table before each; every new load with all fourteen front ports confirmed and the second XGS 3300 answering a ping | [unload-780-2026-10-25.md](../measurements/unload-780-2026-10-25.md) |
| [02-sfos21-unload-path-fixed.png](measurements/unload-780-2026-10-25/02-sfos21-unload-path-fixed.png) | Four reloads with npuep.ko 68a87305, the change: five connections in the flow table before each, all fourteen front ports confirmed after each load, the second XGS 3300 answering | [unload-780-2026-10-25.md](../measurements/unload-780-2026-10-25.md) |
| [03-sfos21-each-unload-from-the-log.png](measurements/unload-780-2026-10-25/03-sfos21-each-unload-from-the-log.png) | The board's system log, each reload's mark and the unload's lines after it: with 68a87305, "5 connection(s) taken back from the fast path at unload, 0 reclaim(s) not sent or not answered" six times; with the comparison build 89d6b526, "5 reclaim(s) not sent or not answered" twice; "function 1's datapath disabled" whenever the inspector's function was there, and "datapath disabled" at every unload | [unload-780-2026-10-25.md](../measurements/unload-780-2026-10-25.md) |

### measurements/v6-136-2026-10-20/ (9)

| image | what it shows | used on |
|---|---|---|
| [01-outer4-mode.png](measurements/v6-136-2026-10-20/01-outer4-mode.png) | The IPv4 tunnel, for comparison: the associations on the engine | [ipv6-136-2026-10-20.md](../measurements/ipv6-136-2026-10-20.md) |
| [02-outer4-run.png](measurements/v6-136-2026-10-20/02-outer4-run.png) | The IPv4 tunnel: 396 into the 136, the first stream since its boot, and 955 out of it | [ipv6-136-2026-10-20.md](../measurements/ipv6-136-2026-10-20.md) |
| [03-outer6-mode.png](measurements/v6-136-2026-10-20/03-outer6-mode.png) | The tunnel moved to its IPv6 addresses: both associations inet6, accepted by the engine | [ipv6-136-2026-10-20.md](../measurements/ipv6-136-2026-10-20.md) |
| [04-outer6-run.png](measurements/v6-136-2026-10-20/04-outer6-run.png) | IPv6 outside, IPv4 inside: 961 and 972 Mbit/s | [ipv6-136-2026-10-20.md](../measurements/ipv6-136-2026-10-20.md) |
| [05-inner-on-mode.png](measurements/v6-136-2026-10-20/05-inner-on-mode.png) | IPv6 inside as well: the selectors widened, the runtime rule loaded | [ipv6-136-2026-10-20.md](../measurements/ipv6-136-2026-10-20.md) |
| [06-inner6-run.png](measurements/v6-136-2026-10-20/06-inner6-run.png) | IPv6 outside and inside: 873 and 872 with IPv6 inside, rx_v6 counting | [ipv6-136-2026-10-20.md](../measurements/ipv6-136-2026-10-20.md) |
| [07-inner6-routed.png](measurements/v6-136-2026-10-20/07-inner6-routed.png) | The 136's route by Port8: 979 both ways with IPv6 inside, through the offload path | [ipv6-136-2026-10-20.md](../measurements/ipv6-136-2026-10-20.md) |
| [08-cbc.png](measurements/v6-136-2026-10-20/08-cbc.png) | AES-CBC with HMAC-SHA-256 over IPv6: 961 to 977 Mbit/s | [ipv6-136-2026-10-20.md](../measurements/ipv6-136-2026-10-20.md) |
| [09-back.png](measurements/v6-136-2026-10-20/09-back.png) | The IPv4 tunnel put back: 985 and 984 into the 136 | [ipv6-136-2026-10-20.md](../measurements/ipv6-136-2026-10-20.md) |

### measurements/verdict-2026-10-23/ (13)

The page: [verdict-2026-10-23.md](../measurements/verdict-2026-10-23.md).

| image | what it shows | used on |
|---|---|---|
| [01-3300-tcp-cut-on.png](measurements/verdict-2026-10-23/01-3300-tcp-cut-on.png) | XGS 3300, TCP, Cut in hardware on: 2 connections dropped in hardware first, 32 dropped on the coprocessor, 10 frames to the host, the stream at 0 from second 12 | [verdict-2026-10-23.md](../measurements/verdict-2026-10-23.md) |
| [02-3300-udp-flood-cut-on.png](measurements/verdict-2026-10-23/02-3300-udp-flood-cut-on.png) | XGS 3300, UDP at 400 Mbit/s, on: 690,389 frames dropped on the coprocessor, 10 to the host, its CPUs 0.3 % busy, the connection still in hardware at second 22 | [verdict-2026-10-23.md](../measurements/verdict-2026-10-23.md) |
| [03-3300-udp-flood-cut-off.png](measurements/verdict-2026-10-23/03-3300-udp-flood-cut-off.png) | XGS 3300, UDP at 400 Mbit/s, off: the connection taken out, 610,533 frames to the host for pf to drop | [verdict-2026-10-23.md](../measurements/verdict-2026-10-23.md) |
| [04-136-tcp-cut-on.png](measurements/verdict-2026-10-23/04-136-tcp-cut-on.png) | XGS 136, TCP, on: 2 dropped in hardware first, 49 on the coprocessor, 40 frames to the host, nothing delivered after the cut | [verdict-2026-10-23.md](../measurements/verdict-2026-10-23.md) |
| [05-136-udp-flood-cut-on.png](measurements/verdict-2026-10-23/05-136-udp-flood-cut-on.png) | XGS 136, UDP at 300 Mbit/s, on: 355,968 frames dropped on the coprocessor, 36 to the host, its CPUs 0.4 % busy | [verdict-2026-10-23.md](../measurements/verdict-2026-10-23.md) |
| [06-136-udp-flood-cut-off.png](measurements/verdict-2026-10-23/06-136-udp-flood-cut-off.png) | XGS 136, UDP at 300 Mbit/s, off: 280,202 frames to the host, its CPUs 7.0 % busy | [verdict-2026-10-23.md](../measurements/verdict-2026-10-23.md) |
| [07-3300-verdict-under-fwd-unasked.png](measurements/verdict-2026-10-23/07-3300-verdict-under-fwd-unasked.png) | XGS 3300, flows made FWD with the IPS switch off: the verdict set to DROP and the stream going on at 5,228 Mbit/s - the fast path never asks | [verdict-2026-10-23.md](../measurements/verdict-2026-10-23.md) |
| [08-3300-verdict-drop-and-back.png](measurements/verdict-2026-10-23/08-3300-verdict-drop-and-back.png) | XGS 3300, flows made IPS with the switch on: DROP - 0 Mbit/s for the six seconds, pf's state kept, nothing to the host - then CUT_THRU and 5,036 Mbit/s again | [verdict-2026-10-23.md](../measurements/verdict-2026-10-23.md) |
| [09-136-verdict-drop-and-back.png](measurements/verdict-2026-10-23/09-136-verdict-drop-and-back.png) | XGS 136, the same: 0 Mbit/s for the six seconds, then 608 Mbit/s again | [verdict-2026-10-23.md](../measurements/verdict-2026-10-23.md) |
| [10-3300-thousand-fwd.png](measurements/verdict-2026-10-23/10-3300-thousand-fwd.png) | XGS 3300, a thousand connections, FWD | [verdict-2026-10-23.md](../measurements/verdict-2026-10-23.md) |
| [11-3300-thousand-ips.png](measurements/verdict-2026-10-23/11-3300-thousand-ips.png) | XGS 3300, a thousand connections, IPS | [verdict-2026-10-23.md](../measurements/verdict-2026-10-23.md) |
| [12-136-thousand-fwd.png](measurements/verdict-2026-10-23/12-136-thousand-fwd.png) | XGS 136, a thousand connections, FWD | [verdict-2026-10-23.md](../measurements/verdict-2026-10-23.md) |
| [13-136-thousand-ips.png](measurements/verdict-2026-10-23/13-136-thousand-ips.png) | XGS 136, a thousand connections, IPS | [verdict-2026-10-23.md](../measurements/verdict-2026-10-23.md) |

### measurements/vlan-2026-10-24/ (6)

The page: [vlan-2026-10-24.md](../measurements/vlan-2026-10-24.md).

| image | what it shows | used on |
|---|---|---|
| [01-3300-before.png](measurements/vlan-2026-10-24/01-3300-before.png) | Before: VLAN 50 has no logical interface - the connection is not made, the host routes every frame | [vlan-2026-10-24.md](../measurements/vlan-2026-10-24.md) |
| [02-136-before.png](measurements/vlan-2026-10-24/02-136-before.png) | Before: no logical interface for VLAN 60 - nothing in hardware, every reply crossing the host, its processors a third busy | [vlan-2026-10-24.md](../measurements/vlan-2026-10-24.md) |
| [03-3300-after.png](measurements/vlan-2026-10-24/03-3300-after.png) | After: both directions in hardware, the tag written - the reply's next hop leaves by VLAN 50 and every reply arrives tagged | [vlan-2026-10-24.md](../measurements/vlan-2026-10-24.md) |
| [04-3300-after-vlan-flows-off.png](measurements/vlan-2026-10-24/04-3300-after-vlan-flows-off.png) | After, dp.vlan_flows 0: the VLAN's entry switched off, the host routes the connection, the VLAN still carrying it | [vlan-2026-10-24.md](../measurements/vlan-2026-10-24.md) |
| [05-136-after.png](measurements/vlan-2026-10-24/05-136-after.png) | After: both of iperf3's connections in hardware, the data tagged VLAN 60 with the TTL lowered, no reply reaching the host, the processors 0.9 % busy | [vlan-2026-10-24.md](../measurements/vlan-2026-10-24.md) |
| [06-136-after-vlan-flows-off.png](measurements/vlan-2026-10-24/06-136-after-vlan-flows-off.png) | After, flow.vlan_flows 0: the VLAN's entry shut, the host routes the connection as before | [vlan-2026-10-24.md](../measurements/vlan-2026-10-24.md) |

### measurements/xgs136-after-boot-2026-10-19/ (8)

The page: [xgs136-after-boot-2026-10-19.md](../measurements/xgs136-after-boot-2026-10-19.md).

| image | what it shows | used on |
|---|---|---|
| [01-xgs136-f1-down-and-up.png](measurements/xgs136-after-boot-2026-10-19/01-xgs136-f1-down-and-up.png) | F1 down for 5 seconds and up again: no slow minutes | [xgs136-after-boot-2026-10-19.md](../measurements/xgs136-after-boot-2026-10-19.md) |
| [02-xgs136-port8-down-and-up.png](measurements/xgs136-after-boot-2026-10-19/02-xgs136-port8-down-and-up.png) | Port8 down for 5 seconds and up again: no slow minutes | [xgs136-after-boot-2026-10-19.md](../measurements/xgs136-after-boot-2026-10-19.md) |
| [03-xgs136-the-minutes-after-a-boot.png](measurements/xgs136-after-boot-2026-10-19/03-xgs136-the-minutes-after-a-boot.png) | The first watch: nothing recorded | [xgs136-after-boot-2026-10-19.md](../measurements/xgs136-after-boot-2026-10-19.md) |
| [04-xgs136-ten-minutes-three-ways.png](measurements/xgs136-after-boot-2026-10-19/04-xgs136-ten-minutes-three-ways.png) | Ten minutes three ways: nothing slow | [xgs136-after-boot-2026-10-19.md](../measurements/xgs136-after-boot-2026-10-19.md) |
| [05-xgs136-the-minutes-after-a-boot.png](measurements/xgs136-after-boot-2026-10-19/05-xgs136-the-minutes-after-a-boot.png) | A boot watched: nothing slow, the host quiet, the coprocessor's fourth core nearly idle | [xgs136-after-boot-2026-10-19.md](../measurements/xgs136-after-boot-2026-10-19.md) |
| [06-cold-right-after-boot.png](measurements/xgs136-after-boot-2026-10-19/06-cold-right-after-boot.png) | A minute after a boot: every path at the link's rate | [xgs136-after-boot-2026-10-19.md](../measurements/xgs136-after-boot-2026-10-19.md) |
| [07-cold-after-an-hour-the-pc-slow.png](measurements/xgs136-after-boot-2026-10-19/07-cold-after-an-hour-the-pc-slow.png) | After an idle hour: the 136 at the link's rate with the PC out of the path, the PC slow on both of its cards | [xgs136-after-boot-2026-10-19.md](../measurements/xgs136-after-boot-2026-10-19.md) |
| [08-cold-thirteen-minutes-later.png](measurements/xgs136-after-boot-2026-10-19/08-cold-thirteen-minutes-later.png) | Thirteen minutes later: every path fast again | [xgs136-after-boot-2026-10-19.md](../measurements/xgs136-after-boot-2026-10-19.md) |

### measurements/xgs136-counts-2026-10-19/ (8)

The page: [xgs136-counts-2026-10-19.md](../measurements/xgs136-counts-2026-10-19.md).

| image | what it shows | used on |
|---|---|---|
| [01-xgs136-untagged-frames.png](measurements/xgs136-counts-2026-10-19/01-xgs136-untagged-frames.png) | The unknown-tag count each second: none idle, 6 or 7 with a stream in hardware, none with the same stream through the host | [xgs136-counts-2026-10-19.md](../measurements/xgs136-counts-2026-10-19.md) |
| [02-xgs136-the-frames-read.png](measurements/xgs136-counts-2026-10-19/02-xgs136-the-frames-read.png) | The frames read once a second through a stream in hardware, decoded: connection counts for slot 2, then interface counts | [xgs136-counts-2026-10-19.md](../measurements/xgs136-counts-2026-10-19.md) |
| [03-xgs136-counted-against-the-wire.png](measurements/xgs136-counts-2026-10-19/03-xgs136-counted-against-the-wire.png) | The first reader: F1 counted 5,455,724 bytes more than the PC sent, four a frame | [xgs136-counts-2026-10-19.md](../measurements/xgs136-counts-2026-10-19.md) |
| [04-xgs136-counted-the-other-way.png](measurements/xgs136-counts-2026-10-19/04-xgs136-counted-the-other-way.png) | The first reader the other way: Port8 counted 5,564,860 bytes more than the XGS 3300 sent | [xgs136-counts-2026-10-19.md](../measurements/xgs136-counts-2026-10-19.md) |
| [05-xgs136-counted-against-the-wire-fcs-off.png](measurements/xgs136-counts-2026-10-19/05-xgs136-counted-against-the-wire-fcs-off.png) | With the FCS taken off: F1 to 42 bytes of what the PC sent | [xgs136-counts-2026-10-19.md](../measurements/xgs136-counts-2026-10-19.md) |
| [06-xgs136-counted-the-other-way-fcs-off.png](measurements/xgs136-counts-2026-10-19/06-xgs136-counted-the-other-way-fcs-off.png) | Before the fix: 73 remakes, and pf 117,531 packets short | [xgs136-counts-2026-10-19.md](../measurements/xgs136-counts-2026-10-19.md) |
| [07-xgs136-counted-the-other-way-given-before-remake.png](measurements/xgs136-counts-2026-10-19/07-xgs136-counted-the-other-way-given-before-remake.png) | XGS 3300 to PC through the 136 in hardware: pf's state equal to what the XGS 3300 sent, to the byte, through 18 remakes | [xgs136-counts-2026-10-19.md](../measurements/xgs136-counts-2026-10-19.md) |
| [08-xgs136-counted-against-the-wire-final.png](measurements/xgs136-counts-2026-10-19/08-xgs136-counted-against-the-wire-final.png) | PC to XGS 3300 through the 136 in hardware: pf's state 0 packets and 6 bytes from what the XGS 3300 received; every interface to the frame | [xgs136-counts-2026-10-19.md](../measurements/xgs136-counts-2026-10-19.md) |

### measurements/xgs136-giveback-2026-10-19/ (10)

The page: [xgs136-giveback-2026-10-19.md](../measurements/xgs136-giveback-2026-10-19.md).

| image | what it shows | used on |
|---|---|---|
| [01-xgs136-given-back-which-way.png](measurements/xgs136-giveback-2026-10-19/01-xgs136-given-back-which-way.png) | The first look: the remakes come with the XGS 3300 sending, not the other way | [xgs136-giveback-2026-10-19.md](../measurements/xgs136-giveback-2026-10-19.md) |
| [02-xgs136-where-the-frames-go.png](measurements/xgs136-giveback-2026-10-19/02-xgs136-where-the-frames-go.png) | The XGS 3300 sending three ways: frames lost between F1 and the PC through the host too, and in hardware hundreds of retransmissions with none lost | [xgs136-giveback-2026-10-19.md](../measurements/xgs136-giveback-2026-10-19.md) |
| [03-xgs136-out-of-order.png](measurements/xgs136-giveback-2026-10-19/03-xgs136-out-of-order.png) | Before: the XGS 3300 sending through the 136 in hardware, 789 Mbit/s, 2,110 retransmissions and 29 remakes, against 887 through the host | [xgs136-giveback-2026-10-19.md](../measurements/xgs136-giveback-2026-10-19.md) |
| [04-xgs136-out-of-order-with-the-wait.png](measurements/xgs136-giveback-2026-10-19/04-xgs136-out-of-order-with-the-wait.png) | The first build: the XGS 3300 sending in hardware fell to 365 Mbit/s | [xgs136-giveback-2026-10-19.md](../measurements/xgs136-giveback-2026-10-19.md) |
| [05-xgs136-out-of-order-held-out-of-the-table.png](measurements/xgs136-giveback-2026-10-19/05-xgs136-out-of-order-held-out-of-the-table.png) | The second build, the run two minutes after its boot: the PC's way slow in the same way | [xgs136-giveback-2026-10-19.md](../measurements/xgs136-giveback-2026-10-19.md) |
| [06-xgs136-out-of-order-held-again.png](measurements/xgs136-giveback-2026-10-19/06-xgs136-out-of-order-held-again.png) | The second build: 812 Mbit/s the XGS 3300's way, and still 1,367 retransmissions | [xgs136-giveback-2026-10-19.md](../measurements/xgs136-giveback-2026-10-19.md) |
| [07-xgs136-a-minute-the-other-way.png](measurements/xgs136-giveback-2026-10-19/07-xgs136-a-minute-the-other-way.png) | The second build over a minute: 851 Mbit/s and 6,073 retransmissions | [xgs136-giveback-2026-10-19.md](../measurements/xgs136-giveback-2026-10-19.md) |
| [08-xgs136-out-of-order-held-both-ways.png](measurements/xgs136-giveback-2026-10-19/08-xgs136-out-of-order-held-both-ways.png) | The first run on the published build, two minutes after its boot: the PC's way at a third of its speed, both paths | [xgs136-giveback-2026-10-19.md](../measurements/xgs136-giveback-2026-10-19.md) |
| [09-xgs136-a-minute-held-both-ways.png](measurements/xgs136-giveback-2026-10-19/09-xgs136-a-minute-held-both-ways.png) | After, over a minute: the holds thinning out, the retransmissions falling, 861 Mbit/s | [xgs136-giveback-2026-10-19.md](../measurements/xgs136-giveback-2026-10-19.md) |
| [10-xgs136-out-of-order-held-both-ways-settled.png](measurements/xgs136-giveback-2026-10-19/10-xgs136-out-of-order-held-both-ways-settled.png) | After: 877 Mbit/s in hardware the XGS 3300's way, no remake, and the PC's way unchanged | [xgs136-giveback-2026-10-19.md](../measurements/xgs136-giveback-2026-10-19.md) |

### measurements/xgs136-hardware-2026-10-18/ (4)

The page: [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md).

| image | what it shows | used on |
|---|---|---|
| [01-host-pc-to-3300.png](measurements/xgs136-hardware-2026-10-18/01-host-pc-to-3300.png) | The host, PC to the XGS 3300 | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [02-host-3300-to-pc.png](measurements/xgs136-hardware-2026-10-18/02-host-3300-to-pc.png) | The host, the XGS 3300 to the PC | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [03-hardware-pc-to-3300.png](measurements/xgs136-hardware-2026-10-18/03-hardware-pc-to-3300.png) | The coprocessor, PC to the XGS 3300 | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [04-hardware-3300-to-pc.png](measurements/xgs136-hardware-2026-10-18/04-hardware-3300-to-pc.png) | The coprocessor, the XGS 3300 to the PC | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |

### measurements/xgs136-hardware-2026-10-18/ipv6/ (5)

The page: [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md).

| image | what it shows | used on |
|---|---|---|
| [01-v6-host-pc-to-3300.png](measurements/xgs136-hardware-2026-10-18/ipv6/01-v6-host-pc-to-3300.png) | IPv6: the host, PC to the XGS 3300, the first run | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [01b-v6-host-pc-to-3300-again.png](measurements/xgs136-hardware-2026-10-18/ipv6/01b-v6-host-pc-to-3300-again.png) | IPv6: the host, PC to the XGS 3300, again | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [02-v6-host-3300-to-pc.png](measurements/xgs136-hardware-2026-10-18/ipv6/02-v6-host-3300-to-pc.png) | IPv6: the host, the XGS 3300 to the PC | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [03-v6-hardware-pc-to-3300.png](measurements/xgs136-hardware-2026-10-18/ipv6/03-v6-hardware-pc-to-3300.png) | IPv6: the coprocessor, PC to the XGS 3300 | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [04-v6-hardware-3300-to-pc.png](measurements/xgs136-hardware-2026-10-18/ipv6/04-v6-hardware-3300-to-pc.png) | IPv6: the coprocessor, the XGS 3300 to the PC | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |

### measurements/xgs136-hardware-2026-10-18/many/ (18)

The page: [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md).

| image | what it shows | used on |
|---|---|---|
| [many-1024-hardware-16384.png](measurements/xgs136-hardware-2026-10-18/many/many-1024-hardware-16384.png) | A thousand connections: the coprocessor, 16,384 candidate slots | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [many-1024-hardware-posted.png](measurements/xgs136-hardware-2026-10-18/many/many-1024-hardware-posted.png) | 1,024 connections, flow writes posted | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [many-1024-hardware-sloppy.png](measurements/xgs136-hardware-2026-10-18/many/many-1024-hardware-sloppy.png) | 1,024 connections, everything fixed | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [many-1024-hardware.png](measurements/xgs136-hardware-2026-10-18/many/many-1024-hardware.png) | A thousand connections: the coprocessor, 1,024 candidate slots | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [many-1024-host.png](measurements/xgs136-hardware-2026-10-18/many/many-1024-host.png) | A thousand connections: the host | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [many-1024-series-1-posted.png](measurements/xgs136-hardware-2026-10-18/many/many-1024-series-1-posted.png) | 1,024 connections, series 1, posted | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [many-1024-series-2-waited.png](measurements/xgs136-hardware-2026-10-18/many/many-1024-series-2-waited.png) | 1,024 connections, series 2, waited | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [many-1024-series-3-posted.png](measurements/xgs136-hardware-2026-10-18/many/many-1024-series-3-posted.png) | 1,024 connections, series 3, posted | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [many-1024-series-4-waited.png](measurements/xgs136-hardware-2026-10-18/many/many-1024-series-4-waited.png) | 1,024 connections, series 4, waited | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [teardown-invalidate-type-0-1-posted.png](measurements/xgs136-hardware-2026-10-18/many/teardown-invalidate-type-0-1-posted.png) | Teardown, invalidate type 0, posted | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [teardown-invalidate-type-0-2-waited.png](measurements/xgs136-hardware-2026-10-18/many/teardown-invalidate-type-0-2-waited.png) | Teardown, invalidate type 0, waited | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [teardown-keepalive-1-posted.png](measurements/xgs136-hardware-2026-10-18/many/teardown-keepalive-1-posted.png) | Teardown, the walk fixed, posted | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [teardown-keepalive-2-posted.png](measurements/xgs136-hardware-2026-10-18/many/teardown-keepalive-2-posted.png) | Teardown, the walk fixed, posted, again | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [teardown-keepalive-3-waited.png](measurements/xgs136-hardware-2026-10-18/many/teardown-keepalive-3-waited.png) | Teardown, the walk fixed, waited | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [teardown-reclaim-1-posted.png](measurements/xgs136-hardware-2026-10-18/many/teardown-reclaim-1-posted.png) | Teardown, reclaimed, posted | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [teardown-reclaim-2-waited.png](measurements/xgs136-hardware-2026-10-18/many/teardown-reclaim-2-waited.png) | Teardown, reclaimed, waited | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [teardown-reclaim-3-posted.png](measurements/xgs136-hardware-2026-10-18/many/teardown-reclaim-3-posted.png) | Teardown, reclaimed, posted, again | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [teardown-reclaim-4-waited.png](measurements/xgs136-hardware-2026-10-18/many/teardown-reclaim-4-waited.png) | Teardown, reclaimed, waited, again | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |

### measurements/xgs136-hardware-2026-10-18/nat/ (4)

The page: [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md).

| image | what it shows | used on |
|---|---|---|
| [01-nat-host-pc-to-3300wan.png](measurements/xgs136-hardware-2026-10-18/nat/01-nat-host-pc-to-3300wan.png) | Translated: the host, PC to the WAN address | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [02-nat-host-3300wan-to-pc.png](measurements/xgs136-hardware-2026-10-18/nat/02-nat-host-3300wan-to-pc.png) | Translated: the host, the WAN address to the PC | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [03-nat-hardware-pc-to-3300wan.png](measurements/xgs136-hardware-2026-10-18/nat/03-nat-hardware-pc-to-3300wan.png) | Translated: the coprocessor, PC to the WAN address | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [04-nat-hardware-3300wan-to-pc.png](measurements/xgs136-hardware-2026-10-18/nat/04-nat-hardware-3300wan-to-pc.png) | Translated: the coprocessor, the WAN address to the PC | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |

### measurements/xgs136-hardware-2026-10-18/revive/ (6)

The page: [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md).

| image | what it shows | used on |
|---|---|---|
| [01-host-pc-to-3300.png](measurements/xgs136-hardware-2026-10-18/revive/01-host-pc-to-3300.png) | Revived: the host, PC to the XGS 3300 | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [02-host-3300-to-pc.png](measurements/xgs136-hardware-2026-10-18/revive/02-host-3300-to-pc.png) | Revived: the host, the XGS 3300 to the PC | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [03-hardware-pc-to-3300.png](measurements/xgs136-hardware-2026-10-18/revive/03-hardware-pc-to-3300.png) | Revived: the coprocessor, PC to the XGS 3300 | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [04-hardware-3300-to-pc.png](measurements/xgs136-hardware-2026-10-18/revive/04-hardware-3300-to-pc.png) | Revived: the coprocessor, the XGS 3300 to the PC | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [05-hardware-3300-to-pc-by-second.png](measurements/xgs136-hardware-2026-10-18/revive/05-hardware-3300-to-pc-by-second.png) | Revived: the coprocessor, the XGS 3300 to the PC, second by second | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [06-hardware-3300-to-pc-by-second-attached.png](measurements/xgs136-hardware-2026-10-18/revive/06-hardware-3300-to-pc-by-second-attached.png) | Tried: one direction attached at a time, second by second | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |

### measurements/xgs136-hardware-2026-10-18/takeout/ (7)

The page: [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md).

| image | what it shows | used on |
|---|---|---|
| [01-killed-state-invalidate-type-0.png](measurements/xgs136-hardware-2026-10-18/takeout/01-killed-state-invalidate-type-0.png) | A killed state, the takeout refused | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [02-killed-state-conn-reclaim.png](measurements/xgs136-hardware-2026-10-18/takeout/02-killed-state-conn-reclaim.png) | A killed state, the connection reclaimed | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [03-return-to-host-reclaim-pf-window-stale.png](measurements/xgs136-hardware-2026-10-18/takeout/03-return-to-host-reclaim-pf-window-stale.png) | Return to host, pf judging the window | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [04-return-to-host-pf-sloppy.png](measurements/xgs136-hardware-2026-10-18/takeout/04-return-to-host-pf-sloppy.png) | Return to host, pf states sloppy | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [05-killed-state-pf-sloppy.png](measurements/xgs136-hardware-2026-10-18/takeout/05-killed-state-pf-sloppy.png) | A killed state, the connection reclaimed, pf states sloppy | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [06-return-to-host-pf-strict.png](measurements/xgs136-hardware-2026-10-18/takeout/06-return-to-host-pf-strict.png) | Return to host, flow.pf_sloppy 0 | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |
| [07-return-to-host-pf-sloppy.png](measurements/xgs136-hardware-2026-10-18/takeout/07-return-to-host-pf-sloppy.png) | Return to host, flow.pf_sloppy 1 | [xgs136-hardware-2026-10-18.md](../measurements/xgs136-hardware-2026-10-18.md) |

### measurements/xgs136-soak-2026-10-19/ (1)

The page: [xgs136-soak-2026-10-19.md](../measurements/xgs136-soak-2026-10-19.md).

| image | what it shows | used on |
|---|---|---|
| [01-xgs136-two-hours-of-tls.png](measurements/xgs136-soak-2026-10-19/01-xgs136-two-hours-of-tls.png) | Two hours of TLS through the XGS 136: 795 passes of 1 GiB, none failed and none changed, 945 Mbit/s on average, the CPUs 2.5 % busy | [xgs136-soak-2026-10-19.md](../measurements/xgs136-soak-2026-10-19.md) |

### measurements/xgs3300-many-2026-10-19/ (18)

The page: [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md).

| image | what it shows | used on |
|---|---|---|
| [01-xgs3300-a-thousand-connections.png](measurements/xgs3300-many-2026-10-19/01-xgs3300-a-thousand-connections.png) | The first run: 10,386 Mbit/s, more than the link carries - the reading's interval, not the traffic | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [02-xgs3300-a-thousand-connections-read-right.png](measurements/xgs3300-many-2026-10-19/02-xgs3300-a-thousand-connections-read-right.png) | The second run: the rate right at 9,816 Mbit/s, the connections counted three times over | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [03-xgs3300-a-thousand-connections-counted-right.png](measurements/xgs3300-many-2026-10-19/03-xgs3300-a-thousand-connections-counted-right.png) | A thousand connections through the XGS 3300: 1,032 in hardware, 9,837 Mbit/s, the CPUs 7.8 % busy | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [04-plain-128-connections.png](measurements/xgs3300-many-2026-10-19/04-plain-128-connections.png) | 128 connections, plain: none revived, the CPUs 0.5 % | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [05-plain-256-connections.png](measurements/xgs3300-many-2026-10-19/05-plain-256-connections.png) | 256 connections, plain: 2 revived | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [06-plain-512-connections.png](measurements/xgs3300-many-2026-10-19/06-plain-512-connections.png) | 512 connections, plain: 58 revived, the CPUs 0.6 % | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [07-plain-1024-connections.png](measurements/xgs3300-many-2026-10-19/07-plain-1024-connections.png) | 1,024 connections, plain: made 3.3 times and revived 13.7 times each, the CPUs 12 %, 9,800 Mbit/s | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [08-through-the-tunnel-1024-connections.png](measurements/xgs3300-many-2026-10-19/08-through-the-tunnel-1024-connections.png) | 1,024 connections through the tunnel: made once each, none revived, the CPUs 20.1 % | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [09-revive-hold-off-0950-utc.png](measurements/xgs3300-many-2026-10-19/09-revive-hold-off-0950-utc.png) | 1,024 connections, plain, dp.revive_hold off, 09:50 UTC: revived 16,067, the CPUs 7.4 % | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [10-revive-hold-on-0951-utc.png](measurements/xgs3300-many-2026-10-19/10-revive-hold-on-0951-utc.png) | 1,024 connections, plain, dp.revive_hold on, 09:51 UTC: revived 734, the CPUs 0.7 % | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [11-revive-hold-off-0952-utc.png](measurements/xgs3300-many-2026-10-19/11-revive-hold-off-0952-utc.png) | 1,024 connections, plain, dp.revive_hold off, 09:52 UTC: revived 1,332, the CPUs 1.1 % | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [12-revive-hold-on-0953-utc.png](measurements/xgs3300-many-2026-10-19/12-revive-hold-on-0953-utc.png) | 1,024 connections, plain, dp.revive_hold on, 09:53 UTC: revived 2,316, the CPUs 1.9 % | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [13-revive-hold-off-0954-utc.png](measurements/xgs3300-many-2026-10-19/13-revive-hold-off-0954-utc.png) | 1,024 connections, plain, dp.revive_hold off, 09:54 UTC: revived 288, the CPUs 1.2 % | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [14-revive-hold-on-0955-utc.png](measurements/xgs3300-many-2026-10-19/14-revive-hold-on-0955-utc.png) | 1,024 connections, plain, dp.revive_hold on, 09:55 UTC: revived 4,939, the CPUs 7.1 % | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [15-revive-hold-off-0956-utc.png](measurements/xgs3300-many-2026-10-19/15-revive-hold-off-0956-utc.png) | 1,024 connections, plain, dp.revive_hold off, 09:56 UTC: revived 528, the CPUs 1.3 % | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [16-revive-hold-on-0957-utc.png](measurements/xgs3300-many-2026-10-19/16-revive-hold-on-0957-utc.png) | 1,024 connections, plain, dp.revive_hold on, 09:57 UTC: revived 1,281, the CPUs 1.0 % | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [17-revive-hold-off-0959-utc.png](measurements/xgs3300-many-2026-10-19/17-revive-hold-off-0959-utc.png) | 1,024 connections, plain, dp.revive_hold off, 09:59 UTC: revived 10,807, the CPUs 8.7 % | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |
| [18-revive-hold-on-1000-utc.png](measurements/xgs3300-many-2026-10-19/18-revive-hold-on-1000-utc.png) | 1,024 connections, plain, dp.revive_hold on, 10:00 UTC: revived 8,346, the CPUs 16.0 % | [xgs3300-many-2026-10-19.md](../measurements/xgs3300-many-2026-10-19.md) |

## The README's drawings

| image | used on |
|---|---|
| [banner-dark.svg](readme/banner-dark.svg) | [README.md](../../README.md) |
| [banner-light.svg](readme/banner-light.svg) | [README.md](../../README.md) |
| [notice-dark.svg](readme/notice-dark.svg) | [README.md](../../README.md), [2026-10-25-update-opnsense-php2shell.md](../../website/news/2026-10-25-update-opnsense-php2shell.md) |
| [notice-light.svg](readme/notice-light.svg) | [README.md](../../README.md), [2026-10-25-update-opnsense-php2shell.md](../../website/news/2026-10-25-update-opnsense-php2shell.md) |
| [status-dark.svg](readme/status-dark.svg) | *no page yet* |
| [status-light.svg](readme/status-light.svg) | *no page yet* |

## Not on any page yet

Kept for a page that will use them - or for the wiki, which links here:

- [hardware/xg330/xg330-front.jpg](hardware/xg330/xg330-front.jpg)
- [readme/status-dark.svg](readme/status-dark.svg)
- [readme/status-light.svg](readme/status-light.svg)
- [screens/ar/07b-xgs-updates-plugins.png](screens/ar/07b-xgs-updates-plugins.png)
