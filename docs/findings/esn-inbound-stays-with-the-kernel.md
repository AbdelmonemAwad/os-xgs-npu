# Under ESN, what arrives stays with the kernel

Measured on an XGS 3300, 2026-10-12, modules `c92ca5dd` and `a4c1091e`. The second item of
[issue 294](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/294) run: extended sequence
numbers (RFC 4303 2.2.1, strongSwan's `esn`, the kernel's `SADB_X_SAFLAGS_ESN`) on an association
offered to the coprocessor. The outcome is one-sided: the association that leaves is taken and
runs at the plain tunnel's rate; the one that arrives is left to the kernel's cipher, because the
coprocessor was measured refusing its frames after a burst.

## Getting ESN negotiated at all

OPNsense 26.7.5's IPsec model has no ESN field, and strongSwan 6.1.0 takes ESN through the ESP
proposal rather than a child option: `esn = yes` in the child block is silently ignored (tried
first; the pair came up without ESN). The generated `swanctl.conf`'s child gets
`esp_proposals = aes128gcm16-esn` at both ends, loaded with `swanctl --load-all`; the pair then
shows `ESP:AES_GCM_16-128/ESN` and `flags=0x400` in the kernel. The lab tool on the bench
(`esn-294.sh`, not in this repository) does the edit at both ends; its `down` regenerates both
files from their templates.

## Build one: both directions taken, and the inbound half dies after a burst

The driver sets bit 18 of the association request's control word (`ext_seq`, as the vendor's host
does for a state with ESN) and changes nothing else: the kernel's own sequence counters are 64 bits
already. Both associations mirrored, `rx_done` and `tx_encrypt` climbing, replay 0. Then the
sequence that kills it, four times:

| run | sequence | outcome |
|---|---|---|
| 1 | four streams up 16 s, then four down 16 s | up 935.5; **down 21.3 with four timeouts, pings lost, one stream each way 0**; a rekey under a four-stream upload 949.2, replay 0, the capture clean; everything fine after the rekey |
| 2 | the same on the pair the rekey made | up 952.9; **down 13.3 with timeouts, pings lost, one stream each way 0**; the rekey 951.7; fine after |
| 3 | four streams up 16 s, a pause of 5 s, pings, one stream down 4 s | 953.8, 5/5, 824.2: **not dead** |
| 4 | four streams up 16 s, then four down at once, the coprocessor read across the download | 953.4; **down 8.2 with timeouts**; across the download `FROM_WIRE_TO_IPSEC_DECR` +13,550, `CRYPTO_DROP_AUTH` +1,379, `TX_DROP` +1,379; pings 0/3; the next 42 inbound frames all `CRYPTO_DROP_AUTH` |

Three runs in four: the coprocessor decrypts and terminates the peer's frames for a while - 1.36
million inbound frames once, about 3 million another time, 1.36 million once without a kill - and
then refuses every one with `CRYPTO_DROP_AUTH` until a rekey gives it a fresh association. No
counter of the driver's moves (`rx_bad` 0, `rx_nosa` 0, `rx_nokey` 0): the drop is inside the
coprocessor's authentication, and the frames it drops are the peer's, which the peer numbered
correctly (the kernel's cipher, given the same frames after build two, authenticates every one).
The outbound half ran at 935 to 954 throughout and the peer dropped nothing. A reckoning of the
high half of the sequence number that goes wrong under a burst is the likely shape; it is the
fast path's, and not readable from here.

## Build two: the arriving association refused, and the sequence that killed build one does not

`octep_ipsec_sa_newkey` answers `EOPNOTSUPP` to an inbound association with the ESN flag, which
leaves it with the kernel's cipher; an outbound one with the flag is taken as before. The refusal
is silent and counted with the others in `sa_refused` - the kernel offers every association to
every interface with the capability, so a pair on this appliance is 24 offers, of which two are
taken; under this build one is, and the count reads 23 refused per pair.

| | |
|---|---|
| the table | one row, `encrypt`; the kernel holds both with `flags=0x400` |
| the peer's frames, decrypted by the kernel | its ESP input 151 to 5,861,941 over the run, replay 0; `rx_done` unchanged at 13; the coprocessor shows no `FROM_WIRE_TO_IPSEC_DECR` and no `CRYPTO_DROP_*` |
| the appliance's frames, encrypted by the coprocessor | `tx_encrypt` 3,876,458 = `FROM_KN_TO_IPSEC_ENCR` +3,876,458; the peer's ESP input +3,876,460 (the kernel's two first frames of the rekey among them), its replay drops unchanged; `out_drop` 0, `tx_nosa` 0 |
| four streams up 16 s, then four down 16 s, then one each way 12 s | **933.9, 955.6, 796.4, 822.8** - the download after the upload that killed build one three times in four |
| pings after it, the PMTU answer, the largest ping | 3/3; a 1,472-byte DF ping answered "needs to be fragmented"; 1,418 bytes 3/3 |
| a rekey under a four-stream upload | 940.7, the peer's replay drops 0; its capture: the kernel's cipher sent two frames numbered 1 and 2, the coprocessor's begin at 0x100002, as on the plain tunnel |
| the host over the run | `irq88: octep0` 45.9%, `swi1: netisr 0` 30.0%, four more `octep0` interrupt threads at 16.7 to 17.4% - the kernel's cipher at a gigabit arriving |
| the coprocessor over the run | `TX_DROP_QUEUE_FULL` +126 (build one's runs saw +1); `FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE` +9,739,497 |

So the three measurements the issue asks for hold for the direction that leaves - installed and
read back, every frame accepted by the peer, the plain tunnel's rates - and the direction that
arrives is correct because the kernel does it, at the host's cost shown in the thread table.

## What is not established

- Why the coprocessor's authentication fails under ESN after a burst, or at what count. The
  volume at the kill was not a fixed number, and the one run with a pause between the upload and
  the download did not die. Nothing in the driver's request differs from the vendor's host's for
  such a state as far as its source shows; what differs is not in the host.
- Whether the replay window's size (32 here) matters to it. Not varied.
- Whether an inbound ESN association that the coprocessor held for a short enough time stays
  correct: the kill came at a million frames or more, and a rekey every few minutes would stay
  under it. Not tried, and not the kind of thing to ship.

## Lessons

- **A child option that strongSwan ignores looks like a setting that took.** `esn = yes` loaded
  without complaint and did nothing; the proposal is where ESN lives. The first reading of the
  pair "with ESN" was of a pair without it.
- **One half working hides the other half dead - again.** As on the first NAT-T build, the
  outbound half ran at full rate through every dead download; only the download's own rate and
  the coprocessor's `CRYPTO_DROP_AUTH` said what was wrong.
- **A reboot regenerates the hand-edited file on one end and not the other.** The module change
  for build two was installed by reboot under a raised lab; the appliance's `swanctl.conf` came
  back from its template with `default` while the peer's still said `aes128gcm16-esn`, and the
  tunnel would not come up at all - every stream timed out before a single frame was offered. The
  tool now puts the proposal back at both ends after any reboot.

## Where it sits

The acceptance is in [octep_ipsec.c](../../contrib/octep/octep_ipsec.c) (`octep_ipsec_sa_newkey`,
the refusal with the measurement in its comment); the request's control bit in
[octep_rpc.c](../../contrib/octep/octep_rpc.c). The pages that said "no ESN" - the README,
[the kernel drives the coprocessor](the-kernel-drives-the-coprocessor.md),
[the UDP header is the host's to write](esp-behind-udp.md) - are struck with the date and this page
after them. The numbers are in [the measurements](../measurements/xgs3300.md#an-association-with-extended-sequence-numbers).
