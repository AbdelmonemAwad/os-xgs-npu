<!-- reviewed: 2026-11-03 -->
# The XGS 136's host panics at the driver's unload, 2026-11-03

[988](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/988): on 2026-11-02 the XGS 136's host stopped
after its coprocessor had been taken up again in place, and left nothing to say why - its kernel waited
in the debugger after a panic, if it was one, until the hardware watchdog restarted it. For the runs of
2026-11-03 `debug.debugger_on_panic` was 0, so that a panic writes its dump and restarts the host. The
lab's XGS 136 (vendor version 21), the same stop as in [the runs of 2026-11-02](coprocessor-quiesce-2026-11-02.md)
(`images/deploy/lab-768-silent.sh`: the coprocessor's getty and watchdog feeder stopped, its end of the
management link down; its own watchdog resets it and `fastpath.py` takes it up again in place), and a
logger on the host reading `dev.npuep.0.heartbeat.state` every half second.

## A sysctl read through the unload

The first run (stop 06:13:30 UTC) did not get to the end of the recovery: the host panicked at 06:15:03,
as `recover.sh` unloaded npuep, and the dump says how.

![The XGS 136, 06:13:30 UTC, npuep.ko 12b01440: the stop, the recovery, and the host back at 06:16 with a new boot time - booted again; the logger's last line in the recovery, the heartbeat dead and recover.sh running](../images/measurements/npuep-unload-panics-2026-11-03/01-136-a-panic-in-the-recovery.png)

    Fatal trap 12: page fault while in kernel mode
    fault virtual address   = 0x488
    current process         = 62789 (sysctl)
    __mtx_lock_sleep() at __mtx_lock_sleep+0xc9
    npugiu_sysctl_hb_state() at npugiu_sysctl_hb_state+0x42
    sysctl_root_handler_locked() at sysctl_root_handler_locked+0x91

A read of the heartbeat's state took the lock of a facility already torn down. Every sysctl of npuep
was in the device's own context, which newbus frees only after the driver's detach returns, while the
facilities' detaches destroy the locks most of their handlers take before that. A destroyed mutex reads
`MTX_DESTROYED`, whose owner bits are a null thread, and the lock's slow path reads a field of that
owner: the fault at 0x488. octep had the same, and has kept its sysctls in a context of its own, freed
first in detach, since [2026-10-31](coprocessor-stop-capture-2026-10-31.md); npuep never had. The
reader was the logger, but `fastpath.py` reads the heartbeat every minute, and anything reading the
driver's sysctls meets the same window.

The same run again at 06:35:31 with that module panicked the same way at 06:37:03 - the logger alone
reading; its label says four loops, which were not started (the series' wrapper did not pass the
setting on).

![The XGS 136, 06:35:31 UTC, npuep.ko 12b01440 again: the host restarted in the recovery once more, the logger's last line with the heartbeat dead and recover.sh running - labelled four loops, which did not run](../images/measurements/npuep-unload-panics-2026-11-03/02-136-the-same-panic-again.png)

**The change.** npuep now makes a context of its own at the top of its attach, every facility adds its
sysctls there, and its detach frees it before anything else (`npuep_sysctl_ctx`); `sysctl_ctx_free`
waits for a handler already running, and no new one starts. Built as npuep.ko `fe1fcd89` and put on the
XGS 136, then run four times with four loops reading the heartbeat's state and the gone latch as fast as
they could besides the logger:

- **06:47:31, a recovery in place:** 1,491,245 reads by the loops; the coprocessor taken up again in place
  in 94 s, the host up throughout.
- **06:55:02, a recovery in place:** the host panicked at 06:57:03 - not in a sysctl handler: see below.
  This run's photograph prints, under "the crash it left", the trace of the 06:37 panic, which the
  message buffer still held - the script read the first trap in it, not the last (corrected); its own
  trace is in the photograph of all four below.
- **07:04:38, `contrib/npuep/reload.sh`** (unload, reset, load): 324,758 reads; the module back, the
  heartbeat alive, the bridge with its ten members, the host up.
- **07:07:04, the same reload:** the host panicked at 07:07:09, three seconds into the unload, the same
  way as at 06:57. The `reload.sh` lines in its photograph are the 07:04 run's - the file as the disk
  held it when the panic came, before this run had written to it - and its first line is cut off.

![The XGS 136, 06:47:31 UTC, npuep.ko fe1fcd89 with four loops: 1,491,245 reads, the coprocessor taken up again in place in 94 s, the host never restarted](../images/measurements/npuep-unload-panics-2026-11-03/03-136-the-change-through-a-recovery.png)

![The XGS 136, 06:55:02 UTC, npuep.ko fe1fcd89 with four loops: the host restarted in the recovery; the trace printed under it is the 06:37 panic's, read from the message buffer by mistake - its own is an IPv6 one](../images/measurements/npuep-unload-panics-2026-11-03/04-136-the-change-a-panic-in-ipv6.png)

![The XGS 136, 07:04:38 UTC, npuep.ko fe1fcd89 reloaded under four loops: 324,758 reads, the module back, the heartbeat alive, bridge0 with its ten members](../images/measurements/npuep-unload-panics-2026-11-03/05-136-the-change-through-a-reload.png)

![The XGS 136, 07:07:04 UTC, the same reload: the host restarted 5 s in; the reload.sh lines shown are the previous run's file, left on the disk by the panic; the panic in IPv6 source selection, by unbound](../images/measurements/npuep-unload-panics-2026-11-03/06-136-the-change-a-reload-and-the-ipv6-panic.png)

In none of the four did a sysctl handler fault.

## The other panic: IPv6 source selection

The four dumps, each with its own trap - a dump's message buffer carries the earlier boots' text too,
and its own trap is the one with the latest time:

![The four crash dumps of the morning, each with its own trap: 06:15:03 and 06:37:03 in npugiu_sysctl_hb_state, a sysctl process, at 0x488; 06:57:03 and 07:07:09 in in6_selectsrc, unbound, at 0x10, each as bridge0 went down](../images/measurements/npuep-unload-panics-2026-11-03/07-136-the-panics-as-the-kernel-wrote-them.png)

The second kind is FreeBSD's own IPv6 path, by unbound connecting a UDP socket for a query, as the
front ports' interfaces were detached:
[994](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/994). In FreeBSD 15.1 an interface's detach
flushes its routes and then sets its IPv6 data to NULL at once, waiting for the network epoch only before
freeing it; a thread that looked a route up before the flush still has the interface, and
`in6_selectsrc()` reads `ND_IFINFO()` of it - the third pointer of that data, at offset 0x10. The same read
is in `in6_selecthlim()`, which [755](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/755)'s panic on
an XGS 126 went through. The change above does not touch it; 994 is where it is taken on.

## What remains

- [994](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/994): the IPv6 panic at the unload - the
  driver to purge its interfaces' addresses and routes and wait for the network epoch before detaching
  them, in npuep and in octep.
- The stop of 2026-11-02, two and a half minutes after a recovery that had ended, is not explained by
  either: nothing was being unloaded then, as far as its logger shows.
- Not run on the other ARMADA models, nor with the GUI open through a recovery.
