# A give-back is answered at once

Measured on an XGS 3300, module `94d6b9ee`, 2026-10-08, the morning after
[the flow carried the association](the-flow-carries-the-association.md). That page ended with a
download that would not stay in hardware: the fast path handed a fast TCP connection back to the
host every few seconds, and a third to nine tenths of it crossed the host. This page is what that
was made of and what was done about it. Four streams downloading through the tunnel went from
**23 to 42 % forwarded by the coprocessor to 95 to 97 %**, and the appliance's host from half a
core to less than a tenth of one. Almost none of the loss was the give-back. It was the second the
driver took to notice, and a millisecond it spent waiting on every command.

## What the fast path does, read in its own code

Five readers, one question each, in the vendor's kernel-side source and in the device's own
binaries, before a line was written.

- **The rule.** For a TCP frame with ACK set, on a connection that is in hardware: the same
  direction as the last such frame, the same acknowledgement, the same end and the same window. A
  frame that differs in any of them, or comes from the other direction, starts the count again.
  The twelfth identical one gives the connection back. Nothing switches the rule off - not the
  firewall configuration's sequence-check bit, not the connection's own - and its limit is a
  constant in the source.
- **A lost segment is exactly that.** The receiver of a fast download answers every segment that
  arrives after the hole with the same acknowledgement, and segments are lost: a sender on a
  10 Gbit/s link finds a 1 Gbit/s port's rate by overrunning its queue.
- **Given back, nothing tells the host.** The connection's entry goes to a state the vendor calls
  reclaim-pending and every frame of the connection is handed up - with the same metadata as any
  other punted frame. There is no reason code in it.
- **The handler that creates a connection does not look at what is there.** It takes the entry's
  lock and copies the request over it: no test of state, none of revision. In the source and in
  the device's module alike. So the command that made the connection, sent again for the same index
  and the same revision with no microflow in it, puts the entry back in service, and the next frame
  is forwarded - the fast path reads the entry on every frame and its microflows name the
  connection by index and revision.
- **But the microflows are already condemned.** The first frame that finds its connection not
  usable puts its microflow on a terminal timer, about five seconds on this board, that no command
  from the host lengthens and that traffic does not refresh. Reviving the connection does not undo
  it. When the timer runs out the microflow is removed and the next frame arrives under a new
  identity, which has to be attached.
- **The vendor's own host never revives.** It frees the index, moves to the next revision and
  starts again when its connection tracker asks. That is also what this driver did: read the entry,
  two commands to deactivate the microflows, one to reclaim, free the record, and make the
  connection again from the next punted frames - on the same condemned microflows, which then went
  five seconds later. The identity that changed every five seconds on the page before was this.

## Three changes

**The receive path kicks.** A punted frame that belongs to a connection the driver holds is news
that cannot wait a second: either the connection has been given back, or its microflow has gone
and the direction needs attaching again. The receive path answers "is this one of ours" with one
read of a table of counts indexed by a hash of the frame's addresses and ports - four thousand
places for at most two thousand tuples, written where a connection is made or freed - and if it
is, queues a task on a thread of the driver's own. The system's shared task thread is where the
once-a-second poll sleeps on the management channel; a kick is worth what it is worth in
milliseconds. A run is kept five milliseconds from the last, deals only with connections that
exist, and leaves a frame of a connection nobody holds exactly as it was for the poll - so the rate
at which connections are *made* is still the poll's. (It was, on this page. Two changes later a
run makes them too: [a connection made between polls](made-between-polls.md).)

**A connection given back by the rule is revived, not rebuilt.** The run reads the entry. Its own
counter of identical frames stands at the limit when the rule is what gave it back, and at whatever
it was for any other reason; if it is at the limit, both directions are in hardware and no frame of
the connection has been seen carrying FIN or RST, the connection block is sent again as it was. One
command. A connection that keeps being given back - a long burst of duplicate acknowledgements does
that a dozen frames after every revival - is left alone for ten milliseconds after the first
revival, twenty after the next, and so on to half a second, starting over after a second of peace.
Everything else given back is taken out as before, at once instead of a second or three later.

**A command is waited for in steps of twenty microseconds.** Every command waits for its answer
with the transmit path's lock held. The wait looked at the answer once a millisecond, so a command
cost a millisecond however soon it was answered: two hundred commands by hand took 0.33 s, of which
0.13 s was the two hundred system calls. The far side answers in **25 microseconds** on average -
over the 4,644 commands of the last run - and 920 at worst. Making a connection was three
milliseconds of every port's transmit, and is now about a tenth of one.

`dev.octep.0.dp.fast` and `dp.revive` turn the first two off, which is how each was measured.

## Measured

The same lab as the page before: two appliances joined at 10 Gbit/s, the tunnel between them, the
receiving machine on a 1 Gbit/s LAN port, `ipsec.flows` 2. One module for every figure on this
page unless a sentence says otherwise. "By the flow" is the share of decrypted frames the
coprocessor forwarded itself, from its own counters.

| downloads | `dp.fast` 0, `dp.revive` 0 | 1, 0 | 1, 1 |
|---|---|---|---|
| four streams, 12 s: by the flow | 23 % | 66 % | **95 %** |
| host, cores of 8 | 0.58 | 0.31 | **0.06** |
| four streams, 30 s: by the flow | 42 % | 80 % | **97 %** |
| host | 0.49 | 0.23 | **0.09** |
| frames handed up while given back | 490,863 | 2,889 | 24,529 |
| connections given back, revived, taken out | 34, 0, 36 | 125, 0, 129 | 159, **159**, 5 |
| one stream, 12 s: by the flow | 76 % | 99 % | 98 % |

The first column is the driver of the page before with only the faster command wait. In the last
column the rule fired five times as often - a connection that is in hardware is there to be given
back - and it no longer matters: 159 give-backs in thirty seconds cost 24,529 frames, a hundredth
of the download.

A second twelve-second row of the last column, in the regression run below, was 86 %: at twelve
seconds the first second of four connections is a twelfth of everything.

Two times, from a single-stream run sampled three times a second on the first build of this
change:

- **from the give-back to the revival:** 61 frames handed up, at eighty thousand a second;
- **from a microflow's removal to its direction being attached again:** 13 frames.

The rest of the page before, run again on this module with both settings on - TCP payload in
Mbit/s, twelve-second rows unless they say otherwise:

| | `ipsec.flows` 0 | 1 | 2 |
|---|---|---|---|
| one stream up | 75 | 224; 283 over a minute | 781; 766 over a minute |
| one stream down | 239 | 262 | 758; 783 over 30 s |
| four streams up | 919 | 923 | 948 |
| four streams down | 927 | 943; 935 over 30 s | 958; 944 over 30 s |
| eight streams up | | | 970 |
| host, four streams down | 0.90 | 0.69 | 0.14; **0.04** over 30 s |
| host, four streams up | 1.12 | 0.59 | 0.06 |

- A rekey under a four-stream upload: 934 and 924 Mbit/s with `ipsec.flows` 2 and 1, no replay
  drop at the peer, nothing dropped by the driver.
- The clear-text probe of the page before, unchanged: 0 of 20 delivered with `ipsec.flows` 0 and 1,
  15 of 20 with 2.
- An index reused: a five-second upload at 791 Mbit/s reported as 508,198,324 and 14,060,720 bytes
  on indexes whose totals read 13.9 and 23.2 GB.
- The tunnel taken down and brought up under a running connection: out with its associations,
  plain while there was no policy, taken out by the sweep and made again; 2,972 of 3,000 answered.
- 4,644 commands in the whole run, none unanswered, none refused.

What is left of a download outside hardware is its first second: a connection is still made by the
poll, when both of its directions have been seen. That second, and what was really in it, is
[a connection made between polls](made-between-polls.md).

## The SYN that looked like an ending

The first build marked a connection as ending when a punted frame of it carried FIN, SYN or RST -
the three flags the fast path itself gives a connection back for. Its first run reached 88 to 91 %
and the log said why not more:

    connection 10 rev 44 taken out by the probe: ... 10 identical in a row, FIN, SYN or RST seen

A download given back by the rule in its fortieth second, and taken out instead of revived, because
it was ending. It was not. The frames that *open* a connection are handed up before it is in
hardware and leave a candidate behind; the connection is made from the other direction's candidate,
and the next run finds the first still unread, with a SYN in it and now a connection to mark. So
ending is FIN or RST. A SYN on a connection that is in hardware is given back by the fast path as
well, and goes out anyway: its counter is not at the limit.

## What the last page left open in its accounting

Issue 306, two things, both on paths no test reaches:

- **Whether flows had used an association was inferred** from the engine's counts - the statistic
  the margin on the kernel's sequence counter exists to cover. It is now recorded where it happens:
  every place a microflow is made to name an association marks the record, makes the poll read it,
  and for the encrypting direction puts the margin on the kernel's counter there and then. Read
  back with `setkey -D`: one second after a rekey under a four-stream upload the successor stood
  at **1,048,576** - the margin exactly, before anything had been counted for it - and after the
  upload at 1,770,660: the margin, the 722,073 packets its flows had been counted for, and the 11
  frames the host handed over itself. An association no connection has named stays at 0.
- **An unanswered read of the index's counters was followed by `SA_ADD` anyway**, into the one
  command buffer the unanswered descriptor still pointed at. The install now stops there, before
  the kernel's cipher is touched.

## What review caught

Three readers after the first measurement, nothing that panics or forwards wrongly:

- a tunnelled connection taken out because it was ending was made again in the same pass from the
  very FIN that ended it - the rule that stops that covered only a candidate read after the
  connection had gone. Found by two readers independently;
- the poll's last write to an association's "has been read" flag was computed before its lock was
  dropped and could undo the one an attach on another thread had just made. Also by two;
- the tick arithmetic for the probe gap and the revive wait compared signed differences, which turn
  negative for a connection older than twenty-five days.

## Where it stands

- **On by default**, both settings, for plain connections as for tunnelled ones: the rule does not
  know the difference and neither does the fix. On a plain connection the revival has not been
  exercised: through this appliance's WAN the line gives about 150 Mbit/s and the rule did not
  fire. Plain downloads through its NAT ran as before - three of 100 MB, 198,705 frames forwarded
  by the coprocessor - and the connections that ended were taken out at their FIN.
- **Reviving is a bridge.** Each give-back still costs the two microflows, five seconds later, and
  two commands to attach their successors. Nothing was found that takes the terminal timer off.
- **A long burst of duplicate acknowledgements keeps a connection on the host**, by design: the
  wait doubles to half a second. In this lab a burst is over in a millisecond or two; across a path
  with tens of milliseconds of round trip it is not, and that has not been measured.
- **`ipsec.flows` 1 is as it was** for downloads: the direction in hardware is the acknowledgements
  themselves, and such a connection is left with the host for a minute after a give-back.
- **The first second of a connection** is the poll's. Making connections from the kick as well
  would need something that remembers what was refused, or every frame of a flow that cannot be
  accelerated would ask again
  ([issue 308](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/308), with the two things
  about the recovery that were not measured).

| under `dev.octep.0` | meaning |
|---|---|
| `dp.fast`, `dp.revive` | the two settings |
| `dp.fast_kicks`, `dp.fast_runs` | times the receive path asked for a run, and runs |
| `dp.flow_revived`, `dp.revive_refused` | connections rewritten in place; rewrites the far side refused |
| `dp.flow_closing` | connections found given back after a FIN or RST of theirs had been handed up |
| `rpc.wait_last_us`, `rpc.wait_max_us`, `rpc.wait_sum_us`, `rpc.wait_n` | how long answers take |

## Lessons

- **Time the wait before blaming what is waited for.** The far side answered in 22 microseconds
  and was being charged a thousand.
- **Ask what a failure is made of.** "The fast path gives connections back" was true and was a
  hundredth of the loss; the rest was the driver's own second.
- **A handler that does not check is also an interface.** The create command overwriting whatever
  is there was a single unconditional copy in the source, and it is the whole revival.
- **What is found to be permanent decides the design.** The terminal timer could not be argued
  with, so the work went into being quick twice instead of clever once.
- **Search for a constant in both of its encodings.** A week-old sentence in this tree said the
  fast path never builds a control message; it has nine builders, and the constant was written as
  its complement.
- **The first run of a fix is a review of it.** The SYN was in the log line the fix itself printed.
