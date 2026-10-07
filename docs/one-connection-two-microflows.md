# One connection, two microflows: the table the coprocessor expects

Built and measured on an XGS 3300, 2026-10-07, on live downloads to a PC behind the appliance.
This is the table allocator that issue #266 asked for, and the answer to the note at the end of
[the-outbound-half-was-untranslated.md](the-outbound-half-was-untranslated.md) about
`CONN_RECLAIM_PENDING` rising alongside a working offload.

## What was wrong with the old table

The driver's flow table held one entry per **direction**. Each entry carried its own connection
index, so a TCP connection became two connection entries on the coprocessor, one seeing only the
data and one seeing only the acknowledgements. The vendor's `FLOW_CREATE_FP` is built the other way
round: one connection entry and two microflows that both point at it, and the vendor's host writes
one connection index into both. The far side keeps its per-direction TCP state - `seen[dir]`, the
retransmission test, the FIN tracking - inside the one entry, indexed by direction. Two
half-connections each looked, to that state, like a stream that never gets its other half.

Measured before the change, on a connection whose both directions were in hardware and running at
line rate: `FROM_WIRE_TO_KN_CONN_RECLAIM_PENDING` rose by about a tenth of the frames forwarded.
The far side was handing one frame in ten back to the host.

And the table held 62 entries, which the automatic trigger filled in fourteen seconds. The board
holds 2,000,000 connections, 65,536 next hops and 4,001,450 microflows - read from its platform
block at attach, and printed at boot - so the bound was the host's alone.

## The table now

An entry is a **connection**: one index, which is its position in the table and the far side's
`conn_idx`; one revision, per index, that climbs on every allocation of that index and is carried
in every command about it, so a command meant for the connection that used to be here is refused
rather than applied to the one that is; two ingress tuples, one per direction, derived from `pf`'s
oriented state and checked against the frame in hand; the translation; and per direction the
microflow identity the fast path chose, read from a punted frame and never computed, with its next
hop and the front port its frames arrive on.

Next hops are a table of their own, keyed by what the host's route and ARP answered - egress
interface, neighbour, our address there, MTU - and shared: two hundred LAN-to-WAN connections
program two. Each index keeps its revision across reuse, as the vendor's host does, because a
microflow names the next hop's revision beside its index.

The host's bounds are 1024 connections and 256 next hops, capped at attach by the board's.

## How a connection is programmed

The frame in hand gives one direction. The other is looked up in the candidate ring, where every
punted frame of the last second left its identity and its port. Both go out in **one**
`FLOW_CREATE_FP`: the connection block, both microflows, both next hops already programmed.

    connection 2: original and reply programmed, 2 pf state(s) marked sloppy
      original direction to <the gateway> on interface 1, mtu 1500
      reply direction to <the PC> on interface 10, mtu 1500, identity from the candidate table

That one read, on a 154 Mbit/s download, and the eight seconds after it:

| | |
|---|---|
| `FROM_WIRE_TO_WIRE` | **+397,887** |
| `CONN_RECLAIM_PENDING` | **+0** |
| rate at the PC | 285, 350, 396 Mbit/s |
| host's own interface counter | 0 |

No frame handed back. That is the measurement the old table could not produce.

**Both directions or neither.** The first version of the automatic trigger programmed whatever
direction a candidate was and attached the other when its frame arrived, one poll later. It does
not work, and the reason is a measurement from the day before: a connection offloaded in one
direction is handed back by the fast path within a few frames - milliseconds on a busy connection -
and an entry the fast path has put in `RECLAIM_PENDING` is not revived by attaching a microflow to
it. Measured: 33 connections programmed direction by direction over twenty seconds, 250 frames
forwarded in hardware, 367,000 handed back. So a new connection waits until both of its directions
have been punted, which on a busy connection is the next poll; `dp.accel_dir` and `dp.accel_half`
are the instruments that ask for one direction on purpose and get it.

With the rule in place, the same download under the automatic trigger for twenty seconds: 27
connections, two next hops between them, 330,000 frames forwarded in hardware, **one** handed back,
no revision mismatch, nothing refused. One sample in the twenty seconds read 5 Mbit/s at the PC
while the forwarded count paused with it; it fell where one 1 GB pass of the download ended and
the next began, and it did not recur under the load below, whose lowest sample was 440.

## Under load

Three concurrent downloads to the PC, the trigger on for sixty seconds, sampled every two seconds
at the PC's adapter and at the fast path:

| | before the trigger | over the sixty seconds |
|---|---|---|
| rate at the PC | 402 Mbit/s, the host forwarding | **560 to 667 Mbit/s**, the hardware forwarding |
| `FROM_WIRE_TO_WIRE` | | **+3,481,704** |
| `FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE` | | +315 |
| `CONN_RECLAIM_PENDING` | | +18 |
| `FW_REV_MISMATCH`, `dp.flow_rc_refused`, `dp.flow_tuple_mismatch` | | 0 |
| connections in the table | | 13 at two seconds, 39 at most |
| next hops programmed | | 2 |
| connections the sweep took out as they finished | | 9 |

Nearly every frame crossed in hardware - the host was given three hundred of three and a half
million - and the PC received more than the host alone had been able to forward: the host was the
ceiling before, and the ceiling is now the uplink. The table was never full; its bound is 1024 on
this host and the far side's is two million. #266 asked for the trigger to cover the traffic, and
this is the number: a tenth of a per cent of frames reached the host.

That one of the three downloads' connections shows as several in the table over the minute is the
downloads themselves ending and starting again: each is a fresh connection, taken out by the sweep
when its state goes and re-created from its first two frames.

## Taking a connection out

The sweep asks `pf` for the connection's state by the tuple that found it and in the list that
held it. When it is gone: each programmed direction is set INACTIVE with `MFLOW_PROGRAM` - the same
identity, connection and next hop, state 1 - and then `CONN_RECLAIM_FP` is sent, which the far
side answers with the connection's final sequence state and per-direction counters. The old code
turned a flow off by sending a `FLOW_CREATE_FP`, which re-copied whatever connection block
happened to be staged over the live entry before touching the microflow; and it never reclaimed,
so nothing it programmed ever left `RECLAIM_PENDING` once the fast path put it there.

A direction the fast path keeps handing back is noticed by the drain - its identity comes round as a
candidate once a poll - and after three such polls the connection entry is read back with
`LO_CONN_READ`. Still valid at our revision: the count starts again. Anything else: the connection
is taken out, reclaimed if it is pending, and the next candidate re-creates it with the next
revision if `pf` still has the state.

## When the fast path hands a connection back anyway

One hand-programmed connection, both directions in one request on a 210 Mbit/s download, did not
forward: 129 frames in hardware, then `CONN_RECLAIM_PENDING` **+100,474** in the first ten seconds
and `FROM_WIRE_TO_KN_TCP_MAX_RETRANS` +1, then every frame punted as `MFLOW_NOT_ACTIVE` for the
three minutes it was watched, because the trigger was off and nothing ran to notice. The same
download programmed again a minute later forwarded at once.

The far side's rule is in its source: a packet with the same direction, acknowledgement number and
end as the last one it saw counts as a retransmission, and the tenth in a row makes the connection
`RECLAIM_PENDING`. Ten identical acknowledgements in a row is what a receiver sends after one lost
segment with a wide window - an ordinary event on a busy download, and it happened to fall on the
first seconds of that offload. After the board's five-second not-usable timeout the fast path
stops using the microflows and the frames come back marked not active.

So the host has to notice, and under the trigger it does: a programmed identity that keeps
arriving as a candidate is probed after three polls, a connection found pending is reclaimed and
freed (`dp.flow_pending` counts it; it read 1 in the forty-second run below), and the next
candidate re-creates it with both directions. Forty seconds of the trigger on that download and
the PC's other connections: 32 connections, 910,000 frames in hardware, 250 to the host, host
interface counter at zero.

## What the far side refuses, and that it is now heard

Every programming command's `rc` is checked. The driver sees it because it posts its descriptors
without the POST flag; with that flag set the reply is never read back and every refusal is
invisible, which is the vendor host's own blind spot. A connection the far side refused is freed
here rather than left in the table pretending to forward, and `dp.flow_rc_refused` counts it.

One refusal the far side does not make visible at all, read in its source: `FLOW_CREATE_FP` copies
the connection block into the table **before** it validates the microflow indices, and a microflow
whose slot has a different six-bit revision than the request names is skipped with a `continue`
and the command still returns 0. So the host checks a slot against the board's microflow count
before posting, and a direction whose identity was stale repairs itself from that direction's
next punted frame, which carries the current one.

## The knobs and the counters

| sysctl | what |
|---|---|
| `dp.flows` | connections currently accelerated, each with one or two directions in hardware |
| `dp.conn_max`, `dp.nhop_max` | the host's bounds after the board's were applied |
| `dp.flow_table`, `dp.nhop_table` | both tables, entry by entry |
| `dp.flow_attached` | second directions attached to a connection already in hardware |
| `dp.flow_reclaimed`, `dp.flow_pending` | connections reclaimed; connections found pending and taken out |
| `dp.flow_tuple_mismatch` | frames whose tuple was not what `pf`'s state implied - zero on a plain connection |
| `dp.flow_rc_refused`, `rpc.refused` | programming the far side answered with an error |
| `dp.nhop_shared`, `dp.nhop_full` | next hops found already programmed; times the table was full |
| `dp.accel_half` | program one direction when the other has not been punted - an instrument |

## Lesson

The vendor's request carried the shape of the answer - one connection, two microflows - and this
driver had been reading it as a convenience rather than a constraint. The far side's per-direction
TCP state is one entry indexed by direction; split it across two entries and each half is a
connection that never hears from the other end. The table had to be the vendor's table before the
offload could be the vendor's offload.
