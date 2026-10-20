<!-- reviewed: 2026-10-20 -->
# AES-CBC with an HMAC on the ARMADA: the four hashes taken, both ways in hardware

2026-10-14, the XGS 136, step 5 slice 8 of [the offload plan](../npuep/offload-plan.md). The OCTEON
TX [took AES-CBC with an HMAC on 2026-10-12](a-block-cipher-at-two-thirds.md) and measured it at two
thirds of GCM's rate; this family's turn, as counts - the rate on a quiet bench is still to come.
Module build `186728`-byte `npuep.ko` from the working tree, the direct-link bench with NAT-T still
on from the previous slice.

## What the driver does now

An association's algorithms are read into one set, `npuipsec_alg_of`: AES-GCM-16 as before, or
AES-CBC with an HMAC the engine knows - cipher mode 1, the hash type and the overhead type as the
vendor's own host sets them for the same state, the ICV the kernel's own truncation (half the hash
for SHA-2, RFC 4868):

| | control word | the envelope |
| --- | --- | --- |
| AES-CBC + HMAC-SHA1 | cimode 1, hash 2 (SHA1_96), overhead 3 | IV 16, ICV 12 |
| AES-CBC + HMAC-SHA2-256 | cimode 1, hash 8 (SHA256_128), overhead 4 | IV 16, ICV 16 |
| AES-CBC + HMAC-SHA2-384 | cimode 1, hash 9 (SHA384_192), overhead 5 | IV 16, ICV 24 |
| AES-CBC + HMAC-SHA2-512 | cimode 1, hash 10 (SHA512_256), overhead 6 | IV 16, ICV 32 |

The HMAC key goes in the 64-byte auth-key field of the request, where GCM puts its four-byte salt.
The association's record carries its IV and ICV lengths, its trailer alignment and whether the IV is
the host's to write; the termination checks its floors at the smallest IV and ICV any association
has and the exact lengths once the association is found, and the transmit side - rewritten around one
parameter block for both doors - sizes the envelope from them, pads the trailer to the cipher's block
(16 for CBC, 4 for GCM) and writes a random IV into a CBC frame's IV room as the kernel's own
`esp_output` does, where for GCM it leaves zeros for the engine's counter. The fragmentation threshold
follows the same lengths. The gate that decides what the driver takes is the same set: an association
outside it is declined to the kernel as before.

## Measured

Each variant: the proposal changed on both ends, the child re-initiated, both associations
`SA_ADD accepted by the engine` (`alg 12`), five pings and three of 1472 bytes from the appliance,
ten from the peer; `FROM_KN_TO_IPSEC_ENCR` one per frame sent, `FROM_WIRE_TO_IPSEC_DECR` one per
frame received, `nosa 0 bad 0`, `netstat -s -p esp` at zero packets out throughout.

| variant | the child | appliance 5 / 1472×3 | peer 10 | frames out / in | the small frame on the wire |
| --- | --- | --- | --- | --- | --- |
| aes128-sha256 | `AES_CBC-128/HMAC_SHA2_256_128` | 5/5, 3/3 | 10/10, 3.1 ms | 41 / 33 over the full run (with the PC's 10 and 5×1472 forwarded: 10/10, 5/5) | 136 bytes |
| aes128-sha1 | `AES_CBC-128/HMAC_SHA1_96` | 5/5, 3/3 | 10/10 | 21 / 18 | - |
| aes128-sha384 | `AES_CBC-128/HMAC_SHA2_384_192` | 5/5, 3/3 | 10/10 | 21 / 18 | 144 bytes |
| aes128-sha512 | `AES_CBC-128/HMAC_SHA2_512_256` | 5/5, 3/3 | 10/10, 8.1 ms | 21 / 18 | 152 bytes |

The frame lengths say what the engine was told: GCM's small frame is 120 bytes; CBC's is 16 longer
for its IV and its 16-byte block, and then grows with the ICV - 136, 144, 152 for ICVs of 16, 24 and
32. The SHA-256 run was the full set of the NAT-T page - the appliance, the peer and the PC behind
the appliance, small and fragmented - and its totals are that page's frame for frame; the other three
were the appliance and the peer.

## Not done, and said so

- The rate. The OCTEON TX ran these at two thirds of GCM; nothing here is a rate yet.
- IPv6 outer and inner~~, extended sequence numbers~~: still declined to the kernel. Extended sequence numbers: taken both ways on the engine since 2026-10-20 ([measured](../measurements/esn-136-2026-10-20.md)); until then an outbound ESN association was declined, and an inbound one was handed to the engine without SA_ADD's `ext_seq` bit. ~~The replay window~~ taken later the same day ([a window on the ARMADA](a-window-on-the-armada.md)).
- AES-192 and AES-256 keys with CBC are in the same set (the cipher by key length, as for GCM) and
  were not measured; every run above was AES-128.
