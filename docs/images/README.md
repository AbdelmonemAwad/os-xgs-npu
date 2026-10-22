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
| [screens/](screens/) | The web interface, page by page, in English and in Arabic | 67 |
| [hardware/](hardware/) | Photographs and drawings of the appliances, one folder each | 41 |
| [measurements/](measurements/) | Each measurement's photographs, in a folder named after its page | 289 |
| [readme/](readme/) | The README's banner and status board, drawn by tools/readme-art.py | 4 |

426 images in all.

## The web interface

Full screen, dark theme, photographed on 2026-10-19 on the second XGS 3300. Each page is described
in [docs/guide/the-pages.md](../guide/the-pages.md), English first and Arabic under it.

<table>
<tr><th width="20%">page</th><th width="40%">English</th><th width="40%">Arabic</th></tr>
<tr><td valign="top"><sub>Lobby: Dashboard on an XGS 3300</sub></td><td valign="top"><a href="screens/en/00-lobby-dashboard.png"><img src="screens/en/00-lobby-dashboard.png" alt="Lobby: Dashboard on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/00-lobby-dashboard.png"><img src="screens/ar/00-lobby-dashboard.png" alt="Lobby: Dashboard on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Overview on an XGS 3300: the front panel drawn with each port's link, cards for the coprocessor, hardware offloading, the IPsec engine, the front ports, the switch chip and the last restart, and the board's limits</sub></td><td valign="top"><a href="screens/en/01-xgs-overview.png"><img src="screens/en/01-xgs-overview.png" alt="XGS: Overview on an XGS 3300: the front panel drawn with each port's link, cards for the coprocessor, hardware offloading, the IPsec engine, the front ports, the switch chip and the last restart, and the board's limits - English" width="100%"></a></td><td valign="top"><a href="screens/ar/01-xgs-overview.png"><img src="screens/ar/01-xgs-overview.png" alt="XGS: Overview on an XGS 3300: the front panel drawn with each port's link, cards for the coprocessor, hardware offloading, the IPsec engine, the front ports, the switch chip and the last restart, and the board's limits - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Settings on an XGS 3300</sub></td><td valign="top"><a href="screens/en/02-xgs-settings.png"><img src="screens/en/02-xgs-settings.png" alt="XGS: Settings on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/02-xgs-settings.png"><img src="screens/ar/02-xgs-settings.png" alt="XGS: Settings on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Settings on an XGS 3300, further down: Never offloaded, Cut open connections when listed, Cut connections by TLS fingerprint</sub></td><td valign="top"><a href="screens/en/02b-xgs-settings-cuts.png"><img src="screens/en/02b-xgs-settings-cuts.png" alt="XGS: Settings on an XGS 3300, further down: Never offloaded, Cut open connections when listed, Cut connections by TLS fingerprint - English" width="100%"></a></td><td valign="top"><a href="screens/ar/02b-xgs-settings-cuts.png"><img src="screens/ar/02b-xgs-settings-cuts.png" alt="XGS: Settings on an XGS 3300, further down: Never offloaded, Cut open connections when listed, Cut connections by TLS fingerprint - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Settings on an XGS 3300: the flow maker, and the two settings that count hardware traffic in pf and on the interfaces</sub></td><td valign="top"><a href="screens/en/02c-xgs-settings-counts.png"><img src="screens/en/02c-xgs-settings-counts.png" alt="XGS: Settings on an XGS 3300: the flow maker, and the two settings that count hardware traffic in pf and on the interfaces - English" width="100%"></a></td><td valign="top"><a href="screens/ar/02c-xgs-settings-counts.png"><img src="screens/ar/02c-xgs-settings-counts.png" alt="XGS: Settings on an XGS 3300: the flow maker, and the two settings that count hardware traffic in pf and on the interfaces - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Ports on an XGS 3300</sub></td><td valign="top"><a href="screens/en/03-xgs-ports.png"><img src="screens/en/03-xgs-ports.png" alt="XGS: Ports on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/03-xgs-ports.png"><img src="screens/ar/03-xgs-ports.png" alt="XGS: Ports on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Ports on the XGS 136, its Power over Ethernet section: port 11 on with no device, port 12 switched off, each with Switch off or on and Power cycle</sub></td><td valign="top"><a href="screens/en/03b-xgs-ports-poe.png"><img src="screens/en/03b-xgs-ports-poe.png" alt="XGS: Ports on the XGS 136, its Power over Ethernet section: port 11 on with no device, port 12 switched off, each with Switch off or on and Power cycle - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: Transceivers on an XGS 3300</sub></td><td valign="top"><a href="screens/en/04-xgs-transceivers.png"><img src="screens/en/04-xgs-transceivers.png" alt="XGS: Transceivers on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/04-xgs-transceivers.png"><img src="screens/ar/04-xgs-transceivers.png" alt="XGS: Transceivers on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: IPsec on an XGS 3300</sub></td><td valign="top"><a href="screens/en/05-xgs-ipsec.png"><img src="screens/en/05-xgs-ipsec.png" alt="XGS: IPsec on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/05-xgs-ipsec.png"><img src="screens/ar/05-xgs-ipsec.png" alt="XGS: IPsec on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: IPsec, Associations in the engine: one association each way through the tunnel to the XGS 136</sub></td><td valign="top"><a href="screens/en/05b-xgs-ipsec-associations.png"><img src="screens/en/05b-xgs-ipsec-associations.png" alt="XGS: IPsec, Associations in the engine: one association each way through the tunnel to the XGS 136 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/05b-xgs-ipsec-associations.png"><img src="screens/ar/05b-xgs-ipsec-associations.png" alt="XGS: IPsec, Associations in the engine: one association each way through the tunnel to the XGS 136 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: IPsec, Counters: frames decrypted and encrypted by the engine</sub></td><td valign="top"><a href="screens/en/05c-xgs-ipsec-counters.png"><img src="screens/en/05c-xgs-ipsec-counters.png" alt="XGS: IPsec, Counters: frames decrypted and encrypted by the engine - English" width="100%"></a></td><td valign="top"><a href="screens/ar/05c-xgs-ipsec-counters.png"><img src="screens/ar/05c-xgs-ipsec-counters.png" alt="XGS: IPsec, Counters: frames decrypted and encrypted by the engine - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Offloaded Flows on an XGS 3300</sub></td><td valign="top"><a href="screens/en/06-xgs-offloaded-flows.png"><img src="screens/en/06-xgs-offloaded-flows.png" alt="XGS: Offloaded Flows on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/06-xgs-offloaded-flows.png"><img src="screens/ar/06-xgs-offloaded-flows.png" alt="XGS: Offloaded Flows on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS, Updates on an appliance</sub></td><td valign="top"><a href="screens/en/07-xgs-updates.png"><img src="screens/en/07-xgs-updates.png" alt="XGS, Updates on an appliance - English" width="100%"></a></td><td valign="top"><a href="screens/ar/07-xgs-updates.png"><img src="screens/ar/07-xgs-updates.png" alt="XGS, Updates on an appliance - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Updates on the XGS 136, its Plugins tab: five plugins, each on its latest release</sub></td><td valign="top"><a href="screens/en/07b-xgs-updates-plugins.png"><img src="screens/en/07b-xgs-updates-plugins.png" alt="XGS: Updates on the XGS 136, its Plugins tab: five plugins, each on its latest release - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: Problem Reports on an XGS 3300</sub></td><td valign="top"><a href="screens/en/08-xgs-problem-reports.png"><img src="screens/en/08-xgs-problem-reports.png" alt="XGS: Problem Reports on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/08-xgs-problem-reports.png"><img src="screens/ar/08-xgs-problem-reports.png" alt="XGS: Problem Reports on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: Log File on an XGS 3300</sub></td><td valign="top"><a href="screens/en/09-xgs-log-file.png"><img src="screens/en/09-xgs-log-file.png" alt="XGS: Log File on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/09-xgs-log-file.png"><img src="screens/ar/09-xgs-log-file.png" alt="XGS: Log File on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>XGS: About on the XGS 136: the project, its status and the release installed</sub></td><td valign="top"><a href="screens/en/09b-xgs-about.png"><img src="screens/en/09b-xgs-about.png" alt="XGS: About on the XGS 136: the project, its status and the release installed - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: About, its Contact tab</sub></td><td valign="top"><a href="screens/en/09c-xgs-about-contact.png"><img src="screens/en/09c-xgs-about-contact.png" alt="XGS: About, its Contact tab - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>XGS: About, its Disclaimer tab</sub></td><td valign="top"><a href="screens/en/09d-xgs-about-disclaimer.png"><img src="screens/en/09d-xgs-about-disclaimer.png" alt="XGS: About, its Disclaimer tab - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>Reporting: Network Report on an XGS 3300</sub></td><td valign="top"><a href="screens/en/10-network-report.png"><img src="screens/en/10-network-report.png" alt="Reporting: Network Report on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/10-network-report.png"><img src="screens/ar/10-network-report.png" alt="Reporting: Network Report on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>System: Fan Control on an XGS 3300</sub></td><td valign="top"><a href="screens/en/11-fan-control.png"><img src="screens/en/11-fan-control.png" alt="System: Fan Control on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/11-fan-control.png"><img src="screens/ar/11-fan-control.png" alt="System: Fan Control on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>System: Front Panel on an XGS 3300</sub></td><td valign="top"><a href="screens/en/12-front-panel.png"><img src="screens/en/12-front-panel.png" alt="System: Front Panel on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/12-front-panel.png"><img src="screens/ar/12-front-panel.png" alt="System: Front Panel on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>Interfaces: Link Health on an XGS 3300</sub></td><td valign="top"><a href="screens/en/13-link-health.png"><img src="screens/en/13-link-health.png" alt="Interfaces: Link Health on an XGS 3300 - English" width="100%"></a></td><td valign="top"><a href="screens/ar/13-link-health.png"><img src="screens/ar/13-link-health.png" alt="Interfaces: Link Health on an XGS 3300 - Arabic" width="100%"></a></td></tr>
<tr><td valign="top"><sub>The image builder's page</sub></td><td valign="top"><a href="screens/en/20-builder-page.png"><img src="screens/en/20-builder-page.png" alt="The image builder's page - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
<tr><td valign="top"><sub>The image builder's form: the release, the image type and the console</sub></td><td valign="top"><a href="screens/en/21-builder-form.png"><img src="screens/en/21-builder-form.png" alt="The image builder's form: the release, the image type and the console - English" width="100%"></a></td><td valign="top"><sub>English only</sub></td></tr>
</table>

## Hardware

### hardware/bench/ - The bench the appliances are measured on (2)

<table>
<tr>
<td width="25%" valign="top"><a href="hardware/bench/desk-2026-10-22.jpg"><img src="hardware/bench/desk-2026-10-22.jpg" alt="The bench the evening it was measured: on the left screen the driver's code, octep_tcp_from_pf - a connection's window made from pf's numbers; on the right a run of the series, segments outside the window dropped on the XGS 136 and four streams through both appliances; below them the XGS 136, opened, and the XGS 3300" width="100%"></a><br><sub>The bench the evening it was measured: on the left screen the driver's code, octep_tcp_from_pf - a connection's window made from pf's numbers; on the right a run of the series, segments outside the window dropped on the XGS 136 and four streams through both appliances; below them the XGS 136, opened, and the XGS 3300</sub></td>
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

### measurements/cage-medium-2026-10-19/ (3)

The page: [cage-medium-2026-10-19.md](../measurements/cage-medium-2026-10-19.md).

| image | what it shows | used on |
|---|---|---|
| [01-cage-medium-before.png](measurements/cage-medium-2026-10-19/01-cage-medium-before.png) | Before: the 136's F1 says 1000baseSX with a 1000BASE-T module in it; the XGS 3300's F1 and F2 say 10Gbase-SR with copper modules | [cage-medium-2026-10-19.md](../measurements/cage-medium-2026-10-19.md) |
| [02-cage-medium-after.png](measurements/cage-medium-2026-10-19/02-cage-medium-after.png) | After: every cage's port reports its module's medium | [cage-medium-2026-10-19.md](../measurements/cage-medium-2026-10-19.md) |
| [03-xgs136-module-media.png](measurements/cage-medium-2026-10-19/03-xgs136-module-media.png) | The XGS 136's module_media: what it refuses, a cage told nothing, and put back | [cage-medium-2026-10-19.md](../measurements/cage-medium-2026-10-19.md) |

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

### measurements/image-136-2026-10-21/ (1)

The page: [image-136-2026-10-21.md](../measurements/image-136-2026-10-21.md).

| image | what it shows | used on |
|---|---|---|
| [01-installed-updated-restored.png](measurements/image-136-2026-10-21/01-installed-updated-restored.png) | The XGS 136's own record: the release the image installed, the driver built at the first boot from
the sources the image carries, the update to 26.7.7 with the reboot held until the driver was built
for the new kernel, and the IPsec offload, ports and route after the restored configuration | [image-136-2026-10-21.md](../measurements/image-136-2026-10-21.md) |

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

### measurements/mac-counters-3300-2026-10-20/ (1)

The page: [mac-counters-3300-2026-10-20.md](../measurements/mac-counters-3300-2026-10-20.md).

| image | what it shows | used on |
|---|---|---|
| [01-the-macs-and-the-host.png](measurements/mac-counters-3300-2026-10-20/01-the-macs-and-the-host.png) | The XGS 3300's F1, F2 and module ports counted by the coprocessor's MACs beside the host's own counts: F2's 1,956 frames and 125,726 bytes in and 1,546 out on both sides | [mac-counters-3300-2026-10-20.md](../measurements/mac-counters-3300-2026-10-20.md) |

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

### measurements/port-errors-136-2026-10-20/ (2)

The page: [port-errors-136-2026-10-20.md](../measurements/port-errors-136-2026-10-20.md).

| image | what it shows | used on |
|---|---|---|
| [01-too-long-counted.png](measurements/port-errors-136-2026-10-20/01-too-long-counted.png) | An error only the switch sees: port 8's frame limit at 1,522 bytes, ten frames of 3,042 bytes lost and counted as too long, the limit put back | [port-errors-136-2026-10-20.md](../measurements/port-errors-136-2026-10-20.md) |
| [02-carrier-against-the-reads.png](measurements/port-errors-136-2026-10-20/02-carrier-against-the-reads.png) | The XGS 136's carrier changes against the cron that reads the switch's counters: every change outside a reload and a boot in the first seconds of the cron's minutes, and four ports still "no carrier" at 18:58 with their traffic crossing them | [port-errors-136-2026-10-20.md](../measurements/port-errors-136-2026-10-20.md) |

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
| [status-dark.svg](readme/status-dark.svg) | *no page yet* |
| [status-light.svg](readme/status-light.svg) | *no page yet* |

## Not on any page yet

Kept for a page that will use them - or for the wiki, which links here:

- [hardware/xg330/xg330-front.jpg](hardware/xg330/xg330-front.jpg)
- [readme/status-dark.svg](readme/status-dark.svg)
- [readme/status-light.svg](readme/status-light.svg)
