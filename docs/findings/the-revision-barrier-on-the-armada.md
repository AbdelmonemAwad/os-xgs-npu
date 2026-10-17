<!-- reviewed: 2026-10-14 -->
# The revision barrier on the ARMADA: a ruleset reload discards every offloaded flow

2026-10-14, the XGS 136, step 4 of [the offload plan](../npuep/offload-plan.md), fourth slice.
[The sweep](the-sweep-accelerates-on-its-own-on-the-armada.md) now accelerates flows on its own; this
slice makes a firewall ruleset reload take them all back, so the coprocessor never forwards under
rules that no longer exist. It is the OCTEON TX driver's `rpc.fw_rev_bump`, on this family.

## The mechanism

Every flow carries a sixteen-bit firewall revision, written into the microflow's revision field
beside the connection revision. `flow.fw_rev_bump` raises the driver's revision and posts
`FW_STATE_REV_SET` (command 0, two bytes - the new revision). The far side answers that one command
by invalidating its entire microflow table at once: one write discards every offloaded flow, which
is exactly, and only, what a ruleset reload has to do. The driver then forgets its own table -
nothing is sent, because the far side has already dropped those flows - and the sweep remakes what
the new rules still allow, each flow stamped with the new revision.

The OPNsense side is one line in the firewall hook. `xgs_firewall()` runs inside
`filter_configure_sync()`, where every reload path converges - the GUI, the API, the three reloads
during boot, a WAN lease renewal, a routing change - and writes the bump sysctl. It fires before the
new ruleset loads, so the hardware table is emptied while the old rules are still filtering, never
the other way round. The write is muted and its result ignored: a box with no coprocessor, or one
whose module has not handshaken yet, is a normal case, not a failure. The OCTEON TX appliance has
`octep` and this one has `npuep`; the hook writes both and the write to the absent driver is a no-op.

## Measured

With the sweep accelerating the host's forwarded flows, writing `flow.fw_rev_bump`:

```
before   connections 2  made 2 (auto 2)  fw_rev 0  bumps 0/0
bump     connections 0  made 4           fw_rev 1  bumps 1/0     table forgotten, far side voided
later    connections 3  made 7 (auto 7)  fw_rev 1                remade, slots reused at a new rev
```

And through OPNsense end to end - a real `filter reload` with the hook in place - `fw_rev` rose
again and the table was remade at the new revision. Before the hook was updated, the same reload
bumped only the OCTEON TX sysctl (a no-op on this appliance) and the revision did not move: the proof
that the hook, and nothing else, is what drives it. Nothing crashed; the bump posts its command
through the control channel, which [a sleeping thread no longer stalls](a-sleeping-thread-held-the-rpc-lock.md).

## What is left

The revision barrier completes the auto datapath's safety: flows made on their own are discarded on
their own when the ruleset changes. What remains in this step is sharing one next-hop entry between
connections that leave the same way, and the throughput number on a quiet single-flow bench. IPsec on
the ARMADA is the step after.
