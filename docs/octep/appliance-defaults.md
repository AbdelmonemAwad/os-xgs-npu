<!-- reviewed: 2026-10-17 -->
# The appliance in OPNsense's own settings

Written 2026-10-17, from OPNsense's System: Settings: Miscellaneous page read option by option
against these two appliances: what each option means on an XGS 3300 and an XGS 136, what this
plugin sets, and what it leaves alone and why. Nothing of the coprocessor goes into that page - it
is core and calls no plugin hook ([offload-is-a-setting.md](offload-is-a-setting.md)); the
coprocessor's own settings are on its own page ([coprocessor-section.md](coprocessor-section.md)).

| option | on these appliances | what the plugin does |
|---|---|---|
| Cryptography: hardware acceleration | the list offers hifn, padlock, qat and safe, and none of them is in either box; AES-NI is always used | nothing |
| Thermal sensors | both hosts are AMD Family 17h, and `amdtemp` attaches to both | **sets it to amdtemp, once** |
| Power savings: PowerD | off; the CPUs run at their top P-state | nothing - measured below |
| Swap file | both have an 8 GB swap partition already | nothing |
| /var/log and /tmp in memory | ZFS on an SSD with TRIM in both | nothing |
| Startup and shutdown beep | `/dev/speaker` attaches on both | nothing - ~~whether a speaker is fitted is not known yet~~ none is heard on the XGS 3300 |

## Thermal sensors: amdtemp

The two hosts are an AMD Ryzen Embedded V1780B (XGS 3300) and an R1606G (XGS 136), and `amdtemp`
attaches to both as "AMD Family 17h CPU On-Die Thermal Sensors": **38.6 C and 53.6 C** at idle on
2026-10-17. With it set, OPNsense loads the module at every boot - after a reboot of the XGS 3300 the
same day the CPU read 40.2 C with no amdtemp line left in `loader.conf.local` - and the CPU's temperature reaches
its own dashboard; the page shows it as set, and the owner changes it there.

`scripts/xgs/defaults.php` sets it when the plugin is installed, once, and only when nothing is
chosen on the page: an AMD host, the module present, and the module attached after loading it.
Its name is then recorded in `/conf/os-xgs-npu-defaults`, so an owner who sets the page back to
None afterwards is not overridden by the next install or update. Run on both appliances on
2026-10-17: `--check` said what it would set, the run set it, a second run left it.

Before this, the installer loaded amdtemp from a line it wrote in `loader.conf.local`, on the XGS
3300 only, and the Miscellaneous page said None while the module was loaded. The installer now takes
that line out once OPNsense's setting carries the module - on the XGS 3300 only, because the XGS
136's `loader.conf.local` was written by hand and is the owner's.

## PowerD, measured

Off is OPNsense's default, and both appliances run that way: `hwpstate` holds the CPU at its top
P-state, 3350 MHz on the XGS 3300 (levels 3350, 2300, 1600) and 2600 MHz on the XGS 136 (2600,
1700, 1400). PowerD in `hiadaptive` lowers the clock while the CPU is idle and raises it under load.

Measured on the XGS 3300 on 2026-10-17, PowerD started by hand in `hiadaptive` and stopped after,
the configuration untouched. TCP to and from the host itself - iperf3 on the appliance, the PC
across the 10G link to F1, four streams for 15 seconds - so the host's own CPU carries every
packet; at MTU 1500 that is the limit, at 9000 the link is.

| PowerD | MTU | PC to the 3300 | the 3300 to the PC | images |
|---|---|---|---|---|
| off | 9000 | 9.88 Gbit/s | 9.59 Gbit/s | [in](../images/measurements/powerd-2026-10-17/01-off-pc-to-3300.png), [out](../images/measurements/powerd-2026-10-17/02-off-3300-to-pc.png) |
| off | 9000 | 9.90 Gbit/s | 9.20 Gbit/s | a repeat; meant for MTU 1500, but the PC refused the change without administrator rights |
| off | 1500 | 5.31 Gbit/s | 5.42 Gbit/s | [in](../images/measurements/powerd-2026-10-17/05-off-pc-to-3300-mtu1500.png), [out](../images/measurements/powerd-2026-10-17/06-off-3300-to-pc-mtu1500.png) |
| hiadaptive | 1500 | 5.36 Gbit/s | 5.35 Gbit/s | [in](../images/measurements/powerd-2026-10-17/07-on-pc-to-3300-mtu1500.png), [out](../images/measurements/powerd-2026-10-17/08-on-3300-to-pc-mtu1500.png) |
| hiadaptive | 9000 | 9.91 Gbit/s | 9.76 Gbit/s | [in](../images/measurements/powerd-2026-10-17/09-on-pc-to-3300-mtu9000.png), [out](../images/measurements/powerd-2026-10-17/10-on-3300-to-pc-mtu9000.png) |

MTU 1500 was set on the appliance's end of the link, F1, through OPNsense, and put back to 9000
after; the PC then advertised and used a segment of 1460 bytes. A first attempt with iperf3's own
segment size option failed on both ends ("unable to set TCP/SCTP MSS") and measured nothing.

The round trip, 100 pings 0.2 s apart from the PC: **0.977 ms** on average with PowerD on (0.594
to 1.444), **1.023 ms** with it off (0.738 to 1.281).

At idle, sampled every 20 seconds - the CPU's die (amdtemp), and the board's CPU, NPU and inlet
readings from the CPLD:

| PowerD | clock | die | board CPU | NPU | inlet | fans |
|---|---|---|---|---|---|---|
| off | 3350 MHz | 37.6 - 37.7 C | 37 C | 69.6 C | 27 C | 6900 - 7200 rpm |
| hiadaptive, after 4 minutes | 1600 to 3350 MHz | 37.0 - 43.1 C | 37 - 43 C | 68.5 - 69.6 C | 26 - 27 C | 6900 - 7200 rpm |

The clock under PowerD jumps to the top for the sample itself, and the 43 C reading was taken with
the load average still at 0.9 from the run before.

**What this says:** on the XGS 3300 PowerD cost nothing measurable - not throughput where the CPU
is the limit, not the round trip - and bought nothing measurable either: the idle temperatures are
within a degree, and the fans did not slow. What it saves in power was not measured, because there
is no meter on this bench. So the plugin leaves it as OPNsense leaves it, off, and the owner may
turn it on. Not measured on the XGS 136.

## Swap, and the logs in memory

Both appliances have an 8 GB swap partition from the installer (`/dev/ada0p3` on the XGS 3300,
`/dev/gpt/swap0` on the XGS 136), so the page's 2 GB swap file adds nothing. Both run ZFS on an SSD
that takes TRIM - a 256 GB LITEON L8H-256V2G-41 in the XGS 3300, a 120 GB Transcend MTS420S in the
XGS 136 - with `/var/log` and `/tmp` as datasets of their own. Putting either in memory would save
little wear on an SSD and would lose the logs at every reboot, which is when this plugin's boot
hooks write the lines that say what the coprocessor did. Left off.

## The beep

`/dev/speaker` is there on both, but `spkr` attaches to the PC timer that every x86 board has, so
it says nothing about whether a speaker is fitted. The beep at boot and shutdown runs either way;
~~whether it is heard is to be checked by ear. Until then the option is left as it is.~~ Listened for
by its owner through a reboot of the XGS 3300 on 2026-10-17: no beep. With nothing to hear,
the option changes nothing there that anyone would notice, and is left as it is. The XGS 136 has not
been listened to.

## The hardware watchdog

Both hosts carry the AMD FCH watchdog timer, and FreeBSD's `amdsbwd` attaches to it on each:
`amdsbwd0: <AMD FCH Rev 41h+ Watchdog Timer> at iomem 0xfed80b00` on the XGS 3300 (2026-10-12) and
on the XGS 136 (2026-10-17). OPNsense has no page for it and nothing arms it, so until now a host
that stopped scheduling stayed stopped until somebody pulled the power.

The coprocessor's Settings page now has **Hardware watchdog**, on by default since 2026-10-17. `scripts/xgs/watchdog.sh` loads the driver and starts `watchdogd -t 128 -s 1`: the
timer is patted every second and fires after 128 seconds without a pat. The start hook
`08-xgs-watchdog` arms it at the end of every boot, and the stop hook `05-xgs-watchdog` disarms it
first thing at every shutdown - watchdogd disarms the timer only when it is asked to stop, and a
timer left armed through a halt would reset the appliance two minutes after it was told to power
off. Apply on the page arms or disarms it at once.

Measured 2026-10-17: armed, disarmed and armed again by hand on both appliances; then through a
reboot of the XGS 3300 with nothing done by hand - the stop hook ran at 14:24:53, the start hook at
14:27:27, and the timer was armed one second later. That the timer actually resets a host that has
stopped was not tested: it would mean stopping the host on purpose. Whether it did, when a host
restarts without a shutdown, is recorded at the next boot since 2026-10-18: bit 25 of the chipset's
reset record, which the problem report of that restart names (docs/problem-reports.md).

**When the fast path stops**, beside it since 2026-10-22, is the same kind of choice one level down:
the host is running and the coprocessor's fast path is not. The driver asks the fast path every five
seconds and takes it as stopped after 30 seconds without an answer
([the heartbeat](../measurements/heartbeat-2026-10-22.md)); every frame through the front ports goes
through the fast path, so while it is stopped nothing passes, and the host has no connection it could
take back and carry on its own. **"Take it up again, or restart", the default since 0.1.0-alpha.8.6**
(#768): the driver stops reading the coprocessor the moment its fast path is taken as stopped - a read
while the coprocessor resets and boots again never completes, and froze the XGS 3300's host until its
hardware watchdog restarted it - and `scripts/xgs/fastpath.py` has `scripts/xgs/recover.sh` take it
up again in place, the host running: the coprocessor restarted by its own watchdog or, on the ARMADA
family, by the host (on the OCTEON TX family, one whose watchdog has not restarted it is asked to by its
own console - ~~written, not yet seen needed~~ measured on 2026-10-26, see below); the driver brought up
again; OPNsense's interfaces reloaded; the management link given its address again in the background.
Measured before it was the default, a coprocessor hung on purpose (its fast path and its watchdog
feeder stopped): every port back and the heartbeat answered 102 s after the hang on the XGS
136, 190 s on the first XGS 3300, the host never restarted. If that does not bring the fast path back,
the appliance is restarted, as below - ~~which is what an OCTEON TX appliance with IPsec associations on
its coprocessor gets: the module cannot be unloaded once the kernel's ESP transform points into it, so
the second XGS 3300 (vendor version 22, its tunnels offloaded) was restarted cleanly 103 s after the
hang, its host never frozen~~. Until 2026-10-26 an OCTEON TX appliance with IPsec associations on its
coprocessor got that restart: the module cannot be unloaded once the kernel's ESP transform points
into it. Now its device is detached and attached again with the module loaded, and both XGS 3300s were
taken up in place with their tunnels on the coprocessor
([photographed](../measurements/coprocessor-recovery-2026-10-26.md#with-ipsec-associations-on-an-octeon-tx-coprocessor)). "Say so, and nothing more", ~~the default,~~ leaves the driver's lines in
the system log and a problem report (`fast-path-stopped`). "Restart the appliance after 2, 5 or 10
minutes" has `scripts/xgs/fastpath.py`, run every minute, restart it cleanly once the fast path has
gone that long without an answer - the problem report collected first, so it goes out with the boot
it happened in - and never more than three times in 24 hours, so an appliance whose coprocessor does
not come back stays up for somebody to look at. ~~The default is off as the vendor's is~~ Until
0.1.0-alpha.8.6 the default was to say so and nothing more, as the vendor's is: its host panics
the box after 120 seconds without an answer, with that switched off unless asked for. A configuration
saved with that choice keeps it. On the XGS 136
the fast path does not answer the heartbeat message, so ~~it is not asked and nothing here acts~~ since
2026-10-23 the answer there is the message being taken off the host's queue by the fast path - the
same states, the same setting ([measured](../measurements/heartbeat-2026-10-22.md#the-answer-on-this-family-the-beat-taken)).

Measured 2026-10-22 on the second XGS 3300, set to 2 minutes: the fast path's process stopped on the
coprocessor, taken as stopped at 30 seconds, the appliance restarted with 158 seconds without an
answer - the minute's check after the two minutes - its report queued and the heartbeat answering again
after the boot ([the run](../measurements/heartbeat-2026-10-22.md#what-is-done-when-it-stops)).

**The console's reboot, measured 2026-10-26** on the first XGS 3300 (vendor version 23): its fast
path alone stopped and its watchdog feeder left running, so its own watchdog never resets it. The
first four automatic tries failed - three ended in a clean restart of the appliance, the fourth left
it up at the day's limit - and what was found on the way was all on the host's side of the console.
The coprocessor has busybox's `reboot` but no link to it on its path, so the first three were
answered "command not found" (vendor versions 22 and 23 alike; `busybox reboot -f` now). After one of
those restarts the host's UART for that console took no interrupt for the whole boot, and the
console's logger, taking the port again, waited for output that never went out with the console's
lock held, so nothing could use the console (found by hand; the logger no longer waits, and
`npu-run.sh` attaches a UART that takes a byte without an interrupt again, by its ACPI handle). And
the callers that ask the coprocessor every minute fell back to its console with the management link
down, one of them holding the lock when the fourth try asked (only the recovery is answered now while
the driver takes the coprocessor as gone). With the three put right, taken up in place twice by hand,
the host up throughout: the console asked at 120 s, the new boot waiting for the host under a minute
later, the fast path answering again at 279 and 287 s, and the management link back after it. Then
by `fastpath.py` itself, as an appliance meets it: taken as gone 30 s after the stop, the console
asked at 120 s, the fast path answering again 280 s into the recovery, the host never restarted, a
ping through the bridge and one from the second XGS 3300 answered. Both families' recoveries on
0.1.0-alpha.8.7 are [photographed](../measurements/coprocessor-recovery-2026-10-26.md). Its driver (octep.ko `dabcfa58`) no longer writes to a coprocessor it takes as
gone either: the NetAgent's once-a-second "did not go idle" line, 339 of them in one such stop before,
was not written once.

**On the ARMADA family the order of the reset and the unload matters**, measured the same day on the
XGS 136 (vendor version 21). Unloaded with the coprocessor still running, the driver's detach raced
the coprocessor's own watchdog: an unload just after its reset went through, but one its reset came
part-way through froze the host, which its hardware watchdog restarted (the chipset's record, bit 25).
Unloaded with the coprocessor held in reset, the detach kept its config space as it read then - all
ones - for the next load to write back, and that load brought no port up (the appliance restarted
cleanly). `recover.sh` now does what every boot does: the coprocessor reset from the host first, its
config space waited for, and the driver unloaded and loaded while it answers - taken up in place in
88 s and 86 s in two runs, the host up throughout, the management link back seconds after.
