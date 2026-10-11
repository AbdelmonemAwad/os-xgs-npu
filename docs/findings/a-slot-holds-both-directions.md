# A slot holds both directions

Measured on an XGS 3300: before on module `73d533b9` (main), 2026-10-08 and the morning after;
after on `f78812a8`, 2026-10-11, which is main with [a packet is not a buffer](a-packet-is-not-a-buffer.md)
and this change. (A first build of this change, `6896a1cc`, was measured on 2026-10-09 and every
one of its rows is withdrawn: a receive ring was behind the block on that boot, which is how that
page's fault was found. They are in
[the appliance's measurements](../measurements/xgs3300.md#the-same-rows-on-a-build-that-was-not-merged---withdrawn)
as taken, and nothing on this page rests on them.)
[Issue 322](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/322), read in the code of
[a connection that collides with itself](a-connection-that-collides-with-itself.md) by another
session and not measured there; and
[issue 326](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/326), which a soak found while this
was being written.

A connection is made when both of its directions are known: the frame in hand, and the other
direction's microflow identity read from the candidate table. One connection in sixty-four has both
tuples in one slot of that table, and a slot held one tuple. The page before this one gave such a
connection one more entry, outside the table, armed with the tuple that was missing. That measured
six of six on the afternoon it was written.

It was one place that every such connection had to take turns at, held by timers. This page is
what held it, and what replaced it: **a slot holds four tuples, and nothing is armed**.

## What held the one entry

A soak of main, ten hours at light load, with the side entry's counters in every line. In the first
four minutes with no test running: **armed 10 times, read 0, let go at its limit 3**. Over the
night, the tests of this page included: armed 721, read 42, limit 83.

`pf`'s states said who. The machine that drives the tests reaches the internet through the
appliance twice over, and one of the two ways in is the management port, which is not a
coprocessor port. A connection from there leaves by the WAN front port, so its reply direction is
handed up by the coprocessor and its opening direction never is. The driver tries to make it at
every poll and refuses it for want of a next hop - after arming the entry, when its two tuples
share a slot. The appliance's own connections do the same: a reply to its NTP query is handed up,
the query is not.

With the entry held like that, the test of the page before, repeated: ten flows of twelve made,
where it had been six of six.

## The hot table, on purpose

The receive path wrote a frame to the entry only if a count read zero: `dp_conn_hot`, 4,096
buckets counting the tuples of the connections the table holds. A bucket that is not zero says
some connection's tuple hashes there, and it was taken for "this tuple's connection exists". By
arithmetic that refuses one self-colliding connection in five with five hundred connections held,
and two in five with a thousand.

To have it without five hundred connections: a UDP flow through NAT whose two tuples share a slot,
found as before; then two more flows from source ports chosen so that each one's opening tuple
counts in the same bucket as one of the first flow's tuples, driven until the table holds them and
kept in use; then the first flow driven for eight seconds.

| | flows that collide with themselves | made a connection |
|---|---|---|
| a held connection in the bucket of each tuple | 7 | **0** - armed, never written, never read |
| the same runs, no neighbour | 6 | 4 |

## The bar that the next tuple lifts

A tuple that held the entry to its limit was to be kept from it for half a minute. What was kept
was whatever tuple was still written in the entry. A flow driven one way only - its other direction
never comes - with a second self-colliding flow driven both ways for three seconds in the middle:

| seconds | |
|---|---|
| 4 | armed for the one-way flow |
| 12 | let go at the limit, kept out |
| 14 to 17 | the other flow: armed, read, made |
| **18** | **the one-way flow has the entry again**, six seconds after it was kept out |
| 28 | let go at the limit a second time |
| 58 | armed again, thirty seconds later, nothing having used the entry in between |

## One at a time

Every flow found whose two tuples share a slot, driven together, and the first reading of the flow
table that showed each. Readings are half a second apart.

| | made by, in seconds |
|---|---|
| six through NAT | 0.5, 1.2, 2.6, 3.9, 4.6, 5.9 |
| six through NAT, again | 1.2, 4.0, 5.3, 6.0, 6.7, 8.0 |
| six through the lab tunnel, UDP | 1.2, 1.9, 3.2, 3.9, 5.3, 6.0 - the control beside them 0.5 |
| three downloads through the tunnel | 1.2, 1.2, 1.9 - with 95,652 decrypted frames handed to the host meanwhile |

And the slot itself, which is not the side entry's fault but is the same table: fresh flows whose
*opening* tuples share one slot, started together.

| flows in one slot | made by, in seconds |
|---|---|
| 3 | 0.5, 1.2, 1.2 |
| 5 | 1.9, 2.6, 4.6, 6.0, 8.0 |
| 8 | 0.5 to 5.9 |
| 12 | eight of the twelve in the eight seconds |

## A first fix kept the entry, and was dropped

It armed the entry under the table's lock, never for a tuple the table holds, so that the receive
path need ask nothing; kept four tuples out, each to its own time; and did not arm for a direction
whose way out is not by a front port. It compiled and was never installed. Three readers and a
skeptic went over it and every finding held; two of them were the end of it.

- A self-colliding connection that is merely idle - a keep-alive and its acknowledgement every ten
  seconds - arms the entry at every exchange, is never read, is let go three seconds later, and
  never reaches the limit, so it is never kept out. A few dozen of those hold one entry most of the
  time. Read, not run.
- "Its way out is not by a front port" is a question about where this host would send, not about
  where frames arrive. A second WAN with `reply-to`, or a lagg over two front ports, gets a no for
  a connection that used to be made.

A third condition on a mechanism is the moment to ask what removes the mechanism.

## Four places a slot

Two designers who did not see each other, one asked for the least change that is right and one
asked to remove the shared thing, came back with the same table. A judge read both against the
code and chose between them on the one point where they differed.

- **Sixty-four slots, four places in each, one word a slot.** The receive path takes the word -
  tried once, a miss is a clash as before - and writes the frame to the place that holds its
  tuple; failing that to a place nothing has been written to; failing that over the place written
  longest ago, to the tick, and of places written in the same tick the first. The choice and the
  write are under the one word. A word a place was the other design: it lets two rings decide at
  once, and a decrypted inner tuple can arrive on two rings across a rekey, which would leave one
  tuple in two places.
- **The two directions of a self-colliding connection stand in the slot side by side**, each with
  its own count of frames and its own FIN mark, and each is asked about at its sixteenth frame like
  any other. Nothing is armed, so nothing is held, timed, or kept out.
- **The side entry is gone**: its three functions, its three timers, its four counters, and the
  test in the receive path that asked the hot table.
- **The hot table has 65,536 buckets.** It is still asked whether a punted frame can belong to a
  held connection, by the one reader for whom a wrong yes costs only a wasted look: the kick, and
  the run it asks for. With four thousand buckets and a thousand connections held, two tuples in
  five that no connection holds read a perhaps, and each is a search of the whole table under the
  transmit path's lock by every run until the poll has read the candidate - four times as many
  candidates now. Three in a hundred with the wider table. That is arithmetic; `dp.fast_scans`
  counts the searches.
- **Every holder has the word inside a critical section**, the receive path too. A holder
  preempted inside it keeps every ring from writing any tuple of that slot.
- **A discard of the flows empties the candidate table.** A candidate is the identity of a
  microflow, and a revision bump discards every microflow.
- The make budget is spent with a compare and swap: a plain decrement by a run could write what was
  left of the old second over the poll's refill.

## Measured after

The same tests, module `f78812a8`, from a clean boot; the receive rings read level - no buffer
written and untaken, sixty-four tuples of sixty-four seen at once - before, between and after.

| | main | four places a slot |
|---|---|---|
| a held connection in the bucket of each tuple | 0 of 7 made | **3 of 3** (in one of the three only one neighbour could be held) |
| the same runs, no neighbour | 4 of 6 | 2 of 2 |
| the test of the page before, three times | 10 of 12 | **6 of 6**, the three controls made |
| every self-colliding flow of three hundred, driven at once: 5, 5 and 11 of them | one a second; the last of six at 5.9 s and at 8.0 s | **all five by 0.5 s; all five by 1.2 s; all eleven by 1.2 s** |
| attempts that found no other direction, over each module's local run | 284 | 25, of which 22 in the one run where the three hundred flows opened to find colliders were still being made |
| six self-colliding flows at once through the lab tunnel, UDP, twice | 1.2 to 6.7 s and 1.2 to 6.0 s | **every one by 0.5 s, twice**; the control 0.5 s |
| three downloads at once through the tunnel, twice | 0.5 to 1.9 s; 33,839 and 95,652 decrypted frames handed to the host | **every one by 0.5 s, twice**; 201 to 266 Mbit/s each |

The two local runs are not the same list of tests - main's has the test of the bar, the other has
the tests of several at once - so the last row is two counts, not a ratio.

Fresh flows whose opening tuples share one slot, never used before the drive:

| flows in one slot | main | four places | unread candidates that made room |
|---|---|---|---|
| 3 | by 1.2 s | by 0.5 s | 0 |
| 4 | | by 0.5 s | 0 |
| 5 | by 8.0 s | by 0.5 s | 6 |
| 8 | by 5.9 s | by 1.2 s | 63 |
| 12 | 8 of 12 in 8 s | **12 of 12**, by 1.9 s | 281 |

The last column is `dp.cand_evicted`, and it reads as designed: nothing at four, and from five on
the tuples take turns at a place. With more busy tuples in a slot than it has places the table is
what the old one was with two.

And a discard of the flows: `dp.cand_table` listed 256 places written before `rpc.fw_rev_bump`
and none after it, with no slot kept; five were written again within three seconds by what was
arriving.

## How old the other direction may be

A place keeps its tuple until the slot needs the room - on main, until the next other tuple came
to the slot. So the identity handed to a make can be older now, and the fast path expires an
identity nobody programmed. How soon, and what happens then: a UDP flow does one exchange, is
silent for a gap, and then only its opening direction is driven. `pf`'s state is settled after
three seconds, so the driver makes the connection from the frame in hand and the reply's candidate,
which is as old as the gap. After the drive one real question is sent.

| gap, seconds | made | the answer's direction attached again |
|---|---|---|
| 4, 6, 8, 9 | yes | no: the identity was alive, the answer came through hardware |
| 10, 12, 14, 20, 26 | yes | **yes: the identity was dead** (twice at 10) |
| on main: 4 | yes | no |
| on main: 12, 16 | yes | yes |
| on main: 20, and 8 in one run of two | no: the candidate was no longer in its slot | |

The board says its microflow timeout is ten seconds, and an identity dies at nine to ten seconds
of silence plus up to a poll. The far side does not refuse a dead one: it skips that microflow and
answers 0, the connection is in hardware in one direction, and the other attaches at its own next
frame. `dp.flow_rc_refused` did not move at any gap, on either module.

So `dp.cand_age`, which makes a make decline a candidate older than that many seconds, stays at 0.
Declining would leave a stream whose other side is silent on the host for as long as it is
silent; the dead identity costs one attach.

## A ping

[Issue 326](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/326). The soak watches
`dp.flow_tuple_mismatch` as a counter that should not move, and it went from 0 to 30 in the night:
every line an ICMP echo on its reply direction, the two port words exchanged. An echo has no ports
and `pf` gives it two, the message's id and its type, and the driver reads every echo frame with
the id first whichever way it travels. The reply's tuple was derived from `pf`'s state as a TCP
reply's is, where a port belongs to its address and changes ends with it.

| twelve echo requests through NAT | main | after |
|---|---|---|
| `dp.flow_tuple_mismatch` | +1 | +0 |
| attempts that found no other direction | +1 | +0 |
| made a connection | yes, from the reply's frame | yes |

Then six addresses, twelve requests each. Five answered twelve of twelve, with
`dp.flow_tuple_mismatch` and the attempts that found no other direction both at +0 across them and
a connection made for each. The sixth answered none, and its twelve attempts each found no other
direction - which is what a request with no reply looks like; in the run before it that address
had answered six of twelve. It answers from here half the time, and none of that is the driver's.

## At nine hundred connections

The row the issue's lesson asks for. Nine hundred UDP flows through the lab tunnel to an echo on
the far end, opened fifty at a time, each then kept in use with a datagram every four seconds;
`ipsec.flows` 2 for the length of the test.

| | main | four places a slot |
|---|---|---|
| the table stopped growing | 110 s after the first flow; 112 s from a clean boot | **33 s** |
| connections held | 895 | 905 |
| attempts by a run that made nothing | 637 | **2** |
| creates the far side refused | 0 | 0 |
| while held: runs between polls, a second | 3 to 17 | 0 to 23 |
| table searches that found nothing, a second | not counted | 0 to 3 |
| candidates evicted, clashes on a word, while held | | 0, 0 |
| commands a second, mean wait | about 510, 20 us | 2 to 982, 19 to 30 us; none timed out |
| one download through the same tunnel | 794 Mbit/s; 774 from a clean boot | 772 Mbit/s |

No command went unanswered on either. The download is one eight-second sample on each. The
receive rings read level before and after - no buffer written and untaken, sixty-four tuples of
sixty-four seen at once - and the statistics messages that row sends were taken whole: 110 of
them, the longest 8,468 bytes, six buffers.

## The rows of the pages before

On this build, the same day, after the row above. The three settings of `ipsec.flows`: four
streams 948 to 960 Mbit/s down and 950 to 954 up, one stream 744 to 783 down and 746 to 773 up;
the six checks and the clear-text probe as they read before. The hand-back rows: 940 to 960 for
four streams down, 752 to 783 for one, 944 to 945 for four over thirty seconds. Three rekeys under
a four-stream upload at 947 to 951 with nothing dropped at either end. Over the boot, no buffer
left written, `dp.oq_lapped`, `dp.rx_resync`, `dp.rx_short`, `dp.oq_unseen` and `dp.rx_long` 0,
and `dp.rxwd_stalls` 2 in the suites, as on the merged build of the page before.

## What the soak said

Ten hours on main at light load, 600 samples, a download every ten minutes: no pass over a lapped
ring, no step of a read index, no stall, no command unanswered or refused, 1,958 connections made.
Of 64 downloads 60 carried data to their six-second cut and four received nothing; those four, and
the five small fetches that failed, each fell inside an alarm of the upstream firewall's own
monitor of its line, and nothing the driver counts moved in those minutes but attempts on
connections that were not established. One connection was given back by the fast path's
retransmission rule in the night and revived in place. What it found is the two issues on this
page. The run is in [the appliance's measurements](../measurements/xgs3300.md#ten-hours-at-light-load).

## What review caught

Before the second version was written, the design round above. On its first build, three readers -
the locks and the memory, the behaviour against main, every sentence against the code - and a
skeptic on five findings. Nothing in the locks: one reader ran the choice of a place through every
combination of empty, same and other in a model, 19,200 cases. What they found:

- The age column of `dp.cand_table` wrapped at seventy-one minutes.
- A dozen sentences still described one tuple a slot and a walk of sixty-four.
- `dp.cand_evicted` does not see a candidate the poll has read making room, and the make would
  still have used it. The description says so now.
- The age test was to be decided by a measurement that could not see a dead identity: "made" is
  the host's own table, and the far side answers 0. The tool sends its one question after the
  drive because of that.
- A revision bump left every candidate of before it in the table.
- The receive path's watchdog writes candidates from a callout and was the one holder of the word
  outside a critical section.

Between that build and this one main took the fix of [a packet is not a buffer](a-packet-is-not-a-buffer.md),
whose changes are in the ring service and not in the table. The change applied onto it without a
conflict, and its lines are the same as the ones that were read.

## Where it stands

- **More than four busy tuples in a slot take turns at a place.** They are made later, by the
  poll, as two were on main. A tuple that keeps coming makes room only when every place was
  written in the same tick as its own; then the first place goes.
- **A true never remains**, by arithmetic and not measured: three flows that can never be made,
  each handed up at least once a tick, in the slot of a slow self-colliding pair. One connection
  in about 150,000 with ten such flows.
- **The held bit is still a slot's.** A refused attempt between polls leaves every tuple of that
  slot to the poll for the rest of the second - four of them now.
- **The poll's walk is 256 places.** A refusal that posts no command is not counted toward its
  eight, and what a pass of 256 refusals costs has not been timed.
- **The hot table's three in a hundred is arithmetic.** At nine hundred held connections the runs
  searched the table for nothing at most five times a second; a row with thousands of tuples a
  second that no connection holds has not been run.
- `dp.cand_evicted` counts every overwrite of another tuple's candidate while `dp.auto` is off,
  because nothing reads the table then.
- **Plain forwarding through the host at a gigabit** is still
  [issue 320](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/320).

| under `dev.octep.0.dp` | meaning |
|---|---|
| `cand_table` | the candidate table: each place, its tuple, its age, whether it has been read |
| `cand_evicted` | unread candidates overwritten by another tuple: the slot was full |
| `cand_lost` | unread candidates overwritten by any frame, their own tuple's included |
| `cand_clash` | frames that found a slot's word taken |
| `cand_kept` | slots a discard of the flows could not empty |
| `cand_age`, `cand_aged` | the age limit, 0 for none; makes it declined |
| `fast_scans` | table searches by a run that found no connection for a candidate nobody asked about |
| `flow_wait_other` | attempts put off because the other direction had no candidate to use |

The four `cand_side_` counters are gone with the entry.

## Lessons

- **One shared place with a timer on it is measured with its other users present.** Six of six was
  an afternoon when nothing else wanted the entry. The other users here were the measuring
  machine's own connections, coming in by a port the driver does not own - which an earlier page
  had already written down as something that cannot be accelerated.
- **When review finds a third condition to add, ask what the mechanism is for.** The entry existed
  because a slot held one tuple. Three patches made it safer and none asked the slot to hold two.
- **A test reads what it can see.** "Made" was the host's table; the far side had dropped the
  identity and answered 0. One question after the drive told the two apart.
- **A counter that should not move must not be moving for another reason.** Thirty harmless
  increments in a night are how the real one gets read past.
