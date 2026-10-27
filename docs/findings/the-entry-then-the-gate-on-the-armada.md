<!-- reviewed: 2026-10-14 -->
# The entry first, then the gate, then the flow table: the ARMADA's chain, measured

2026-10-14, the XGS 136, module 304f82c7 unchanged; step 2 of [the offload plan](../npuep/offload-plan.md),
sent by hand through `dev.npuep.0.rpc_channel.command` with the payloads of
[rpc-payloads.md](../npuep/rpc-payloads.md), and read with `tools/npuep-fpcnt.sh` - this family's
176 fast-path counters by name, in the order its own `usfp_rh.ko` declares them
(~~`fpcntr-names-armada.txt`; the OCTEON TX's 182 are in another order from index 1 on~~ -
corrected 2026-10-25: the order belongs to the vendor version, not to the family. These 176 are vendor version
21's - the build this appliance's coprocessor runs, and the OCTEON TX module of the same build
declares the same 176 - now in `tools/fpcntr-names-sfos21.txt`; the 182 are vendor version 22's
([rpc-numbering.md](../families/rpc-numbering.md)).

## What the fast path does with a frame here, in three counters

| state of the entry and the gate | where the port's frames land | measured |
|---|---|---|
| the entry says `offload_disabled` (how `npuep` programs every port), the gate closed | `FROM_WIRE_TO_KN_LIF_OFFLOAD_DISABLED`, one per wire frame; `FORCED` 0 | the baseline: 19,829 = `RX_WIRE`; four pings through +42 |
| the entry opened (`LIF_ADD_UPDATE`, flags 0x0001, update_mask 0x0010), the gate still closed | `FROM_WIRE_TO_KN_FORCED` - the XGS 3300's #210 counter, 0 until this moment | +32 for four pings and their replies; `LIF_OFFLOAD_DISABLED` +28 from the other ports |
| the entry opened and the gate opened (`FW_CFG_PARAMS_SET` 0x0003) | `FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE` - the entry the fast path made for the connection has no action loaded, so every frame is punted | +21 for four pings; `FORCED` +0 |

Every ping went through, 4 of 4 each time, and 3 of 3 after each restore: the host received
every punted frame in all three states. Both were put back - the entry to 0x0009, the gate to
0x0002, the 3300 driver's default - and read back where a read exists; `fw_state` has no read,
so the value the gate held before the first write is known only by its effect, `FORCED` having
counted, which says bit 0 was clear.

So the chain on this family is the OCTEON TX's chain with the first two links in the other
order: on the 3300 the gate forced everything to the host until `FW_CFG_OFFLOAD` was set and
the entries were opened afterwards; here `npuep` opens nothing and the entries hold the frames
before the gate ever sees them, which is why `FORCED` read 0 on this appliance for weeks. Past
both, the fast path makes a microflow for the connection and waits for the host to load it -
the state is the gate, as [the 3300 found](the-state-is-the-gate.md), with the same counter.

## Two things the commands taught

- `LIF_ADD_UPDATE` is eighteen bytes: index 4, mac 6, mtu 2, flags 2, **two bytes of padding**,
  update_mask 2. A sixteen-byte request - the fields packed - is answered rc 1 and changes
  nothing; the padding is where `usfp_lif_config` puts it, at +14, with the mask at +16.
- A selective update works: `update_mask 0x0010` changed `offload_disabled` alone and left the
  mac, the mtu and the fast path's own `rep_pid_mlb` (0x200 in the flags word, which the driver
  never wrote) exactly as they were.

## Next

Step 3: `LO_MFLOW_READ` for the entry the ping made (its identity and `fw_valid`),
`MFLOW_PROGRAM` with state 2 and the drop action, `FROM_WIRE_DROP_MFLOW_ACTION` on the next frame,
then `MFLOW_INVALIDATE` - the 3300's "a frame was accelerated", on this family, by hand.
