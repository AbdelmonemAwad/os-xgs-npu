<!-- reviewed: 2026-11-22 -->
# The eMMC of the XGS 87 and FreeBSD's bus test

2026-11-22, from the readings an XGS 87's owner sent in
[#1205](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1205) (and
[#1204](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1204)). There is no XGS 87 in the lab:
everything below about the hardware is that owner's reading, and nothing here has run on one yet.

## The fault

The XGS 87 (an AMD SoC, board AMDA0200) keeps a 16 GB eMMC on the AMD FCH SD controller, PCI
1022:7906 revision 0x01, an SDHCI 2.00 host. OPNsense 26.7 (FreeBSD 15.1) attaches it as `<Generic SD
HCI>`: `sdhci_pci` has no entry for it and takes it by its class alone, with no quirks. The eMMC is
then not usable, and the appliance runs from a USB disk.

A cold boot with `hw.mmc.debug=3` and `hw.sdhci.debug=3` shows how far it gets. The SD probe fails as
it should for an eMMC (CMD8 and CMD55 time out); CMD1 is answered, the card is ready, and CMD2, CMD3,
CMD9, CMD13, CMD7 and the 512-byte EXT_CSD read all succeed. The next command is CMD19,
`MMC_BUSTEST_W`: four bytes written over a 4-bit bus. It never completes. FreeBSD's own timeout ends
it ten seconds later, and the register dump at that moment says why it was waiting:

- Transfer Mode 0x03 (DMA and block count, no read bit) and Block Size 0x7004: a 4-byte write;
- Present State 0x01ff0106: data inhibit, DAT line active, write active - and all four DAT lines high,
  so the card is not holding the bus busy;
- Interrupt Status 0x01: command complete, and nothing else - no transfer complete, no error;
- Clock 0x4007: the 390 kHz identification clock; Host Control 0x03: 4-bit.

The card took the command and the four bytes, and the controller then waited for the end of a write
that never came. After it the card answers nothing: CMD14, the read half of the test, and then the
CMD6 that sets ERASE_GROUP_DEF (EXT_CSD byte 175, four tries) all end in command timeouts, Interrupt
Status 0x18000. `mmc0: Error setting erase group 1`, and the card is dropped. The controller itself
is idle again after FreeBSD's resets - a dump taken after the failure has Present State 0x01ff0000
and nothing pending - so it is the card that stays silent. It stays so until its power is removed:
after a warm reboot it does not even answer CMD1 ("No compatible cards found on bus"), and
`devctl suspend` and `resume` do not bring it back.

Linux - Alpine 3.24, kernel 6.18 - brings the same card up on the same controller without an error,
at 4 bits and 50 MHz high speed. The difference is in how the two find the bus width:

- FreeBSD's `mmc_discover_cards()` runs `mmc_test_bus_width()` on every eMMC of version 4 or later,
  as soon as EXT_CSD is read: CMD19 to write a pattern, CMD14 to read it back. Only after that does
  it set ERASE_GROUP_DEF.
- Linux sends the bus test only where a host asks for it (`MMC_CAP_BUS_WIDTH_TEST`, which no SDHCI
  host but JMicron's sets). Everywhere else it switches the card's width with CMD6, reads EXT_CSD
  again over the new width and compares its read-only fields with the copy read over 1 bit
  (`mmc_compare_ext_csds()`), and it sets ERASE_GROUP_DEF before any of that.

Linux also gives this controller two things of its own (`sdhci-pci-core.c`, `amd_probe()` and
`amd_sdhci_reset()`): on the chipsets before the generation whose SMBus, 1022:790b, is revision 0x51
or later, it clears the Transfer Mode register before every command without data
(`SDHCI_QUIRK2_CLEAR_TRANSFERMODE_REG_BEFORE_CMD`); and for 0x7906 it replaces every full reset with
one through the PCI power states. This XGS 87's SMBus is revision 0x4a, the generation Linux calls
"CZ", so Linux clears Transfer Mode there. The dump of the hang has the register still at 0x03 - the
last data command's mode - when the next command goes out; that the stale mode matters on this chip is
Linux's reading, not one measured here.

## The patch

[contrib/emmc/amd-fch-emmc.patch](../../contrib/emmc/amd-fch-emmc.patch), against FreeBSD 15.1's
`sys/dev/mmc` and `sys/dev/sdhci` - the sources of the OPNsense 26.7 kernel - written the way FreeBSD
takes such a change: a host capability and a controller quirk, both off for every host that does not
ask for them, and one table entry that asks. Change by change:

1. `mmc/bridge.h`: `MMC_CAP_NO_BUS_TEST`, a host capability for a host that cannot take the CMD19/CMD14
   bus test. A capability that a host sets to opt out, like `MMC_CAP_BOOT_NOACC` beside it, so that
   every host that does not set it keeps the test exactly as it is.
2. `mmc/mmc.c`, `mmc_compare_bus_width()`: on such a host the width is found as Linux finds it. The
   widest width the host can do is set on the card with CMD6, the host follows, EXT_CSD is read again,
   and the width is kept only when the seventeen bytes Linux compares - partition support, erased
   memory content, revision, structure, card type, the sleep/awake, write-protect group, erase and
   trim fields, the security features and the four bytes of the sector count - read back as they did
   over 1 bit. A switch the card refuses, a read that fails or a byte that differs leaves the next
   narrower width to try, and 1 bit at the end. Card and host are put back at 1 bit before it returns,
   as `mmc_test_bus_width()` leaves them, because `mmc_calculate_clock()` sets both to the width found
   later and the rest of discovery runs at 1 bit.
3. `mmc/mmc.c`, `mmc_discover_cards()`: the CMD6 timeout and ERASE_GROUP_DEF moved before the width
   is looked for, on every host, as Linux does: while the bus is still the 1-bit one the card came up
   on, and before any command that could leave the card unable to answer. Neither depends on the
   width; ERASE_GROUP_DEF is a command without data.
4. `mmc/mmcreg.h`: the names of the six EXT_CSD bytes the comparison reads that the file did not name
   yet (181, 194, 217, 229, 230, 232).
5. `sdhci/sdhci.h` and `sdhci/sdhci.c`: `SDHCI_QUIRK_CLEAR_TRANSFER_MODE`, the last free quirk bit.
   With it, `sdhci_set_transfer_mode()` writes 0 to Transfer Mode before a command without data instead
   of leaving the last data command's mode there - Linux's quirk. Tuning commands carry data in this
   driver, so they never reach that branch (Linux makes the same exception by name). Off unless a front
   end sets it; `hw.sdhci.quirk_clear` can still take it away.
6. `sdhci/sdhci_pci.c`: an entry for 1022:7906, "AMD FCH SD/eMMC Controller", and
   `sdhci_pci_amd_fch()`, which gives its slots `MMC_CAP_NO_BUS_TEST` and, read the way Linux's
   `amd_probe()` reads it, the quirk: where a Hudson-2 SMBus (1022:780b) is present, or a 1022:790b
   SMBus below revision 0x51. With `bootverbose` it says which it set.

Left out, on purpose: Linux's reset of 0x7906 through the power states. In this owner's readings the
controller is idle again after FreeBSD's own resets; it is the card that stays silent after the bus
test, and with no bus test the card is not put in that state. If a reading of the patched driver shows
the controller stuck, the reset comes next. It would not help a card already left silent by a boot
of the unpatched driver, which only a power-off brings back - a warm reboot from such a boot is not
a test of the patch.

The patch is meant to be offered to FreeBSD as it stands.

## How it reaches an XGS 87 - not as modules of their own

The plan was to build `mmc.ko`, `sdhci.ko` and `sdhci_pci.ko` from the patched sources for the running
kernel, as the plugin builds its own drivers (`install/kernel-follow.sh`), and have the loader take
them instead of `/boot/kernel`'s. That cannot work on this kernel: OPNsense 26.7's `/boot/kernel/kernel`
has these drivers compiled in. `kldstat -v` on OPNsense 26.7 appliances - 15.1-RELEASE-p1 and -p6,
the kernel of #1205 - lists `pci/sdhci_pci`, `sdhci_pci/mmc`, `acpi/sdhci_acpi`, `sdhci_acpi/mmc` and
`mmc/mmcsd` among the modules of `/boot/kernel/kernel` itself. A module that declares `mmc` or `sdhci` again is
refused by the kernel's linker as already present; the `.ko` files in `/boot/kernel` are built
whether the kernel uses them or not.

So the patch reaches an appliance only in a kernel built with it, or through a driver of the plugin's
own that takes 1022:7906 before the generic match does. Which, is not decided yet; nothing of either
is in this tree.

## What is tested, and what is not

Tested here, by [tools/test-emmc-patch.py](../../tools/test-emmc-patch.py):

- that the patch applies to a copy of FreeBSD 15.1's `sys/dev/mmc` and `sys/dev/sdhci` with no fuzz
  and no offset, and comes off again leaving them as they were (where those sources are given; a
  GitHub runner has none, and skips it);
- `mmc_compare_bus_width()` and `sdhci_pci_amd_fch()` cut out of the patch, compiled with stand-ins,
  every warning fatal, and run: 4 bits kept when EXT_CSD reads back the same, 1 bit when it reads back
  changed, when the card refuses the switch or when the read fails, 8 bits tried first, nothing sent on
  a 1-bit host, card and host always handed back at 1 bit; the quirk for SMBus revision 0x4a and for a
  Hudson-2, none for 0x51 and later or for no known SMBus, and the capability always;
- `sdhci_set_transfer_mode()` cut out of the patched `sdhci.c`: 0 written before a command without data
  with the quirk, nothing without it, and a command with data set as before;
- that the comparison covers the bytes Linux compares, and the order discovery now sends its commands in.

Not tested: the whole of `mmc.c`, `sdhci.c` and `sdhci_pci.c` compiled - that needs a FreeBSD 15.1
kernel tree; any build of them; and any run on an XGS 87, where everything above about the card is
still to be seen.
