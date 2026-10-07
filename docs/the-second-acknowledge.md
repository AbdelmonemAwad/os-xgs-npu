# The second acknowledge: measured, and made unnecessary

[#227](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/227) found one log line 126 times in a
single system log, at every boot:

    octep0: nwa: status 1 with a 9 byte reply stranded in the window - acknowledging it

Every NetAgent transaction ended by writing `TURN = ACK`, ringing the doorbell, and returning
without waiting for the target's `STATUS` to go back to `IDLE`. The next transaction's idle wait
then found the window still held, and `octep_nwa_release()` - written for a *previous host* that
had died between reply and acknowledge - acknowledged it a second time, with a wait, before the new
request could go out. The issue ended with the one question that decides the fix: **how long does
the target take to let go once asked?** Microseconds would mean the acknowledge is simply not
noticed until the next doorbell and the fix is in the ordering; milliseconds would mean the
transaction is right to return at once and only the log line should go.

## The instrument

`octep_nwa_ack_settle()` waits for `STATUS == IDLE` after an acknowledge and says how long it
took: 250 reads two microseconds apart with the mutex held, then the existing sleeping wait with
the result marked *slow*, the elapsed time measured whole with `sbinuptime()`. Two places use it:

| sysctl under `dev.octep.0.nwa` | measures |
|---|---|
| `releases`, `release_slow`, `release_us_last`, `release_us_max` | the second acknowledge in `octep_nwa_release()`: how often a transaction found the window held, and how long that acknowledge took. A release made by hand through `nwa.release` is timed and logged but not counted |
| `ack_wait`, `ack_waits`, `ack_slow`, `ack_us_last`, `ack_us_max` | with `ack_wait` set, the transaction itself waits for idle after its own acknowledge, and this is what that wait cost |

The old log line is gone from the quick case. It remains, with the time, when the acknowledge was
slow or never completed.

## What the appliance said, 2026-10-07

One boot of the module carrying the instrument, the link poll running its one transaction a
second, nothing else on the mailbox.

| | `commands` | `releases` | `release_us_max` | `ack_waits` | `ack_slow` | `ack_us_max` |
|---|---|---|---|---|---|---|
| two minutes after boot | 82 | 5 | 52 µs | – | – | – |
| `ack_wait=0`, 60 s later | 612 → 669 | **13 → 13** | 61 µs | 0 | 0 | – |
| `ack_wait=1`, 60 s | 669 → 727 | 13 | 61 µs | 58 | 0 | 67 µs |
| `ack_wait=1`, 120 s | 727 → 784 | 13 | 61 µs | 115 | **0** | 410 µs |

Three things in that table.

**The second acknowledge is a bring-up phenomenon.** Thirteen releases, all in the first two
minutes, when the port loop and the driver's own filter requests issue transactions back to back.
In steady state, with a second between transactions, the target has long let go by the time the
next one begins: `releases` did not move in a minute of `ack_wait=0`.

**Letting go takes tens of microseconds.** Every release settled in 20 to 61 µs; every one of 115
waits inside the transaction settled inside the half-millisecond spin, 18 to 67 µs with one outlier
at 410 µs. Nothing ever slept. The target notices the acknowledge at once; it was never waiting for
a second doorbell. The issue's first hypothesis is the right one.

**So the wait belongs inside the transaction**, and from this change it is the default: `ack_wait`
is 1, a transaction ends with the window idle, and the next one - the bring-up's back-to-back ones
included - finds it so. The cost is the table's: well under a tenth of a millisecond per
transaction, paid once a second, against a request that itself costs about twelve. The spin budget
of 500 µs covers the outlier with room; a target that ever needs longer is counted in `ack_slow`
and logged, and `octep_nwa_release()` stays as what it was first written for - the recovery from a
host that died mid-transaction.

The overlap test for #224 ran on the same boot: three hundred hand requests in four seconds over
the running poll, `releases` unchanged throughout, `timeouts` 0.

## The next two boots, with the default on

| boot | `commands` / `ack_waits` | `ack_slow` | `ack_us_max` | `releases` |
|---|---|---|---|---|
| second, two minutes in | 80 / 80 | 0 | 68 µs | **0** |
| third, two minutes in | 82 / 82 | **2** | 10,994 µs | **0** |
| third, after the day's measurements | 261 | 3 | 10,994 µs | 0 |

`releases` stayed at zero: a transaction that ends idle leaves nothing for the next to acknowledge,
and the line the issue counted 126 times is gone because the case is gone. But the third boot
corrects the second's "nothing ever slept": three acknowledges of 261 outlived the half-millisecond
spin and slept one tick, 10 ms, two of them during the bring-up. About one in a hundred, bounded by
the tick, counted in `ack_slow`, and still three orders of magnitude from the four seconds the
transaction was first kept from waiting for. The default stands; the number beside it is now "tens
of microseconds, and one in a hundred takes a tick".

**Lesson.** A transaction that ends without confirming the peer has let go has not ended; it has
deferred its ending onto whoever comes next. The issue said that. What the measurement adds is
that "confirming" cost fifty microseconds, and the four-second fear that kept it out of the
transaction was the failure path's cost, not the success path's - which nobody had measured
separately until there was a counter for each.
