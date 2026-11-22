<!-- reviewed: 2026-11-22 -->
# The eMMC of the XGS 87 and FreeBSD's bus test

2026-11-22, from the readings an XGS 87's owner sent in
[#1205](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1205) (and
[#1204](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1204)). There is no XGS 87 in the lab:
everything below about the hardware is that owner's reading, ~~and nothing here has run on one yet~~ and the
driver has run on that owner's XGS 87 since (the end of this page). Two things come of it: a patch
for FreeBSD's own drivers, to offer upstream, and a driver of the plugin's own, `fchemmc`, that
reaches an XGS 87 with no new kernel.

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

The card answered the command; the write did not complete, and the controller waited for the end
of it. After it the card answers nothing: CMD14, the read half of the test, and then the CMD6 that
sets ERASE_GROUP_DEF (EXT_CSD byte 175, four tries) all end in command timeouts, Interrupt Status
0x18000. `mmc0: Error setting erase group 1`, and the card is dropped. A dump taken right after the
failure has Present State 0x01ff0000 and nothing pending, so FreeBSD's resets leave the controller
idle - but not whole: after `devctl suspend` and `resume` it prints "Cannot issue a command
(power=21 clock=390625)" and times out with Present State 0x01ff0001, the command inhibit still set,
a controller FreeBSD's own resets did not bring back. And the card stays silent until its power is
removed: after a warm reboot it does not even answer CMD1 ("No compatible cards found on bus").
Under Linux the same controller comes back without an error from every full reset, each one through
the PCI power states (below).

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
(`SDHCI_QUIRK2_CLEAR_TRANSFERMODE_REG_BEFORE_CMD`); and for 0x7906 it puts a pass through the PCI
power states before every full reset, the hard reset its commit says this controller needs to clear
all of its internal state. This XGS 87's SMBus is revision 0x4a, the generation Linux calls "CZ", so Linux
clears Transfer Mode there. The dump taken after the failure has the register at 0x13 - the read mode
of CMD14, the last command with data - after the CMD6s without data had gone out with it still set;
that the stale mode matters on this chip is Linux's reading, not one measured here.

## The patch

[contrib/emmc/amd-fch-emmc.patch](../../contrib/emmc/amd-fch-emmc.patch), against FreeBSD 15.1's
`sys/dev/mmc` and `sys/dev/sdhci` - the sources of the OPNsense 26.7 kernel - written the way FreeBSD
takes such a change: a host capability, a controller quirk and a reset for one controller, and one
table entry that asks for them. A host or controller that does not ask sends the same commands in the
same order as before. Change by change:

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
3. `mmc/mmc.c`, `mmc_discover_cards()`: on a host with `MMC_CAP_NO_BUS_TEST`, ERASE_GROUP_DEF is set
   before the width is looked for, as Linux does: while the bus is still the 1-bit one the card came up
   on. Every other host sets it where it always did, after the bus test and the speeds. The code that
   sets it is the one discovery always had, moved into `mmc_set_erase_group()` so that both places call
   it. The CMD6 timeout is worked out before the width search, since the comparison's CMD6 needs it;
   that is a sum, and sends nothing.
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
7. `sdhci/sdhci_pci.c`, `sdhci_pci_reset()`: the front end's own `sdhci_reset` method, as
   `sdhci_fsl_fdt.c` has one. A full reset of 1022:7906 first takes the controller to D3hot and back
   to D0, as Linux's `amd_sdhci_reset()` does with D3cold: configuration space saved, bus mastering
   off, D3hot, 10 ms,
   D0, 10 ms, configuration space restored, then up to 100 ms in 10 ms steps for Present State's
   card-detect-stable bit, then the generic reset. The power state is written to the power management
   register itself, not with `pci_set_powerstate()`: the method runs with the slot's mutex held, and
   `pci_set_powerstate()` can go through ACPI, which may sleep. Every other controller, a 7906 without
   power management, and every partial reset get the generic reset alone, as before.

What the reset cannot do: bring back a card already left silent by a boot of the unpatched driver,
which only a power-off brings back - a warm reboot from such a boot is not a test of the patch.

The patch is meant to be offered to FreeBSD as it stands, once it has been read and has run on an
XGS 87. Nothing has been sent.

## Why a driver of its own, not the patched drivers

The first plan was to build `mmc.ko`, `sdhci.ko` and `sdhci_pci.ko` from the patched sources for the
running kernel, as the plugin builds its own drivers (`install/kernel-follow.sh`), and have the loader
take them instead of `/boot/kernel`'s. That cannot work on this kernel: OPNsense 26.7's
`/boot/kernel/kernel` has these drivers compiled in. `kldstat -v` on OPNsense 26.7 appliances -
15.1-RELEASE-p1 and -p6, the kernel of #1205 - lists `pci/sdhci_pci`, `sdhci_pci/mmc`,
`acpi/sdhci_acpi`, `sdhci_acpi/mmc` and `mmc/mmcsd` among the modules of `/boot/kernel/kernel`
itself. A module that declares `mmc` or `sdhci` again is refused by the kernel's linker as already
present; the `.ko` files in `/boot/kernel` are built whether the kernel uses them or not.

What a module can do is take the controller before `sdhci_pci` does. `sdhci_pci_probe()` has no entry
for 1022:7906 and takes it by its class alone, at `BUS_PROBE_GENERIC`. A driver that matches the id
and returns `BUS_PROBE_DEFAULT` wins that probe, if it is there when the PCI bus is probed - loaded by
the loader. Such a driver needs nothing the kernel does not already have: `sdhci_init_slot()`,
`sdhci_start_slot()` and the `sdhci_generic_*` methods are the kernel's own sdhci core, which
`sdhci_pci` is built on too, and `mmc_switch()` and `mmc_send_ext_csd()` are mmc(4)'s own. It declares
nothing the kernel has: its modules are `pci/fchemmc` and `fchemmc/mmc`. So what the patch changes in
`mmc.c` and `sdhci.c` - the bus test and Transfer Mode - the driver does in the front end, where a
front end can.

## The driver: contrib/fchemmc

[contrib/fchemmc/fchemmc.c](../../contrib/fchemmc/fchemmc.c), BSD-licensed, adapted from FreeBSD's
`sdhci_pci.c` with that file's copyright notice kept. It is `sdhci_pci` for one controller, and
differs from it in these:

1. Probe: 1022:7906 alone, by its id, at `BUS_PROBE_DEFAULT`, as "AMD FCH SD/eMMC Controller". Every
   other controller is left to `sdhci_pci` as before.
2. The bus test (`fchemmc_request()`, `fchemmc_bus_test()`). Every request of mmc(4) passes through
   the driver's own `mmcbr_request` method. mmc(4) still runs `mmc_test_bus_width()`, which nothing
   can tell it not to, but CMD19 never reaches the controller: the driver completes it at once,
   without an error, and `mmc_test_bus_width()` does not look at its result. CMD14 is answered the
   way Linux checks a width: the card is switched to 4 bits with CMD6 (`mmc_switch()`, polled with
   CMD13 until the card is back in transfer state), EXT_CSD is read again (`mmc_send_ext_csd()`), and
   the seventeen bytes Linux compares are compared with the copy mmc(4) read over 1 bit just before.
   Then the card is switched back to 1 bit, as the bus test leaves it, whatever the result. Only when
   they match does CMD14 return 0xA5 0x00 0x00 0x00, the pattern `mmc_test_bus_width()` takes for a
   working 4-bit bus; otherwise it fails, and mmc(4) stays at 1 bit. The 1-bit copy is taken on its
   way back from the controller - the 512-byte CMD8 read while the host is at 1 bit - and the card's
   address from CMD7; CMD0 forgets both. These requests go through mmc(4) and back into the same
   method, from mmc(4)'s CMD14, where mmc(4) holds no lock and may sleep. mmc(4) then sets
   ERASE_GROUP_DEF and picks the width and the clock as it always does, now with a card that answers.
3. Transfer Mode (`fchemmc_write_2()`): when the command register is written for a command without
   data (no `SDHCI_CMD_DATA`), Transfer Mode is written 0 first - where a Hudson-2 SMBus (1022:780b)
   is present, or a 1022:790b SMBus below revision 0x51 (`fchemmc_needs_clear_tm()`, read as Linux's
   `amd_probe()` reads it). This XGS 87's is 0x4a. Tuning commands carry data, so they never come
   there. `dev.fchemmc.0.clear_transfer_mode` says whether it is on.
4. The reset (`fchemmc_reset()`): a full reset goes through D3hot and back to D0 before the generic
   reset, as change 7 of the patch above; the partial resets get the generic reset alone. This is
   D3hot, not the D3cold Linux asks for: Linux's `pci_set_power_state(pdev, PCI_D3cold)` can cut the
   power through ACPI where the platform has it, and a write to the power management register cannot.
   Whether D3hot clears the controller's state depends on its No_Soft_Reset bit, which nobody has read
   on an XGS 87; `dev.fchemmc.0.no_soft_reset` shows it (0: D3hot resets it). The configuration space
   is restored only once D0 reads back, so that `pci_restore_state()` never calls
   `pci_set_powerstate()` under the slot's mutex; if D0 does not read back, the driver says so,
   restores nothing, and the device's `d0_failed` sysctl counts it (`d3_cycles` counts the cycles). sdhci
   asks for a full reset at attach, detach, suspend and resume only. Its command and data timeouts
   reset CMD and DATA, so this cycle does not recover a stuck card while the machine runs.
5. The slot (`fchemmc_limit_slot()`): 4 bits and never 8, no DDR52, HS200, HS400 or UHS modes, no
   1.8 V or 1.2 V signaling, and 50 MHz at most, set after `sdhci_init_slot()` has read the
   capabilities and before the slot is started. That is how Linux runs this card: 4 bits, 50 MHz,
   high speed. An SDHCI 2.00 host offers none of the faster modes anyway.
6. `hw.fchemmc.disable=1`, a loader tunable, leaves the controller to `sdhci_pci` with no rebuild: in
   `/boot/loader.conf.local`, or at the loader prompt as `set hw.fchemmc.disable=1`. And
   `fchemmc_load="NO"` in `/boot/loader.conf.local`, which the loader reads after
   `/boot/loader.conf.d`, keeps it from being loaded at all.
7. The kernel it is for. The loader loads the module before any script can compare its stamp with the
   kernel, and a module built for another kernel of the same branch loads without a complaint. So the
   module carries the version line of the kernel it was built for (`fchemmc_kernel.h`, which its
   Makefile writes from `FCHEMMC_KERNEL`) and does not probe on any other kernel: it prints why and
   leaves the controller to `sdhci_pci`.

`dev.fchemmc.0.bus_test` says what the bus test found: 4 when EXT_CSD read back the same over 4 bits,
1 when it did not or could not be run, 0 before it has run.

What the driver does not do that the patch does: set ERASE_GROUP_DEF before the width search. That
order is mmc(4)'s own, in `mmc_discover_cards()`, and only a patched `mmc.c` can change it. With CMD19
never sent, the card is no longer silent when mmc(4) sends it, after the bus test, as it always has.

## How the plugin delivers it

- `install/install.sh` lays the sources down in `/usr/local/share/os-xgs-npu/fchemmc` only where
  `pciconf -l` lists a 1022:7906 controller, and takes every file of the driver away on any other
  machine.
- `install/kernel-follow.sh` builds `fchemmc.ko` after the module it follows (`npuep.ko` on the
  XGS 87), against the same kernel sources and for the same kernel, at an install and at every kernel
  the follower follows: in `/var/db/os-xgs-npu/build-fchemmc`, installed as `/boot/modules/fchemmc.ko`
  with a stamp beside it for the follower, and `/boot/loader.conf.d/fchemmc.conf`
  (`fchemmc_load="YES"`) written beside the module, never without one. Where the eMMC holds data
  only, it is not critical. A build that fails, or sources that are not here, are said once for that
  kernel; the next boot is not sent to `kernel.old` for it; and a boot of a kernel it was not rebuilt
  for leaves the eMMC to `sdhci_pci` (item 7). On this controller that means the stock bus test: the
  card is silent until the power is removed, as it is today. No earlier `fchemmc.ko` is kept, so a boot
  back into the old kernel after a new one was staged is such a boot. Where the system disk is the
  eMMC, all of this is different: see [the eMMC as the system disk](#the-emmc-as-the-system-disk).
- `install/uninstall.sh` removes the module, its stamp, the loader line and the build directory; the
  sources go with `/usr/local/share/os-xgs-npu`. A loaded fchemmc stays until the next reboot.

~~Root on the eMMC is not a part of this; the eMMC is for data until a later step.~~ Since
2026-11-22 the system can be installed onto the eMMC from the XGS image (below).

## The eMMC as the system disk

With the system disk on the eMMC, a kernel booted without an `fchemmc.ko` built for it has no root
disk, and the appliance does not boot at all. Nothing below changes anything for an appliance whose
system is anywhere else - every other model, and an XGS 87 that stays on a USB disk.

**Which disk the system is on.** `src/opnsense/scripts/xgs/system-disk.sh` follows the root
filesystem down to its disk: the device from `mount -p` (OPNsense's installer mounts it by a label -
`/dev/gpt/rootfs` for UFS, `opnsense-ufs.sh` in opnsense/installer - or as the pool `zroot` for ZFS,
whose devices `zpool list -vHP` names), then down `kern.geom.conftxt`, where every provider is printed
after the one it is built on, one level deeper. `-e` answers 0 only where that disk is an `mmcsd`; a
root it cannot follow to a disk counts as not the eMMC. In a plain OPNsense 26.7 VM it printed `ada0`
for a root on the label `ufs/OPNsense_Nano` and answered 1.

**Where the builds live, and why in the kernel's own directory.** The loader, not a boot hook, loads
fchemmc. It looks in the booted kernel's own directory first: `config.loadKernel()` in
`stand/lua/config.lua` sets `module_path` to `/boot/<kernel>;` and the rest, and `mod_search_hints()`
in `stand/common/module.c` falls back to the file name `fchemmc.ko` in each directory whose
`linker.hints` does not name the module. A kernel update moves the whole directory: `install_kernel()`
in opnsense-update (opnsense/update, `src/update/opnsense-update.sh.in`) unpacks the new kernel into
`/boot/kernel.new`, removes `/boot/kernel.old`, renames `/boot/kernel` to `/boot/kernel.old` and
`kernel.new` to `kernel`. So a build kept as `/boot/kernel.old/fchemmc.ko` - or as
`/boot/kernel/fchemmc.ko` just before an upgrade's rename - goes with the kernel it was built for, and
the next kernel update removes it with that kernel. `/boot/modules` keeps one build, for the kernel to
follow, as before. A build left in `/boot/kernel` is taken out again once `/boot/modules` has the same
one: opnsense-update runs `kldxref /boot/kernel` at every base update, and the health check compares
`/boot/kernel` with the kernel set's own list. Tested in a plain OPNsense 26.7 VM, which has no
1022:7906 controller: with a build for `kernel.old` in `/boot/kernel.old` and one for `/boot/kernel` in
`/boot/modules`, a boot sent to `kernel.old` by `nextboot -k kernel.old` loaded
`/boot/kernel.old/fchemmc.ko` (`kldstat -v`), and the boot after it loaded `/boot/modules/fchemmc.ko`.

**What the kernel follower does there** (`install/kernel-follow.sh`, "the eMMC as the system disk"):

- the next boot is never sent to a kernel the loader would find no matching fchemmc for, while
  `kernel.old` has one: the fall-back to `kernel.old` (`nextboot -k kernel.old`) is armed as it is for
  npuep - before a rebuild, again at every boot, and at every orderly reboot by the stop hook - and let
  go only once fchemmc is built for `/boot/kernel`, never for npuep alone;
- the fall-back is never armed for npuep where `kernel.old` would find no fchemmc: a boot without its
  front ports is still a boot, one without its system disk is not;
- the build that is replaced is kept, for the kernel it was built for, in that kernel's own directory;
- an upgrade to a new major release, whose kernel is a staged set that OPNsense's `upgrade.sh` applies
  only when every upgrade hook succeeded, is stopped by the upgrade hook where fchemmc could not be
  built for that kernel (`EMMCCHECK=1`): the upgrade says why, applies nothing, and can be run again.

`install/uninstall.sh` leaves fchemmc, its loader line, the kept builds and a fall-back to
`kernel.old` where the system disk is the eMMC, and says that nothing builds it for a new kernel once
the plugin is gone.

One window is left: between an update installing a new kernel and the follower's next run (its
five-minute tick, or the stop hook at the reboot the update asks for), a power cut leaves the next boot
on the new kernel without its fchemmc. The boot menu's **Kernel** entry (`k`) then picks `kernel.old`,
which has one.

**In the XGS image.** The image is made on a build machine with no XGS 87, and its first boot is too
late for the plugin to build fchemmc: `install.sh` runs from the image's first-boot hook after the
kernel has probed the bus, and a warm reboot does not bring the card back. So the image build itself
runs `install/image-fchemmc.sh put <image root> [<sys>]`: fchemmc built for the image's own kernel
(its version line read out of `/boot/kernel/kernel`), against that kernel's sources - the archive the
image already carries in `/usr/local/share/os-xgs-npu/kernel-sources` when no `<sys>` is given - with
the make and cc of a FreeBSD of the image's own major release (refused on any other). It puts in
`/boot/modules/fchemmc.ko` with its stamp, `/boot/loader.conf.d/fchemmc.conf`, the sources, and a
mark, `/usr/local/share/os-xgs-npu/fchemmc.image`; a step that fails takes all of it away. The live
system of an XGS 87 then boots with fchemmc, and its installer offers `mmcsd0`: the UFS installer
lists every `/dev/<letters><digits>` that `diskinfo` reads (`opnsense_load_disks()`,
opnsense/installer), the ZFS one every GEOM disk (`f_device_find`, bsdconfig's `device.subr`), and the
copy of the live system takes `/boot` whole and every directory of `/usr/local`
(`opnsense-install.sh`, cpdup). At the first boot `install.sh` runs `image-fchemmc.sh settle` before
anything else: where a 1022:7906 is on the bus of a board this project installs on, the plugin takes
the module over (the mark goes); anywhere else everything the image put in is taken away, so after
its first boot an appliance of any other model is as it would have been without it. A loaded fchemmc
stays until the next boot, attached to nothing. In the same VM, `put` built `fchemmc.ko` for the VM's
own kernel against its sources; the module loaded with `kldload` and by the loader at the next boot,
with no message and no device attached; and `settle` then left the made-up image root as it was
before `put`.

## What is tested, and what is not

Run here:

- [tools/test-fchemmc.py](../../tools/test-fchemmc.py): `fchemmc.c` compiled whole with stand-ins for
  the kernel, every warning fatal, and run. The probe takes 1022:7906 alone, at `BUS_PROBE_DEFAULT`,
  and nothing with `hw.fchemmc.disable=1` or on a kernel whose version line is not the one it was built
  for. CMD19 never reaches the controller. CMD14 returns the pattern only when EXT_CSD reads back the
  same over 4 bits, and fails when any one of the seventeen bytes differs, when the card refuses the
  switch, when the switch times out after the card took it, when the read fails, with no 1-bit copy,
  after a CMD0, or with the host not at 4 bits; the card is switched back to 1 bit after every
  attempt to switch it, and the switch time follows
  GEN_CMD6_TIME. The 1-bit copy is taken only from a 512-byte read at 1 bit, and the request's own
  completion is always given back. Every other command goes to the controller unchanged. Transfer Mode
  is written 0 only before a command without data and only with the quirk; the quirk is set for
  SMBus revisions 0x4a and 0x50 and for a Hudson-2, and not for 0x51, 0x61 or no known SMBus. A full
  reset goes through D3hot and back, keeps the No_Soft_Reset bit, waits at most ten 10 ms steps for
  card detect and then resets; when D0 never reads back it restores nothing, says so and counts it; a
  partial reset, or a controller without power management, gets the generic reset alone. The slot is
  started at 4 bits and 50 MHz at most, with high speed and without DDR52, HS200, HS400, UHS or 1.8 V.
  Then the source: the notices kept from `sdhci_pci.c`, no match by class, the methods, the tunable,
  the module declarations and the Makefile. And the wiring: `install.sh` and `uninstall.sh` read, and
  the kernel follower run with stand-ins for make, the fetcher, logger and sysctl - `fchemmc.ko` built
  after the module it follows against the same sources, given the kernel's version line, stamped,
  rebuilt for a new kernel, built at a tick when only it is missing, said once and not built when the
  sources are missing, a failed build not tried again until an install, the loader line only beside a
  module, and nothing at all where its sources are not laid down. A fault put into the driver on
  purpose - CMD19 sent on, the quirk set for revision 0x51, the configuration restored when D0 did
  not read back, or no switch back after a failed switch to 4 bits - is caught.
- [tools/test-emmc-root.py](../../tools/test-emmc-root.py), for the eMMC as the system disk:
  `system-disk.sh` given what `mount -p`, `kern.geom.conftxt` and `zpool list -vHP` print (UFS on
  `gpt/rootfs` and on the partition itself on the eMMC, UFS on a USB disk, ZFS on an NVMe label and on
  an encrypted eMMC partition, a root on no disk); the kernel follower and the upgrade hook on a made-up
  `/boot`, each run asked what the loader would load for the kernel the next boot takes - after an
  update with fchemmc built, with its build failing and at the boots after, with a build cut off, at
  the stop hook's reboot, with no fchemmc for either kernel, at an upgrade with its kernel staged with
  and without a matching build, and every one of them with the system disk elsewhere, where nothing is
  kept in a kernel's directory and npuep alone decides as before; and `image-fchemmc.sh`: what `put`
  lays into an image root, from a given `sys` and from the archive the image carries, and nothing on a
  failed build, on machine headers from another tree, on a build machine of another major release or
  for a kernel without a version line; and what `settle` leaves on an XGS 87, on a board without the
  controller, on a board this project does not install on, and where the system disk is the eMMC.
- [tools/test-emmc-patch.py](../../tools/test-emmc-patch.py), for the patch: it applies to a copy of
  FreeBSD 15.1's `sys/dev/mmc` and `sys/dev/sdhci` with no fuzz and no offset and comes off again
  (where those sources are given; a GitHub runner has none and skips it); `mmc_compare_bus_width()`,
  `mmc_set_erase_group()`, `sdhci_pci_amd_fch()`, `sdhci_pci_reset()` and the patched
  `sdhci_set_transfer_mode()` compiled with stand-ins and run; the bytes compared; and the order
  discovery sends its commands in.
- A compile, and only that: `contrib/fchemmc` built with `make` against the kernel sources of
  OPNsense 26.7 (FreeBSD 15.1-RELEASE-p6) that the kernel follower had fetched on the lab's second
  XGS 3300, with the kernel's own flags and `-Werror`: no warning and no error. Every symbol the module
  takes from the kernel is in that kernel's symbol table. The version line the follower reads from
  that kernel file is, byte for byte, the start of the running kernel's `kern.version`, followed by
  one newline and nothing else, which is what the module's match takes. That XGS 3300 has no
  1022:7906 controller; the module was not loaded there or anywhere, and the build was removed.

~~Not run: anything on an XGS 87. Only one can show that the loader loads the module and that it
wins the probe; that the card answers after the bus test is answered; that EXT_CSD reads back the same
over 4 bits, so that mmc(4) runs the card at 4 bits; that the clock reaches 50 MHz in high speed;
whether the controller's No_Soft_Reset bit is clear, so that D3hot resets it as Linux's D3cold does,
and that the reset through D3hot brings the controller back to D0; and that mmcsd0 reads and
writes.~~ An owner's XGS 87 has run the driver from `main` since 2026-11-22, with its system on a USB
disk: `mmcsd0` at 50 MHz and 4 bits, no errors, at cold and warm boots. Not run on an XGS 87: the
system installed onto its eMMC from an image carrying fchemmc, the BIOS starting it from there, and a
kernel update or an upgrade with the system on it; and the image's live system with fchemmc built in.
The whole of the patched `mmc.c`, `sdhci.c` and `sdhci_pci.c` has not been compiled either; the patch is for FreeBSD,
not for an appliance.
