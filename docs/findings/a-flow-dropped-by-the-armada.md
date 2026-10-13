# A flow the host named, dropped by the ARMADA's fast path: the first action on this family

2026-10-14, the XGS 136, step 3 of [the offload plan](../npuep/offload-plan.md), after
[the chain](the-entry-then-the-gate-on-the-armada.md) was measured. Module `a699f490`: the
driver of the previous reading plus one instrument, `dev.npuep.0.giu.last_meta` - the last
received frame's port tag and the sixty-four bytes of metadata the fast path puts in front of
every frame, decoded as `struct usfp_kn_md` at the offsets the XGS 3300's driver reads its own.
Everything else by hand through `rpc_channel.command`, with the gate open (0x0003) and one
interface entry opened (flags 0x0001) as in the previous finding.

## The identity rides in the metadata, as on the 3300

With every entry `offload_disabled` the frames carry `id_tag 0xb44399a2, md_valid 0, flow 0`.
With the gate and label 3's entry open, the frames from that port carry `md_valid 1` and a
microflow identity - `0x8000003d`: id 61, rev 0, valid - and a later frame `0x8000003a`, id 58.
The signature word is the one the OCTEON TX sets, the layout is the same, and the id is the
table index, which the next step confirms.

## Reading the entry: 112 bytes here, and only with READ_ALL

`LO_MFLOW_READ` of an index with flags 0 answered with the first word `-(index + 1)` and a
stale payload - the 3300's encoding of a refused read - for the two ids above, which had aged
out between the ping and the read. With `USFP_TABLE_FLAG_READ_ALL` (flags 0x0002) the read of
the last frame's id, 23, answered `header 23` and then the key the fast path built from the
packet: the LIF index of the ingress port (0x2000), ethertype 0x0800, family 2, protocol 6, the
two addresses, destination port 22 and a source port - the PC's own ssh session to the appliance,
as it happened - then the entry (`mflow_id` 23, rev 0, `fw_valid` 1, `host_valid` 0, a timeout,
a hash index) and the operation block, all zero. On this family the table entry's header is one
`int32 idx` - four bytes, 112 per entry - where the 3300's is eight (idx and reserved, 116).

## Programming it: state 2 and action 0, and the fast path drops the flow

`MFLOW_PROGRAM` with identity `0x80000017` (id 23, rev 0, valid), `opr_fl` 0x20000000 - state 2,
`MF_ACTIVE`, action 0, `MF_ACT_DROP`, as the 3300 found them - and the rest zero, rc 0. Within
six seconds `FROM_WIRE_DROP_MFLOW_ACTION` rose by 8 and `TX_DROP` by 8: the fast path had taken
the host's word and dropped that flow's frames itself, without the host seeing them. The entry
read back `host_valid 1, state 2, action 0`. Programmed again with state 1, the entry became
inert and its frames went back to the host on `MFLOW_NOT_ACTIVE`, 4 more drops before it took.
The pings running through the appliance for the whole minute - a different flow - went 45 of 45.

Then everything was put back: the entry to 0x0009, the gate to 0x0002, read back; 4 of 4 pings
after. The programmed entry was left inert to age out with its timeout.

## Two lessons, one of them paid for

- **Pick a flow by its key, never by "the last frame".** The last frame on the port when the
  identity was read was the operator's own ssh segment, so the flow that got the drop action was
  the ssh session this was being done over. It survived on TCP's retransmissions, and eight
  dropped segments is the price of the lesson. The driver's own table will match keys.
- **The request is thirty-two bytes.** This one went out with thirty-four - a slip - and was
  accepted rc 0 with its timeout field landing two bytes off (it read back as 8,388,096). The
  fast path checks a minimum length, not the exact one; the sender has to.

So on the ARMADA, as on the OCTEON TX: the fast path makes the flow, the metadata names it, the
host loads the action, and the state is the gate. Step 4 - the driver's own flow table on
`npuep`, keyed and verified as the 3300's - is next.
