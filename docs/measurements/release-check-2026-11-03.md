<!-- reviewed: 2026-11-03 -->
# The review before the next release, 2026-11-03

The candidate for the next release - main at `2a000ea` - checked against v0.1.0-alpha.12, the release
the appliances have now, on the lab's three appliances, each on OPNsense 26.7.8_1 (FreeBSD
15.1-RELEASE-p6): the first XGS 3300 (vendor version 23), the second XGS 3300 (vendor version 22) and
the XGS 136 (vendor version 21). Between the two, four installed files differ - the driver's module with
its kernel stamp, `feeder.py` and `fastpath.py` - from
[989](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/989),
[991](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/991),
[993](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/993),
[995](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/995),
[996](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/996) and
[998](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/998). In the tables A is v0.1.0-alpha.12 and B
the candidate. Times are UTC. Automatic problem reports were off on all three for the review, and
fastpath.py's count of recoveries in a day was emptied first, so that no recovery was held back by the
tests before.

## What it found

| where | what | issue | state |
|---|---|---|---|
| npuep, since 996 | `devctl detach npuep0` panics the host | [1000](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1000) | fixed in [1007](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1007), run on the XGS 136 |
| npuep, since 995 | the attach's early exit leaves its sysctl context allocated, empty | - | fixed in [1007](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1007), not reached on hardware |
| feeder.py, since 989 | its watch never arms while its looks are slow; one stop after a boot missed | [1001](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1001) | fixed in [1008](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1008) |
| feeder.py, since 989 | SysRq keys sent after its own quiesce, toward the coprocessor's boot firmware | [1003](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1003) | fixed in [1008](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1008), seen working 2 of 2 |
| 998 and its page | a pending bit down while masked read as a message never made | [361](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361) | corrected in [1009](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1009) |
| feeder.py | with the management link down, a stopped watchdog feeder is not put right before its reset | [1002](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1002) | open: 2 of 2 froze the host |
| mgmt-link.sh, the XGS 136's family | it types into the coprocessor's console without the console's lock, once: after a recovery the host's link to its coprocessor (`mvmgmt0`) stayed down | [1010](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1010) | open: seen once, down for the rest of that boot |
| a recovery after a quiesce | the host froze in one recovery | [1004](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1004) | open: 1 of 8 octep recoveries that day |
| the XGS 136 | the host stopped once under HTTP with a new connection for each response | [1005](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1005) | open: 1 of 14 runs |
| the XGS 136 | one download stream at a fifth of the rate in some boots, both versions | [1006](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1006) | open |

What the candidate brings, measured here: a panic of the coprocessor's Linux on an XGS 3300 did not
freeze its host, 2 of 2 (F4); npuep reloaded under the loads that panicked v0.1.0-alpha.12's module on
the morning of 2026-11-03 ([npuep-unload-panics-2026-11-03](npuep-unload-panics-2026-11-03.md)) lived
10 of 10, and its recovery 2 of 2 (F1, F2); and no regression in performance on either family
(section 3).

What it leaves, measured: a stop of the coprocessor's Linux in the first two minutes after a boot freezes
the host (F5, 2 of 2), and so does a stop of its userland alone, its kernel answering every echo (F6, 1
of 1) - both as the design of
[768](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/768)'s watch says, now measured.

## 1. Static checks

| what | result |
|---|---|
| CI on `2a000ea` | all 9 workflows passed - checks, CodeQL, Scorecard, the website and the others |
| code scanning alerts open | 0 |
| `checks.yml` run locally | 34 checks: 32 pass on Linux; the 2 that need PHP (`test-assign-mgmt`, `test-firewall-hook`) passed in CI on the same commit |
| the build an appliance makes | no warning on any of the three; octep 530,288 bytes, npuep 437,872 bytes |
| reproducible | the modules each appliance's update built are byte for byte the build box's: octep `e109669f`, npuep `d0d9952c` |
| clang's extra warnings (`-Wextra -Wshadow -Wconditional-uninitialized` and others) | none in the lines the candidate changed; 22 in older code, 16 inside FreeBSD's `mbuf.h` |
| clang's static analyser | not run: the build box's base compiler has none ("action RunAnalysis not compiled in"), and WSL has no clang |
| reviews | three independent reviews of the difference, every finding checked against the code by hand; a third of the fixes themselves changed [1008](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1008)'s first form |

## 2. The update, as an appliance gets it

XGS: Updates' own steps - `update.py channel main`, `update.py check`, then `configctl xgs update apply`,
which the button runs - on each of the three:

| what | first XGS 3300 | second XGS 3300 | XGS 136 |
|---|---|---|---|
| the update | `2a000ea` shown with its eight changes, installed in 43 s | the same, 47 s | the same, 41 s |
| warnings in the update's build | 0 | 0 | 0 |
| "nothing waits for a reboot" | yes - the module built is the one loaded | yes | yes |
| `config.xml` | not written | not written | saved again with the same content, a revision more ("mvmgmt0 assigned as opt15") |
| `install/verify.sh` after a boot | exit 0: 8 passed, 0 failed | exit 0: 8, 0 | exit 0: 13, 0 |
| the 14 configd actions the XGS pages read | all valid JSON, no error | all valid | 12 valid; `portstats` not read on the XGS 136 on purpose ([639](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/639)); `poe` "console busy" twice of three - a look of feeder.py held the console - and valid the third time, 17 s on |

On the XGS 136 every update that changes nothing in `config.xml` still saves it again, and so adds a
revision. RELEASING.md's two checks of octep's stop and detach ran on the second XGS 3300 (F8, section 4).

![The first XGS 3300, vendor version 23, after its boot on the candidate: install/verify.sh with 8 checks passed, 0 failed, exit 0](../images/measurements/release-check-2026-11-03/01-first-3300-verify-after-boot.png)

![The XGS 136, vendor version 21, after its boot on the candidate: install/verify.sh with 13 checks passed, 0 failed, exit 0](../images/measurements/release-check-2026-11-03/02-136-verify-after-boot.png)

![The first XGS 3300: the 14 configd actions the XGS pages read, as the pages run them, each valid JSON with no error](../images/measurements/release-check-2026-11-03/03-first-3300-pages-data.png)

![The second XGS 3300, vendor version 22, after its boot on the candidate: install/verify.sh with 8 checks passed, 0 failed, exit 0](../images/measurements/release-check-2026-11-03/04-second-3300-verify-after-boot.png)

![The second XGS 3300: the 14 configd actions the XGS pages read, each valid](../images/measurements/release-check-2026-11-03/05-second-3300-pages-data.png)

![The XGS 136: 12 of the 14 actions valid, portstats not read on the XGS 136 on purpose, poe "console busy" twice and valid the third time](../images/measurements/release-check-2026-11-03/06-136-pages-data.png)

## 3. Performance, A against B

Each arm was installed once the way an appliance gets it - B by XGS: Updates, A by v0.1.0-alpha.12's
own installer - and its four files were kept on the box and put back before each series, with a
restart. The order was B, A, B, A on the XGS 136 (boots at 11:24, 12:09, 12:29 and 13:16) and B, A, B on
the second XGS 3300 (11:46, 12:53 and 13:37); the other box stayed on B. A series: 300 s idle; iperf3
three times for each kind, 10 s each, from the PC on the XGS 136's LAN to the second XGS 3300 behind
its Port8; the round trip by 200 UDP probes, idle and under load; on the XGS 136, HTTP of 512 KiB
responses over 1,024 connections, a new connection for each response or the connections kept open; on
the second XGS 3300, eight minutes of three iperf3 loops through its translation and fast path. The
figures are medians with their range and count, and an exact Mann-Whitney U test (a normal
approximation where marked ~). `images/deploy/rc-ab-report.py` made the tables from the series' logs.

**The XGS 136:**

| figure | A | B | B against A | p |
|---|---|---|---|---|
| one stream down, IPv4, Mbit/s | 289 [106-533], 6 | 253 [52-552], 6 | -12.6 % | 0.59 |
| its retransmissions | 1,600 [1,332-2,036] | 1,429 [992-1,955] | -10.7 % | 0.49 |
| four streams down, IPv4 | 711 [430-928], 6 | 600 [192-927], 6 | -15.7 % | 0.39 |
| four streams up, IPv4 | 941.3 [935.1-941.6], 6 | 940.7 [928.6-941.4], 6 | -0.1 % | 0.29 |
| one stream down, IPv6 | 287 [146-452], 6 | 268 [60-535], 6 | -6.7 % | 1.00 |
| four streams down, IPv6 | 653 [376-908], 6 | 564 [174-917], 6 | -13.6 % | 0.39 |
| HTTP, a connection for each response, Mbit/s delivered | 969 [963-979], 4 | 975 [958-980], 3 | +0.6 % | 0.86 |
| its frames that reached the host | 947,497 [932,106-959,182] | 984,532 [961,288-1,013,026] | +3.9 % | 0.057 |
| its host CPU, % | 27.4 [25.1-38.8] | 25.2 [25.0-35.0] | -7.9 % | 0.63 |
| HTTP, connections kept open, Mbit/s delivered | 972 [966-981], 4 | 965 [953-979], 4 | -0.7 % | 0.60 |
| its responses failed | 1 [0-5] | 1 [0-48] | | 1.00 |
| host CPU idle over 300 s, % | 2.29 [2.27-2.30], 2 | 2.36 [2.35-2.37], 2 | +3.3 % | 0.33 |
| feeder.py's CPU time in those 300 s, s | 1.58 [1.58-1.59] | 2.01 [2.00-2.03] | +27 % | 0.33 |
| round trip idle, median, ms | 0.55 [0.54-0.56], 2 | 0.54 [0.53-0.54], 2 | -2.7 % | 0.33 |
| round trip idle, 99th percentile, ms | 6.12 [2.15-10.09] | 1.17 [0.87-1.46] | | 0.33 |
| round trip under load, median, ms | 0.63 [0.51-0.75] | 0.70 [0.52-0.88] | +11 % | 0.67 |
| round trip under load, 99th percentile, ms | 1.59 [1.44-1.73] | 13.93 [1.40-26.45] | | 1.00 |

- **The downloads follow the boot, not the arm.** One stream's median per boot: 57 Mbit/s in B's
  first, 523 in A's first, 509 in B's second and 107 in A's second - a slow boot in each arm. The sender
  was not slow (photo 15), and uploads and HTTP ran at the line's rate in every boot.
  [1006](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1006) has the figures.
- **HTTP with a new connection for each response stopped the XGS 136's host once**, in B's first boot:
  its run is not in the table ([1005](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1005)).
- **The round trip's 99th percentile** is the second-highest of 200 probes. One run in an arm had a high
  one: A's second boot idle (10.09 ms), B's second boot under load (26.45 ms, its highest probe 50 ms).
  With two runs a side, neither is a difference; one of B's runs with the connections kept open had 48
  of 9,070 responses fail.

![The XGS 136, B's first boot (11:24, npuep d0d9952c): the series - idle, iperf3, the round trip and HTTP - one stream down at 52-62 Mbit/s in that boot; its HTTP close run 2 has no figures, the run in which the host stopped (1005); the script's error line came from reading the second XGS 3300's uptime while it restarted](../images/measurements/release-check-2026-11-03/10-136-perf-B-b1.png)

![The XGS 136, A's first boot (12:09, npuep 12b01440): the same series, one stream down at 423-533 Mbit/s](../images/measurements/release-check-2026-11-03/11-136-perf-A-a1.png)

![The XGS 136, B's second boot (12:29, npuep d0d9952c): the same series, one stream down at 443-552 Mbit/s](../images/measurements/release-check-2026-11-03/12-136-perf-B-b2.png)

![The XGS 136, 12:47: where one stream down loses - its part a went through the host, not the fast path, the flow maker being off then ("flows in the table now: 0"), and is a second host-path run; part c the second XGS 3300's host sending straight to the PC, 533-616 Mbit/s](../images/measurements/release-check-2026-11-03/13-136-one-stream-down-where-it-loses.png)

![The XGS 136, A's second boot (13:16, npuep 12b01440): the same series, one stream down at 106-155 Mbit/s; the sender, the second XGS 3300, was on A's octep 1709a70f in this series](../images/measurements/release-check-2026-11-03/14-136-perf-A-a2.png)

![The second XGS 3300, 13:35, its boot of 12:53 on A's octep 1709a70f: its host sending straight to the PC over its F1, no XGS 136 between - one stream at 516-1,198 Mbit/s, four at 9,241](../images/measurements/release-check-2026-11-03/15-second-3300-host-sending-boot-1253.png)

![The A against B tables as rc-ab-report.py printed them, for both boxes](../images/measurements/release-check-2026-11-03/16-ab-comparison.png)

**The second XGS 3300** (two figures in B: its first boot, then its second):

| figure | A | B | B against A | p |
|---|---|---|---|---|
| a stream in the 8-minute soak, Mbit/s | 3,058 [2,086-4,162], 61 | 3,158 [2,011-4,536], 122 | +3.3 % | 0.73~ |
| the soak's passes that delivered | 61 of 61 | 122 of 122 | | |
| host CPU in the soak, % | 14.0 | 14.4 and 14.0 | | |
| rings stalled in the soak (`rxwd_stalls`) | 0 | 0 and 1, the known stall of [361](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361) | | |
| host CPU idle over 300 s, % | 1.22 | 1.34 and 1.22 | | |
| feeder.py's CPU time in those 300 s, s | 1.53 | 2.06 and 1.97 | +32 % | |
| round trip idle, median, ms | 0.52 | 0.47 and 0.49 | | |
| round trip idle, 99th percentile, ms | 25.19 | 0.79 and 25.57 | | |
| round trip under load, median, ms | 0.70 | 1.09 and 0.74 | | |
| round trip under load, 99th percentile, ms | 3.72 | 3.15 and 18.08 | | |

B's first idle figure (1.34 %) holds the XGS 136's restart at 11:53-11:55, which took the lab link down
and up within those 300 s; it is shown, not compared.

**No regression that belongs to the candidate was found on either box.** The one steady cost: about half a
second of CPU in five minutes for feeder.py's watch of the management link - 0.43 s on the XGS 136, 0.49 s
on the XGS 3300, about 0.15 % of one core. Nearest to a difference: the frames that reached the XGS 136's
host in HTTP with a new connection for each response, +3.9 % in B at p = 0.057, three runs against four -
not a difference at that count; none of the candidate's changes is in the packets' path as it runs by
default.

![The second XGS 3300, B's first boot (11:46, octep e109669f): idle, the round trip and the eight-minute soak, 61 of 61 passes](../images/measurements/release-check-2026-11-03/30-second-3300-perf-B-b1.png)

![The second XGS 3300, A's boot (12:53, v0.1.0-alpha.12's octep): the same series, 61 of 61 passes](../images/measurements/release-check-2026-11-03/31-second-3300-perf-A-a1.png)

![The second XGS 3300, B's second boot (13:37, octep e109669f): the same series, 61 of 61 passes](../images/measurements/release-check-2026-11-03/32-second-3300-perf-B-b2.png)

## 4. Faults made on purpose

The stop of [768](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/768): the coprocessor's getty and
watchdog feeder stopped and its end of the management link taken down, so that its own watchdog resets it
about a minute later (`images/deploy/lab-768-silent.sh`). On the second XGS 3300 IPsec associations were
up, so its recoveries detach and attach the device with the driver loaded. The plan written before the
runs had F3 five times and F4 and F7 three times each; they ran three, two and two times.

| run | box | what | runs | result |
|---|---|---|---|---|
| F1 | XGS 136 | npuep reloaded (`contrib/npuep/reload.sh`) under four loops reading its sysctls and four connecting UDP over IPv6 | 10 | 10 of 10 lived, reload.sh exit 0 each time, 97,798-116,212 sysctl reads and 16.8-19.4 million connects a run; kernel memory of type devbuf the same before and after every run: 2,389 allocations, 551,610,352 bytes |
| F2 | XGS 136 | 768's stop, fastpath.py's recovery under four loops reading sysctls | 2 valid | taken as gone after 58-60 s, taken up in place in 90 and 99 s, the host never restarted, no panic under 1.49 million sysctl reads |
| F9 | XGS 136 | HTTP, 512 KiB, 1,024 connections, a new one for each response, pf's states flushed first ([1005](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1005)) | 6 | 6 of 6 lived; 553, then 954-963 Mbit/s delivered; 1 response of about 51,000 failed |
| F10 | XGS 136 | `devctl detach npuep0` ([1000](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1000)) | 1 + 1 | the candidate: a panic; [1007](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1007)'s module: detached, the host up |
| F8 | second XGS 3300 | RELEASING.md's checks after a change to octep's stop and detach, under a stream | 1 each | `test-detach-refusal.sh`: every path refused while a ring is held (if_del, stop, rx_quiesce, kldunload), the 16 ports kept, exit 0; `test-stop-writes.sh`: 4,564 words written a second before the stop, 0 after, exit 0 |
| F3 | second XGS 3300 | 768's stop, the watch steady, main's feeder.py | 3 | **1 of 3 kept the host up.** 1: quiesced 39 s after the stop, then the host froze in the recovery ([1004](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1004)). 2: no quiesce before the reset, the host froze ([1001](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1001)). 3: quiesced 39 s after the stop, taken up in place in 172 s. SysRq keys after the quiesce in 1 and 3 ([1003](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1003)) |
| F3, fixed | second XGS 3300 | the same with [1008](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1008)'s feeder.py | 2 | **2 of 2 kept the host up:** the driver told 39 and 37 s after the stop, no keys sent, taken up in place in 176 and 178 s |
| F3b | second XGS 3300 | the coprocessor's sshd stopped first, four minutes of looks over the console, then 768's stop - sent over the console, from inside a login | 1 + 1 | main's feeder.py: no quiesce, the host froze; 1008's: no quiesce, the host froze. The stop left the console answering - its getty was not stopped - so this was a stopped watchdog feeder with the link down, not a stopped Linux: 1008's watch asked the console, was answered and left it, as it should, and the feeder was not put right before its reset ([1002](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1002)) |
| F4 | second XGS 3300 | a panic of the coprocessor's Linux, `kernel.panic` 60 | 2 | 2 of 2: the host up, the coprocessor back in 213 and 236 s |
| F7 | second XGS 3300 | an octep recovery under four loops connecting UDP over IPv6 ([994](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/994) on octep) | 2 | 2 of 2 with no panic, 123.6 and 125.1 million connects, taken up in place in 332 and 334 s; 3 of 3 with the run of that morning |
| F5 | second XGS 3300 | 768's stop as soon as the link answers after a boot | 2 | 2 of 2 froze: no quiesce, the watch needs 120 s of a steady link, as designed |
| F6 | second XGS 3300 | the coprocessor's userland stopped - getty, the watchdog feeder and sshd - its kernel and link left up | 1 | no quiesce (its kernel answers every echo), the host froze at the watchdog's reset |

**F10, `devctl detach npuep0`.** The candidate's module took a page fault in `sysctl_root()`, called from
`npuep_ifnets_quiet()`; the fix's detached. The fix's run's first line names `12b01440` - the file on the
disk by then, put there for the next boot by v0.1.0-alpha.12's installer and not loaded; the module that
ran was the fix's `4780b80e`, loaded at that boot (its kernel's log: npuep0 attached at 11:54:43,
detached at 12:07:34, attached again only at the next boot).

![The XGS 136, 11:48:45, npuep d0d9952c: devctl detach npuep0 with debug.debugger_on_panic 0 - the host restarted and wrote a dump, a page fault; the last two lines are the script's own errors (it was changed while it ran), and the trap is in the next photo](../images/measurements/release-check-2026-11-03/20-136-n1-devctl-detach-candidate.png)

![The XGS 136: the dump's trap - fault virtual address 0x28, process devctl, sysctl_root from kernel_sysctlbyname from npuep_ifnets_quiet](../images/measurements/release-check-2026-11-03/21-136-n1-the-trap.png)

![The XGS 136, 12:07:27: devctl detach npuep0 on the fix's module 4780b80e, loaded since the boot of 11:53 - the first line names the file on the disk by then, 12b01440, alpha.12's for the next boot; devctl exit 0 and the clean restart 75 s on: the host lived the detach. The dump lines after that are the candidate's of 11:48, read by the script's mistake; this run wrote none (next photo)](../images/measurements/release-check-2026-11-03/22-136-n1-devctl-detach-the-fix.png)

![The XGS 136, the boot after the fix's run: devctl's exit 0, npuep0 detached with 4,662 DMA allocations given back, the line the host wrote when it had lived, and no dump newer than the candidate's; npuep 12b01440 is A's, loaded at this boot](../images/measurements/release-check-2026-11-03/23-136-n1-the-fix-the-next-boot.png)

**F1, F2 and F9 on the XGS 136.**

![The XGS 136, 14:15-14:37, npuep d0d9952c checked at every run: ten reloads under four sysctl loops and four IPv6 connect loops, every one lived, devbuf 2,389 allocations and 551,610,352 bytes before and after each - the K after that figure is a label error, the unit is bytes; a path on the PC in its header is masked](../images/measurements/release-check-2026-11-03/24-136-F1-ten-reloads.png)

![The XGS 136, F2 run 1, 14:41: not a result - the host's link to its coprocessor was down after the reloads by hand (the last reload's one blind try to give its address was not answered, 1010), so the stop never reached the coprocessor](../images/measurements/release-check-2026-11-03/25-136-F2-stop-1.png)

![The XGS 136, F2 run 2: the coprocessor's Linux stopped under four sysctl loops, taken as gone and taken up in place in 90 s, the host never restarted; the recovery's lines carry the tag octep: on the XGS 136 too](../images/measurements/release-check-2026-11-03/25-136-F2-stop-2.png)

![The XGS 136, F2 run 3: the same, taken up in place in 99 s; after it the host's link to its coprocessor stayed down (1010)](../images/measurements/release-check-2026-11-03/25-136-F2-stop-3.png)

![The XGS 136: six HTTP runs of 1,024 connections with a new one for each response, pf's states flushed before each - all lived, 553 then 954-963 Mbit/s, 1 response of about 51,000 failed](../images/measurements/release-check-2026-11-03/26-136-F9-http-close-six-runs.png)

**F8, F3, F4, F7, F5 and F6 on the second XGS 3300.**

![The second XGS 3300, octep e109669f: test-detach-refusal.sh and test-stop-writes.sh under a stream, both exit 0](../images/measurements/release-check-2026-11-03/41-second-3300-F8-releasing-checks.png)

![The second XGS 3300, F3 run 1, 15:28: quiesced 39 s after the stop, SysRq sent after it, then the host froze in the recovery; the photo ends while the host was down - it booted again at 15:34:35, reset by the hardware watchdog (1004)](../images/measurements/release-check-2026-11-03/42-second-3300-F3-stop-1.png)

![The second XGS 3300, F3 run 2, 15:39: no quiesce before the coprocessor's reset, and the host froze](../images/measurements/release-check-2026-11-03/42-second-3300-F3-stop-2.png)

![The second XGS 3300, F3 run 3, 15:48: quiesced 39 s after the stop, taken up in place in 172 s; its "booted again: YES" is false - the boot time moved by one second, a step of the clock](../images/measurements/release-check-2026-11-03/42-second-3300-F3-stop-3.png)

![The second XGS 3300, 17:31 and 17:38, the two F3 runs with 1008's feeder.py (sha256 1843b3a4) as the run's script logged them: quiesced at 32 s of silence, the coprocessor's kernel not asked for its state, taken up in place in 176 and 178 s, booted again: no](../images/measurements/release-check-2026-11-03/50-second-3300-F3-the-fix-two-runs-logged.png)

![The second XGS 3300, F3b with main's feeder.py (md5 7c1a4728, sha256 7a3ac402), 16:24-16:30: its coprocessor's sshd stopped and a look taking 13.0 s, then the stop, sent over the console - which left the console answering; no quiesce, and the host froze](../images/measurements/release-check-2026-11-03/45-second-3300-F3b-O1-candidate.png)

![The second XGS 3300, F3b with 1008's feeder.py as it was then (md5 4cc9303d, sha256 28b2edbe, the same logic), 16:40-16:46: the same; its watch asked the console, was answered and left the coprocessor, and the host froze at the reset of its stopped watchdog feeder](../images/measurements/release-check-2026-11-03/46-second-3300-F3b-O1-the-fix.png)

![The second XGS 3300, F4 run 1: a panic of the coprocessor's Linux, the host up, the coprocessor back in 213 s](../images/measurements/release-check-2026-11-03/43-second-3300-F4-panic-1.png)

![The second XGS 3300, F4 run 2: the same, back in 236 s](../images/measurements/release-check-2026-11-03/43-second-3300-F4-panic-2.png)

![The second XGS 3300, F7 run 1: an octep recovery under four IPv6 connect loops, 123.6 million connects, taken up in place in 332 s, no panic](../images/measurements/release-check-2026-11-03/44-second-3300-F7-recovery-udp6-1.png)

![The second XGS 3300, F7 run 2: the same, 125.1 million connects, taken up in place in 334 s](../images/measurements/release-check-2026-11-03/44-second-3300-F7-recovery-udp6-2.png)

![The second XGS 3300, F5 run 1, 16:59: the stop as soon as the link answered after a boot - no quiesce, and the host froze](../images/measurements/release-check-2026-11-03/47-second-3300-F5-early-stop-1.png)

![The second XGS 3300, F5 run 2, 17:10: the same](../images/measurements/release-check-2026-11-03/47-second-3300-F5-early-stop-2.png)

![The second XGS 3300, F6, 17:19: the coprocessor's userland stopped, its kernel answering every echo - no quiesce, and the host froze at the watchdog's reset](../images/measurements/release-check-2026-11-03/48-second-3300-F6-userland-stopped.png)

The two F3 runs with 1008's feeder.py also ran a watcher of the console's lock, to see who holds the
console while the link is silent; its patterns matched its own commands, so every sample shows the
watcher itself, and its photographs are not published. Who holds the console in the plain stop is not
measured.

## 5. The soak

Six hours from 17:47:45, the candidate on all three: the XGS 136 carrying one IPv4 and one IPv6
download stream at a time, 20 s a pass; the second XGS 3300 three iperf3 loops through its translation
and fast path from the first; every ten minutes, each box's uptime, kernel memory of type devbuf, mbuf
zones, feeder.py's memory and CPU time, the driver's own counters, pf's states and kernel messages.
The XGS 136 came to it in a slow boot ([1006](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1006)),
and with its link to its coprocessor down since F2's third run
([1010](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1010)), so that every reader of its
coprocessor used the console throughout. Results to follow when it ends.

## 6. Found by reading the code, not run

- **The wait of 996 when the route copy's rebuild fails for want of memory.** The rebuild allocates
  with `M_NOWAIT` and is retried after 50 ms, doubling up to about 51 s (`fib_algo.c`), while the
  detach waits `fib_max_sync_delay_ms` and 100 ms more. A failure there would leave a stale copy past
  the wait. It needs an allocation failure injected in the kernel; not run.
- **npu-run.sh exits 1 when it could not ask** - the console's UART without its interrupt, no prompt, the
  markers not arriving - as when the coprocessor's Linux does not answer, and feeder.py reads both as a
  stop: a healthy coprocessor would be quiesced if its link were silent 30 s and asking failed at once.
  A trade-off between a missed stop and a false one, left as it is for now.
- **After a quiesce, octep reads all-ones as a reset**: a detach before the reset frees memory that a
  coprocessor still alive could write. Only by hand: recover.sh waits for the new boot before it detaches.
- Smaller: `octep_poll` after a quiesce; the watch asking a busy console again with no deadline;
  fastpath.py's time limit ending lockf, not recover.sh under it
  ([985](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/985)); the look's tries 3 and 4 with the
  MSI-X entry written again (an instrument off by default).
- ~~`contrib/npuep/reload.sh` does not give the host's link to its coprocessor its address again after
  the reload, as recover.sh does; in service nothing runs it without recover.sh.~~ `contrib/npuep/reload.sh`
  does give it, through `mgmt-link.sh coprocessor` as the boot does - once, and blind on the XGS 136
  ([1010](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1010)): in this review that try was not
  answered after 3 of the 15 reloads (F1's runs 5 and 10, and one of the five that loaded the morning's
  build), and after 1 of the 2 recoveries of F2. (Corrected the same evening: the link down before F2's
  first run was that, not a step reload.sh lacks.)
- `octep`'s `npu-platform.sh`, which reads the coprocessor's platform database over the console when an
  expansion module is fitted, at every bring-up of the driver, writes to the console without its lock
  too; read in the code by a review, not seen failing.
- fastpath.py writes its lines with the tag `octep` on every appliance, the XGS 136 too, whose driver is
  npuep (photos 25-2 and 25-3).

## 7. Not checked, and why

- clang's static analyser (section 1); CodeQL in CI, clang's extra warnings and the three reviews were
  run instead.
- A boot from the 26.7 images: they are built from the release's tag after it is made. The candidate
  changes neither the boot hook nor `xgs.inc`, and `test-firewall-hook.py` passed in CI.
- The XGS pages in a browser: they need the owner's login. Their 14 data sources were read on both XGS
  3300s as the pages run them (photos 03 and 05), and the candidate changes no page and no PHP.
- Models not in the lab: XGS 2100, 2300, 3100, 87, 107 and 126.
- A fourth boot of the second XGS 3300 (A's second): three were run, B, A, B.
- The times of [984](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/984) - Linkup and configd - in
  each recovery above: not read from the logs yet.

## Corrections in the runs

- B's second series on the XGS 136 first began against a box still going down: a wait by ping after its
  shutdown came back too early. The series was run again from its start, and the waits now look for a
  new boot time.
- Every series on the XGS 136 ended with its flow maker off, from the HTTP runs' preparation; each
  series' iperf3 runs came first, on a fresh boot with it on, so the figures stand. Photo 13's part a ran
  with it off.
- The second XGS 3300, the sender for the XGS 136's series, was left on A through the XGS 136's second A
  series; the sender was then measured straight (photo 15).
- F1's first five runs loaded the morning's build, `4b721602`, not the candidate, by a script's mistake.
  They lived and are not counted; their photograph does not show which module the reloads loaded, and
  is not published. F1 was run again with the module's md5 checked at every run.
- Photo 24's devbuf memory is in bytes; its K is a label error.
- F2's first run is not a result (photo 25-1).
- F3's third run's "booted again: YES" is false: the boot time moved by one second.
- F3b's stop went over the console from inside a login, where it does not stop the console's getty: the
  console kept answering, so F3b measured a stopped watchdog feeder with the link down.
  [1002](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1002) first took it for a stopped Linux, and
  was corrected the same evening from the console's log. Its runs' "(0 s)" is taken before the stop's
  command ran; that ended about 33 s later. F3b is to be run again with the stop landing after its own
  session.
- Photo 22's dump lines, after the fix's result, are the candidate's dump of 11:48.
- feeder.py is named by its md5 in some photos and by its sha256 in the stop script's header: main's is
  `7c1a4728` and `7a3ac402`, 1008's `b09fdad5` and `1843b3a4`.
- The watcher of the console's lock in the two F3 runs with the fix showed only itself (section 4).
- The XGS 136's link to its coprocessor stayed down from F2's third run to the end of the review
  ([1010](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1010)).
