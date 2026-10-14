# A replay window on the ARMADA: the engine's check, measured with a replayed frame

2026-10-14, the XGS 136, step 5 slice 9 of [the offload plan](../npuep/offload-plan.md). The OCTEON
TX [measured its windows on 2026-10-12](a-window-wider-than-256.md) as rates over millions of frames;
this family's turn, as counts and with one measurement that page did not have: a captured ESP frame
sent again by the peer, so the drop is seen and not inferred. Module build `fbb844c5` (187,032-byte
`npuep.ko`) from the working tree, the direct-link bench with NAT-T and AES-CBC/SHA-384 still on from
the previous slices.

## What was missing

Until this build the decrypt association went to the engine with a window of 0 and the anti-replay
bit (ctrl bit 17) off, and the receive termination does not advance the kernel's own window for an
association it terminates. So an offloaded inbound association had **no replay check at all**: not
the engine's, not the kernel's. The pages before this one say "the replay window is the engine's";
that was the intention, not a measurement, and it is struck there. The measurement below, on the
previous build, shows what that meant on the wire.

## What the driver does now

The window the kernel keeps is in bytes of bitmap (`sav->replay->wsize`; 4 bytes is strongSwan's
default of 32 packets). `npuipsec_win_of` turns it into packets for the decrypt side, and `SA_ADD`
carries it at +112 with the anti-replay bit set when it is not 0 - the OCTEON TX driver's
`rec.win` and `rpc_sa_arw`. The encrypt side's window stays 0 with the bit off, as the vendor's host
sends it. A window that is not a power of two, or wider than the engine's `USFP_SA_RWIN_MAX` of
4,096, keeps the association with the kernel: `npuipsec_win_ok` refuses it before an index is taken,
`ipsec.win_refused` counts it, and the association's row on `ipsec.sas` says why. Nothing in the
engine's `SA_ADD` checks the size it is sent - the bucket index is masked, so a wider window would be
accepted and tracked wrongly - which is why the check is the driver's, as on the other family.

## Measured

Each window: `replay_window` on the child in the appliance's `swanctl.conf` (the peer's end is its
own and was not changed), `swanctl --load-all`, the child re-initiated; the row on `ipsec.sas` read
for the window the engine was asked; ten pings each way; `FROM_WIRE_TO_IPSEC_DECR`,
`FROM_KN_TO_IPSEC_ENCR` and the two replay counters read across each.

| window | the row | the kernel (`setkey -D`) | appliance 10 / peer 10 | decrypted by | replay counters |
| --- | --- | --- | --- | --- | --- |
| 32 (the default) | `win 32`, accepted | `replay=4` | 10/10, 10/10 | the engine: `DECR +10`, `rx_done +10` | none |
| 1,024 | `win 1024`, accepted | `replay=128` | 10/10, 10/10 | the engine: `+10`, `+10` | none |
| 4,096 | `win 4096`, accepted | `replay=512` | 10/10, 10/10 | the engine: `+10`, `+10` | none |
| 48, not a power of two | `win 48`, **window refused, left to the kernel**; `win_refused 1` | `replay=6` | 10/10, 10/10 | the kernel's cipher: `esp packets in +10`, `rx_done +0`, no `DECR` | - |
| 8,192, above the engine's | `win 8192`, **refused**; `win_refused 2` | `replay=1024` | 10/10, 10/10 | the kernel's cipher: `+10`, `rx_done +0` | - |

The encrypt side stayed on the engine throughout, `ENCR +10` on every row, refused windows
included: the refusal is the decrypt association's alone.

### The replayed frame

At a window of 32: the peer captured one of its own ESP frames to the appliance (`seq=0x1` of a
fresh child, one ping) and sent the same bytes again, twice, from the port facing the appliance
(a BPF write with the source address kept). Then forty more pings, to move the window past 32, and
the same old frame once more.

| what the peer sent | `FROM_WIRE_TO_IPSEC_DECR` | `CRYPTO_DROP_REPLAY_DUP` | `CRYPTO_DROP_REPLAY_OOW` | `rx_done` |
| --- | --- | --- | --- | --- |
| the frame of `seq=0x1`, twice more | +2 | **+2** | - | +0 |
| forty pings, then the old frame once | +41 | - | **+1** | +40 |

Both drops are the engine's: the frames reached its decrypt path (the `DECR` count moved) and the
host never saw them (`rx_done` did not). The same test against the kernel's own check, at the
refused window of 48: `netstat -s -p esp` "possible replay packets detected" +2, the two frames
counted in and dropped by the kernel. So an association the driver refuses is not unprotected
either; it is the kernel's, with the kernel's window.

### The previous build, for the gap

The build before this one (`727d0f32`, the CBC slice's) was put back for one run, the same bench,
the same test: one ping from the peer, its ESP frame (`seq=0x1`) captured and sent twice more.

| build | `FROM_WIRE_TO_IPSEC_DECR` | `CRYPTO_DROP_REPLAY_*` | `rx_done` | the host's ICMP echo replies | `FROM_KN_TO_IPSEC_ENCR` |
| --- | --- | --- | --- | --- | --- |
| `727d0f32`, window 0 and the bit off | +2 | none | **+2** | **+2** | **+2** |
| `fbb844c5`, this one, `win 32` | +2 | `DUP +2` | +0 | +0 | +0 |

On the previous build the two replayed frames were decrypted, terminated, delivered and answered:
the appliance sent two more encrypted echo replies for one ping. That is the gap, measured; it is
[issue 405](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/405), and this build closes it.

## Lessons

- **A check that is intended is not a check.** The receive termination's comment said the window
  was the engine's. The bytes sent said 0 and bit 17 off. The replayed frame is the instrument
  that tells the two apart, and it costs one capture and one write to a BPF descriptor.
- **Measure the negative on the build that had it.** The previous build was put back for one run
  so the gap is a measurement, not a reading of the bytes - the row above.
- **The window is one end's own.** `replay_window` is the receiver's setting; the peer's child was
  not touched for any row.

## Where it sits

`npuipsec_win_of`, `npuipsec_win_ok` and `NPUIPSEC_SA_WIN_MAX` in
[npuipsec.c](../../contrib/npuep/npuipsec.c); the window at +112 and the bit in `npuipsec_sa_add`;
the refusal in `npuep_ipsec_sa_newkey` before an index is taken; `ipsec.win_refused` and the row's
`win N` on `ipsec.sas`.
