<!-- reviewed: 2026-10-14 -->
# The host answers through the tunnel on the ARMADA: the receive termination

2026-10-14, the XGS 136, step 5 slice 4 of [the offload plan](../npuep/offload-plan.md). The engine
[hands up the decrypted frame](the-engine-hands-up-the-decrypted-frame-on-the-armada.md) with the
association's handle in the metadata; this slice terminates it - a port of the OCTEON TX's
`octep_ipsec_rx` - and the appliance answers pings through an AES-GCM tunnel with the inbound side
decrypted in hardware. Measured both ways on the direct-link bench of the page above. Module build
`8495d091`, from the working tree.

## What the driver does now

`npuipsec_rx` (contrib/npuep/npuipsec.c): for a received frame whose metadata word at +16 is not
zero, the receive path keeps the frame off the stack's input chain and puts it on a chain of its
own, carrying the word in the packet header's scratch space; the tick walks that chain with the GIU
lock dropped, because what follows calls into the key database, the filter and netisr. The
termination checks the handle, revision, direction and SPI against the driver's own table (the
handle is the `SA_ADD` index plus one, as on the OCTEON TX - read back on the first frame:
`rx_last_word 0x00010002`, index 1 revision 1), finds the kernel's association by its destination and
SPI, strips the envelope (link header, outer IPv4, ESP header, the 8-byte IV slot in front; pad, pad
length, next header and the 16-byte ICV behind), checks the inner IPv4 length, and delivers the inner
packet the way the kernel's own `esp_input_cb` does: `PACKET_TAG_IPSEC_IN_DONE` with the transform
history, `M_DECRYPTED`, counted on the association, tapped and filtered on `enc0`, queued to IP. ~~The
replay window is the engine's.~~ (Corrected 2026-10-14: this build sent the engine a window of 0 with the anti-replay bit off, and the termination does not advance the kernel's window, so there was no replay check at all until [the window slice](a-window-on-the-armada.md).) Counters on `dev.npuep.0.ipsec.rx_done` and its siblings
(`rx_nosa`, `rx_nokey`, `rx_bad`, `rx_noenc`, `rx_blocked`, `rx_queuefail`, and `rx_last_word`, the
last marked frame's sa word) and a `receive:` line on `ipsec.sas`.

## Measured

- **Peer to appliance:** twenty pings from the peer through the tunnel, **20 of 20 answered**, 4.8 ms
  average. On the 136: `receive: done 70 nosa 0 nokey 0 bad 0 noenc 0 blocked 0 queuefail 0` (the
  twenty among earlier bursts), `rx_done` +20 for the burst; `RX_IPSEC +20`,
  `FROM_WIRE_TO_IPSEC_DECR +20` on the fast path. The peer's child counted 30 in, 30 out.
- **Appliance to peer:** ten pings from the 136 through the tunnel, **10 of 10**, 7 ms average; the
  ESP output histogram `aes-gcm-16: 10` (software, the encrypt association is still declined), and
  the ten replies came back as ESP, were decrypted by the engine and terminated here: `rx_done` +10.
- The kernel's view: ICMP `echo` received and `echo reply` generated one for one; IP delivered them
  to this host; nothing refused by the inbound policy check, nothing blocked on `enc0`.

So: inbound ESP decrypted in hardware and terminated by the driver, the inner packet answered by the
host, the answer encrypted (in software, for now) and taken by the peer. That is the receive half of
IPsec offload on this family, working.

## What stood in the way, and was not this driver

The first burst with the termination loaded was delivered - `rx_done 20`, `echo reply: 20`
generated - and still the peer got nothing: `netstat -s -p esp` showed every reply counted out and
then `dropped; no transform`, which is the counter the ESP output callback raises on a **crypto
error**, not a missing transform. The 136's host is an AMD Ryzen Embedded, and the kernel had
`ccp.ko` loaded - AMD's platform crypto engine, `ccp0: <AMD CCP-5a>` - beside `aesni0`; IPsec's
default `crypto_support` prefers a hardware provider, and every AES-GCM operation handed to it
failed. With `ccp` unloaded and the tunnel re-established, every reply went out and came back. That
is [its own page](amd-ccp-breaks-ipsec-gcm-on-the-xgs-136-host.md) and an issue; it is not
part of the offload, but it would have hidden this slice's result entirely.

## Not done, and said so

- The transmit side: the encrypt association is declined, so outbound ESP is the kernel's software
  cipher. The envelope and the outbound sequence hand-over are the next slice, as on the OCTEON TX.
- IPv4 outer and inner only, AES-GCM-16 only. NAT-T's UDP shape is taken by the code and not yet
  measured; CBC with HMAC, IPv6 and the replay window beyond the engine's own are later slices.
- The open-entry regime: a port whose interface entry is opened takes the decrypted frame to the
  flow table (the earlier page); that is to be measured on this bench with the entry opened, and is
  where the flow-integrated tunnel path (a microflow carrying `sa_index`) will begin.
- `ipsec.flows 2` is never set here.
