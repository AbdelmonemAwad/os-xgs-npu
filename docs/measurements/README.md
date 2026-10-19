<!-- reviewed: 2026-10-17 -->
# Measurements, by appliance

One page per machine, because a measurement belongs to a board rather than to a family. A second
appliance of the same model with a different kernel, a different cable or a different neighbour will
produce different figures, and anything that generalises belongs in the family page instead.

Each page says **how** every number was taken and **what it does not mean**, because a figure
without its method is not evidence of anything, and the ones that look best are usually the ones
measuring something other than what they claim.

| appliance | what has been measured |
|---|---|
| [XGS 3300](xgs3300.md) | round trip and loss, the transmit ceiling and where it is spent, two interface counters that the measuring found to be wrong, the receive path, a kernel update, a bridged port, ten hours at light load, the candidate table with more than one user, and a receive ring behind the block build by build |
| [The three-appliance bench, 2026-10-17](bench-2026-10-17.md) | the two XGS 3300s and the XGS 136 routed to each other, a PC at 10 Gbit/s: clear text and IPsec (AES-GCM-256) with a hardware peer at both ends, every run photographed |
| [A block list that cuts the connections already open, 2026-10-19](lists-cut-2026-10-19.md) | both appliances: a stream in hardware stopped within two seconds of its address being listed, at once or by the minute's cron (60 s and 34 s) |
| [The MTU from 576 to 9000, 2026-10-17](mtu-2026-10-17.md) | the same bench at eight MTUs, clear text and IPsec both ways: 757 Mbit/s to 7.41 Gbit/s into an XGS 3300, the XGS 136's gigabit link full from 1000; 52 runs photographed, in `images/measurements/mtu-2026-10-17/` |

~~Nothing from the XGS 136 is here yet.~~ Its first numbers are on the bench page below.
