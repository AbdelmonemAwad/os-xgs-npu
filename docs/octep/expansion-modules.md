# Expansion modules: every port the coprocessor reports, named as the coprocessor names it

Since 2026-10-16 the XGS 3300's front-bay module ports come up as interfaces of their own, named as
the metal names them - `A1` to `A4` for a 4x10G module in bay A - after the panel's twelve. Nothing
in the code names a module: the ports are read from the coprocessor at every boot, so a module that
nobody here has seen binds the same way. This page says where each fact comes from, what was
measured, and what is not tested yet.

## The bay belongs to the coprocessor

The front bay with the PCIe-like edge is the coprocessor's slot, not the host's -
[hardware/xgs3300.md](../hardware/xgs3300.md#the-expansion-bay-belongs-to-the-coprocessor). The
board file names how its presence and its identity are read, on the coprocessor:

```
npu0.slotA.presdet = cpld:8:20      presence: CPLD register 0x08, bit 20
npu0.slotA.vpd     = i2c1:1:0x50    the module's VPD EEPROM, on the coprocessor's I2C bus 1
```

A module is an assembly of its own, with its own CPLD and its own VPD, and the vendor's board files
describe it as a **template**: a module file (`AMDA0207-0001R00.txt`, "phymod 4x10GBaseSR (SFP+)")
names its ports `%c4` to `%c1` and leaves their numbers to be computed, and at every boot the
coprocessor's `xgs-platform-init` folds the template into the board's own file under the bay's
letter. The result is the coprocessor's platform database, and `xgs-platform` - the vendor's read
tool, on the coprocessor - prints it.

## Three sources, joined by address

| source | what it knows | how the host reads it |
|---|---|---|
| the platform database | every port's **name** on the metal, its **lifport** (the fast path's logical interface), its speed, its MAC; each bay's module part number | `src/opnsense/scripts/octep/npu-platform.sh`, through the coprocessor's console (`/dev/cuau2`), with `xgs-platform` |
| NetAgent's discover | every port's **tag** - the handle the host binds and every frame carries | `dev.octep.0.nwa.discover`, then `dev.octep.0.nwa.ports` |
| NetAgent's MAC attribute | the MAC of a tag | `nwa.op=4 nwa.sub=3 nwa.port=<tag>` |

Neither side knows what the other does - NetAgent has no names and the platform database has no
tags - but both carry the port's address, so the address is the join:
`src/opnsense/scripts/octep/ports.py`.

### The discover answer, decoded

The reply's layout is the vendor's own, `struct nwa_msg_init_ret` in its GPL NetAgent host module
(`netagent/module-host/mv_nwa_host.h`): after the eight-byte header (the type `0x14` and the status),
a port count, then one twenty-byte `struct nwa_msg_port` per port - the tag and the flags, the
largest MTU, the unicast, multicast and MAC table sizes, and two feature words. Read on the second
XGS 3300 with the 4x10G module fitted:

| # | tag | flags | name | |
|---|---|---|---|---|
| 0 | `0x0000` | `0x0000` | | not a front port |
| 1, 2 | `0x0001`, `0x0002` | `0x0001` | F1, F2 | the board's two 10G cages |
| **3 - 6** | **`0x0003` - `0x0006`** | `0x0001` | **the module** | the same table sizes as F1 and F2: ports straight on the coprocessor |
| 7 | `0x8000` | `0x0000` | | the switch itself, not a front port |
| 8 - 17 | `0x8100` - `0x8a00` | `0x0001` | Port1 - Port8, F3, F4 | the ten behind the switch |

Eighteen entries, sixteen front ports. Before this the answer could not be read at all: the link
poll asks the same window every round and wrote over `nwa.last` the instant a discover released it.
The discover now keeps its answer in a record of its own.

### The join, measured

| tag | MAC matches | name | lifport | speed |
|---|---|---|---|---|
| 1 | eth10 | F1 | 10 | 10G |
| 2 | eth11 | F2 | 11 | 10G |
| 3 | eth12 | **A4** | 12 | 10G |
| 4 | eth13 | **A3** | 13 | 10G |
| 5 | eth14 | **A2** | 14 | 10G |
| 6 | eth15 | **A1** | 15 | 10G |
| `0x8100` | eth0 | 1 | 0 | 1G |
| `0x8a00` | eth9 | F4 | 9 | 1G |

The rows for tags 1, 2, `0x8100` and `0x8a00` agree with what the driver already knew about the
panel's ports, which is what says the join is right; the module's four rows are new. The coprocessor
lists the same sixteen with `xgs-ports -b`: 1 to 8, F3, F4, F1, F2, A4, A3, A2, A1.

## What the boot does now

`src/opnsense/scripts/octep/bringup.sh`, after it has bound the panel's twelve as before:

1. `npu-platform.sh` reads the platform database through the console - logging in only if the
   console asks (root, no password), reading with `xgs-platform`, and logging out again - into
   `/var/db/os-xgs-npu/npu-platform.txt`.
2. `ports.py` issues a discover, takes every port with flag bit 0 set that is not one of the panel's
   twelve, asks each for its address, and joins it to the database. It prints one line per port for
   the boot and writes everything known - the module's part number included - to
   `/var/db/os-xgs-npu/ports.json`.
3. Each port is bound as the board's own cages are: raised, its logical interface installed with its
   own address (`LIF_ADD`), then its tag (`PPORT_UPDATE`).
4. After the panel's twelve interfaces, each module port is presented with `dp.if_iface` set to its
   lifport and `dp.if_label` to its name, so the driver describes it "XGS front port A1" the way it
   describes the panel's. **`oxp0` to `oxp11` keep the panel's ports**, so an assignment made before
   the module was fitted still names the same metal.

`OCTEP_DP_IF_MAX` is 32 (it was 13): the largest the vendor's board files put on one coprocessor is
twenty front ports, plus a module's eight.

**Without the console** - a reader already holding it, or a coprocessor that does not answer - the
ports still bind: a direct port's lifport follows the board's two cages in tag order, which is the
rule the measurement above shows, and its name is `Ext` and the tag (`Ext3`), which says plainly
that it is not the name on the metal.

## The modules the vendor's board files describe

From the GPL `xgs-bsp/data` files (the first line of each module template). Only the first has been
fitted here.

| assembly | module |
|---|---|
| **AMDA0207-0001** | **4 x 10GBase-SR (SFP+)** - fitted, measured above |
| AMDA0105-0001 | 4 x 2.5GBase-T PoE |
| AMDA0106-0001 | 8 x 1000Base-T |
| AMDA0108-0001 | 8 x 1000Base-SX (SFP) |
| AMDA0108-0002 | 8 x 10GBase-SR (SFP+) |
| AMDA0109-0001 | 4 x 1000Base-T, fail-to-wire |
| AMDA0110-0001 | 4 x 1000Base-SX (SFP), 2 x 1000Base-SX fail-to-wire |
| AMDA0110-0002 | 4 x 10GBase-SR (SFP+), 2 x 10GBase-SR fail-to-wire |
| AMDA0110-0003 | 2 x 10GBase-SR fail-to-wire |
| AMDA0112-0001 | 2 x 40GBase-SR4 (QSFP) |
| AMDA0112-0002 | 1 x 40GBase-SR4 (QSFP) |
| AMDA0124-0001 | 4 x 1000Base-T, 4 x 1000Base-T PoE |
| AMDA0213-0001 | 12 x 1000Base-T, 4 x 2.5GBase-T ("nicmod") |
| AMDA0214-0001 | 2 x NBase-T (RJ45), 2 x 10GBase-SR (SFP+) |

An XG-generation module - a host PCIe NIC - does not belong in this bay and was measured absent from
all five places it could appear; see the hardware page.

## Tested, and not yet

On the second XGS 3300 (board revision 09, the 4x10G module in bay A), 2026-10-16, after a reboot
onto this driver and bring-up:

- the bring-up log: "read the coprocessor's platform database", "bound 4 expansion module port(s):
  A4 A3 A2 A1", "up: 16 front-port interfaces" (it said 12 before);
- `oxp12` to `oxp15` described "XGS front port A4" to "A1"; `oxp15`, A1, reports link at
  10Gbase-SR, the other three no carrier;
- the panel's twelve unchanged - the WAN still on `oxp3`, its address and route as before;
- `dp.if_label` empty after the adds; the fallback, run by hand without the database, gave the same
  lifports under the names `Ext3` to `Ext6`.

**Not yet measured: traffic through a module port.** With A1 up and nothing assigned to it, nothing
arrived in fifteen seconds and the driver's count of frames with an unclaimed tag recorded none for
tag 6 - which says nothing about the module until it is known what the far end of that cable sends.

Also open: Link Health draws the panel's twelve and not the bay; the first-boot assignment
([interface-assignment.md](interface-assignment.md)) puts module ports in the LAN bridge only on a
box nobody has set up, so on a box in service they come up unassigned and are assigned by hand.

## The XGS 136 is a different kind of bay

The desktops' expansion bay is the **host's**: the vendor sells a 3G/4G module, a 5G module, or a
second Wi-Fi radio for the w models, and the XGS 136's board has an empty M.2 socket (`CN8`). The
vendor's host system carries modem drivers (`qmi_wwan`, `cdc_mbim`, `option`, `qcserial`, `sierra`)
and Wi-Fi drivers (`ath9k`, `ath10k`, `mt7915e`). OPNsense 26.7's kernel has `umb` (MBIM) and
`u3g` for modems, so a modem in MBIM or serial mode is a FreeBSD device the moment it is fitted;
it has neither `ath10k` nor `mt7915`. None of that has been fitted or measured here, and none of it
goes through this page's code.
