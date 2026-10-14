# The driver's flow table on the ARMADA begins: the candidates, and the vnet that killed the sweep

2026-10-14, the XGS 136, step 4 of [the offload plan](../npuep/offload-plan.md), first slice. Where
[step 3](a-flow-dropped-by-the-armada.md) programmed one microflow by hand, this is the driver
starting to keep its own side of the fast path's flows - and nothing is sent to the coprocessor
yet. The host's side of a flow is the same on both families, so the OCTEON TX driver's pf and
next-hop code move here unchanged in substance.

## What was added

- `contrib/npuep/npupf.c` and `npunhop.c`: the OCTEON TX driver's `octep_pf.c` and
  `octep_nhop.c`, renamed. Neither is about either coprocessor: one asks pf, through the two
  symbols it resolves, what state it holds for a tuple, whether that state is settled, how it is
  translated; the other resolves a destination to a front port and a neighbour MAC through the
  routing table and ARP, with the bridge hint for a LAN destination behind the member ports.
- `contrib/npuep/npuflow.c`: the candidate table. The receive path pushes every punted frame whose
  metadata names a microflow - its tuple and identity - into a bounded table, and a one-second task
  asks pf and the routing table what each candidate would take to accelerate. `npugiu.c` gains the
  port accessors the next-hop code needs, the receive-path hook, and the attach and detach of the
  flow side. The result is on `dev.npuep.0.flow.candidates`.

## The crash, and the lesson it was already written down

The first build booted - the hook is dormant while the offload gate is closed - but the moment the
gate was opened by hand and real traffic made a candidate, the box took a page fault. The
appliance's own textdump named it exactly:

```
panic: page fault
pf_find_state_all+0x36
npuep_pf_state_read+0x18d
npuflow_sweep+0xca        thread "npuflow"
```

pf's state hash is per-VNET, resolved against the current thread's vnet, and a taskqueue thread has
none set - so the first lookup from the sweep dereferenced a null base. This is the identical fault
`contrib/octep/octep_dp.c` records from its own textdump, and the lesson of an earlier panic on the
OCTEON TX box: a driver taskqueue that calls into pf or the routing table must
`CURVNET_SET(vnet0)` first. The fix is that wrap around the sweep. The running box was held safe
while the fix built by stopping the task - `sysctl dev.npuep.0.flow.period=0` - rather than by a
reboot.

## Slice 1, working

With the fix in and the gate open, a handful of pings, a DNS query and one HTTP fetch from a
machine on a front port, the box steady and every ping returned, `flow.candidates` read:

```
 1 id 127 proto 1  10.0.0.2:1    -> <internet>:8   | pf state settled dir out snat | nhop port 8 iface 10 tag 0x0001 mtu 1500 <gateway MAC>
 6 id 124 proto 6  10.0.0.2:56850 -> <web>:80       | pf state dir out snat        | nhop resolved, same next hop
 2 id 62  proto 17 10.0.0.2:63003 -> <resolver>:53  | pf state dir out snat        | nhop resolved
 8 id 59  proto 6  10.0.0.2:56851 -> <this box>:22  | pf none                      | nhop error
```

The sweep tells the accelerable from the not: the flows out to the internet are settled, their
source translation is read off pf's two keys, and their next hop resolves to the WAN gateway; the
traffic to the appliance's own address has no front-port route and is left alone. So the ported pf
reads the state and orients the NAT, and the next-hop code resolves the way out - the whole host
side of a flow, on the ARMADA, with the logic unchanged from the OCTEON TX driver. Nothing was sent
to the coprocessor; the next slice turns a settled, resolved candidate into a `FLOW_CREATE_FP`.
