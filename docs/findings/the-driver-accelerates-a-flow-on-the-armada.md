# The driver accelerates a flow on the ARMADA: FLOW_CREATE_FP, and the microflow goes active

2026-10-14, the XGS 136, step 4 of [the offload plan](../npuep/offload-plan.md), second slice.
[Slice 1](the-flow-table-on-the-armada-begins.md) read what a flow would take to accelerate; this
one hands a flow to the fast path and reads back that it took. Module built on the appliance; by
hand through a sysctl, one flow at a time - the sweep making flows on its own is a later slice.

## What was added

`npurpc_send` gives the flow code a locked way to post a command. `npuflow.c` gains the microflow
and `NHOP_PROGRAM` builders and `npuflow_accelerate`, driven by
`dev.npuep.0.flow.accelerate=<id>` (and `=off`): it reads the candidate's pf state, derives the
connection's original and reply tuples, finds the reply direction's candidate so both directions
can be named, resolves and programs both next hops, and sends one `FLOW_CREATE_FP` carrying the
connection - with the SNAT read off pf's two keys - and both microflows set `MF_ACTIVE`, action
forward, pointing at the next hops. All of it is the OCTEON TX driver's byte layout, confirmed
here command by command in the earlier slices.

## Proven at the microflow

With a settled, translated, resolved candidate chosen, `flow.accelerate` reported the flow handed
over - original microflow out one next hop, reply out the other, over one connection - and
`FLOW_CREATE_FP` was accepted. Reading the microflow back (`LO_MFLOW_READ` with `READ_ALL`):

```
host_valid 1   state 2 (MF_ACTIVE)   action 1 (forward)   next hop set
```

That is the state the XGS 3300 reaches when it accelerates a flow: the host has loaded the entry,
it is active, its action is to forward, and it points at the next hop the driver resolved and
programmed. The fast path forwards a frame that matches an entry in that state in hardware, which
is the whole of what acceleration is. Nothing crashed; traffic stayed up; `=off` invalidated both
microflows and every interface entry and the offload gate were put back.

## What this does and does not establish

It establishes that the ARMADA driver can build the whole of a flow - the connection with its
translation, both microflows active and forwarding, both next hops - from what pf and the routing
table hold, and that the coprocessor accepts it and marks the microflow active. It is the same
chain the OCTEON TX driver runs, on the other family.

It does not yet show the accelerated flow's throughput, nor the counter of frames the fast path
forwards for it: isolating that needs a quiet bench with a single sustained flow, where the
hardware-forwarded count separates cleanly from other traffic. Two things are also deliberately
left for later slices: a flow is only accelerable while both of its ports have offload enabled
(here done by hand per flow), and the sweep does not yet make flows on its own - that, the
connection table that lets a flow be taken back and remade, and IPsec, are the remaining work.
