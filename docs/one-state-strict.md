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
not the whole of the resets. Those continue at a lower rate, and one of them can now be placed:
the Ubuntu download reset at 14:26:47, the probe took connection 6 out as RECLAIM_PENDING at
14:26:46 - one second earlier, which is the order the fast path imposes, since a RST it sees moves
the connection to RECLAIM_PENDING before the host can read it. The take-out is the reset's
consequence. The reset's cause is on the PC, which sent it, and the PC's side of the wire is the
one place not yet captured: `receiver-capture.ps1` takes it, elevated, and that needs a hand on
the machine.

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
