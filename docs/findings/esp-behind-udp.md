<!-- reviewed: 2026-10-12 -->
# The UDP header is the host's to write

Measured on an XGS 3300, 2026-10-12, modules `b2564f90` and `1707f625`. The first item of
[issue 294](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/294) run: an association behind
NAT - ESP inside UDP, RFC 3948, what strongSwan calls `encap` and the kernel marks `esp-udp` -
offered to the coprocessor instead of refused.

## What the driver refused, and why

Until this day [installed first, taken second](installed-first-taken-second.md)'s driver answered
`EOPNOTSUPP` to any association with a NAT-T record on it, because both of its own halves spoke
plain ESP: the receive termination looked for protocol 50 after the outer IP header, and the
envelope it lays out for the coprocessor to encrypt in place put protocol 50 there. The
coprocessor's association entry has the two things such an association needs - `udp_enable` in
its options and the two ports - and the vendor's own host sets them for a state with
encapsulation (`sp2fp_sa.c`: `usfp_opt->udp_enable = host_sa->control.en_nat_t`, the ports beside
it). The request this driver builds had carried the fields all along, set to nothing.

## Build one: the association taken, the inbound half working, the outbound half refused

The driver takes the association, puts the ports in the request and sets bit 22 of the options,
the termination accepts a decrypted frame with the UDP header still on it or already removed, and
the envelope is left as it was. Both ends of the lab tunnel set to encapsulate, the tunnel
re-established, the association mirrored - `esp-udp` in the kernel, `TUNNEL-in-UDP` at the peer,
the table's two rows as for any other:

| | |
|---|---|
| the peer's ESP, inside UDP 4500, decrypted and terminated here | `rx_done` 53 to 159 over the run, `rx_bad` 0 |
| the appliance's own frames handed for encryption | `tx_encrypt` 43 |
| what left the port | nothing: the peer's capture shows only its own packets, its ESP input count did not move |
| the coprocessor, over five pings | `FROM_KN_TO_IPSEC_ENCR` +5, **`CRYPTO_DROP_PROTO_ERR` +5** |

Every TCP stream timed out, every ping was lost. With `udp_enable` set the coprocessor expects the
UDP header **in the frame it is handed** - the vendor's host hands it that way, its kernel having
inserted the header before the offload - and a plain ESP outer header is a protocol error to it.
"Add/Rem UDP header", the option's own comment, is the receive side's removal; on the transmit
side the header is the host's to write.

## Build two: the envelope carries the header

Outer IP header with protocol 17, the eight bytes of the UDP header - the association's source and
destination port, the length of everything after the IP header, no checksum - then the ESP header,
the IV's eight bytes, the inner packet, the trailer and the ICV's sixteen bytes, as before. The
frame's size and the port MTU the envelope is given both know the eight bytes. The same tunnel:

| | |
|---|---|
| four streams up, 16 s | 929.2 Mbit/s |
| four streams down, 16 s | 957.9 |
| one stream up, 12 s | 798.5 |
| one stream down, 12 s | 826.4 |
| a 1,472-byte ping with DF | answered "needs to be fragmented", as it must be |
| a 1,410-byte ping, eight less than the plain tunnel's largest | three of three replies |
| the driver over the run | `rx_done` 4,538,392, `tx_encrypt` 2,526,537, `rx_bad` 0, `rx_nosa` 0, `rx_nokey` 0, `out_drop` 0, `tx_nosa` 0 |
| the peer | ESP in 54,064,918 to 56,591,455; replay drops unchanged |
| the wire at the peer | the appliance's ESP inside UDP 4500, sequence numbers from 0x100001: the coprocessor's |
| the coprocessor over five pings and a 4 s upload | `FROM_WIRE_TO_IPSEC_DECR` +285,858, `FROM_KN_TO_IPSEC_ENCR` +285,787, no `CRYPTO_DROP_*` |

The three measurements the issue asked for: installed and read back; every inbound frame
terminated with no bad authentication; every outbound frame accepted by the peer. And the rates
are the plain tunnel's.

## What is not established

- Only the lab's encapsulation: both ends on port 4500, no NAT between them. A real translator
  that rewrites the port, and the keep-alives strongSwan sends through it, were not in the path.
- Whether the coprocessor hands the decrypted frame up with the UDP header still on it or removed:
  the termination takes both and was not made to say which it saw.
- The rest of [issue 294](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/294): ~~IPv6~~ (the
  outer header since the same day: [the outer header can be IPv6](the-outer-header-can-be-ipv6.md);
  inside too: [IPv6 inside the tunnel](ipv6-inside-the-tunnel.md)), ~~ESN~~
  (ESN was run the same day: [under ESN, what arrives stays with the
  kernel](esn-inbound-stays-with-the-kernel.md)), ~~the other GCM lengths, transport mode~~ (settled the
  same day: [GCM-12 and transport are not ours](gcm-12-and-transport-are-not-ours.md)), ~~the window
  sizes~~ (the windows were run the same day: [a window wider than 256](a-window-wider-than-256.md));
  AES-CBC with an HMAC was taken the same day ([a block cipher at two thirds](a-block-cipher-at-two-thirds.md)).

## Lessons

- **The option's comment named the receive side.** "Add/Rem UDP header" read as "the coprocessor
  does both", and the first build was written on that reading. The counter that refused it,
  `PROTO_ERR`, said which side was wrong in five packets; the vendor's host, read afterwards,
  had said so all along in where it does not insert the header - nowhere, because its kernel
  already has.
- **An inbound half that works hides an outbound half that does not** for as long as the far end
  keeps sending: `rx_done` climbed and the tunnel was dead.
