# GR

    probe      /proc/cpuinfo contains "Atom" AND "P69"
    assembly   AMDA0004-*
    driver     not needed
    hardware   NONE
    state      there is no Marvell coprocessor; the silicon is Intel

Like [TOPAZ](topaz.md), a GR board has **no PCIe coprocessor** of the kind the rest of this project
exists to drive. It is detected by two CPU model strings together rather than by a PCI id, and its
assembly numbers are `AMDA0004-*` - which is why it is absent from the U-Boot platform map entirely:
there is no coprocessor bootloader to check.

What it does have is Intel accelerators, and Sophos publishes their drivers in the open-source ISO as
`kmods-xgs-gr-drivers`: QuickAssist crypto, an Intel Ethernet Switch driver, and a handful of
smaller ones. That source has been archived for reference, but nothing has been built or run, and
none of it belongs in this repository - it is Linux driver source for silicon that needs no help from
us on FreeBSD.
