<!-- reviewed: 2026-10-17 -->
# Installing, the pages, updating

> [!NOTE]
> **This page installs the driver by hand**, on an OPNsense you already run, from a FreeBSD shell.
> The whole appliance from one USB stick, with a picture for each step, is
> [Installing, step by step](step-by-step.md).

*Moved here from the [README](../../README.md) on 2026-10-18, word for word, when the README was
made shorter. Struck claims stay struck, with what replaced them beside them; links were only
re-pointed to work from this folder.*

## 📦 Installing

> [!IMPORTANT]
> **One installer, and it decides the appliance before it installs anything.** It reads the
> board's assembly number (`src/opnsense/scripts/xgs/board.sh`) and installs only that board's
> pieces: **AMDA0201** (XGS 126/136) gets the ARMADA set below, **AMDA0202** (run on the XGS 3300)
> gets the OCTEON TX set - `08-octep`, the driver's sources, and the module built through the kernel
> follower. **Any other board gets nothing**, and so does an ARMADA board with an OCTEON TX
> endpoint on its bus. Each set removes the other's pieces if an older version put them there.
> The OCTEON TX module build needs the running kernel's sources already in `/usr/src-<series>-<sha>`
> (`contrib/npuep/fetch-sources.sh`); see [docs/families/octeon-tx.md](../families/octeon-tx.md).

```sh
git clone https://github.com/AbdelmonemAwad/os-xgs-npu
cd os-xgs-npu
./install/install.sh
```

> [!WARNING]
> **Before blaming anything here for a network that has no internet, check that the firewall can
> resolve a name.** A fresh OPNsense leaves the resolver recursing to the root servers, and an
> upstream that does not allow that makes every query fail. It cost most of a day on this
> appliance.
>
> ```sh
> host example.com 127.0.0.1
> ```
>
> `SERVFAIL` means the resolver, not the datapath. The fix is to give the system nameservers and
> let the resolver forward to them instead of recursing: **System - Settings - General - DNS
> servers**, then **Services - Unbound DNS - Query Forwarding - Use System Nameservers**.
>
> It hides well. A machine with more than one network keeps every adapter's nameservers and
> resolves through whichever works, so the port you administer the box from looks fine while a
> network whose only resolver is the firewall has no internet at all - and the firewall still
> answers `ping` by address, because an address needs no name. The first network that depends on
> this box alone is where it surfaces, which may be long after the install.

On an ARMADA board the installer puts in place two early boot hooks and, if a module has been
built, installs it:

- `06-npuctl` pulses the coprocessor out of reset. It comes out of power-on **held**, and nothing
  in OPNsense releases it — which is why the appliance boots with a silent coprocessor and no
  ports.
- `07-npuep` loads the driver and waits for the interfaces to appear, because every early hook
  runs before OPNsense configures its interfaces and one that returns too soon leaves them out of
  that pass.

The kernel module is **built on the appliance**, not packaged: it is C against that kernel's
headers. `contrib/npuep/fetch-sources.sh` fetches the sources that match the running kernel,
pinned to the commit the kernel names in `uname -v`; `contrib/npuep/build.sh` builds against them
and stamps the result with the kernel it was built for; the installer copies both to
`/boot/modules`.

That stamp is not bookkeeping. A module's kernel dependency is a range running to the end of its
branch, so a module built for the wrong 15.x kernel **loads without complaint** rather than being
refused — the loud failure this project was designed around does not arrive. `install/verify.sh`
compares the stamp instead, and `compat.json` records `kern_version` rather than the version
labels, which do not move when OPNsense ships a kernel set inside a series.

Both hooks are numbered above OPNsense's own `05-upgrade`, which finalises a pending firmware set
and reboots from inside the early boot sequence. Numbered below it, as `01` and `02`, this code
brought the coprocessor up and then had the machine rebooted underneath it.

It cannot be preloaded from `loader.conf`, and not for one reason but two — the reset pulse has
not happened at that point, so there is no live device to attach to, and the BAR restore this
hardware needs only happens on the `kldload` path. See
[docs/npuep/porting-notes.md](../npuep/porting-notes.md).

Both hooks are written so that they can never be the reason a firewall fails to boot: unfamiliar
hardware, a missing module or a coprocessor that never answers each log the reason and exit 0.

### In the web interface

Every page below is photographed on an appliance, in English and in Arabic, in
[the-pages.md](the-pages.md).

Everything is in one section of the main menu, ~~**XGS Coprocessor**~~ **XGS** - shortened on
2026-10-18, because the long name broke over two lines in the collapsed menu - between Interfaces and
Firewall: **Overview**, **Settings**, **Ports**, **IPsec**, **Updates** and **Log File**. The
settings page has every setting the plugin has - offloading, the flow maker, IPsec in the engine,
the XGS 3300's tunnel flows, the XGS 136's receive poll and its crypto pin, the switch chip's jumbo
mode - and shows only the ones the appliance has; each is written where it takes effect, and the
page says when a reboot is needed. A box whose IPsec tunables were set by hand opens the page with
them as they are. Every string of the section is in Arabic as well (139 on 2026-10-17, where
OPNsense's own catalogue covered 20%), merged into the GUI's catalogue at install and at every
boot. See [docs/octep/coprocessor-section.md](../octep/coprocessor-section.md) -
tested from the command line on an XGS 3300 and an XGS 136 on 2026-10-17; the pages in a browser
not yet.

**Overview** opens, since 2026-10-18, with a drawing of the front panel: every port in the order the
chassis has them, in a group for each kind - copper, the cages, the expansion module - with its link
as a light; a port links to the Ports page. Under the cards, **Last restart** says what the host's
chipset recorded as the cause of the reset that started this boot (a reboot, the hardware watchdog,
an uncorrected hardware error, overheating, the reset pin, the power button), from the record
[docs/problem-reports.md](../problem-reports.md) describes. The board's limits draw a bar of how much
of each table is in use. A value of figures alone is kept left to right, so Arabic does not show
"8 / 16" as "16 / 8". The dashboard has a fourth widget, **XGS Offloaded Flows**: connections in
hardware against the table's size, next hops, and what was made, taken back, revived and refused,
from the driver's figures without its table - and no throughput, because a forwarded connection never
reaches the host's counters. Tested on 2026-10-18 with the XGS 136's own data, under both themes and
right to left (the page itself, away from the appliance); the readers on the XGS 136.

**What stays with the firewall** is the owner's to say, since 2026-10-18. **Offloaded Flows** has a
**Return to host** button on each connection: the coprocessor lets it go, the host forwards it from
then on, and the flow maker leaves it alone until pf lets its state go - so a connection someone
wants to watch, shape or capture can be taken out without turning offloading off. **Settings** has
**Never offloaded**: addresses, networks and ports (`192.168.1.10`, `10.0.0.0/8`, `2001:db8::/32`,
`port 22`), one per line or separated by commas and up to 64, whose connections are never made - either end of a
connection can match. It is applied when the page is saved, taking out at once the connections in
hardware it names, and at every boot before the flow maker starts; an entry that cannot be read is
named in the apply's report and the rest are applied. The button is granted with the settings, not
with the page's own read-only privilege. A fifth card counts the connections returned by hand and
how many times the list said no - an attempt each, not a count of connections. Tested on 2026-10-18
with live connections on an XGS 3300 (each kind of entry, the list from the page's own save, at
boot, and a connection returned) and on the XGS 136 (a connection returned, and the list at boot and
against a live connection) - on the XGS 136 with two ports' interface entries opened by hand,
because its driver leaves them shut and no connection is made there otherwise
([#483](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/483)).

**A block list that cuts what is already open**, since 2026-10-19. A rule that blocks a list - an
alias filled from a URL table or a feed - stops new connections only; a connection opened before its
address was listed goes on, in the firewall and in the coprocessor. **Settings** has **Cut open
connections when listed**: the aliases chosen there are compared every minute, and when the page is
saved, with what they held before, and every connection with either end in what a list gained is cut
- its pf state killed by id, and the coprocessor lets go of it with its state. Choosing a list cuts
what it already names, and the XGS log says what was cut. The internal aliases (`__lan_network` and
the like) are not offered: cutting one would cut a whole network. Measured on both appliances: a
stream in hardware stopped within two seconds of its address being listed, and through the minute's
cron within 34 and 60 seconds ([lists-cut-2026-10-19.md](../measurements/lists-cut-2026-10-19.md)).

**Connections cut by their TLS fingerprint**, since 2026-10-19. A JA4 fingerprint names the software
that opened a TLS connection - what its first message offers - rather than the address it went to, so
it still matches a program that changes its server. **Settings** has **Cut connections by TLS
fingerprint**: a list of JA4 fingerprints, one per line. It reads the log of OPNsense's own Intrusion
Detection, which must be on - IDS mode, which only listens, is enough - with its TLS log carrying JA4:
**Services: Intrusion Detection: Administration**, advanced mode, **Enable eve TLS logging**, and
**ja4** in **Eve TLS custom logging**, on the interfaces the connections come in on. The log is read
every two seconds, and every connection it shows with a listed fingerprint is cut - its pf states
killed by id, and the coprocessor lets go of it with them. Listing a fingerprint cuts what is already
open with it. To find one, `/usr/local/opnsense/scripts/xgs/ja4_cut.py --seen` lists the fingerprints
in the log, the most frequent first, each with the last connection that offered it. Measured on the
XGS 3300: a fingerprint is logged before its connection goes into hardware, and of two streams from
the same program, only the one with the listed fingerprint stopped
([ja4-cut-2026-10-19.md](../measurements/ja4-cut-2026-10-19.md)). Not measured on the XGS 136.

**Unusual traffic, reported**, since 2026-10-19. **Settings** has **Watch for unusual traffic**, off
until it is turned on. Every minute it reads the firewall's connections every ten seconds and reports
in the XGS log - never blocks - a port scan (one host trying 100 ports or more on one address within a
minute), a sweep (one host reaching 256 new addresses or more), and a host that sends, receives or
opens far more than it usually does, once it has an hour of history. Measured on the XGS 3300
([anomaly-2026-10-19.md](../measurements/anomaly-2026-10-19.md)).

**Hardware traffic counted where OPNsense looks**, since 2026-10-19, on both appliances. A connection
the coprocessor forwards is not seen by pf or by the interfaces, so their counters - the Sessions
page, the rules' statistics, the traffic graphs and the dashboard - stop at the moment it goes into
hardware. The coprocessor counts what it forwards and tells the host about once a second; **Settings**
has **Count hardware traffic in pf** and **Count hardware traffic on the interfaces**, both on by
default, which add those counts to pf's states and rules and to the interfaces' counters. Measured:
pf's state held the whole connection to the packet, and both ports counted a stream to 327 packets in
5.1 million ([hw-counters-2026-10-19.md](../measurements/hw-counters-2026-10-19.md)). ~~Not yet on the XGS 136.~~
On the XGS 136 since the same day: pf's state came to what crossed the wire, 0 packets apart in 1.37
million either way, and every interface to the frame
([xgs136-counts-2026-10-19.md](../measurements/xgs136-counts-2026-10-19.md#the-reader-measured)).

### Problem reports

What goes wrong on an appliance - a driver line that says something failed, an error from one of the
appliance plugins, a failed update, a driver module built for another kernel, a kernel panic, a
restart without a shutdown - is written down, cleaned of every address, serial and name, and filed as
a GitHub issue in the repository of the part it belongs to, through the project's relay, so nobody
needs a GitHub account. Automatic by default, said so in a banner until the owner has chosen; manual
or off on **XGS: Problem Reports**, which shows exactly what is sent. See
[docs/problem-reports.md](../problem-reports.md) and [relay/README.md](../../relay/README.md) - tested on
a made-up machine by `tools/test-reporter.py` and `relay/test.mjs`; on an appliance, not yet.

### OPNsense's own settings, for this hardware

OPNsense's System: Settings: Miscellaneous page, read option by option against both appliances:
the plugin sets **Thermal Sensors to amdtemp** once, where nothing is chosen, so the CPU's
temperature reaches OPNsense's dashboard (38.6 C and 53.6 C at idle, measured on both on 2026-10-17)
and the page shows it. **PowerD** was measured on an XGS 3300 and left off: in `hiadaptive` it cost
nothing measurable (5.36 against 5.31 Gbit/s at MTU 1500, a round trip of 0.98 against 1.02 ms) and
bought nothing measurable at idle. Swap, the logs in memory and the beep are left as they are, each
for a reason. See [docs/octep/appliance-defaults.md](../octep/appliance-defaults.md).

### Updating

~~**System - Firmware - XGS Coprocessor.**~~ **XGS - Updates.** This project is not in
OPNsense's package repository, so System - Firmware - Updates never offers it; this page does.

- **Check for updates** asks GitHub for ~~the newest commit on `main` whose checks all passed - a
  commit whose checks are running or failed is never offered, however new - and lists what changed
  since the installed one, newest first~~ the newest **release** (since 2026-10-18), and lists every
  release since the installed one by the lines of its notes - which are its section of
  [CHANGELOG.md](../../CHANGELOG.md). A release is made when a fix is ready, or when improvements
  have been gathered ([RELEASING.md](../../RELEASING.md)), so a day's merges are not offered one by
  one. Only forward: an appliance running something newer is not taken back.
- **Update** downloads that ~~commit~~ release and runs the `install/install.sh` it carries, the same script a
  hand install runs. The driver module is built for the running kernel and stamped, and the page
  ~~follows the log while it builds~~ shows a progress bar (since 2026-10-18): six steps - the
  download, the unpacking, the installer's sections, the kernel sources, the driver's build and the
  finish - and a clock. It is read from what the update leaves behind, never estimated from the time:
  during the build, from the object files appearing beside the sources, so "5 of 9 source files
  compiled" is exactly that. The log stays on its own tab. Before this, the page showed one fixed line
  and the log sat at `== kernel module ==` for the whole build, which writes a log of its own.
- **What changed** lists every release newer than the one installed - and, when there is nothing
  newer, what the newest release itself carries, rather than an empty tab. An installation that is a
  commit rather than a release (made by hand, or by the page before it followed releases) says so,
  and one ahead of the newest release is told the next release will be offered.
- **Channel** (since 2026-10-18): **Releases**, the default and the one for an appliance in service,
  or **Test**, which follows `main` - each change as soon as it is merged and its checks pass, before
  it is part of a release - after a warning. It is the one-line file
  `/var/db/os-xgs-npu/update/branch` the updater has always read, so a shell and the page set the
  same thing; changing it drops the last check, which belonged to the other channel. Tested on the
  XGS 136: set to main, read back, set to releases.
- **The new driver loads at the next boot**, not before: reloading it takes the front ports away,
  and on an XGS 3300 the WAN is one of them. The page then offers **Reboot now**, or the update
  waits for whenever the firewall next boots. Nothing reboots on its own.

What is installed is recorded in `/usr/local/share/os-xgs-npu/installed.json` - the commit, read
from a git checkout, from the stamp an update leaves, or from the one an
[xgs-image-builder](https://github.com/AbdelmonemAwad/xgs-image-builder) image carries. The check
is unauthenticated, and GitHub allows sixty such requests an hour from one address; a check costs
up to seven.

Tested in a QEMU virtual machine installed from an XGS 3300 image (the OCTEON TX set, without the
coprocessor): check, update from the page, the buttons held while it builds, **Reboot now**, and
the page after the boot. Not yet run on an appliance.

### The first boot assigns the front ports

On an appliance whose front ports are all unassigned, the first boot that has them counts thirty
seconds down on the console and then assigns them by the names on the chassis: **Port2** is WAN on
an XGS 3300, **Port1** on an XGS 136, and every other front port is a member of one bridge, the
LAN, on **192.168.1.1/24**; the management port, where there is one (the XGS 3300's), becomes MGMT
on **192.168.100.1/24** - both with DHCP. Any key during the countdown leaves the assignment to the
console menu or the GUI. A box whose
ports were set up by hand is never touched. See
[docs/octep/interface-assignment.md](../octep/interface-assignment.md#on-the-appliances) - tested
under QEMU as both boards, ~~not yet booted on an appliance with unassigned ports~~ and booted on
an XGS 3300 (with a 4x10G module: fifteen ports in the bridge) and on an XGS 136 on 2026-10-17:
each assigned itself, handed out addresses on its LAN and its MGMT, and reached the internet through
its WAN.

### Expansion modules

On the XGS 3300 a FleXi module in the front bay belongs to the coprocessor, and its ports come up as
interfaces after the panel's twelve, named as the metal names them - `A1` to `A4` for a 4x10G module
in bay A. Nothing names a module in the code: at every boot the bring-up reads the coprocessor's own
platform database and joins it, by address, to the ports NetAgent reports, so a module nobody here
has seen binds the same way. Measured with a 4x10G module (AMDA0207): sixteen interfaces, the
module's four named and one with link; traffic through a module port not yet measured. See
[docs/octep/expansion-modules.md](../octep/expansion-modules.md).
