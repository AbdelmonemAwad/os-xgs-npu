<!-- reviewed: 2026-10-17 -->
# Families and hardware

*Moved here from the [README](../../README.md) on 2026-10-18, word for word, when the README was
made shorter. Struck claims stay struck, with what replaced them beside them; links were only
re-pointed to work from this folder.*

## 🧩 Families

Sophos XGS appliances are not one machine with a range of speeds. The coprocessor behind the front
ports belongs to one of **six** families, and the firmware picks the family by probing for a single
PCI id or a CPU model string - `xgs-host-startup.sh` in the vendor's own tooling is where that table
lives. Two of the six have no coprocessor at all.

The table is what has been **run**, not what has been read. A row with no hardware means exactly
that: the constants are carried because the vendor's tables give them, and nothing has been powered
on.

| family | probed by | platforms | driver | binds? | hardware here? | what works |
|---|---|---|---|---|---|---|
| [ARMADA](../families/armada.md) | `11ab:7080` | `xgsdt1`, `xgsdt2-116`, `xgsdt2-126136`, `xgsdt2-138` | `npuep` | yes | **XGS 136** | **all 14 front ports** |
| [OCTEON TX](../families/octeon-tx.md) | `177d:a300` | `xgs1us` | `octep` | yes | **XGS 3300** | **all 12 front ports** |
| [OCTEON TX2](../families/octeon-tx2.md) | `177d:b200` | `xgs1ul`, `xgs1ul_4x80`, `xgs2u`, `xgs2ub` | none | no | no | nothing - documented only |
| [OCTEON TX2 98XX](../families/octeon-tx2-98xx.md) | `177d:b100` | shares the TX2 platforms | none | no | no | nothing - documented only |
| [TOPAZ](../families/topaz.md) | `Atom C11` in `/proc/cpuinfo` | - | not needed | - | no | no coprocessor exists |
| [GR](../families/gr.md) | `Atom` **and** `P69` | `AMDA0004-*` | not needed | - | no | no coprocessor exists |

**The two drivers are not at the same stage, and the table says so.** `npuep` carries a datapath;
`octep` brings up a management link, completes the SDP handshake, drives NetAgent, raises a front
port and reads its link back, and posts frames on SDP rings that the coprocessor consumes - they
leave PortF1, arrive on PortF2 and are written back into host memory, but each ring delivers one
packet and then stops, so it has no usable front-port interface. Both are built on the appliance against the running
kernel's own sources and neither is packaged - see
[docs/families/octeon-tx.md](../families/octeon-tx.md) for how to build and start `octep`,
including why its handshake is a separate step you have to ask for.

**`177d:b100` is its own family and not a variant of TX2.** It has a separate branch in the vendor's
startup script, and that branch counts how many times the id appears, because on those boards it
appears more than once. Calling it OCTEON TX2 loses that.

**The two families are genuinely different protocols, not one protocol with two PCI ids.** ARMADA
keeps its facility table at a fixed offset in a BAR and guards it with a cookie; OCTEON publishes a
pointer to its table in a CSR and guards it with a different magic word. Their facility records have
different fields in a different order. ARMADA raises target-to-host doorbells as MSI-X vectors; on
OCTEON every facility reports that it has none. That is why there are two drivers and not one with a
switch in it - see [docs/families/](../families/) for each one.

## 🖥️ Hardware

Two appliances, running OPNsense 26.7 on FreeBSD 15.1:

- **Sophos XGS 136** - assembly AMDA0201, Marvell CN9131, ARMADA family, 14 ports. All fourteen
  carry traffic. - **Sophos XGS 3300** - assembly AMDA0202-0004, Cavium OCTEON TX CN83XX, 12 panel
  ports - of which ten are ports of an on-board 88E6193X switch and only two attach to the
  coprocessor directly, see [the family
  page](../families/octeon-tx.md#how-the-ports-are-actually-wired) - plus a host-side Intel
  management NIC. Its management link to the coprocessor is up, the SDP handshake completes, the
  host drives SDP datapath rings, traffic crosses in both directions, and the front ports are
  ordinary FreeBSD interfaces - up to twelve of them, one per front port, each with that port's own
  address.

Everything below in this section is about the XGS 136 and the ARMADA reset tables.

The per-board reset values are a table, not a constant — the polarity is inverted between board
generations — and the board is read from its own assembly number, which sits inside the SMBIOS
type 2 serial (`src/opnsense/scripts/xgs/board.sh`). Values are carried for AMDA0200, AMDA0201
(XGS 126/136), AMDA0202-0205 - AMDA0202 being the XGS 3300's own OCTEON TX assembly, carried here
because the reset table is per assembly and not per family - AMDA0208 (XGS 116) and AMDA0224
(XGS 138). **Only AMDA0201 has been driven on real hardware, and the tool refuses to drive the
pins on any other board.** The others come from the vendor tool and should be treated as
unverified.

An earlier version of this section said the board was already read and looked up. It was not: the
tool carried AMDA0201 as a constant, and on an XGS 3300 that sent the 136's values to a bridge
wired to the coprocessor's reset and boot flash. The coprocessor dropped off the bus until the
bridge was put back to its power-up state with `mcp2210.py restore`.
