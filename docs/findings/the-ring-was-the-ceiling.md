# The receive ring was the ceiling

Measured on an XGS 3300 running OPNsense 26.7.5, 2026-10-05, against a real multi-gigabyte download
through the appliance.

| | before | after |
|---|---|---|
| throughput | **~84 Mbit/s** | **288 and 305 Mbit/s**, two samples |
| frames per second | ~5,000 | **28,052** |
| `FPCNTR_TX_DROP_QUEUE_FULL` | accumulating | **did not move** |
| host CPU | not measured | **96% idle** |

The driver is no longer the bottleneck. With the ring deep enough, the coprocessor stops running out
of places to put frames, the host CPU is almost entirely idle, and what is left is the line.

## What was changed

Two constants, and that is the weakness of this measurement - see the caveat below.

**`OCTEP_DP_OQ_DESCS`, 256 to 1024.** The comment above it had said "raise it once something has
run" since the first bring-up. The vendor ships 4096. 256 buffers is about a fifth of a millisecond
of a gigabit line, so a burst arriving while the host was between service passes had nowhere to go -
and the coprocessor said so, in `TX_DROP_QUEUE_FULL`: it had frames to hand over and the host's ring
had no room.

**`OCTEP_DP_OQ_INTR_PKT` and `_TIME`, 8 packets / 2 µs to 32 / 50.** An interrupt per eight frames
is right for proving that an interrupt arrives and wrong for a download: the host spent its time
entering and leaving the handler instead of draining the ring, and one service pass may take
`DRAIN_ROUNDS x RSIZE` packets anyway, so a later interrupt costs nothing. (One *visit* by the
handler may; a pass takes at most `RSIZE`, and usually a handful - [a visit is not a pass](a-visit-is-not-a-pass.md).)

## Why not the vendor's 4096

Because there is no longer any evidence it would help. At 1024 the queue-full counter does not move
at all and the CPU is 96% idle: the ring is not the constraint any more, so another four-fold would
buy nothing and cost memory. Each of the eight rings carves its buffers out of **one contiguous DMA
allocation** of `DESCS x BUF_STRIDE`, so 4096 asks for 6.4 MiB contiguous per ring and 51 MiB in
all, where 1024 asks for 1.6 MiB and 13 MiB. The appliance has 16 GiB with 14.9 free, measured, so
it is affordable - and a contiguous allocation that fails takes the interface with it, which is a
poor trade for a ceiling that has already stopped binding.

## The caveat, which matters

**Two constants were changed in one step, so the gain is not attributed between them.** The ring
depth is the one the queue-full counter pointed at, and the coalescing is the one that would explain
a CPU cost - and the CPU turned out to be idle either way, which weakly suggests the depth did the
work. That is an argument, not a measurement.

Separating them is two builds and two reboots and has not been done. It is worth doing, because a
constant that was raised for no reason is a constant nobody will dare lower again.

## What was examined first and did not survive

**`dev.octep.0.dp.credit_capped`** reads 436,152 and was the obvious candidate. It is not evidence:
it increments whenever the grant is clamped, and that includes the ordinary steady state of a fully
credited ring, where buffers were consumed but the doorbell is already at its ceiling. A counter
that fires in the healthy case cannot distinguish the sick one. It was nearly the number this change
was justified by.

## Lesson

The constant was marked provisional in a comment, by the same hand, months before it mattered -
"256 of each for a first bring-up... raise it once something has run". Something had been running
for weeks. The note was right, it was addressed to whoever came next, and nobody was whoever came
next until a download was slow enough to ask. **A provisional constant with a comment saying so is a
deferred measurement, and it stays deferred until something makes it hurt.** The cost of finding it
was one `grep`; the cost of not finding it was every download since.
