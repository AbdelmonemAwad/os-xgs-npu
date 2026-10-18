# Changelog

What changed in each release of os-xgs-npu, newest first. **Unreleased** is what has been merged since
the last release and waits for the next one.

Written by `tools/changelog.py` from the commits on `main` - every change reaches it as one commit
named after its pull request - and each release is published with its own section as its notes, so
the Updates page on an appliance shows exactly this. How a release is made, and when:
[RELEASING.md](RELEASING.md). The project is in alpha - every number so far ends in `-alpha.N` - and
no release is advised for a firewall carrying real traffic.

## Unreleased

Nothing yet.

## 0.1.0-alpha.1 - 2026-10-18

The first release: everything merged until this day, as one version an appliance can be updated to
and back from. Nothing here is advised for a firewall carrying real traffic - ~~read the notice at the
top of the README first~~ read the safety box in the README first (the work-in-progress notice above it
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
