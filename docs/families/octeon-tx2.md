# OCTEON TX2 - Marvell CN9xxx

    PCI id     177d:b200   (VF 177d:b203)
    driver     none here
    platforms  xgs1ul, xgs1ul_4x80, xgs2u, xgs2ub
    hardware   NONE. Nothing has been powered on.
    state      documented from the vendor's own tables and binaries

**No claim is made about this family.** What follows is what the vendor's material says, carried
because it is useful to anyone who owns such a board and because the family table has to be complete
for the other rows to mean anything.

## What is known without the hardware

The target-side kernel modules are **the same build** as OCTEON TX's. Checked on the BSP: all 105
modules in each of the three trees differ by exactly 20 bytes at file offset `0x50` - the GNU
build-id - and nothing else, with identical module version magic. Family selection happens at
runtime: the endpoint module carries the string `Unsupported CPU type` and the device-tree aliases
`marvell,octeontx-ep` and `marvell,octeontx2-ep`, and takes `epf_num`, `pem_num` and `host_sid` as
parameters, the last with its formula stated in the module itself.

Per-family divergence lives in **user space**, not in the modules. The vendor's fast-path binaries
differ genuinely between families: the TX2 one is dominated by `otx2_ssogws` and `otx2_nix` symbols
and whitelists real PCI addresses with a DSA switch header and an IPsec SPI limit, where OCTEON TX
presents a single `eth_octeontx` virtual device and counts host-facing ports as a parameter of it.
The host-facing plumbing is therefore shaped differently again, and an implementation should not
assume either of the other two families.

## Where upstream already helps

Linux's mainline `octeon_ep` driver **does** bind this part. Its id table, in
`drivers/net/ethernet/marvell/octeon_ep`, covers `0xB100`, `0xB200`, `0xB203`, `0xB400`, `0xB900`,
`0xBA00`, `0xBC00` and `0xBD00`, all under vendor `0x177d`. It does **not** cover `0xA300`.

It is a different protocol generation, though: its ready magic is `0xdeaddeadbeefbeef`, and it finds
its control mailbox **by construction** - a PEM BAR index into BAR4 - rather than by following a
pointer out of a scratch register. Two things do carry over: the PEM BAR index mechanism with its
4 MB windows, and the BAR0 window-address/window-data register pair that lets a host read target
physical memory without mapping it.

## What is held here, and what could be done without the hardware

| material | detail |
|---|---|
| hardware | none |
| fast path | `usfp` for this family, **6.8 MB, with full symbols and DWARF** |
| firmware | U-Boot for `xgs1ul`, `xgs1ul_4x80`, `xgs2u`, `xgs2ub`, both BSP versions |
| launcher | `usfp_startup_octtx2.sh`, and `DP_1UL/2U/2UB_STARTUP_SCRIPT` in `dp_startup.conf` |
| source | the shared GPL drop, whose `sources-dpdk_app` builds this same application |

The symbols are the point. Most of what this project learned about OCTEON TX was recovered from the
equivalent binary, so a reader with no CN9xxx hardware can still establish the message numbering, the
port model and the startup configuration for this family from what is here.
