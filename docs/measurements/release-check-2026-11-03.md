<!-- reviewed: 2026-11-04 -->
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
| npuep, since 996 | `devctl detach npuep0` panics the host | [1000](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1000) | fixed in [1007](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1007), run on the XGS 136: the detach 1 of 1, reloads under F1's loads 3 of 3 |
| npuep, since 995 | the attach's early exit leaves its sysctl context allocated, empty | - | fixed in [1007](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1007), not reached on hardware |
| feeder.py, since 989 | its watch never arms while its looks are slow; one stop after a boot missed | [1001](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1001) | fixed in [1008](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1008) |
| feeder.py, with 1008 | a look blinds the watch while it lasts and is then lent whole: a stop of the coprocessor's Linux inside a slow look was decided after its reset (F3b again) | [1012](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1012) | fixed in [1021](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1021), merged: the looks in a thread of their own; the same stop inside slow looks told octep at 32-34 s, 4 of 4, the host up (the watch, 2026-11-04). Before: 2 of 2 froze the host |
| feeder.py, since 989 | SysRq keys sent after its own quiesce, toward the coprocessor's boot firmware | [1003](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1003) | fixed in [1008](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1008), seen working 2 of 2 |
| 998 and its page | a pending bit down while masked read as a message never made | [361](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361) | corrected in [1009](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1009) |
| feeder.py | with the management link down, a stopped watchdog feeder is not put right before its reset | [1002](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1002) | fixed in [1021](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1021), merged: the feeder continued in the session of the watch's own ask at 24 s, and at 44 s behind portstats.py holding the console, no reset (the watch, 2026-11-04). Before: 2 of 2 froze the host; made on purpose after the merges (F12), main froze it 1 of 1 and the fix prepared for it kept it up 2 of 2 |
| feeder.py, the watch's redesign | a console without a Linux prompt while the link answers its echoes - F6, a userland stopped under a kernel that answers | - | said and not acted on since [1021](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1021): measured 1 of 1, the line said 32 s after the stop, no quiesce, the host froze at the coprocessor's reset as on main |
| mgmt-link.sh, the XGS 136's family | it types into the coprocessor's console without the console's lock, once: after a recovery the host's link to its coprocessor (`mvmgmt0`) stayed down | [1010](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1010) | fixed in [1017](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1017), merged: the address told through npu-run.sh under the console's lock, and again while the link does not answer; the link answered in each of two boots on all three (F11 at boot). Seen once, down ~~for the rest of that boot~~ eight and a half hours, until F11 gave it its address; typed into another session 2 of 2 with the console held (F11) |
| octep, taken as gone | its link's and RPC's reads answer all ones without reaching the device, and the teardown took that for a coprocessor off the bus: their buffers were freed while a coprocessor still running held their addresses | [962](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/962) | fixed in [1016](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1016), merged: the device asked once, its buffers kept while it holds them (F14) |
| octep, its detach | its ports detached with no route quiet, as npuep's were before 996 | [994](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/994) | fixed in [1016](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1016), merged: the detach under four loops of IPv6 connects lived on main and on the fix, 1 of 1 each (F13) |
| octep, its RPC | three callers held the softc's lock and rang the doorbell through a function that takes it again; the lock is not recursive - a panic on a kernel with INVARIANTS | - | fixed in [1016](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1016), merged |
| npuep, its doorbell | the detach's withdrawal of the softc and the doorbell handler's count were not ordered against each other's load | [962](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/962) | fixed in [1015](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1015), merged: F1 3 of 3 (F1, fence) |
| reporter.py | a failed update was reported once a collect, not once an attempt, and its title lost urllib's reason | [971](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/971) | fixed in [1020](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1020): one failure counted once, with its reason, on all three (photo 96) |
| a recovery after a quiesce | the host froze in one recovery | [1004](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1004) | open: 1 of 8 octep recoveries that day |
| the XGS 136 | the host stopped once under HTTP with a new connection for each response | [1005](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1005) | open: 1 of 14 runs |
| the bench, not the XGS 136 | ~~one download stream at a fifth of the rate in some boots, both versions - frames lost on the link from Port3 to the PC in those boots, as far as read~~ the PC's link on the 136's Port3 lost frames in some boots, and the streams through it ran at a fifth of the rate; from the 136's F1, in such a boot, 937-945 Mbit/s | [1006](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1006) | closed: a fault of the bench |

What the candidate brings, measured here: a panic of the coprocessor's Linux on an XGS 3300 did not
freeze its host, 2 of 2 (F4); npuep reloaded under the loads that panicked v0.1.0-alpha.12's module on
the morning of 2026-11-03 ([npuep-unload-panics-2026-11-03](npuep-unload-panics-2026-11-03.md)) lived
10 of 10, and its recovery 2 of 2 (F1, F2); and no regression in performance on either family
(section 3).

What it leaves, measured: a stop of the coprocessor's Linux in the first two minutes after a boot freezes
the host (F5, 2 of 2), and so does a stop of its userland alone, its kernel answering every echo (F6, 1
of 1) - both as the design of
[768](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/768)'s watch says, now measured. And, run after
the four fixes merged: a stop of its Linux after minutes of slow looks - its sshd not answering - froze
the host 2 of 2 with 1008's feeder.py, the stop falling inside a look that blinded the watch for 57 s
([1012](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1012), F3b again in section 4).

The soak - 5 h 42 min on all three at once, 3,854 passes - lost none; no box restarted and none grew its
kernel memory of type devbuf by a byte. On the second XGS 3300, forwarding three loops at about 3.3
Gbit/s each, 53 receive rings were left with packets in them and emptied by the receive watchdog
([361](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361), open; section 5).

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

**The fixes, merged, as an update brings them** (2026-11-04, 00:03-00:18). Main with
[1007](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1007),
[1008](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1008),
[1009](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1009) and
[1011](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1011) merged - a local commit, `5f4e4b0`,
made only for this, no conflict among them - installed by its own `install/install.sh` as the update
runs it, on the first XGS 3300 and the XGS 136, each restarted after:

| what | first XGS 3300 | XGS 136 |
|---|---|---|
| install.sh | exit 0 in 30 s, no warning | exit 0 in 30 s, no warning |
| the module the appliance built | octep `71a6c3f0`, the new description of `look_pba_down` in it | npuep `4780b80e`, byte for byte the module run in F10 and in F1 again (section 4) |
| `feeder.py` | `b09fdad5` (1008), watching | the same, watching |
| `install/verify.sh` after the boot | exit 0: 8 passed, 0 failed | exit 0: 13 passed, 0 failed |
| the 14 configd actions the pages read | all valid, no error | 13 valid; `portstats` not read on purpose ([639](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/639)) |
| traffic | the soak's path from it through the second XGS 3300: one stream 1,623, 2,023 and 4,030 Mbit/s, four 9,753, 8,787 and 9,053, no error | its link to its coprocessor up at the boot, the heartbeat alive |

1009's description gave its build as `4fccf671`: that build was of an earlier state of `octep_dp.c`,
three lines shorter where the module's lock calls carry their line numbers - 11 such numbers, and the
build's id, are the only bytes that differ. Its commit builds as `71a6c3f0`.

![The first XGS 3300 after its boot on the four merged: install/verify.sh with 8 checks passed, 0 failed, exit 0, from the source tree 5f4e4b0](../images/measurements/release-check-2026-11-03/67-first-3300-merged-verify-after-boot.png)

![The first XGS 3300 on the four merged: the 14 configd actions the pages read, each valid with no error](../images/measurements/release-check-2026-11-03/68-first-3300-merged-pages-data.png)

![The first XGS 3300 on the four merged, octep 71a6c3f0: three passes of one stream and three of four through the second XGS 3300's fast path, then everything set up taken away](../images/measurements/release-check-2026-11-03/69-first-3300-merged-traffic.png)

![The XGS 136 after its boot on the four merged: install/verify.sh with 13 checks passed, 0 failed, exit 0](../images/measurements/release-check-2026-11-03/70-136-merged-verify-after-boot.png)

![The XGS 136 on the four merged: 13 of the 14 actions valid, portstats not read on the XGS 136 on purpose](../images/measurements/release-check-2026-11-03/71-136-merged-pages-data.png)

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

**The XGS 136** - its streams from the PC's card on its Port3, a bench link with a record of faults; its
download rows measure that link's state in each boot more than the 136 (below, and
[1006](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1006)):

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
  [1006](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1006) has the figures. The retransmitted
  share of the segments held steady within a boot whatever the streams' number or rate: 1.99-2.68 % and
  1.22-1.72 % in the slow boots, 0.34-0.63 % and 0.41-0.61 % in the fast ones, 0.06-0.08 % with no XGS
  136 between. And in the soak's boot, a slow one, the PC's card on the 136's LAN counted receive errors
  on 2.0 % of its frames: frames lost on the copper link from Port3 to the PC, or at one of its ends -
  ~~not yet read in a fast boot.~~ and in that same boot the same download from the PC's other card on
  the 136's F1 ran at 937, 940 and 945 Mbit/s, that card counting no receive error. So the slow boots were
  the Port3 link's, not the 136's nor either version's - a fault of the bench, and the download rows
  above do not compare the two versions. The uploads, HTTP, CPU and round-trip rows ran at the line's
  rate or beside it in every boot.
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

**No regression that belongs to the candidate was found on either box** - on the XGS 136 outside its
download rows, which measured the bench's Port3 link. The one steady cost: about half a
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
| F1, 1007 | XGS 136 | the same with [1007](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1007)'s npuep `4780b80e`, after the soak | 3 | 3 of 3 lived, reload.sh exit 0 each time, 98,072-99,115 sysctl reads and 16.99-17.27 million connects a run; devbuf the same before and after every run: 6,629 allocations, 560,351,632 bytes; the host's link to its coprocessor back after each |
| F2 | XGS 136 | 768's stop, fastpath.py's recovery under four loops reading sysctls | 2 valid | taken as gone after 58-60 s, taken up in place in 90 and 99 s, the host never restarted, no panic under 1.49 million sysctl reads |
| F9 | XGS 136 | HTTP, 512 KiB, 1,024 connections, a new one for each response, pf's states flushed first ([1005](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1005)) | 6 | 6 of 6 lived; 553, then 954-963 Mbit/s delivered; 1 response of about 51,000 failed |
| F10 | XGS 136 | `devctl detach npuep0` ([1000](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1000)) | 1 + 1 | the candidate: a panic; [1007](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1007)'s module: detached, the host up |
| F8 | second XGS 3300 | RELEASING.md's checks after a change to octep's stop and detach, under a stream | 1 each | `test-detach-refusal.sh`: every path refused while a ring is held (if_del, stop, rx_quiesce, kldunload), the 16 ports kept, exit 0; `test-stop-writes.sh`: 4,564 words written a second before the stop, 0 after, exit 0 |
| F3 | second XGS 3300 | 768's stop, the watch steady, main's feeder.py | 3 | **1 of 3 kept the host up.** 1: quiesced 39 s after the stop, then the host froze in the recovery ([1004](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1004)). 2: no quiesce before the reset, the host froze ([1001](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1001)). 3: quiesced 39 s after the stop, taken up in place in 172 s. SysRq keys after the quiesce in 1 and 3 ([1003](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1003)) |
| F3, fixed | second XGS 3300 | the same with [1008](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1008)'s feeder.py | 2 | **2 of 2 kept the host up:** the driver told 39 and 37 s after the stop, no keys sent, taken up in place in 176 and 178 s |
| F3b | second XGS 3300 | the coprocessor's sshd stopped first, four minutes of looks over the console, then 768's stop - sent over the console, from inside a login | 1 + 1 | main's feeder.py: no quiesce, the host froze; 1008's: no quiesce, the host froze. The stop left the console answering - its getty was not stopped - so this was a stopped watchdog feeder with the link down, not a stopped Linux: 1008's watch asked the console, was answered and left it, as it should, and the feeder was not put right before its reset ([1002](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1002)) |
| F3b again | second XGS 3300 | the same on main `cb0eb13`, after the review - 1008's feeder.py `b09fdad5` - with the stop sent so that it lands 30 s after its session ran it, past that session's logout, and every getty stopped from then on | 2 | **2 of 2 froze the host:** no quiesce; the stop fell inside a look of feeder.py, which blinded the watch while it lasted (57 s in run 2) and was then lent whole, so the decision fell due after the coprocessor's reset ([1012](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1012)) |
| F12 | second XGS 3300 | [1002](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1002)'s case on main `cb0eb13`: the coprocessor's watchdog feeder stopped and its end of the link taken down, the console's getty left running | 1 + 2 | main's feeder.py `b09fdad5`: its console ask answered "alive" and left the coprocessor; the next look reached the console 51 s after the stop, 4 s before the reset - the host froze. The fix prepared for 1002, `a8e42b96`: its console ask continued the stopped feeder in the same session, about 45 s after the stop - the host lived, 2 of 2 |
| F4 | second XGS 3300 | a panic of the coprocessor's Linux, `kernel.panic` 60 | 2 | 2 of 2: the host up, the coprocessor back in 213 and 236 s |
| F7 | second XGS 3300 | an octep recovery under four loops connecting UDP over IPv6 ([994](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/994) on octep) | 2 | 2 of 2 with no panic, 123.6 and 125.1 million connects, taken up in place in 332 and 334 s; 3 of 3 with the run of that morning |
| F5 | second XGS 3300 | 768's stop as soon as the link answers after a boot | 2 | 2 of 2 froze: no quiesce, the watch needs 120 s of a steady link, as designed |
| F6 | second XGS 3300 | the coprocessor's userland stopped - getty, the watchdog feeder and sshd - its kernel and link left up | 1 | no quiesce (its kernel answers every echo), the host froze at the watchdog's reset |
| F11 | XGS 136 | `mgmt-link.sh coprocessor` while another session of npu-run.sh holds the console, the coprocessor's end of the link without its address ([1010](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1010)) | 2 + 2 | main's script typed its login into the other session 2 of 2 and left the console at a password prompt, the link up once by its command run in feeder.py's shell; the fix prepared for 1010: the link up 2 of 2, in 31 and 78 s, the other session undisturbed |
| F11, at boot | all three | `mgmt-link.sh coprocessor` from the bootup hook, two restarts of each box: the fix prepared for 1010 (`c33cd5f5`), then [1017](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1017)'s (`a84cf06c`) | 6 + 6 | the link answered in every boot, counted from the boot: `c33cd5f5` +128 and +128 s on the first XGS 3300, +159 and +159 s on the second, +98 and +99 s on the XGS 136; `a84cf06c` +184 and +210 s, +147 and +263 s, +87 and +86 s. main's, one boot each before: +119, +151 and +77 s - the XGS 136's a blind write without the lock |
| F13 | first XGS 3300 | `devctl detach octep0` under four loops connecting UDP over IPv6 ([994](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/994)'s case on octep) | 1 + 1 | main's octep `71a6c3f0`: exit 0 in 5 s, 4.83 million connects, the host up; [1016](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1016)'s `4adb8488`: exit 0 in 6 s - a second more for the route quiet - 5.67 million connects, the host up. No panic on either in one run each: the window 1016 closes is shown by the code, not by this run |
| F14 | first XGS 3300 | F13's detach with the coprocessor quiesced first ([962](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/962)'s gone path), 1016's octep | 1 | exit 0 in 5 s, 5.33 million connects, the host up; taken as gone, octep asked the device and kept the buffers it still held - its management link's and its RPC's - and every ring stopped, the longest in 0 us |
| F8, first XGS 3300 | first XGS 3300 | RELEASING.md's checks with 1016's octep `4adb8488`, under an upload stream | 1 each | `test-detach-refusal.sh`: every path refused while a ring is held, exit 0; `test-stop-writes.sh`: 4,564 words written in the second before the stop, 0 after, exit 0 - the same count as on the second XGS 3300 |
| F1, fence | XGS 136 | F1 with [1015](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1015)'s npuep `147248cf` | 3 | 3 of 3 lived, reload.sh exit 0 each time, 98,135-116,231 sysctl reads and 16.72-19.34 million connects a run; devbuf the same before and after every run: 2,389 allocations, 551,610,352 bytes. Run 1 unloaded 1007's `4780b80e`, loaded at that boot, and loaded the fence's: the fence's own detach ran in runs 2 and 3 |

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

![The XGS 136, 23:54-00:00, F1 again with 1007's npuep 4780b80e, checked at every run: three reloads under the same loops, every one lived, reload.sh exit 0, devbuf 6,629 allocations and 560,351,632 bytes before and after each](../images/measurements/release-check-2026-11-03/66-136-F1-1007-three-reloads.png)

![The XGS 136, F2 run 1, 14:41: not a result - the host's link to its coprocessor was down after the reloads by hand (the last reload's one blind try to give its address was not answered, 1010), so the stop never reached the coprocessor](../images/measurements/release-check-2026-11-03/25-136-F2-stop-1.png)

![The XGS 136, F2 run 2: the coprocessor's Linux stopped under four sysctl loops, taken as gone and taken up in place in 90 s, the host never restarted; the recovery's lines carry the tag octep: on the XGS 136 too](../images/measurements/release-check-2026-11-03/25-136-F2-stop-2.png)

![The XGS 136, F2 run 3: the same, taken up in place in 99 s; after it the host's link to its coprocessor stayed down (1010)](../images/measurements/release-check-2026-11-03/25-136-F2-stop-3.png)

![The XGS 136: six HTTP runs of 1,024 connections with a new one for each response, pf's states flushed before each - all lived, 553 then 954-963 Mbit/s, 1 response of about 51,000 failed](../images/measurements/release-check-2026-11-03/26-136-F9-http-close-six-runs.png)

**F11 on the XGS 136, after the soak, 23:43-23:50: `mgmt-link.sh coprocessor` while the console is
held** ([1010](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1010)). The coprocessor's end of the
host's link to it without its address - as after F2's third run, and taken away over the link before
each run after the first - and the script started while another session of npu-run.sh held the
console (`images/deploy/rc-l1-test.sh`):

| run | script | the session that held the console | what the script did | the link | the other session |
|---|---|---|---|---|---|
| 1 | main's, md5 `39c52191` | feeder.py's look (the test's own holder could not get the console) | typed its login into that session (`root: command not found`) and its address command after it; said "answers" in 8 s | up, by its command run in feeder.py's shell | logged out by its `exit`; the console left at a password prompt |
| 2 | main's | the test's holder, 25 s | typed the same into the holder's session while it slept; said "does not answer ... yet" and ended in 10 s | down when it ended; up later, when the holder's shell ran what it had queued | read its own line; the console left at a password prompt |
| 3 | the fix prepared for 1010, md5 `52a8850a` | the test's holder, which found run 2's password prompt and gave up | waited for the console, logged in in a session of its own and gave the address there | up in 31 s | - |
| 4 | the fix | the test's holder, 25 s, and then a look of feeder.py | waited through both, then the same | up in 78 s | read its own line, undisturbed |

The console's echo doubles a letter now and then (`ip linkk set` in photos 64 and 65); nothing was
printed between the session's markers, so both of the fix's commands ran without an error. ~~The fix
is not in a pull request yet.~~ It is [1017](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1017),
merged on 2026-11-04 after F11 at boot (below).

![The XGS 136, 23:43, F11 run 1: main's mgmt-link.sh coprocessor while feeder.py's look held the console - its login typed into that session, root: command not found, its address command run there; the test's holder could not get the console](../images/measurements/release-check-2026-11-03/62-136-L1-main-console-held.png)

![The XGS 136, 23:46, F11 run 2: main's script while the test's holder held the console - it ended in 10 s with the coprocessor not answering, and its typing ran later in the holder's shell, leaving the console at a password prompt](../images/measurements/release-check-2026-11-03/63-136-L1-main-session-held.png)

![The XGS 136, 23:47, F11 run 3: the fix's script, md5 52a8850a, with no blind writer - the holder found the password prompt and gave up; the fix logged in in its own session and the coprocessor answered in 31 s](../images/measurements/release-check-2026-11-03/64-136-L1-fix-session-held.png)

![The XGS 136, 23:48, F11 run 4: the fix's script while the holder held the console for 25 s and a look of feeder.py came after it - it waited through both, the holder read its own line undisturbed, and the coprocessor answered in 78 s](../images/measurements/release-check-2026-11-03/65-136-L1-fix-session-held-2.png)

**F8, F3, F4, F7, F5 and F6 on the second XGS 3300.**

![The second XGS 3300, octep e109669f: test-detach-refusal.sh and test-stop-writes.sh under a stream, both exit 0](../images/measurements/release-check-2026-11-03/41-second-3300-F8-releasing-checks.png)

![The second XGS 3300, F3 run 1, 15:28: quiesced 39 s after the stop, SysRq sent after it, then the host froze in the recovery; the photo ends while the host was down - it booted again at 15:34:35, reset by the hardware watchdog (1004)](../images/measurements/release-check-2026-11-03/42-second-3300-F3-stop-1.png)

![The second XGS 3300, F3 run 2, 15:39: no quiesce before the coprocessor's reset, and the host froze](../images/measurements/release-check-2026-11-03/42-second-3300-F3-stop-2.png)

![The second XGS 3300, F3 run 3, 15:48: quiesced 39 s after the stop, taken up in place in 172 s; its "booted again: YES" is false - the boot time moved by one second, a step of the clock](../images/measurements/release-check-2026-11-03/42-second-3300-F3-stop-3.png)

![The second XGS 3300, 17:31 and 17:38, the two F3 runs with 1008's feeder.py (sha256 1843b3a4) as the run's script logged them: quiesced at 32 s of silence, the coprocessor's kernel not asked for its state, taken up in place in 176 and 178 s, booted again: no](../images/measurements/release-check-2026-11-03/50-second-3300-F3-the-fix-two-runs-logged.png)

![The second XGS 3300, F3b with main's feeder.py (md5 7c1a4728, sha256 7a3ac402), 16:24-16:30: its coprocessor's sshd stopped and a look taking 13.0 s, then the stop, sent over the console - which left the console answering; no quiesce, and the host froze](../images/measurements/release-check-2026-11-03/45-second-3300-F3b-O1-candidate.png)

![The second XGS 3300, F3b with 1008's feeder.py as it was then (md5 4cc9303d, sha256 28b2edbe, the same logic), 16:40-16:46: the same; its watch asked the console, was answered and left the coprocessor, and the host froze at the reset of its stopped watchdog feeder](../images/measurements/release-check-2026-11-03/46-second-3300-F3b-O1-the-fix.png)

**F3b again, 2026-11-04, after the four fixes merged** ([1012](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1012)).
The second XGS 3300 on main `cb0eb13` (octep `71a6c3f0`, feeder.py `b09fdad5`); its coprocessor's sshd
stopped, a look then taking 13.1 s (run 1) and 27.4 s (run 2), and after 240 s the stop, `nohup`'d so
that it waited 30 s past the session that sent it: the watchdog feeder stopped, the link down, and every
getty stopped once a second from then on. Run 2's half-second logger also wrote the link's echo and who
held the console:

| UTC | the link | the console |
|---|---|---|
| 00:54:46 | answers | feeder.py's look has it, after its npu-run.sh tried ssh for 10 s |
| between 00:54:46 and 00:54:57 | stops answering | the look's session |
| 00:55:08 | - | that session ends, having read the watchdog feeder `T (stopped)`; health.py's next |
| 00:55:15-00:55:28 | - | the look's second session, to continue the feeder: it waits, then finds no Linux prompt |
| 00:55:28 | - | the look ends, 57 s after it began; nobody holds the console after it |
| 00:55:53 | - | the logger's last line, heartbeat alive and gone 0: the host froze at the coprocessor's reset |

Both runs: reset reason bit 25, the host's hardware watchdog. A look runs in the watch's own loop - no echo
goes out and the link is not looked at while it lasts - and its time is then lent to the link, so the
silence counted only from the look's end and its decision fell due about 00:55:58, after the reset.

![The second XGS 3300, F3b again run 1, 00:33-00:45 UTC: sshd stopped and a look taking 13.1 s, the stop sent at 00:37:59, the host lost at 00:39:50 and booted again; the run's own report ends in a syntax error - its script was edited while it ran](../images/measurements/release-check-2026-11-03/72-second-3300-F3b-late-1.png)

![The second XGS 3300, F3b again run 2, 00:49-01:01 UTC: a look taking 27.4 s, the stop sent at 00:54:02, no quiesce - gone 0 at the logger's last line - and the host booted again](../images/measurements/release-check-2026-11-03/73-second-3300-F3b-late-2.png)

![The second XGS 3300, F3b again run 2's timeline: the link's echo and the console's holder at each change, the console's sessions - the look reading T (stopped) at 00:55:08 - feeder.py's lines and the reset reason](../images/measurements/release-check-2026-11-03/74-second-3300-F3b-late-2-timeline.png)

**F12, [1002](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1002)'s case, 2026-11-04.** On main `cb0eb13`, the
watch steady first: the coprocessor's watchdog feeder stopped and its end of the link taken down, sent over
the link while its sshd answered, its console's getty left running - what F3b's first runs turned out to
be. Then the same with the fix prepared for 1002 put in place of main's feeder.py for the run: a look's
console session, and the watch's own console ask, continue a watchdog feeder found stopped in the session
that found it, instead of a second session.

| feeder.py | the link silent | the watch asks the console | the stopped feeder | the host |
|---|---|---|---|---|
| main's `b09fdad5` | 01:09:32 | 01:10:04, answered "alive": left as it is | the next look has the console at 01:10:23; the coprocessor resets at about 01:10:26 | froze, bit 25 |
| the fix, `a8e42b96`, run 1 | 01:20:40 | 01:21:12; answered "alive" and continued the feeder | continued about 45 s after the stop; the next looks read it sleeping | lived |
| the fix, run 2 | 01:32:14 | 01:32:46; the same | the same | lived |

The fix is not in a pull request yet. Where the stop falls inside a slow look, the watch's ask comes too late
for it as well ([1012](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1012)).

![The second XGS 3300, F12 with main's feeder.py b09fdad5, 01:07-01:17: the watchdog feeder stopped and the link down, no quiesce - the console answered - and the host booted again](../images/measurements/release-check-2026-11-03/75-second-3300-1002-main.png)

![The second XGS 3300, F12 with main's feeder.py, its timeline: the watch's ask from 36.1 s answered alive, the next look's session from 54.6 s, the logger's last line at 58.4 s](../images/measurements/release-check-2026-11-03/77-second-3300-1002-main-timeline.png)

![The second XGS 3300, F12 with the fix prepared for 1002 (feeder.py a8e42b96), run 1, 01:18-01:29: left as it is and the watchdog feeder continued over its console at 01:21:23; the host up throughout](../images/measurements/release-check-2026-11-03/76-second-3300-1002-fix-1.png)

![The second XGS 3300, F12 with the fix, run 1's first 100 s: the link silent from 4.3 s, the watch's ask from 36.1 s, the next looks reading the feeder sleeping](../images/measurements/release-check-2026-11-03/78-second-3300-1002-fix-1-timeline.png)

![The second XGS 3300, F12 with the fix, run 2, 01:29-01:40: the same, the feeder continued at 01:32:57; the host up throughout](../images/measurements/release-check-2026-11-03/79-second-3300-1002-fix-2.png)

![The second XGS 3300, F12 with the fix, run 2's first 100 s: the link silent from 4.3 s, the watch's ask from 35.9 s, the next looks reading the feeder sleeping](../images/measurements/release-check-2026-11-03/80-second-3300-1002-fix-2-timeline.png)

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

**After the review, 2026-11-04: its fixes on the appliances.** Each module was built by the appliance's
own update from its branch. Each header now says whether the file at the loaded module's path was in
place before the boot: a file put there after the boot is not the module that runs, and two headers
below got that wrong (see the corrections).

*F1 with [1015](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1015)'s fence, on the XGS 136.*

![The XGS 136, 03:00-03:07: three reloads under F1's loads with 1015's npuep 147248cf - every one lived, reload.sh exit 0, devbuf 2,389 allocations and 551,610,352 bytes before and after each. The module run 1 unloaded was 1007's 4780b80e, loaded at that boot, though the header names only 147248cf; the paths on the PC in the header are masked](../images/measurements/release-check-2026-11-03/83-136-F1-fence-three-reloads.png)

*F13, F14 and F8 on the first XGS 3300, with [1016](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1016)'s octep.*

![The first XGS 3300, 02:58: main's octep 71a6c3f0 - devctl detach octep0 under four loops connecting UDP over IPv6: exit 0 in 5 s, 4,829,114 connects, the host up. The header's "octep loaded 4adb8488" is the file put on the disk for the next boot, not the module that ran; the kernel line is cut at 170 characters, and the figure it loses, the longest a ring took to stop, was 0 us](../images/measurements/release-check-2026-11-03/81-first-3300-994-octep-main.png)

![The first XGS 3300, 03:02: 1016's octep 4adb8488, loaded at this boot - the same detach: exit 0 in 6 s, 5,673,744 connects, the host up; the longest a ring took to stop, cut off here, was 0 us](../images/measurements/release-check-2026-11-03/82-first-3300-994-octep-fix.png)

![The first XGS 3300, 03:06, F14: the coprocessor quiesced first - gone 1, its heartbeat alive - then the same detach: exit 0 in 5 s, 5,332,668 connects, the host up. Its section for the kernel's lines came back empty; the next photo has them from the system log](../images/measurements/release-check-2026-11-03/84-first-3300-962-octep-fix-quiesced.png)

![The first XGS 3300, F14's kernel lines from the system log, 03:06:24-03:06:39: taken as gone, the management link down with its buffers kept, ring 1 down, the RPC's buffers kept, detached. The lines are cut at 194 characters; the longest a ring took to stop was 0 us](../images/measurements/release-check-2026-11-03/85-first-3300-962-octep-fix-kernel-lines.png)

![The first XGS 3300, 03:10, the boot after F14, source ad48cca: verify.sh - the chain intact, 8 passed, 0 failed, 1 worth a look (no ARMADA endpoint, as on every XGS 3300), exit 0](../images/measurements/release-check-2026-11-03/86-first-3300-octep-fix-verify.png)

![The same boot: the 14 data sources of the XGS pages, each answering in a second or less](../images/measurements/release-check-2026-11-03/87-first-3300-octep-fix-pages-data.png)

![The same boot, 03:10-03:13: the PC downloading from an iperf3 server on the first XGS 3300 through the second's translation - one stream 2,145, 2,094 and 3,663 Mbit/s, four streams 9,733, 9,849 and 9,750 Mbit/s; afterwards gone 0 and no receive watchdog stall on either box. Its 3 kernel lines naming a timeout are one line of octep giving the board's microflow timeout, once for each of the three boots its message buffer kept](../images/measurements/release-check-2026-11-03/88-first-3300-octep-fix-traffic.png)

![The first XGS 3300, 03:17-03:20, 1016's octep 4adb8488 loaded at this boot: RELEASING.md's checks - test-detach-refusal.sh refused if_del, stop, rx_quiesce and kldunload while a ring was held, exit 0; test-stop-writes.sh under a stream toward the box, 4,564 words written in the second before the rings stopped and 0 in the second after, exit 0; 12 front ports and the datapath up after the restart](../images/measurements/release-check-2026-11-03/89-first-3300-octep-fix-releasing-checks.png)

*F11 at boot, on all three.* The bootup hook's `mgmt-link.sh coprocessor`, two restarts of each box,
the link's answer counted from the boot by the plugin's own log line (`images/deploy/rc-l1-boots.sh`):

| box | main's, one boot | `c33cd5f5` | `a84cf06c`, merged as 1017 |
|---|---|---|---|
| first XGS 3300 | +119 s | +128, +128 s | +184, +210 s |
| second XGS 3300 | +151 s | +159, +159 s | +147, +263 s |
| XGS 136 | +77 s, a blind write without the console's lock | +98, +99 s | +87, +86 s |

`c33cd5f5` waited the whole 15 s after an address that went through before it asked again; `a84cf06c`
asks after 3 s. On the XGS 136 that brought the answer 11-13 s sooner. On the XGS 3300s it did not: three
of their four boots were slower, and the console's log says why. In each of those three, a look of
feeder.py reached the console before the address, the start hook's `switch-jumbo.sh` followed it with
four sessions back to back - about 70 s, over the console since the link was not up - and the address
came after them. Where the address was the first session, the link answered soon and `switch-jumbo.sh`
used it. main's script on the XGS 3300s waits in the same queue. Every boot got its link; the order is
[1018](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1018).

![The first XGS 3300, 03:24-03:30, mgmt-link.sh c33cd5f5: two restarts, oxmgmt0 up 96 and 95 s after the boot, the coprocessor answering at +128 s both times](../images/measurements/release-check-2026-11-03/90-first-3300-1010-boots.png)

![The second XGS 3300, 03:25-03:32, c33cd5f5: oxmgmt0 up at +127 and +126 s, the coprocessor answering at +159 s both times](../images/measurements/release-check-2026-11-03/91-second-3300-1010-boots.png)

![The XGS 136, 03:25-03:30, c33cd5f5: the coprocessor answering at +98 and +99 s](../images/measurements/release-check-2026-11-03/92-136-1010-boots.png)

![The first XGS 3300, 03:33-03:41, a84cf06c, merged as 1017: oxmgmt0 up at +95 s, the coprocessor answering at +184 and +210 s - in both, a look of feeder.py and switch-jumbo.sh had the console first](../images/measurements/release-check-2026-11-03/93-first-3300-1010-boots-3s.png)

![The second XGS 3300, 03:33-03:41, a84cf06c: the coprocessor answering at +147 s with the address first, and at +263 s with a look of feeder.py first](../images/measurements/release-check-2026-11-03/94-second-3300-1010-boots-3s.png)

![The XGS 136, 03:33-03:38, a84cf06c: the coprocessor answering at +87 and +86 s](../images/measurements/release-check-2026-11-03/95-136-1010-boots-3s.png)

*An update's failure in the problem reports ([971](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/971)),
on all three.* The installed reporter.py of main and [1020](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1020)'s
side by side on each box, each with its own state and its own files, nothing sent
(`images/deploy/rc-971.sh`):

![All three lab boxes, 08:50: one failed download read by main's reporter.py and by 1020's - main counted it 3 times in three collects and 4 after a second attempt, its title without the reason; the fix counted 1 and then 2, its title ending "[Errno 51] Network is unreachable", first seen at the failure's own time](../images/measurements/release-check-2026-11-03/96-971-reporter-three-boxes.png)

*The watch over the coprocessor, [1021](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1021), on all
three.* Its feeder.py (`44b17f7e`) and npu-run.sh (`1aaed7e5`) installed as an update installs them, before
the merge; the stops are `images/deploy/lab-768-silent.sh`'s, as in F3, F12 and F3b above:

| run | box | runs | result |
|---|---|---|---|
| installed and restarted | all three | 1 each | both files in place before the boot; feeder.py with two threads; a look every 20-22 s; the link answering |
| npu-run.sh on the box, no stop | first XGS 3300 | 1 | a fresh console mark refuses at once (75), one from the future or two minutes old does not, the watch's and the recovery's asks pass; a caller queued before the mark is refused and the console's logger let back in; with getty stopped, exit 69 in 9 s |
| a shell left in a continuation prompt, the coprocessor's sshd refusing | first XGS 3300 | 1 + 1 | npu-run.sh's second read, after a Ctrl-C, found the prompt; a look of feeder.py met one at 11:31:34 and went on; nothing said, no quiesce |
| a boot with the coprocessor's sshd refusing for 180 s from its first answer | first XGS 3300 | 1 | no quiesce, nothing said, the host and the coprocessor up |
| #768's plain stop | first XGS 3300 | 2 | 2 of 2: found silent at 19 s and again at 31 s, octep told at 33 s, no SysRq, taken up in place in 170 and 173 s |
| [1012](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1012): sshd stopped, 240 s of slow looks, then the stop (F3b again's) | both XGS 3300s | 4 valid | 4 of 4: octep told at 32-34 s, no SysRq, the host up; taken up in place in 175, 190 and 211 s, and once the appliance restarted - the day's six recoveries used up by the tests |
| [1002](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1002): the feeder stopped, the link down | first XGS 3300 | 2 | continued over the console 24 and 23 s after the stop, no reset; the address given again at 45 s, and in the run within ten minutes of that, 602 s after it |
| 1002 with portstats.py holding the console | second XGS 3300 | 1 + 1 | portstats.py on the console 13:09:10-13:09:37: the watch's ask waited behind it and continued the feeder 44 s after the stop, before the reset; in the first run portstats.py was still trying the link when the watch asked |
| the feeder stopped as soon as the coprocessor's ssh answers after a boot | first XGS 3300 | 1 | continued 8 s later, no reset |
| the feeder stopped, the link down | XGS 136 | 1 | continued over the console at 32 s, no reset; over the next hour feeder.py's memory 21,308 to 21,312 KB, 4.5 s of CPU |
| the userland stopped, the link answering (F6) | first XGS 3300 | 1 | said 32 s after the stop and not acted on; the host froze at the coprocessor's reset, as on main, and came back by its watchdog |

On the XGS 136, the coprocessor's end of the link taken down by the test stayed without carrier after it was
set up again - `<NO-CARRIER,BROADCAST,UP>`, 300 s of `mgmt-link.sh coprocessor` and the host's own down and
up did not bring it back; a restart of the appliance did, the link answering 77 s into the boot
([1022](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1022)).

![The first XGS 3300 after the install, 11:19-11:25: feeder.py 44b17f7e, npu-run.sh 1aaed7e5 and octep 4adb8488 in place before the boot, feeder.py with two threads, looks at +12, +32 and +54 s, the link answering](../images/measurements/release-check-2026-11-03/97-first-3300-w2-l0.png)

![The second XGS 3300 after the install: the same, looks at +13, +34 and +55 s](../images/measurements/release-check-2026-11-03/101-second-3300-w2-l0.png)

![The XGS 136 after the install: the same with npuep 147248cf, looks at +11, +32 and +52 s](../images/measurements/release-check-2026-11-03/102-136-w2-l0.png)

![The first XGS 3300, 11:27-11:32: npu-run.sh on the box - the console's mark, a stopped getty (exit 69), the coprocessor's sshd stopped, a continuation prompt cleared by the Ctrl-C (the console's log shows "> ^C" and then "~#"), and the look of feeder.py that met one; the password prompt could not be made, the coprocessor's root logs in without one](../images/measurements/release-check-2026-11-03/98-first-3300-w2-l9-a.png)

![The first XGS 3300, a boot with its coprocessor's sshd refusing for 180 s from its first answer: no quiesce, gone 0, nothing said](../images/measurements/release-check-2026-11-03/104-first-3300-w2-b.png)

![The first XGS 3300, 11:33: #768's plain stop - found silent at 19 s and again at 31 s, octep told at 33 s, taken up in place in 170 s](../images/measurements/release-check-2026-11-03/99-first-3300-w2-l6-plain-stop-1.png)

![The first XGS 3300, 11:41: the same, taken up in place in 173 s](../images/measurements/release-check-2026-11-03/100-first-3300-w2-l6-plain-stop-2.png)

![The second XGS 3300, 12:00: not a result - the stop, sent over a console the slow looks held, was refused by the console's lock and never sent; nothing happened for 420 s](../images/measurements/release-check-2026-11-03/103-second-3300-w2-l1-1.png)

![The second XGS 3300, 12:21-12:30: 1012's stop again, the stop's script waiting 40 s for the console now - octep told at 33 s; the recovery found six recoveries in 24 hours and restarted the appliance](../images/measurements/release-check-2026-11-03/106-second-3300-w2-l1-1-again.png)

![The second XGS 3300, 12:33-12:45: 1012's stop - octep told at 34 s, taken up in place in 211 s](../images/measurements/release-check-2026-11-03/109-second-3300-w2-l1-2.png)

![The second XGS 3300, 12:45-12:57: the same - octep told at 34 s, taken up in place in 190 s](../images/measurements/release-check-2026-11-03/111-second-3300-w2-l1-3.png)

![The first XGS 3300, 12:59-13:11: the same - octep told at 32 s, taken up in place in 175 s](../images/measurements/release-check-2026-11-03/115-first-3300-w2-l1.png)

![The first XGS 3300, 12:31: 1002's case - left as it is and the feeder continued over the console 24 s after the stop, the address given again at 45 s, the link back](../images/measurements/release-check-2026-11-03/108-first-3300-w2-l3-1002-1.png)

![The first XGS 3300, 12:38: the same within ten minutes of that - the feeder continued at 23 s, the address given again at 12:42:17, 602 s after the first](../images/measurements/release-check-2026-11-03/110-first-3300-w2-l3-1002-2-retell-held.png)

![The second XGS 3300, 12:57: 1002's case with portstats.py started 8 s after the stop - still trying the link when the watch asked, so the watch had the console first; continued at 24 s](../images/measurements/release-check-2026-11-03/114-second-3300-w2-l4-1002-behind-portstats.png)

![The second XGS 3300, 13:09: the same with portstats.py on the console from 13:09:10 to 13:09:37 - the watch waited behind it and continued the feeder at 13:09:47, 44 s after the stop; no reset](../images/measurements/release-check-2026-11-03/116-second-3300-w2-l4-1002-behind-portstats-2.png)

![The first XGS 3300, 12:24-12:31: the coprocessor's watchdog feeder stopped as soon as its ssh answered, 113 s into the host's boot - continued 8 s later, no reset](../images/measurements/release-check-2026-11-03/107-first-3300-w2-l7.png)

![The XGS 136, 12:02: the feeder stopped, the link down - continued over the console at 32 s, no reset; the link stayed down after it (1022)](../images/measurements/release-check-2026-11-03/105-136-w2-l10-feeder.png)

![The XGS 136, 12:45-13:45: feeder.py every five minutes for an hour - one process, two threads, 21,308 to 21,312 KB, 4.5 s of CPU, gone 0, the link answering](../images/measurements/release-check-2026-11-03/112-136-w2-hour.png)

![The first XGS 3300, 12:49: the userland stopped, the link answering - said and not acted on 32 s after the stop; the host froze at the coprocessor's reset and came back by its watchdog, as on main](../images/measurements/release-check-2026-11-03/113-first-3300-w2-l5-f6-report-only.png)

## 5. The soak

5 h 42 min of load on the candidate, all three appliances at once, from 17:47:46 to 23:30:33: the XGS
136 carrying one download stream at a time, 20 s a pass; three iperf3 loops from the first XGS 3300
through the second's translation and fast path; every ten minutes, each box's uptime, kernel memory of
type devbuf, the mbufs in use, feeder.py's resident memory, the driver's own counters and pf's states.
The XGS 136 came to it with its link to its coprocessor down since F2's third run
([1010](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1010)), so every reader of its coprocessor
used the console throughout.

| | part 1, 17:47:46-20:28:47 | part 2, 20:29:18-23:30:33 |
|---|---|---|
| the XGS 136's streams came from | the PC's card on its Port3, the bench link of [1006](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1006) | the PC's card on its F1 |
| the XGS 136, IPv4 download, Mbit/s | 115 [73.9-253], 407 - that link's rate, not the 136's | 937 [844-949], 456 |
| the XGS 136, IPv6 download, Mbit/s | 115 [69.7-210], 407 - the same | not run: it has no path but Port3 |
| the three loops through the second XGS 3300, Mbit/s | 3,337, 3,188 and 3,305 [1,161-5,147], 1,219 | 3,365, 3,424 and 3,276 [1,675-5,029], 1,365 |
| passes that failed | 0 of 2,033 | 0 of 1,821 |

The median pass of each hour stayed between 3,211 and 3,409 Mbit/s for the loops through the second XGS
3300 in both parts, and at 936-937 Mbit/s for the XGS 136 on F1. At the boxes, in 35 looks each:

- None restarted: each one's uptime grew 20,523 s between its first look and its last, 20,523 s apart.
- Kernel memory of type devbuf was the same at every look, to the byte: 6,629 allocations and
  560,351,632 bytes on the XGS 136, 434 and 989,256,224 on the second XGS 3300, 442 and 989,258,112 on
  the first.
- The mbufs in use moved with the load and did not climb: 595 at every look on the XGS 136; 8,470, 9,017
  and 8,827 on the second XGS 3300 at the first look, at 20:29 and at the last; 14,009, 13,738 and
  12,438 on the first.
- feeder.py's resident memory: 20,568 KiB at the first look and 20,584 KiB at the last on the XGS 136,
  20,572 and 20,600 KiB on the second XGS 3300, 20,532 KiB at every look on the first. Its CPU time was
  not read (corrections, below).
- `gone` was 0 at every look on all three, `quiesce` 0 on both XGS 3300s, and the XGS 136's heartbeat
  alive.
- On the second XGS 3300, which forwarded the three loops, `rxwd_stalls` - rings left with packets in
  them, the counter that should not move - moved: 0 at the first look, 24 at 20:29, 53 at the last. The
  receive watchdog found and emptied each one, and no pass failed
  ([361](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361), open). On the first XGS 3300 it
  stayed 0.
- Each box's system log from 17:47 to 23:31 (photo 61) holds no kernel line naming a panic, an error, a
  failure, a timeout, a watchdog or a stall. The second XGS 3300 logged 2,477 kernel lines, 2,452 of
  them its probe taking out connections that had ended and 12 on IPsec associations; the first 13, 12 of
  them on IPsec associations; the XGS 136 9: three of its driver's RPC at 17:51:14, and Port3's carrier
  down at 21:39:20 and up at 21:39:23, the PC's card on that link logging the same at 21:39:17 and
  21:39:21. In part 2 that link carried only the looks' ssh; the 136's stream on F1 ran at 906 Mbit/s in
  the pass across the drop. The PC's card on Port3 had counted receive errors on 1.8 % of its frames
  since it started, 3,427,919 of 188,425,228; the card on F1 none in 5,389,608,863.

![The soak in its two parts - the XGS 136's streams from Port3, then from F1 - and the first, middle and last looks at each box](../images/measurements/release-check-2026-11-03/60-soak-two-parts.png)

![What the three kernels logged in the soak's hours, and the PC's own record of its link on the XGS 136's Port3](../images/measurements/release-check-2026-11-03/61-soak-kernel-lines-and-port3.png)

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
  Fixed in [1016](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1016) on 2026-11-04: the device asked
  once, its buffers kept while it holds them - run as F14 (section 4).
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
  first run was that, not a step reload.sh lacks.) Since
  [1017](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/1017) the address goes through npu-run.sh under
  the console's lock and is told again while the link does not answer, for about five minutes.
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

- The XGS 136's streams ran from the PC's card on its Port3, a bench link with a record of faults - its
  carrier flapped through 2026-10-25, it came up at 100 Mbit/s on 2026-10-27 - and it was used all the
  same. Its download figures measure that link's state in each boot, not the 136
  ([1006](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1006)); from 20:29 the soak's stream ran
  from the PC's card on the 136's F1.
- The soak's looks read feeder.py's resident memory, not its CPU time as planned: FreeBSD's `ps` takes
  `-o rss=,time=` as rss alone, under the header `,time=`. And their count of kernel messages naming a
  panic or an error fell on the second XGS 3300 from 7 to 0 as its message buffer filled with the
  probe's lines, so it says nothing; each box's system log for the soak's hours was read instead (photo
  61).
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
  command ran; that ended about 33 s later. ~~F3b is to be run again with the stop landing after its own
  session.~~ Run again so on 2026-11-04 (F3b again, section 4).
- F3b again's first run lost its own report: the script it ran was edited while it ran, and the shell read
  the rest of it at a shifted place ("Syntax error", photo 72). Its facts - no session on the console
  after the look of 00:39:00, gone 0 at the logger's last line, bit 25 - were read afterwards from the
  console's log, the logger's file and the boot's reset reason.
- Photo 22's dump lines, after the fix's result, are the candidate's dump of 11:48.
- feeder.py is named by its md5 in some photos and by its sha256 in the stop script's header: main's is
  `7c1a4728` and `7a3ac402`, 1008's `b09fdad5` and `1843b3a4`.
- The watcher of the console's lock in the two F3 runs with the fix showed only itself (section 4).
- The XGS 136's link to its coprocessor stayed down from F2's third run
  ([1010](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/1010)) through the soak, until F11's first
  run gave it its address at 23:43.
- Photo 81's header names `4adb8488` as the octep loaded: that was the file put on the disk for the next
  boot; the module that ran was main's `71a6c3f0`, loaded at the boot of 00:05. Photo 83 names only
  `147248cf`: its run 1 unloaded 1007's `4780b80e`, loaded at that boot, and loaded the fence's, so the
  fence's own detach ran in runs 2 and 3. The scripts now say whether the file was in place before the
  boot.
- F8 on the first XGS 3300 first ran with its stream the wrong way, the box sending, so no frame reached its
  receive rings and `test-stop-writes.sh` said, rightly, that it proved nothing (exit 1); it was run again
  with the stream toward the box (photo 89).
- Photos 81, 82 and 85 cut octep's kernel lines at 170 and 194 characters; the figure they lose, the
  longest a ring took to stop, was 0 us in all three, in the system log.
- The watch's first run of 1012's stop (photo 103) is not a result: its stop, sent over a console the slow
  looks held, waited the console's usual 5 s, was refused, and was never sent. The stop's script waits 40 s
  for the console now, and a run whose stop was not sent ends there and says so.
- Photos 103, 106, 109, 111 and 115 print, after feeder.py's md5, the old test script's label for the lent
  time - "the candidate: a look's time counted as a gap" - which describes neither 1021's feeder.py nor its
  runs, and a line "feeder.py said meanwhile" that holds kernel lines, the script's pattern matching them;
  feeder.py's own lines are the ones under "feeder.py and the recovery".
- The appliance restart in photo 106 was the recovery's limit of six in 24 hours, used up by the day's
  tests; fastpath.py's counts were emptied for the runs after it, as for the review.
