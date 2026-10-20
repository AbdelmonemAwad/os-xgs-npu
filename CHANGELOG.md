# Changelog

What changed in each release of os-xgs-npu, newest first. **Unreleased** is what has been merged since
the last release and waits for the next one.

Written by `tools/changelog.py` from the commits on `main` - every change reaches it as one commit
named after its pull request - and each release is published with its own section as its notes, so
the Updates page on an appliance shows exactly this. How a release is made, and when:
[RELEASING.md](RELEASING.md). The project is in alpha - every number so far ends in `-alpha.N` - so try a
release on an appliance you can reach before you trust it with a network you depend on.

## Unreleased

Nothing yet.

## 0.1.0-alpha.4.3 - 2026-10-20

A fix for the XGS 136 that 0.1.0-alpha.4.2 made urgent: reading the switch's port counters every
five minutes - installed everywhere by that release - made the 136's coprocessor report linked
ports down, sometimes for as long as no cable moved (#639). The 136 no longer reads them; after the
update, a reboot of the 136 clears any port it still believes down. And on the XGS 3300, XGS: Ports
counts F1, F2 and the expansion module's ports from the coprocessor's own MACs. No driver changed.

### Fixes

- xgs: the XGS 136's switch counters not read - the reads made the network agent report ports down ([#640](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/640))

### Improvements and additions

- xgs: the XGS 3300's own MACs counted on XGS: Ports - F1, F2 and the module's ports ([#637](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/637))
- website: the release notice's notes formatted, each pull request's number its link, the unsubscribe link plain ([#638](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/638))

## 0.1.0-alpha.4.2 - 2026-10-20

Checking for updates no longer runs out of GitHub's hourly allowance: XGS: Updates and its Plugins
tab ask npuforge.org's copy of GitHub's answers first, and what is installed is still confirmed with
GitHub itself. The switch's port counters on XGS: Ports work - their script was never installed - a
first install fetches the kernel's sources it needs to build the driver, and an appliance is counted
on npuforge.org as soon as it is installed or updated. On the XGS 136 the receive runs on the
coprocessor's doorbells by default, with a setting to turn them off; the driver changed, so it takes
effect at the next boot.

### Fixes

- xgs: the problem reports of 2026-10-20 - portstats.py installed, one report for one failing action, a first install fetches the kernel's sources ([#632](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/632))
- xgs: an appliance counted at the end of every install, not the next morning ([#635](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/635))

### Improvements and additions

- xgs: update checks ask npuforge.org's copy of GitHub's API first ([#631](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/631))
- npuep: the receive on the coprocessor's doorbells by default, and a setting for it ([#633](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/633))

## 0.1.0-alpha.4.1 - 2026-10-20

Three things on the pages of 0.1.0-alpha.4, found on the first appliance it was installed on: the
dates at the top of XGS: Updates read "Invalid Date" (since 0.1.0-alpha.3), its Channel menu cut its
text off, and XGS: Ports' switch counters showed empty headings until their first reading. And XGS:
About has a Releases tab: every release's changes, the one installed here marked. Nothing in the
drivers changed; no reboot.

### Fixes

- xgs: a Releases tab on XGS: About; the dates on XGS: Updates read right again ([#621](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/621))
- xgs: XGS: Updates' channel in OPNsense's own picker ([#622](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/622))
- xgs: the switch's counters read when the page opens, or 'Not available on this appliance' ([#624](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/624))

### Documentation

- website: /stats/ as figures, a chart, the most read pages by name, and the appliances ([#619](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/619))

## 0.1.0-alpha.4 - 2026-10-20

A reload of the XGS 136's driver could stop the host when the coprocessor held the tunnel's
encrypting association at that moment (#603); the association now gets the kernel's own ESP transform
back first. XGS: Ports shows the switch's own counters for each port behind it - the errors and drops
the host never sees. The XGS 136 can take its receive from the coprocessor's doorbells
(`hw.npuep.giu.rx_msix`, off by default until a soak with it on), and a reload of its driver applies
what a boot applies. The XGS 136's driver changed, so it asks for a reboot; the switch tool on the
coprocessor is replaced at the next boot.

### Fixes

- npuipsec: the kernel's ESP transform back when the engine lets an association go ([#615](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/615))

### Improvements and additions

- npuep: a reload applies what a boot applies once the ports are there ([#607](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/607))
- xgs: the switch's own counters for each port on XGS: Ports ([#614](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/614))
- npuep: the XGS 136's receive rings on the coprocessor's doorbells ([#616](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/616))
- relay, site worker: the first CodeQL alerts ([#612](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/612))

### Documentation

- website: a link to the Sponsors page, in both languages ([#608](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/608))
- news: sponsoring the project ([#610](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/610))
- CITATION.cff: GitHub's Cite this repository ([#611](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/611))
- docs: two hours of TLS through the XGS 136's IPsec engine ([#613](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/613))
- docs: six IPsec tunnels at once through the XGS 136 - 96 % ([#617](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/617))

## 0.1.0-alpha.3 - 2026-10-20

Power over Ethernet on the XGS 136, read and switched from XGS: Ports. The XGS 136's tunnel
connections in its own flow table, their encrypting direction in hardware, with what they encrypt
handed to the kernel's association, and a connection an IPsec policy covers kept out of hardware.
What the Traffic Shaper and the Captive Portal name is kept with the host, where ipfw sees it. XGS:
About, a Plugins tab on XGS: Updates, the coprocessor's own uptime, load and memory on the Overview,
and the IPsec page counting the XGS 136's associations right. Both drivers changed, so both
appliances ask for a reboot.

### Fixes

- reporter: a driver's tally of none is not a failure ([#605](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/605))

### Improvements and additions

- npuep: a connection an IPsec policy covers stays out of the XGS 136's hardware, and leaves it when a tunnel comes to cover it ([#576](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/576))
- status board: the feature rows read from parity.md, as the completions are from the tables ([#577](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/577))
- website: the newest release from GitHub's releases feed when its API refuses, so the notice goes ([#578](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/578))
- status board: the measured rows the same topics on both cards, each figure read from its page ([#579](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/579))
- npuep: tunnel connections in the XGS 136's flow table, their encrypting direction in hardware, set in XGS: Settings ([#580](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/580))
- README, legal page, home page: no Sophos software or firmware distributed, and where a rights holder writes ([#584](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/584))
- xgs: Power over Ethernet on XGS: Ports - read, and switched off, on or off-and-on ([#585](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/585))
- xgs: the IPsec page, the Overview and the widget count the XGS 136's associations right ([#587](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/587))
- octep: the platform's name and IPsec count; the XGS 136's hold measured on the XGS 3300, off ([#590](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/590))
- xgs: a Plugins tab on XGS: Updates - each appliance plugin's release, installed by hand ([#591](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/591))
- xgs: XGS: About - the project, its author, its links and addresses, the disclaimer ([#592](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/592))
- npuep: what tunnel flows encrypt is handed to the kernel's association ([#593](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/593))
- xgs: the coprocessor's own uptime, load and memory on the Overview ([#594](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/594))
- xgs: what the Traffic Shaper and the Captive Portal name is kept with the host ([#597](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/597))
- npuep: reload.sh puts OPNsense's interfaces back after a reload ([#604](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/604))

### Documentation

- docs: the XGS 3300's policy sweep measured - a connection in its hardware taken out when a tunnel comes to cover it ([#583](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/583))
- docs: the XGS 3300's revivals against the number of connections ([#586](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/586))
- docs: the PoE section of XGS: Ports, photographed on the XGS 136 ([#588](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/588))
- docs: the XGS 136's slow minutes are the bench PC's ([#589](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/589))
- docs: an IPv6 translation is beyond the fast path's entry - the XGS 3300 at 97 % ([#595](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/595))
- README: the pages section names XGS: About and the Plugins tab ([#596](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/596))
- docs: an hour of TLS through the XGS 136's IPsec engine - the XGS 136 at 95 % ([#598](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/598))
- docs: sixteen IPsec tunnels at once between the two XGS 3300s ([#602](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/602))

## 0.1.0-alpha.2.3 - 2026-10-20

Two IPsec features on the XGS 136's own engine, both measured at the gigabit link's rate in both
directions: extended sequence numbers, and IPv6 - a tunnel between IPv6 addresses and IPv6 packets
inside one. The XGS 3300's coprocessor console is now driven only once its Linux is up, and a slow
handshake is given six minutes. From this release an appliance also counts itself once a day for the
site's home page: a random identifier it made for itself, its model and this release, and nothing
else - XGS: Problem Reports turns it off. The rest is the project's site, npuforge.org. The XGS 136's
driver changed, so it asks for a reboot.

### Fixes

- xgs: the coprocessor's console only to its Linux, and six minutes for a slow handshake ([#561](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/561))
- website: the hosts Worker's deploy back in the deploy step ([#564](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/564))
- website: the language switch leads page to page, and the head names no alternate the theme would fetch a sitemap beside ([#565](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/565))
- website: a sitemap for each language, so the theme opens the other language with a full load ([#566](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/566))
- website: the API's answers kept at the edge, and never for hours in a browser ([#568](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/568))

### Improvements and additions

- website: npuforge.org, the project's site ([#552](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/552))
- website: the workflow deploys the other names' Worker too ([#554](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/554))
- docs: the build page is at build.npuforge.org ([#555](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/555))
- website: every message in text and HTML, from "npuforge" ([#556](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/556))
- status board: the completion weighed from the tables, drawn by the site ([#557](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/557))
- website: the release endpoint answers any page ([#558](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/558))
- website: the home page counts the appliances, the images built, the clones and the visits ([#562](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/562))
- website: for search engines, and the repository's facts without asking GitHub from the browser ([#563](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/563))
- website: /api/builds - the image builder's last builds and the steps of the ones still going, for its page ([#567](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/567))
- docs: installing, step by step - from nothing to the first sign-in, a picture for each step, in English and Arabic ([#569](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/569))
- npuep: extended sequence numbers on the XGS 136's engine, both ways - SA_ADD's ext_seq bit, measured ([#571](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/571))
- npuep: IPsec over IPv6 on the XGS 136's engine, outside and inside, both ways - measured ([#572](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/572))
- website: a welcome when an address confirms the release notices, with the latest release ([#573](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/573))

### Documentation

- README: link npuforge.org, the project's site ([#553](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/553))
- docs: the XGS 136's coprocessor does not drop an address either - the DoS commands not dispatched, measured ([#570](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/570))

## 0.1.0-alpha.2.2 - 2026-10-19

A small update: from this release an appliance sends its problem reports to the project's own
domain, `reports.npuforge.org`. The earlier address keeps answering, for the appliances that have not
updated yet.

### Improvements and additions

- relay: reports go to reports.npuforge.org, the project's own domain ([#548](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/548))
- SECURITY.md: without a GitHub account, write to security@npuforge.org ([#549](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/549))

### Documentation

- Update the Code of Conduct ([#547](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/547))

- README: the measured table one feature a row, both appliances side by side ([#543](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/543))
- docs: a thousand connections at once through the XGS 3300, measured ([#545](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/545))
- docs: unusual traffic and the JA4 cut, measured on the XGS 136 ([#546](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/546))
- docs: two hours of TLS through the XGS 136, every byte checked ([#550](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/550))

## 0.1.0-alpha.2.1 - 2026-10-19

A small update on the same day: the Updates page, as the three appliances were updated to
0.1.0-alpha.2 from it. An appliance installed from a test branch is offered the release, a reboot is
asked for only when the driver changed, and the changes are listed with their links. Every step of
an update is photographed in the guide, docs/guide/updating.md.

### Fixes

- updates: a branch build is offered the release, a reboot only when the driver changed ([#541](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/541))

## 0.1.0-alpha.2 - 2026-10-19

Two days of work on both appliances, gathered. The XGS 136 forwards connections in its
coprocessor through a takeout that works, revives a connection its fast path gives back and holds
one that keeps coming back, and reads the coprocessor's own counts into pf and the interfaces. Block
lists and TLS fingerprints cut connections already open, and unusual traffic is reported. Every
page of the XGS menu renders again on appliances set up by the installer, and each cage reports
the medium its module carries.

### Fixes

- npuep: every next hop named an empty cage - a whole link header read into a MAC's six bytes ([#486](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/486))
- npuep: the flow maker opens the front ports' interface entries itself ([#487](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/487))
- npuep: a connection the fast path let go is made again; install keeps a working module when a build fails ([#491](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/491))
- npuep: a takeout that takes the connection out, flow writes posted on the high ring ([#499](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/499))
- Problem reporter: one event logged over several lines is one report ([#510](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/510))
- Relay: no allowance spent on a refused report, ten notes a day per issue, no fingerprint in the issue ([#520](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/520))
- Updates: download the commit the check found, and show the log and a failure as text ([#521](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/521))
- npuep, octep: nothing posted on the high ring is written past its slot ([#526](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/526))
- XGS 136: a connection given back soon after it was made waits with the host before it is made again ([#532](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/532))
- npuep, octep: a cage reports the medium its module carries, read on the coprocessor ([#535](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/535))
- install: every view at once, so the credit partial is installed ([#537](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/537))

### Improvements and additions

- Actions: checkout and setup-node 4 to 7, Dependabot's monthly group ([#476](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/476))
- The menu section is XGS: the long name broke over two lines in the collapsed menu ([#480](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/480))
- Overview: the front panel drawn, the last restart, and how full the tables are; Updates: a test channel; the dashboard: offloaded flows ([#482](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/482))
- Flows: return a connection to the host, and a never-offloaded list ([#484](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/484))
- npuep: a connection the fast path gave back is revived in place, at once from the receive path ([#492](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/492))
- docs: the XGS 136's progress in the README, its status card and its weighing ([#494](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/494))
- docs: IPv6 through the XGS 136's coprocessor, measured; the 136 at 92 % ([#495](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/495))
- npuep: the candidate table at 16,384 slots; a thousand connections through the XGS 136, measured ([#497](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/497))
- docs: every page photographed in English and Arabic, and the images in folders by kind with an index ([#500](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/500))
- Cut the connections already open when a block list gains their address ([#508](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/508))
- Cut connections by their TLS fingerprint (JA4) ([#511](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/511))
- The images carry their owner and licence (CC BY 4.0) ([#512](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/512))
- octep: read the coprocessor's connection counts, and show them on the flows page ([#514](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/514))
- octep: a reused slot's counts, counts given to pf, the port counts; the pages with traffic ([#516](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/516))
- Settings: count hardware traffic in pf and on the interfaces (XGS 3300) ([#517](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/517))
- Watch for unusual traffic: a scan, a sweep, a host beyond its own normal - reported, never blocked ([#518](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/518))
- The coprocessor drops no address it is given: the DoS commands, probed ([#519](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/519))
- XGS 136: keep the last unknown-tag frame, and what its coprocessor sends, read ([#525](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/525))
- XGS 136: read the coprocessor's counts, into pf and onto the interfaces ([#528](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/528))
- reporter: the first notice is closable and opens its page, in the header instead of a banner ([#538](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/538))

### Documentation

- docs: the reset record read through the boot hook on the first XGS 3300 and the XGS 136 - the 136 has it, and bit 11 stays there too ([#478](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/478))
- docs: parity - problem reports run on the XGS 136 since the update to v0.1.0-alpha.1.1 ([#479](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/479))
- docs: parity - the XGS 136's flow maker needs interface entries the driver never opens ([#485](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/485))
- docs: the XGS 136 translating LAN to WAN in its coprocessor, measured ([#493](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/493))
- docs: five Arabic pages photographed again with their own column titles ([#501](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/501))
- docs: the Arabic pages with their tables right to left ([#502](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/502))
- README: badges for the block-list and TLS-fingerprint cuts ([#513](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/513))
- docs: the slow minutes on the XGS 136, looked for; both its cages hold copper modules ([#534](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/534))

## 0.1.0-alpha.1.1 - 2026-10-18

A small update on the same day as the first release: the problem reporter tells crashes apart
correctly, and the chipset's own record of why the host last reset is kept at every boot. The
Updates page shows a progress bar while it installs. The repository gains its community files.

### Fixes

- Reporter: crashes told apart by where they happened, info.last not counted, the npuep module's frames recognised ([#470](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/470))

### Improvements and additions

- A release is the latest, so the repository's page shows it ([#459](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/459))
- Updates: a progress bar from the update's own traces, the newest release's notes when nothing is newer, smaller release steps ([#466](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/466))
- Reset reason: what the host's chipset recorded as the cause of the last reset, kept at every boot and named in the report of a restart without a shutdown ([#471](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/471))
- Community files: a security policy, the code of conduct, issue forms and a pull request template ([#472](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/472))
- Security: report a vulnerability through GitHub's private reporting, now on; the e-mail address stays for those without an account ([#473](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/473))
- Dependabot keeps the workflows' Actions current, once a month; questions go to Discussions, now on ([#475](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/475))

### Documentation

- The work-in-progress notice lifted: the main part is done, and what is left arrives as updates ([#461](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/461))
- README: two roads onto an appliance - the image for most people, the files for experts ([#462](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/462))
- The status board: integration done, the notice lifted, the rest completed as updates ([#463](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/463))
- docs: the XGS 136's way in before the ports - the USB adapter, and others whose chip FreeBSD names ([#464](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/464))
- docs: CN8 is the expansion bay's connector; the empty socket beside it is Mini PCIe, not M.2 ([#465](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/465))

## 0.1.0-alpha.1 - 2026-10-18

The first release: everything merged until this day, as one version an appliance can be updated to
and back from. Nothing here is advised for a firewall carrying real traffic - ~~read the notice at the
top of the README first~~ read the README's "Getting it onto an appliance" first (the work-in-progress notice
was lifted on 2026-10-18). What each appliance has and lacks, side by side:
[docs/families/parity.md](docs/families/parity.md).

### Improvements and additions

- XGS 3300 (OCTEON TX, `octep`): all twelve front ports and an expansion module's as FreeBSD interfaces, named as the chassis names them; a receive interrupt per ring and seven transmit rings
- XGS 3300: pf's connections forwarded by the coprocessor - IPv4 with its translation, and IPv6 - in a table the size the board states, 2,000,000, written on the high-priority ring the board asks for; a connection handed back is revived in place
- XGS 3300: IPsec in the coprocessor's engine under the kernel's own offload contract - AES-GCM, AES-CBC with an HMAC, NAT traversal, IPv6 inside and out, replay windows to 4,096, outbound extended sequence numbers - and tunnel connections in the flow table
- XGS 3300: MTU 72 to 9,000 per port and jumbo frames on the switch chip; the board's sensors, the front panel, the identify light and the hardware watchdog
- XGS 136 (ARMADA, `npuep`): all fourteen front ports; pf's connections forwarded by the coprocessor, IPv4 with its translation, in a table the size the board states, 1,500,000; IPv6 flows built; four transmit queues; IPv6 neighbours found on a routed port
- XGS 136: IPsec in the coprocessor's engine - AES-GCM, AES-CBC with an HMAC, NAT traversal, replay windows to 4,096, fragmentation before the envelope; MTU 9,000
- The plugin: one section of the menu - Overview with the board's own limits, Settings, Ports, Transceivers, IPsec, Offloaded Flows, Updates, Problem Reports, Log File - three dashboard widgets, and every string in Arabic
- The plugin: the front ports assigned at the first boot by the names on the chassis; boot hooks that never stop a boot; the driver rebuilt for every kernel OPNsense installs
- Updates follow releases from this one on, not every commit merged
- Each page the plugin adds ends with a credit line; OPNsense's own footer is left as it is

### Documentation

- A guide to what works, how it works and what it cannot do; a page per family and per driver; every measurement with how it was taken and a photograph of the run; a dated page for every finding
- Every page dated with its last review, a catalogue of them all, and a monthly check that keeps the pages that are due in one issue
