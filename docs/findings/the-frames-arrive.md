# The frames arrive, and TCP will not have them

Measured on an XGS 3300 against live traffic, 2026-10-05, with the appliance's owner taking the
readings on the receiving machine. It closes four of the five explanations for `#250` and leaves one.

## The frames leave the right port

The per-port counters answer this, and they had been dismissed once as the wrong instrument - they
are per DPDK port rather than per front port, which is exactly what was needed and was read as
though it were not.

The table is `counter * 256 + port`, measured rather than assumed: `PLATFORM_PORT_MAX_SIZE` is not in
any header this project holds, so indices 0 to 383 were read and the layout fell out of where the
numbers were.

    index 0   = 12,708,721     port 0 received
    index 1   =  1,669,540     port 1 received
    index 256 =  1,675,815     port 0 transmitted
    index 257 = 12,706,179     port 1 transmitted

Port 0 is the switch uplink and port 1 the cage: a download comes in on 0 and goes out on 1, and the
acknowledgements the other way. `SOCA_GET_PORT_FROM_TAG(tag)` is `tag & 0x1f`, so a panel port's tag
`0x8000 | (p << 8)` has zero in its low five bits and resolves to port 0 - every panel port leaves by
the uplink, with an EDSA tag inside the frame choosing which - while a cage's tag of 1 or 2 resolves
to port 1 or 2 directly.

**In ordinary operation the two are equal to the packet:**

    gate closed, nothing programmed, ten seconds
      port 0 received +254,123    port 1 transmitted +254,123
      port 1 received  +30,068    port 0 transmitted  +30,068

**And with a flow programmed, over a two-second window, they are still equal:**

    forwarded +481
      port 0 received +4,689    port 1 transmitted +4,689

Every frame that arrived from the wire was transmitted towards the destination, the 481 forwarded
ones among them. **The egress is correct**: the port tag, the interface id, the `iface2pport` table
and the next hop are all doing what they should.

A ten-second window had said the opposite - 6,028 received against 935 transmitted - and that
reading was worthless: the connection dies within a second of being programmed, so a long window
measures the wreckage rather than the event. **The window has to be shorter than the failure.**

## They arrive, and nothing below TCP objects

Taken on the receiving machine either side of five programmed windows, 1,934 frames forwarded in
total:

| | before | after | delta |
|---|---|---|---|
| `netstat -e` Received Errors | 0 | 0 | **0** |
| `netstat -e` Received Discards | 2,876 | 2,876 | **0** |
| `netstat -e` Received Unicast | 84,205,889 | 87,527,671 | +3,321,782 |
| `netstat -s -p ip` Received Header Errors | 0 | 0 | **0** |
| `netstat -s -p ip` Received Address Errors | 14,530 | 14,586 | +56, background |

- **Not the frame check sequence.** Received Errors is zero, before and after, across 84 million
  packets. The adapter is not rejecting anything.
- **Not the IP header checksum.** Received Header Errors is zero on the same scale.
- **And the translation is applied.** This is the one that turns a negative into a positive:
  `Received Address Errors` did not move. A frame forwarded with its destination left at the WAN
  address would land on a machine that does not own it and be counted there, 1,934 times. It was
  not. **The destination address was rewritten correctly before the frame left.**

So the frames arrive, the adapter accepts them, IP accepts them and hands them up, and **TCP does
not acknowledge a byte of them.**

## What that leaves

The TCP checksum, which no counter on either machine reports. Windows has none for it, the fast
path's 182 counters have none for it, and a forwarded frame never enters the appliance's host, so
nothing there can capture it.

It is a by-elimination conclusion and it is labelled as one. What would turn it into a measurement
is a capture on the receiving machine - one `tcpdump` or Wireshark window during a programmed
period, reading the checksum of a single forwarded segment.

The size hypothesis that would have explained it neatly - that the hardware's L4 checksum offload has
an MTU limit, so full-size frames go out unchecksummed while small ones do not - does not survive
reading `soc_agent_octeon.c`: `ops->get_max_l4_cksum_mtu = NULL` on this family, so the limit is
ARMADA's concern and not this appliance's.

## Lesson

Four explanations died here and every one of them died to a gauge on the far side of the link, after
days of refining gauges on this side that could not, even in principle, tell them apart. The one
that mattered most was the cheapest: `Received Address Errors` not moving says the translation
works, which no measurement on the appliance could have said at all.

And the per-port counters were in hand the whole time and had been judged useless an hour earlier -
"per DPDK port, not per front port" - which was true, and was the wrong reason to put them down.
**A gauge dismissed for measuring the wrong thing should be re-read when the question changes**, and
the question had changed twice since.
