# The management link gets its place in OPNsense

A plan, written 2026-10-14 for [issue 374](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/374)
and reviewed before anything is implemented. Measured on the XGS 136 that day: the link over PCIe
is whole in both directions once the host brings `mvmgmt0` up, and OPNsense's default deny drops
every frame the coprocessor sends in, because the interface is nobody's. The driver needs no
change; the plugin does.

## What the plugin does at install, and at every boot

1. **Assigns the interface.** `mvmgmt0` becomes an OPNsense interface named `NPUMGMT` with a
   static IPv4 address, `10.255.255.1/30`, and no IPv6. The coprocessor's end gets
   `10.255.255.2` from the plugin over the console at boot, because its root filesystem is
   read-only and forgets at every reset; `/persistent` on the coprocessor is where that address
   lives once the plugin may write there. The pair is private, fixed, and never routed.
2. **Admits the coprocessor.** One firewall rule on `NPUMGMT`: pass in from `10.255.255.2` to
   the interface address, ICMP and TCP 22, keep state, logged. Nothing else enters by this
   interface; nothing from it is forwarded anywhere.
3. **Brings it up.** `07-npuep` (the early hook that loads the driver) ends with
   `ifconfig mvmgmt0 up` once the module is loaded, so the link establishes before OPNsense
   configures its interfaces and the assignment finds the link already carrying a lease-free
   static address.
4. **Trusts the host.** The host keeps one key for the coprocessor, `/root/npu/id_ed25519_npu`,
   made at install. The coprocessor's `sshd` reads `/root/.ssh/authorized_keys` on a read-only
   filesystem, so the plugin places the public half on `/persistent` and bind-mounts or links it
   in at the coprocessor's boot through the vendor's own `xgs-mount-persistent.sh` path - the
   exact step is read from that script before it is written, not guessed.
5. **Uses it.** Everything this project now does over the console at 115200 - the vendor's
   `xgs-*` tools, `mvsw`, the platform store, the sensors - runs over `ssh root@10.255.255.2`
   with that key, at PCIe speed, with the console kept as the fallback and the boot log.

## What is not done

- The interface is never bridged, never given a gateway, never offered to the WAN or the LAN.
- No DHCP on either end. No IPv6 on either end.
- The vendor's firmware image on the coprocessor is not rewritten; only `/persistent` is used.
- Nothing is driven on the coprocessor by the plugin's own initiative; the link is for reading
  and for tools the owner runs.

## Order of work

1. Read `xgs-mount-persistent.sh` and the coprocessor's boot scripts for how `/persistent` is
   applied, and confirm the key placement on the bench with the console still open.
2. The hook's `up` and the assignment, by the installer; the rule, by the installer; measured on
   the XGS 136 with a reboot: the link up before the firewall is configured, the ping both ways,
   the ssh.
3. The console tools move to ssh with the console as fallback.
4. Close 374 on the measurement.
