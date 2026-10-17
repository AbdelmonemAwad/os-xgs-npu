<!-- reviewed: 2026-10-12 -->
# AES-CBC with an HMAC runs at two thirds of GCM's rate

Measured on an XGS 3300, 2026-10-12, module `b282ce62`. The block-cipher item of
[issue 294](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/294): an association with AES-CBC
and an HMAC - SHA-1, SHA-256 or SHA-512, the three the lab ran - offered to the coprocessor instead
of refused, and the three measurements on each. All three are taken and correct; all three run at
about two thirds of what the same tunnel does with AES-GCM-16.

## What changed in the driver

Until this day the acceptance in `octep_ipsec_sa_newkey` wanted `SADB_X_EALG_AESGCM16` and
nothing else. The request the driver builds had carried the fields a block cipher needs all along:
the control word's cipher mode and hash type, a 64-byte auth-key field whose first word is the
AEAD salt for GCM, an overhead-type byte in the options. The vendor's host fills them for an xfrm
state with `ENCR_AES_CBC` and an `AUTH_HMAC_*` (`sp2fp_sa.c`, `generate_usfp_control`: cimode
CBC, the hash type by the HMAC and its truncation, the overhead type `BLOCK_96` to `BLOCK_256`),
and so does the driver now, from `sav->alg_enc`, `sav->alg_auth` and `sav->key_auth`:

| the kernel's association | the request | on the wire |
|---|---|---|
| AES-CBC + HMAC-SHA1 | cimode 1, hash 2 (SHA1_96), overhead 3 | IV 16, ICV 12 |
| AES-CBC + HMAC-SHA2-256 | cimode 1, hash 8 (SHA256_128), overhead 4 | IV 16, ICV 16 |
| AES-CBC + HMAC-SHA2-384 | cimode 1, hash 9 (SHA384_192), overhead 5 | IV 16, ICV 24 |
| AES-CBC + HMAC-SHA2-512 | cimode 1, hash 10 (SHA512_256), overhead 6 | IV 16, ICV 32 |

The ICV lengths are the kernel's own truncations (`xform_ah_authsize`: half the hash for SHA-2,
RFC 4868; 96 bits for SHA-1), which are what the vendor's type names say. The association record,
the transmit descriptor, the envelope and the termination carry the IV and ICV lengths per
association instead of the GCM constants; the trailer is padded to the 16-byte block for CBC
(RFC 4303 2.4) and to four bytes for GCM as before; the envelope writes a random IV into a CBC
frame's IV room (`arc4rand`, as the kernel's `esp_output` does) where for GCM it leaves zeros for
the coprocessor's counter. SHA-384 was not run; it is the same code with 24 and 9.

## The three proposals

The lab tunnel over the 10G path, the proposal hand-set at both ends (`esp_proposals =` in the
generated `swanctl.conf`), `dp.auto` 1, `ipsec.flows` 0; each row four streams up for 16 s, four
down for 16 s, one each way for 12 s, five pings, the coprocessor's counters read across it.

| proposal | installed and read back | inbound, the peer's frames | outbound, the appliance's frames | up x4 / down x4 / up x1 / down x1, Mbit/s |
|---|---|---|---|---|
| `aes128-sha256` | both rows mirrored; the kernel shows `aes-cbc` and `hmac-sha2-256` | `rx_done` +3,140,345, `rx_bad` 0, `rx_nosa` 0, `rx_nokey` 0; `FROM_WIRE_TO_IPSEC_DECR` +3,140,965, `CRYPTO_DROP_REPLAY_OOW` +45, no other drop | `tx_encrypt` +1,965,655 = `FROM_KN_TO_IPSEC_ENCR`; the peer's ESP input +1,965,655, its replay and authentication drops unchanged | **669.9 / 609.4 / 602.4 / 561.1** |
| `aes256-sha1` | both rows; `aes-cbc`, `hmac-sha1` | +3,173,430, 0, 0, 0; +3,174,098, `REPLAY_OOW` +44 | +1,997,046 = `ENCR`; the peer's input **+1,995,655**, its authentication drops 0 | **688.4 / 612.1 / 608.9 / 556.9** |
| `aes256-sha512` | both rows; `aes-cbc`, `hmac-sha2-512` | +3,286,180, 0, 0, 0; +3,287,001, `REPLAY_OOW` +40 | +2,097,933 = `ENCR`; the peer's input +2,097,933 | **714.2 / 632.5 / 633.0 / 556.9** |

Every ping answered. The same tunnel with `aes128gcm16` the same hour: 947 to 955 up, 957 to 961
down, 781 to 795 one stream ([a window wider than 256](a-window-wider-than-256.md)). So a block
cipher with an HMAC costs about a third of the rate in both directions, whichever hash, and the
hash's size does not show: the coprocessor's crypto engine does CBC and the HMAC as two passes
where GCM is one, which is the usual shape, and nothing on the host changed between the runs.

The SHA-1 row's peer input is 1,391 short of what the coprocessor sent. The peer's authentication
and replay counters did not move; its "packets dropped; queue full" counter - the software
cipher's queue, since the peer runs no offload - stood at 3,131 cumulative at the end of the day
and was not read before the run. ~~That is the likely place; it is not established.~~ Read before
and after on the IPv6 lab the same day: with SHA-256 that counter rose by 1,150 while the peer's
input matched the coprocessor's count exactly, and with SHA-1 it rose by 658 while the input was
1,072 short again - so the queue is not the place, the shortfall is SHA-1's alone, and where the
frames go is not established.

## The largest packet

A 1,472-byte DF ping is answered "needs to be fragmented" on all three. With a 16-byte ICV a
1,402-byte ping passes (the inner packet is 1,430, the largest that fits is 1,438: 1,500 less the
outer header, ESP header, IV and ICV, rounded down to the block, less the two trailer bytes); with
the 32-byte ICV the largest is 1,422 and the 1,402-byte ping is refused, as it should be.

## What is not established

- ~~Whether the coprocessor writes its own IV over the random one the envelope gives a CBC frame.
  The peer decrypts everything either way; the capture that would say was lost to a shell quirk
  and is the next cycle's.~~ Captured the same day over the IPv6 lab: five frames, five different
  random-looking IVs ([the outer header can be IPv6](the-outer-header-can-be-ipv6.md)). Whose
  bytes they are is not knowable from the wire; that the IV is not fixed is.
- SHA-384. Same code, not run.
- The SHA-1 shortfall above.
- Why two thirds and not some other fraction: the engine's own accounting is not readable.

## Lessons

- **The request was ready; the acceptance was not.** Every field a block cipher needs was in the
  SA_ADD layout from the first day, read out of the module's DWARF; only the driver's one-line
  check kept the coprocessor to GCM. Reading the vendor's host mapping took an hour; the lab, two.
- **Editing a running script moves its offsets.** A fix to the lab tool while one of its steps
  was still running gave the running shell a syntax error at the next line it read. The step had
  finished its work; the lesson is to leave a running file alone.

## Where it sits

The acceptance and the per-association lengths are in [octep_ipsec.c](../../contrib/octep/octep_ipsec.c)
(`octep_ipsec_sa_newkey`, `octep_ipsec_wire_len`, `octep_ipsec_envelope`, `octep_ipsec_rx`); the
request's cipher mode, hash type, overhead type and auth key in
[octep_rpc.c](../../contrib/octep/octep_rpc.c) (`octep_rpc_sa_install`); the record and the
descriptor in [octep.h](../../contrib/octep/octep.h). The numbers are in
[the measurements](../measurements/xgs3300.md#aes-cbc-with-an-hmac-on-the-coprocessor).
