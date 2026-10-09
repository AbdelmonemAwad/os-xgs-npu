# A connection that collides with itself

Measured on an XGS 3300, 2026-10-08: before on module `929c0246`, after on `92a8eeca`.
[Issue 313](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/313), found on
[the page before](made-between-polls.md) by a counter and two tuples, and left open there.

A connection is made when both of its directions have been punted: the frame in hand names one
microflow, and the other direction's is read from the candidate table, where its last punted frame
left it. The table has 64 slots and a slot holds one tuple. When a connection's two directions hash
to the same slot the slot holds whichever wrote last, the other is never there, and every attempt
ends at "the other direction has not been punted yet". **One pair in sixty-four, never made** - not
by the poll and not between polls - and every frame of it crosses the host for as long as it
lasts.

## Measured before

It had been seen twice with a tuple in hand, both downloads through NAT that were never made a
connection: 90,945 frames handed to the host with both directions in slot 16, and 62,717 with both
in slot 24.

To have it on demand: `pf` chooses the translated port, so a colliding flow cannot be chosen, but
it can be found. A hundred and sixty UDP flows are opened through NAT, the appliance is asked what
port each was given, and the slots are computed with the driver's own function. Three of the
hundred and sixty had both directions in one slot. Two of them, and one that did not collide, were
then driven for eight seconds each, a question and its answer every twenty milliseconds:

| | in the flow table | "the other direction has not been punted" |
|---|---|---|
| collides, slot 61 | **no** | 7 |
| collides, slot 57 | **no** | 5 |
| control, slots 35 and 31 | yes | 0 |

## One more entry

The frame in hand says which slot it is in, and `pf` says what the other direction's tuple is; so
the collision can be recognised at the moment it defeats an attempt. Then:

- **The side entry is armed** with the other direction's tuple - one more candidate, outside the
  table, for one tuple at a time.
- **While it is armed the receive path writes that tuple's frames there instead of the shared
  slot.** The direction that was in hand keeps the slot to itself, so its run of frames counts and
  it is asked about like any other; the direction that was missing is somewhere it can be read.
- **The lookup of the other direction falls back to the side entry** when the slot does not hold
  it.
- **It is let go** when the connection is made; or by the poll, two seconds after it was last
  wanted; or, whatever happens, eight seconds after it was armed for that tuple, which is then not
  given it again for half a minute - so that a stream whose other direction never comes cannot
  keep it from every other connection that needs it.

A frame of a connection the table already holds is never diverted. It goes to its slot and kicks
the task, as it did before, whatever the side entry is armed for.

## Measured after

The same search, on the build with the side entry:

| | flows that collided, driven | made a connection | the control |
|---|---|---|---|
| first build | 7 | **7** | made, each time |
| second build, after review | 6 | **6** | made, each time |

Each armed the entry once, had its other direction read from it once, and was made - by a run of
the kicked task in twelve of the thirteen.

Through the lab tunnel nothing is translated, so the source port can be chosen to collide:

| | made a connection | |
|---|---|---|
| UDP, fifty a second each way, both directions in slot 36 | **yes** | 334 of 334 echoed |
| UDP, both in slot 52 | **yes** | 336 of 336 |
| UDP control, slots 34 and 28 | yes | 344 of 344 |
| TCP download, both in slot 55 | **yes** | 783 Mbit/s |
| TCP download, both in slot 38 | **yes** | 776 Mbit/s |
| TCP download, both in slot 39 | **yes, late** | 424 Mbit/s over its six seconds |
| TCP control, slots 63 and 2 | yes | 785 Mbit/s |

The late one is the limit of having one entry: it was another tuple's when this download wanted
it - `cand_side_taken` counted six askings - and the download crossed the host for the two or three
seconds that took.

The tunnel suites of the pages before, on this build: as on the build before it - four streams
down 99 % forwarded by the coprocessor, four and eight up 100 %, single streams 99 and 100 %, a
rekey under a four-stream upload without a replay drop, the clear-text probe unchanged. The side
entry was armed 27 times in that session and read 18: with four and eight streams at once, one
pair in sixty-four is not rare. 10,029 commands, none unanswered.

## What review caught

Two readers and their verifiers on the first build. Nothing that panics, forwards wrongly or loses
a state; all of it about what the entry does when things do not go as the simple case goes.

- It could stay armed, or be armed afresh by a second thread, for a connection that had just been
  made - and then hide that connection's punted frames from the task that revives it. That is the
  rule that a held connection's frames are never diverted.
- Every receive thread tried the entry's one busy word for every punted frame while it was armed.
  The tuple is compared first now, and the rings that carry other tuples never touch the word.
- It was armed only at the refusal, which comes after the next hop is looked for - and for a
  destination on the bridge the next hop needs the very direction that is missing. A connection
  could be recognised as colliding only from one of its two directions.
- One tuple could hold it for ever: a stream whose other direction never comes, asked about every
  second, armed it again every second.
- A FIN or RST kept in the entry was never taken, and refused every attempt until the entry was
  let go.

## Measured again, with other users

The same evening and the next morning, on main (module `73d533b9`), for
[issue 322](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/322) - which is open, and whose rows
are in [the appliance's measurements](measurements/xgs3300.md#the-candidate-table-with-more-than-one-user).
The tables above stand as what those two builds did on an afternoon when nothing else wanted the
entry. This is what it does when something does.

| | |
|---|---|
| the test of this page again, two colliding flows and a control | 10 of 12 colliding flows made, where it reads 7 of 7 and 6 of 6 above |
| a colliding flow with a held connection in each bucket its two tuples count in | **0 of 7 made**; the entry armed 11 times and read 0 |
| every colliding flow of a few hundred, driven at once | one made a second or so: six took 5.9 s, and another six 8.0 s |
| three minutes with no test running | armed once, read 0; 1,338 attempts refused for a connection whose opening direction no ring carries |
| a tuple barred for half a minute | armed again six seconds later, after another tuple's turn |

## Where it stands

- **One at a time.** A second connection that collides with itself while the entry is taken waits
  for it: `cand_side_taken` counts the askings. Two hundred flows opened in two seconds, three to
  five of them colliding, three times over, counted two. (Driven at once and not one after
  another, five such runs counted 35, and the last flow of six waited 5.9 and 8.0 s: above.)
- **Once made, such a connection shares its slot again.** When its microflows expire and its
  frames come under new identities, each direction is attached from its own frame, a run apart,
  where another connection's are attached together; and a FIN in the slot can be overwritten by the
  other direction's next frame before it is read. Read in review, not measured.
- ~~**The limits were not reached**: no tuple held the entry for eight seconds in these runs.~~
  **The limits are reached all the time.** True of those runs and not of the appliance: an hour
  after a boot the entry had been let go at its eight seconds 29 times against 2 reads, and over
  the ten hours of light load that followed 83 times against 42 - by connections that come in by
  the management port and can never be made. Measured for issue 322, above.
- **Two busy tuples of different connections in one slot** are not this, and are as the page
  before left them: each is in the slot some of the time, and both are made, later.

| under `dev.octep.0.dp` | meaning |
|---|---|
| `cand_side_arms` | times the side entry was armed for a new tuple |
| `cand_side_hits` | times a connection's other direction was read from it |
| `cand_side_taken` | times it was wanted while it was another tuple's |
| `cand_side_barred` | times a tuple had held it as long as it may, and was let go |
| `flow_wait_other` | attempts put off because the other direction was not there |

## Lessons

- **A table sized by how often two things collide was not asked how often one thing collides with
  itself.** The two directions of a connection are not independent visitors to it: they are a pair
  that has to be there at the same moment.
- **Find the case, do not wait for it.** One connection in sixty-four came by twice in an
  afternoon and each time a tuple had to have been written down. Opening a few hundred flows and
  computing which of them collide gives three to drive on purpose, every time.
- **The second thing a fix must answer is what it does when it does not work**: armed for a
  connection that never comes, armed for one that already exists, wanted by two at once. Review
  found all of that and measurement had found none of it, because the measurement was of the case
  the fix was written for.
