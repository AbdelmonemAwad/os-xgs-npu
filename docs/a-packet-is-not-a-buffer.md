# A packet is not a buffer

Measured on an XGS 3300, 2026-10-09, module `73d533b9` (main) before and `8e5cc09b` after.
[Issue 327](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/327), found by the row at scale of
[issue 322](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/322): after nine hundred
connections had been held in the coprocessor's table, about one new connection in eight timed
out, and went on timing out until the appliance was rebooted.

No counter the driver had moved. The receive path counted every way a count could be without its
buffer and had nothing for a buffer without its count.

## What it looked like

Connections from one host to one address and port, source ports in a row. Some ports failed five
times of five and their neighbours never: sixty to the appliance's own address, 10 failed; sixty
through the tunnel, 9; sixty from the same host by another path, 0. One in eight is the share of
tuples the fast path hashes to one host ring of eight.

`dp.state`, on the build it was first seen on (`6896a1cc`, a build under test whose receive path
is main's):

```
ring 0   OUT_SLIST_DBELL, low half 1758 (granted 2032; the seven siblings 2016 to 2030)   OUT_CNTS 0
268 receive buffers have been written, across every armed ring
```

268 buffers the block had written and the host had not taken, all on ring 0, with a count of
nothing - and the figure stayed at 268 while the ring went on carrying traffic. `dp.rx_resync`,
`dp.rx_short`, `dp.oq_lapped`, `dp.oq_unseen` and `dp.rxwd_stalls` read 0.

Numbered datagrams on a tuple that lands on that ring, watched on the interface's own tap:

| sent | seen |
|---|---|
| 150 | none of them |
| then 400, on another tuple of the same ring | the first 149 |
| 150 on a tuple of another ring | 150 |

Ring 0 took 421 packets during the second row. The ring was not stopped and it was not losing
anything: every frame on it was handed up 268 arrivals after it had been written. At rest that
ring carries a frame or two a second, so a connection's opening frame waited minutes.

## On main

Module `73d533b9`. A clean boot with the rings level - no buffer written and untaken, 64 of 64
tuples seen at once - and then the scale row: nine hundred UDP flows through the lab tunnel, made
fifty at a time at fifty datagrams a second each, then each kept in use with a datagram every four
seconds. Ring 0 read every second:

| connections in the table | buffers written and not taken |
|---|---|
| 0 | 0 |
| 120 | 31 |
| 438 | 69 |
| 777 | 72 |
| 893, and after they were taken out | 73 |

Afterwards ring 0's doorbell read 1950 of 2032 with a count of 0; 8 of 64 tuples were not seen
within a second of being sent; and of 150 numbered datagrams on one of them the first 103 were
seen in the twelve seconds the tap ran, as later traffic on the ring pushed them out, and the last
47 were not.

## Why

A packet longer than a receive buffer is not refused and it is not cut. The block writes on into
the next buffer, and the one after, and counts the whole packet once. The vendor's host works out
how many buffers a packet used from its length word: the first buffer holds the length word, the
response word and then what is left of the buffer, and every buffer after it is all packet.

The driver's pass took one buffer for each packet counted. Read in the code as it was: for a
packet of two buffers it took the first, dropped it for a length no frame could have, acknowledged
one and returned one unit of credit. The second buffer stayed. The next packet counted paid for it
- the pass took the second buffer, read eight bytes of payload as a length and dropped that too -
and from then on the ring was one buffer behind: each count took the buffer of the packet before.

The packets that do it are the coprocessor's own. The fast path pushes each connection's packet
and byte counts to the host in control messages, on tag 254, to ring 0, and a message carries an
entry for every connection that moved since the last one. One of them, kept by the build that
reads their length:

```
ring 0  tag 0x00fe  length word 1898  2 buffers
... ef ef                      the control EtherType
00 00 01 01                    type 1, connection statistics; version 1
61 00 0e 01                    97 entries in 14 bursts, both directions
```

That is eighteen bytes an entry. By that arithmetic a buffer stops being enough when about eighty
connections have moved within one message's interval - which is why four streams of a download
never did this, and fifty new flows at fifty datagrams a second did it several times a second.

At rest, with a tunnel up, the message on that tag is another one: type 2, the associations'
counters, two entries, once a second, and it fits.

A frame from a wire does not span: 1,478 to 1,550 bytes of ping sent to the appliance from the far
end were refused by the fast path, four counted for each four sent, and nothing was written.

## What changed

**A packet is taken as what it used.** The pass reads how many buffers the length word means by
the vendor's rule, moves its read index by that many, re-poisons each of them and owes each its
unit of credit, and acknowledges one packet. Credit is in buffers and the count is in packets;
they had been the same number until now. Every walk of a ring's buffers asks the same question
the same way - the pass, the scan for a gap, and `dp.state` - so they agree where a packet ends.

A packet of more than one buffer is taken whole and read by nothing: no interface's frame is that
long, and the statistics messages have no reader here yet. `dp.rx_spans` counts them,
`dp.rx_span_bufs` the buffers they used after their first, `dp.rx_span_max` the longest, and
`dp.rx_span_frame` keeps the head of the last. A length word above 65,535, or one that would span
the ring, is not taken for a length: one buffer is consumed, nothing in it is read, and
`dp.rx_long` counts it.

**And a ring that is behind has a way back.** A pass that reads a count of zero now looks at the
buffer its read index is on. If that buffer holds a packet, the packets that are there are
counted and the time is kept; if the same buffer is still there two ticks later, with nothing
taken in between, the count is never going to hold them. As many are taken as were there when the
wait began, and nothing is acknowledged for them. `dp.oq_behind` counts the packets taken that
way. They are handed up late and in order. A packet that lands after the wait began has a count
coming and is left for it.

A written buffer with a count of zero is also what an ordinary arrival looks like for the moment
between the block's write and its count, which is why it has to last. On a ring with a packet
every millisecond it never lasts, and there a ring that is behind stays behind until a gap; on a
quiet one the watchdog's visits find it, two of them, a period apart.

Since nothing sends a ring behind any more, there is an instrument that does: `dp.oq_lose`
acknowledges that many packets on one ring without taking them.

**Two smaller things in the scan for a gap**, which is the path a count without a buffer takes. A
packet that lands on the read index while that scan is running is no longer stepped over: for a
packet of several buffers that would have left the read index inside it. And what the scan holds
against the count is packets, not the buffers a long one went on into.

`dp.state` says which ring holds written buffers and where its reader is, and walks from there, so
it does not print a long packet's later buffers as packets of their own. `dp.rx_untagged_frame`
keeps 128 bytes, which is enough to reach a control message's own header.

## Measured after

The same row from a clean boot.

| | main, `73d533b9` | this change, `8e5cc09b` |
|---|---|---|
| packets of more than one buffer during the row | not counted | 62, the longest 3,114 bytes |
| buffers written and not taken afterwards, the count at 0 | 73 | 0 |
| ring 0's doorbell, of 2,032 | 1,950 | 2,017 |
| tuples of 64 seen within a second of being sent | 56 | 64 |
| 150 numbered datagrams on a tuple of ring 0 | 103, over twelve seconds | 150 |
| the table stopped growing after | 112 s | 114 s |
| a download through the tunnel while the table was held | 774 Mbit/s | 810 |

Two earlier builds of the change ran the same row: 72 such packets using 77 buffers after their
first - so some of three - and 60, the longest 2,736 bytes. No buffer was left in either.

**The way back**, with the instrument, on ring 0:

| | taken without a count | in | numbered datagrams |
|---|---|---|---|
| 5 acknowledged and not taken, the ring at rest | 5 | 5 passes | - |
| 40, under 300 datagrams sent back to back | 40 | 3 passes | 300 of 300, in the order sent |
| 40, under 300 datagrams 4 ms apart | 40 | 40 passes | 300 of 300, in the order sent |

After each row no buffer was left written, and `dp.oq_unseen` and `dp.rx_resync` read 0.
`dp.rxwd_stalls` counted 1 in the back-to-back row: a walk that brings back thirty-two or more at
once, from a ring nobody had been inside for two ticks, is what the watchdog calls a ring that had
been left, and that is what this one was.

**The rows of the pages before**, on this build. The three settings of `ipsec.flows`: four streams
926 to 955 Mbit/s down and 938 to 946 up, one stream 789 to 820 down and 762 to 792 up, the six
checks and the clear-text probe as they read before. The hand-back rows: 941 to 967 for four
streams down, 776 to 907 for one. Three rekeys under a four-stream upload at 945 to 949 with
nothing dropped at either end.

Over that boot, 22.1 million frames: no buffer left written, `dp.oq_lapped`, `dp.rx_resync`,
`dp.rx_short`, `dp.oq_unseen` and `dp.rx_long` 0, and `dp.oq_behind` at the 85 the instrument had
made.

`dp.rxwd_stalls` counted 2 in those suites, besides the instrument's one. The build before this
one, `7544df3a`, which lacks only what the last reading of the change asked for, ran the same
three suites over 21.3 million frames and counted none; an earlier one, `b69959d1`, counted 2 in
the hand-back and rekey suites alone, with 2 passes that found a count and no buffer. No walk
without a count was made in any of them. That is the stall
[a ring that fills](a-ring-that-fills.md) describes - a tick that lands between a burst on a ring
that had been idle and its handler - at a rate this page did not set out to measure. It is noted
on [issue 320](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/320).

## What is not established

- **Nothing reads the statistics.** The messages are taken and dropped. They are each
  connection's packets and bytes as the coprocessor counted them, which is what the host cannot
  see for a connection it has handed over.
- **What main does when the ring is behind by its whole grant** was not measured. The block stops
  fetching for a ring whose doorbell is under the watermark, and a ring a thousand buffers behind
  is there.
- **A busy ring that is behind** was not made: on a ring with arrivals closer than two ticks apart
  the way back waits for a gap.
- **`dp.rx_long` never moved**, and no length word near the bound was seen. The longest message
  was 3,114 bytes; the vendor's receiver bounds one at 800 entries.
- The entry size and the figure of eighty connections are arithmetic on one message's length, not
  a reading of the fast path's source, which is not published.
