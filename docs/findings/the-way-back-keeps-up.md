# The way back keeps up

Measured on an XGS 3300, 2026-10-11: module `f78812a8` (main) for the first rows, `c12b6dfd` -
main with the two instruments this page adds - for the rest.
[Issue 320](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/320), the list of what the receive
path had not been measured at, and the two things
[a packet is not a buffer](a-packet-is-not-a-buffer.md) left on it: what a ring does when it is
behind the block by its whole grant, and whether a busy ring that is behind ever comes back.

Both were put to the instrument that page left for the purpose, `dp.oq_lose`: a pass that reads a
count acknowledges one packet and does not take it, which is the fault itself, made on purpose,
on one ring. What was measured is what the way back - a pass that finds the next buffer written,
and still written two ticks later with nothing taken in between, takes what was there and
acknowledges nothing - does under it.

## A slow stream, two thousand lost

Numbered datagrams from a machine on the LAN to the appliance, on a tuple the fast path hashes to
ring 0, four milliseconds apart, with the interface's own tap saying what was handed up and in
what order. 2,100 to be lost, one a pass.

| | |
|---|---|
| lost during a stream of 3,000 | 1,489, in 1,489 passes |
| brought back during the same stream | 1,489, one a walk |
| buffers written and not taken, read every second | never above 1 |
| datagrams handed up | 3,000 of 3,000, in the order sent |
| the ring's doorbell, of 2,032 | 2,018 to 2,030 throughout |

A packet the instrument leaves behind is at the read index; the next counted packet's pass takes
it first, and the ring is one behind; the pass that ends that visit reads a count of nothing with
a written buffer under it and starts the wait; four milliseconds later the next visit finds the
wait over and takes what was there. One lost, one back, every time. The ring never gets further
behind than one, and nothing is late by more than a packet.

(A first run of this row read 337 datagrams missing. They were sent after the tap had closed: the
tap ran fourteen seconds and the stream fifteen and a half. The tool now keeps the tap open as
long as the stream.)

## A busy ring

The same, with the datagrams sent as fast as the machine sends them - a send costs 0.7 ms there
whatever it is asked for, so 1,400 a second - for 42 seconds, 2,100 to be lost:

| | |
|---|---|
| lost during the stream | 1,463 |
| brought back during the stream | 1,463, in 469 walks - three at a time |
| buffers written and not taken | never above 1 |

So a ring with a packet every 0.7 ms is not a ring that never has two ticks with nothing taken.
A visit ends on a pass that reads a count of nothing; that pass starts the wait; and the next
packet's visit, when the wait is over, takes the packets that were there before it takes its own.
What the page before called a busy ring that waits for a gap is a ring at a rate this machine
cannot send at. Not measured: a ring at ten thousand a second, which the sender here cannot make.

## Bursts

One a pass loses no faster than the way back recovers, and a pass can only lose what it counted:
at four milliseconds a packet every pass counts one, so bursts of sixty-four lost one. A ring is
put a whole grant behind only where a pass counts hundreds, which is a download onto its ring
through the tunnel:

One download through the lab tunnel with `ipsec.flows` 0, every frame of it crossing the host:
780 Mbit/s, 105,000 frames a second, all on one ring. The ring is found while the download runs
- a connection hashes to a ring of its own, so it has to be the download that is measured - and
the instrument is put on it three seconds in: 3,000 to be lost, sixty-four a pass.

| | |
|---|---|
| the instrument spent | 3,000 of 3,000, within four seconds |
| brought back | 3,000, in **18 walks** |
| buffers written and not taken, read every second | 18 before the instrument, 42 at the most under it, 0 at the end |
| the download | 780 Mbit/s over its thirty seconds; 775 for a second one after |
| `dp.oq_unseen`, `dp.rx_resync` | 0 |
| `dp.rxwd_stalls` | +4: the watchdog's walks that brought back thirty-two or more at once |

A pass that counts a hundred and loses sixty-four leaves the ring sixty-four further behind each
time, and the way back cannot run while every visit ends with something taken. What gives it its
two ticks is the stream itself: one TCP stream pauses for a few milliseconds every few
milliseconds, waiting for its acknowledgements, and in each pause the ring is brought level. So
the ring was never more than forty-two buffers behind at any reading, and a download that lost
three thousand frames to the instrument ran at 780 Mbit/s.

Not reached, then: a ring behind by its whole grant. It needs a stream with no pauses at all -
datagrams at line rate from the far end, through the tunnel - which is the load the host cannot
keep up with that [issue 320](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/320) lists
separately. With TCP the way back is always given its gap.

## Order

Whether what the way back hands up is in the order it was written: a control stream of 3,000 with
nothing lost, 3,000 in order; 300 lost one a pass, in order; 2,100 lost, in order. One earlier run
with 2,100 lost, before the tool counted inversions, printed that the order was not kept, and the
run after it, the same row, that it was. Not reproduced; recorded.

## The stalls, named

`dp.rxwd_stalls` is the count of visits by the watchdog that found a stall's worth of packets on a
ring nobody had been inside for two ticks: the number [a visit is not a pass](a-visit-is-not-a-pass.md)
introduced as the one that should not move. It moved - 0, 2 and 2 over three builds of the page
before, twenty million frames of the same suites each - and a count says neither which ring nor
what was waiting. `dp.rxwd_stall_log` keeps the last eight: how long ago, the ring, what the first
pass took, how many ticks nobody had been inside, what the whole visit took.

Read on this build over the rows above and the three standing suites, run twice:

| when | ring | the first pass took | ticks nobody had been inside | the visit took |
|---|---|---|---|---|
| under a download, before the instrument was armed | 0 | 50 | 4 | 50 |
| in the rekey suite, first run | 7 | 237 | 12 | 603 |
| in the rekey suite, second run | 1 | 96 | 5 | 270 |
| under the instrument, four times | the download's ring | the way back's walks | | |
| in four rekeys run for this table, at the first of them | 2 | 257 | 12 | 624 |
| in six more, with the sampler kept for every one, at the fourth | 1 | 109 | 6 | 320 |

The three-mode tunnel suite and the hand-back suite counted none, twice over. Both stalls in the
suites are in the rekey suite - a four-stream upload with a new association installed five
seconds in - and both are on a ring that had a servicer inside it five and twelve ticks before,
with two to six hundred packets waiting when the watchdog came. That is not a tick landing
between a burst and its handler on an idle ring, which is what the count had been put down to:
the handler had been there, left, and did not come back for twelve ticks while six hundred
packets arrived. What keeps it away for that long at a rekey is the next thing to find; the
record is what it was found with.

The first row, 50 on ring 0 four ticks after a servicer let go, during a download that was on
another ring, is the control messages' ring taking a burst of statistics - fifty messages for
the connections the download moved - and reads as the harmless kind.

Four more rekeys, run for this table alone - the four-stream upload with a new association five
seconds in, the peer capturing the ESP around the change, the log read before and after each, and
a sampler on the appliance every 100 ms across each - gave the last row: one stall, at the first
rekey, ring 2, 257 in the first pass, twelve ticks, 624 in the visit; none at the other three.
That is three stalls at a rekey in six rekeys, each on a different ring - 7, 1, 2, where the four
streams hash - two of them twelve ticks and one five, and no stall anywhere else in the suites.
The peer dropped nothing to replay in any of the four, and the uploads ran at 908 to 950 Mbit/s
through them. The sampler of a rekey without a stall never read more than 29 buffers written and
not taken on a ring, in the upload's first second, and single digits after. The sampler of the
rekey with the stall was lost: the tool read the appliance's log through its csh with a `$(...)`
in the command, which printed an error where the readout should have been, and each rekey's
sampler overwrote the one before. The stall is in the appliance's own log, which keeps the last
eight; the tool now reads through `sh` and keeps every sampler.

Six more rekeys with that tool, 2026-10-12, every sampler kept: one stall, at the fourth - ring 1,
109 in the first pass, six ticks nobody inside, 320 in the visit - and the sampler has its moment:
at the 100 ms reading of the install, ring 1 held 95 buffers written and not taken, the other
rings 3 to 9. That is four stalls at a rekey in twelve rekeys now, on rings 7, 1, 2 and 1, five to
twelve ticks each. The sampler also caught the other kind of moment: at the second rekey ring 2
held 203 written and not taken at one reading and no stall was counted, because a servicer was
inside it - a backlog at a rekey is ordinary, a stall is a backlog nobody is inside. The six
uploads ran at 932 to 954 Mbit/s; the peer dropped one packet as a replay at the first rekey and
none at the other five; the installs took 5,008 and 1,176 microseconds at the first and the
fourth, and 28 to 95 at the rest. One thing was checked and does not fit: an install holds the
driver's lock across its command to the coprocessor, and the termination of every decrypted frame
takes that lock - but the fourth rekey's handler was away six ticks for an install of 1.2 ms, and
the first's install of 5 ms kept no handler away at all. Still not explained.

## What is not established

- A ring at ten thousand packets a second with the instrument on it. The sender here makes
  1,400.
- The one run that read out of order.
- **What keeps the handler away from a ring for five to twelve ticks at a rekey.** ~~Three stalls
  in six rekeys~~ Four stalls in twelve rekeys, on the streams' rings, each with a hundred or more
  waiting, none outside a rekey; the log names them, the sampler has caught one at its moment, and
  neither explains them. The install's hold of the driver's lock was checked against the
  durations and does not account for them on its own.
- The rest of [issue 320](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/320): plain
  forwarding through the host at a gigabit, which needs a far end on a front port outside the
  tunnel's selectors; and the watermark, which the host still cannot read.

| under `dev.octep.0.dp` | meaning |
|---|---|
| `oq_lose`, `oq_lose_ring` | the instrument: acknowledge this many packets on that ring and do not take them, counting down |
| `oq_lose_burst` | how many of them one pass may spend, at most what it counted; 0 is one |
| `oq_behind`, `oq_behind_runs` | packets taken without a count, and the walks that took them |
| `rxwd_stalls` | the count, as before |
| `rxwd_stall_log` | the last eight stalls: when, which ring, first pass, idle ticks, the visit |
