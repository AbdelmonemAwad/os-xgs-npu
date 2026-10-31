<!-- reviewed: 2026-10-31 -->
# Interfaces sharing a subnet: what it does, and a warning that names it, 2026-10-31

[911](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/911): an XGS 3100's MGMT would not answer on a
static address, though it did by DHCP, and its LAN came back after a reboot only once a cable had been
plugged into MGMT and back. LAN - the bridge over the front ports - and MGMT were in one subnet, the
owner's home network. What that does was measured in the lab.

## Measured

The second XGS 3300 (vendor version 22), managed over F1 so MGMT and LAN could change; its LAN bridge with
no port plugged in, given an address in MGMT's subnet (192.168.100.0/24); the PC on MGMT.

| | what | the subnet's route | MGMT from the PC |
|---|---|---|---|
| 1 | LAN's address saved and applied | stays on MGMT, which had it first | answers |
| 2 | reboot | MGMT - it has its address before the bridge is made | answers |
| 3 | MGMT applied again, as saving its page does | **moves to the bridge** | **no ping, no web GUI** |
| 4 | LAN applied again | back on MGMT | answers |
| 5 | from 4, MGMT's link down and up - its cable pulled and plugged: OPNsense applies MGMT again | **moves to the bridge** | **silent** |
| 6 | from 5, MGMT given a gateway on its segment, as a DHCP lease gives one | still the bridge | **answers**: OPNsense's `reply-to` sends MGMT's replies out of MGMT |

And the first XGS 3300 (vendor version 23) with both cabled, MGMT and LAN on two of the PC's ports: LAN
given an address in MGMT's subnet, MGMT applied again - the route on the bridge, MGMT silent, LAN fine.

So: **two interfaces in one subnet share one route, and the one applied last loses it**, whether its cable
is in or not - its address is taken off and put back, the route moves to the other's address meanwhile,
and the address put back cannot take it again. Plugging an interface's cable applies it again. A DHCP
address answers anyway through `reply-to`; a static one without a gateway does not. OPNsense checks an
overlap only between static addresses.

## What changed

`scripts/xgs/subnets.py` reads the addresses the system has - a DHCP address counts - finds the subnets
that overlap, and says which interface holds each one's route and which answers nothing from it. It is shown:

- by `verify.sh`, a section of its own: `ok no two interfaces share a subnet`, or a note naming them;
- on **XGS: Ports**, a warning above the ports;
- in the support bundle, as `subnets.json`.

Tested on the first XGS 3300, deployed by the installer: with nothing shared, verify.sh said ok and the page
had no warning; with the LAN bridge given 192.168.3.60/24 beside MGMT's 192.168.3.1/24, it said:

```
LAN (bridge0) and MGMT (igb0) share a subnet: bridge0 192.168.3.60/24, igb0 192.168.3.1/24
  192.168.3.0/24 is routed by igb0
  LAN (bridge0) answers nothing from 192.168.3.0/24: its replies leave by igb0
```

and XGS: Ports' template compiled with the warning in it. Nothing is changed for the owner: the warning
names the cause, and the fix - each interface a subnet of its own - is the owner's.
