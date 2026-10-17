# The coprocessor's section of the menu

Written 2026-10-17, when the plugin's pages were gathered into one section and every setting it
has was put on its settings page.

Everything the plugin puts in the web interface is in one section of the main menu, **XGS
Coprocessor**, between Interfaces and Firewall. It replaced three entries in three places: the
settings under System: Settings (as "Hardware Offloading"), the status under Interfaces:
Diagnostics, and the updates under System: Firmware. An owner looking for one of them had to know
which errand OPNsense files it under; now they are where the hardware is.

| page | address | what it does |
|---|---|---|
| Overview | `/ui/xgs/status` | the coprocessor at a glance, in tabs since 2026-10-17: Status - the five summary cards (the coprocessor and its link, offloading and the flow maker, the IPsec engine, the ports with link, the switch chip; ~~replaced by a summary table~~ kept, on the owner's word that they looked right) over a striped table of the coprocessor's link and the control facility with OPNsense's section headings; the datapath counters in OPNsense's grid with what each counts (~~counter tiles~~); the technical details in a table - read-only |
| Settings | `/ui/xgs/settings` | what the coprocessor and the driver have now, read back from them; every setting the plugin has, below; and the port assignment's second run - one OPNsense form |
| Ports | `/ui/xgs/ports` | each front port by the name on the metal: what it is assigned as, its link, speed, MTU and bridge, in OPNsense's own grid (search, sort, columns, export); an expansion module's ports; what the switch chip's ports were set to at the last boot |
| Transceivers | `/ui/xgs/transceivers` | since 2026-10-17: the module in every front cage - vendor, part, type, serial - and, where the module reports them, its temperature, supply voltage, laser bias and the light it sends and receives in dBm; read on the coprocessor every five minutes (`scripts/xgs/transceivers.py`) or on **Read again now**; four cards: modules fitted, modules with diagnostics, the warmest, the weakest light received |
| IPsec | `/ui/xgs/ipsec` | laid out as OPNsense's own IPsec pages since 2026-10-17: a Status tab (IPsec in the engine since this boot, the family's own setting, the driver), the associations the engine holds in OPNsense's grid - direction, SPI, the tunnel's two ends, the port, the replay window, the handle - and every counter of its IPsec subtree in a grid with its group, the ones at zero shown when asked for |
| Offloaded Flows | `/ui/xgs/flows` | since 2026-10-17: every connection the coprocessor forwards by itself - protocol, the two ends, the port it arrives on by its panel name, NAT, whether it goes through IPsec with its association in hardware, its state - read from the driver's own table (`dev.octep.0.dp.flow_table` on the XGS 3300, `dev.npuep.0.flow.flows` on the XGS 136) by `scripts/xgs/flows.py`; four cards: in hardware now, the flow maker on or off, through IPsec, taken back since the driver loaded |
| Updates | `/ui/xgs/updates` | the updates page that was under System: Firmware, laid out as that page is since 2026-10-17: a Status tab (source, installed, latest, last update, the reboot it waits for, the two buttons in the table), What changed, and the installer's log |
| Problem Reports | `/ui/xgs/reports` | how this appliance reports what goes wrong - automatic, manual or off - and the reports, each with exactly what is sent: [docs/problem-reports.md](../problem-reports.md) (since 2026-10-17) |
| Log File | `/ui/diagnostics/log/core/xgs` | every tag the plugin logs under: `octep` (and `octep-follow`, `octep-stop`), `npuep`, `npuctl`, `xgs-assign`, `xgs-switch` |

**Why the transceivers are read on the coprocessor.** The cages' EEPROMs hang off the coprocessor's I2C,
not the host's, and neither driver offers `SIOCGI2C`; the vendor's `xgs-sff-decode -l <label>` reads a
module's identity and `xgs-sff -p <phy> -g` its diagnostics. The decode tool says "Diagnostic
Monitoring not supported" for every module, also for ones whose byte 92 says they have it (0x68), so
byte 92 decides here. Measured 2026-10-17 on the XGS 3300: five cages of eight fitted, three with
diagnostics (an Intel 10GBASE-SR module in A1 at 33.8 C, -2.01 dBm out and -3.42 dBm in; F2's module
receiving no light); a whole read takes 28 seconds over the console, two commands. On the XGS 136 both
cages are empty and the read over the management link is immediate.

**The offloaded flows, seen with traffic.** Two iperf3 streams from a PC on F1 of an XGS 3300 to a
second XGS 3300 behind A1, through the IPsec tunnel between the two (2026-10-17): the page listed the
three connections (two data, one control) in hardware, arriving on F1, encrypted out and decrypted in
by their association - the flow maker on, three taken back from an earlier run. On the XGS 136 the
flow maker is off and the table empty; its rows have no arrival port, because its driver does not
print one.

**On the dashboard** (since 2026-10-17), three widgets in the Lobby's "Add widget" list, each opening
its page when clicked:

| widget | shows | reads |
|---|---|---|
| XGS Coprocessor | whether it answers, whether offloading is on, how many ports have a link, its temperatures (the die, and the inlet air on the XGS 3300) | `/api/xgs/status/get`, `/api/xgs/ports/search`, OPNsense's own sensor list |
| XGS Ports | a tile per port in the Ports page's order - green with its speed where it has a link, grey where it has none - with the name it is assigned under | `/api/xgs/ports/search` |
| XGS IPsec Engine | the engine on or off, the associations it holds now, frames encrypted and decrypted, associations turned down | `/api/xgs/ipsec/get` |

Seen in the browser on the XGS 3300 the same day, added to the dashboard without saving it:
answering, offloading on, 5 of 16 ports with a link, the die at 73.1 C and the inlet at 31.1 C;
the engine on with 4 associations held and 60 turned down.

The appliance plugins - link health, fan control, the front panel - are meant to put their pages in
the same section from their own packages; until they do, they stay where they are.

The privileges keep their old ids, so a user or group already granted one keeps it. The read-only
pages (Overview, Ports, IPsec) share one privilege; Settings and Updates have one each, because one
takes traffic out of the firewall's path and the other installs software and reboots.

## In Arabic

Every string the section shows - the pages, the settings form, the menu, the privileges - has its
Arabic in `src/opnsense/scripts/xgs/i18n/ui/ar_SA.json`: 139 strings on 2026-10-17 (157 once the
forms below were redone the same day), against 26 of
127 (20%) that OPNsense's own Arabic catalogue happened to cover before. They are merged into the
catalogue of each installed GUI language the way os-linkhealth and os-netreport merge theirs: by
the installer, again at every boot (`rc.syshook.d/start/63-xgs-i18n`), and by pkg whenever a
package writes a catalogue (`src/share/pkg/triggers/os-xgs-npu.ucl`). A string the catalogue
already translates keeps core's words; what the merge wrote is remembered in
`/var/db/os-xgs-npu/i18n-owned.json`. `tools/check-gui-strings.py`, run by the checks, fails when a
page gains a string without its Arabic. The terms follow the glossary of the opnsense-arabic
project (its GLOSSARY.md) and the words OPNsense's own Arabic catalogue already uses - for offloading,
the switch and the driver among them.

## A command on the coprocessor, for the other plugins

`scripts/xgs/npu-run.sh <seconds> <command>` runs one command on the coprocessor's own Linux and
prints its output, on both appliances: over the coprocessor's sshd on the PCIe management link on
the XGS 136, and over its root console on `/dev/cuau2` on the XGS 3300, which has no such link (and
on a 136 whose link is down). On the console it logs in only if asked, cuts the output out between
two markers, and logs out again; one caller at a time, under a lock, and a second gives up after
five seconds with exit 75 rather than interleave. It is what os-linkhealth's port identify needs on
the XGS 3300, where the management link script it used is not installed.

Tested on 2026-10-17: on the XGS 3300 over the console (`uname`, `pidof`, the coprocessor's
startup script read back, `xgs-led-identify -l 4 5` answering 0), on the XGS 136 over the link, and
two calls at once on the XGS 3300 - the first answered, the second gave up with 75. Whether the
light blinked was not seen.

## The settings

~~**Save and apply**~~ **Apply**, the form's own button since the page became one OPNsense form,
writes each setting where it takes effect, and reports each step:

| setting | family | written to | takes effect |
|---|---|---|---|
| Hardware offloading | both | the coprocessor's gate, through `offload.sh` | at once, and again at every boot |
| Accelerate connections automatically | both | `dev.octep.0.dp.auto` / `dev.npuep.0.flow.auto` | at once, and again at every boot |
| IPsec in the coprocessor's engine | both | the loader tunable `hw.octep.ipsec_on` / `hw.npuep.ipsec_on` | at the next reboot |
| Outbound tunnel flows forwarded by the coprocessor (~~Tunnel flows forwarded by the coprocessor~~ until 2026-10-17: only the direction that leaves encrypted is) | XGS 3300 | the loader tunable `hw.octep.ipsec_flows`, 0 or 1 - never 2 | at the next reboot |
| Keep IPsec's software sessions off ccp | XGS 136 | `net.inet.ipsec.crypto_support` | at once, and at every boot (`07-npuep`) |
| Receive poll interval | XGS 136 | `dev.npuep.0.giu.poll_ms` and the tunable `hw.npuep.giu.poll_ms` | at once, and at every boot |
| Receive budget | XGS 136 | `dev.npuep.0.giu.rx_budget` and the tunable `hw.npuep.giu.rx_budget` | at once, and at every boot |
| Jumbo frames on the switch ports | both | read by `switch-jumbo.sh` | at the end of every boot |

A setting this appliance does not have is not shown. The tunables go through OPNsense's own Tunables
model, so they are listed under System: Settings: Tunables like any other, and `loader.conf` is
written by the function that page's apply runs. A tunable already holding the wanted value is not
written again, so a save that changes nothing writes nothing. When a setting needs a reboot the page
says so; nothing reboots on its own.

There is no restart button for the coprocessor. Nothing restarts it safely while the firewall runs:
it is reset at every boot of the appliance, and that is the way to restart it.

**Assign the ports again at the next boot** removes the mark the first-boot assignment leaves
([interface-assignment.md](interface-assignment.md)). The assignment still acts only on a box with
no front port assigned, so on a box in service the next boot marks it again and changes nothing.

### A box set up by hand keeps its settings

Before these settings were on the page, IPsec in the engine was turned on by adding the tunable by
hand. The page reads those rows when the plugin is installed (the model's 1.1.0 migration): each
field takes the value of the tunable it now writes, so the page opens showing what the box does, and
saving it unchanged changes nothing. Without that, the page would open at the defaults and the first
save would turn IPsec off in the engine at the next boot.

### Three things that were wrong, found while testing it

- **The privileges kept their old names.** OPNsense caches them in
  `/var/lib/php/tmp/opnsense_acl_cache.json` beside the menu's cache, and the installer removed only
  the menu's, so a renamed or added privilege waited for the cache to expire. It removes both now.

- **`loader.conf` missed the new rows.** The apply saved the tunable and then called
  `system_login_configure()`, which writes `loader.conf` from the legacy `$config` array that
  `config.inc` filled when the script started - the rows as they were before the save. The array is
  now reloaded after the save.
- **The Ports page showed OPNsense's names on the XGS 3300.** The driver names each port in the
  interface's description, and OPNsense replaces that description once the port is assigned. The
  bring-up now saves each name as the driver gives it (`/var/db/os-xgs-npu/labels`); until a boot has
  written that file, the page uses the order the bring-up creates the interfaces in.

## Tested

On 2026-10-17, on an XGS 3300 (with a 4x10G module) and an XGS 136, both on OPNsense 26.7.7, after
an install of the branch: the migration (`Migrated OPNsense\XGS\XGS from 1.0.0 to 1.1.0`, IPsec in
the engine read as on from each box's tunable); `configctl xgs apply` twice on each, the second
writing nothing; `loader.conf` gaining the new rows and nothing else; `configctl xgs ports` naming
all sixteen ports of the XGS 3300 (`F1` ... `A1`) and all fourteen of the XGS 136; `configctl xgs
ipsec` listing the four associations of each box's two tunnels; the menu read through OPNsense's
own menu class, the section between Interfaces and Firewall with its six pages; the privileges under
their new names once the cache was gone; the four views compiled by Phalcon's Volt compiler. Not
~~yet: the pages in a browser~~ (seen on 2026-10-17, below)~~, and a boot with the new bring-up and switch state file~~. Booted on an
XGS 3300 on 2026-10-17: the bring-up wrote `/var/db/os-xgs-npu/labels` with all sixteen names, A1 to
A4 included, the Ports page showed them, and the switch state read "ports 1-10 at 10,240 bytes,
confirmed".

### Seen in a browser, 2026-10-17

The six pages were opened on the XGS 3300 through its own name (`xgs3300.eg2.uk`, a Let's Encrypt
certificate) and looked at page by page. Two things were wrong, and both are fixed:

- **Text escaped twice.** OPNsense's API escapes every string it returns, and the pages escaped
  them again, so a media type read `1000baseT &lt;full-duplex&gt;` and the IPsec table `-&gt;`.
  The pages now insert what the API returns as it comes and escape only what they build
  themselves; the IPsec page reads the engine's text back as text before parsing it.
- **The Log File page was empty.** `xgs_syslog()` names the log to the GUI and does not create it:
  syslog-ng writes `/var/log/xgs` only with a filter template under `OPNsense/Syslog/local`, which
  every package that keeps its own log ships. The plugin ships `xgs.conf` now, and the installer
  writes syslog-ng's configuration again; the page then showed the kernel follower's lines. Its
  default filter is Warning, and most of what the plugin logs is Notice - choose Notice or
  Informational to see it.

And the pages were made easier to read: the Overview became five summary cards over two compact
tables, the Ports page one table with the module's ports in it and the switch's state as a chip,
the IPsec page a real table of associations and its counters in groups with the zeros hidden, the
Settings page's two long notes two short ones (~~two short ones~~ none: see below), and the Updates
page folds a good update's log away.

### In OPNsense's own form and grid, 2026-10-17

The Ports and Settings pages were then rebuilt from the pieces OPNsense's own pages are made of, so
they look and behave like the rest of the GUI:

- **Ports** is the grid Interfaces: Overview uses, fed by `/api/xgs/ports/search`: search, sort,
  choose columns, export, a plug for the link, the speed taken out of the media string. The
  module's part number and the switch chip's state sit in a two-row table under it, and a port in
  the module carries only its bay.
- **Settings** is one form with the form's own **Apply** button. Its first rows, under Status, are
  what the coprocessor and the driver have now - offloading on the coprocessor, IPsec in the engine
  since this boot - read back from them on every load, so the page no longer opens with two notes
  above the form. The port assignment's second run is the form's last row, its button in the row,
  its explanation under the row's help. Every note is under full help, where OPNsense keeps them.

One thing was wrong in the grid: a one-pixel horizontal scrollbar under the last row. The columns
fill the width exactly and the cells start one pixel in; Interfaces: Overview has the same pixel,
but its vertical scrollbar takes it. The page hides sideways scrolling from 768 pixels up, where
the table always fits, and keeps it below.

Seen on the XGS 3300 in a browser on 2026-10-17: the grid with all sixteen ports, a search and a
sort answered by the API (seven rows for `opt1`, the two 9,000-byte ports first); the form with
the XGS 136's three rows hidden, the two Status rows reading On, and full help on and off.

### The Updates page in System: Firmware's shape, 2026-10-17

Updates was the last page of the section still drawn its own way - a blue note over a small table,
headings of its own, the installer's log folded under a link. It is now laid out as System: Firmware
is: three tabs, **Status** a striped table of where updates come from, what is installed, what is
offered, what the last update did and the reboot it waits for, with the two buttons in its last row;
**What changed** the commits between the two, with their count on the tab; **Installer log** the log.

Looking at it found one thing wrong in the updater itself. The XGS 3300 ran a build of a local branch
whose commit was never pushed, and every check ended in "GitHub refused the request (404)" with
nothing offered: the comparison of the installed commit with the newest one fails when GitHub has
never seen the first. `update.py check` now offers the newest passing commit in that case, as it does
for an installation that recorded no commit, and the page says the installed version is not on
GitHub; nothing is installed until the owner presses Update. Checked on the XGS 3300: the check
offered `562ab3f` with its five commits listed.

### Summary cards on every page that has something to sum up, 2026-10-17

The Overview's cards were kept on the owner's word, and the other pages that have a state to read at a
glance got the same cards over their tables, in the same style and the theme's colours: **Ports** (the
ports with link, the ports assigned, the expansion module, the switch chip), **IPsec** (the engine, the
associations it holds, how many were installed since this boot, how many offers it declined), **Updates**
(what is installed, what is offered, what the last update did) and **Problem Reports** (the sending mode,
the reports waiting, the reports sent, the last problem). Settings is a form and has none.
