<!-- reviewed: 2026-10-21 -->
# The words these pages use

What each word means on this site, in a sentence, with the page that says more.

| word | what it means here | more |
|---|---|---|
| **appliance** | a Sophos XGS box: two computers in one case - the host and the coprocessor | [how it works](how-it-works.md) |
| **host** | the appliance's x86 computer: it runs OPNsense, and reaches the front ports only through the coprocessor | [how it works](how-it-works.md) |
| **coprocessor** | the appliance's second computer, an ARM network processor - Marvell CN913x on the ARMADA family, Cavium CN83xx on OCTEON TX - which owns every front port | [the families](families-and-hardware.md) |
| **family** | the appliances built on one kind of coprocessor; each family has a driver of its own | [the families](families-and-hardware.md) |
| **driver** | the FreeBSD kernel module that speaks to the coprocessor: `npuep` on ARMADA, `octep` on OCTEON TX | [ARMADA](../families/armada.md), [OCTEON TX](../families/octeon-tx.md) |
| **plugin** | the OPNsense package around the drivers: its pages in the web interface, its settings, its scripts | [the pages](the-pages.md) |
| **front port** | a socket on the panel - `Port1`, `F1`, `A1` - named as on the metal; the host sees each as an interface (`npup1`, `oxp0`) | [front ports by model](../families/front-ports-by-model.md) |
| **tag** | the coprocessor's name for a port, which travels with every frame between it and the host | [front ports by model](../families/front-ports-by-model.md#how-a-port-is-tagged) |
| **switch** | the chip behind most RJ45 ports (an 88E6193X); its ports reach the coprocessor over one link | [front ports by model](../families/front-ports-by-model.md) |
| **cage** | the socket an SFP or SFP+ transceiver goes into: 1 Gbit/s for SFP, 10 Gbit/s for SFP+ | [what a cage carries](../measurements/cage-medium-2026-10-19.md) |
| **PHY** | the chip between a MAC and a socket, which turns the coprocessor's lanes into a copper or fibre signal | [the families](families-and-hardware.md) |
| **module bay** | the slot in the front for an expansion module; on the 1U appliances it is the coprocessor's, on the desktops the host's | [expansion modules](../octep/expansion-modules.md) |
| **assembly number** | the board's part number - `AMDA0201-0003` is an XGS 136 - read from the planar serial; it says the model, the board revision does not | [front ports by model](../families/front-ports-by-model.md) |
| **board file** | the vendor's description of one assembly: each port's label, socket and where it attaches | [front ports by model](../families/front-ports-by-model.md) |
| **fast path** | the coprocessor forwarding a connection port to port by itself, without the host | [what works](what-works.md) |
| **flow offload** | putting a connection the firewall has let through into the fast path | [what works](what-works.md) |
| **give-back** | the fast path handing a connection back to the host - when it ends, or when something the host must see happens to it | [what works](what-works.md) |
| **IPsec association** | the keys and settings of one direction of one IPsec tunnel; the coprocessor's engine can hold it and encrypt or decrypt for it | [what works](what-works.md) |
| **ESP** | the IPsec packet that carries the encrypted traffic | [what works](what-works.md) |
| **NAT-T** | ESP carried inside UDP port 4500, to cross a NAT on the way | [what works](what-works.md) |
| **ESN** | extended sequence numbers: 64-bit counters for an association that carries a great deal | [ESN, measured](../measurements/esn-136-2026-10-20.md) |
| **MTU** | the largest packet a link carries in one piece; these appliances take up to 9000 bytes on every port | [the MTU, measured](../measurements/mtu-2026-10-17.md) |
| **bypass** | a pair of ports that relays join when the appliance is off or stalled, so the two networks stay connected | [the 1U appliances](../families/octeon-tx-peripherals.md) |
| **PoE** | power over Ethernet: a port that powers what is plugged into it - labels 11 and 12 on the XGS 136 | [PoE, measured](../measurements/poe-136-2026-10-20.md) |
| **problem report** | an issue the plugin files on its own when something goes wrong, when reporting is on | [problem reports](../problem-reports.md) |
| **port check** | on a model not run here, its first boot holding the driver's ports against what its coprocessor reports | [front ports by model](../families/front-ports-by-model.md#what-says-whether-a-models-table-is-right) |
| **run here** | a model this project has been run on, in the lab; every other model is **added, untested** | [every model](https://npuforge.org/devices/) |
| **the vendor's figures** | the numbers in the vendor's datasheet, taken its own way; written as the vendor's, beside ours | [how the measurements are taken](../measurements/methods.md) |
