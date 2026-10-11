# Reading the frame settled it, and the fault was in the request all along

Measured on an XGS 3300 against the owner's own live traffic, 2026-10-05. It closes `#250`.

A translated flow is now forwarded by the coprocessor **and delivered**: the destination address and
port are rewritten, the MACs are replaced, the TTL is decremented, the IP checksum is recomputed
correctly, and the connection keeps running while it is accelerated.

## How a forwarded frame was read at all

Nothing on this appliance could see one. A frame the coprocessor forwards to a front port goes on
the wire; it never crosses the SDP, so no capture on the host can reach it, and four days of
measurement had been spent on gauges that could not distinguish the remaining explanations.

**The host is a DPDK port like any other.** The vendor's `soca_octeon_prep_mbuf_for_port` has a
branch for `SOCA_PORT_TYPE_NPU_PF`, and `mv-app-start-otx2.sh` passes `$EAL_NET_PORTS $EAL_SDP_PORTS`
in that order - network ports first. With three network ports, the host is DPDK port 3, and
`SOCA_GET_PORT_FROM_TAG(tag)` is `tag & 0x1f`, so a local tag of 3 names it.

So: `PPORT_UPDATE` maps a spare interface id to tag 3, a next hop points at that interface, and the
frame the fast path builds arrives on this driver's own rings.

Three things had to be fixed before it could be read, and each was its own small discovery.

**It arrives with a different header.** The first capture showed a tag of `0xfc10`, which is not a
tag at all - it is bytes 2 and 3 of this appliance's own MAC, read out of an Ethernet header that was
not where the fixed offset said. A forwarded frame carries a `cvmcs_resp_hdr_t`, which is shorter
than the 82-byte punt prefix. The capture had to start at the buffer, not past a prefix that is not
there.

**The control channel shares the path.** A frame with a tag no interface owns is dropped as
untagged - and so is every control message, on tag 254, which the link poll sends once a second. The
first capture caught one of those. The buffer had to be filtered.

(Whose message that is was read on 2026-10-09, with a capture long enough to reach its own header:
the coprocessor's. With a tunnel up it is a statistics message once a second - type 2, the
associations' counters - and under load a connection-statistics message that can be longer than a
receive buffer: [a packet is not a buffer](a-packet-is-not-a-buffer.md). What was arriving on the
day this page was written, with no tunnel, was not read.)

**And the frames are rare against the traffic.** `dp.rx_frame` holds whatever arrived last, and four
hundred reads of it never caught one of 285 frames known to be present. Capturing in the untagged
branch catches them and nothing else.

## What the frame said

With the request as it had been written for four days:

    c8 d9 d2 2b 10 42  58 9c fc 10 50 b4  08 00     destination MAC, source MAC, IPv4
    45 00 05 ac 5c dc 40 00  3b  06  cf c2          TTL 59 - decremented
    c0 a8 46 2e                                     source      the WAN address
    c0 a8 46 2e                                     destination the WAN address
    ad d7  ad d7                                    ports 44503 and 44503

The flow was `<a server>:443 -> <the WAN address>:44503` and should have become
`-> <the PC>:54080`. **The WAN address is in both address fields and the WAN port in both port
fields.** The fast path was doing exactly what it was told; it had been told nonsense.

## The two faults, both in the request

**The NAT block was filled from the frame's point of view, and it wants the connection's.**
`ipv4_orig_src` is the machine that *opened* the connection, in its original form - not the source
address of whichever frame is in hand. For a reply arriving from the internet:

| field | value |
|---|---|
| `ipv4_orig_src`, `orig_src_port` | the PC, its own port |
| `ipv4_orig_dest`, `orig_dest_port` | the server, 443 |
| `ipv4_nat_src`, `nat_src_port` | the WAN address, the translated port |
| `ipv4_nat_dest`, `nat_dest_port` | the server, 443 |

All four come straight out of `pf`: the originator is the stack key's far end, the responder is
either key's near end, and the translated originator is the wire key's far end. Nothing has to be
computed.

With that corrected the source came out right - `5e ca cf 3b`, the server - and the destination was
still the WAN address.

**And the direction flag describes the connection, not the frame.** `do_snat` and `do_dnat` say what
the *connection* does, and the fast path applies the inverse on the reply. This connection is source
translated on the way out, so it is `do_snat`, and setting `do_dnat` for a reply frame - which is
what the frame in hand looked like it needed - leaves the destination alone.

## The frame, correct

    c8 d9 d2 2b 10 42  58 9c fc 10 50 b4  08 00
    45 00 05 ac 18 70 40 00  3b  06  ce b5
    5e ca cf 3b      source      <the server>
    c0 a8 64 78      destination <the PC>        translated
    01 bb            source port 443
    d3 40            destination port 54080      translated

The IP header checksum was verified by hand from the bytes above: the one's complement of the folded
sum is `0xCEB5`, and the frame carries `ce b5`. **The fast path recomputes it correctly** - which
also retires the checksum hypothesis that four earlier measurements had been unable to test.

## And the connection lives

Pointed at the destination's real port rather than at the host, over an eight-second window:

    forwarded                     +1,130
    the connection's ACKs in pf   392,651 -> 392,754

**+103.** In every earlier run that number was frozen at precisely its starting value for the whole
window - 8,695, then 81,537, then 313,187, then 589,674, then 68,082, five times, never moving by
one. It moves now.

## Lesson

The question was "why does the coprocessor's forwarding not work", and the answer was that it works
perfectly and had been handed a malformed request. Every measurement that treated the coprocessor as
the suspect - counters, port tables, interface ids, bridge membership, the receiving machine's error
statistics - was looking at the wrong side, and each one came back clean because each one was clean.

**The thing that was never measured was the thing being sent.** It took three days to go from "the
frames vanish" to "read one", and the reading answered it in a single line of hex. When every gauge
on both ends of a path reports healthy, stop measuring the path and look at what you put into it.
