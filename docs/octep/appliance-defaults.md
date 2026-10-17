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
| off | 9000 | 9.88 Gbit/s | 9.59 Gbit/s | [in](../images/powerd/01-off-pc-to-3300.png), [out](../images/powerd/02-off-3300-to-pc.png) |
| off | 9000 | 9.90 Gbit/s | 9.20 Gbit/s | a repeat; meant for MTU 1500, but the PC refused the change without administrator rights |
| off | 1500 | 5.31 Gbit/s | 5.42 Gbit/s | [in](../images/powerd/05-off-pc-to-3300-mtu1500.png), [out](../images/powerd/06-off-3300-to-pc-mtu1500.png) |
| hiadaptive | 1500 | 5.36 Gbit/s | 5.35 Gbit/s | [in](../images/powerd/07-on-pc-to-3300-mtu1500.png), [out](../images/powerd/08-on-3300-to-pc-mtu1500.png) |
| hiadaptive | 9000 | 9.91 Gbit/s | 9.76 Gbit/s | [in](../images/powerd/09-on-pc-to-3300-mtu9000.png), [out](../images/powerd/10-on-3300-to-pc-mtu9000.png) |

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
