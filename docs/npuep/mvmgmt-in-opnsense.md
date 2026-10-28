<!-- reviewed: 2026-10-21 -->
# The management link gets its place in OPNsense

Written 2026-10-14 as a plan for [issue 374](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/374),
reviewed, built the same day and proven across a reboot of the XGS 136. Measured first: the link
over PCIe is whole in both directions once the host brings `mvmgmt0` up, and OPNsense's default
deny drops every frame the coprocessor sends in, because the interface is nobody's. The driver
needed no change; the plugin did. What follows is what was built, with the two places where the
plan said one thing and the appliance said another.

## What the plugin does at install, and at every boot

1. **Assigns the interface.** `mgmt-link.php assign`, run by the installer on the ARMADA
   appliance, makes `mvmgmt0` an interface of this firewall named `NPUMGMT` with the fixed
   link-local address `169.254.254.1/30`, written through the same `config.inc` every page uses,
   with a revision line and a backup beside it. Idempotent: an assignment that already carries
   `mvmgmt0` is kept and only its name and address made right. On the XGS 136 it became `opt15`.
2. **Admits the coprocessor.** Not a rule in the configuration: the plugin's own firewall hook,
   `xgs_firewall`, registers two rules at every reload on whichever interface key carries
   `mvmgmt0` - ICMP, and TCP 22 for the coprocessor's `sshd`, from `169.254.254.2` to this host
   alone, quick, logged. Nothing is forwarded, nothing else enters, and a machine with no such
   assignment gets no rule at all.
3. **Brings it up.** `07-npuep`, the early hook that loads the driver, ends with
   `ifconfig mvmgmt0 up` once the front ports are present, so the link establishes before
   OPNsense configures its interfaces. Measured on the boot after: `mgmt: link 0x00 -> 0x82`,
   `0x82 -> 0x83` at 38 seconds, the assignment then found the link already up.
4. **Tells the coprocessor its address.** Its root filesystem is read-only and forgets every
   address at its reset, and nothing on its side knows the pair, so the plugin's bootup hook
   (`xgs_configure` -> `mgmt-link.sh coprocessor`) logs in over the console and sets
   `169.254.254.2/30` there, then pings it and says so in the log:
   `the coprocessor answers at 169.254.254.2`.
5. **Trusts the host.** `mgmt-link.sh trust` makes the host's key for this link,
   `/root/npu/id_ed25519_npu`, and appends its public half to the coprocessor's
   `/root/.ssh/authorized_keys` - one line, guarded by the key itself, a line of an earlier install
   replaced rather than added beside. ~~Guarded by its own comment~~, until 2026-10-21: the
   coprocessor's file outlives the host's disk, so a host installed again found its old line under
   the same comment and added nothing. Since 2026-10-21 it is not a step done by hand: the bootup
   hook's `mgmt-link.sh coprocessor`, once the coprocessor answers its ping, tries the link's ssh
   and runs `trust` when it is refused. ~~Done once, by hand~~ - which an appliance installed from an
   image never had, so every reader of the coprocessor queued on its console
   ([668](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/668)). ~~The plan said this
   would go on the coprocessor's `/persistent` partition.~~ That partition is mounted by the
   vendor's start-up script and holds only its `breakouts/`; nothing there is read for keys. What
   the vendor does have is `xgs-remount.sh rw|ro`, its own script for taking the root filesystem
   writable and back, and that is what `trust` uses, around the one appended line.
6. **Uses it.** `mgmt-link.sh ssh [command]` is a root shell, or one command, on the coprocessor
   at PCIe speed - four megabytes in about a second against the 115200 console - with the
   console kept as the fallback and the boot log. The vendor's `xgs-*` tools, `mvsw`, the
   platform store and the sensors are all reachable through it.

`mgmt-link.sh status` prints the interface, the assignment, the live rules, a ping and an ssh,
which is what the proof below is read from.

## What is not done

- The interface is never bridged, never given a gateway, never offered to the WAN or the LAN.
- No DHCP on either end. No IPv6 on either end.
- The vendor's firmware image on the coprocessor is changed by exactly one appended line in
  `authorized_keys`, under its own remount script; `/persistent` is not used.
- ~~Nothing is driven on the coprocessor by the plugin's own initiative; the link is for reading
  and for tools the owner runs.~~ Since 2026-10-27 the plugin does ~~two~~ three things on the
  coprocessor of its own accord, all over this link:
  - it starts the coprocessor's watchdog feeder again when the feeder has stopped
    ([measured](../measurements/coprocessor-feeder-2026-10-27.md));
  - it sets the coprocessor's clock from the host's (below);
  - since 2026-10-28 it tells the coprocessor's log forwarder to send the coprocessor's log to the
    host (below).

  Otherwise the link is for reading, and for tools the owner runs.

## The coprocessor's clock, since 2026-10-27

Nothing set it. A few minutes into a boot, both XGS 3300s' coprocessors read 1970 and the XGS 136's
2097, so every time the coprocessor writes - in its own log, on its console - was wrong. The vendor's
host sets it at every boot, and now this one does too.

- **How:** `scripts/xgs/npu-clock.sh` reads the coprocessor's clock over this link, never the console.
  When it is more than two seconds from the host's, it sets it in UTC and says so in the system log
  under the tag `xgs-npu-clock`, with what it read.
- **When:** the start hook `71-xgs-npu-clock` runs it at the end of the boot - every 15 s, for up to
  five minutes, while the coprocessor comes up. Cron runs it every ten minutes after that, so a
  coprocessor taken up again in place is set again within ten minutes.

Measured on 2026-10-27, on each appliance:

| appliance | vendor version | the coprocessor's clock read |
|---|---|---|
| the first XGS 3300 | 23 | 1970-01-01 00:15:20 |
| the second XGS 3300 | 22 | 1970-01-01 00:13:23 |
| the XGS 136 | 21 | 2097-07-24 07:19:10 |

Each was within a second of the host's afterwards, the next run left it alone, and the fast path's
heartbeat stayed alive throughout.

## The coprocessor's log, since 2026-10-28

The coprocessor's syslogd keeps its log in RAM - `/tmp/messages`, half a megabyte at most - so each
reset of the coprocessor lost it, and with it the lines that said why. Its syslogd also hands every
line to a small forwarder on the coprocessor, which sends nothing until it is told where. The vendor's
host tells it at every boot, and now this one does too. The forwarder's source is in the vendor's
open-source drop (`xgs-bsp/tools/xgs_log_fwd.c`).

- **How:** `scripts/xgs/npu-log.sh` writes this host's end of the link into the forwarder's address
  file - `169.254.254.1%mvmgmt0%514`: the address, the coprocessor's own interface, the port - and
  signals it. From then on every line goes to 169.254.254.1, port 514. syslog-ng cannot listen on
  that address: it starts before the interfaces have theirs, and one source that cannot bind stops it
  whole. So it listens on 127.0.0.1:5140 (`/usr/local/etc/syslog-ng.conf.d/xgs-coprocessor.conf`), and the
  plugin's firewall hook redirects the link's port 514 there, from the coprocessor's address alone -
  `rdr pass`, the way a transparent proxy is reached.
- **Where:** a source of its own, never mixed with this host's lines, written as OPNsense writes its
  own logs: a file a day under `/var/log/xgs/coprocessor`, kept as long as the others, shown on
  **XGS: Coprocessor Log**, and the last 256 KB of it in the support bundle. Each line is dated when
  it arrives - the coprocessor's times carry no zone, and its clock is wrong until this host sets it -
  and carries the sender's address as its origin, which is also what OPNsense's log viewer needs to
  read a line's date, severity and process: without it the whole line showed under Line.
- **When:** the start hook `72-xgs-npu-log` at the end of the boot, every 15 s for up to five minutes;
  cron every ten minutes after that, so a coprocessor taken up again in place is told again within
  ten minutes. While the forwarder's file holds this host's address, nothing is done. Telling it is
  said under the tag `xgs-npu-log`, on the Log File page.
- **Removal:** the uninstaller starts the forwarder again, sending nothing, before the redirect goes;
  otherwise every line would be refused by the default rule, and logged.

What the coprocessor writes before it is told - its own start - stays in its RAM; the console page,
XGS: Coprocessor Console, has that part. The vendor's script writes the forwarder's file from a second
file, which the coprocessor's start-up also reads to choose its path, so that second file is left
alone and the forwarder's own written directly.

Measured on 2026-10-28, on each appliance, with every piece above in place:

| appliance | vendor version | the coprocessor's forwarder | the log arrives by |
|---|---|---|---|
| the first XGS 3300 | 23 | 14,448 bytes, TCP with UDP while TCP is refused | TCP, one connection |
| the second XGS 3300 | 22 | the same binary | TCP, one connection |
| the XGS 136 | 21 | 10,320 bytes, UDP only | UDP |

On each, a line written with `logger` on the coprocessor was in the host's file as it was written,
dated, with its severity and process, and on the page. A coprocessor at rest writes almost nothing:
the first XGS 3300's wrote no line between 17:54 and 18:34 that day; what it writes comes with its
events - its start, an error, a port, a reset.

## Sources

The link itself - the rings in host memory, the status word, the polling - is on
[mvmgmt.md](mvmgmt.md), read out of Marvell's `pcinet` source and checked against the module
on the appliance; the measurements that led here are on
[issue 374](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/374); the coprocessor's
console and the vendor's tools on it are in [the family page](../families/armada.md); the
scripts are `src/opnsense/scripts/npuctl/mgmt-link.sh` and `mgmt-link.php`, the hook
`src/etc/rc.syshook.d/early/07-npuep`, the rules in `src/etc/inc/plugins.inc.d/xgs.inc`; the
board, with its photographs, is on [the hardware page](../hardware/xgs136.md).

## Proven on the XGS 136, 2026-10-14

The installer ran `assign` (`opt15 NPUMGMT 169.254.254.1/30`), a filter reload put the two rules
in the live set, `coprocessor` set the far address and `trust` the key, then the appliance was
rebooted. On the boot after: the link established at 38 seconds from the hook's `up`, OPNsense
gave `opt15` its address, `pfctl -sr` carried both rules, the bootup hook logged the coprocessor
answering, and `mgmt-link.sh ssh` returned from the coprocessor ninety seconds into its uptime.
Fourteen front ports and the WAN lease as before. Issue 374 closes on that.
