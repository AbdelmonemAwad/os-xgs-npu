# A flow table, a sweep, and the panic that came with them

Built and measured on an XGS 3300, 2026-10-05. **It panicked the appliance once**, which is the
most useful thing in this page.

## What is here

A flow table of 64 entries, so a flow gets an index of its own rather than every flow being
programmed at index 1 and silently replacing the last. And a sweep on the link-poll task that asks
`pf` about each one and takes out of `MF_ACTIVE` any whose state has gone.

The sweep is the half that makes the other half safe. A microflow's own timeout is sixty seconds,
which is a long time for a connection the firewall has finished with, and a five-tuple reused inside
that window would be forwarded on the strength of a connection that no longer exists. **That is the
one failure here worse than no offload at all.**

Taking a flow out means setting its state to something other than 2, not deleting it. The entry
belongs to the fast path - it made it when it first saw the flow and will reuse it - and what the
host owns is only whether it is used.

Indices start at 2. The hand experiments that found all of this used index 1 and `bringup.sh` uses
0, and a collision there would look exactly like the fast path misbehaving.

## The panic

The first version faulted the appliance within seconds of being enabled. From its own textdump:

```
Fatal trap 12: page fault while in kernel mode
panic: page fault
octep_pf_state_exists() at octep_pf_state_exists+0xcb
octep_dp_link_poll() at octep_dp_link_poll+0x1da
```

**`pf`'s lookups and the routing table are reached through VNET variables**, which resolve against
`curvnet` - and a taskqueue thread has none set, so every one of them dereferences a null base.
`CURVNET_SET(vnet0)` around the sweep and the automatic path is the fix, and `NET_EPOCH_ENTER` around
`fib4_lookup` went in beside it, because a `nhop_object` is borrowed and valid only inside the epoch.

**What makes this worth writing down is that the same call had been correct for days.** Every
earlier use of `octep_pf_state_exists` was from a sysctl handler, which runs in a process context
that already has a vnet, and worked perfectly. It faulted on the first run from a task. A function
that is safe on one path and fatal on another, with no difference in how it is called, is the kind
of defect that review catches and testing does not.

## And the real fault was mine before that

Three pieces went into one build - the allocator, the sweep and an automatic trigger - and that
build went onto a live firewall. This repository already holds the rule that was broken:
*hardware passing is not proof of correctness; review before hardware.* **Had the sweep gone in
alone, the fault would have appeared in one task with nothing else to confuse it**, which is exactly
what happened when it was finally tested that way.

## The sweep, measured alone

With `dp.auto` off throughout, so the only new thing running was the sweep:

| | |
|---|---|
| the task over an empty table | two minutes, no fault |
| one flow by hand | `flows=1` - the allocator works |
| ten seconds with a live flow in the table | **still answering** - the sweep asked `pf` from the task and survived, which is exactly what killed the first version |

Then left alone for two minutes with two flows in the table:

```
 0s: flows=2 gone=0
40s: flows=1 gone=1     a DNS state expired, and its flow was taken out
100s: flows=1 gone=1    the other flow's state is still alive, and it was kept
```

**It removed what should go and kept what should stay**, which is the whole of its job.

## What is not done

**The automatic trigger is still off and still untested.** `dp.auto` exists and defaults to 0. It
should stay there until the sweep has run for longer than one afternoon.

**A bridged destination cannot be resolved yet.** The appliance's own front ports are members of a
host bridge, and a route to a machine behind it resolves to `bridge0`, which is not a port this
driver owns:

    no next hop for 0x7864a8c0: the route leaves by an interface this driver does not own

Finding which bridge member holds an address needs asking the bridge, which has no exported
in-kernel lookup - and building a learning table of our own is the thing `octep_nhop.c` already
explains why not to do.

**Resolved the next day, and not by a lookup.** The answer arrives on every frame: see
[the bridge cannot be asked](the-bridge-cannot-be-asked.md).

## Lesson

The panic and the thing it broke were both in the same sentence: **a call that is correct from one
context and fatal from another**. Nothing about `octep_pf_state_exists` changed; only who called it.
The context a kernel function needs is rarely in its signature, and a function that has been working
for days carries no evidence at all about a path it has never been on.
