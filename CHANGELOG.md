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

## 0.1.0-alpha.8.6 - 2026-10-26

A small release: when the coprocessor stops, the host stays up and takes it up again in place, on
both families, and restarts the appliance only if that fails. Until now an XGS 3300's host froze as
its coprocessor reset, and its hardware watchdog restarted it four minutes later. The new choice in
XGS: Settings, "Take it up again, or restart", is the default; a configuration saved with another
choice keeps it. The drivers changed, so the update asks for a reboot.

### Improvements and additions

- The coprocessor taken up again in place when it stops, on both families; the appliance restarted if that fails (#768) ([#821](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/821))

## 0.1.0-alpha.8.5 - 2026-10-26

A small release: the ARMADA family's driver waits for a receive descriptor the device is still
writing instead of dropping its frame, and names the kinds of frames that come on no port; and the
port check of an untested model says why it could not read the coprocessor, and whether two
interfaces that share an address are a LAGG's members. The driver changed, so the update asks for a
reboot on an ARMADA appliance.

### Improvements and additions

- Problem reports: an untested model's port check tells a coprocessor that did not answer from one without its list of ports ([#813](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/813))
- Problem reports: the port check says whether two interfaces share an address as a LAGG's members ([#814](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/814))
- npuep: a receive descriptor found unfilled is waited for, not dropped; frames on no port counted by kind (#748) ([#816](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/816))

## 0.1.0-alpha.8.4 - 2026-10-25

A small release: a plugin's update and install ask npuforge.org's copy of GitHub's API once
GitHub's git endpoint confirms the release's commit, so updating an appliance takes nothing of
GitHub's hourly allowance for its address while the site answers; and a plugin whose install
failed is installed at the next update.

### Improvements and additions

- XGS: Updates - a plugin's update asks npuforge.org once GitHub's git endpoint confirms its commit; a failed install is tried again ([#809](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/809))
- Comments and pages: what a change does, since when and why ([#810](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/810))

## 0.1.0-alpha.8.3 - 2026-10-25

A small release: XGS: About shows each release's text without the photographs its page on GitHub has,
and the paragraphs of the two small releases before it as they read now.

### Fixes

- XGS: About - a release's photographs left out of its text ([#807](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/807))

### Documentation

- CHANGELOG: the small releases said plainly ([#806](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/806))

## 0.1.0-alpha.8.2 - 2026-10-25

A small release: the plugins come along with the project's update. After installing itself, the update
brings every installed plugin of the family to its latest release and installs every one the appliance
never had and that suits it - the front panel only on the OCTEON TX family's rack models, which have
one. A plugin its owner took away is not brought back, and one that does not install never fails the
update. XGS: Updates says Checking for updates in the standard words.

### Improvements and additions

- The project's update brings the plugins along: the installed updated, the missing that suit installed ([#804](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/804))

## 0.1.0-alpha.8.1 - 2026-10-25

A small release. XGS: Updates asks for a reboot when an update changed the appliance's boot steps, not
only its driver: a boot step takes effect at the next boot, as a new driver does. Updating from
0.1.0-alpha.8 asks for that reboot once, so that 0.1.0-alpha.8's own boot steps take effect where its
driver was already the one running.

### Fixes

- XGS: Updates asks for a reboot when the boot steps changed, not only the driver ([#802](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/802))

## 0.1.0-alpha.8 - 2026-10-25

A gathered release. The OCTEON TX driver lays an IPsec association out as vendor version 23 reads it, in
200 bytes where vendor versions 21 and 22 take 192, so an XGS 3300 whose coprocessor came with vendor
version 23 offloads its tunnels too; an offloaded tunnel's counts come from the coprocessor's statistics
messages, the appliance's own traffic into it included. On the XGS 1xx family the coprocessor's PCIe
relaxed ordering and no-snoop are cleared at attach, and at unload the driver says goodbye to the
datapath before anything else. XGS: Overview has a Coprocessor tab (its software, chips, flash and eMMC,
read-only) and the fast path's counters; the coprocessor's console has a page of its own. Five plugins
join the family: the hardware watchdog, the kernel follower, block lists, port guard and fallback
crypto. Every plugin of the family sits under the XGS menu when this project is installed, in OPNsense's
order, and XGS: Updates installs one that is missing. A route through a tunnel's interface - a VTI with
an address on its gateway - no longer panics the flow sweep: both drivers leave such a connection with
the host. The drivers changed: the Updates page asks for a reboot.

![XGS: Overview, the Coprocessor tab](https://raw.githubusercontent.com/AbdelmonemAwad/os-xgs-npu/v0.1.0-alpha.8/docs/images/screens/en/01b-xgs-overview-coprocessor.png)

![XGS: Overview, the fast path's counters](https://raw.githubusercontent.com/AbdelmonemAwad/os-xgs-npu/v0.1.0-alpha.8/docs/images/screens/en/01c-xgs-overview-fast-path.png)

![XGS: Coprocessor Console](https://raw.githubusercontent.com/AbdelmonemAwad/os-xgs-npu/v0.1.0-alpha.8/docs/images/screens/en/09e-xgs-coprocessor-console.png)

![XGS: Updates, a plugin that is not installed, and Install](https://raw.githubusercontent.com/AbdelmonemAwad/os-xgs-npu/v0.1.0-alpha.8/docs/images/screens/en/07c-xgs-updates-plugins-install.png)

The five new plugins' pages: [Hardware Watchdog](https://raw.githubusercontent.com/AbdelmonemAwad/os-xgs-npu/v0.1.0-alpha.8/docs/images/screens/en/14-hardware-watchdog.png),
[Kernel Follower](https://raw.githubusercontent.com/AbdelmonemAwad/os-xgs-npu/v0.1.0-alpha.8/docs/images/screens/en/15-kernel-follower.png),
[Block Lists](https://raw.githubusercontent.com/AbdelmonemAwad/os-xgs-npu/v0.1.0-alpha.8/docs/images/screens/en/16-block-lists.png),
[Port Guard](https://raw.githubusercontent.com/AbdelmonemAwad/os-xgs-npu/v0.1.0-alpha.8/docs/images/screens/en/17-port-guard.png),
[Fallback Crypto](https://raw.githubusercontent.com/AbdelmonemAwad/os-xgs-npu/v0.1.0-alpha.8/docs/images/screens/en/18-fallback-crypto.png).

### Fixes

- SA_ADD laid out by the vendor version: 200 bytes on vendor version 23 (#759) ([#760](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/760))
- Problem reporter: a crash report carries the trap and what the kernel logged before it (#755) ([#772](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/772))
- npuep: clear the coprocessor's PCIe relaxed ordering and no-snoop at attach (#748) ([#773](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/773))
- octep: an offloaded tunnel's counts from the statistics messages, not SA_GET_STATS (#761) ([#775](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/775))
- Both drivers: the appliance's own traffic into an offloaded tunnel counted in its lifetime (#774) ([#778](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/778))
- npuep: the datapath's goodbye said first at unload, the host's datapath stopped, no PF_CLOSE (#741, #742) ([#782](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/782))
- Problem reporter: a repeated reset is its own boot's; models' reports told apart ([#788](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/788))
- npuep: the unload path - the inspector's function told, flows taken out while the channel is up (#780) ([#790](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/790))
- Both drivers: no next hop through a tunnel's interface - a VTI's gateway panicked the flow sweep (#792) ([#800](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/800))

### Improvements and additions

- Copies for a packet capture: the fast path's mirror, shown on the port it crossed (#680) ([#757](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/757))
- The coprocessor's counters cleared, and vendor version 21's numbers from 46 on (#680) ([#758](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/758))
- An offloaded association's state kept in step: the three commands measured (#680) ([#762](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/762))
- The ciphers nobody had tried: SHA-384 taken, ChaCha20 refused (#680) ([#763](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/763))
- Discovery 22 measured: Dragonfly turned on, its exports reach nothing seen, a tunnel's traffic lost (#680) ([#765](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/765))
- Discovery 27: a logger for the coprocessor's console, from every boot and in the support bundle (#680) ([#771](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/771))
- Port check: say when two interfaces, or two coprocessor ports, carry one address (#777) ([#779](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/779))
- XGS: Overview: a Coprocessor tab - its software, chips, flash and eMMC, read-only ([#783](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/783))
- XGS: Overview's Coprocessor tab: summary cards, and the vendor's build by that name ([#784](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/784))
- GUI: the coprocessor's console, fast-path counters, watchdog and memory errors, PCIe ordering, IPsec counts ([#785](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/785))
- The family's plugins follow the XGS section: told at install and removal ([#791](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/791))
- octep: instruments for the deaf ring (#361), off by default ([#793](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/793))
- XGS: Updates, Problem Reports and About take five more plugins; the menu in OPNsense's order ([#794](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/794))
- XGS: Updates installs a plugin of the family that is not installed ([#796](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/796))

### Documentation

- Update OPNsense now: a notice for PHP2Shell, test appliances included ([#751](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/751))
- docs: the OCTEON TX driver on vendor version 23, measured (#736) ([#752](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/752))
- docs: hardware flow offload over vendor version 23, measured (#736) ([#753](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/753))
- docs: the command numbers from 36 on are those of vendor versions 21 and 22 (#736) ([#754](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/754))
- Discoveries 16 and 18 read further: the intrusion-prevention stage's, not built (#680) ([#764](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/764))
- Discovery 20 measured: every frame from the coprocessor waits in queue 0, so the switch's scheduling orders nothing (#680) ([#766](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/766))
- Discovery 21 measured: nothing crosses between two front ports in the switch, and routed the host's rules decide (#680) ([#767](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/767))
- Discovery 24 measured: the coprocessor's watchdog is fed, and when it runs out the XGS 3300's host freezes with it (#680) ([#769](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/769))
- Discovery 25 measured: the network agent's SET_POLLING_MODE is refused by all three, and is about the mailbox (#680) ([#770](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/770))
- Completion: the discoveries all measured - the XGS 3300 and the XGS 136 at 96% (#680) ([#776](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/776))
- docs: thirty boots of the XGS 136, Port1 receiving after every one (#676) ([#789](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/789))
- docs: the pages of 2026-10-25 photographed - the Coprocessor tab, the fast-path counters, the console, nine plugins under XGS ([#795](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/795))
- docs: the measurement of 2026-10-25 named by the vendor version ([#797](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/797))
- CHANGELOG: 0.1.0-alpha.7.1's change named by the vendor version ([#798](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/798))

## 0.1.0-alpha.7.1 - 2026-10-24

### Improvements and additions

- docs: an interface entry can be removed - LIF_DELETE measured (#725) ([#745](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/745))
- Updates: an update checks again, and a newer build is not taken back (#677) ([#746](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/746))
- npuep: no receive-filter commands to the inspector's function (#699) ([#747](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/747))
- Both drivers find which vendor version left its software on the coprocessor - 21, 22 or 23 - and number their commands to match: vendor version 23 moved every command from 36 on one higher, so on a board last run with it the interface table could not be read back. The ARMADA driver's channel also opens on a coprocessor that listens late, and the counter tools name the counters of the version found (#736) ([#749](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/749))

## 0.1.0-alpha.7 - 2026-10-24

### Improvements and additions

- Release: a main release starts its ready-made images at once ([#739](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/739))
- npuep: say why the front ports' interface table is not confirmed - one line, `dev.npuep.0.iftable`, which `verify.sh` prints with the model and the board's revision, so its output is enough to report a failure (#736) ([#740](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/740))
- npuep: wait for the command channel's window to be claimed - the driver looked once at boot, and an ARMADA appliance whose coprocessor claimed it a second later carried nothing on any port; now it waits up to two minutes (#736) ([#743](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/743))

## 0.1.0-alpha.6 - 2026-10-24

### Improvements and additions

- Live users: a user's logout takes their connections out of hardware, measured on both appliances ([#707](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/707))
- docs: ready-made images, published with each release ([#708](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/708))
- site: the XGS 126 and 136 ports at the speeds their own board files give ([#710](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/710))
- docs: the guide's image table names every model ([#711](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/711))
- docs: the limit that remains with Suricata, on "What it cannot do" ([#712](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/712))
- docs: the images under the name committed to the OPNsense project ([#713](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/713))
- XGS: Overview and Settings say when the Intrusion Detection does not see what the coprocessor carries ([#714](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/714))
- Guide: the front ports appear at the first boot from the disk, not from the stick ([#715](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/715))
- Support bundle: each interface's counters with its drops, and the switch's frame limit ([#716](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/716))
- Captive Portal clients in hardware: each session a live user, and the coprocessor's counts for the portal ([#718](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/718))
- A port's rate, a connection's priority and the QoS commands, measured on the XGS 3300 ([#720](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/720))
- Flows: pf's DSCP mark kept on a connection in hardware (discovery 8) ([#722](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/722))
- Flows: a connection a bridge carries made a bridged flow (discovery 9) ([#724](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/724))
- Flows: a connection routed by a VLAN subinterface made in hardware (discovery 10) ([#725](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/725))
- npuep: a connection given back is revived every time, as on the XGS 3300 (issue 717) ([#726](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/726))
- Flows: a FIN or RST of a connection in hardware judged by pf with the fast path's numbers (issue 679) ([#728](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/728))
- XGS 136: the network agent sets the switch's frame limit from each port's MTU (#706) ([#731](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/731))
- DSCP on IPv6: neither fast path writes an IPv6 traffic class, measured ([#732](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/732))
- ARMADA: each front port takes its own address from the network agent (#730) ([#733](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/733)) - **after this update and its reboot every front port of an ARMADA appliance (XGS 87 to 138) answers to a new MAC address**: a DHCP server upstream sees a new client, and a reservation or a filter written against the old addresses needs writing again. An address set by hand on an interface's page is now followed as well; before, such a port received broadcast only
- XGS pages: each front port's MAC address in the port tables ([#734](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/734))

### Documentation

- README: Captive Portal clients in hardware, at a glance and among the measured badges ([#719](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/719))

## 0.1.0-alpha.5.6 - 2026-10-23

### Improvements and additions

- A cut drops the connection on the coprocessor itself, by its verdict: XGS: Settings "Cut in hardware", on by default; XGS: Settings in four tabs and XGS: Ports in three, Power over Ethernet among them ([#696](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/696))
- Where a connection sent to inspection goes: to a virtual function OPNsense does not make, measured ([#697](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/697))
- The inspector's virtual function on the XGS 3300: Suricata inspects a connection in hardware, then the hardware carries the rest ([#698](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/698))
- The inspector's function on the XGS 136: Suricata inspects a connection in hardware, then the hardware carries the rest ([#700](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/700))
- Inspect, then hardware: a setting, off by default, and OPNsense's own Intrusion Detection reading oxips0 - its inline IPS and the offload together, on both appliances ([#702](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/702))
- XGS: Updates, Plugins: each update's progress, Update all, and the release shown once installed ([#703](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/703))
- The flow maker's help no longer says an inline IPS and the offload are one or the other ([#704](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/704))

### Documentation

- The question on XGS: Overview, photographed in English and Arabic ([#693](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/693))

## 0.1.0-alpha.5.5 - 2026-10-23

### Improvements and additions

- The site is kept out of releases: not in an appliance's download, not in the changelog ([#678](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/678))
- The TCP window of a connection in hardware, checked there (#679) ([#682](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/682))
- An appliance carries what it reads: the documentation, the workflows and the repository's tools left out of its archive ([#683](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/683))
- The heartbeat with the fast path: the XGS 3300's answers every time, the XGS 136's does not (discovery 11) ([#687](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/687))
- When the fast path stops: reported, shown, and the appliance restarted if its owner chose that ([#688](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/688))
- The XGS 136's heartbeat: the answer is the beat taken off the host's queue ([#689](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/689))
- The XGS 3300's coprocessor reached over its PCIe management link instead of its console (discovery 28) ([#690](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/690))
- Nothing leaves an appliance unless its owner chooses: reports manual and the count off by default, asked once on XGS: Overview - an appliance updated to this release is turned the same way and asked again; and Suricata's inline IPS and the offload said to be, for now, one or the other ([#691](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/691))

### Documentation

- The discoveries of 2026-10-22: what the hardware can do that was not known, and the completion figures lowered to count it ([#681](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/681))
- The bench photograph as the measurements' main picture, and a news post on what the hardware can do ([#684](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/684))

## 0.1.0-alpha.5.4 - 2026-10-21

### Improvements and additions

- xgs: the coprocessor pages' Arabic is in opnsense-arabic's catalogue; nothing is merged from here ([#674](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/674))

## 0.1.0-alpha.5.3 - 2026-10-21

### Improvements and additions

- npuctl: trust the management link at boot when its ssh is refused; a reinstalled host replaces its old key (#668) ([#671](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/671))
- xgs: a restored configuration's driver settings reach the driver at the first boot (#669) ([#672](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/672))

### Documentation

- docs: an image from the builder on the XGS 136 - installed, updated from the GUI, its configuration restored ([#670](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/670))

## 0.1.0-alpha.5.2 - 2026-10-21

### Improvements and additions

- site: the add-your-appliance form on the home page too, beside every model, in every language ([#665](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/665))
- installer: say last when the next boot will have no driver; remove old kernel sources once the new kernel has settled ([#666](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/666))

## 0.1.0-alpha.5.1 - 2026-10-21

### Improvements and additions

- site: a page for every model and every expansion module, drawn; the menus, the footer and the home page; where the project stands and its releases; what search engines read ([#659](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/659))
- site: German, Spanish, French, Turkish, Russian and Japanese - the home page, the models, the modules, the status, the releases and how the measurements are taken ([#660](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/660))
- site: a link in a drawing no longer stops the page from changing; every language in the language menu; favicon.ico and a touch icon ([#662](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/662))
- reporter: a driver that is not attached is reported with what became of its module - installed or not, built for which kernel, loaded or not - and the last lines of the installer's build of it ([#663](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/663))

### Documentation

- docs: questions people ask, a glossary, how the measurements are taken, and the models' sources in the provenance ([#658](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/658))

## 0.1.0-alpha.5 - 2026-10-21

A gathered release. On both families, the appliance's own traffic through a tunnel that strongSwan has
moved onto a narrower path no longer loops in the kernel. On the XGS 3300, a receive ring whose interrupt
was raised and never handled is found within two ticks instead of up to 50 ms, every ring's interrupt has
a CPU of its own, and an arriving ESN association is decrypted by the engine. Every model of both families
now installs, its front ports named from one table by the assembly number on its board; every model but
the XGS 136 and the XGS 3300 is added untested - its status page says so, and its first boot checks its
ports against what its coprocessor reports. The drivers changed: the Updates page asks for a reboot.

### Fixes

- npuep, octep: the appliance's own packet too big for the tunnel's path is fragmented, not answered EMSGSIZE - which looped its TCP in the kernel ([#647](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/647))
- octep: a ring left with a burst - the interrupt raised and never handled; the look each tick, two faults fixed ([#651](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/651))

### Improvements and additions

- xgs: an update asks for a reboot only when the driver the boot loaded changed - not when its file is merely newer ([#642](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/642))
- octep: an arriving ESN association on the XGS 3300's engine, by default - measured to hold ([#645](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/645))
- xgs, npuep, octep: every model's front ports from one table, by the assembly number on its board ([#652](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/652))
- xgs, npuctl: every model installs, and every model not run here is marked research and development; every model as a table ([#654](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/654))
- xgs, npuep: each port's path from the vendor's device trees, the 138's tags by its coprocessor's order, a port check reported from untested models, tables marked added untested ([#655](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/655))

### Documentation

- README: the two address-drop badges say the same thing ([#643](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/643))
- docs: the XGS 136's receive marked done on the status picture, and the picture drawn again ([#646](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/646))
- docs: RESEND does not wake a ring that lost its interrupt ([#653](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/653))

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
