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
model, from the vendor's board file, not something seen working on it.

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
A port on one of the coprocessor's own MACs is tagged by its place among those, from 1, in the order
the board file lists them: confirmed on the XGS 136, whose four such ports are Port9 to Port12, and
on the XGS 3300, whose two are F1 and F2; on the XGS 138 that order puts F2 first, and it has not
been seen there.
