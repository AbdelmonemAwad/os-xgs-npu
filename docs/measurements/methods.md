<!-- reviewed: 2026-10-21 -->
# How the measurements are taken

Every measurement page says how its numbers were taken and what they do not mean. This page gathers
what all of them have in common: the bench, where a figure is read, the tools, the arithmetic behind
each kind of figure, and the rules every page keeps - each with the reason it is done that way,
because a figure without its method is not evidence of anything, and the ones that look best are
usually the ones measuring something other than what they claim.

## The bench

Two XGS 3300s and an XGS 136, routed to each other, and a PC on the middle one at 10 Gbit/s - as on
[the bench page](bench-2026-10-17.md), where each run's window is photographed:

| link | from | to | medium |
|---|---|---|---|
| the PC | the PC's 10GBASE-T NIC | the second XGS 3300's F1 | copper, 10 Gbit/s |
| the fibre | the second XGS 3300's module port A1 | the first XGS 3300's F1 | fibre, 10 Gbit/s |
| the gigabit link | the second XGS 3300's Port8 | the XGS 136's Port8 | copper, 1 Gbit/s |

| what | why |
|---|---|
| **A hardware peer at both ends of every tunnel.** | Until 2026-10-17 the far end of a measured tunnel was a firewall doing ESP in software, so a figure could be that firewall's rather than the appliance's. |
| **Each link a /30 of its own, out of the LAN bridge, with a pass rule and a static route.** | Traffic is routed through the appliance rather than switched past it, and nothing else on the network shares the path. |
| **Never a public test server as the far end.** | Those are other people's servers and they refuse what looks like a flood; a refused line once left three "downloads" in a table that had never run. Anything parallel or repeated goes to a far end on the bench. |

## Where a figure is read

**At the receiver, at the far end - never from the appliance's own counters.** A connection in
hardware goes from port to port inside the coprocessor and never reaches the host: pf's state, the
interface counters and every graph built on them stop growing. A gauge that reads zero when things
work is not a gauge - a run was once stopped by a rule that read such a zero as a collapse that never
happened.

**The receiver's figure, not the sender's.** iperf3 prints both: the sender counts what it wrote into
its socket, the receiver what arrived. Only the second is throughput.

**A row is read only after it is known to have run** - bytes received, not just counters moved.

## The tools

| tool | what it measures here | why this one |
|---|---|---|
| **iperf3** - 3.20 under WSL on the PC to send, iperf3 for Windows to receive, 3.22 (OPNsense's package) as the server on an appliance's host | throughput, one TCP stream or four, ten seconds a run | the same tool at both ends of every run, its version named. Two clients on the PC because WSL's mirrored networking passes a frame above 1500 bytes out and not in: every run in which the PC received above 1500 measured 0.00 bits/s until it was measured again with the Windows build |
| **curl over HTTPS, and SHA-256** | the integrity soaks: a 1 GiB file of random bytes fetched in loops for hours | TLS refuses any record that does not authenticate, and the file's hash against the server's catches anything that got through whole; throughput alone says nothing about whether the bytes were right |
| **ping with the don't-fragment bit, at set sizes** | whether a frame of a given size crosses a path, and round trips | the plainest test of a path's MTU and of a port that receives at all |
| **sysctl** | the CPUs (`kern.cp_time`), the drivers' own counters (`dev.octep.0.*`, `dev.npuep.0.*`), and the TCP host cache emptied (`net.inet.tcp.hostcache.purgenow`) | the kernel's own numbers, read the same way on every run |
| **pfctl** | pf's states and their packet and byte counts | what the host's firewall saw, to set beside what the hardware saw |
| **the coprocessor's own statistics**, read through the driver | what the hardware forwarded: frames from the wire, frames it sent back to the wire itself, its count of each connection | the only witness of the traffic the host no longer sees |
| **mvsw** | the switch chip's registers: a port's frame limit, its error counters | errors and limits that only the switch sees, read from its own registers |
| **shot-run.ps1** | runs each measuring command in a console window of its own and photographs that window when it finishes, with the window's own pixels | the evidence is the screen as it was, not a rendering of the text retyped afterwards, and nothing else on the desktop gets into the picture |
| **tools/mark-images.py** | puts the owner and the licence (CC BY 4.0) in a strip under every photograph; the unmarked original is kept apart | a picture that leaves the page still says whose it is |
| **tools/readme-art.py** | computes each appliance's completion from its family page's table | see the arithmetic below; the checks fail if a page states anything else |

The scripts that drive each run are named on the page of that run.

## The arithmetic

| figure | how it is computed | why |
|---|---|---|
| **throughput, one run** | the receiver's bits per second over the run, as iperf3 prints it | the receiver's count is what arrived |
| **throughput, a soak** | bytes received in the passes that succeeded × 8, over the seconds from the first pass's start to the last one's end | an average over hours, failures not counted as delivered |
| **CPU busy** | `kern.cp_time` read before and after the run; busy = (total − idle) ÷ total, of the differences | what the run cost over all of it, where a glance at `top` picks one moment |
| **share of frames in hardware** | of the frames the coprocessor received from the wire over the run (`RX_WIRE`), the share it sent back to the wire itself (`FROM_WIRE_TO_WIRE`): 6,630,528 of 6,657,897 is 99.6 % | counted by the hardware that forwarded them, not inferred from the host's silence |
| **a counter, trusted** | set against an amount known beforehand: the PC sends exactly 4,000,000,000 bytes, and the coprocessor's count and pf's together must come to what crossed the wire - they came to it within 14 packets in 2.74 million | a counter is trusted only after it has agreed with something counted independently; two were found wrong this way |
| **an integrity soak's result** | passes that failed (curl's exit), passes whose file differed (the hash), and bytes | a soak passes only if both are zero |
| **an appliance's completion** | each row of its family page's table has a done percentage and a weight; completion = Σ(done × weight) ÷ N, where the weights add up to N and a struck value does not count | a percentage that cannot drift from what it weighs: `tools/readme-art.py` computes it, and the checks fail if the page or the README says another |

## The rules every page keeps

| rule | why |
|---|---|
| **How it was taken, and what it does not mean**, on every page | the limits of a figure are part of it: a stream that ends on an appliance's own host measures the smallest of the PC, the forwarding, the link and that host's receive path - not the forwarding ceiling, which needs a machine behind the far appliance |
| **The build is named** - the driver's commit or its checksum | a number belongs to the code that produced it |
| **Every window is photographed** when its run finishes | anyone can see the run, not a copy of its numbers; lab addresses are left in the pictures, serial numbers and hardware addresses are blurred |
| **What a host has learned is forgotten before each run** - its TCP host cache, and the PC's path-MTU cache | a path MTU learned in one run was carried into the next: a whole pass ran with 500-byte segments and was thrown away |
| **What went wrong in the measuring is written up** | the mistakes - a learned MTU, WSL's dropped frames - are why a page measured the way it did, and the next measurement avoids them |
| **A withdrawn figure is struck, not deleted**, the correction beside it | a reader sees what changed, and why |
| **The vendor's figures are labelled the vendor's**, beside ours | they come from Keysight-Ixia BreakingPoint under ideal conditions, with the MTU, the ports and whether both directions count not stated: not the same kind of number as one taken here |

## Where the numbers are

Every measurement, by appliance and by day: [the measurements](README.md).
