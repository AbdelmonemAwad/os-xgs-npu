<!-- reviewed: 2026-10-12 -->
# A window wider than 256 ends the coprocessor's replay drops

Measured on an XGS 3300, 2026-10-12, modules `a4c1091e` and `ca92801b`. The last item of
[issue 294](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/294): replay windows other than
strongSwan's default of 32 on an association the coprocessor holds. Six sizes run on the lab
tunnel, the three measurements on each, and one number the series settles: at 1,024 and above the
coprocessor drops nothing as out of window, where 32 to 256 drop a few in a million.

## How the window gets there

strongSwan's child option `replay_window = N` (OPNsense 26.7.5's model has no field for it, so it
goes into the generated `swanctl.conf` by hand at both ends, loaded with `swanctl --load-all`).
The kernel keeps the window in bytes of bitmap - `replay=N/8` in `setkey -D` - and takes any size
up to 2^32-32 packets through `SADB_X_EXT_SA_REPLAY`. The driver sends the size in packets, as
the vendor's host does (`req->opr.win_size = hsa->control.window_sz`), and refuses what is not a
power of two. The coprocessor's association holds at most 4,096 (`USFP_SA_RWIN_MAX` in the
vendor's `sa_table.h`, a bitmap of twice that so the whole range is usable), and nothing in its
SA_ADD checks the size it is sent: `sa->cfg.win_size = req->opr.win_size`, and the window update
masks the bucket index. A larger window would be accepted and tracked wrongly, not refused. So the
driver refuses it, since the second build of the day, and the kernel keeps such an association.

## The series

Four streams up for 16 s, four down for 16 s, one each way for 12 s, five pings, the coprocessor's
counters read across all of it; `dp.auto` 1, `ipsec.flows` 0. The inbound column is the peer's
frames decrypted and terminated here; the outbound one the appliance's frames encrypted by the
coprocessor and counted by the peer.

| window | installed and read back | inbound | outbound | four streams up / down, one up / down, Mbit/s |
|---|---|---|---|---|
| 64 | `win 64`, kernel `replay=8` | `rx_done` +4,500,152, `rx_bad` 0; `FROM_WIRE_TO_IPSEC_DECR` +4,500,902, **`CRYPTO_DROP_REPLAY_OOW` +19** | `tx_encrypt` +2,519,961, the peer's ESP input the same, its replay drops unchanged | 933.4 / 960.8 / 788.4 / 802.3 |
| 128 | `win 128`, `replay=16` | +4,511,552, 0; +4,512,334, **+14** | +2,550,740, the same | 949.4 / 961.1 / 790.1 / 791.8 |
| 256 | `win 256`, `replay=32` | +4,535,721, 0; +4,536,328, **+11** | +2,585,429, the same | 944.5 / 933.5 / 828.5 / 819.6 |
| 1,024 | `win 1024`, `replay=128` | +4,542,081, 0; +4,542,939, **none** | +2,568,113, the same | 954.7 / 959.7 / 795.2 / 808.8 |
| 4,096 | `win 4096`, `replay=512` | +4,500,636, 0; +4,501,227, **none** | +2,554,122, the same | 947.4 / 957.0 / 788.9 / 791.4 |
| 48, not a power of two | **refused**: one table row, the kernel's `replay=6` | the kernel's cipher: its ESP input +4,534,348, **its replay drops 0 to 32**; `rx_done` unchanged | +2,564,493, the same | 952.6 / 956.2 / 802.7 / 801.9 |
| 8,192, above the coprocessor's limit (module `ca92801b`) | **refused**: one table row, the kernel's `replay=1024` | the kernel's cipher: +4,532,578, **its replay drops 0**; `rx_done` unchanged | +2,558,312, the same | 934.4 / 956.3 / 803.6 / 822.4; host `irq88: octep0` 43.4 %, `netisr` 27.5 % |
| 4,096 again, on `ca92801b` | `win 4096`, `replay=512` | +4,474,873, 0; +4,475,369, **none** | +2,534,903, the same | 955.0 / 961.0 / 780.7 / 761.1 |

Every ping answered, every stream at the plain tunnel's rate; `rx_nosa`, `rx_nokey`, `out_drop`,
`tx_nosa` all 0 throughout. At 32 the same download lost 35 to 45 of 1.07 million on 2026-10-07
([one encryptor per association](one-encryptor-per-association.md): 44 of 1,063,464), and
[the flow carries the association](the-flow-carries-the-association.md) had one run at 1,024 with
none; the series puts the knee between 256 and 1,024.

## The kernel's cipher on the same path, now

The row at 48 is also a measurement of the kernel's cipher on the 10 Gbit/s port, where
[issue 297](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/297) found 149 Mbit/s down with
1,966 replay drops at a window of 32 on 2026-10-07. At 48, on this day's receive path
([a visit is not a pass](a-visit-is-not-a-pass.md), [a ring that fills](a-ring-that-fills.md)),
it is 956.2 Mbit/s down with 32 drops in 4.5 million, at a host cost of `irq84: octep0` 45.2 % and
`swi1: netisr 0` 30.4 %. The disorder issue 297 measured between the port and the kernel's cipher
is mostly gone with the ring work; what is left is of the order the coprocessor sees too.

## What is not established

- Where the drops at 64 to 256 are made. The peer encrypts in software on several cores and may
  hand its frames to the wire out of sequence order; the coprocessor checks the window on frames
  as they arrive at the port, on more than one worker. Neither was instrumented. The number is
  small - 2 to 4 in a million - and TCP recovers it, which is why the rates do not show it.
- Whether a window wider than 4,096 is ever wanted. The kernel takes one; the coprocessor cannot,
  and the driver leaves such an association with the kernel rather than hand the coprocessor a
  window it will track wrongly.
- The cost of the window to the coprocessor: none visible in the rates between 64 and 4,096.

## Lessons

- **The receiver does not enforce its own limit.** `USFP_SA_RWIN_MAX` is a constant in a header;
  SA_ADD stores whatever it is sent. A limit that lives only in a comment is the sender's to keep.
- **One run each is a hint; a series is a measurement.** The earlier "none at 1,024" stood on one
  run. Five windows, each over 4.5 million frames, put the knee where it is.

## Where it sits

The refusal is in [octep_ipsec.c](../../contrib/octep/octep_ipsec.c) (`octep_ipsec_sa_newkey`,
`OCTEP_SA_WIN_MAX` with the reason beside it); the request's `win_size` at +112 is in
[octep_rpc.c](../../contrib/octep/octep_rpc.c). The numbers are in
[the measurements](../measurements/xgs3300.md#replay-windows-other-than-32).
