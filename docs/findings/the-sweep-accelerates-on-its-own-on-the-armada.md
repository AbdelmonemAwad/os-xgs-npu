<!-- reviewed: 2026-10-14 -->
# The sweep accelerates flows on its own on the ARMADA: flow.auto, a flow table, and a keepalive

2026-10-14, the XGS 136, step 4 of [the offload plan](../npuep/offload-plan.md), third slice.
[Slice 2](the-driver-accelerates-a-flow-on-the-armada.md) accelerated one flow by hand, with fixed
table indices, as a proof the chain works. This slice makes the driver do it by itself: with
`flow.auto` set, the sweep hands every settled, resolved connection to the fast path, keeps each
while pf still holds its state, and takes it back when pf lets go - the OCTEON TX driver's `dp.auto`
and `dp.flows`, on this family. Built on the appliance, measured there.

## What was added

- **A real flow table and next-hop table.** One flow owns one connection index and two next-hop
  indices, allocated from bounded tables that start at index 2 (0 and 1 are the bring-up's and the
  by-hand experiments', the reason the OCTEON TX driver gives) and keep a revision across reuse, so
  a stale command is refused by the far side rather than applied to whatever now holds the index.
  The quick field work is under the table lock; the sleeping work - the route lookup,
  `NHOP_PROGRAM`, `FLOW_CREATE_FP`, `MFLOW_INVALIDATE` - is done with that lock dropped, and
  make, takeout and the sweep are serialised by a sleepable lock so two never touch the tables at
  once. On any failure every index reserved is given straight back, so a refusal leaves nothing
  half-made.
- **`npuflow_make`**, the slice-2 accelerate generalised to allocate indices from the tables rather
  than the fixed 2/3/2 of the proof.
- **A keepalive.** Each sweep, for every live flow, pf is asked - by the tuple and the list that
  found the state - whether the state still exists; if it is gone, both microflows are invalidated
  and the connection and its next hops freed. A connection in hardware sends the host nothing, so
  without this a flow would forward past the firewall finishing with it. A pure-hardware TCP flow
  does not idle out of pf (its established timer is hours), so a steady connection does not churn;
  a short-lived one the keepalive takes out, which is correct.
- **`npuflow_automake`**: when `flow.auto` is set, every candidate the sweep found settled and
  resolved and not already a flow is handed over, a few a sweep, the rest next time. Once a flow is
  made its frames stop being punted, so its candidate ages out and is not tried again.
- **sysctls**: `flow.auto` (0/1), `flow.flows` (the live table), `flow.auto_made`, `flow.auto_gone`.

## Measured

With both ports' interface entries and the gate opened by hand, as in slice 2, and `flow.auto` set,
the sweep accelerated a firewall's worth of forwarded connections with no identity named by hand.
`flow.flows` read, over a run:

```
auto on | connections 11 next hops 22 | made 17 (auto 17) taken 6 gone 6 | flow-full 0 nhop-full 0
```

- **made 17 (auto 17)**: seventeen flows accelerated by the sweep itself, each a connection the
  firewall was already routing, with its translation read off pf's keys, both microflows active and
  forwarding, and its two next hops resolved from the host's own route and neighbour tables.
- **gone 6, taken 17**: the keepalive took flows out as pf let go of their closed states; the final
  `flow.auto=0` and `accelerate=off` took the rest. The table emptied cleanly to no connections and
  no next hops, with the full-table counters at zero - no index leaked.
- **`FPCNTR_FROM_WIRE_TO_WIRE` rose from zero** and climbed as the flows forwarded frames in
  hardware. This is the counter [slice 2 could not isolate](the-driver-accelerates-a-flow-on-the-armada.md);
  here it is plainly non-zero and rising. It was driven only by the connections' own light traffic,
  so it is ~~proof that accelerated flows cross the appliance in hardware~~ proof that they left the
  fast path by a wire, not a throughput figure. **Corrected 2026-10-18:** every next hop then named
  interface 8, an empty cage, so they left by PortF2 and arrived nowhere -
  [every next hop named an empty cage](every-next-hop-named-an-empty-cage-on-the-armada.md); delivered
  in hardware since that fix.
- A connection delivered locally to the appliance itself - not forwarded - resolved no next hop and
  was never accelerated, so the management path stayed up throughout.

Nothing crashed, through the whole run and a deliberate contention stress, because the control
channel's lock was made sleepable first - [a sleeping thread held the rpc lock](a-sleeping-thread-held-the-rpc-lock.md).
On restore every interface entry and the gate were put back and the table emptied.

## What this establishes, and what is left

The ARMADA driver now accelerates the firewall's flows on its own, keeps a table of what it has
handed over, and takes each flow back when the firewall is done with it - the whole of the OCTEON
TX driver's automatic datapath, on the other family, bounded for a bench. What remains in this step:
the revision barrier that discards the table on a ruleset reload (`fw_rev_bump`), sharing one
next-hop entry between connections that leave the same way, and the throughput number on a quiet
single-flow bench, where the hardware-forwarded count separates cleanly from other traffic. IPsec
on the ARMADA is the step after.
