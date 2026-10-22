<!-- reviewed: 2026-10-18 -->
# A takeout took nothing out on the ARMADA, and what came back stopped at pf

Measured on an XGS 136, 2026-10-18.
[Issue 498](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/498): on this family, taking a
connection out of the coprocessor's fast path did nothing, and a connection handed back to the host
stopped there. Three faults, found together while measuring the high-priority ring; each is fixed
below and measured before and after.

## What was seen

The high-priority ring was being measured by tearing a thousand connections down at once - made in
hardware, then pf's states flushed, so the next sweep takes all of them back - and the pass that did
it held the flow table for **7.0 seconds** with its commands posted and **12.4 seconds** with each
one waited for. A command on this channel is answered in 138 microseconds
([456](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/456)); these were taking 3.6 to 9.5
milliseconds each. The coprocessor's own console said why, one line per command, about 6.3 ms apart
- the time a console line takes to print:

```
error: mflow_invalidate invalid mflow invalidate type 0
```

## The causes

**A takeout took nothing out.** `npuflow_takeout` sent two `MFLOW_INVALIDATE`s of invalidation type
0, taken to mean "the microflow the data names". The coprocessor's handler, `mflow_fpop_invalidate`
in the vendor's usfp source, invalidates by interface (`0x1`), by IPv4 source or destination
address (`0x2`, `0x4`), by IPv6 source or destination address (`0x8`, `0x10`) and by revision
(`0x10000`) - the values read from the coprocessor's own module - and has no type 0: it refused every
one. Nothing read the answer, so nothing noticed. The XGS 3300's driver never sends this command; it
takes a connection back with `CONN_RECLAIM_FP`, by its index and the revision it was made with.

What that left, measured with module `52fc321c`: one stream from the bench PC through the XGS 136 to
the second XGS 3300, made in hardware; at second 10 its pf states killed by id. The host's flow
table emptied, and the stream went on through the coprocessor at 942 Mbit/s to the end of the run -
998 Mbit/s delivered at the far end from second 13 to 23, with no state for it in the firewall.
Everything that takes a connection out goes this way: the keepalive when pf drops a state, the
never-offloaded list, Return to host.

**A mass takeout went by halves.** The keepalive walks the live list and takes out a connection
whose pf state is gone; the takeout moves the last live connection into the freed place. The walk
then went on to the next place, passing the one just moved, and stopped once it had asked as many
connections as were still live. With every state gone, each sweep took out half the table: 1,024
connections took about ten sweeps, ten seconds.

**What came back stopped at pf.** While the coprocessor forwards a connection pf sees none of it,
so pf's idea of each side's sequence numbers stays where the last frame it saw left it. The first
frames to come back - handed back by Return to host, given back by the fast path, the FIN at the end
- are judged against a window pf never saw move and dropped as `state-mismatch`. The XGS 3300's
driver marks both of a connection's pf states sloppy the moment it is made, measured as needed in
[#287](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/287); this driver has had the same function,
`npuep_pf_mark_sloppy`, since its flow table began (#387), with a comment describing exactly this -
and nothing called it.

## The fixes

- A takeout sends **`CONN_RECLAIM_FP`** (command 16: the connection's index, its revision, 8 bytes),
  posted on the high-priority ring. A revision that no longer matches is refused there, so a late
  one cannot take out the connection that reused the index.
- The keepalive asks next about the connection a takeout moved, and stops once every live connection
  has been asked and kept.
- Both of a connection's pf states are marked sloppy when it is made - `flow.pf_sloppy`, 1 by
  default, the XGS 3300 driver's `dp.pf_sloppy`; 0 is there only to measure without it. Since
  2026-10-22 the mark is cleared again when the connection is taken back, `pf` given the fast path's
  window, and the window is checked by the coprocessor meanwhile
  ([measured](../measurements/tcp-window-2026-10-22.md),
  [679](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/679)).

## Measured

Every run was taken in a window of its own and photographed;
[the measurement page](../measurements/xgs136-hardware-2026-10-18.md#taking-a-connection-out) has the
photographs and the runs in full.

| | before | after |
|---|---|---|
| one stream, its pf states killed | went on at 942 Mbit/s through the coprocessor | stopped within the second: 0 Mbit/s delivered from second 13 |
| a thousand connections, pf's states flushed | 961 of 1,025 out after 15 s; the table held 7.0 s | all 1,031 out within 100 ms, in one sweep that held the table 8.3 ms |
| one stream handed back to the host | stopped: pf counted 1,290 `state-mismatch` drops | went on through the host at 941 Mbit/s; pf counted 2 |

## What this corrects

- [The sweep accelerates flows on its own](the-sweep-accelerates-on-its-own-on-the-armada.md) said the
  keepalive invalidates a gone connection's microflows; it sent a command that was refused.
- [The driver accelerates a flow](the-driver-accelerates-a-flow-on-the-armada.md) said `=off`
  invalidated both microflows; what put the traffic back was the interface entries and the gate.
- [Every next hop named an empty cage](every-next-hop-named-an-empty-cage-on-the-armada.md) said a
  session was handed back to the host by #484's release; the release's takeout was refused, so what
  moved that session to the host is not known.
- [Parity](../families/parity.md) and [armada.md](../families/armada.md) said Return to host was tested
  live on this family; it took nothing out until now.

## The lesson

A command whose answer nobody reads can fail for its whole life unseen: read the answer of every
command at least once, and look at the far side's own log. And a helper written for a known failure
and never called is that failure waiting to happen.
