# A ring that fills

Measured on an XGS 3300, 2026-10-08.
[Issue 297](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/297): with the kernel's cipher, a
tunnel that ends on a 10 Gbit/s front port ran at 149 Mbit/s down and 256 up, and the kernel
dropped ESP frames by the thousand as replays - where the coprocessor, checking the same window on
the same frames at the port, refused 44 in a million.

They were put out of order by this driver's receive ring, whenever the ring had been left to fill -
and until [a visit is not a pass](a-visit-is-not-a-pass.md), earlier the same day, a busy ring was
left to fill twenty times a second.

## The rows of the issue, three times

Four and one TCP streams through the lab tunnel for twelve seconds, `dev.octep.0.ipsec.on` off -
the kernel's cipher, which is the default - and strongSwan's replay window of 32.

| | main before #319, `9ecc3bd5` | after #319, `e139dd09` | this change, `10addf02` |
|---|---|---|---|
| four down | 346 and 297 Mbit/s, 2,703 and 2,977 replay drops | 953 and 956, 45 and 46 | 967, 30 |
| four up | 337, 21,709 replay drops | 947, none | 956, 1 |
| one down | 249 | 785 | 791 |
| one up | 173 | 772 | 805 |

The first column is the issue's own row measured again the same evening. **The rows went with
#319**, which stopped rings being left - and the thing that reorders was still there, one full ring
away.

## Where the ESP goes: one ring

The issue's first candidate was the fast path spreading an association's frames over the host's
rings. It does not. The interrupts each ring's vector took over a one-stream download: one vector
276,446 and one other 45,630, which is the acknowledgements coming back from the LAN; the other
six, 19 to 39.

## The order the driver hands them up

A capture on the tunnel's port on the appliance - the tap is fed as the driver hands a frame to the
stack - and the sequence number of each ESP frame in arrival order:

| | of 40,000 frames, behind a higher number | of those, by more than 32 | the furthest behind |
|---|---|---|---|
| before #319, four down | 920 in one capture, none in the other | 843 | **1,023** |
| before #319, four up | 8,107 | 8,107 | **1,023** |
| after #319, four down | 14 and 23 | 1 and 0 | 568 and 29 |
| after #319, four up, one down, one up | 2, 0, 0 | 0 | 1 |

1,023 is the ring less one. On the module before #319 the driver's read index had been stepped past
a gap 18 times, over 1,679 buffers, by the end of those five rows (`dp.rx_resync`,
`dp.rx_skipped`, since the module loaded); on the one after it, not once in 61.6 million frames -
until the ring was made to fill. With the quiesce a detach runs held for a tenth of a second three
times during a four-stream upload, on the module after #319: the index stepped 3 times over 815
buffers, 813 frames arrived behind a higher number, 802 of them by more than 32, and the kernel
dropped 802 as replays.

## What a full ring did

It needs no tunnel. Numbered UDP datagrams from the LAN machine to the appliance's own address,
120,000 a second, the quiesce held for a tenth of a second, and a trace from the driver - a
throwaway build of main - of every pass over the ring that carried them: the read index, the count
the register gave, the doorbell, and for each frame the buffer it came out of and its number.

When the host came back:

```
read index 184   count 12,910   doorbell 3,472
took 1,024:   buffers 184-844   numbers 249832-250492
              buffers 845-183   numbers 249469-249831      363 behind a higher number
```

**The block had written 12,910 packets into a ring of 1,024 buffers.** It does not stop at a full
ring. It goes round, over frames nobody has read, and the host then reads the buffers in index
order - which after a lap is the newest frames first and the older ones behind them. From there the
read index and the place the block writes no longer agree: the host finds its index empty with full
buffers ahead of it, the frames in those wait until the index comes round, and after two ticks the
driver steps over the gap. Of that run's datagrams 11,950 were never delivered, and nothing this
driver counts moved for them; 701 arrived behind a higher number, the furthest by 1,023; and the
count register was left 11,180 too high, so every visit after it ended on a count with no buffer
behind it.

## Why it did not stop: the credit, and a watermark

A ring's doorbell, `R_OUT_SLIST_DBELL`, is what the host tells the block it may use. This driver
granted a ring sixteen units for each buffer and, having measured that the block spends one,
held the credit to a ceiling of sixteen times the ring. So the block always had credit for sixteen
rings, and a full ring was nothing it could notice.

What the block does with the doorbell was measured on that trace, by setting the ceiling and
reading the registers when the host came back:

| ceiling on the credit | the doorbell | the count | |
|---|---|---|---|
| 1,024 | 1,008 | 7 | the ring delivers sixteen at a time |
| 1,040 | 1,008 | 24 | |
| 2,048 | 1,008 | 1,028 | more than the ring holds |
| 4,096 | 1,008 | 3,086 | three times round it |
| 16,384, the driver's own | 3,472 | 12,910 | and still going |

It takes buffers sixteen at a time for as long as the doorbell reads 1,024 or more, and stops below
that. On a ring published at 512 entries the doorbell stopped at the same 1,008 under a ceiling of
1,520, and with the ceiling at 1,008 the ring delivered nothing at all.

**The 1,024 is the coprocessor's output watermark**, `SDP_OUT_WMARK`: the block stops sending to a
ring whose doorbell reads under it. The host cannot reach that register. The coprocessor's own
kernel driver writes it when it configures the port - `0x100` in the stock source, `0x400` in
Sophos's patch to it - and on this appliance the vendor's own register tool, run on the
coprocessor's console, reads `0x400`. It was measured here before it was looked for, in a file this
project had already read for something else.

That is also what the first weeks of this ring look like now. A grant of 256 for a ring of 256
delivered one packet and stopped; sixteen times that ran; and the reading was that the doorbell
counts sixteen for a buffer, the size of a scatter-list entry. The block spends one. 256 is under
the watermark and 4,096 is over it - which fits everything that was measured then, and was not
measured again.

## What it is now

- **A ring is granted its own size and the watermark, less one fetch**: 1,024 + 1,024 - 16. The
  block fetches while the doorbell reads the watermark or more, so the last fetch it can make is
  the one that leaves the doorbell sixteen under it, and that is a ring's worth: credit for exactly
  the buffers the host has taken.
- **One unit is returned for a buffer taken**, which is what the block spent on it and what the
  vendor's host returns. `dp.credit_unit` only reports it now.
- **Credit that is not returned is kept.** Exact has no slack: nothing tops a ring up any more, so
  credit a pass cannot return - the ceiling lowered on a live ring - used to be gone, and the ring
  with it. Each ring carries what it is owed, and every pass, one that takes nothing too, gives
  what the ceiling has room for. A buffer the read index is stepped over that held something is
  owed the same way.
- **The watermark is a setting**, `dp.oq_wmark`, because it is this firmware's: an image that
  writes the stock value would be granted 768 buffers too many by the default.
- **A ring cannot be published larger than the buffers behind it**, nor in anything but whole
  fetches, and `dp.ring` cannot be moved while the rings are up or past the state the driver keeps.
  A throwaway build of this work that halved the allocation under a bring-up that publishes 1,024
  had the driver writing its poison over the kernel's memory: two panics, 153 and 279 seconds
  after booting ([issue 323](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/323)).
- `dp.oq_lapped` counts a pass that reads a count larger than the ring. It should not move.

With that, the same tenth of a second, on the first build of this change with the trace:

```
read index 888   count 1,000   doorbell 1,016
took 1,000:   buffers 888-863   numbers 228089-229088      none behind
```

1,000 waiting, in order, the count right, no gap and no step. What did not fit was refused by the
coprocessor, which is where a full ring should be felt and where it is counted: on the build of
this change its `TX_DROP_QUEUE_FULL` moved by 10,260 across one such hold.

And a ring starved on purpose - the ceiling lowered under the watermark while 119,000 datagrams a
second arrived - received 57,125 frames in the half second before, 12 in a half second with the
ceiling down, and 61,156 in a half second starting a fifth of a second after it was given back.
Before the credit was kept nothing in the driver could have brought it back: credit was written
only by a pass that had taken something. That is read in the code; the ring that was starved on
that build was not watched after its ceiling was restored.

## Measured after

Module `10addf02`. The commit differs from it in two sentences of comment and description and
builds as `73d533b9`; that was booted and the two local runs above repeated on it - 57,115, 9 and
60,324 frames, and 9,942 refused by the coprocessor across a hold.

**The ring made to fill**, through the tunnel with the kernel's cipher: the quiesce held a tenth of
a second three times a row, six rows, 160,000 ESP frames captured in each.

| | the read index stepped | behind a higher number | by more than 32 | the kernel's replay drops |
|---|---|---|---|---|
| after #319, four up | 3 times, 815 buffers | 813 | 802 | 802 |
| this change, four up, twice | never | 9, 8 | 0 | 0, 1 |
| this change, one up, one down | never | 0, 2 | 0 | 0 |
| this change, four down, twice | never | 245, 337 | 6, 8 | 47, 39 |

The four-stream downloads read the same with nothing held - 10 of 40,000 behind, 1 by more than 32,
30 replay drops - and did before this change, where the same holds stepped nothing in a download
either: that disorder is on the wire. So the row this table stands on is the upload. And nothing
in the tunnel rows of this build says a ring reached full - the coprocessor counted no frame
refused in the upload rows, the sender having stopped for want of acknowledgements long before a
tenth of a second was up. That a full ring is read in order rests on the trace and on the hold
under numbered datagrams above.

In the two four-stream uploads with holds the far end dropped 13 and 7 of the appliance's own
frames as replays, and none in the rows without. Those are frames the kernel's cipher numbered and
then sent late: what a resume hands up after a hold is sent from the thread that lifted the
quiesce. Not followed further.

**The rows of the pages before**, coprocessor's cipher, `ipsec.flows` 0: one stream 776 up and 794
down, four 951 and 961, eight 970 and 989. The three settings of `ipsec.flows`: 736 to 801 for one
stream and 923 to 960 for four in every one, the clear-text probe as it read. Three rekeys under a
four-stream upload at 947 to 950 with nothing dropped. The quiesce cycled 2,864 times in eight
seconds under one stream at 759 Mbit/s and 2,879 times under four at 943.

Over that boot, 47.7 million frames: `dp.oq_lapped` 0, `dp.rx_resync` 0, `ipsec.out_drop` 0, one
pass that found a count and no buffer, and `dp.rxwd_stalls` 1 - below.

## What the watchdog calls a stall, again

With the credit right the block holds packets back when the ring is full and writes them in one
burst when buffers are handed back. `dp.rxwd_stalls`, which
[a visit is not a pass](a-visit-is-not-a-pass.md) introduced as the number that should not move,
counted 2 and then 3 on the first builds of this change in the rows where the ring was made to
fill, and two things were changed for it:

- a stall is now also a ring that no servicer but the watchdog has been inside for two ticks. A
  tick that lands between a burst and its handler finds thirty-two packets in a ring whose servicer
  left it microseconds ago;
- after a quiesce the watchdog can reach a ring before the resume's own sweep does, and what it
  finds there was held by the quiesce. Resume marks every ring as having asked for the visit.

That those were what the 2 and the 3 were is a reading of them, not a trace. With both, the eighteen
holds counted none. And over the suites that followed, 1 was counted in 18 million frames: a tick
that lands between a burst arriving on a ring that had been idle and its handler still meets the
definition, and whether that one was that was not established. The quiesce-cycling rows cannot
count a stall at all - every resume marks every ring - so their zero says nothing.

## What was read wrong before

- **"The doorbell counts bytes of the scatter list - sixteen per buffer."** It is in the README, on
  the family page, on [the road](the-road.md) and on the capture plan, as the finding that opened
  the last hop. What was measured then stands: a grant of 256 delivered one packet, and sixteen
  times that ran. The reading does not. The block spends one unit on a buffer - measured when the
  credit was first given a ceiling, and written in the driver then - and sixteen times the ring
  was a grant over the watermark, with fifteen rings to spare.
- **"It is not the cause of the stall"**, on the family page, of a ring granted "a full 256" that
  still delivered one packet: 256 is under the watermark, which fits. It was not run again.
- **"Why the kernel's cipher is slow here"**, on
  [one encryptor per association](one-encryptor-per-association.md): the frames were not spread
  over the host's receive rings. They were on one, and the block had been round it.
- **The comment on the issue that located this** said the frames turn up in the buffers the driver
  stepped over. They were already in the ring, ahead of a read index the block had passed.
- **The first draft of this page** said nothing that had been read explains the 1,024. The
  coprocessor's driver does, in a file on the same disk; a reader of the draft found it.

## Not in it

- **Another firmware.** The watermark is this image's. The default is right for it and wrong for
  the stock value; the driver cannot read the register, and nothing checks the setting against the
  coprocessor when the rings start. `dp.oq_lapped` moving after a hold, or a ring that delivers
  nothing, is how a wrong one would show.
- **Rings the port's channels do not cover.** The watermark stops the block through backpressure
  the coprocessor enables for the channels of its port. It was traced on one ring and exercised on
  the rings the tunnel rows used.
- **The vendor's slack.** Its host grants a ring its own size when it enables it and one for each
  buffer it refills, and the appliance's own firmware ran rings of 4,096: against the same
  watermark that leaves the last thousand buffers of a ring never fetched. This driver's grant
  leaves none. Read, not run.
- **The disorder on the wire** - 30 to 50 replay drops in a four-stream download at a window of 32.
- **Plain forwarding through the host at a gigabit**, which is still
  [issue 320](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/320). What a load the host
  cannot keep up with does is different now - the coprocessor drops and says so - and has still
  only been seen for a tenth of a second at a time.

## Lesson

A number that makes the thing run is not yet the number. Sixteen times the ring ran from the day
the last hop opened, and it ran because it was large: the question that was not asked is what the
smallest grant is that still runs, and what the block does with the rest.
