# A visit is not a pass

Measured on an XGS 3300, 2026-10-08.
[Issue 298](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/298), open since
[one encryptor per association](one-encryptor-per-association.md): one TCP stream through the
tunnel ran at 75 Mbit/s where four filled the link.

It was not the tunnel, the window, the round trip or the load tool. It was the way the host looks
at a receive ring, and it was there for every packet the host carries - which, as the appliance
boots, is every packet.

## What it looked like

One stream sent up through the lab tunnel for ten seconds by a second generator - one blocking
socket, a 256 KB buffer sent in a loop - on main, module `9ecc3bd5`, the host handing every packet
to the coprocessor (`ipsec.flows` 0). Not 75: **360 and 169 Mbit/s**, and second by second

```
195  41 517 679 220 488 362 379 415 297
358 197 497 411  37  39  39  37  39  37
```

A capture at the far end, on the decrypted side:

| | |
|---|---|
| the window the receiver advertises | 1 MB, median 1,046,016 bytes - never near the limit |
| the handshake's round trip | 0.6 ms |
| the segments | 1,406 bytes each, in order |
| how they arrive | in bursts, with pauses of **53 ms** between them: median 52.6 and 53.3, ninetieth centile 53.6 and 53.8 |
| the pauses, of the time captured | 2.19 s of 2.64 in one run, 0.92 s of 1.38 in the other |

After a pause the next segment is the next byte. Nothing was lost and nothing was sent twice: the
stream stood still for fifty-three milliseconds and went on.

## Fifty milliseconds

`OCTEP_DP_RXWD_TICKS` is `hz / 20`. The receive watchdog is a timer that looks at every ring
twenty times a second, whatever the interrupts are doing, and `dp.rxwd_runs` counts the times it
found packets. It read 10,477 after forty-two minutes, and under that one stream it moved 196
times in ten seconds - every period.

The period is a setting that can be written while the appliance runs. At one tick, same module,
same stream:

| `dp.rxwd_ticks` | one stream up | the watchdog found packets, in ten seconds |
|---|---|---|
| 50, the default | 360, 169 | 196 times |
| 1 | **798, 769** | 7,305 and 6,527 times |

So the stream was being carried by the timer, at whatever pace the timer kept.

## The mechanism

The block sends a ring's interrupt when the ring's packet count **crosses** the interrupt level,
not while it sits above it. A ring left with packets in it and nobody inside gets no further
interrupt. [The measurements page](measurements/xgs3300.md) found that when the handler made one
pass at a ring and stopped, and the fix was a handler that goes round until a pass takes nothing,
with the watchdog as the net under it.

The handler took the ring's flag for each pass and gave it back after. The watchdog made **one
pass** over each ring, every period.

1. The watchdog's tick finds the handler at work on a ring and takes the flag between two of the
   handler's passes.
2. The handler's next pass finds the ring held. That reads as a pass that took nothing, which is
   how the handler knows a ring is empty, and the handler leaves.
3. The watchdog's one pass takes what was in the ring and hands it up the stack. That is
   milliseconds, and packets go on arriving while it lasts. The pass ends; they are still in the
   ring; the count never fell below the level; nothing will cross it.
4. The ring waits for the next tick. The watchdog makes one pass again - of a ring holding
   everything the sender could send before it ran out of acknowledgements - and the acknowledged
   sender fills the ring again while that pass is handing up. And so on, a window a period:
   37 Mbit/s second after second, and the usual tool's steady 74 to 76.

Four streams were fast because they are spread over several rings, each lightly loaded, and the
handler is seldom inside one when the tick comes. And sometimes they were not - seven of that
day's thirty-four four-stream rows read 166 to 597 Mbit/s - which is a ring caught the same way.

*Anything that declines to service a ring owes a service when it stops declining* is that page's
own sentence, and it says the watchdog had learned it. It had not, and the handler that found the
ring held was declining with nobody owing.

## What was tried first: the vendor's bit

The vendor's host driver does not depend on the crossing. When its poll of a ring finishes it
writes the count it took to `R_OUT_CNTS` and then bit 59 of the same register, which its register
header calls RESEND and its comment says raises the interrupt again if packets are still pending.

Written here whenever a servicer that had held a ring left it (module `482611b6`, never in the
tree):

| | one stream up | the watchdog found packets |
|---|---|---|
| RESEND written | 761, 775 | 64 and 69 times in ten seconds |
| the same module, the switch off | 105 | 196 times |

It does raise the interrupt. But rings were still being left - the watchdog still found them, and
pauses of 17 to 29 ms were still in the capture - and once servicers stopped leaving rings there
was nothing for it to do. It is not in the driver. What the block takes for *pending* - the
condition as it stands, or the latched status this driver clears at the start of every pass - was
not established.

## What it is now

A servicer does not make a pass at a ring. It makes a **visit**, and three rules say what a visit
is. Each is there because of a way a ring was left.

- **It holds the ring for the whole visit, and goes round until a pass takes nothing** - whoever
  it is. There is no gap between two passes for another servicer to come in by, and a servicer
  that has read a count of zero has left the count below the level, where the block's own rule
  brings one back.
- **A servicer that finds the ring held says so, and the holder looks.** Between a holder's last
  reading of the count and its giving the flag back, a packet can arrive, raise the interrupt and
  have the handler find the ring held: the interrupt is spent and the packet is in a ring nobody
  is inside. So the one turned away leaves a note and reads the flag once more, and the holder
  reads the note after it has let go and goes in again if there is one. Each side writes and then
  reads what the other writes, so both writes are locked operations: with plain stores both can
  read the old value.
- **A visit that has to leave work behind asks for the watchdog's next tick.** It has to when it
  reaches its bound, or when the register counts a packet whose buffer cannot be read yet. The
  request is remembered as well as made, because whatever arms the watchdog for its period after
  the request puts the request back fifty ticks - the watchdog did that to itself on its way out,
  and so did the resume after a quiesce.

The bound is in packets: sixteen rings' worth for the handler, one for the watchdog, the sysctl
and each sweep of a resume. It was sixteen passes, written when a pass was taken to be a ring's
worth. A pass takes what is in the ring, which under a steady stream is a handful: on the first
build with the flag held for a visit, 71 to 211 visits a second ended on the sixteenth pass.

And the watchdog now says what it found. `dp.rxwd_rescues` counts the rings it found packets in on
a visit that ring had not asked for; `dp.rxwd_stalls` counts those whose first pass took thirty-two
packets or more, which is a ring that had been left (and, since [a ring that fills](a-ring-that-fills.md),
one that nobody had been inside for two ticks). `dp.oq_declined`, `dp.oq_handed`,
`dp.oq_bound` and `dp.oq_unseen` count the servicers turned away, the notes honoured, the visits
that ended on the bound and the passes that read a count and found no buffer.

## Measured after

Module `e139dd09`, `ipsec.flows` 0, the watchdog at its default.

| | main | now |
|---|---|---|
| one stream up, the usual tool, 12 s | 74 to 76 | **789, 755, 797** |
| one stream down, the usual tool, 12 s | 88 to 362 | **782, 769, 793** |
| four up | 166 to 935 | 941, 936, 941 |
| four down | 926 to 963 | 947, 948, 935 |
| eight up, eight down | | 969, 990 |
| one stream up, the second generator, 10 s | 360, 169 | 740, 767, 775 |
| one stream for a minute, up and down | | 759, 781 |

The four streams of an upload row now run at 230 to 238 Mbit/s each.

The same four rows in each setting of `ipsec.flows`, which is the table of
[the flow carries the association](the-flow-carries-the-association.md):

| one stream, 12 s | up | down |
|---|---|---|
| the host carries it (0) | 797 - it was 75 to 147 | 793 - it was 100 to 195 |
| the encrypting direction in hardware (1) | 781 - it was 194 to 275 | 775 - it was 271 to 622 |
| both directions in hardware (2) | 760 - it was 731 to 750 | 757 - it was 681 to 740 |

**The host now carries one stream as fast as the flow table does.** What limits these rows is no
longer the appliance: the second generator's stream reads 759 to 802 with the host out of the
path altogether. The host does it with one interrupt thread at 69 to 74 % of a core and the
network thread at 26 to 28 %: 0.82 to 0.97 of eight cores, where the flow table uses 0.01.

Over the session on that module - thirty-three minutes, 61.6 million frames received:

| | |
|---|---|
| `dp.rxwd_stalls` | **0** |
| `dp.oq_bound`, `dp.oq_unseen`, `dp.rx_resync`, `ipsec.out_drop` | 0, 0, 0, 0 |
| `dp.rxwd_rescues` | 5,080 |
| `dp.oq_declined`, `dp.oq_handed` | 52,739 and 1,732 |

(None of these could see a ring that is behind the block with a count of nothing, which the build
of this page could have been: [a packet is not a buffer](a-packet-is-not-a-buffer.md), found the
day after. It was not read here.)

**The quiesce**, which is what a detach runs and which a visit now holds a flag across, taken and
lifted in a loop under load - the test of [the measurements page](measurements/xgs3300.md):

| | cycles in 8 s | the stream | frames received during them | had to wait for a servicer | longest wait | stalls |
|---|---|---|---|---|---|---|
| one stream up | 2,782 | 771 Mbit/s | 1,003,410 | 1,656 times | 80 us | 0 |
| four streams up | 3,139 | 931 Mbit/s | 1,330,921 | 2,568 times | 190 us | 0 |

**The suites of the pages before**, on this module: the clear-text probe as it read (twenty
refused in each of the first two settings, fifteen let through in the third, which is
[issue 304](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/304)); four rekeys under a
four-stream upload at 933 to 951 Mbit/s with nothing dropped while an association installed and
one packet dropped by the peer in two of them; a download given back and revived at 98 to 99 % in
hardware. The upload rows that used to read a quarter of the rate did not appear.

## What was read wrong before

- **`dp.rxwd_runs` was read as interrupts lost.** The measurements page says of one 16 MB
  download that its eleven are "interrupts genuinely lost, and caught by the timer instead". What
  the counter says is that the watchdog found packets, and it finds some on a healthy ring - 5,080
  times in the session above, none of them counted as a ring that had been left. No lost interrupt
  has been shown on this block. What was shown is rings left by their servicers.
- **The slow rows were charged to the cipher's cabling, then to the tool.** Three pages say one
  stream is slow for a reason not known, and the comment on the issue earlier that day made the
  load tool the first suspect, because six uploads by a second generator had read 925 to 936. They were
  four streams each. One stream by that generator read 360 and 169.

## What was not it

- The pauses of 5 to 45 ms that remain in the capture are there with both directions in hardware,
  when the host takes 582 interrupts in ten seconds, and they were there with the watchdog at one
  tick. They are the endpoints'.
- The first connection after the tunnel comes up stands still for 0.3 s: its first ten segments
  are 1,460 bytes, the path takes 1,406, the appliance answers *fragmentation needed* ten times
  and the sender sends again when its 300 ms timer runs out. Once.
- A TCP connect from the lab machine takes 24 ms to anything, an address the coprocessor is not on
  the way to included. It was dropped as a round-trip probe.

## Before it was written, and after

The vendor's host driver was read for how it leaves a ring before any code. Four readers on the
build that first measured well - the hand-off, the callout, the quiesce, and every sentence the
change wrote - and a skeptic on each of the six findings that were not about wording. None was
serious and five held. What changed because of them and of the readers' lesser findings:

- the resume after a quiesce armed the watchdog for its period over a request for the next tick,
  which is the override the change had just removed from the watchdog itself;
- the stall count was judged by one request flag for the whole device, so a request from one ring
  excused a ring found left beside it. It is a flag a ring now;
- a visit could overrun its bound by up to a ring. A pass is told what is left of it;
- a note left at any moment of a visit sent the holder round once more at its end. It is taken
  down before each pass: 168 to 174 notes honoured in a ten-second run became 4 to 11;
- the quiesce path gave the flag back without looking, and set its own flag with a plain store.

The sixth - that a zero count does not mean the next packet crosses the level - was refuted for
the level the appliance runs with, which is 1, and holds for the driver's own default of 32: there
it is the block's timer that brings a servicer back. The sentence says both now.

## Not in it

- **Plain forwarding through the host at this rate was not measured.** The only path to a far end
  that can take a gigabit is the lab tunnel. One download at a time through NAT and the WAN read
  58 to 95 Mbit/s on the build before the last, which was the WAN that hour, with no stall
  counted: it says nothing either way.
  The mechanism is not the tunnel's, and that is read, not run.
- **The watchdog still visits a ring whose handler is at work**, and is turned away about thirty
  times a second under a stream. Having it pass over a ring something has visited since its last
  tick was suggested in review and not done.
- **Under a load the host cannot keep up with**, a ring that reaches the handler's bound is
  carried by the watchdog, a ring's worth a tick, until a visit finds it empty. The bound was
  not reached in any run here. (What a ring that fills does was found the same day, and it was
  not this: [a ring that fills](a-ring-that-fills.md).)
- **What RESEND takes for pending** - above.

## Lesson

A net under a mechanism is a second user of it. It has to obey the mechanism's rule as strictly as
the thing it is there to catch, and it is the last place anyone looks: what this one caught was
read, on the page that introduced it, as proof that the block loses interrupts.
