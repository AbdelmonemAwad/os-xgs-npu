# OCTEON TX2 98XX - Marvell CN98XX

    PCI id     177d:b100
    driver     none here
    platforms  shares the OCTEON TX2 platforms
    hardware   NONE. Nothing has been powered on.
    state      documented from the vendor's own tables

**This is its own family, not a variant of OCTEON TX2.** It has a separate branch in the vendor's
`xgs-host-startup.sh`, and that branch counts how many devices carry the id, because on these boards
**the id appears more than once**. Folding it into TX2 loses that, which is why the table keeps the
two apart.

Everything in [octeon-tx2.md](octeon-tx2.md) about shared module builds, runtime family selection and
mainline `octeon_ep` applies here as well - mainline's id table covers `0xB100` explicitly. The
vendor's fast-path binary for 98XX differs from the TX2 one only in compiler-generated local symbol
numbering, so even in user space the 98XX distinction is made at runtime rather than by separate code.

## What is held here

| material | detail |
|---|---|
| hardware | none |
| fast path | `usfp` for this family, **6.9 MB, with full symbols and DWARF** - a distinct binary, not a copy of the TX2 one |
| firmware | from BSP 21.0.0.169 and 22.0.2.546 |
| launcher | `usfp_startup_octtx2_98xx.sh`, with its own `usfp_rh.ko` |

That the binary and the launcher are separate from OCTEON TX2's is the concrete evidence for treating
this as its own family rather than a variant, which is also how the vendor's own probe treats it.
