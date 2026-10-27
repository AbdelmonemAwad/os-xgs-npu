<!-- reviewed: 2026-10-27 -->
# What wakes a deaf ring: three tries and the vendor's way, on the second XGS 3300, 2026-10-27

[Issue 361](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361): now and then a receive ring of the
XGS 3300 raises its interrupt and the host never runs the handler. The look of 2026-10-21 finds such a
ring within a tick or two and takes its packets, but the ring stays deaf: the block sends nothing for the
next ones ([lost-interrupt-2026-10-21.md](lost-interrupt-2026-10-21.md#so-the-host-looks)). #793 put in
three things to try on such a ring, and a way to leave the ring's status bit alone as the vendor's host
does, all off by default. This is the soak that compares them. None of the three brings the ring back as a
rule, and leaving the status bit alone does not stop rings going deaf.

## How it was measured

The trigger-on soak of 2026-10-21, unchanged (`images/deploy/lab-3300-361-soak.sh`, not in this
repository): the PC downloads through the second XGS 3300, which translates it and hands each connection
to the fast path, from the first XGS 3300. Five settings, twenty minutes each, one after another the same
evening (18:01 to 19:44 UTC):

- **The box under test:** the second XGS 3300, vendor version 22.
- **The driver:** `octep.ko 211004b5`, main as of #842, which carries #793's instruments, on both XGS 3300s.
- **The settings:** `images/deploy/lab-361-wake-1027.sh` set two sysctls before each run and put the
  defaults back at the end. The translation rule, the route, the address and the servers were removed
  after each run, and each report says so.

The two sysctls:

- **`dp.look_wake`:** what the look tries on a ring holding packets with the count's interrupt bit set
  (bit 62), before it takes them. It then waits `dp.look_wake_us` and counts whether the handler ran:
  `look_wakes`, `look_woke`.
  - 0: nothing, the default.
  - 1: RESEND.
  - 2: the ring's status bit cleared.
  - 3: the ring's MSI-X entry masked and put back.
- **`dp.istat_clear`:** 1, the default, clears the ring's bit of `R_OUT_INT_STATUS` at the start of every
  pass. 0 leaves it alone, as the vendor's host does.

## Five settings, twenty minutes each

![The five settings side by side on the second XGS 3300: 152 or 153 passes each, medians 3,071 to 3,196 Mbit/s; stalls 1, 2, 2, 0 and 2; rings the look found 100, 68, 81, 94 and 85; tries 33, 53 and 73, of which 0, 0 and 4 woke the handler; and the look's log of the MSI-X run, ring 5's handler entries 10,251,108 before the try and 10,251,109 after](../images/measurements/deaf-ring-wakes-2026-10-27/01-second-3300-what-wakes-a-deaf-ring.png)

| setting | `look_wake` | `istat_clear` | passes | median a stream | stalls | rings the look found | tries | the handler ran |
|---|---|---|---|---|---|---|---|---|
| base, what the driver does | 0 | 1 | 153 | 3,162 Mbit/s | 1 | 100 | - | - |
| wake1, RESEND | 1 | 1 | 152 | 3,196 Mbit/s | 2 | 68 | 33 | 0 |
| wake2, the status bit cleared | 2 | 1 | 152 | 3,161 Mbit/s | 2 | 81 | 53 | 0 |
| wake3, the MSI-X entry masked and put back | 3 | 1 | 153 | 3,104 Mbit/s | 0 | 94 | 73 | 4 |
| noclr, the status bit left alone | 0 | 0 | 152 | 3,071 Mbit/s | 2 | 85 | - | - |

What the columns count:

- **Passes:** every one completed with a receiver's figure.
- **Stalls:** `rxwd_stalls`, rings the watchdog found left with packets in them - the number that should
  not move.
- **Rings the look found:** `rxpeek_found`. It counts both kinds the look meets: a ring whose interrupt was
  raised and not handled, and a lone packet waiting for the time threshold.
- **Tries:** made only on the first kind, so in the three runs that try, it is the count of deaf rings met.

`msix_moved` was 0 in every run: no visit found a ring's MSI-X entry changed from the kernel's. The first
XGS 3300, the far end on vendor version 23 with the defaults, counted no stall in any run.

## What it shows

- **RESEND does not wake a deaf ring.** None of 33. The vendor's host writes it after every poll; on
  2026-10-21 it was written as every visit left a ring, and now on the deaf ring itself. Neither brought
  the handler.
- **Neither does clearing its status bit.** None of 53.
- **Masking its MSI-X entry and putting it back woke 4 of 73.** In the look's log, ring 5's handler entries
  went from 10,251,108 to 10,251,109 across the try; the other seven tries the log kept show no change. The
  log cannot tell whether those four were the lost message delivered at last, or a new one raised while the
  entry was masked. Either way, one in eighteen is not a remedy.
- **The driver's own clear is not what makes a ring deaf.** With `dp.istat_clear` 0 the ring's status bit
  was never cleared (`istat_reset`, which counts a clear found undone at once, read 0, against 14.7 to
  16.1 million with it). Even so:
  - The watchdog counted 2 stalls, rings left with 48 and 121 packets.
  - The look's log shows rings holding 1, 32 and 86 packets with bit 62 set and the handler not run.
- **The stalls cannot rank the settings.** 0 to 2 a run is too few, so wake3's 0 is not a result. What
  brings a deaf ring back is still the look, within a tick or two, with the watchdog's sweep behind it.

## What is left

- **The defaults stay as they are:** `dp.look_wake` 0 and `dp.istat_clear` 1. The instruments stay in,
  off.
- **The cause is still unknown.** Why the block's message does not reach the host is not known, and
  [361](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361) stays open.
- **What to try next:** the only try that brought the handler back, now and then, is the MSI-X entry's. A
  run that reads the entry's pending bit after masking it, before putting it back, would say which of the
  two those wakes were.
