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
