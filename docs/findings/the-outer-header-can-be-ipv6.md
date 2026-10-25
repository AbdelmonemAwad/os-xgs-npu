<!-- reviewed: 2026-10-20 -->
# The outer header can be IPv6

Measured on an XGS 3300, 2026-10-12, module `c644d91b`. The first item of
[issue 294](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/294), its outer half: an
association whose two ends are IPv6 addresses - the tunnel's outer header - offered to the
coprocessor instead of refused, with IPv4 inside as before. Taken, both directions, at the plain
tunnel's rates with GCM and at the block cipher's with CBC. The inner half, IPv6 inside the tunnel,
is not done and is not the driver's to decide alone: see the end.

The XGS 136 takes an IPv6 outer header on its own engine since 2026-10-20, both ways, with GCM and
with CBC ([measured](../measurements/ipv6-136-2026-10-20.md)).

## What changed in the driver

The vendor's association request has carried four words per address and an `ipv6` option bit
("Ena/Dis Outbnd IPv6 Header format") since the first day; the driver filled one word and left the
bit clear. Now:

- the direction check takes a pair of `AF_INET6` ends (the interface that owns the destination
  takes the inbound association, the one that owns the source the outbound, as for IPv4);
- the record carries the sixteen bytes of each address and a flag; the request gets all four words
  (big-endian, as the vendor's host hands them) and option bit 21;
- the next hop of an IPv6 far end is resolved by a twin of the IPv4 resolver: `fib6_lookup` for the
  route, `nd6_resolve` for the neighbour, which sends the solicitation on the way out as
  `arpresolve` sends the request;
- the envelope writes a 40-byte IPv6 outer header (ethertype `0x86DD`, payload length, next header
  ESP or the NAT-T UDP, hop limit 64, the two addresses); the trailer, the IV and the ICV are as
  the cipher wants them, unchanged;
- the termination takes an IPv6 outer header on what the coprocessor hands up (next header at its
  sixth byte, ESP at +40) and asks the kernel for the association with an `in6` destination;
- the table prints the addresses; the flow path, whose outer addresses are IPv4, skips such an
  association; the rekey-replacement matcher compares the sixteen bytes.

## The three measurements, over IPv6

The lab tunnel's endpoints moved to a ULA /64 laid on the 10G path at both ends (runtime
aliases, as the IPv4 lab address is), the selectors left IPv4, `dp.auto` 1, `ipsec.flows` 0.

| proposal | installed and read back | inbound, the peer's frames | outbound, the appliance's frames | up x4 / down x4 / up x1 / down x1, Mbit/s |
|---|---|---|---|---|
| `aes128gcm16` | both rows, the table printing `fd00:…::2 -> fd00:…::1` and the reverse; the kernel's two with `mode=tunnel` | `rx_done` +4,502,986, `rx_bad` 0, `rx_nosa` 0, `rx_nokey` 0; `FROM_WIRE_TO_IPSEC_DECR` +4,503,704, `CRYPTO_DROP_REPLAY_OOW` +47, nothing else | `tx_encrypt` +2,551,889 = `FROM_KN_TO_IPSEC_ENCR` = the peer's ESP input; its replay and authentication drops unchanged; `out_nonhop` 0 | **935.9 / 941.1 / 786.4 / 773.2** |
| `aes128-sha256` | both rows | +3,102,597, 0; +3,103,086, `REPLAY_OOW` +41 | +1,989,485 = `ENCR` = the peer's input | 674.6 / 589.4 / 594.0 / 502.8 |
| `aes256-sha1` | both rows | +3,156,423, 0; +3,156,998, `REPLAY_OOW` +45 | +2,024,805 = `ENCR`; the peer's input +2,023,733 (1,072 short, its authentication drops 0 - the second time with SHA-1, see [a block cipher at two thirds](a-block-cipher-at-two-thirds.md)) | 691.7 / 589.6 / 604.7 / 517.5 |

Every ping answered; a 1,472-byte DF ping "needs to be fragmented"; with GCM a 1,398-byte ping
passes and the largest inner packet is 1,426, twenty less than over IPv4 for the twenty bytes the
outer header grew. The same tunnel over IPv4 the same hour: 947 / 957 / 789 / 791 with GCM, 670 /
609 / 602 / 561 with CBC-SHA256 - the outer family costs nothing the rates can see.

**The IV, settled on the way.** Five of the appliance's CBC frames captured at the peer: sequence
numbers 0x100001 to 0x100005, the coprocessor's; the sixteen bytes after them different in every
frame and random-looking. Whether they are the envelope's `arc4rand` bytes or the coprocessor's own
is not knowable from the wire, and does not matter: the IV is not fixed.

## Five attempts to raise it

The first run measured nothing three times over, each for a different reason, all recorded:

1. The peer's alias was still `tentative` (duplicate address detection, about a second) when it
   initiated IKE to an address it had never resolved: five retransmits, no SA. The tool now waits,
   and primes neighbour discovery from the appliance, whose solicitation carries its own link
   address so one exchange fills both caches.
2. With the caches full the appliance's own packet filter blocked the peer's IPv6 IKE on the lab
   port: its automatic IPsec rules still named the IPv4 peer, and a filter reload left them so (the
   generator keys the family on the phase-1 protocol). A runtime rule appended to the loaded
   ruleset admits the peer for the lab's duration; the filter reload at the end drops it.
3. With IKE through and the pair mirrored - the inbound half working from the first frame, `rx_done`
   climbing on the peer's pings - every packet of the appliance's own was refused by its next-hop
   resolver, `out_nonhop` one per packet. Instrumented and read: `nd6_resolve` wants the address
   family in the upper half of its second argument (`LLE_SF(AF_INET6, is_gw)`, as `ether_output`
   passes it); with a plain `is_gw` the neighbour lookup runs for family 0, misses, takes the slow
   path and answers `EWOULDBLOCK` for ever while `ndp` shows the neighbour reachable. `arpresolve`
   takes a plain `is_gw`, which is why the IPv4 twin never showed it. One line.

## What is not established

- ~~**IPv6 inside the tunnel.** The termination still refuses an inner IPv6 packet (`rx_v6`), the
  envelope's next-header byte is IPv4, and the peer's own rule for decrypted traffic is IPv4-only
  (`pass in quick on enc0 inet`), which is its owner's to change, not this lab's. The driver side is
  a day's work; the lab needs that rule and an IPv6 address on the machine behind the appliance.~~
  Done the same morning, with a temporary rule on the peer:
  [IPv6 inside the tunnel, delivered](ipv6-inside-the-tunnel.md).
- Why the peer counts about 0.05% fewer frames than the coprocessor sends with HMAC-SHA1, twice
  now, and never with SHA-256 or SHA-512 or GCM; its authentication and replay drops stay at 0.
- NAT-T over IPv6: the envelope writes the UDP header after an IPv6 outer header exactly as after
  an IPv4 one, and the coprocessor's `overhead` accounting adds the two together; not run.
- An IPv6 far end behind a gateway: the resolver takes the gateway's address from the route as the
  IPv4 one does; the lab's peer is on-link.

## Lessons

- **A twin written from the IPv4 one inherits its calling convention, not the IPv6 one.** The
  kernel's two resolvers look alike and are not; the cost was three void raises of the lab and one
  instrumented reboot. When a counter climbs with no other movement, instrument the exits and read
  the kernel's function before raising the lab again.
- **The receive half working is not the tunnel working.** As with NAT-T's first build and ESN's
  dead downloads: `rx_done` climbed from the first frame, and the pings still died in the other
  direction.

## Where it sits

The resolver is [octep_nhop.c](../../contrib/octep/octep_nhop.c) (`octep_nhop_resolve6`); the
direction check, the record, the envelope and the termination are in
[octep_ipsec.c](../../contrib/octep/octep_ipsec.c); the request's four words and option bit in
[octep_rpc.c](../../contrib/octep/octep_rpc.c). The numbers are in
[the measurements](../measurements/xgs3300.md#an-ipv6-outer-header).
