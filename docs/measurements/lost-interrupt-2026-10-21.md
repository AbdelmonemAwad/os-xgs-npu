<!-- reviewed: 2026-10-21 -->
<!-- board: rings left with a burst, an hour | XGS 3300 | 10 to FINAL_STALLS -->
# A ring left with a burst: the interrupt raised, the handler never run. Measured, 2026-10-21

[Issue 361](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361): with the trigger on, a few times
an hour, a burst landed in one of the XGS 3300's receive rings and waited there until the receive
watchdog's next sweep - up to a period, 50 ms - with TCP covering the gap every time. Two instrument
builds on 2026-10-13 read the block's registers at such a stall: the ring's interrupt status set, the
vector unmasked, nothing pending. The reading drawn from them - a status a visit had left up gives the
next packet no edge - is withdrawn below. The ring was left clean; the block raised its interrupt; the
host never ran the handler. Why the message does not arrive is still not known. What changed is that
the host no longer waits for it: it looks at every ring's head buffer each tick.

## The soak, rebuilt inside the lab

The trigger-on soak on a translated path of 2026-10-12, without the peer it used then: the PC on the
second XGS 3300's F1 downloads from the first XGS 3300, behind the second's A4, in three loops of
iperf3 -R, 20 s a pass; the second XGS 3300 translates the PC to its own address on A4 and its trigger
is on, so each connection is handed to the fast path once made and the host's rings go idle between
bursts. An address added to the first XGS 3300's F1 for the run carries it, because the address the
lab already has there is inside the lab's tunnel between the two (`images/deploy/lab-3300-361-soak.sh`,
not in this repository; the translation rule, the route, the address and the servers are removed at
the end, and each report says so).

## Every visit now says how it left the ring

Each pass of a visit already read the status after clearing it, and the count; the last pass's
readings are now kept per ring with the time and the ring's handler entry count - no register read
more - and a stall prints them beside what the watchdog read.

**The hour before, `octep.ko 6c7e3f56`** (03:50 to 04:50 UTC): 456 passes, all complete, 3,178 Mbit/s
median a stream; **10 stalls**:

![The first soak's report: 456 passes, rxwd_stalls +10, and eight stall records - each ring left by the visit before with int_status 0x0 and cnts 0x0, then holding 44 to 166 packets with its status bit and the count's interrupt bit 62 set, the vector unmasked, nothing pending, and the handler's entry count unchanged](../images/measurements/lost-interrupt-2026-10-21/01-ten-stalls-each-ring-left-clean.png)

```
ring 6  first pass 92  idle 30 ticks  visit 106
before it: int_status 0x41  cnts 0x400051a30000005c  levels 0x5600000001  vector 22 mask 0 pending 0 handler entries 1252207
as the visit before left it, 30 ticks earlier: int_status 0x0  cnts 0x0  handler entries 1252207
```

The visit before left ring 6 **clean** - status clear, a count of nothing, the count's interrupt bit
(62) clear. Then 92 packets, bit 62 set, the vector unmasked, nothing pending in the PBA, and the
handler's entry count **unchanged**: the block raised the interrupt and the handler did not run.
Every one of the eight records kept reads the same.

## Two things the records showed were wrong, and fixed - and neither was it

**The status register holds every ring's bit.** `int_status` 0x41 is bits 0 and 6; ring 3's read 0xa,
ring 1's 0x22. Read through ring N's window the register holds a bit for every ring, bit N for ring N,
and every pass of every servicer wrote back what it read - clearing the other rings' bits too. Each
pass now clears its own and leaves the others (`dp.istat_others` counts the passes that found others
set: 37,967,104 in the hour below).

**Every ring's vector was delivered to cpu0.** `hw.intrs` on both XGS 3300s: `octep0` eight times, every
one `@cpu0`, so the rings' handlers took turns on one CPU. The vendor's host gives each ring's vector a
CPU of its own, and the driver now does too when it hooks them (`dp.msix_spread` 1, `dp.msix_bound` 8):
ring0 `@cpu0` to ring7 `@cpu7`.

With the first alone (`octep.ko 18b0f9b2`) the soak counted a stall in its first minutes; with both
(`octep.ko bef3458f`), three in its first ten. Each the same way round - ring 5 on cpu5, left clean by
the sweep 50 ticks before, 81 packets, bit 62 set, nothing pending, the handler not run. Both soaks were
stopped there. Neither fix is what keeps a ring from being left.

## So the host looks

Between the watchdog's sweeps, every tick, the driver looks at each ring's head buffer in host memory:
the length word the block's DMA writes is poison until a packet lands. A ring whose head has held the
same packet since the last look, with nobody inside, is visited. It reads no register. It finds two
things: a ring whose interrupt was raised and never handled - ring 4 with 6 and then 11 packets, bit 62
set, the handler not run, found after a tick or two instead of a sweep - and a lone packet: the count
interrupt is raised when the count goes past the level, so with a level of one a single packet waits
for the time threshold (`cnts 0x3300000001` - a count of 1, bit 62 clear). Each visit is kept in
`dp.rxpeek_log`, apart from the stalls; `dp.rxpeek_found` counts them, `dp.rxpeek_ticks` sets the
interval (1; 0 turns it off). At rest it costs a callout a tick: 1.06 % of the eight CPUs busy with it
on, 0.89 % with it off, a minute each on the same boot.

**The hour with the look, `octep.ko 2b488b3e`** (05:41 to 06:41 UTC): 456 passes, all complete, 3,209
Mbit/s median a stream; **4 stalls**, and **548 rings found by the look** before the watchdog came, 7,310
packets taken from them:

![The soak with the look: 456 passes, rxwd_stalls +4, rxpeek_found +548 taking 7,310 packets, istat_others +37,967,104, and the look's records - ring 4 found with 6 and 11 packets, its status bit and bit 62 set and the handler not run, and lone packets with a count of 1 and bit 62 clear](../images/measurements/lost-interrupt-2026-10-21/03-the-look-four-stalls-548-found.png)

FINAL_HOUR

## What it does not mean

- **The interrupt is still lost, now and then.** Why the block's message does not reach the host when
  the ring was left clean and nothing on the host had touched it since is not known. The host finds the
  ring sooner; it does not make the message arrive.
- **The four left are the watchdog's own lateness**, read so: the look runs in the watchdog's callout,
  and `dp.rxwd_gap_max` - the longest from one run's start to the next's - read 134 ticks in that hour.
  While the callout is late neither the look nor the sweep runs, and the sweep that runs first counts
  the stall. That is a reading, not a measurement of each of the four.
- **The XGS 136** is a different driver with its own receive path, and has shown nothing of the kind.
- `dp.oq_left_up`, visits that left a ring empty with its own status bit still set right after the
  clear, is harmless: 29,718 of 58,003 in a minute of bursts on the first XGS 3300, with the
  interrupts going on and no ring left. The reading it was built to test is the one withdrawn above.
