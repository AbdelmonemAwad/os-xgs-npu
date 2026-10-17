<!-- reviewed: 2026-10-18 -->
# The switch dropped IPv6's neighbour solicitations on the ARMADA

Measured on an XGS 136, 2026-10-18.
[Issue 455](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/455): a routed front port with an
IPv6 address could not be found by the machine at the other end of its cable until the 136 had
spoken to it first.

## What was seen

The 136's Port8 is a routed /30 to the second XGS 3300's Port8 on the
[three-appliance bench](../measurements/bench-2026-10-17.md). Each end was given an IPv6 address on a
/64 of its own, with OPNsense's own interface settings and a pass rule. Then:

- from the 3300, `ping6` to the 136's address: nothing back, and `ndp -an` on the 3300 had no entry
  for it - its neighbour solicitation had not been answered;
- from the 136, `ping6` to the 3300: answered, and the 3300 now had an entry for the 136, learned from
  the 136's own solicitation;
- from the 3300 again: answered, until that entry expired.

IPv4 on the same port had always worked - ARP goes to broadcast.

## Why

An IPv6 neighbour solicitation is not sent to everyone. It goes to the target's solicited-node group,
an Ethernet address of the form `33:33:ff:xx:xx:xx` made from the last three bytes of the address
being looked for. A host that has that address joins that group, and its network card accepts frames
for it.

On the XGS 136 the network card is not the host's. Every front port is a port of the coprocessor's
88E6193X switch, and [netagent.md](../npuep/netagent.md) records how it is set up: a TCAM whose live
entries deliver broadcast, deliver the unicast addresses the host has named for each port, and drop
everything else. A port in the LAN bridge is opened with its catch-all, because a bridge has to see
frames for other machines; a routed port is not. So a solicitation for a routed port's address
matched nothing and was dropped in the switch, before the coprocessor or the host saw it.

The network agent has an attribute for multicast beside the one for the catch-all - `NWA_SUB_ALLMULTI`,
0x46, in `contrib/npuep/npunwa.h` since the agent's attributes were mapped - and the driver had never
sent it.

## The change

`npunwa`'s link poll, which already reconciles each port's catch-all with whether its interface wants
promiscuous mode, now does the same for multicast: a port whose interface asks for all multicast, or
has joined any group at all, is opened with `NWA_SUB_ALLMULTI`, and closed when it has none. Every
interface with an IPv6 address has joined two groups - its solicited-node group and all-nodes - so in
practice an IPv6 port is open. It is done from the poll and never from the multicast ioctl, because
the stack may issue that ioctl from a context that cannot wait on the agent's mailbox; the poll visits
one port every 100 ms, so the panel is reconciled within a second and a half. A port that refuses three
times is not asked again, and the log says so.

## Measured after

`npuep.ko` md5 `4a97d5be`, installed and booted on the XGS 136 on 2026-10-18:

    npuep0: nwa: Port7 multicast opened
    npuep0: nwa: Port8 multicast opened
    npuep0: nwa: Port1 multicast opened

Port1 is the WAN, Port7 and Port8 the two routed links; every other port is in the LAN bridge and was
already open through its catch-all. Then from the 3300, with its entry for the 136 deleted first:

    # ndp -d <the 136's address>; ndp -an | grep <the 136's address> || echo "no entry"
    no entry
    # ping6 -c 3 <the 136's address>
    3 packets transmitted, 3 packets received, 0.0% packet loss

and from the virtual machine on the bench, through the 3300, to the 136's Port8 and to its LAN
address: both answered. The IPv6 measurements of the same night are
[ipv6-and-queues-2026-10-18.md](../measurements/ipv6-and-queues-2026-10-18.md).

## What it does not say

That the XGS 3300 needed the same: it did not - its front ports reached IPv6 neighbours from the start,
and its own switch is set up by the OCTEON TX driver's bring-up, not by this agent. Nor that an IPv6
connection crosses the 136's coprocessor in hardware: the flow code for it is built and installed
([board-limits.md](../families/board-limits.md)), and has not yet had traffic through it.
