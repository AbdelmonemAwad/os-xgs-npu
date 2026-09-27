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
