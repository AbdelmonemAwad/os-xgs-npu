# A connection made between polls

Measured on an XGS 3300, 2026-10-08. The page before last left one thing outside hardware: a
connection's first second, because a connection was made by the once-a-second poll and by nothing
else ([issue 308](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/308)). This is that change.
It is also two faults that the change's own counters showed before it was ever switched on, both
older than the poll's cadence and both worth more than it:

- the poll's eight attempts a second were being **spent on refusals**, and a connection behind
  eight of them was never made at all
  ([#311](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/311));
- the table a connection's second direction is read from took **the wrong bits of its hash**, so
  connections from one machine to one server shared a slot and were made one a second
  ([#312](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/312)).

A third is found and left open: a connection whose own two directions share a slot is never made
([#313](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/313)).

## The counters came first

A connection is made by `octep_dp_flow_make`, which has a dozen ways of saying no, and for a plain
connection most of them were counted nowhere. Four are counted now - `pf` has no state, the other
direction has not been punted, there is no next hop, the other direction is ending - and the first
build carried nothing else that was switched on. Its first tables said this:

    not made: unsettled +8 no-state +5 other-not-punted +0 no-next-hop +51

Fifty-one refusals for want of a next hop in twelve seconds, with one download running. They were
the session's own traffic: the measuring machine has a second address that reaches the appliance by
a port the coprocessor does not own, and nothing sent from there can be accelerated.

## The poll, starved

The poll walks the 64 slots of the candidate table in order and stops after eight attempts. An
attempt was counted whether or not it sent a command - "counting only successes would let a run
of candidates that all fail walk the whole table", the comment said. So eight refusals in slots
ahead of a connection's own were the whole of every poll.

Forty-eight slow flows from that second address, one question a second each, beside downloads of
six seconds through NAT:

| | downloads made a connection | refusals for want of a next hop |
|---|---|---|
| nothing beside them | 4 of 4 | about one a second |
| forty-eight slow flows, before | **4 of 8** | **exactly eight a second** |
| forty-eight slow flows, after | 8 of 8 | thirty to forty a second |

The four that were not made crossed the host whole: 30,679 to 40,298 frames each handed up, none
forwarded. The one download of [the page before](a-state-that-sees-nothing-runs-out.md) that was
never made, and was not explained there, most probably met this; its tuple was not kept, so that
stays probable.

The poll now counts an attempt when it posted a command. The budget is there to bound the commands
a pass sends with the transmit path's lock held; a refusal that sends none costs a few lookups, and
the walk is sixty-four slots at most.

## The slot that neighbours share

On the first build that counted this way seven of eight were made, and the eighth was looked at by
its tuple. Both of its directions computed to slot 16 of the candidate table - which is #313 - and
so did the opening direction of the five downloads around it:

| source port | 55334 | 55342 | 64366 | 64373 | 64380 | 64388 | 64396 | 64403 |
|---|---|---|---|---|---|---|---|---|
| slot | 63 | 63 | 16 | 16 | 16 | 16 | 16 | 16 |

The slot was `(h >> 16) % 64` of a multiplied mix: bits 16 to 21 of the product, into which a
multiply carries nothing from above. The source port's second byte on the wire - the one that
changes from one connection to the next - is in bits 24 to 31 and never reached the index. Every
connection from one machine to one server in a run of 256 ports had one slot for its opening
direction, the slot holds one tuple, and a poll finds one of them in it.

The comment beside the function already said "a multiply and a shift of the top bits", and named
this very fault as the reason. The code now does what the comment says. Four streams at once
through the lab tunnel, their source ports consecutive, the appliance asked ten times a second
when each was made:

| | made at |
|---|---|
| before, through NAT (one run) | 0.7, 1.6, 2.6, 3.5 s |
| the slot corrected, the poll alone | 0.9, 0.9, 0.9, 1.9 s; 0.8, 0.8, 0.8, 1.7 s |
| and made between polls | 0.2, 0.3, 0.3, 0.3 s; 0.3, 0.3, 0.3, 0.3 s |

## Made between polls

The receive path already kicks a task when a punted frame belongs to a connection the table
holds. It now also asks for a connection to be made:

- **A candidate slot counts the frames it has taken in a row from the tuple it holds.** At the
  sixteenth, and each time the count doubles after that, the receive path marks the candidate due
  and kicks the task. A flow that cannot be made is asked about a dozen times in its first hundred
  thousand frames. TCP and UDP; not a frame that carries FIN or RST.
- **A run makes a connection only for a candidate that is due**, at most four a run and sixty-four
  a second. Those attempts are counted apart from the run's eight, so a burst of new connections
  cannot keep a given-back one waiting for its revival.
- **A slot whose attempt was refused is the poll's until the poll has been round**, one bit a
  slot; so are both slots of a connection that has just been taken out. Doubling alone forgets
  the moment another tuple touches the slot.
- A full table ends the making until the next poll and nothing else. A create the far side
  answers with a refusal is an answer; only one it does not answer keeps the runs away.

Everything else is as it was: the same function makes the connection, under the same conditions -
both directions punted, `pf`'s states on their long timer, a next hop for each direction. Two of
those conditions are a little stricter, because making sooner leans on them harder: the state's
timer class is asked as well as its peers, and a connection is not made while its other direction
carries a FIN or RST that has not been read.

`dev.octep.0.dp.fast_make` is 1; 0 leaves the making to the poll, which is how it was measured.

## Measured

The lab tunnel of the pages before, `ipsec.flows` 2, one build, the setting off and on by turns,
twice each. TCP payload forwarded by the coprocessor, and the frames handed to the host for a
microflow that is not active - which is every frame of a connection before it is in hardware:

| | the poll alone | made between polls |
|---|---|---|
| four streams down, 2 s | 87 %, 87 %; 28,966 and 28,632 frames | **100 %, 99 %; 631 and 622** |
| four streams up, 2 s | 100 %, 91 %; 38 and 31,367 | 100 %, 100 %; 308 and 53 |
| four streams down, 12 s | 95 %, 98 %; 31,366 and 12,212 | 99 %, 98 %; 968 and 1,170 |
| one stream down, 12 s | 99 %, 96 %; 6,894 and 8,218 | 99 %, 100 %; 129 and 229 |
| four streams down, 30 s | 98 %, 98 %; 31,134 and 30,111 | 99 %, 97 %; 2,318 and 2,482 |

And the rows of the pages before, on this build as it ships, against the build before it - which
had neither the corrected slot nor this:

| | the build before | this |
|---|---|---|
| four streams down, 12 s | 88 %, 147,505 frames handed up | **99 %**, 1,594 |
| four streams up, 12 s | 94 %, 124,098 | **100 %**, 91 |
| eight streams up, 12 s | 83 %, 348,042 | **100 %**, 437 |
| four streams down, 30 s | 94 %, 162,894 | 99 %, 2,299 |
| one stream up, a minute | 100 % | 100 % |
| one stream down, 30 s | 86 % | 95 % |

Most of that second table is the slot: four streams, and eight, are neighbours.

Through NAT, to a public server forty milliseconds away: thirty fetches of a megabyte, three at a
time - connections of a second and a half, the kind that was mostly never in hardware.

| | made a connection, of 30 | frames forwarded by the coprocessor |
|---|---|---|
| the old slot, the poll alone | 13, 17 | 32 %, 41 % |
| the slot corrected, the poll alone | 30, 28 | 84 %, 84 % |
| and made between polls | 30, 30 | **97 %, 98 %** |

A single long download through that line has few frames in its first second whoever makes it - 9
to 40 handed to the host before the poll made it, 2 to 48 before a run did - because a connection
across forty milliseconds starts slowly. The first second is worth having where the path is fast or
the connection is short.

Two of the eighteen single downloads traced were never made a connection. The one whose tuple was
kept had both of its directions in slot 24: #313 again, on the corrected function.

- A rekey under a four-stream upload, twice: no replay drop at the peer. The clear-text probe,
  unchanged: 0 of 20 delivered with `ipsec.flows` 0 and 1, 15 of 20 with 2. An index reused, the
  tunnel taken down and brought up under a connection: as before.
- 13,942 commands on this build, none unanswered: twenty-five microseconds on average, three
  milliseconds at worst.

What bounds it, measured through NAT with the setting on:

- **A scan** - two thousand tuples, a frame or two each: nothing asked for.
- **A flood that does reach the threshold** - two hundred UDP tuples to an address that does not
  answer, forty datagrams each in a burst: 75 attempts in 2.6 seconds, at most one a slot between
  two polls, none of which made anything.

## What review caught

Three readers and their verifiers on the first build, two more on the second; before any of it, a
critic set on the design. Nothing that panics, forwards wrongly or loses a state. What they found
is why the list above is as long as it is:

- the doubling forgot a refusal the moment another tuple touched the slot;
- a create the far side refused was taken for a far side that is silent, and cost every held
  connection a second of runs;
- every run took the transmit path's lock and searched the table once for each unread slot;
- a request outlived the connection it was made for, and the count of frames never started again;
- counting only attempts that cost a command, which was meant for the poll, had been given to the
  runs as well and left them with no bound of their own;
- a connection taken out while its frames kept coming was asked for again sixteen frames later -
  made, given back and taken out forty times a second where the poll made it once.

## Where it stands

- **A connection whose two directions share a slot is never made** - one pair in sixty-four. Found
  here, seen twice with the tuple in hand, not fixed: #313.
- **Two busy tuples of different connections in one slot** take turns in it, and both are made,
  later. One of four streams waited four polls for that in one row with the setting off.
- **A UDP stream in one direction** is not made any sooner: its state has to be three seconds old
  first, which is [the rule of the page before](a-state-that-sees-nothing-runs-out.md). Read, and
  not run again on this build.
- **The table has 1,022 places and nothing is evicted.** A connection leaves when `pf`'s state
  for it does. That a full table only stops the making was read and not run: nothing here filled
  it.
- **Sixty-four a second was not reached.** The most that was made in these runs was a few a
  second.
- **One row is not explained.** With `dp.revive` 0, which is an instrument, a single download
  was taken out once and not made again for six seconds; three repeats made it at once each time.
  The row did not record why it was refused, and the tool now does.
- **The two things about the recovery that #308 said were not measured still are not**: a path
  with a real round trip, and a plain connection.

| under `dev.octep.0.dp` | meaning |
|---|---|
| `fast_make` | the setting |
| `fast_make_log2` | frames in a row before a connection is asked for, as a power of two: 4 is sixteen |
| `fast_made`, `fast_make_tries`, `fast_make_refused` | connections a run made; attempts it spent; attempts that made nothing |
| `fast_make_spent` | times a run passed over a candidate that was asked for because the budget was spent |
| `flow_nostate`, `flow_wait_other`, `flow_nonhop`, `flow_other_closing`, `flow_unsettled` | why a connection was not made |

## Lessons

- **Count the reasons.** Four counters that name a refusal showed in a minute what a week of
  throughput tables had not: the tables said how much was in hardware, and nothing said why the
  rest was not.
- **A bound belongs on what costs.** The poll's budget was on attempts and its reason was
  commands. The comment gave the reason for counting failures and it was true of a table larger
  than sixty-four.
- **A hash meets its inputs as neighbours.** Over every port it filled the table evenly. The
  ports that arrive together differ in one byte, and that byte was not in the index.
- **A comment that describes the fix is not the fix.** The two were four lines apart, for weeks,
  and nobody put a number through the function to see which of them it was.
- **An end that is not yours is not a test bench.** Sixteen downloads at once from a public server
  made it stop answering, and three rows of the next table had not run. Everything parallel on
  this page is from the lab, where both ends are ours.
