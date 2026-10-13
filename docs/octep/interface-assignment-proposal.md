# Proposal: the front ports assign themselves, by their panel labels

**Status: a proposal, 2026-10-13, to be reviewed by the owner before anything is implemented.**
Nothing on this page exists in the tree yet. It is written from what the boot path, the plugin and
the driver do today, read on the XGS 3300, and the steps are in the order they would be taken.

## The ask

A machine that gets OPNsense and this project installed should come up with its front ports already
assigned to OPNsense interfaces, named as the panel names them - `Port1` to `Port8`, `F1` to `F4` on
the XGS 3300 - for everyone who installs it, not by hand on each box. The XGS 3300 first, because it
is the appliance on the bench; the XGS 136 after, on the owner's word.

## What is there today

- **The devices exist before OPNsense looks for them.** The driver's bring-up runs from
  `rc.syshook early`, which `/usr/local/etc/rc` calls before `rc.bootup`; by the time `rc.bootup`
  reaches its own check the twelve `oxpN` devices are up.
- **OPNsense's own answer to an unassigned box is a console prompt.** `rc.bootup` calls
  `is_interface_mismatch()` and, when the configured devices are not there, loops on
  `set_networking_interfaces_ports(true)` - an interactive assignment on the serial console that
  blocks the boot until someone answers. On an appliance with the console in a cupboard that is a
  box that never comes up.
- **The plugin already tells OPNsense what an `oxpN` is.** `xgs_devices()` in
  `src/etc/inc/plugins.inc.d/xgs.inc` registers the `^oxp[0-9]` pattern as a device class (type
  `xgs`, not spoofable); it carries no `names`, deliberately, so it invents nothing and changes no
  assignment. `get_interface_list()` lists the ports as real hardware.
- **The owner's own assignment on the bench is the reference.** On his XGS 3300: `oxp0` F1, `oxp1`
  F2, `oxp2` Port1, `oxp3` Port2 (WAN), `oxp4` to `oxp9` Port3 to Port8, `oxp10` F3, `oxp11` F4; the
  management NIC `igb0` as MGMT; a bridge of nine of them as LAN. Made by hand, port by port.
- **The label is known to the driver.** A panel port's tag is `0x8000 | (switch port << 8)`: label 1
  is `0x8100`, label 8 `0x8800`, F3 `0x8900`, F4 `0x8a00`
  ([octeon-tx.md](../families/octeon-tx.md)); F1 and F2 are the two cage ports on the coprocessor's
  own MAC. The driver has every tag; it publishes none of them as a name.

## Rules the thing would obey

1. **It never touches an assignment that exists.** It fills what is unassigned and leaves the rest
   as it finds it, so a box set up by hand - the owner's - is not changed by installing it.
2. **It runs once, at the first boot after installation**, before `rc.bootup` reaches its console
   prompt, and never prompts itself. A marker file says it has run; after that it does nothing
   unless asked by name.
3. **It names by the panel, from one source.** The label comes from the driver, not from a table
   kept in step with it by hand; a table per board is the check, not the source.
4. **It backs up first and says what it did.** `config.xml` is copied before the write, the write
   goes through OPNsense's own `write_config()` (which keeps its own backup and revision), and the
   log names every port it assigned.
5. **It has a dry run** that prints the plan - device, label, what is assigned now, what would be
   done - and writes nothing. The dry run is what gets run on the bench first.

## The steps, in order

1. **The driver publishes each port's panel label.** A read-only sysctl per port,
   `dev.octep.0.port.N.label` (`Port1`, `F3`, ...), derived from the port's tag and the cage
   ports' position - nothing new is learned, the tag is already there. A driver change, built and
   tested on the appliance, committed on the owner's word as every driver change is. (The
   alternative is a table in the plugin keyed on the board's assembly number, `AMDA0202` for the
   3300; it is listed under the open questions.)
2. **A script that plans and applies.** `src/opnsense/scripts/xgs/assign.php`, PHP because the
   assignment lives in `config.xml` and OPNsense's `config.inc` owns the lock, the backup and the
   revision: `--plan` prints the table and exits; `--apply` fills the unassigned front ports as
   `optN` entries with `descr` = the label and `enable` set, no address; `--config <file>` works on
   a copy instead of `/conf/config.xml`, for testing off the box; `--roles` is below.
3. **The roles.** What becomes LAN and WAN is one of three choices, and the choice is the owner's
   (open question 1): *none* - every port an `optN` by its label and the roles left to the person
   at the console, which is what OPNsense's own prompt would offer; *opnsense* - Port1 as LAN with
   OPNsense's default `192.168.1.1/24`, Port2 as WAN on DHCP, the rest by label; *appliance* - the
   same with LAN a bridge of Port1 to Port8, which is how the appliance left the factory. The
   default is written down once chosen and the others stay as flags.
4. **The hook.** `src/etc/rc.syshook.d/early/30-xgs-assign`: after the bring-up hook, only on a
   board `board.sh` recognises, only when no front port is assigned yet and the marker
   `/var/db/os-xgs-npu/assigned` is absent; it runs `assign.php --apply`, writes the marker, logs.
   Every path exits 0: a failed assignment leaves the console prompt where it was, which is the
   behaviour that was always there.
5. **The test, on the bench, in this order.** (a) `--plan` against the live config of the owner's
   3300: expected "nothing to do, twelve of twelve assigned", and the table's labels equal to his
   descriptions port for port. (b) `--plan` and `--apply` against a copy of that `config.xml` with
   the twelve `oxpN` entries removed: the result compared with his hand-made assignment, label by
   label. (c) With the owner present: the twelve entries removed from the live config under a
   backup, a reboot, the hook assigning them, and the backup restored after. (d) The real case, a
   fresh installation, when one is next made.
6. **The 136.** The same script with its labels - 1 to 8 behind its switch, 9 to 12 on the AQR412C,
   F1 and F2 ([xgs136.md](../hardware/xgs136.md)) - once its driver publishes them; deferred.
7. **The pages.** This page becomes the design page when the thing exists; the README's "what works"
   gets one sentence; the measurements page gets the boot that assigned itself, with its log.

## Open questions, for the owner

1. **The roles' default**: none, opnsense, or appliance (step 3)?
2. **Label from the driver or from a table?** The driver is the one source and costs a driver
   change; a table is cheaper and goes stale.
3. **Once, or at every boot?** Once with a marker is proposed; "at every boot, fill the gaps" would
   also assign a port the owner deliberately left unassigned.
4. **The management NIC.** `igb0` is the host's own NIC, not a front port; the proposal leaves it
   alone (OPNsense's prompt would offer it). Should it be named MGMT by default, as on the bench?

## What this is not

Not a GUI, not an API, not a change to how OPNsense assigns interfaces in general: one script and
one hook, for boards this project knows, filling what is empty with the names on the panel.
