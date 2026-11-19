<!-- reviewed: 2026-11-19 -->
# Switch weights on the second XGS 3300: the shares follow the table once port 0's pause is off, 2026-11-19

os-trafficpriority gives a class a minimum - a part of a full port that its switch queue keeps - by
the switch's weight table: os-xgs-npu's switch tool (`src/opnsense/scripts/xgs/switch_qos.py`) writes
one sequence of queue numbers for the whole 88E6193X chip, and a port in weighted mode serves its
queues in that order. Written for two classes at 30 and 20 turns, the table did not show: earlier the
same day the class at 30 took 914.5 to 924.2 Mbit/s at line rate and the class at 20 took 10.3 to
18.4. These runs found why, and the lever: **with port 0's pause off - the switch's flow control
toward the coprocessor - the shares follow the table**, 59:41 for 30:20 and 38:62 for 30:50, for 1 to
3 % of the line rate with two streams.

## How it was measured

The second XGS 3300 (vendor version 22), 20:55 to 21:13 UTC, with os-trafficpriority's second part
and os-xgs-npu's switch tool from their branches of that day. The bench machine sends through the
3300, which routes the traffic in hardware, out of Port8 (the switch's port 8) to iperf3 servers on
the XGS 136.

- **Classes**, set by the plugin's Apply: A, traffic to port 5311, marked EF - TCAM entry 3, queue 7,
  minimum 30; B, to port 5313, marked AF41 - entry 255, queue 6, minimum 20. Traffic to port 5312 is
  in no class and stays in queue 0. Port8 in weighted mode, and the chip's table 105 turns: queue 7
  30, queue 6 20, queue 0 50, queues 1 to 5 one each. For 30 against 50, class A runs against the
  traffic in no class.
- **UDP**: two streams of 100 Mbit/s for 10 s, 1,400-byte datagrams, both sent unmarked - the classes
  mark them - into Port8 limited to 100 Mbit/s (a Firewall: Shaper pipe followed into the switch: eDec
  100, N 500). **TCP**: two streams at line rate for 60 s, Port8 without a limit; and one stream alone
  for 30 s, for the cost.
- **Read**: the rates received at the XGS 136; the switch's counters before and after each run - port
  8's frames out of each queue (OutQueue0 to 7), its frames discarded (OutDiscards) and the pause
  frames it took in (InPause); port 0's TCAM counters for entries 3 and 255, its frames discarded at
  the input (InDiscards) and the pause frames it sent to the coprocessor (OutPause). And once a second
  during three runs - A against B by UDP in E0 and in E1, by TCP in E4: the frames waiting in port 8's
  queues 0, 6 and 7 (port register 0x1b), port 0's status (bit 4, flow control asserted) and the
  chip's free buffers (Global1 register 0x1b).
- **Written by hand**, each value read three times alike first, read back, and put back as found
  after: port 0's flow control (port register 0x02, pointer 0x10, found 0x8c and read as 0x108c), for
  E1 and E3; port 0's Queue Control pointers 0x50 and 0x51, its pause band (found 0xff and 0xdf), for
  E4. A safety net on the 3300's host would have put all three back after 10 to 20 minutes; it was
  cancelled each time with nothing written by it.

| experiment | port 0's flow control, pointer 0x10 | port 0's pause band, 0x50 / 0x51 |
|---|---|---|
| E0, as found | 0x8c: forced (bit 3), pause on (bit 2), both ways, and bit 7, the queue controller's path | 0xff / 0xdf |
| E1, pause off | 0x88: written 0x9088, read back 0x1088 - as UMSD's gprtSetFlowCtrl writes it with en false, bit 7 kept | as found |
| E3, bit 7 off, pause kept | 0x0c: written 0x900c, read back 0x100c | as found |
| E4, a smaller band, pause kept | as found | 0x40 / 0x20, 0x51 written first |

## Results

| run | turns | E0: pause on, as found | E1: pause off | E3: bit 7 off | E4: smaller band |
|---|---|---|---|---|---|
| UDP, A against B, 100 Mbit/s port | 30:20 | 87.2 / 9.6 | **57.9 / 38.7** | - | 48.6 / 48.1 |
| UDP, A against no class, 100 Mbit/s port | 30:50 | 85.9 / 10.9 * | **36.4 / 60.3** | - | 48.0 / 47.9 |
| TCP, A against B, line rate | 30:20 | 913.6 / 17.7, sum 931.3 | **534.5 / 370.5**, sum 905.1 | 657.8 / 257.6, sum 915.5 | 471.4 / 462.2, sum 933.7 |
| TCP, A against no class, line rate | 30:50 | 917.3-920.7 / 13.6-15.1, sum 932.1-934.3 * | **352.4 / 568.9**, sum 921.3 | - | 471.1 / 464.7, sum 935.8 |
| TCP, one stream in queue 0 | - | 935.7 | 896.5 | 885.7 | 939.3 |
| TCP, one stream in queue 7 | - | 903.9 | 909.9 | - | - |

Mbit/s received at the XGS 136. \* Earlier the same day, 18:19 to 18:27 UTC, with the same classes
and table: three TCP runs and one UDP run. A dash: not run.

| retransmissions | E0 | E1 | E3 | E4 |
|---|---|---|---|---|
| TCP, A against B | 732 + 4,263 | 3,990 + 2,712 | 2,279 + 12,505 | 569 + 1,313 |
| TCP, A against no class | 38-128 + 2,560-3,704 * | 1,520 + 2,103 | - | 32 + 156 |
| one stream in queue 0 | 88 | 979 | 1,054 | 30 |
| one stream in queue 7 | 303 | 1,717 | - | - |

### E0: why the table does not show

With the pause as found, during the UDP run of A against B:

- **Both classes reached the switch alike**: entry 3 counted 78,691 frames and entry 255 79,086.
- **The lower one was discarded there.** Port 8 sent 78,690 frames from queue 7 and 8,702 from queue 6,
  and its OutDiscards rose by 70,384 - B's frames in, less B's frames out, exactly. Port 0 discarded
  nothing at its input (InDiscards +0), and the XGS 136 never paused Port8 (InPause +0); port 0 sent
  the coprocessor 13,808 pause frames.
- **Queue 7 held the backlog**: 450 to 509 frames waiting in it, 0 to 6 in queue 6 and none in queue 0.
  Port 0's status said flow control asserted in every sample of the run, and the free buffers fell
  from 1,017 to about 500.
- So the scheduler is not what loses. Queue 6 is empty at its turns, its frames discarded as they
  arrive, while queue 7 holds the whole backlog that port 0's flow control lets in.

### E1: port 0's pause off

- **Both queues backlogged, served by the table.** Queues 6 and 7 each held 324 to 354 frames. Port 8
  sent 51,796 frames from queue 7 and 34,622 from queue 6, 60:40 - the 57.9 against 38.7 Mbit/s of the
  UDP run. For 30 against 50: 32,528 frames from queue 7 against 53,900 from queue 0, 38:62.
- **The discards moved to port 0's input**: InDiscards rose by 91,940 and 92,159 in the two UDP runs;
  port 8's OutDiscards and port 0's OutPause by 0.
- **TCP followed the table as UDP did**: 534.5 against 370.5 Mbit/s (59:41) and 352.4 against 568.9
  (38:62). The two streams' sum, second by second, never fell below 740.3 and 802.3 Mbit/s (with the
  pause on: 660.7 to 804.3) - no collapse.
- **Put back to 0x8c, the split broke again at once**: 913.6 against 17.7. The pause is the lever.
- **The cost**: two streams at line rate carried 905.1 and 921.3 Mbit/s against 931.3 and 932.1 to
  934.3 with the pause on, 1 to 3 % less; one stream alone in queue 0 896.5 against 935.7, 4 % less (in
  queue 7, 909.9 against 903.9); and more retransmissions, as in the table above.

### E3 and E4: two other levers, and why not them

- **E3, bit 7 off, the pause kept**: 657.8 against 257.6 Mbit/s, 72:28 for a table of 60:40, with
  2,279 + 12,505 retransmissions; one stream 885.7. Worse than E1 on each count.
- **E4, the pause band lowered to 0x40 / 0x20, the pause kept**: the queues stayed short, 0 to 92
  frames, and the port split evenly whatever the table said - 471.4 against 462.2 for 30:20, 471.1
  against 464.7 for 30:50, and 48.6 against 48.1 and 48.0 against 47.9 by UDP. It keeps the line rate
  (933.7 and 935.8 Mbit/s for two streams, 939.3 for one), but it does not give the table's shares.

## What os-xgs-npu does with it

- **The switch tool turns port 0's pause off while a weight table is written** - classes with minimums,
  or Firewall: Shaper's shares - after the weights, and only over the 0x8c the coprocessor leaves; it
  keeps what it found, puts the pause back first when the weights go, and turns it off again after
  every coprocessor boot with the rest. Its show and check read it.
- **Strict classes, with no table, leave the pause as found.**
- So with minimums the switch keeps each class's share by discarding what the coprocessor sends
  beyond what a port takes, instead of holding the coprocessor back.

## What it does not show

- **The build itself on an appliance.** The pause was written by hand here. The switch tool writing it
  is to be measured in the lab: the plugin's Apply with minimums 30 and 20, and with a strict class
  only, port 0's pointer 0x10 read after each; the shares by UDP at a 100 Mbit/s Port8 and by TCP at
  line rate; a full reboot, with the boot hook turning the pause off again; and the plugin off, with
  0x108c read and nothing of the tool's left.
- **Other front ports while Port8 is congested with the pause off.** With the pause on, one full port
  holds back the coprocessor's whole uplink; with it off, the excess is discarded at port 0's input.
  Whether traffic for a port that is not full loses frames there was not measured.
- **A coprocessor reboot with the pause off**: whether its start leaves 0x8c there again, as it read on
  2026-11-17 and 2026-11-19.
- **The XGS 136**: os-xgs-npu only reads its switch, and writes no table there.
- **Frames or bytes**: every frame here was full-size, so whether a turn counts a frame or its bytes
  was not measured.
- **Other tables and loads**: 30:20 and 30:50 on one port, with two streams; one appliance, vendor
  version 22.
- **What it was before**: every frame from the coprocessor waited in queue 0 until a TCAM entry gave
  it another ([2026-10-25](qsched-2026-10-25.md)).
