<!-- reviewed: 2026-10-24 -->
# Questions people ask

Short answers, each with the page that says it in full.

## Which appliances does it run on?

The XGS 136 and the XGS 3300 have been run here, and everything on this site was measured on them.
Every other model of their two families - the XGS 87, 107, 116, 126 and 138, and the XGS 2100, 2300
and 3100 - installs, and is marked **added, untested**: its ports come from the vendor's board files,
and no such appliance has been here to test on. The larger families have no driver yet.
[Every model](https://npuforge.org/devices/), and [what works on each](what-works.md).

## What does installing change on the appliance?

**Installing erases the appliance's internal disk**, and with it the Sophos firmware and its
settings; the host then runs OPNsense, and this project's drivers bring up the coprocessor and its
front ports. The guide does not go back the other way.
[Step by step](step-by-step.md).

## Is it made by Sophos, or by OPNsense?

No. This project is independent: it is not affiliated with, endorsed by or sponsored by either, and
their names are used only to say what it runs on and works with. No Sophos software or firmware is
distributed here. [Legal](../../website/pages/legal.md).

## What does it cost?

Nothing. The code is under the BSD-2-Clause licence, and everything the guide uses - OPNsense, this
project, GitHub's build, the tools to write the stick and open the console - is free.

## How stable is it?

It is an alpha: the releases are numbered 0.1.0-alpha, and it comes with no warranty. Try a release on
an appliance you can reach before you trust it with a network you depend on. What is not finished, for
each appliance, is in its family page's table - [ARMADA](../families/armada.md#where-this-stands) and
[OCTEON TX](../families/octeon-tx-reference.md).

## What runs in the coprocessor's hardware?

Connections, once the firewall has let them through - the fast path forwards them port to port - and
IPsec, whose associations the coprocessor's engine encrypts and decrypts. What has to be seen by the
host stays with it: a connection the traffic shaper or the captive portal names is kept on the host -
a captive portal zone runs in hardware since 2026-10-24 if XGS: Settings **Captive Portal clients in
hardware** is on, each login a live user of the coprocessor whose connections leave hardware when its
session ends ([measured](../measurements/cp-hardware-2026-10-24.md)) - and Suricata sees a connection before it goes into hardware - how it starts, and nothing after. ~~**Suricata's
inline IPS and the offload are, for now, one or the other**~~ **Suricata's inline IPS and the offload
work together since 2026-10-23, as an experiment**: XGS: Settings **Inspect, then hardware**, off by
default, has OPNsense's Intrusion Detection in IPS mode see every frame of a new connection in hardware
until it has let its first megabyte through, drop what its rules drop, and leave the rest to the
hardware - measured with the service itself on both appliances ([measured](../measurements/inspect-2026-10-23.md#the-setting-and-the-intrusion-detection-service)).
Without it, what the IPS must see in full goes in
**Never offloaded** on XGS: Settings, or acceleration stays off. Whether the two can work together is
under research: the coprocessor's fast path has a verdict that sends a connection's start to an
inspector and carries the rest itself, found on 2026-10-22 ~~and not tried yet~~ and tried on
2026-10-23: it hands the frames to a PCIe virtual function that the vendor's host makes and OPNsense
does not, so they are lost until the drivers make one
([the discoveries](../findings/discoveries-2026-10-22.md#inspection-then-hardware),
[measured](../measurements/inspect-2026-10-23.md)). On the XGS 3300 the driver made one the same day,
and Suricata inspected connections in hardware through it - their first megabyte, then the hardware
carried the rest. That is an experiment, off unless asked for, ~~until~~ ~~the XGS 136 has it too and~~ ~~there
is a setting~~ ([measured](../measurements/inspect-2026-10-23.md#then-made-on-the-xgs-3300)); the XGS 136
has it too since the same evening ([measured](../measurements/inspect-2026-10-23.md#then-made-on-the-xgs-136)),
and the setting above since later that evening.
[What works](what-works.md), [the shaper, measured](../measurements/shaper-3300-2026-10-20.md),
[a TLS fingerprint, cut](../measurements/ja4-cut-2026-10-19.md).

## How fast is it?

As fast as the links it was measured on allow. On the XGS 3300, 1,024 connections at once filled the
10 Gbit/s link at 9,837 Mbit/s with the CPUs 7.8 % busy, and sixteen IPsec tunnels carried 9,019 Mbit/s
from one 10G port. On the XGS 136, its gigabit link stayed full for hours - 945 Mbit/s over two hours
of TLS. [Every measurement](../measurements/README.md), and
[how they are taken](../measurements/methods.md).

## How is it installed, and kept current?

An image from [the image builder](https://npuforge.org/build/) - a ready one, published with ~~each
release~~ each main release, or one it builds for you - written to a USB stick, installed from it -
[step by step](step-by-step.md); or the installer, on an OPNsense already there -
[installing](installing.md). Releases arrive in the web interface, on the XGS: Updates page -
[updating](updating.md).

## How do I report a problem, or my appliance?

With problem reports switched on, the plugin files what goes wrong on its own - [problem
reports](../problem-reports.md). An appliance of a model nobody has reported yet, and what works on it:
[the compatibility form](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/new?template=appliance.yml).
Anything else: [an issue](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/new/choose), or
[write to the project](../../website/pages/contact.md).
