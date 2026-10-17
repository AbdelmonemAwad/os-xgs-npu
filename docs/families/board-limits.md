<!-- reviewed: 2026-10-18 -->
# Each board's own limits

Written 2026-10-18. The two drivers no longer carry bench-sized tables: each sizes itself from what
the coprocessor says about its own board, and from the host's memory, and each figure can be set by
hand at the loader. This page is for an owner with any XGS that has a coprocessor: what the driver
will use on your board, where the number comes from, and what has been measured.

**Only the XGS 3300 (OCTEON TX) and the XGS 136 (ARMADA) have been measured.** Everything else in the
tables below is what the code will do on other boards of the same family; it has not been run on
them.

## Where the numbers come from

Every board answers `PLATFORM_READ` with its platform block - `struct platform_info` in the vendor's
tree - which states how many connections, next hops and IPsec associations the coprocessor holds,
how many cores it has and how many high-priority rings it asks the host for. The command changes
nothing on the far side. Both drivers read it once and print it:

    octep0: rpc: the board holds 2000000 connections, 65536 next hops and 4001450 microflows, ...
    npuep0: rpc: platform XGS_DT2 (id 1, assembly AMDA0201-0003): 4 cores, 128 interfaces, 1 high ring(s), 1500000 connections, 65536 next hops, 16384 associations, microflow timeout 10 s

| | XGS 3300 (measured) | XGS 136 (measured) |
|---|---|---|
| platform | XGS_1US, AMDA0202 | XGS_DT2, AMDA0201-0003 |
| coprocessor cores | 20 | 4 |
| connections | 2,000,000 | 1,500,000 |
| next hops | 65,536 | 65,536 |
| IPsec associations | - | 16,384 |
| high-priority rings asked for | 1 | 1 |
| default microflow timeout | 10 s | 10 s |
| host | Ryzen V1780B, 16 GB | Ryzen R1606G, 8 GB |

## What the drivers take

**The connection table** is the board's figure, or what a sixteenth of the host's memory holds if
that is fewer. An entry, with its share of the hash chains, the free stack and the live list, is
284 bytes on the OCTEON TX driver and about 170 on the ARMADA one.

| | XGS 3300 | XGS 136 | a host with 8 GB | a host with 4 GB |
|---|---|---|---|---|
| OCTEON TX (`octep`) | **2,000,000, 568 MB** | - | about 1.8 million | about 900,000 |
| ARMADA (`npuep`) | - | **1,500,000, 262 MB** | the board's figure | about 1.4 million |

The OCTEON TX driver allocates at attach, before the block is read, and the block then lowers what
it will use if the board holds fewer. The ARMADA driver allocates when the block has been read, at
the board's own size; if a board never answers, it allocates its family default after two minutes.

**The next-hop table** is the board's figure (65,536 on both) and only the part in use is searched.

**The rate connections are handed over.** On the XGS 3300, flow writes go to the high-priority ring
the board asks for, posted without waiting for each answer; up to 8,000 a second. On the XGS 136 every
command now waits for its answer by spinning for up to 2 ms instead of sleeping 10 ms each time: the
average answer took 138 microseconds over the first 399 commands after a boot, and the sweep makes up
to 256 connections a second (`flow.make_per_sweep`).

**Transmit rings.** The OCTEON TX host sends on as many of the board's datapath rings as it has
CPUs, up to seven beside the first: on the XGS 3300, forwarding with offload off went from 4.31 to
9.35 Gbit/s (measured on 2026-10-17; its page is not published yet). ~~The ARMADA driver still
transmits on one queue.~~ The ARMADA driver sends on all four transmit queues the device is given,
each with its own lock and buffers, a connection kept to one queue by its flow id - measured on the
XGS 136 with every queue in use and nothing falling back, the gigabit link full either way
([ipv6-and-queues-2026-10-18](../measurements/ipv6-and-queues-2026-10-18.md#the-xgs-136s-four-transmit-queues)).

**IPv6.** Both drivers hand untranslated IPv6 connections to the coprocessor. A connection pf
translates, or one an IPsec policy covers, stays with the host. On the XGS 3300, IPv6 from the PC to
a server behind another port ran at 9.24 Gbit/s, and 9.27 back, with all eighteen directions in
hardware ([ipv6-and-queues-2026-10-18](../measurements/ipv6-and-queues-2026-10-18.md#ipv6-through-the-xgs-3300)).
On the XGS 136 the switch in front of the coprocessor drops multicast until it is told otherwise, so
IPv6 could not find a neighbour on a routed port; the driver now opens multicast on every port whose
interface has joined a group (the agent's `ALLMULTI`), which every interface with an IPv6 address has
([the finding](../findings/the-switch-drops-multicast-on-the-armada.md)). The 136's IPv6 flows are
built and installed and have not had traffic through them yet.

## Setting them by hand

All are loader tunables (System: Settings: Tunables, then a reboot) or sysctls that take effect at once.

| setting | driver | what it does | default |
|---|---|---|---|
| `hw.octep.flow_max` | OCTEON TX | connection table size, 1,024 to 2,000,000 | the board's, capped by a 16th of memory |
| `hw.octep.txq` | OCTEON TX | transmit rings beside the first | 7, capped by CPUs and the board's rings |
| `dev.octep.0.rpc.hi_post` | OCTEON TX | post flow writes on the high-priority ring | 1 |
| `dev.octep.0.dp.fast_make_per_sec` | OCTEON TX | connections made a second at most | 8000 |
| `hw.npuep.flow_max` | ARMADA | connection table size, 1,024 to 2,000,000 | the board's, capped by a 16th of memory |
| `dev.npuep.0.flow.make_per_sweep` | ARMADA | connections made a second at most | 256 |
| `hw.npuep.giu.txq` | ARMADA | transmit queues the stack sends on; 1 sends everything on the first, as before 2026-10-18 | 4, capped by CPUs |

The driver says on every boot which it used and why:

    octep0: flow table: 2000000 connections, 2097152 chains, 568388608 bytes - this host's own size (the family's 2000000; a 16th of 16282 MB of memory holds 3757274)
    npuep0: flow table: 1500000 connections, 2097152 chains, 65536 next hops, 262223616 bytes - this host's own size (the board's 1500000; a 16th of 8098 MB of memory holds 3085905)

## Which boards this reaches

| family | coprocessor | boards (from the assembly map) | this page applies |
|---|---|---|---|
| OCTEON TX | `177d:a300` | the 1US boards, AMDA0202: XGS 2100, 2300, 3100, 3300 | yes - measured on the XGS 3300 |
| ARMADA | `11ab:7080` | AMDA0200 XGS 87/107, AMDA0201 XGS 126/136, AMDA0208 XGS 116 | yes - measured on the XGS 136 |
| OCTEON TX2, TX2 98XX | `177d:b200`, `177d:b100` | the larger boards | no driver here yet |
| GR, TOPAZ | none | boards without a coprocessor | nothing here applies |

Which family the 1UL boards (AMDA0203, XGS 4300/4500) and the 88-138 generation belong to has not
been read off a board; the driver that attaches is the answer.
