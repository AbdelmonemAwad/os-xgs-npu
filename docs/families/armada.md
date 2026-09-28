# ARMADA - Marvell CN913x

    PCI id     11ab:7080
    driver     npuep  (contrib/npuep)
    platforms  xgsdt1, xgsdt2-116, xgsdt2-126136, xgsdt2-138
    hardware   Sophos XGS 136, assembly AMDA0201, 14 ports - ON THE BENCH
    state      all fourteen front ports carry traffic

This is the family the project started on and the only one where the datapath works. Everything in
the top-level README's "What works" section is this family, and the protocol documents describe it:

- [../facility-protocol.md](../facility-protocol.md) - the facility framework and the handshake
- [../giu.md](../giu.md) - the GIU datapath, which is what carries packets
- [../mvmgmt.md](../mvmgmt.md) - the management netdev
- [../netagent.md](../netagent.md) - the network agent and the switch
- [../rpc.md](../rpc.md) - the RPC facility
- [../npu-bring-up.md](../npu-bring-up.md) - what has to happen, in order

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
