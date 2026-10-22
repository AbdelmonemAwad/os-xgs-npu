---
date: 2026-10-22
slug: what-the-hardware-can-do
title: "What the hardware can do that was not known"
summary: "A sweep of the vendor's sources and the coprocessor's own log found 29 capabilities of these appliances this project had not known. The completion figures went down to count them, and the first was done the same day: the TCP window is checked in hardware."
---

# What the hardware can do that was not known

![The bench the evening the first of them was done: the driver's code on one screen, a run of the measurements on the other, and below them the XGS 136, opened, and the XGS 3300](../../docs/images/hardware/bench/desk-2026-10-22.jpg)

A sweep of the vendor's own sources, its board package and the coprocessor's own log, read on the
second XGS 3300 and on the XGS 136, found **29 capabilities of this hardware that this project had
not known about**. They matter, so the completion figures now count them - lowered on purpose, the
XGS 3300 from 97% to 89% and the XGS 136 from 96% to 91%.

<!-- more -->

Among them:

- **Dragonfly**, a deep packet inspection and threat detection engine built on nDPI, running on the
  XGS 3300's coprocessor - not on the XGS 136's.
- **A transmit rate limit for each front port**, in the coprocessor's firmware: on for the XGS 3300,
  switched off there for the XGS 136.
- **The TCP window checked in hardware** - which also showed that a connection in hardware had its
  window checked by nobody.
- A connection dropped in hardware by its verdict, inspection that hands the rest of a connection
  back to hardware, live users, a heartbeat with the fast path, and the switch's own rules: 254 of
  its 256 TCAM entries are the vendor's.

Every one is on [the discoveries page](../../docs/findings/discoveries-2026-10-22.md), with where it
was found and how far it has been taken, and they are worked on one at a time,
[in this order](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/680).

**The first was done the same day.** The coprocessor now checks the TCP window of every connection
it is given, filled from pf's own numbers: segments outside the window are dropped in hardware, and
a connection taken back gives pf the coprocessor's numbers. Measured on both appliances, every run
photographed - the photograph above is the bench while they ran: segments 64 MiB and 2 GiB outside
a live connection's window dropped, 5 of 5 each, on both; four streams at 9.91 Gbit/s and
941 Mbit/s without one ordinary segment dropped. What it does not cover yet is on
[the page](../../docs/measurements/tcp-window-2026-10-22.md) and in
[679](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/679). The figures went up with it, to 90%
and 92%.
