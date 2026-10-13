# ARMADA - Marvell CN913x

    PCI id     11ab:7080
    driver     npuep  (contrib/npuep)
    platforms  xgsdt1, xgsdt2-116, xgsdt2-126136, xgsdt2-138
    hardware   Sophos XGS 136, assembly AMDA0201, 14 ports - ON THE BENCH
    state      all fourteen front ports carry traffic

This is the family the project started on and the only one where the datapath works. Everything in
the top-level README's "What works" section is this family, and the protocol documents describe it:

- [../facility-protocol.md](../npuep/facility-protocol.md) - the facility framework and the handshake
- [../giu.md](../npuep/giu.md) - the GIU datapath, which is what carries packets
- [../mvmgmt.md](../npuep/mvmgmt.md) - the management netdev
- [../netagent.md](../npuep/netagent.md) - the network agent and the switch
- [../rpc.md](../npuep/rpc.md) - the RPC facility
- [../npu-bring-up.md](../npuep/npu-bring-up.md) - what has to happen, in order

## What is specific to this family

The facility table sits at a **fixed offset** in the window and is guarded by a cookie -
`0xAFACAFAC` for the facility itself, `0xD0FAC10D` for the barmap - and its facility records are
`{bar, type, offset, size}`. Target-to-host doorbells are **MSI-X vectors**, which `npuep` allocates.

A fifth facility, **GIU**, exists here and is what carries the front ports. It does not exist on
OCTEON at all, and its absence there is the clearest single sign that these are different protocols
rather than one protocol with two PCI ids.

The reset polarity is a per-board table rather than a constant, so the module reads the assembly
number out of the bridge's EEPROM and looks it up. Values are carried for AMDA0200, AMDA0201,
AMDA0202-0205, AMDA0208 and AMDA0224. **Only AMDA0201 has been tested on real hardware.**

## What is held here, and what it allows

| material | detail |
|---|---|
| hardware | **XGS 136 on the bench**, assembly AMDA0201 |
| coprocessor rootfs | complete, including `NPU-BSP-slot1/rootfs/` |
| fast path | `usfp` for this family, 1.9 MB, with symbols |
| firmware | U-Boot for all four ARMADA platforms, from BSP 21.0.0.169 and 22.0.2.546 |
| source | the Sophos GPL release, 794 files, including the `pcie_ep_armada` patches |
| launcher | `armada_target_setup_usfp.sh`, selected by `DP_DT1/DT2_STARTUP_SCRIPT` |

This is the family with the most complete material and the only one whose ports all carry traffic.

## Building and running it

`npuep` is not packaged, for the same reason `octep` is not: an out-of-tree module has to be
compiled against the headers of the kernel that is actually running, and OPNsense ships no kernel
sources. So it is built on the appliance. `install/install.sh` lays the sources down in
`/usr/local/share/os-xgs-npu/npuep` and builds through `install/kernel-follow.sh` - the same
follower that rebuilds the OCTEON TX module after every kernel update, which since 2026-10-13
follows whichever of the two modules is installed - and `07-npuep` loads it at boot, after
reading the kernel stamp beside it and refusing a module built for another kernel (the previous
module is kept as `npuep.ko.prev` for the kernel it was built for). Before that date the follower
answered every tick on this appliance with "the driver is not installed on this machine", the
upgrade and stop hooks were gated on the OCTEON TX's PCI identity alone, and the boot hook read
no stamp: a kernel update here would have loaded the old module unchecked.

Measured on 2026-10-13, on the XGS 136, with the owner running the update from the GUI: OPNsense
26.7 to 26.7.7 brought the kernel from 15.1-RELEASE-p1 to p4. The reboot's stop hook ran the
follower; it read p4 from `/boot/kernel/kernel`, fetched that commit's sources, built the module
from the laid-down tree, kept the p1 module as `npuep.ko.prev` with its stamp, installed the p4
one and cleared the fall-back - 2 min 59 s in all - and the host came up on p4 with the module
loaded, all fourteen ports present and the WAN lease back, nothing done by hand.

    sh contrib/npuep/fetch-sources.sh
    sh install/install.sh
