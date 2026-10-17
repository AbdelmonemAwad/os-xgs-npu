<!-- reviewed: 2026-10-14 -->
# Fragmentation before the envelope on the ARMADA, and a neighbour not yet resolved

2026-10-14, the XGS 136, step 5 slice 6 of [the offload plan](../npuep/offload-plan.md). With
[both directions in hardware](the-engine-encrypts-outbound-esp-on-the-armada.md), the two things
that page left undone on the transmit side: an inner packet whose envelope does not fit the port's
MTU, and an encrypt association whose far end the port has not resolved yet. Module build
`8307c478`, from the working tree; the direct-link bench, the peer's inner address inside the
appliance's LAN prefix as the previous page describes.

## What the driver does now

One send path for both doors, `npuipsec_send_inner`, the OCTEON TX's `octep_ipsec_send_inner`: an
inner packet that fits is wrapped and sent as before. One that does not is fragmented first, at the
largest size whose envelope fits (`ip_fragment`), each fragment's header checksum redone - the
kernel leaves it to an interface that offloads it, and this one does not - and each fragment wrapped
and sent as its own ESP frame, taking its own number on the engine. A packet that must not be
fragmented (DF), or one no fragment of which would fit, is not sent: a forwarded one is answered
with ICMP "fragmentation needed" naming the size that fits, as the kernel would; one this host
generated is dropped and its sender told by the error. Counters `tx_fragmented` and `tx_needfrag`.

And the next-hop resolver fills the port, its MTU and the source MAC before it asks the neighbour, so
a caller told `EWOULDBLOCK` - the neighbour has just been asked for - still knows which port the
route leaves by; the encrypt accept takes that answer as the OCTEON TX's does, instead of declining
the association for a neighbour it would have had a moment later.

## Measured

- **From the appliance**, 1472 bytes of payload (a 1500-byte packet, no DF), five pings: **5 of 5**,
  `tx_fragmented +5`, each as two ESP frames. These came through the interposed transform, not the
  kernel's own offload door: the kernel does not take a packet that size on that path, and hands it
  to the transform - the driver's `forwarded` counter has them. With DF: `ping: sendto: Message too
  long` for each, `tx_needfrag +3`, nothing sent.
- **Forwarded**, the PC behind the appliance's bridge, 1472 bytes, no DF, ten pings: **10 of 10**,
  14 ms, `tx_fragmented +10`, `FROM_KN_TO_IPSEC_ENCR +20` (two frames each), the ten replies
  decrypted by the engine and terminated (`receive: done +10`, `FROM_WIRE_TO_IPSEC_DECR +10`); the
  kernel's `fragments created` grew by twenty. With DF, three pings: the PC printed `Reply from
  <the appliance's LAN address>: Packet needs to be fragmented but DF set` for the first and refused
  the next two itself from the path MTU it had just learned; `tx_needfrag +1`.
- **The accept with the neighbour unresolved**: the peer's ARP entry deleted, the child terminated and
  re-initiated - the install runs within milliseconds, before any traffic could resolve it - and the
  new encrypt association was accepted: `accepted_encrypt 1 → 2`, `accept_encrypt_fail 0 → 0`,
  `SA_ADD accepted by the engine` on its line; three pings through it, 3 of 3. (Data traffic cannot
  show this: `ether_output` resolves the neighbour before any frame reaches the driver.)
- Nothing on `nosa`, `drop`, `nonhop`, `toobig` or `nobuf` throughout.

## Not done, and said so

- NAT-T (the UDP header in the envelope), CBC with HMAC, IPv6 outer and inner, extended sequence
  numbers: still declined to the kernel.
- The open-entry regime on this bench, and the flow-integrated tunnel path (a microflow carrying
  `sa_index`): still to measure.
- Throughput through the tunnel on a quiet bench is not measured on this family yet; every number
  above is a count, not a rate.
