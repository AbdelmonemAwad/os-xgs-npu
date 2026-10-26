<!-- reviewed: 2026-10-26 -->
# The coprocessor taken up again in place, on both families, 2026-10-26

[768](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/768): when the coprocessor's fast path
stops, the host stays up and takes it up again in place, and restarts the appliance only if that
fails - the default since 0.1.0-alpha.8.6 ("Take it up again, or restart", XGS: Settings). What is
measured here is 0.1.0-alpha.8.7 - and, with IPsec associations on an OCTEON TX coprocessor, the
octep.ko and `recover.sh` of the change after it - as an appliance meets it: the fast path stopped on
purpose on the coprocessor, and nothing done by hand after that - `scripts/xgs/fastpath.py`, from
cron, sees the driver take the coprocessor as gone and runs `scripts/xgs/recover.sh`. The PC asks the
appliance every 3 s how its driver stands and prints a line when the answer changes; after the
heartbeat answers again it prints what `recover.sh` logged, whether the host was restarted (its boot
time before and after), the management link, and a ping through the appliance.

What the runs before these found, and what changed for them, is in
[appliance-defaults.md](../octep/appliance-defaults.md) under "When the fast path stops".

## The first XGS 3300, vendor version 23 - the console asked to reboot it

Its coprocessor's fast path alone was stopped and its watchdog feeder left running, so its own
watchdog never resets it: the case where `recover.sh` has to ask the coprocessor's console to
reboot it (`images/deploy/lab-recover-3300-photo.sh`, octep.ko `dabcfa58`).

![The first XGS 3300 on 0.1.0-alpha.8.7: its fast path stopped at 20:20:13; taken as gone at 30 s; fastpath.py's recovery from 20:21:00 - its console asked at 20:23:00, the new boot waiting for the host at 20:23:17, the driver up at 20:24:35, the fast path answering again 250 s into the recovery; the host's boot time unchanged, not restarted; the management link up; a ping from the PC through its bridge and one from the second XGS 3300 to oxp0, none lost](../images/measurements/coprocessor-recovery-2026-10-26/01-vendor23-first-3300-console-reboot.png)

The fast path stopped at 20:20:13; the driver took the coprocessor as gone at 30 s; the next
minute's check started the recovery at 20:21:00. The coprocessor had not restarted on its own in two
minutes, so its console was asked at 20:23:00 (`busybox reboot -f`); its new boot was waiting for the
host 17 s after that; the driver was up at 20:24:35 and the fast path answered again at 20:25:10 -
250 s into the recovery, 297 s after the stop. The host was never restarted, its management link
came back, and both pings went through without a loss.

## The XGS 136, vendor version 21 - reset from the host first

Its coprocessor's fast path and its watchdog feeder were both stopped, as a coprocessor that hangs
(`images/deploy/lab-recover-136-photo.sh`, npuep.ko `74dc7bce`). The PC reaches the 136 over its LAN,
which runs through the coprocessor's own ports, so from the stop until the driver is up again the
136 does not answer it.

![The XGS 136 on 0.1.0-alpha.8.7: its fast path and watchdog feeder stopped at 20:16:33; the host unreachable over its LAN from 7 s; fastpath.py's recovery from 20:18:01 - the coprocessor reset from the host, its config space answering at once, the driver unloaded and loaded, up at 20:18:51, the fast path answering again 86 s into the recovery; the host's boot time unchanged, not restarted; the management link answering; the receive path with no frame refused](../images/measurements/coprocessor-recovery-2026-10-26/02-vendor21-136-reset-before-unload.png)

The stop at 20:16:33; the recovery from 20:18:01: the coprocessor reset from the host through its
MCP2210, its config space answering within the second, the driver unloaded and loaded while it
answers, OPNsense's interfaces reloaded at 20:18:51, the fast path answering at 20:19:27 - 86 s into
the recovery, 174 s after the stop. The host was never restarted, the management link answered, and
the receive path had refused no frame.

The order is the boot's own - the coprocessor reset first, the driver changed after - because the
two other orders failed on this board the same day: an unload with the coprocessor still running,
which its own watchdog reset part-way through, froze the host until its hardware watchdog restarted
it (the chipset recorded bit 25); and an unload with the coprocessor held in reset kept its config
space as it read then, all ones, for the next load to write back - that load brought no port up, and
the appliance was restarted cleanly.

## With IPsec associations on an OCTEON TX coprocessor

The same XGS 3300 at 20:01, on 0.1.0-alpha.8.7's octep.ko `dabcfa58`: IPsec associations had been put
on its coprocessor that boot (at 18:52 and 19:48, on oxp0), and the kernel's ESP transform pointed into
octep. The recovery found the coprocessor waiting for the host after its console's reboot, as above,
and then:

    octep0: an IPsec association's transform points into this module: detach refused until the host reboots

so `recover.sh` gave up and `fastpath.py` restarted the appliance cleanly; the host answered again
350 s after the stop - the fallback as it was written then. (Not photographed: the script then in use
misread the boot times and said "not restarted"; it was corrected before every run on this page.)

The module still cannot be unloaded once an association's transform points into it - the kernel
copies an association, transform and all, without telling the driver (`contrib/octep/octep_ipsec.c`) -
but its device need not stay. Since octep.ko `54a32be8` the module refuses `kldunload` itself, and
`recover.sh` detaches the device and attaches it again with the module loaded, its copy of the
transform staying where the associations point; an association with no record on the device attached
again goes to the kernel's cipher. The same XGS 3300, a tunnel's two associations on its coprocessor
(`images/deploy/lab-recover-3300-ipsec-photo.sh`):

![The first XGS 3300 on octep.ko 54a32be8: the tunnel's two associations mirrored on the coprocessor, five pings into it encrypted there; its fast path stopped at 20:45:36, taken as gone at 30 s; fastpath.py's recovery - the console asked, the new boot waiting for the host, the driver staying loaded with IPsec associations pointing into it, its device detached and attached again, the fast path answering 279 s into the recovery; not restarted; the management link up; five pings into the tunnel encrypted by the kernel, the second XGS 3300's inbound association 420 bytes more; both pings through the appliance answered](../images/measurements/coprocessor-recovery-2026-10-26/03-vendor23-first-3300-ipsec-device-reattached.png)

Five pings into the tunnel before the stop were encrypted on the coprocessor. The recovery went as in
the first run, until the driver stayed loaded and its device was detached and attached again; the fast
path answered 279 s into the recovery, and the host was not restarted. After it, five pings into the
tunnel were encrypted by the kernel - the associations have no record on the device attached again -
and the second XGS 3300's inbound association counted 420 bytes more, five pings of 84. A rekey after
that put the new pair on the coprocessor again and five more pings were encrypted there; four minutes
on, the kernel's log held no error, the associations left behind had been deleted, and `kldunload`
was still refused.

The second XGS 3300 (vendor version 22), with associations of two tunnels on its coprocessor, its
fast path and watchdog feeder both stopped. The bench reaches it through its coprocessor's F1, so the
run was made on the appliance itself (`images/deploy/m768-3300new-t1.sh`, a line a second to its
disk), and its record photographed once it was back:

![The second XGS 3300 on octep.ko 54a32be8, its own record: usfp and the watchdog feeder stopped; the heartbeat dead and the coprocessor gone at 28 s, alive again at 209 s; recover.sh - the coprocessor booted again by its own watchdog and waiting for the host, the driver staying loaded with IPsec associations pointing into it, its device detached and attached again, the fast path answering 202 s into the recovery; two associations mirrored on its coprocessor again; the management link up; the host's boot the same as the run's](../images/measurements/coprocessor-recovery-2026-10-26/04-vendor22-second-3300-ipsec-device-reattached.png)

At 16:09 the same day, on the module before, its recovery had ended in "the driver did not unload; not
taken up in place" and a restart. Now: its own watchdog restarted the coprocessor, the device was
detached and attached again, and the fast path answered 202 s into the recovery, the host not
restarted, two associations on its coprocessor again and the management link up.

## What these do not show

- A coprocessor whose watchdog feeder alone stops on an XGS 3300, its fast path still answering: the
  heartbeat stays alive, the driver does not take the coprocessor as gone, and its watchdog resets it
  60 s later under a driver still reading it - the freeze #768 began with. Nothing on the host sees it
  coming.
- A tunnel carrying traffic through the detach itself: what went through it here was pings, before and
  after.
- The second XGS 3300 without a tunnel on its coprocessor, by the console's reboot: the run above had
  its own watchdog restart it.
