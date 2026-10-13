# Plan: the front ports assign themselves at the first boot, with a countdown

Written 2026-10-13, to be implemented in the order below. Nothing on this page is in the tree yet.

## What it does

A machine with OPNsense and this project installed comes up, at its first boot with the driver,
with every front port assigned to an OPNsense interface and named as the panel names it - `Port1`
to `Port8`, `F1` to `F4` on the XGS 3300 - without anyone at a keyboard. The console shows a
countdown while it waits; **any key pressed during the countdown stops it**, nothing is written,
and the assignment is made by hand the way OPNsense always offers it (the console menu, or the
GUI). Nothing is asked and nothing blocks: the box boots either way.

## What is there today, read on the XGS 3300

- The driver's bring-up runs from `rc.syshook early`, which `/usr/local/etc/rc` calls before
  `rc.bootup`; the twelve `oxpN` devices are up before OPNsense looks for them.
- `rc.bootup` calls `is_interface_mismatch()` and, when the configured devices are missing, loops
  on `set_networking_interfaces_ports(true)` - an interactive assignment on the console that holds
  the boot until someone answers. That loop is OPNsense's manual way, and it stays.
- `xgs_devices()` in `src/etc/inc/plugins.inc.d/xgs.inc` registers the `^oxp[0-9]` class, with no
  `names`, so it invents nothing; `get_interface_list()` lists the ports as hardware.
- The bench box was assigned by hand, port by port: `oxp0` F1, `oxp1` F2, `oxp2` Port1, `oxp3`
  Port2 (WAN), `oxp4` to `oxp9` Port3 to Port8, `oxp10` F3, `oxp11` F4, the host's own NIC `igb0`
  as MGMT, a bridge of nine ports as LAN. That is the reference the automatic result is compared
  with.
- A panel port's tag is `0x8000 | (switch port << 8)` - label 1 is `0x8100`, label 8 `0x8800`, F3
  `0x8900`, F4 `0x8a00` ([octeon-tx.md](../families/octeon-tx.md)); F1 and F2 are the two cage
  ports on the coprocessor's own MAC. The driver holds every tag and publishes no name yet.

## The rules

1. **An assignment that exists is never touched.** The script fills what is unassigned and leaves
   the rest as it finds it, so a box set up by hand does not change when this is installed.
2. **Every front port gets an interface named by its label**, enabled, with no address.
3. **WAN is Port2 if no WAN is assigned; LAN is Port1 if no LAN is assigned**, with OPNsense's own
   LAN defaults (`192.168.1.1/24`, its DHCP server), so the GUI is reachable the way it is on any
   fresh OPNsense. On a box installed through the host's own NIC that NIC is already LAN, and it
   stays LAN by rule 1.
4. **The label comes from the driver**, which has the tag, through one sysctl per port; a table per
   board in the test checks the driver, never the other way round.
5. **It runs once**, at the first boot with unassigned front ports, with a 30-second countdown on
   the console; any key stops it. A marker says it ran or was stopped; after that it runs only when
   called by name.
6. **It backs up first and says what it did**: `config.xml` copied before the write, the write
   through OPNsense's `write_config()` (its own backup and revision), one log line per port.
7. **It has a dry run** that prints the plan - device, label, assigned now, to be done - and writes
   nothing.

## The steps, in order

1. **The driver publishes each port's label.** A read-only sysctl `dev.octep.0.port.N.label`
   (`Port1` ... `Port8`, `F1` ... `F4`), derived from the tag and the cage ports' position. Built and
   tested on the appliance like every driver change.
2. **`assign.php`, beside `board.sh` in `src/opnsense/scripts/xgs/`.** PHP, because the assignment lives in `config.xml`
   and `config.inc` owns the lock, the backup and the revision. `--plan` prints the table and
   exits; `--apply` makes the assignment by the rules; `--config <file>` works on a copy for the
   tests; `--roles none` leaves WAN and LAN to the manual way for whoever wants only the names.
3. **`countdown.py`, in the same directory.** Opens `/dev/console`, prints
   `XGS: assigning the front ports by their panel labels in N s - press any key to do it by hand`
   with N counting down, returns 0 when the time runs out and 1 on a key. Thirty seconds.
4. **`30-xgs-assign`, in `src/etc/rc.syshook.d/early/` beside the bring-up hooks.** After the bring-up hook. On a board `board.sh`
   knows, when a front port is unassigned and `/var/db/os-xgs-npu/assigned` is absent: run the
   countdown; on 0 run `assign.php --apply`, on 1 write nothing; write the marker with which of the
   two happened; log it; exit 0 on every path, so a failure leaves OPNsense's own prompt exactly
   where it was.
5. **The tests, in this order.** (a) `--plan` on the bench box's live config: "nothing to do,
   twelve of twelve assigned", and the labels equal to the descriptions there port for port.
   (b) `--plan` then `--apply` on a copy of that config with the twelve `oxpN` entries removed; the
   result compared with the hand-made assignment label by label. (c) On the bench box, the twelve
   entries removed under a backup and a reboot - once letting the countdown run out, once pressing
   a key - and the backup restored after each. (d) A fresh installation, the first one made after
   this exists.
6. **The XGS 136.** The same script with its labels - 1 to 8 behind its switch, 9 to 12 on the
   AQR412C, F1 and F2 ([xgs136.md](../hardware/xgs136.md)) - once its driver publishes them.
7. **The pages.** This page stays as the design page; the README's "what works" gets one sentence;
   the measurements page gets the boot that assigned itself, with its console log.

## What it never does

Touch an assignment that exists; wait without a countdown; invent a port that is not there; change
how OPNsense assigns interfaces in general. One script, one countdown and one hook, for boards this
project knows, filling what is empty with the names on the panel.
