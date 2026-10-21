<!-- reviewed: 2026-10-21 -->
# Every model's front ports, by the number on its board

Written 2026-10-21. Which ports an XGS has and what each is called on the metal is a fact of its
model, and the model is a fact of its board: the assembly number the board was built as, which the
planar serial carries whole - `AMDA0202-0004` is an XGS 3300, `AMDA0201-0003` an XGS 136. Both
drivers, the bring-up and the first boot's assignment read the model's ports from one table,
[`scripts/xgs/models.py`](../../src/opnsense/scripts/xgs/models.py), restated from the vendor's own
board file for each assembly; `tools/test-models.py` holds the ARMADA driver's copy in C and the
fallbacks against it.

**Not the board revision.** `smbios.planar.version` is the revision, not the model: the two XGS 3300s
in the lab are the same assembly, `AMDA0202-0004`, system version `3300r1`, and report revisions 17
and 09. A table keyed on the revision named one of them and not the other. Nothing here reads it.

**Only the XGS 3300 and the XGS 136 have been run.** Every other row is what the code does on that
model, from the vendor's board file, not something seen working on it. What was run, 2026-10-21:

| | XGS 136 | first XGS 3300 | second XGS 3300 |
|---|---|---|---|
| driver | `npuep.ko e415e935`: "XGS 136 (AMDA0201-0003): 14 front ports" | `octep.ko c7c41418` | the same |
| after a reboot | the fourteen interfaces, their names, flags and MTU identical to before | bring-up's panel F1 F2 Port1-Port8 F3 F4; the labels file identical to before | the same, and the module's A4 to A1 after them |
| the first boot's plan, dry | "XGS 136: set up already ... nothing is changed" | "XGS 3300: set up already ... nothing is changed" | - |
| another model's serial, dry | - | an XGS 2100's (`AMDA0202-0001`): Port1-Port8, F1, F2 | - |

The last row is the table read for an XGS 2100 on an XGS 3300's hardware - it shows the names the
table gives, not a 2100's ports.

~~**And only where the project installs.** `board.sh` names the boards the project has been run on, and
nothing else is installed on: the ARMADA family's reset line is driven by a pin that differs from board
to board, and a wrong guess drives the coprocessor's reset. So the XGS 126 and 136 (`AMDA0201`) and the
whole OCTEON TX family (`AMDA0202`) install, and the XGS 87, 107, 116 and 138 do not yet: their rows
are ready in the table and in the driver for the day their reset wiring is known.~~

**Every model installs, and every model not run here says so (2026-10-21).** The reset wiring was not
unknown: the vendor's `xgs-usb-spi-flash` carries a table of it, read out of the binary on 2026-09-25
and already in `scripts/npuctl/mcp2210.py` - the same three lines on every ARMADA board, GP0, GP3 and
GP5, with the XGS 87 and 107's board (`AMDA0200`) driven the other way round from the rest. So
`board.sh` names every board of both families, and gives every model but the XGS 136 and the XGS 3300
a third word, `rnd`: **research and development, untested, because there is no such appliance here to
test on.** The installer prints it, the status page shows a card for it, a problem report carries it,
and the reset tool drives the XGS 87, 107, 116 and 138's lines with the vendor's values and says so
each time. The XGS 107's board is also described from the outside - CN9130 at `11ab:7080`, an 88E6193X
with Port1 to Port8 on its ports 1 to 8 and F1 on 9, an MCP2210 - by a project that runs OpenWrt on its
coprocessor ([openwrt-sophos-xgs107](https://github.com/nicologiuliani6/openwrt-sophos-xgs107/blob/main/docs/hardware.md)),
which is what the table here says for it.

## What changes on each model

- **ARMADA family** (`npuep`, `npupN`): the driver makes the model's ports and no others - five on an
  XGS 87, nine on a 107 or 116, eight on a 138, fourteen on a 126 or 136. `npupN` is PortN; the cages
  follow the highest port.
- **OCTEON TX family** (`octep`, `oxpN`): the bring-up adds the model's ports and gives each its label.
  The XGS 2100 and 2300 have no cages on the coprocessor itself, and the two behind their switch are
  F1 and F2 on the metal - which the XGS 3300's names, F3 and F4, would have got wrong.
- **The first boot's assignment** names every port as the table does, and gives the internet to Port2
  on the OCTEON TX family and Port1 on the ARMADA family, as on the two appliances run here.
- A model the table does not have is given the XGS 3300's or the XGS 136's ports, which is what every
  appliance of each family was given before the table, and the driver or the bring-up says so.

## The table

| model | assembly | ports | label and interface, in unit order | run here |
|---|---|--:|---|:-:|
| XGS 87 | `AMDA0200-0001` | 5 | Port1 `npup1`, Port2 `npup2`, Port3 `npup3`, Port4 `npup4`, F1 `npup5` | no |
| XGS 87w | `AMDA0200-0002` | 5 | Port1 `npup1`, Port2 `npup2`, Port3 `npup3`, Port4 `npup4`, F1 `npup5` | no |
| XGS 107 | `AMDA0200-0003` | 9 | Port1 `npup1`, Port2 `npup2`, Port3 `npup3`, Port4 `npup4`, Port5 `npup5`, Port6 `npup6`, Port7 `npup7`, Port8 `npup8`, F1 `npup9` | no |
| XGS 107w | `AMDA0200-0004` | 9 | Port1 `npup1`, Port2 `npup2`, Port3 `npup3`, Port4 `npup4`, Port5 `npup5`, Port6 `npup6`, Port7 `npup7`, Port8 `npup8`, F1 `npup9` | no |
| XGS 126 | `AMDA0201-0001` | 14 | Port1 `npup1`, Port2 `npup2`, Port3 `npup3`, Port4 `npup4`, Port5 `npup5`, Port6 `npup6`, Port7 `npup7`, Port8 `npup8`, Port9 `npup9`, Port10 `npup10`, Port11 `npup11`, Port12 `npup12`, F1 `npup13`, F2 `npup14` | no |
| XGS 126w | `AMDA0201-0002` | 14 | Port1 `npup1`, Port2 `npup2`, Port3 `npup3`, Port4 `npup4`, Port5 `npup5`, Port6 `npup6`, Port7 `npup7`, Port8 `npup8`, Port9 `npup9`, Port10 `npup10`, Port11 `npup11`, Port12 `npup12`, F1 `npup13`, F2 `npup14` | no |
| XGS 136 | `AMDA0201-0003` | 14 | Port1 `npup1`, Port2 `npup2`, Port3 `npup3`, Port4 `npup4`, Port5 `npup5`, Port6 `npup6`, Port7 `npup7`, Port8 `npup8`, Port9 `npup9`, Port10 `npup10`, Port11 `npup11`, Port12 `npup12`, F1 `npup13`, F2 `npup14` | **yes** |
| XGS 136w | `AMDA0201-0004` | 14 | Port1 `npup1`, Port2 `npup2`, Port3 `npup3`, Port4 `npup4`, Port5 `npup5`, Port6 `npup6`, Port7 `npup7`, Port8 `npup8`, Port9 `npup9`, Port10 `npup10`, Port11 `npup11`, Port12 `npup12`, F1 `npup13`, F2 `npup14` | no |
| XGS 116 | `AMDA0208-0001` | 9 | Port1 `npup1`, Port2 `npup2`, Port3 `npup3`, Port4 `npup4`, Port5 `npup5`, Port6 `npup6`, Port7 `npup7`, Port8 `npup8`, F1 `npup9` | no |
| XGS 116w | `AMDA0208-0002` | 9 | Port1 `npup1`, Port2 `npup2`, Port3 `npup3`, Port4 `npup4`, Port5 `npup5`, Port6 `npup6`, Port7 `npup7`, Port8 `npup8`, F1 `npup9` | no |
| XGS 138 | `AMDA0224-0001` | 8 | Port1 `npup1`, Port2 `npup2`, Port3 `npup3`, Port4 `npup4`, Port5 `npup5`, Port6 `npup6`, F1 `npup7`, F2 `npup8` | no |
| XGS 2100 | `AMDA0202-0001` | 10 | Port1 `oxp0`, Port2 `oxp1`, Port3 `oxp2`, Port4 `oxp3`, Port5 `oxp4`, Port6 `oxp5`, Port7 `oxp6`, Port8 `oxp7`, F1 `oxp8`, F2 `oxp9` | no |
| XGS 2300 | `AMDA0202-0002` | 10 | Port1 `oxp0`, Port2 `oxp1`, Port3 `oxp2`, Port4 `oxp3`, Port5 `oxp4`, Port6 `oxp5`, Port7 `oxp6`, Port8 `oxp7`, F1 `oxp8`, F2 `oxp9` | no |
| XGS 3100 | `AMDA0202-0003` | 12 | F1 `oxp0`, F2 `oxp1`, Port1 `oxp2`, Port2 `oxp3`, Port3 `oxp4`, Port4 `oxp5`, Port5 `oxp6`, Port6 `oxp7`, Port7 `oxp8`, Port8 `oxp9`, F3 `oxp10`, F4 `oxp11` | no |
| XGS 3300 | `AMDA0202-0004` | 12 | F1 `oxp0`, F2 `oxp1`, Port1 `oxp2`, Port2 `oxp3`, Port3 `oxp4`, Port4 `oxp5`, Port5 `oxp6`, Port6 `oxp7`, Port7 `oxp8`, Port8 `oxp9`, F3 `oxp10`, F4 `oxp11` | **yes** |

## How a port is tagged

A port behind the board's switch is tagged `0x8000 | port << 8`, port being its number on the switch.
~~A port on one of the coprocessor's own MACs is tagged by its place among those, from 1, in the order
the board file lists them: confirmed on the XGS 136, whose four such ports are Port9 to Port12, and
on the XGS 3300, whose two are F1 and F2; on the XGS 138 that order puts F2 first, and it has not
been seen there.~~

**A port on one of the coprocessor's own MACs is tagged by its place among the coprocessor's own
ports in the order of their addresses, from 0, the link to the switch among them** (read
2026-10-21). The board file writes such a port's address as `0:N`, N being the MAC's number across
the two CPs - CP0's eth0 to eth2 are 0 to 2, CP1's 3 to 5. The coprocessor's Linux names its ports in
that order whatever its device tree's aliases say, and its network agent lists them so
(`/sys/kernel/nwa_ports_info`), each one's place its tag. On the XGS 136, read on the appliance:

| place = tag | coprocessor's Linux | device-tree node | board file | label |
|:-:|---|---|---|---|
| 0 | eth0 | CP0 eth0 | - | the link to the switch, 10GBASE-KR |
| 1 | eth1 | CP0 eth2 | `0:2` | Port9 |
| 2 | eth2 | CP1 eth0 | `0:3` | Port12 |
| 3 | eth3 | CP1 eth1 | `0:4` | Port10 |
| 4 | eth4 | CP1 eth2 | `0:5` | Port11 |

The XGS 3300's agent lists a tag 0 as well, with no front-port flag, and F1 and F2 at 1 and 2. On the
XGS 138 the vendor's device tree puts the link to the switch on CP1's eth1, fourth by address, so the
order is F1 (CP0 eth0, `0:0`) tag 0, Port5 (CP0 eth2) 1, F2 (CP1 eth0) 2, the switch 3, Port6 (CP1 eth2)
4 - which the table and npuep now carry; until 2026-10-21 they had F2 at 1, F1 at 2, Port5 at 3. That is
the device tree read, not an XGS 138 run.

## What says whether a model's table is right

`scripts/xgs/portcheck.py`, on the appliance. On the ARMADA family it reads the network agent's own
ports and each one's device-tree node, the board file's address for each label, and which of the
driver's interfaces each port carries the address of - npuep gives the agent every tag's address - and
says, port by port, whether the board and the driver name the same label. On the OCTEON TX family it
holds the table's tags against the agent's discover answer. It prints labels, tags and interface names,
no address. On the XGS 136 every one of its four agrees; on the second XGS 3300 all twelve of the
table's are the agent's, and the module's four are named as not in the table.

On a model marked research and development the problem reporter runs it once a boot, ten minutes in,
and files what it says as an issue - one for each model and verdict - when reporting is on. The first
owner of a model is who confirms its table, or shows where it is wrong.
