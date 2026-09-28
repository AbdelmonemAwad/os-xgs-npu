# Families

Six coprocessor families, read out of the vendor's own `xgs-host-startup.sh`, which probes for one
PCI id per family, or for a CPU model string - two greps, for GR, which wants `Atom` and `P69` -
and exports the result:

| family | probe |
|---|---|
| `OCTEON_TX` | PCI `177d:a300` |
| `OCTEON_TX2` | PCI `177d:b200` |
| `OCTEON_TX2_98XX` | PCI `177d:b100` - **its own family**, and its branch counts the devices, because the id appears more than once |
| `ARMADA` | PCI `11ab:7080` |
| `TOPAZ` | `/proc/cpuinfo` contains `Atom C11` - **no coprocessor** |
| `GR` | `/proc/cpuinfo` contains `Atom` **and** `P69` - **no coprocessor** |

## Assembly to platform

`xgs-check-uboot-version.sh` maps the assembly part number to a platform name, and the platform name
alone selects the boot image `<platform>-boot.img`:

| assembly | platform | family |
|---|---|---|
| AMDA0200-0001..0004 | `xgsdt1` | ARMADA |
| AMDA0201-0001..0004 | `xgsdt2-126136` | ARMADA - **the XGS 126 and 136** |
| AMDA0208-0001..0002 | `xgsdt2-116` | ARMADA |
| AMDA0224-0001 | `xgsdt2-138` | ARMADA |
| AMDA0202-0001..0004 | `xgs1us` | OCTEON TX - **the XGS 3300** |
| AMDA0203-0001..0002 | `xgs1ul` | OCTEON TX2 |
| AMDA0228-0003 | `xgs1ul_4x80` | OCTEON TX2 |
| AMDA0204-0001/0002/0099, AMDA0225-0001/0099 | `xgs2u` | OCTEON TX2 |
| AMDA0205-0001, AMDA0226-0001 | `xgs2ub` | OCTEON TX2 98XX - `dp_startup.conf` starts the `octtx2-98xx` fast path on 2Ub |

Anything else hits the default arm and the script exits with `ERROR: unknown system`. GR
assemblies are not in this map at all, because a GR board has no coprocessor bootloader to check -
one would hit that default arm.

**A defect in that map, worth knowing before trusting it.** In the 22.x version, `AMDA0228-0001` and
`AMDA0228-0002` appear in **two** case arms - first under `xgs1ul`, then again under `xgs1ul_4x80`.
A shell `case` takes the first match, so those two assemblies can never reach `xgs1ul_4x80`; only
`AMDA0228-0003` does. This is the script that decides whether to reflash a coprocessor bootloader and
with which image, so treat the map as a starting point and not as an authority for boards nobody here
owns.

## Two identity mechanisms, and they are independent

- **The assembly part number**, above, picks the **U-Boot image**.
- **A hash of two DMI strings** picks the **model, the install target and the coprocessor firmware**.
  The installer never reads a model name: for each row of its table it runs a probe, hashes the
  output with MD5 and compares digests, over `dmidecode -s system-version` and
  `-s system-product-name`. The format is `<model>[W]r<revision>`, and the hash covers the trailing
  newline that the probe leaves in place.

If a board ever reports the wrong model, or gets the wrong coprocessor image written to it, suspect
DMI before suspecting hardware.

## What is held, per family

Scope is set by material, not by ambition. This is what exists on the bench, so that a reader can see
at a glance which families can be worked on and which can only be described.

| family | hardware | fast path binary | U-Boot | coprocessor rootfs | state |
|---|---|---|---|---|---|
| ARMADA | **XGS 136** | `usfp` 1.9 MB | 4 platforms, both BSPs | complete | all fourteen ports carry traffic; board powered off, work deferred |
| OCTEON TX | **XGS 3300** | `usfp` 2.9 MB, two builds | `xgs1us`, both BSPs | complete, plus a 591 MB image of the running v22.0.2 | ports raise and link; the return direction does not work |
| OCTEON TX2 | none | `usfp` 6.8 MB, symbols and DWARF | 4 platforms | a BSP rootfs, holding the `octtx2` fast path and its `OCTEONTX2` module tree; none from a running board | described only |
| OCTEON TX2 98XX | none | `usfp` 6.9 MB, symbols and DWARF, distinct binary | both BSPs | a BSP rootfs, holding the `octtx2-98xx` fast path and its `OCTEONTX2-98XX` module tree; none from a running board | described only |
| GR | none | not applicable | none | not applicable | no coprocessor exists |
| TOPAZ | none | not applicable | none | not applicable | no coprocessor exists |

Two things follow that are easy to miss.

**The fast path binaries carry full symbols and DWARF.** Most of what this project knows about the
OCTEON TX control plane was recovered from one of them rather than from source, and the same
method applies to the two families nobody here has hardware for. Reading TX2 is not blocked on
buying a board. Verifying anything about it is: desk work can go as far as the binaries and no
further, and this repository claims nothing for a family it has not run. OCTEON TX is the
demonstration - the hardware is here, the binaries were read, and the reading was still wrong in
places until a measurement said so.

**U-Boot is held for all nine platforms**, which is the whole XGS line, not only the two appliances on
the bench. The per-platform launchers in `etc/sophos/dp_startup.conf` and the `usfp_startup_*.sh`
scripts are held with them, and they are the clearest statement of how the vendor configures each
family that exists anywhere - clearer than the source, because they are the version that ships.
