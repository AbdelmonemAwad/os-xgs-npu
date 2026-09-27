# TOPAZ

    probe      /proc/cpuinfo contains "Atom C11"
    driver     not needed
    hardware   NONE
    state      there is no coprocessor to drive

TOPAZ boards have **no PCIe coprocessor at all**. The vendor's `xgs-host-startup.sh` detects them by
CPU model rather than by a PCI id, and their branch does none of the endpoint work the other families
need: no facility table, no doorbell, no handshake, because the host owns the ports directly.

Nothing in this repository applies to such a board, and nothing needs to. It is listed so the family
table is complete, and so that somebody reading the vendor's startup script finds the same six names
here that they find there.
