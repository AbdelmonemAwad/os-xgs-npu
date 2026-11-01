<!-- reviewed: 2026-11-01 -->
# Plan: the front ports assign themselves at the first boot, with a countdown

Written 2026-10-13, to be implemented in the order below. ~~Nothing on this page is in the tree
yet.~~ Implemented on 2026-10-16 - see [What was built](#what-was-built), which says where it
differs from the plan below.

## What was built

`src/etc/rc.syshook.d/early/30-xgs-assign`, `src/opnsense/scripts/xgs/assign.php` and
`src/opnsense/scripts/xgs/countdown.py`, installed on both boards by `install/install.sh`.

- **When.** At a boot that has front ports and no mark in `/conf/os-xgs-npu-assign`, after the
  bring-up hooks and before `rc.bootup`. The installer copies `/conf` to the disk, so the question
  is asked once. A freshly installed appliance meets it at the first boot of its disk: the live boot
  from the stick has no driver yet, so OPNsense's own assignment runs there as it always did.
- **Only on a box nobody has set up.** If one front port is assigned already, the hook marks the
  box and changes nothing - so installing or updating this on an appliance in service is safe.
  This is stricter than rule 1 below, which would have filled the gaps on a box set up by hand.
- **The names.** On the XGS 3300 from the driver, as rule 6 asks, but without a new sysctl: octep
  already names every front port in its interface description from the port's tag ("XGS front port
  Port2"), and OPNsense replaces that only when it assigns the port. Nothing is printed beside the
  XGS 136's sockets, so its ports are numbered as npuep numbers them: npupN is PortN for 1 to 12,
  npup13 and npup14 are F1 and F2. **Since 2026-10-21, every model's:** the names, which ports
  there are and the WAN port come from the model's own table, by the assembly number on its board -
  [front-ports-by-model.md](../families/front-ports-by-model.md); the XGS 3300 and the XGS 136 get
  what they got before.
- **The addresses (2026-10-17, his decision).** The LAN - bridge0, which **is** `lan` on every
  board - is **192.168.1.1/24** with the factory's DHCP range and rules, and the management port,
  where there is one, is an interface of its own named MGMT on **192.168.100.1/24** with a DHCP
  range (.100-.199) and pass rules. So the host NIC an XGS 3300 is installed through answers on
  192.168.100.1 from the first boot from disk on, and 192.168.1.1 is on the front ports.
- **The 3300.** Port2 is WAN; F1, F2, Port1, Port3 to Port8, F3 and F4 are members of bridge0.
  ~~bridge0 is an interface named LAN on 10.0.0.1/24 with a DHCP range and pass rules; the
  installer's `lan` (igb0) stays and is named MGMT.~~ bridge0 is `lan`, and igb0 is MGMT, as above.
- **The 136.** Port1 is WAN; Port2 to Port12, F1 and F2 are members of bridge0, which is `lan` -
  the 136 has no NIC of its own, so there is no MGMT.
- **After a factory reset (2026-11-01, issue 957).** OPNsense's reset empties `/conf`, the mark
  with it, so the next boot assigns the ports again - but its configuration's `lan` names a device
  that is not there, the factory's placeholder, and MGMT used to be taken from `lan`. So after a reset
  the host NIC was left unassigned and MGMT did not come back. Now, when `lan` names no device, the
  OCTEON TX boards' host NIC, igb0, becomes MGMT if nothing has it.
- **Expansion module ports** are front ports like the rest: the driver describes them by the name
  the coprocessor gives them ("XGS front port A1"), so on a box nobody has set up they join the
  bridge with the panel's ports - [expansion-modules.md](expansion-modules.md). On a box in
  service they come up unassigned, as any port does that the box was not set up with.
- **The countdown.** Thirty seconds on `/dev/console`, every number shown; any key stops it and
  writes the mark, and the ports are assigned by hand - the console menu's option 1 or the GUI.

Tested under QEMU, with NICs renamed and described the way each driver leaves its ports (a
test-only hook, not in this tree), as both boards: run out on each, the assignment above and a
boot that never stops at OPNsense's own question, the WAN on DHCP, SSH reached through a bridge
member; a key on the 3300, nothing assigned and the mark written; the boot after, no countdown.
On the second XGS 3300, whose ports were assigned by hand: `--check` says "set up already" and
`config.xml` is byte for byte unchanged. ~~Not yet booted on an appliance with unassigned ports.~~
Booted on both appliances on 2026-10-17 - [On the appliances](#on-the-appliances).

The addresses, tested under QEMU on 2026-10-17 the same way: as an XGS 3300, the console banner
"LAN (bridge0) 192.168.1.1/24, MGMT (em0) 192.168.100.1/24, WAN Port2 DHCP", SSH reached both
through the host NIC at 192.168.100.1 and through a bridge member at 192.168.1.1, a DHCP range on
each (192.168.1.100-199 the factory's, 192.168.100.100-199 added) and MGMT's two pass rules; as an
XGS 136, "LAN (bridge0) 192.168.1.1/24, WAN Port1 DHCP" and no MGMT.

### On the appliances

Booted on 2026-10-17 with main at 381ec6a, on two appliances that had been assigned by hand. On each,
the front ports were first put back to what the installer leaves - the interfaces, the bridges,
dnsmasq's interfaces and ranges and the filter rules taken from a `config.xml` saved before the
ports were assigned, any front port in it dropped, the coprocessor's management link kept, and the
rest of the configuration (certificate, users, SSH, plugins) left as it was - and the mark was
removed. Then one reboot, and no key pressed.

![The two appliances of the table, on the bench the same night: the XGS 136 at the top, the second XGS 3300 in the middle](../images/hardware/bench/three-appliances-2026-10-17.jpg)

The XGS 136 at the top and the second XGS 3300 in the middle - the two of the table - photographed the
same night; the first XGS 3300 below them took no part in this test.

| | XGS 3300 (second unit, 4x10G module in bay A) | XGS 136 |
|---|---|---|
| mark written | 2 min 54 s after the reboot: "assigned by the names on the chassis" | 2 min 02 s after the reboot, the same |
| WAN | Port2 (`oxp3`), a DHCP lease, the default route through it | Port1 (`npup1`), a DHCP lease, the default route through it |
| `lan` | `bridge0`, 192.168.1.1/24, **15 members**: F1, F2, Port1, Port3 to Port8, F3, F4 and the module's A1 to A4 | `bridge0`, 192.168.1.1/24, **13 members**: Port2 to Port12, F1, F2 |
| MGMT | `igb0`, 192.168.100.1/24 | none - no host NIC |
| DHCP | dnsmasq on `lan` (.100-.199) and MGMT (.100-.199) | dnsmasq on `lan` (.100-.199); Kea, which served the old hand-made bridge, off |
| rules | allow LAN to any and allow MGMT to any, IPv4 and IPv6 | allow LAN to any, IPv4 and IPv6 |
| from a PC on a bridge member | a lease in 192.168.1.100-199 on Port4; SSH and the GUI at 192.168.1.1 | a lease in 192.168.1.100-199 on Port3; SSH and the GUI at 192.168.1.1 |
| from a PC on MGMT | a lease in 192.168.100.100-199; SSH and the GUI at 192.168.100.1 | - |
| out through the WAN | DNS resolves, TCP 443 to the internet open | DNS resolves, TCP 443 to the internet open; the coprocessor's management link answers |

The countdown itself was not watched: neither appliance's console was on a serial line during these
boots, so what shows that it ran out rather than being skipped is the mark and the log, which writes
the plan before the countdown and the assignment after it.

## What it does

A machine with OPNsense and this project installed comes up, at its first boot with the driver,
with every front port assigned to an OPNsense interface and named as the panel names it - `Port1`
to `Port8`, `F1` to `F4` on the XGS 3300 - **one port as WAN and all the others joined in one
bridge as LAN**, the layout of the bench box, without anyone at a keyboard. The console shows a
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
2. **Every front port gets an interface named by its label**, enabled, with no address of its own.
3. **Port2 is WAN**, on DHCP, when no WAN is assigned.
4. **Every other front port - Port1, Port3 to Port8, F1 to F4 - is a member of one bridge, and the
   bridge is LAN**: `bridge0`, ~~assigned as an interface named `LAN`, on a private /24 of its own
   apart from the host NIC's - `10.0.0.1/24` is the default written in the script -~~ `lan` itself
   on 192.168.1.1/24 since 2026-10-17, its DHCP server on, and the pass rule OPNsense gives a LAN,
   so a machine on any of the eleven ports gets an address and reaches the GUI.
5. ~~**The host's own NIC stays what the installer made it** - on the bench `igb0`, the `lan` entry
   named MGMT on `192.168.1.1/24`, through which OPNsense and this project were installed - by rule
   1; the bridge is an `optN` entry named LAN beside it, as on the bench.~~ **The host's own NIC
   leaves `lan` for an interface of its own named MGMT on 192.168.100.1/24**, with a DHCP range and
   pass rules (2026-10-17).
6. **The label comes from the driver**, which has the tag, through one sysctl per port; a table per
   board in the test checks the driver, never the other way round.
7. **It runs once**, at the first boot with unassigned front ports, with a 30-second countdown on
   the console; any key stops it. A marker says it ran or was stopped; after that it runs only when
   called by name.
8. **It backs up first and says what it did**: `config.xml` copied before the write, the write
   through OPNsense's `write_config()` (its own backup and revision), one log line per port.
9. **It has a dry run** that prints the plan - device, label, assigned now, to be done - and writes
   nothing.

## The steps, in order

1. **The driver publishes each port's label.** A read-only sysctl `dev.octep.0.port.N.label`
   (`Port1` ... `Port8`, `F1` ... `F4`), derived from the tag and the cage ports' position. Built and
   tested on the appliance like every driver change.
2. **`assign.php`, beside `board.sh` in `src/opnsense/scripts/xgs/`.** PHP, because the assignment lives in `config.xml`
   and `config.inc` owns the lock, the backup and the revision. `--plan` prints the table and
   exits; `--apply` makes the assignment by the rules - the twelve `optN` entries, the `bridged`
   entry with its eleven members, the `LAN` entry on `bridge0` with its address, DHCP range and pass
   rule, the WAN entry on Port2; `--config <file>` works on a copy for the tests; `--names-only`
   makes the twelve named entries and nothing else, for whoever wants another layout by hand.
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
   (b) `--plan` then `--apply` on a copy of that config with the twelve `oxpN` entries, the bridge and
   the LAN entry removed; the result compared with the hand-made assignment label by label - the
   bench bridge has nine members where the rule makes eleven, because F1 and Port8 were taken out
   of it there by hand for the lab, and that difference is the expected one. (c) On the bench box, the twelve
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
