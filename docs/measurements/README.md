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
| [Does the coprocessor drop an address it is given?, 2026-10-19](dos-probe-2026-10-19.md) | XGS 3300: no - the vendor's three DoS commands answered "not dispatched" in every form, and a stream from the listed address ran on at 4.4 to 4.8 Gbit/s; one connection can be dropped |
| [Unusual traffic, reported, 2026-10-19](anomaly-2026-10-19.md) | XGS 3300: a port scan of 200 ports, a sweep of 312 addresses (missed by the first version, which read pf only twice a window) and a host sending 5 GB where it sent bytes, each reported in the XGS log within its minute; nothing blocked |
| [Slow minutes on the XGS 136, looked for, 2026-10-19](xgs136-after-boot-2026-10-19.md) | XGS 136: a stream from the PC ran at a quarter of its speed four times; not the links, not the boot's logs, not seen again in 28 minutes of watching and a boot watched; both cages found to hold copper modules |
| [A connection given back by the XGS 136's fast path, and made again at once, 2026-10-19](xgs136-giveback-2026-10-19.md) | XGS 136: the fast path's own rule gave a connection back, and making it again at once made it give it back again - 29 times in 15 seconds, the XGS 3300's way slower in hardware than through the host; with a wait before the remake, level with the host and a quarter of the retransmissions |
| [What the XGS 136's coprocessor says about the traffic it forwards, 2026-10-19](xgs136-counts-2026-10-19.md) | XGS 136: the same messages as the XGS 3300's, read; given to pf, a connection's state came to what crossed the wire, 0 packets apart in 1.37 million either way, and the interfaces to the frame - after two faults found and fixed on the way: arriving bytes counted with their FCS, and counts lost when a connection was made again |
| [What the coprocessor counts of a connection, 2026-10-19](hw-counters-2026-10-19.md) | XGS 3300: the coprocessor's count of a connection and pf's together came to what crossed the wire, to 14 packets in 2.74 million; read from messages the driver had dropped unread |
| [Connections cut by their TLS fingerprint (JA4), 2026-10-19](ja4-cut-2026-10-19.md) | XGS 3300: Suricata fingerprints a connection before it goes into hardware; of two streams from the same program, the one whose JA4 was listed stopped - at once, or within its first two seconds - and the other ran on |
| [A block list that cuts the connections already open, 2026-10-19](lists-cut-2026-10-19.md) | both appliances: a stream in hardware stopped within two seconds of its address being listed, at once or by the minute's cron (60 s and 34 s) |
| [The MTU from 576 to 9000, 2026-10-17](mtu-2026-10-17.md) | the same bench at eight MTUs, clear text and IPsec both ways: 757 Mbit/s to 7.41 Gbit/s into an XGS 3300, the XGS 136's gigabit link full from 1000; 52 runs photographed, in `images/measurements/mtu-2026-10-17/` |

~~Nothing from the XGS 136 is here yet.~~ Its first numbers are on the bench page below.
