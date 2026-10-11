# Address translation lives in the connection, not the microflow

Read out of the vendor's headers and confirmed against the appliance, 2026-10-05, while preparing
to accelerate a flow that transits the box rather than one addressed to it.

This is the fact that separates a demonstration from a product. On an ordinary appliance every
connection out to the internet is translated, so a flow forwarded by the coprocessor **without its
translation** leaves with a private source address and is dropped by the first router it meets. The
flow that was accelerated in [a-frame-was-accelerated.md](a-frame-was-accelerated.md) was addressed
to the appliance itself and needed no translation, which is why it could be accelerated by
programming almost nothing.

## Where it is

`struct usfp_conn_entry`, not `struct usfp_mflow_entry`. That is why `FLOW_CREATE_FP` carries a
whole connection and not just two identities, and it is the reason a microflow has no address
fields of its own: the microflow says *which* flow, the connection says *what to do to it*.

```c
struct usfp_nat_info {
	__be32 ipv4_orig_src;
	__be32 ipv4_orig_dest;

	__be32 ipv4_nat_src;
	__be32 ipv4_nat_dest;

	__be16 orig_dest_port;
	__be16 orig_src_port;

	__be16 nat_dest_port;
	__be16 nat_src_port;
};
```

Note the order of the ports: **destination before source**, in both pairs, which is the opposite of
the addresses above them. A block filled in the obvious order is a block with every port in the
wrong place, and every field in it is a plausible port number.

Two bits in the connection's flags word choose the direction: `do_dnat` at bit 19 and `do_snat` at
bit 20, beside `verdict` at 21 and `state` at 30. Those four positions were confirmed by writing a
connection and reading it back with `LO_CONN_READ`: `rev_num 0x1234`, `verdict 1` and `state 2` all
came back where they were put.

## The offset, by two routes that had to agree

The NAT block sits at offset 84 in a `FLOW_CREATE_FP` request. That number was not taken from one
calculation.

**Measured.** `mflow_valid` is read at request offset 112 and programming a microflow there works,
so the connection ahead of it occupies `4 + 108`. `LO_CONN_READ` returns 108 bytes for one entry,
independently.

**Computed.** `atomic` 8 + `session_id` 4 + `qos[2]` 8 + `tcp` 60 + `nat` 24 + `entry_lock` 4 = 108.

**And the computation nearly went wrong by eight bytes**, which is why the measurement mattered.
The vendor's header comments `struct usfp_tcp_info` as `/* 13 LW */`, which is 52 and would put the
NAT block at 76. It is 60: `usfp_tcp_seq` is `__attribute__((packed))` and 48 bytes of its own -
`seen[2]` alone is 32, being two `usfp_tcp_seq_td` of four words each - then `usfp_fin_state` is 8,
and four bytes of bitfields follow. The comment is stale by exactly the eight bytes that the
measured total already said were missing.

`rpc.conn_nat_off` is settable anyway. It costs nothing, and the next structure whose comment
disagrees with its members will be found with it rather than argued about.

## The host does not have to compute the mapping

`pf` already holds it. Every state has two keys - `key[PF_SK_WIRE]` and `key[PF_SK_STACK]` - one
side as the packet appears on the wire and the other as the host sees it. **For a translated
connection they differ, and the difference is exactly what `usfp_nat_info` wants.** Identical keys
mean the connection is not translated.

So `dp.pf_state` now prints both keys for the last punted frame, as raw 32-bit values rather than
dotted quads - because that is the form `rpc.conn_nat_src` and its five neighbours take. They want
network order, which is what the frame holds and what `pf` holds, so a number read out of the
diagnostic can be written into the staging sysctl unchanged. A dotted quad would have to be
converted by whoever read it, and converted the wrong way half the time.

## What is not settled

**Nothing here has been exercised.** The NAT block is written only when `rpc.conn_snat` or
`rpc.conn_dnat` is set, so until then it is twenty-four bytes of somebody else's structure that
this driver does not touch - which is deliberate, and is why the staging could be merged before the
experiment.

Whether the fast path applies the translation correctly, whether the checksums are fixed up by
hardware or have to be handed to it, and whether a TCP flow survives being handed over mid-stream
are all open.

## Lesson

A structure's comment is documentation and its members are the truth, and when they disagree the
members win - but only if somebody notices. Two routes to the same 108 is what made this safe: the
arithmetic alone would have been eight bytes out, and the measurement alone would have given a
total without saying which member was bigger than it claimed. The habit worth keeping is not "read
the header" or "measure it" but **make them meet**, because the disagreement is where the bug would
have been.

## Measured: a real translated flow, accelerated

Done on the appliance with its owner's agreement, on his own live traffic, because a flow that
transits the box cannot be manufactured from the box - traffic the host originates takes the
`FROM_KN` path, which has no microflow gate at all.

The subject chose itself. With the offload gate open, `dp.pf_state` was read until a punted frame
turned out to belong to a translated connection - an inbound HTTPS flow, the reply direction of a
connection the appliance had already translated on the way out:

    frame 1600  proto 6  <a server>:443 -> <the WAN address>:3116
      wire   0xa5429522:443  0x2e46a8c0:3116
      stack  0xa5429522:443  0x7864a8c0:60838
      translated: the two keys differ

and `dp.rx_prefix` gave that same frame's slot and revision. The fast path's own entry for the slot
confirmed it was the right flow before anything was written:

    entry 60
      lif 4096  proto 6  dport 3116  sport 443
      src <the server>  dst <the WAN address>
      rev 0  fw_valid 1  host_valid 0

`lif 4096` is `(1 << 12)`, so the WAN port's interface index is **1** - not 3, which its name
`oxp3` would suggest. The name does not follow the index, and reading it rather than assuming it
saved a next hop pointed at the wrong port.

Then one `FLOW_CREATE_FP`: the connection with `do_dnat`, the original pair and the translated pair;
the microflow at `state` 2, `action` 1, pointing at a next hop resolved to the PC on interface 10.

    FPCNTR_RX_WIRE              +17
    FPCNTR_FROM_WIRE_TO_WIRE     +5   (0 -> 5)
    FPCNTR_TX_KN                +12
    FPCNTR_..._MFLOW_NOT_ACTIVE +12

12 punted before the write landed, 5 forwarded by the coprocessor, 17 arrived. And `LO_CONN_READ`
on connection 7 shows every one of the eight NAT fields where it was put:

    [ 11] 0x2e46a8c0a5429522    orig_src <the server>   orig_dest <the WAN address>
    [ 12] 0x7864a8c0a5429522    nat_src  <the server>   nat_dest  <the PC>
    [ 13] 0xbb01a6edbb012c0c    orig 3116/443           nat 60838/443
    [  1] 0x40280001            rev 1, do_dnat set, do_snat clear, verdict 1, state 1

which puts the block at entry offset **80**, measured, where the arithmetic said it would be.

Afterwards: the connection was still `ESTABLISHED` in `pf`, 120 states live, and 0% loss on the
LAN, the WAN and the PC's own segment.

## What this does and does not prove

**Proven:** a real, translated, transiting flow can be handed to the coprocessor, and it forwards
it without the host seeing it. Every field of the connection entry including the NAT block lands
where the handler reads it. The connection survived.

**Not proven: that the translation was applied to the bytes on the wire.** There is no counter for
it - the whole counter table has no NAT counter - and the forwarded frames never enter the host, so
no capture on the appliance can see them. A frame forwarded *without* its translation would arrive
at the PC addressed to the WAN address, be discarded there, and be retransmitted by the server,
which leaves the connection `ESTABLISHED` and the counters exactly as they are above. The evidence
is consistent with the translation working and does not distinguish it from five dropped frames.

**What would settle it** is a flow accelerated for long enough that retransmission alone could not
carry it - a sustained download, watched for a throughput collapse at the moment the flow is
programmed. That is a bigger test on live traffic and is the next one worth asking for.
