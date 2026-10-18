<!-- reviewed: 2026-10-18 -->
# Every next hop named an empty cage on the ARMADA

Measured on an XGS 136, 2026-10-18.
[Issue 483](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/483): a connection made in hardware
through the XGS 136 stopped carrying traffic the moment it was made. Every next hop the driver
programmed named logical interface 8 - PortF2, an SFP cage with nothing in it - whatever port the
route left by.

## What was seen

Testing the per-connection hand-back of #484, with two ports' interface entries opened by hand (the
driver opens none: see [parity](../families/parity.md)), an SSH session from the bench PC on Port3, a
LAN bridge member, to the second XGS 3300 behind Port8, a routed /30, printing a line every 0.2 s. The
flow maker made it within seconds, and the session's last line arrived at that moment; the 3300's
socket held 19,800 bytes it could not deliver.

The candidate list printed the next hop it had resolved for each connection, and the interface was
wrong in every one - 8 for Port8, whose interface is 7, and 8 for Port1, whose interface is 0:

```
nhop port 7 iface 8 tag 0x8800 mtu 9000 ...
nhop port 0 iface 8 tag 0x8100 mtu 1500 ...
```

The port, the tag and the MTU were right; only the interface, which is what `NHOP_PROGRAM` tells the
fast path to send by, was not.

## The cause

The table the driver reads the interface from was right in the module itself - the fourteen entries
dumped from `npuep.ko`'s `.rodata`, Port1 interface 0 to PortF2 interface 8 - and the lookup read the
right byte of it. The value was overwritten afterwards, by the call that asks for the neighbour's MAC.

With a valid entry, FreeBSD's `arpresolve` copies the whole link header it keeps prepared for that
neighbour - destination, source and type, fourteen bytes on Ethernet - not the six of a MAC. The
driver passed it the six bytes of `dmac`; it ran on over `smac`, then over `iface`, which follows,
and left there the first byte of the type, `0x08` of `0x0800`. `nd6_resolve` does the same for IPv6,
where it would have left `0x86`.

The XGS 3300's driver makes the same call into the same kind of structure and was never affected:
it fills `smac` and `iface` after the call, so both are written over the overrun. The ARMADA's driver
filled them before it, deliberately - so that a caller told "not yet" (`EWOULDBLOCK`) still knows the
port and its MTU - from the first slice of its flow table
([the flow table begins](the-flow-table-on-the-armada-begins.md), #387).

## The fix

The neighbour is resolved into a buffer with room for a whole link header, `LLE_MAX_LINKHDR`, and
only its first six bytes - the destination MAC - are copied into the next hop; IPv4 and IPv6 alike.
The XGS 3300's driver is unchanged but for a comment that its order must stay.

## Measured after

Module `634cb5fa` on the XGS 136, the same two entries opened by hand, the same session, one at a
time:

- the candidates' next hops named the right interface - `nhop port 0 iface 0 tag 0x8100`;
- the session was made in hardware and delivered every line, 250 of 250 at five a second;
- **the host saw none of its frames**: over ten seconds with it in hardware, Port8's own counters
  read 767 in and 743 out before and after, and Port3's 1,000 out before and after (three frames
  came in on Port3, the bench's other traffic or the session's, too few to be its fifty) - the
  session's frames crossed the appliance both ways without reaching the host.

With a second session handed back to the host by #484's release beside it, Port8 counted fifty in
and fifty out in the same ten seconds - that session's five lines a second and their
acknowledgements - while the one in hardware went on uncounted. Both delivered every line.

The entries were put back to `0x0009` and the flow maker turned off after each run.

## What this corrects

[The sweep accelerates flows on its own](the-sweep-accelerates-on-its-own-on-the-armada.md) read a
rising `FROM_WIRE_TO_WIRE` as proof that accelerated flows crossed the appliance in hardware. They
left it in hardware, and by PortF2; that page now says so. The next-hop sharing of
[one next hop for many flows](one-next-hop-for-many-flows-on-the-armada.md) is unaffected: every entry
carried the same wrong interface, and the sharing it measured is the same with the right one.

## The lesson

A buffer handed to a kernel call is the size the call writes, not the size of the field wanted out of
it: read the call's copy, not its name. And a counter that says frames left by the wire says they
left, not where they arrived - the delivery is measured at the far end.
