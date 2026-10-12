# IPv6 inside the tunnel, delivered

Measured on an XGS 3300, 2026-10-12, module `f9f5ba7e`. The second half of
[issue 294](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/294)'s IPv6 item: an IPv6 packet
carried in the tunnel, encrypted by the coprocessor on the way out and decrypted, terminated and
handed to the kernel's IPv6 input on the way in. Over IPv4 outside, with AES-GCM-16, sourced at the
appliance itself, at 1.8 and 2.1 Gbit/s up and 1.3 and 1.7 down.

## What changed in the driver

The trailer's next-header byte is where the inner family lives, and the driver had only read it
to refuse `41`. Now:

- the termination takes `IPPROTO_IPV6` beside `IPPROTO_IPV4`: the inner packet's version nibble
  and its payload length plus forty are checked against what is left after the trailer, the
  decrypted packet is shown to enc0 with `AF_INET6` and run through the IPv6 filter head, and
  queued to `NETISR_IPV6`; `rx_v6`, which counted refusals, counts deliveries;
- the ESP interposition, which gets the packet with the outer header the kernel built, takes an
  outer protocol of `41` beside `4` and writes it as the trailer's next header; the tagged path
  takes ethertype `0x86DD` the same way;
- a too-big IPv6 packet is not fragmented (RFC 8200 4.5): a forwarded one is answered with ICMPv6
  Packet Too Big carrying the size that fits, the appliance's own gets `EMSGSIZE`;
- the flow candidate, whose tuple parser is IPv4, is not offered an IPv6 packet.

## The three measurements

The lab tunnel over its IPv4 10G path, with IPv6 selectors: a ULA /64 on the appliance's LAN bridge
and the peer's ULA /128 on its bridge. The traffic is the appliance's own - the machine behind it
could not be given an IPv6 address from the bench - so the outbound frames go through the ESP
interposition, as every packet the appliance originates does.

| | |
|---|---|
| the association | IPv4 outside, the table's two rows as for any other; the kernel's policies `fd00:…::/64 <-> fd00:…::2/128` with `esp/tunnel/10.99…` at both ends |
| five pings through the tunnel | 5/5, 0.34 ms |
| one stream up / down, 10 s | **1,820.8 / 1,325.5 Mbit/s** |
| four streams up / down, 12 s | **2,136.0 / 1,699.7 Mbit/s** |
| inbound, the peer's frames | `rx_v6` +7,395,177 delivered to IPv6 input, `rx_bad` 0, `rx_blocked` 0; `FROM_WIRE_TO_IPSEC_DECR` +7,395,602; `rx_done` +7,184,569 - the 210,000 between the two are `rx_queuefail`, the IPv6 netisr queue refusing frames at 1.7 Gbit/s into the host's own stack, which TCP covered |
| outbound, the appliance's frames | `tx_encrypt` +7,397,169 = `out_taken` = `FROM_KN_TO_IPSEC_ENCR`; the peer's ESP input +7,397,169 exactly, its authentication drops 0; `out_orig` 0 - every packet took the interposition |
| the largest packet | a 1,398-byte ping passes (inner 1,446, the same as IPv4 inside); 1,402 is refused and not fragmented |
| the coprocessor | `CRYPTO_DROP_REPLAY_OOW` +94 in 7.4 million at a window of 32, `TX_DROP_QUEUE_FULL` +3 |
| the host | `netisr` 68.6 %, the load generator itself 63.8 %, one `octep0` thread 54.9 % |

The rates are higher than every figure on these pages because this is the first measurement with
the appliance as the TCP endpoint on the 10G path: no 1 Gbit/s port in the way. They are the
coprocessor's GCM through the host's own IPv6 stack, both ways, and 2.1 Gbit/s is what the host's
`netisr` could feed it before the queue refused frames.

## Four raises for one measurement

Each void one is a fact about the kernel, not the driver:

1. The policies were installed at both ends and nothing moved: `ping6: No route to host`. This
   kernel's `ip6_output` selects the route (`in6_selectroute`) before it applies the policy
   (`IPSEC_OUTPUT`), so an inner IPv6 destination needs a route even though the policy will wrap
   the packet. The IPv4 lab never showed it: its inner destination is on-link.
2. The route added (the first time, not at all: the lab tool's `2>&1` in a csh command string).
3. With the route, `ping6: Network is down`: the kernel's offload path hands the tagged inner
   packet to the association's port as a plain IPv6 frame for the driver to wrap, and a port
   without an IPv6 address is `ifdisabled` in OPNsense, so `nd6_resolve` answers `ENETDOWN`.
4. A ULA on that port in the peer's /64 makes the inner destination on-link and the port
   IPv6-capable; the first ping answered.

So an IPv6 selector through this offload needs what an IPv4 one needs and OPNsense gives for free
in IPv4: a route for the inner destination out of the association's port, and that port speaking
the inner family.

## The forwarded path, the same afternoon

The bench machine given a temporary address in the appliance's LAN /64 and a route to the peer's
/64 through the appliance (its owner approving the elevation), the same lab raised again:

| | |
|---|---|
| five pings from the machine through the tunnel | 5/5 |
| one stream up / down, 10 s | 760.0 / 802.7 Mbit/s |
| four streams up / down, 12 s | 935.3 / 946.6 Mbit/s - the machine's 1 Gbit/s port, as with IPv4 inside |
| inbound, delivered and forwarded on to the machine | `rx_v6` +3,613,105, `rx_bad` 0, `rx_blocked` 0; `FROM_WIRE_TO_IPSEC_DECR` +3,613,476 |
| outbound, the machine's packets through the tagged path | `tx_encrypt` +2,084,625 = `FROM_KN_TO_IPSEC_ENCR`; the peer's ESP input +2,084,625 exactly, its authentication drops 0 |
| the largest ping from the machine | 1,398 bytes of data **lost** (3/3), where the same size sourced at the appliance passes; 1,402 lost as it should be |

So the forwarded path carries IPv6 at the port's rate both ways. The 1,398-byte ping is the one
thing it does differently from the appliance's own: the kernel's offload path has its own size
check before the driver's, and where it draws the line for a tagged IPv6 packet was not bisected.

## What is not established

- ~~The forwarded path for IPv6: a machine behind the appliance sending IPv6 into the tunnel takes
  `octep_ipsec_tx_prepare` with the kernel's tag, compiled and not run - the bench machine could not
  take an IPv6 address.~~ Run the same afternoon, above.
- ~~The exact largest IPv6 packet a machine behind the appliance can send into the tunnel, and
  whether it gets Packet Too Big for a larger one: 1,398 bytes of ping data is already refused on
  the forwarded path, not bisected, not captured.~~ Bisected the same hour: 1,300 bytes of ping
  data pass and 1,350 do not, so the line is between 1,348 and 1,398 bytes of inner packet, a
  hundred below what the driver takes from the appliance itself; the driver's counters do not
  move for a refused one and the appliance's kernel counts a Packet Too Big per attempt, so it is
  the kernel's own line on the tagged path. **Settled, by reading and then by the byte**: a
  forwarded IPv6 packet entering an IPsec policy passes `ipsec6_check_pmtu` (`ipsec_output.c`)
  before the offload decision, and that check subtracts `ipsec_hdrsiz_internal` - the policy's
  worst case, computed with no association in hand - from the far end's path MTU. The worst
  case is `sizeof(newesp)` 8 + `EALG_MAX_BLOCK_LEN` 64 (ChaCha20's native block, "keep this
  updated") + 9 + 16, plus the outer IPv4 header 20: **117 bytes**, where AES-GCM-16 really
  needs 54. So on a 1,500-byte path the kernel refuses an inner packet above **1,383** with
  Packet Too Big; the bisect on the bench found 1,335 bytes of ping data passing and 1,336 not,
  which is 1,383 to the byte. The path MTU comes from the TCP host cache and, when the cache has
  no entry, from the route, which the check then writes into the cache - so the gate is always
  there (purging the cache changed nothing, and the entry was back after one ping). The IPv4
  twin, `ipsec4_check_pmtu`, is off unless `net.inet.ipsec.dfbit` is set, which is why a
  1,446-byte IPv4 packet with DF passes the same tunnel; the appliance's own packets skip the
  check (`if (!forwarding) return 0`), which is why 1,446 bytes pass from the appliance itself.
  Nothing in the driver can move the line; what would is the kernel using the association's own
  header size (`esp_hdrsiz(sav)`, which `ipsec6_check_pmtu` has in hand) instead of the policy's
  worst case - 63 bytes of path MTU for every AES-GCM tunnel that forwards IPv6 - which is a
  change for FreeBSD, proposed on the issue. In the meantime TCP through the tunnel adapts to
  the 1,383 it is told, as the four-stream rates above show.
- The peer's side under its own offload: the peer runs the kernel's cipher.

## Lessons

- **A kernel that is told to encrypt still asks for a route first.** Two of the four raises were
  the kernel refusing before the policy was consulted; the driver's counters were all zero both
  times, which said where to look.
- **Every `2>&1` in a quoted ssh command to a csh root is a measurement lost.** Fourth time this
  day; the tools now have none.

## Where it sits

The termination, the interposition and the too-big answer are in
[octep_ipsec.c](../../contrib/octep/octep_ipsec.c) (`octep_ipsec_rx`, `octep_ipsec_xf_output`,
`octep_ipsec_send_inner`, `octep_ipsec_tx_prepare`). The numbers are in
[the measurements](../measurements/xgs3300.md#ipv6-inside-the-tunnel).
