# The resolver asked lo0 for a neighbour: `looutput: af=35 unexpected`, once a second

2026-10-14, the XGS 136, found by the owner on the appliance's console while the IPsec work of the
day was being measured; issue #402. Not the coprocessor's: the host driver's.

## What the console said, and what it is

```
looutput: af=35 unexpected
```

once a second, without end. `looutput` is the kernel's loopback output, and 35 is `AF_ARP`: the
loopback interface was being handed an ARP packet, which it cannot send and says so.

## Where it came from

`npuep_nhop_resolve` (contrib/npuep/npunhop.c) looks the destination's route up, takes the route's
interface, and asks that interface for the neighbour (`arpresolve`). It did not check that the
interface was one a neighbour can be asked on. A destination the appliance itself owns routes by
`lo0`; the resolver's hint - the port a candidate's frames arrive on, meant for routes that leave by
the bridge - then stood in for the port, and `arpresolve` on `lo0` ended in an ARP request on `lo0`.

Such a destination had just entered the flow table. While [the open-entry regime](the-engine-hands-up-the-decrypted-frame-on-the-armada.md#the-regime-test-measured-the-same-evening)
was being measured, the fast path - the port's entry and the gate open - made a microflow for the
tunnel's inner packet, which was addressed to the appliance's own LAN address, and punted it with
that identity; the driver recorded it as a flow candidate: a connection to this host. The sweep
resolves every candidate's next hop once a second (`npuflow.c`, `period = hz`): hence the rate, and
hence the start - the first line was logged at the minute of that measurement, 1,991 of them by the
time it was found.

## The fix, and how it was verified

The resolver refuses a route whose interface is loopback with `EHOSTUNREACH`, before the hint can
stand in: a destination this host owns is never a flow's way out, and nothing is asked on `lo0`.
Verified by absence: the message buffer cleared, the same trigger re-created - entry and gate opened,
a burst from the peer to the appliance's own address through the tunnel (`cand_pushed 0 → 1`) -
and **no line in twenty seconds**, where before there had been twenty; the burst answered 10 of 10,
and again after the entry and gate were restored. Module build `021f2f0d`, from the working tree.

The OCTEON TX driver's resolver has the same shape and no such guard; the same line can appear on
the XGS 3300 once a candidate to that host sits in its table. The same guard applies there, to be
built and tested on that appliance in its own session.

## Lesson

A route's interface is not always one you can resolve on: a destination the host owns routes by
`lo0`, and a resolver that takes the route's interface on trust - or lets a hint port stand in for it
- sends ARP where ARP cannot go. No counter of the driver's moved; the console said it once a second,
and the owner read it.
