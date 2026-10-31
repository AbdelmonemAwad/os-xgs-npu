<!-- reviewed: 2026-10-31 -->
# Front ports that say their link state is real, on both families, 2026-10-31

[911](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/911): on an XGS 3100, replugging a LAN port never
brought LAN back, while plugging MGMT did. Looking at why, the second XGS 3300's LAN bridge stood at
`LOWER_UP` with no cable in any of its ports, every member reporting its link down.

The cause was ours: neither driver set `IFCAP_LINKSTATE` on its front ports (`capabilities=0<IPSEC>`), and
if_bridge takes a bridge none of whose members claims link state as always up. octep and npuep report a
port's carrier the moment it changes; the bridge over them did not believe it.

## What changed

`IFCAP_LINKSTATE` in the capabilities and enabled capabilities of each front port - octep's `oxp` interfaces
and npuep's `npup` interfaces - set when the port is made.

## Measured

| | first XGS 3300 (vendor version 23), octep | XGS 136 (vendor version 21), npuep |
|---|---|---|
| loaded | installed by the installer, after a reboot | built on the appliance, loaded in place by its `reload.sh`; back after 57 s, 14 ports, heartbeat alive |
| a front port's capabilities | `80000<LINKSTATE,IPSEC>` | `80000<LINKSTATE,IPSEC>` |
| a bridge left with no cabled member | the LAN bridge with its one cabled port taken out: `bridge0: link state changed to DOWN` | a bridge made for the test over one uncabled port: `bridge9: link state changed to DOWN` |
| the cabled port back | `bridge0: link state changed to UP`; the PC on it answered | - |

So a bridge over the front ports now says what is true: down with no cable, up with one.

## What it does not do

~~OPNsense acts on a link change through devd, and its rule (`/usr/local/etc/devd/ifnet.conf`) runs
`rc.linkup` only for interfaces whose media is Ethernet or 802.11. A bridge has no media, so a bridge's link
going down or up is still not acted on: replugging a LAN port does not apply LAN again, with or without this
change.~~ OPNsense's devd rule (`/usr/local/etc/devd/ifnet.conf`) runs `rc.linkup` only for interfaces whose
media is Ethernet or 802.11, and a bridge has none. Since the same day os-xgs-npu adds the two calls for
bridges (`/usr/local/etc/devd/xgs-bridge.conf`), so a bridge whose link is now true is acted on as an
Ethernet port is. Measured on the first XGS 3300, the rule installed by the installer: the LAN bridge with its
one cabled port taken out - `DEVD: Ethernet detached event for opt12(bridge0)`; put back -
`DEVD: Ethernet attached event for opt12(bridge0)`, then LAN applied again (`updateip (,[opt12])`), and the
PC on it answered.

What made 911 behave as it did - LAN and MGMT in one subnet, where the interface applied last loses the
subnet's route, and plugging MGMT applies MGMT again - is in the issue.
