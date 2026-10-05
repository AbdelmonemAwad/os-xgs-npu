# The bridge cannot be asked, so the flow answers

Measured on an XGS 3300, 2026-10-06.

## The refusal

Every accelerated flow whose destination was a machine on this appliance's own LAN was refused with
one line:

    no next hop for 0x786433c6: the route leaves by an interface this driver does not own

The route is right. Eleven front ports are members of one bridge, the bridge holds the LAN address,
and a route to the LAN leaves by the bridge - so the route says the frame must go out by L2 towards
that address and does not say by which port. The coprocessor can only be told a port.

## Why the bridge is no help

Not because asking is hard. Because there is nothing to ask. In `if_bridge.c`, every function that
could answer is `static`:

| | |
|---|---|
| `bridge_rtlookup` | the address cache lookup, `static` |
| `bridge_lookup_member` | by name, `static` |
| `bridge_lookup_member_if` | by ifnet, `static` |

The only non-static functions in the file are `vnet_bridge_init` and `vnet_bridge_uninit`. The one
way the cache leaves the kernel is a `copyout` to userspace from the ioctl, which a driver cannot
call. A module could find a static function's address in the symbol table and call it anyway; that
is a private function of another module, reached past its interface, and it would keep compiling
and stop being the right function without anything saying so.

## The flow already knows

A punted frame carries the pport tag of the port it came in on - that is how twelve front ports are
twelve interfaces at all. So a frame from the machine *is* the answer to which port the machine is
on, and the opposite direction of the connection is made of those frames.

`struct octep_flow` now records it. When the route names an interface this driver does not own, the
egress port comes from the other direction's entry in a table this driver already keeps and already
sweeps.

Which tuple the other direction has was the only thing to get right, and it is two cases that say
the same thing:

| | |
|---|---|
| translated | pf's two keys give the untranslated pair, oriented with the rewritten end as the source |
| not translated | the reverse of this frame's own tuple |

Both are the same statement - **the opposite direction is punted before translation** - so in both
cases the tuple to look for is the one the other direction's frames arrive with.

**It is not a learning table.** `octep_nhop.c` says why a map of address to MAC kept by this driver
would be wrong, and the same argument applies to a map of address to port. This is one field of one
flow: it is re-read every time that flow is punted again, and it is gone when the sweep takes the
flow out.

## The source MAC was wrong too, and nobody would have seen it

The source MAC and the MTU now come from the interface the route chose rather than from the port the
frame leaves by. For every route out of a front port those are the same interface and it makes no
difference, which is why it was never visible. For a route out of a bridge it is the whole
difference: the machine on the other end has the bridge's address in its ARP cache for its gateway,
and a frame from a member port's own address is one it was never told to expect.

## Measured

Sixty reads of `dp.accelerate`, each acting on whatever had last been punted, on a live firewall
carrying its ordinary traffic. Addresses here are from the documentation ranges, in place of the
appliance's own; nothing else about the output is changed.

```
frame 2364  proto 6  192.0.2.10:443 -> 203.0.113.46:39007
slot 733 rev 0 accelerated as entry 11
  to 02:00:00:00:00:42 on interface 10, mtu 1500  (port from the other direction of this flow, not from the route)
  do_snat, 0x2e7100cb:39007 becomes 0x786433c6:62256
```

The reply of a translated connection, arriving on the WAN port, for a machine behind the bridge:
`192.0.2.10` is the far end, `203.0.113.46` the appliance's WAN address, and `0x786433c6` the LAN
machine the frame is really for. **Three things that do not know about each other agree on the
answer:**

| | |
|---|---|
| the forward flow's record | entry 2, same five-tuple untranslated, `in oxp0` |
| the bridge's own address cache | that MAC is on `oxp0` |
| the host's ARP table | that address has that MAC |

And the connection stayed `ESTABLISHED:ESTABLISHED` in both directions afterwards, which is what
says the frames arrived: a wrong port would have left the return half with nothing coming back.

Nineteen of the sixty were accelerated and forty-one refused. That is the shape of the design rather
than a fault - the reverse direction cannot be accelerated until the forward one is in the table, so
most reads land on a frame whose counterpart is not there yet.

## What it cannot see

**A machine that moves to another front port in the middle of a connection.** Its own direction
keeps working, because the switch relearns from the frames it sends; this direction keeps using the
port it was last punted from, until the state expires and the sweep takes the flow out. The port is
checked for link before it is used, which covers a moved cable and not a moved machine.

**A destination behind the bridge whose connection this driver has only seen one half of.** There is
nothing to fall back on, and it refuses and says which of the two reasons it was.

## Lesson

The refusal said *the route leaves by an interface this driver does not own*, which reads like a
missing lookup, and two of the three places that could have provided one were closed. The answer
was not a lookup at all: it was already arriving on every frame, in the two bytes that make a front
port an interface. **A fact the datapath carries is worth looking for before a table is built to
hold it** - and the review that found the two defects in this change found both of them in the
fallback, not in the lookup, which is where a guess about where the traffic goes can hide.
