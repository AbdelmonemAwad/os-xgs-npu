<!-- reviewed: 2026-10-25 -->
# The coprocessor's own watchdog and memory error reports, 2026-10-25

Discovery 24 of [the discoveries](../findings/discoveries-2026-10-22.md#the-coprocessors-own-watchdog-memory-reports-clock-and-randomness).
Both coprocessors have a watchdog of their own and the XGS 3300's reports memory errors (EDAC); whether
anything fed the watchdog, and what its firing would do, were not known. Read on the three
coprocessors' own Linux through `npu-run.sh` - the second XGS 3300 on vendor version 22, the first
on vendor version 23, the XGS 136 on vendor version 21 - and the watchdog left unfed once on the first XGS 3300, three
times (`images/deploy/lab-copro-health.sh`, `lab-copro-wdt.sh`, `lab-copro-wdt2.sh`).

## What each has

![The three coprocessors: an sbsa-gwdt watchdog in each, kept fed by busybox "watchdog -t 5"; on both XGS 3300s the memory controller's EDAC and nine cache controllers, 19 corrected-error and 19 uncorrected-error counters, all 0; on the XGS 136 no EDAC controller at all](../images/measurements/copro-health-2026-10-25/01-three-boards-the-watchdog-and-edac.png)

| | XGS 3300, vendor version 22 | XGS 3300, vendor version 23 | XGS 136, vendor version 21 |
|---|---|---|---|
| the watchdog | sbsa-gwdt, reset on expiry (`action=0`) | the same | the same |
| kept fed by | busybox `watchdog -t 5`: every 5 s, its timeout set to busybox's default, 60 s | the same | the same |
| EDAC | the memory controller (`thunderx-lmc`) and nine L2 cache controllers: 19 corrected-error counters, 19 uncorrected | the same | none registered |
| errors counted | 0 and 0 | 0 and 0 | - |

The kernel logs the watchdog at 10 s, and the feeder sets it to 60 at its start - busybox's `-T`,
not given here, defaults to 60.

## When the watchdog goes unfed, the XGS 3300's host goes down with it

The feeder was stopped (`kill -STOP`: it keeps the device open and writes nothing) on the first XGS
3300, whose host is reached on its own management port, which no coprocessor carries; and the host
was asked every 3 s for its heartbeat with the fast path, its management link to the coprocessor and
whether the coprocessor answered.

![The first XGS 3300, vendor version 23: the coprocessor's feeder stopped at 05:35:43; the host answering, the heartbeat alive and the coprocessor answering until 56 s; at 63 s the host no longer answers; it booted 220 s after the stop, and its chipset recorded why: the hardware watchdog expired](../images/measurements/copro-health-2026-10-25/02-sfos23-the-feeder-stopped.png)

| run | the host last answered | silent | the host booted again | why, as its chipset recorded it |
|---|---|---|---|---|
| 1 | 56 s after the stop | - | 134 to 194 s after | the hardware watchdog expired |
| 2 | 57 s | 64 s | 220 s | the hardware watchdog expired |
| 3 | 56 s | 63 s | 220 s | the hardware watchdog expired |

Everything stood until the coprocessor's 60 s ran out; then the host stopped answering on its own
management port and logged nothing more, and the watchdog that reset the appliance was the host's -
the AMD FCH's, armed at 128 s by os-xgs-npu (XGS: Settings, on by default since 2026-10-17) and fed
from the host every second. 60 s, then 128 s, then the time a boot takes before its kernel starts:
220 s. So the coprocessor's reset froze the host, and the host's own watchdog brought the appliance
back - about four minutes in all, with every port and tunnel through it down. Nothing records that
the coprocessor's watchdog started it: the reason the host's chipset keeps is its own.

## What it means for os-xgs-npu

- **The host's watchdog is what brings an XGS 3300 back** after its coprocessor resets itself, and
  it does - so it stays on by default.
- **A coprocessor reset should not freeze the host.** It does on the XGS 3300, which is now
  [an issue](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/768).
- The coprocessor's EDAC counts are readable on the XGS 3300 and could be shown with the appliance's
  health; not built. The XGS 136 has none to show.

## What it does not say

- What froze the host - the driver, the PCIe link or the processor - and whether a host with its
  watchdog off would stay frozen: not tried.
- The XGS 136: its host has no management port this bench reaches apart from the coprocessor's own
  ports, so a frozen one could not be reached or restarted from here - it was read, not left unfed.
- Why the coprocessor's feeder would stop in the field: here it was stopped by hand.
