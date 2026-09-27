# Where the OCTEON TX knowledge came from

This page is the OCTEON TX (Cavium CN83XX) counterpart of [../provenance.md](../provenance.md), and it
is separate on purpose. The two families reached us through **different upstream components, from
different release channels**, and a reader checking what was taken from where should not have to
disentangle two upstreams inside one page. There will be one of these per family that ever gets a
driver here.

This project is **BSD-2-Clause**. Every file says so, and `LICENSE` is the whole of it.

It is not legal advice. It is a statement of practice, written so that anyone reviewing the project
can check the practice against the tree.

## The rule, unchanged

> **Facts are transcribed. Expression is not copied.**

A register offset, a magic word, a field width, the order of fields in a structure the hardware
itself defines, the minimum length a device will accept - these are facts about an interface. The code
that acts on them is written here, from nothing.

Where that rule is visible in this family: `contrib/octep/` declares **no structure over device
memory at all.** The vendor's `barmap.h` and `desc_queue.h` both declare C structs, and it would have
been shorter to transcribe them. Instead `octep.h` and `octep_mgmt.c` carry named byte offsets -
`OCTEP_BARMAP_FCLT_OFF`, `OCTEP_DQ_CONS_IDX`, `OCTEP_DESC_SIZE` - and every access goes through
`bus_read_*` and `bus_write_*` at a computed offset.

That is the same decision, for the same two reasons, as `contrib/npuep/npugiu.h`: a C struct laid over
MMIO invites the compiler to merge, split, reorder or elide accesses that must happen exactly once,
and a list of offsets is the only form that could be published under this licence.

## Where it came from, exactly

Sophos publishes the source. It is four archives deep, and the path is worth recording because
the component matters more than the ISO:

    SFOS_OSS-22.0.1_MR-1-490.iso
      BSP/npuos-gpl-package-2026.0213-1238-56-v22.0.Maint.040.Luzon/
        gpl-package-...tar.bz2
          gpl-package/gpl-pkg.tar.bz2
            extensions-sources-pcie_ep_octeontx-SDK10.22.03/pcie_ep_octeontx/
              sources-pcie_ep_octeontx-SDK10.22.03.tar.bz2

`manifest-gpl-package.json` at the top of that package names 46 components with a git remote and
commit for each. The two that matter here:

| component | branch | commit |
|---|---|---|
| `pcie_ep_octeontx` | `pcie_ep_octeontx-release-SDK10.22.03` | `c5bf95b85650575727e0a81f4afbccb5eacdb63c` |
| `sdk-ext-pcie-ep` | `sdk-ext-pcie-ep-release-SDK10.22.03` | `d133e9c8125c15b01c3f595d4cd395d5286f002b` |

**Licence.** Every file consulted carries `Copyright (c) 2020 Marvell` and
`SPDX-License-Identifier: GPL-2.0`.

**A second component in the same package, and it is not the same component.** The SDP work needed
the code that runs on the *coprocessor*, which is not in `pcie_ep_octeontx` at all. It is the
coprocessor's Linux kernel, shipped in the same GPL package by a different route:

    marvell-base-sdk-sources/linux/sources-linux-4.14.76-SDK10.22.03.tar.bz2
      drivers/net/ethernet/cavium/octeontx-83xx/

The manifest records `marvell-base-sdk-sources` and `linux` as **version `10.22.03` with no git
remote and no commit** - unlike the components above, which each name one. So this material is
traceable to a release, not to a revision, and that is worth saying plainly rather than implying a
precision the manifest does not offer.

**And it is not the kernel that is running.** The drop carries 4.14.76; the coprocessor runs
4.14.207-10.22.03. Where the two could disagree, this page says which one a claim rests on.

**How this differs from the ARMADA material**, and why the two pages are apart: that came from the
`pcie_ep_armada-release-SDK10.22.03` release, a different component with its own branch and commit,
and its files are marked `GPL-2.0-only`. The licence family is the same; the **origin is not**, and
traceability is per component rather than per licence. Keeping one page per family means any claim
here can be checked against one named commit of one named component.

## What was read, and what was taken

| Source | Licence | What was taken |
|---|---|---|
| `host/.../common/cn83xx_pf_regs.h` | GPL-2.0 | CSR offsets: the scratch register at `0x20180`, the SLI window register pair, the EPF stride. For SDP: `SDP_EPF_RINFO` at `0x20190` and its four field positions, the 128 KiB ring stride, the per-ring `R_IN_*` and `R_OUT_*` offsets, and the `IDLE`/`RDSIZE`/`IS_64B`/`IMODE` bit positions |
| `host/.../osi/cn83xx_pf_device.c` | GPL-2.0 | That the ring CSRs are reached as `mmio[0]` plus an offset, i.e. BAR0 directly and not through the SLI window; that the host walks every ring it owns reading these same registers; and the names and order of the bring-up steps, recorded in the family page as a ladder and not implemented here |
| `drivers/net/ethernet/cavium/octeontx-83xx/octeontx_main.c` | GPL-2.0, and a **different component** - see above | The one fact that separates the vocabulary: `net_port_count` is the BGX front ports, `pci_port_count` is the SDP host-facing ports. Three lines of a structure assignment, read as a definition, not copied |
| `drivers/net/ethernet/cavium/octeontx-83xx/slipf_main.c` | GPL-2.0, same component | The identity of `slipf`: module name, PCI driver name `octeontx-sli`, and that `/sys/module/slipf/parameters/pci_port` is its own. And the target half of the EP-mode handshake: the order of the four steps, that both of its waits are untimed busy loops, that it publishes its timer rate in the low sixteen bits of its reply, and that it creates the SDP port only after the exchange completes |
| `drivers/net/ethernet/cavium/octeontx-83xx/sli.h` | GPL-2.0, same component | The four handshake constants and the field positions of the info word |
| `host/.../osi/octeon_device.c` and `.../kernel/drv/octeon_main.c` | GPL-2.0 | The host half of the same handshake: which value the host writes first, what goes into the info word and where each field sits, and that the host answers on a once-a-second poll rather than inline |
| `target/drivers/pcie_ep/src/barmap.h` | GPL-2.0 | The barmap's field order and sizes, the version encoding, the 4 MB BAR index granularity, that entry 15 is GICD, `GICD_SETSPI_NSR` and the SPI base |
| `host/.../osi/octeon_device.c` and Sophos's 29 patches to it | GPL-2.0 | The readiness magic `0xABCDABCD`, that the high half of the scratch word is an offset, that an all-ones version means retry, and which mapped BAR the table lives in per chip |
| `host/.../kernel/drv/facility.c` | GPL-2.0 | That a doorbell is a 32-bit store of an SPI number to `gicd_offset`, and that the number is range-checked first |
| `host/drivers/mgmt_net/bar_space_mgmt_net.h` | GPL-2.0 | The management register map - status, interrupt and mailbox offsets, the state numbering, the two ring offsets |
| `host/drivers/mgmt_net/desc_queue.h` | GPL-2.0 | The descriptor queue header layout and the descriptor bitfield positions |
| `host/drivers/mgmt_net/host_ethdev.c` | GPL-2.0 | The order of the bring-up: shadows and buffers before any address is published, rings complete before `HOST_READY`, and that a polling mode is a legitimate alternative to interrupts here |
| `target/drivers/mgmt_net/target_ethdev.c` | GPL-2.0 | What the target validates, and that it goes to `TARGET_FATAL` rather than refusing - including the `ETH_ZLEN` minimum that a host must pad to |
| `pcie_ep.ko` on the appliance | proprietary binary | Confirmation of the constants, by decoding the module's own `MOVZ`/`MOVK` immediates, and its printf formats |
| `octeon_drv.ko` on the appliance | proprietary binary | That a CN83xx path exists at all, from its two distinct readiness log formats |
| The appliance's own logs and registers | - | Confirmation that every reading above was right |

Every one of those is a description of an interface. **None of it is in this tree as code.**

Where source and hardware disagreed, the hardware won and the comment says so - which is also how the
one real error was caught: an early note put the facility table in the wrong mapped BAR, because the
patch context being read was the CN9xxx branch rather than the CN83XX one.

Two later disagreements are recorded the same way, both on the family page. The published
`poll_for_ep_mode` abandons the handshake eleven seconds after the coprocessor boots; the running
kernel answered it after five hours, and the running kernel is 4.14.207 against a published 4.14.76.
And the vendor's host driver decodes the coprocessor's reported timer rate from the wrong sixteen
bits, so what this tree reads as 800 ticks/us their own log would have printed as 44510. In both
cases the appliance decided it.

## Quotation

Two fragments appear verbatim, both in `contrib/octep/octep_mgmt.c` and both in the comment that
explains why frames are padded:

- the condition `if (len < ETH_ZLEN || is_frag)`, which is the target's own test; and
- the log line `"mgmt_net:bad rx pkt"`, which is what it prints.

They are there because the consequence is severe and non-obvious - the target refuses the frame
*without consuming the descriptor*, so one short frame stops the ring permanently - and a reader
should be able to check that claim against what the vendor actually wrote. A paraphrase would be worse
for exactly that reason.

If any rights holder disagrees with that judgement, the remedy is two lines and the surrounding
explanation stands without them. Open an issue.

## Nothing was taken from Linux's octeon_ep

Mainline Linux has an `octeon_ep` driver, also GPL-2.0, by the same Marvell author. It was **read**,
and it is cited in [../families/octeon-tx2.md](../families/octeon-tx2.md) because it covers `0xB100`
and `0xB200` and therefore matters to anyone with those boards. It does **not** cover `0xA300`, it is a
different protocol generation - a different magic word, and a mailbox found by construction rather
than by following a pointer - and nothing from it is in this tree in any form.

## What is deliberately not here

- **No vendor source.** Not a file, not a function, not a structure declaration.
- **No firmware and no boot images.** The coprocessor runs the vendor's own, which stays on the
  appliance. The per-platform U-Boot images and CPLD files exist on the bench and are not published.
- **No binaries.** Nothing is redistributed.
- **No credentials, no real addresses, no hardware identifiers.** `tools/check-private-data.py` runs
  in CI and fails the build over any of them. It caught a real MAC address and a live IP in a draft of
  the OCTEON TX family page, before that page was ever pushed.
