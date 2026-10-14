# A sleeping thread held the RPC lock: the control channel's lock had to be sleepable

2026-10-14, the XGS 136. Building the automatic sweep of [the offload plan](../npuep/offload-plan.md)'s
step 4 turned a latent fault in the control channel into a reliable panic, and fixing it is a change
to `contrib/npuep/npurpc.c` worth its own page.

## The panic

The first boot of the module that posts flow commands from the sweep panicked, in the kernel
debugger:

```
panic: sleeping thread holds npurpc
```

The backtrace named two threads. The owner, asleep, was the RPC channel's own taskqueue
reprogramming the interface table at start-up:

```
pause_sbt() ... npurpc_command() ... npurpc_task() ... taskqueue_thread_loop()
```

The contender, blocking on the same lock, was a `sysctl` thread reaching `npurpc_sysctl_command`.

## Why it is a fault, and why it was hidden

The control channel is a DMA ring the host polls - there is no inbound doorbell, so a command is
posted and then `npurpc_command` waits for the answer with `pause()`. That wait was held across the
channel's lock, which was an ordinary `MTX_DEF` mutex. FreeBSD forbids sleeping while holding a
default mutex: the moment a second thread blocks on a mutex whose owner is asleep, the priority
machinery (`propagate_priority`) panics rather than deadlock. So the design only ever worked while
nothing contended the lock - the start-up port-programming task and a stray sysctl rarely overlap.

The automatic sweep removes that luck. It posts `NHOP_PROGRAM` and `FLOW_CREATE_FP` from its own
taskqueue, now beside the port-programming task, the network agent and any `sysctl` that reads the
channel - so the owner is asleep in `pause()` with something waiting on the lock often, and the
panic becomes the common case rather than the rare one.

## The fix

The channel's lock becomes a sleepable `sx`. Holding an `sx` across `pause()` is legal, and a second
caller waits on it through a mechanism that does not involve the sleeping-owner panic. Nothing else
about the channel changes: a command still holds the lock exclusively from the moment it is posted
until the target answers, so two commands never share the single command buffer.

This is safe because of who calls the channel. Every caller is a taskqueue or a sysctl - all free
to sleep, as they must be, since the wait already sleeps - and none holds another lock across the
call. The flow code in particular drops its own table lock before every `npurpc_send`; it reserves
its connection and next-hop indices under that lock, releases it, and only then posts the commands.

## Proven

The fixed module booted clean through the same start-up reprogram that panicked before, with no
crash recorded. Under load it was pushed on purpose: forty rounds of the by-hand command sysctl and
the channel's probe, hammered concurrently with the sweep accelerating real connections (so the
sweep was posting commands the whole time), and the box stayed up. The panic does not recur.

The lesson is the OCTEON TX driver's own, from its network agent long ago - a periodic task that
sends a command and waits for it may not run under a non-sleepable lock - met again on this family,
in a different facility, because a second poster finally contended the one that was already there.
