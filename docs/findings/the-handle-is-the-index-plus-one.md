# The handle is the index plus one

Measured on an XGS 3300, 2026-10-07, with the encrypted frames read off the wire at the peer.
**A microflow names a security association by `SA_ADD`'s index plus one.** The index in `SA_ADD` is
0-based; the handle the fast path reads out of the microflow's `sa_index` field is 1-based, and
`sadb_hw_entry_get` subtracts one before it indexes the table. Program `sa_index = saidx + 1`, or the
flow uses the association installed one slot *below* the one meant - which is what every trial on
[the association belongs to an interface](the-crypto-engine-is-fed.md) did, and why that page's
headline is withdrawn below.

The same measurement is the first time a frame the coprocessor encrypted was seen **off the
appliance**: 731 ESP frames at the peer's NIC, sequence 1 upward, outer header as the association
said. The afternoon's second half mirrored the real inbound association and read what the fast
path hands the host after decrypting: the ESP frame itself, payload in the clear, ICV attached -
which the kernel then drops.

## The experiment

Two associations at adjacent, fresh indices, identical except for the SPI, so the SPI on the wire
says which slot a frame used:

```
SA_ADD  idx 8  spi 0x0a0a0a08  rev 1  dir 0 (encrypt)  lif 0x1000  tunnel  ESP  AES-128 CTR + GF128   rc 0
SA_ADD  idx 9  spi 0x0a0a0a09  rev 1  the same                                                        rc 0
```

One live UDP flow from the LAN (198.51.100.120:54817 to 203.0.113.36:5555, two hundred frames a
second) was accelerated in the original direction only (`dp.accel_half=1`, because its replies never
arrive as wire frames), then its own microflow was re-programmed with `MFLOW_PROGRAM` three times,
four seconds apart, everything else in the operation block unchanged: `sa_index` 9, then 8, then 0.
`tcpdump` ran on the peer's WAN interface throughout, filtering on our address.

| `sa_index` | `FROM_WIRE_TO_IPSEC_ENCR` | `FROM_WIRE_TO_KN_STALE_SA` | what reached the peer |
|---|---|---|---|
| **9** | **+695** | 0 | **731 ESP frames, SPI `0x0a0a0a08` - index 8's** - seq 1 upward |
| **8** | +36 (the tail of the first window) | **+724** | nothing |
| 0 | | | the UDP itself, in the clear - see below |

Read back with `LO_MFLOW_READ` after each write, the operation word was `0x2e010009`, `0x2e010008`,
`0x2e010000`: `sa_index` stored as written, action 1, state 2 ACTIVE, the entry never left.

Both rows say the same thing. Handle 9 used slot 8. Handle 8 used slot 7, which still held an
association from the 2026-10-06 trials with another revision: `STALE_SA` is the revision-mismatch
exit, the one that clears the handle and punts the frame. Under a 0-based reading the second row
would have encrypted with SPI `0x0a0a0a08` as well, and it sent nothing.

## The frame, off the appliance

```
203.0.113.46 > 203.0.113.36: ESP(spi=0x0a0a0a08,seq=0x1), length 128
  IP (tos 0x0, ttl 63, id 0, offset 0, flags [none], proto ESP (50), length 148)
  Ethernet: from oxp3's own address to the next hop's
```

Beside it, the host's own ESP for the same inner packet on the real association: length 148, TTL 63,
`id 52405`, DF clear. So the coprocessor's outer header is what the vendor's source builds - TTL 63,
identification 0, DF clear, TOS 0, addresses from the association, sequence from `sequence + 1` -
and the destination MAC is the **microflow's next hop's**, so the L2 rewrite is applied to the
encrypted frame, not lost under it. The chain that [the crypto path completes](the-crypto-path-completes.md)
counted end to end with gdb transmits, and at the sender's full rate: 181 frames a second of ESP
at the peer, which is what the sender produced.

## The 2026-10-06 table, re-read

| installed at, with `lif_index` | the flow named | so it used slot | and the far side did |
|---|---|---|---|
| 5, `lif 0` | 5 | 4 - empty | nothing: every frame punted |
| 6, `lif 10` | 6 | 5 - **the `lif 0` association** | `FROM_WIRE_TO_IPSEC_ENCR +7` |
| 7, `lif 0xa000` | 7 | 6 - the `lif 10` association | `FROM_WIRE_TO_IPSEC_ENCR +6` |

The association with a zero `lif_index` encrypted. **The conclusion that a zero `lif_index` is
refused on an encrypt association is withdrawn**; what the first row measured was an empty slot.
This is also what the source says: `ipsec_fpop_sa_add` stores `lif_index` and, for an encrypt
association, nothing reads it again - the one interface-keyed structure, the SPI hash, is written
for decrypt associations only, where a VLAN in the field is refused and the interface part becomes
the hash key's port. The measurement and the source now agree, which they did not before.

## A flow the kernel's policy covers is forwarded in the clear

The UDP flow was chosen because the lab tunnel's `out ipsec` policy covers it - the host had been
encrypting it on the real association, 181 ESP frames a second at the peer. The moment the flow
was in hardware with `sa_index 0`, the host's ESP for it stopped (the capture shows a whole second
with none) and nothing arrived at the peer *as* ESP: the fast path forwarded the UDP as it came,
to the peer's MAC. `FROM_WIRE_TO_WIRE` counted it, `punts 0`.

So the offload trigger and the kernel's security policy database are not yet acquainted, and with
a tunnel configured `dp.auto` would bypass it for every flow the policy covers. Until the
association mirror lands, a flow a policy covers must be **either** programmed with its
association **or** left to the host - never forwarded plain. Issue 290 tracks it.

## The decrypt side, the same afternoon

The real inbound association of the lab tunnel - its SPI and its 20-byte key read from `setkey -D`,
the first 16 bytes as the cipher key and the last 4, the salt, as `auth_key` - was mirrored by hand
at coprocessor index 10: `dir 1`, `lif_index 0x1000` (the WAN port's interface, no VLAN), anti-replay
on with a window of 32, while the peer sent ten ESP frames a second on it.

| | |
|---|---|
| `FROM_WIRE_TO_IPSEC_DECR`, `RX_IPSEC` | +107 in ten seconds, +58 in six: **every frame on that SPI**, caught by the SPI hash, decrypted, returned from the engine |
| the punted frame's `kn_md` | **`sa_index 11, sa_rev 1`** - index 10 plus one. The handle is 1-based on this side too |
| far side charge | `FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE`, not `FROM_IPSEC_DECR_TO_KERNEL`: the inner packet was re-classified, had no microflow, and was punted |
| the kernel | `netstat -sp esp`: *packets dropped; bad authentication detected* **+49 in five seconds** - every one of them; *input packets processed* +0; the kernel SA's byte count did not move |

**What is handed to the host is not the inner packet.** Read with `tcpdump -XX` on the front port,
frame for frame against the same flow a moment earlier:

```
  0x00  Ethernet, 14 bytes               unchanged
  0x0e  outer IPv4, 20 bytes             re-templated: id 0 (was 0x7c77), checksum recomputed, TTL as it came
  0x22  ESP: SPI, sequence               intact
  0x2a  the 8-byte IV slot               overwritten with the last 8 bytes of the frame's own L2 header
  0x32  the inner packet                 PLAINTEXT - 45 00 00 54 ... the ICMP echo, readable
        ESP trailer                      01 02 | 02 | 04 - pad, pad length, next header IPv4, in the clear
        ICV, 16 bytes                    still attached, already verified
```

Same length as the frame that arrived. So the fast path decrypts **in place**, verifies the ICV, and
hands over the frame still in its ESP dress - the shape the vendor's Linux host consumes with
`CRYPTO_DONE`, where `esp_input` keeps the header and trailer processing and skips the cipher. The
FreeBSD kernel has no such entry: `esp_input` runs the cipher over plaintext, the authentication
fails, the frame is dropped, and a mirrored inbound association **blackholed the tunnel's inbound
half** until the driver learned to do what the vendor's `esp_input` does - strip the outer header,
the ESP header and the IV slot from the front, the trailer and the ICV from the back by the
trailer's pad length, and deliver the inner packet marked as decrypted. It does, since
[the kernel drives the coprocessor](the-kernel-drives-the-coprocessor.md). The 8 bytes of L2 residue in the IV slot
are the trace of the re-injected inner packet having been framed as Ethernet before the punt
re-dressed it; nothing reads them.

## What the mirror does with this

- Install at `saidx N`, program the microflow with `sa_index N + 1`, and read `kn_md.sa_index` on a
  punted decrypted frame as `N + 1`: measured on both sides.
- A punted frame with `kn_md.sa_index != 0` is decrypted-in-place ESP, ICV attached. The receive
  path terminates it itself since the same evening - see
  [the kernel drives the coprocessor](the-kernel-drives-the-coprocessor.md).
- The two counters are the instruments: `FROM_WIRE_TO_IPSEC_ENCR` is the handle naming a valid
  association with the right revision; `FROM_WIRE_TO_KN_STALE_SA` is a valid association with the
  wrong revision (or a cleared one); neither moving, with the flow active, is an empty slot.
- Indices 1 to 7 on the coprocessor were deleted the same day, both stages, rc 0 each, so the
  table holds nothing from the trials. Allocate from 1 upward, bump the per-index revision before
  every install, and never name a slot by its index.

## Lesson

An off-by-one between two numbering spaces survived three days because the wrong slot was often a
valid association too: the trials were run in sequence at consecutive indices, so each one named
the previous one's work. The disassembly had `sub w0, w0, #1` before the table index at three sites
and the source had `spiht_add(..., req->saidx + 1)`, and both were read before the measurement that
settled it was designed. The measurement that settles a numbering question is one whose two
candidate answers are *different observables* - two associations differing only in the SPI, and the
SPI read off the wire.
