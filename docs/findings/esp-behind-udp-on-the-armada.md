<!-- reviewed: 2026-10-14 -->
# ESP behind UDP on the ARMADA: NAT-T, both ways in hardware

2026-10-14, the XGS 136, step 5 slice 7 of [the offload plan](../npuep/offload-plan.md). The
OCTEON TX [took NAT-T on 2026-10-12](esp-behind-udp.md); this family's turn. Module build
`71abd87e`, from the working tree; the direct-link bench with `encap = yes` on both ends, which makes
strongSwan encapsulate without a NAT in between - the child installs as `TUNNEL-in-UDP` with both
ends on port 4500.

## What the driver does now

`SA_ADD` for an association with encapsulation sets `udp_enable` (bit 22 of the options) and the two
ports (the far end's at +160, this end's at +162, big-endian): what the vendor's own host sets for
such a state, and what the OCTEON TX driver measured - the engine removes the UDP header on receive
and expects it in the frame it is handed to encrypt. So the envelope carries it, RFC 3948's eight
bytes between the outer IPv4 header and the ESP header - the association's two ports, the length of
what follows the IP header, no checksum - and the room the envelope needs is eight bytes more, which
the fragmentation threshold accounts for. The receive termination already took both shapes. Until this
slice an encrypt association behind a NAT was declined, and a decrypt one would have been accepted
without the bit - a black hole that never opened only because no such association was offered; both
directions now take it, with the ports checked non-zero.

One trap on the way: `encap` is a connection-level option in `swanctl.conf`, not a child one. Put
in the child block it stops the whole connection from loading, and the tunnel simply never comes up.

## Measured

Every frame from the appliance on the wire: `<the appliance>.4500 > <the peer>.4500: UDP-encap:
ESP(spi=…)` - the port's own capture, before the engine wrote the sequence.

| | result | on the appliance |
| --- | --- | --- |
| appliance to peer, 5 pings | 5 of 5 | `generated +5`, `tx_natt +5` |
| appliance to peer, 3 pings of 1472 bytes | 3 of 3 | `forwarded +3` (the transform's door), `fragmented +3`, `tx_natt +6` - each fragment its own frame with its own UDP header |
| peer to appliance, 10 pings | 10 of 10, 5.3 ms | `receive: done +10` - the header removed by the engine; the 10 replies `generated +10`, `tx_natt +10` |
| PC behind the appliance to the peer, 10 pings | 10 of 10, 15 ms | `forwarded +10`, `tx_natt +10`; replies `done +10` |
| the same, 5 pings of 1472 bytes | 5 of 5, 14 ms | `forwarded +5`, `fragmented +5`, `tx_natt +10`; replies `done +5` |

Totals over the run: `tx_natt 41` = every envelope and fragment sent (15 generated, 10 forwarded
whole, 16 fragments); `FROM_KN_TO_IPSEC_ENCR 41`, one per frame; `receive: done 33`,
`FROM_WIRE_TO_IPSEC_DECR 33`; `netstat -s -p esp` at zero packets out and zero dropped - nothing in
software, nothing refused. The peer's child counted 21 in, 18 out at its last reading.

## Not done, and said so

- CBC with HMAC, IPv6 outer and inner, extended sequence numbers: still declined to the kernel.
- A real NAT between the two ends is not on this bench; `encap = yes` exercises the same code on
  both sides, the encapsulation and the engine's removal of it, but not a translator rewriting the
  ports.
- Throughput through the tunnel on a quiet bench is still a count, not a rate, on this family.
