# Half the connections kept one strict state

[#287](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/287). The first hour of sustained load
through the offload - three downloads restarted whenever one ended, `dp.auto` on, a sample every
ten seconds, every `curl` exit recorded - gave the best throughput this appliance has shown and
reset one download in three.

| the hour, 2026-10-07 11:00 | |
|---|---|
| receive rate at the PC | median 353 Mbit/s, maximum 386, two samples of 360 under 100 |
| frames forwarded by the coprocessor | 121.8 million |
| the driver's own counters | `auto_full` 0, `flow_rc_refused` 0, `flow_tuple_mismatch` 0, RPC timeouts 0, refusals 0 |
| downloads | 31 started: 10 clean, **10 reset** (`Recv failure: Connection was reset`), **5 stalled** to a megabit for their ten minutes, 3 at the soak's own limit, 3 killed at its end |
| pf `state-mismatch` | 1 → 16,104 |
| fast path `CONN_RECLAIM_PENDING` | 85 → 19,610 frames |

The driver believed every flow it made was fine, and the connection on top of it died. The
sixty-second load test of the connection table and the three-hour light soak had not shown it,
because neither recorded how its connections ended.

## Who sent the reset

One download at a time under two streams of load, `tcpdump` on the host's own `oxp0` and `oxp3`
for RST and FIN of the mirror. The host's interfaces carry only what the host itself sends or is
handed, so a reset made by the appliance shows with its MAC and a TTL of 64, and a mirror's shows
arriving from the WAN. Nineteen minutes in, one segment on each side:

    oxp0  <the PC's MAC> > <oxp0's MAC>  192.168.1.120.61768 > 203.0.113.108.443  Flags [R.] win 0
    oxp3  <oxp3's MAC> > <the gateway's MAC>  10.0.0.46.8615   > 203.0.113.108.443  Flags [R.] win 0

**The PC sent it** - its own MAC as the source on the LAN side - 22 µs before the host forwarded it translated out of the WAN; nothing came
from the mirror. A Windows stack answers with `RST, win 0` when the application closes a socket
that still holds unread data, which is what `curl` does after a receive error - so the reset is
the end of the story, not its cause. Through those nineteen minutes pf's two states for the
connection read `ESTABLISHED:ESTABLISHED` at every sample.

## What the code review found

Three readers over the take-out paths and pf's lookup, two refuters per finding (the workflow's
journal is local). The sweep's lookup was cleared: the tuple and list it asks pf with are the ones
that found the state at creation, and pf unlinks an ESTABLISHED state for nothing at these
timeouts. The far side's own hand-backs were few: `TCP_MAX_RETRANS` 24 in the hour, flagged
segments 734. The finding that mattered was adjacent to the question:

**The sloppy marking reached only the states a frame's own tuple could name.** `octep_pf_mark_sloppy()`
was called with the tuple of the frame in hand. A tuple reaches only the pf states that share a
key arrangement with it. From the original direction - the PC to the server - both states are
reachable: the LAN-side state's key is that pair, and the WAN-side state's stack key is the same
pair reversed. From the reply direction - the server to the WAN address - only the WAN-side state
is: the LAN-side state's key carries the opener's address, which the reply tuple never has. The
drain takes candidates as the ring holds them, so roughly half of all connections were met
reply-first, and those kept a strict LAN-side state.

## Read on the appliance

`pfctl -vss` prints `sloppy` on the verbose line of a state that carries the flag. A connection
learned from its reply, left over from the diagnostic:

    all tcp 198.51.100.238:80 <- 192.168.1.120:56269   ESTABLISHED:ESTABLISHED
       age 01:38:10, 45:141 pkts, rule 95
    all tcp 10.0.0.46:25862 (192.168.1.120:56269) -> 198.51.100.238:80   ESTABLISHED:ESTABLISHED
       age 01:38:10, 45:227 pkts, rule 91, allow-opts, sloppy

One state sloppy, one not. A connection learned from its original, started a minute later: both
lines end in `sloppy`. And pf saw 45 and 141 packets of a download that moved hundreds of
megabytes - the rest went through the coprocessor, which is the point of the offload and the
reason the strict state had never seen its window advance.

So every frame of such a connection that the fast path handed back - and it hands back every
frame of both directions for the seconds between its own `RECLAIM_PENDING` and the host's
reclaim, 460,000 of them in nineteen minutes of the diagnostic - met a state whose window was
where it had been when the connection was learned, and was dropped. `state-mismatch` is that
drop. The PC saw a gap, retransmissions that the strict state also dropped, and in the end a
`recv` that failed.

## The change

`octep_pf_mark_sloppy()` is now called with both of the connection's tuples, `tup[0]` and
`tup[1]`, which between them name every arrangement either state can be found by. Beside it, the
probe's four take-out outcomes are counted separately (`dp.probe_valid`, `probe_timeout`,
`probe_read_err`, `probe_rev_mismatch`, `probe_state_other`, with `flow_pending` as before), a
refused reclaim is counted (`dp.reclaim_refused`), each probe take-out says why in the log, and
`dp.flow_table` prints which tuple and which pf list the sweep asks with - so the next ending of
a live connection can be attributed rather than inferred.

First reading after the change, a minute into the same load: the three downloads' six states,
six `sloppy`; 21 table entries keyed by the original tuple and 24 by the reply, which is the half
the review predicted.

## Thirty minutes of the same load, after

| | the hour before | thirty minutes after |
|---|---|---|
| receive rate at the PC | median 353 Mbit/s, two samples under 100 | median 363, minimum 229, **none under 100** |
| pf `state-mismatch` | **16,104** | **1** |
| downloads reset | 10 of 31 | 4 of 17 |
| downloads stalled to a megabit | 5 | 2 |
| `TCP_MAX_RETRANS` (the fast path's own hand-back) | 24 | 2 |
| probe take-outs, by cause | not counted | 70, every one `far side state 2` - RECLAIM_PENDING; read errors 0, revision mismatches 0, other states 0, refused reclaims 0 |

The strict state was the whole of the state-mismatch and the whole of the sub-100 samples. It was
not the whole of the resets, and the rest took the PC's side of the wire to see.

## The PC's side, and what the rest was

With a hand on the machine, `pktmon` on the PC captured the belwue download's own port under the
two other streams (`receiver-capture-until.ps1`, `diag-reset-pc.sh`, read with `receiver-tcp.py`;
all local). Eight and a half minutes, 35,000 frames:

- the data arrived at a crawl, one 1,412-byte segment every 140 ms, with **510 holes** - 355 of
  them a single missing segment - 855 duplicates and 2,739 duplicate ACKs from the PC: a sender in
  permanent loss recovery, with about one segment in fifty of this connection lost before the PC;
- then the server stopped, the PC sent three keepalive probes a minute apart, and **the server
  answered the third with a RST** - forwarded in hardware, so no host-side capture had seen it. In
  the earlier case the PC's own RST was the end; here the mirror's. Both are the end of a stalled
  stream, not its cause.

Then the same download, the same load, **through the host alone** (`dp.auto=0`): 522 holes, 1,371
duplicates, 2,743 duplicate ACKs, 39 MB in six minutes. The same shape to the frame. Whatever
loses one segment in fifty of this connection does it with the coprocessor out of the path.

And the PC adapter's own discard counter, read around each run: **zero** while the belwue download
collapsed beside the other two, so the loss is before the PC's NIC as well.

What remained was the link. Read in the same hour:

| | belwue | the PC's aggregate |
|---|---|---|
| alone, 10:10 and 15:24 | 95 and 125-200 Mbit/s | |
| alone, 17:07 | 109 Mbit/s | 96 |
| beside two downloads at full rate | 0.4 Mbit/s | 262 |
| beside two downloads limited to 20 MB/s each | 0.6 Mbit/s | 284 |
| beside **one** download | 4.8 Mbit/s | 234 |
| beside an upload filling the upstream (17.6 Mbit/s of 21.5) | 85 Mbit/s | |
| two downloads, no belwue, 15:26 | | **477** |
| two downloads, no belwue, 17:16 | | **264** |

The upstream is not it (the two downloads' acknowledgements are 2 Mbit/s of a 21.5 Mbit/s
upstream, and belwue ran at 85 beside an upload that filled it). The downstream was 477 Mbit/s
wide at half past three and 264 at a quarter past five, and beside anything that takes it belwue
gets what a connection of its round-trip time gets at a saturated bottleneck, which is nearly
nothing. None of that is in this driver, this host, or this coprocessor, and the measurement that
says so is the one with the coprocessor out of the path.

So the appliance's part of #287 is the strict state, and it is fixed. A reset or a stall would
reopen it if a capture shows frames lost between the WAN port and the PC while the link is not
full - which is the measurement `stall-check.sh` and `diag-reset-pc.sh` are now built to take.

What the far side reports for its own hand-backs across the thirty minutes: two dup-ACK runs, 354
flagged segments - which is the count of connections ending, not of live ones failing - and
37,424 frames charged to RECLAIM_PENDING against 144,643 to MFLOW_NOT_ACTIVE: the window between
a connection's end and the host noticing, three polls wide, is where those frames are, and
`dp.flow_pending` 77 is that count of endings.

**Lesson.** A marking made from the frame in hand covers what that frame can name, and a
connection has two names. The first version of this was measured necessary with one connection,
learned from its original direction, where the one name was enough - and the measurement was
right about what it measured. What it did not measure was the other half of the connections, and
nothing in the driver's counters could show them, because pf's drop is not the driver's drop.
