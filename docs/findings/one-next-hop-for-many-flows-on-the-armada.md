# One next hop for many flows on the ARMADA: the next-hop table is shared

2026-10-14, the XGS 136, step 4 of [the offload plan](../npuep/offload-plan.md), fifth slice. The
first auto sweep gave every flow its own two next-hop entries; this makes flows that leave the same
way share one, so the table no longer runs out before the connection table does. It is the OCTEON TX
driver's `octep_nhop_get`, on this family.

## Why it matters

A next-hop entry is the resolved way out: an egress interface, an MTU, and the two MACs. Every flow
to the internet resolves to the same answer - out the WAN port, to the WAN gateway's MAC - and every
reply to a given opener resolves to that opener's MAC on the LAN. With a fresh entry per flow
direction, the bounded next-hop table filled at about sixty flows while the connection table sat
nearly empty. Sharing makes the next-hop table hold one entry per distinct way out, not per flow.

## What changed

The next-hop entry grew the key it is matched on - interface, MTU, both MACs - and a reference
count. `npuflow_nhop_get` scans for an entry with that key: a match raises its count and returns the
existing index and revision, already programmed; a miss reserves a free slot, stores the key, and
asks the caller to post `NHOP_PROGRAM` once. Takeout drops the reference, and the slot is freed only
when its last flow lets go. The sweep serialises every maker, so a freshly reserved entry is always
programmed before another maker's lookup can match it. The revision is kept across reuse, as the far
side checks it beside the index.

## Measured

With the sweep accelerating a host's forwarded connections to many internet destinations:

```
connections 11   next hops 2   made 11 (auto 11)   nhop-full 0
```

Eleven connections, two next-hop entries: every forward direction shared the one entry for the WAN
gateway, every reply shared the one for the opener. Without sharing that is twenty-two entries; the
flow ceiling is now the connection table, not the next-hop table. Nothing crashed; on restore the
table emptied with no entry leaked.
