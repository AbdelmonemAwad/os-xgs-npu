<!-- reviewed: 2026-10-21 -->
# Problem reports

Written 2026-10-17, when the plugin gained a problem reporter, and the day the XGS 3300 restarted
twice with nothing in its logs to say why.

An appliance in somebody else's rack fails where nobody from the project can see it. The problem
reporter writes down what went wrong, takes out of it everything that identifies the machine or its
networks, and files it as a GitHub issue in the repository of the part it belongs to, so that it can be
fixed. It works like OPNsense's own crash reporter (System: Firmware: Reporter), and beside it: that one
sends OPNsense's crashes to OPNsense, this one sends the coprocessor's and the appliance plugins'
problems to this project.

| | |
|---|---|
| the page | XGS: Problem Reports (`/ui/xgs/reports`) |
| the script | `src/opnsense/scripts/xgs/reporter.py` |
| when it looks | at boot (`rc.syshook.d/start/65-xgs-report`) and every five minutes (`xgs_cron()`) |
| where it keeps its queue | `/var/db/os-xgs-npu/reports` |
| where reports go | the project's relay, `https://reports.npuforge.org/v1/report` ([relay/README.md](../relay/README.md)) |
| the tests | `tools/test-reporter.py`, `node relay/test.mjs` - both run by the checks |

## The owner decides

Three modes, chosen on the page:

- **Automatic**, the default: each report is sent as it is found - a new problem at once, the same
  problem again at most once a day.
- **Manual**: each report waits in the list until its **Send** is pressed. A waiting report is a notice
  in OPNsense's status, in the header.
- **Off**: nothing is looked at and nothing is sent.

Automatic is the default by the project's decision, and it is never left for somebody to discover:
until the owner has applied a choice on the page or closed it, ~~a banner on every page of the GUI~~ a
notice in OPNsense's status, in the header, says that reports are sent and where to change it. The
notice opens the page, and its X closes it for good; a banner could do neither (2026-10-19). **View** on any report shows exactly what is sent, and **Report
a problem** writes one by hand, which goes the same way.

Nobody needs a GitHub account. The relay files the issue as the project's GitHub App; an optional
contact address goes to the relay with the report and is kept beside the issue's number, never written
into the issue.

## What is looked at

Only the parts that are installed, each one's reports going to its own repository:

| part | its programs in the logs | its paths |
|---|---|---|
| os-xgs-npu | `octep`, `octep-follow`, `octep-stop`, `npuep`, `npuctl`, `xgs-assign`, `xgs-switch`, and the kernel lines of `octep*`, `npuep*`, `oxp*`, `npup*`, `mvmgmt*` | `OPNsense/XGS`, `scripts/xgs`, `scripts/octep`, `scripts/npuctl` |
| os-fanctl | `fanctl` | `OPNsense/FanCtl`, `scripts/fanctl` |
| os-frontpanel | `frontpanel`, `LCDd` | `OPNsense/FrontPanel`, `scripts/frontpanel` |
| os-linkhealth | `linkhealth` | `OPNsense/LinkHealth`, `scripts/linkhealth` |
| os-netreport | `netreport` | `OPNsense/NetReport`, `scripts/netreport` |

| kind | found in | is a problem when |
|---|---|---|
| logged error | the system log, `/var/log/xgs`, configd's log | a line of severity error or worse from one of the parts' programs, or a configd action of one of them that failed; lines one program logs within three seconds of each other are one report, its first line the title and every line its evidence - since 2026-10-19, when the kernel follower's four-line account of one failed build became four issues ([#509](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/509)) |
| driver | the same logs, kernel lines | a line of one of the coprocessor's devices that says something failed, timed out, was refused or did not complete - `device_printf` carries no severity, so the words are what there is |
| page error | PHP's error log | an error in one of the parts' pages or scripts |
| driver missing | `board.sh`, `sysctl`; for the evidence the kernel's lines, `/boot/modules`, `kldstat` and `/var/db/os-xgs-npu/install-build.log` | the board has a coprocessor and no driver is attached ten minutes after boot. Since 2026-10-21 the report also says whether the module is installed, which kernel it was built for and whether it is loaded, and carries the last twelve lines of the installer's build of it - the kernel's lines alone said nothing on an XGS 126 whose module was never built, and why was in the build log ([#625](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/625), [#657](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/657)) |
| driver not ready | `dev.octep.0.ready` | attached, and the handshake not done ten minutes after boot |
| fast path stopped | `dev.octep.0.heartbeat.*`, `dev.npuep.0.heartbeat.*`; for the evidence this boot's `heartbeat:` lines of the kernel | the coprocessor's fast path went 30 seconds without answering its heartbeat at least once since boot - whether or not it answers again ([the heartbeat](measurements/heartbeat-2026-10-22.md)). When XGS: Settings says to restart the appliance for it, the report is collected before the restart, so it goes with the evidence of the boot it happened in |
| module and kernel | `/boot/modules/*.ko.kernel`, `kern.version` | the driver module was built for another kernel than the running one: the kernel follower did not rebuild it |
| update | `/var/db/os-xgs-npu/update/apply.json` | an update from XGS: Updates failed |
| fan incident | `/var/db/fanctl/incident.json` | fan control recorded an incident |
| kernel panic | `/var/crash/info.N`, `textdump.tar.N` | a crash dump was saved; the panic string, the backtrace and the end of the message buffer go into the report. Two crashes are the same problem when the panic and the first frames **after** the debugger's, the panic's, the trap's and the lock code's are the same - ~~the first three frames~~, which are the same for every page fault and filed two different crashes as one issue (#467, corrected 2026-10-18). `info.last`, savecore's link to the newest dump, is not a dump of its own. Nothing in `/var/crash` is removed: it is OPNsense's crash reporter's |
| restart without shutdown | at boot | the boot before this one ended without an orderly shutdown - see below |

The logs are read from where the last look stopped; the first look after installing reads none of what
was there before.

### A restart without a shutdown

`rc.reboot`, `rc.halt` and `rc.shutdown` all run `rc.syshook stop`, and `stop/90-xgs-report` writes the
time down there. At boot, a boot before this one that left no such line, and no `shutdown` record in
the login accounting (`reboot(8)` at a shell bypasses the hook but not that), did not shut down. The
report says what is known about why:

- a crash dump on the dump device (`savecore -C`): the kernel panicked, and the dump is reported on its
  own once savecore has saved it;
- no dump, and the message buffer kept the boot before: a reset without a loss of power - a watchdog,
  a reset line or a button;
- no dump, the message buffer cleared, and a dump device configured: memory was cleared - a loss of
  power or a reset by the hardware; a panic would have written a dump;

and the last twenty lines the system log had before the boot.

**What the host's chipset recorded** goes in too, since 2026-10-18. The AMD FCH beside the host's
processor keeps the cause of the last reset in `FCH::PM::S5_RESET_STATUS` - power-management register
0xC0, on family 17h and later - one bit each for the watchdog, a sync flood after an uncorrected
error, a thermal trip, the reset pin, the power button, a software reset through port 0xCF9 and
others. The start hook `06-xgs-reset-reason` reads it early in every boot
(`scripts/xgs/reset_reason.py`), logs it under `xgs-reset-reason`, keeps it in
`/var/db/os-xgs-npu/reset-reason.json` and writes it back, which clears it for the next boot. Its
named bits go into the report's fingerprint, so a watchdog reset and a sync flood are two problems,
not one. The support bundle carries it as `reset-reason.json`.

Why it was needed: the XGS 3300 reset itself on 2026-10-17 under a minute after its last log line,
with the watchdog armed at 128 s, no dump and no machine-check record, and nothing on the appliance
could say what had reset it (#454). Read by hand the next day it held `0x00080800`: bit 19, software
wrote 0x6 to port 0xCF9 - an ordinary reboot had ended that way - and bit 11, which has no published
meaning. Nothing of the reset before; several boots had passed. Written back, bit 19 cleared and bit
11 stayed: bit 11 is a state of this chipset, not a cause, and is left out of the fingerprint. ~~On the
XGS 136 it has not been read yet.~~ Through the hook itself, at the boots after the update to
v0.1.0-alpha.1.1 on 2026-10-18 (an ordinary reboot each), the first XGS 3300 and the XGS 136 both kept
`0x00080800`, and both had `0x00000800` left after clearing: the 136's host has the register, and
bit 11 stays there too.

This is what was established by hand on 2026-10-17 on the XGS 3300, which restarted twice within ten
minutes: no reboot was requested (the audit log has three that day, none then), there was no shutdown
record, `savecore -C /dev/ada0p3` found no dump on a configured dump device, and the message buffer
held one boot. The machine lost power or was reset by its hardware; it did not panic.

## What leaves the machine

The text **View** shows, and nothing else: built on the appliance, cleaned there, posted as it is.

Every report carries the appliance block: the model (`smbios.system.*`), the board's assembly number,
vendor and family, the **board revision on a line of its own** (with SMBIOS's value beside it when the
two disagree), the BIOS, the processor and memory, the host's network controllers and the coprocessor
as PCI names them (device, ids, revision), the coprocessor's device and switch chip and the driver's
description, the expansion module in each bay with its revision and vendor, OPNsense's version, the
kernel and its build, whether the driver module was built for that kernel, the installed commit of this
plugin and of each appliance plugin, the plugin's files changed by hand since it was installed (by name,
against the digests `install.sh` writes to `share/os-xgs-npu/manifest.sha256`), and the uptime. The
appliance itself is named by twelve hex digits of a hash of its board serial and UUID, which cannot be
turned back into either.

The board's revision, its vendor and the management port's chip are there because they are what told
two XGS 3300 apart when it mattered: the first is board revision 17 from vendor NEA with an I210 on its
management port, the second revision 09 from LAA with an I211, and os-linkhealth, which knew the first
by its revision, drew no faceplate for the second until it matched the model (`smbios.system.version`)
as well. A report from a third board says at once which of these it shares. The vendor is the first
three letters of the board's serial, or the coprocessor's own record of it; the rest of the serial is
never read into a report.

Taken out of every report before it is even stored: IPv4 and IPv6 addresses (loopback, unspecified and
link-local ones say nothing about anybody and stay), MAC addresses, e-mail addresses, the SMBIOS
serials and UUID, the coprocessor's serials, the host name and the domain, key blocks, key fingerprints,
and any long run of hex digits.

At most five new reports and twenty repeats a day leave one appliance, and the relay holds each
appliance to twenty and itself to five hundred; the owner's own Send is never refused.

## For a test

One line in `/usr/local/etc/os-xgs-npu/report-relay` sends the reports to another relay, `https://`
only. `reporter.py collect`, `list`, `show <id>`, `send <id>` and `system` work at a shell as on the
page.

## Tested

On 2026-10-17, on the XGS 3300:

- `reporter.py` run from `/tmp` against the day's whole logs, sending nothing (`XGS_REPORT_NO_SEND`),
  with the boot before the last one given as the one to judge: it found the 08:52 restart as a
  restart without a shutdown, "lost power or was reset by its hardware", the appliance block complete
  (model, board AMDA0202-0004 rev 09, CN8365 and 88E6193X, bay A: AMDA0207-0001 rev 02, the module
  built for the running kernel, every plugin's commit) and every address and key fingerprint taken
  out. It also took octep's "NPU not ready yet", which it prints while it waits at every boot, for a
  failure - twelve times in eight days of logs - so "not ready" is no longer one of the words; after
  that, the day's logs gave nothing.
- Installed by hand (the reporter's files only; the driver module untouched): the banner on every
  page, the page and its menu entry, the form saving the reporting section into config.xml, Apply
  taking the banner away, a report written by hand, its View with the address in it replaced, its
  Send failing plainly ("the relay could not be reached") while the relay does not exist yet, its
  Dismiss, the cron entry in the crontab, and `configctl xgs report collect`.

Not yet: the relay itself (it waits for the GitHub App and the Worker, [relay/README.md](../relay/README.md)),
a boot with the start and stop hooks, and the ARMADA family.

## Counting the appliance

The same page has one more setting, **Count this appliance**, on unless it is turned off. Once a day
(`cron.d/xgs-count`, at a random minute of the hour) ~~`scripts/xgs/count.py` sends~~ - and since
2026-10-20 once at the end of every install or update, so a new appliance is counted the same hour -
`scripts/xgs/count.py` sends npuforge.org three things, which is how the site's home page knows how
many appliances run the project:

- a random identifier the appliance made for itself the first time - 32 hexadecimal digits in
  `/var/db/os-xgs-npu/appliance-id`, nothing read from the hardware;
- its model, as SMBIOS names it (`xgs3300r1`, `xgs136r1`);
- the release installed, or its commit when it is not a release.

`count.py --show` prints exactly what would be sent, and sends nothing. The site counts each
identifier once, keeps no address, and forgets one not heard from for a year; its privacy page says
the rest.

## The support bundle

Since 2026-10-17 the Problem Reports page has **Download a support bundle**: one `.tar.gz` with what
somebody helping with the appliance needs, made on the spot by `scripts/xgs/bundle.py` and handed
straight to the browser - kept nowhere on the appliance and sent nowhere.

| file | what it holds |
|---|---|
| `versions.txt` | OPNsense's version, the kernel, the plugin's installed record |
| `system.json` | the same machine description every report carries (names and versions, no serials) |
| `status.json`, `ports.json`, `transceivers.json`, `ipsec.json`, `updates.json`, `reports.json` | what the coprocessor's pages read, as they read it |
| `watchdog.txt`, `sysctl-driver.txt`, `kldstat.txt` | the hardware watchdog, the driver's sysctl tree and tunables, the loaded modules |
| `dmesg.txt`, `ifconfig.txt`, `log-plugin-lines.txt` | the boot messages, the interfaces, and the plugins' own lines from the system log |

Every file goes through the same cleaner as a report before it is written: addresses, MACs, serial
numbers, the host and domain names, e-mail addresses, keys and fingerprints are replaced. The modules'
own serial numbers are taken out of `transceivers.json` first, because they look like nothing the
cleaner knows. Nothing is read from `/conf` but the two names the cleaner removes.

Made on the XGS 3300 on 2026-10-17: 14 files, 45 KB; searched afterwards for the lab's addresses, any
MAC address and the modules' serial numbers - none found.
