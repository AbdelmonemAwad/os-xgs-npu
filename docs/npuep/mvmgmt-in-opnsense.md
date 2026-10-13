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
   `/root/.ssh/authorized_keys` - one line, once, guarded by its own comment. ~~The plan said this
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
- Nothing is driven on the coprocessor by the plugin's own initiative; the link is for reading
  and for tools the owner runs.

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
