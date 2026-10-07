# The kernel drives the coprocessor

Measured on an XGS 3300, 2026-10-07, between 15:50 and 16:32. **strongSwan installs a security
association, the kernel offers it to the front ports, the driver puts it on the coprocessor, and from
then on the coprocessor runs every cipher operation of the tunnel** - inbound and outbound - with no
command typed anywhere. A web page fetched through the tunnel from the LAN came back `200`, the
peer's capture held the coprocessor's ESP frames with the real SPI, and the peer decrypted 330 of the
331 it was sent. That is issue 185's product, in its owner's terms: driven by OPNsense, the sysctls
only instruments.

It took four modules in one afternoon, and the measurement at the end is honest in the other
direction too: with every packet still crossing the host, the offload buys **no throughput** on this
box and costs host CPU. The gain is in the next page, when the flow path carries the association.

## The premise that was wrong

The design for this had assumed a userland mirror of the kernel's SADB, because the kernel was
believed to lack `IPSEC_OFFLOAD`. It does not:

```
sysctl -n kern.conftxt | grep IPSEC     options IPSEC_OFFLOAD
                                        options IPSEC_SUPPORT
nm /boot/kernel/ipsec.ko | grep -c ipsec_accel          50
nm /boot/kernel/kernel | grep if_setipsec_accel_methods  T
sysctl net.inet.ipsec.offload.verbose                   0
```

The OPNsense 26.7 kernel is built with the option, `ipsec.ko` carries `ipsec_offload.c`, and the
kernel exports the one call a driver needs. So the contract is `struct if_ipsec_accel_methods`
(`sys/net/if_var.h`): six methods on the interface, and the kernel does the rest.

## What the driver answers

`contrib/octep/octep_ipsec.c`, registered on every `oxp` interface with `IFCAP2_IPSEC_OFFLOAD`,
behind `dev.octep.0.ipsec.on` (default 0: every offer declined, IPsec stays with the host).

| method | what the driver does |
|---|---|
| `if_sa_newkey` | takes a tunnel-mode ESP association over IPv4 with AES-GCM-16 and a 16, 24 or 32-byte key, no ESN, for the interface that owns its local end - the **destination** for the inbound association, the **source** for the outbound one - allocates a coprocessor index, posts `SA_ADD` from a record and clears the key. Everything else, and every other interface, gets `EOPNOTSUPP` |
| `if_sa_deinstall` | `SA_DEL` free 0, then free 1; the index rests two seconds before reuse, the revision climbs on every install |
| `if_sa_cnt` | `SA_GET_STATS`: the engine's bytes and packets, which the kernel adds to the association's lifetime - so `swanctl --list-sas` stays honest for traffic the host never saw |
| `if_hwassist` | 0: the stack completes its checksums before the frame reaches the driver |
| `if_spdadd`, `if_spddel` | nothing yet; the policy is consulted when a packet or a flow is handled |

The kernel offered each of the lab tunnel's two associations to all twelve interfaces; one took each:

```
octep0: ipsec: inbound association spi 0xc8d6cd31 on oxp3: coprocessor index 1, handle 2, rev 2, drv_spi 6
octep0: ipsec: outbound association spi 0xc1258677 on oxp3: coprocessor index 2, handle 3, rev 2, drv_spi 18
```

`dev.octep.0.ipsec.table` prints them; `sa_refused` climbs by 22 per tunnel, which is the other eleven
interfaces declining. An association installed while the gate was off is not offered again until it
is re-established.

## Inbound: the frame arrives decrypted, and the kernel would drop it

[The handle is the index plus one](the-handle-is-the-index-plus-one.md) measured what the
coprocessor hands the host for a frame it decrypted: the ESP frame itself, payload and trailer in the
clear, ICV attached, outer header re-templated. `esp_input` has no entry for that shape - it ran the
cipher over the plaintext and dropped every frame as a bad authentication - so the receive path
terminates it (`octep_ipsec_rx`): the handle names the association, the SPI is checked against it,
`key_allocsa` finds the kernel's association, the front is stripped to the inner packet and the back
by the trailer's pad length, and the inner packet goes where `ipsec4_common_input_cb` would have sent
it: tagged `PACKET_TAG_IPSEC_IN_DONE` (which also makes `ip_input` skip the WAN's filter rules for
it, as it does for the software path), marked `M_DECRYPTED` (which the inbound policy check
requires), counted on the kernel's association, shown to enc0's capture and inbound rules with the
packet looking as if it arrived on enc0 - which is where OPNsense's IPsec rules live - and queued to
IP.

The peer pinged the LAN through the tunnel for twenty seconds:

| | |
|---|---|
| `ipsec.rx_done` | 3 to 169; `rx_bad`, `rx_nosa`, `rx_nokey`, `rx_blocked` all 0 |
| ESP reaching the host on the front port | 0 (it had been every frame) |
| `netstat -sp esp`, bad authentication | 0 to 0 |
| the kernel association's bytes | 252 to 14,196; `swanctl` 169 packets |
| `FROM_WIRE_TO_IPSEC_DECR` = `RX_IPSEC` | +165: every frame |
| inner ICMP leaving the LAN port | 31 in four seconds |
| pf | an `icmp` state for the inner tuple, made on enc0 |

## The first decrypted frame took the appliance down

The first module panicked on that frame: `key_allocsa` from `octep_ipsec_rx` from the ring's
interrupt thread, page fault (issue 292, textdump kept). The kernel is built with `VIMAGE`, every `V_`
variable resolves through the current thread's vnet, and an interrupt thread has none. The flow
trigger had met the same wall on its taskqueue and wrapped its work in `CURVNET_SET`; the new path
had not. The gate defaults to 0 at boot, so the panic could not repeat before the fix was in.

## Outbound: the kernel's path never sees a forwarded packet

With both associations mirrored, the first outbound test moved `tx_encrypt` by nothing and the host
made every ESP frame itself. `ipsec_accel_output` is reached from `ip_output` with the output
interface in hand - which is how a packet the appliance *generates* is offered to the driver. A
*forwarded* packet never gets there: `ip_forward` calls `ipsec4_forward`, which hands it to the
IPsec output path with `ifp NULL` (the source says `XXXKIB`), and with no interface there is no
handle to find. On a firewall nearly everything is forwarded, and two encryptors on one association
are not an option: the sequence numbers collide and the peer's replay window drops one of them.

So forwarded packets are taken before `ip_forward` reaches them: a pfil hook, linked **after pf** on
the inet inbound chain (`pfilctl heads` shows `pf:default-in` then `octep:ipsec-forward`), sees a
packet pf has passed and translated and, when it is to be forwarded and the kernel's outbound policy
covers it with an association this driver mirrors, does `ip_forward`'s share - the TTL, enc0's
capture and outbound rules, the association's byte count - frames it for the tunnel's next hop, tags
it exactly as `ipsec_accel_output` would, and sends it out of the front port, where the transmit path
turns the tag into the metadata the coprocessor encrypts by: `sa_is_out`, and the handle. Packets
for this host, fragments, expiring TTLs, bundles, oversize packets and policies whose association is
not mirrored pass to the kernel untouched - and the kernel then encrypts those itself, which the
coprocessor therefore never does.

### And the coprocessor wants the envelope

The second outbound test reached the coprocessor - `FROM_KN_TO_IPSEC_ENCR +294` - and lost every
frame to `CRYPTO_DROP_PROTO_ERR`. The host path encrypts **in place**, the way a Linux crypto offload
hands a packet over: outer header, ESP header, the IV's room, the plaintext, the trailer and the
ICV's room already there. The vendor's `state_remote_overflow` says as much of the sequence number
on that path: *"will be overridden"*. It is the mirror image of what arrives on the way in, and a
bare inner packet is refused. The hook now appends the RFC 4303 trailer and sixteen bytes for the
ICV, prepends the outer header, the ESP header with the SPI and a zero sequence, and eight bytes for
the IV, and counts the tunnel's overhead exactly against the next hop's MTU. The coprocessor writes
the sequence, the IV and the ICV and overwrites the outer header with the association's template.

The third outbound test, the LAN sending UDP at the peer's closed port for twenty seconds:

| | |
|---|---|
| `ipsec.fwd_diverted` = `ipsec.tx_encrypt` | 295; `fwd_host` 1 (the first packet, before the neighbour was known) |
| `FROM_KN_TO_IPSEC_ENCR` | +292, no `CRYPTO_DROP_*` |
| the peer's capture | **331 ESP frames from this appliance, SPI `0xc123d836`, sequence 1 to 0x14a** |
| the peer's `swanctl` | inbound association **330 packets** - decrypted and accepted |
| then `curl -k https://<peer>/` from the LAN | **`200`, 18,761 bytes**, 0.13 s |

## The number, and what it says

A 600 MB file served by the peer, fetched from the LAN through the tunnel, the same way the software
baseline was measured, with the gate on and then off (the tunnel re-established in between, so the
gate-off run's associations stayed with the host):

| | throughput | host idle | load |
|---|---|---|---|
| gate on: coprocessor cipher, host forwarding | 358 Mbit/s | 85 % | 0.59 |
| gate off: software ESP | 355 Mbit/s | 95 % | 0.13 |

With the gate on: `rx_done` 480,162 - every inbound frame decrypted by the coprocessor and terminated
here; `tx_encrypt` 72,236 - every acknowledgement encrypted there; bad authentications 0.
`CRYPTO_DROP_REPLAY_OOW` 631: frames outside the 32-packet window, and the software run saw 616
*possible replay packets* on the same stream, so that is reordering on the way here, not the mirror.

**Equal throughput, more host CPU.** This appliance's host has AES-NI, and at 355 Mbit/s the software
cipher leaves it 95 % idle; moving the cipher off it saves nothing visible, while every packet still
crosses the host and now pays the termination (association lookup, tag, enc0 filter, netisr) or the
hook (policy lookup, association lookup, next-hop resolve, envelope). The ceiling is the host's
forwarding, as it was for plain traffic before the flow path - and the flow path is where plain
traffic went to 99.99 % in hardware. For tunnel traffic that means the LAN-to-peer microflow
carrying the association's handle and a next hop at the tunnel's far end, and the decrypted
direction's candidate taken from the inner packet; issue 293.

## Where it stands

Done, measured: the contract, both associations, the inbound termination, the outbound hook and
envelope, the end-to-end fetch, the counters the kernel sees. Honest limits: IPv4 only; AES-GCM-16
only; no ESN; NAT-T carried in the record but unmeasured (issue 294); policies with one transform;
the fast path does not yet carry the association (issue 293), so `dp.auto` refuses policy-covered
connections rather than forward them in the clear (issue 290, closed by this). The kernel's own
replay window for a mirrored inbound association is not advanced - the coprocessor's is the one that
checks.

| knob or counter | meaning |
|---|---|
| `ipsec.on` | the gate; re-establish the tunnel after turning it on |
| `ipsec.table` | every mirrored association: index, handle, direction, SPI, outer addresses, interface, the engine's last counts |
| `sa_installed`, `sa_refused`, `sa_failed`, `sa_full`, `sa_removed` | the offers |
| `rx_done`, `rx_nosa`, `rx_nokey`, `rx_bad`, `rx_v6`, `rx_noenc`, `rx_blocked`, `rx_queuefail` | decrypted frames, and why one did not reach IP |
| `fwd_diverted`, `fwd_host`, `fwd_blocked`, `fwd_nomem` | the forward hook's decisions |
| `tx_encrypt`, `tx_nosa`, `tx_bypass` | the transmit path's reading of the kernel's tag |
| `flow_policy` | connections the trigger refused because a policy covers them |

## Lessons

- **Check the premise against the running kernel before designing around its absence.** One
  `sysctl -n kern.conftxt` would have saved a design.
- **A kernel offload written for hosts is not a router's.** `ipsec4_forward` passes no interface;
  the fix had to sit after pf, where the forwarded packet is still whole.
- **The two halves of an in-place cipher are mirror images.** What arrives dressed must be sent
  dressed; the first `CRYPTO_DROP_PROTO_ERR` said so in one word.
- **Measure the gain, not the mechanism.** Every counter said the offload worked, and the number
  that matters did not move. The next slice is chosen by that number.
