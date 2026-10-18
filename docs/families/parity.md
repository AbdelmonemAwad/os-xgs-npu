<!-- reviewed: 2026-10-18 -->
# What one appliance has and the other does not

Written 2026-10-18, from the drivers, the pages and the measurements. The two appliances on the bench
are not the same silicon and were not brought up in the same order, so neither has everything the
other has - and the status board on the README, which lists what each one has, made that easy to
miss: each card listed its own strengths. This page lists the **same** things for both, so every gap
shows.

| mark | means |
|:-:|---|
| ✅ | built and run on that appliance, with the page that shows it |
| 🟡 | built but not measured there, or only part of it |
| ⬜ | not built for that family yet |
| ➖ | the hardware has nothing of the kind |

The completion figures the families' own pages weigh are **96%** for the XGS 3300
([octeon-tx-reference.md](octeon-tx-reference.md#where-this-stands)) and **91%** for the XGS 136
([armada.md](armada.md#where-this-stands)).

## The table

| | | XGS 3300 &middot; OCTEON TX | XGS 136 &middot; ARMADA |
|---|---|:-:|:-:|
| **Ports** | every front port an interface | ✅ 12 of 12, and the module's four | ✅ 14 of 14 |
| | assigned at the first boot, by the chassis names | ✅ | ✅ |
| | jumbo frames | ✅ MTU 72 to 9,000 per port, the switch at 10,240 | ✅ MTU 9,000, measured at 989 Mbit/s |
| | transceiver diagnostics | ✅ the Transceivers page | 🟡 the cages have never held a module |
| | expansion module | ✅ 4x10G, ports A1 to A4 | ➖ the bay is the host's: modem or Wi-Fi |
| | port identify light | ✅ | ✅ |
| **Data path** | receive | ✅ an interrupt per ring | 🟡 polled every millisecond |
| | transmit | ✅ seven rings beside the first | ✅ four queues, [measured](../measurements/ipv6-and-queues-2026-10-18.md#the-xgs-136s-four-transmit-queues) |
| | without a copy per frame | ⬜ a copy each way | ⬜ a copy each way |
| **Flow offload** | connections made automatically | ✅ on | ✅ ~~🟡 built and measured, off in this unit's settings~~ ~~built and measured with two ports' interface entries opened by hand; the driver opens none, so turned on by itself it makes nothing~~ the maker opens the ports' interface entries itself since 2026-10-18 and shuts them when turned off - measured from a boot with it on, nothing by hand ([the finding](../findings/every-next-hop-named-an-empty-cage-on-the-armada.md#the-entries-opened-by-the-maker)); off in this unit's settings |
| | a table the board's size | ✅ 2,000,000 | ✅ 1,500,000 |
| | flow writes posted without a wait | ✅ the high-priority ring | ⬜ each command waited for, 138 microseconds |
| | IPv4, with its translation | ✅ | 🟡 ~~✅ LAN to WAN, [measured](../findings/one-next-hop-for-many-flows-on-the-armada.md)~~ the table was measured then, not the delivery: every next hop named an empty cage until 2026-10-18 ([#483](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/483)); delivered in hardware since, [measured](../findings/every-next-hop-named-an-empty-cage-on-the-armada.md) from a LAN port to a routed one without translation |
| | IPv6 | ✅ 9.24 and 9.27 Gbit/s, [measured](../measurements/ipv6-and-queues-2026-10-18.md#ipv6-through-the-xgs-3300) | 🟡 built, not measured |
| | a connection handed back, revived in place | ✅ 6,100 revived, none refused | ✅ ~~⬜ taken back only when pf lets go~~ ~~🟡 made again from its frames' identities within a sweep~~ revived in place from the receive path, and made again when its microflows come back under new identities - 99.9 % of a download's frames in hardware, [measured](../measurements/xgs136-hardware-2026-10-18.md#revived-in-place-the-same-evening) 2026-10-18 |
| | a connection returned to the host by hand, a never-offloaded list | ✅ tested live, 2026-10-18 | ✅ tested live with two entries opened by hand ([#483](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/483)) |
| | every flow voided by a ruleset reload | ✅ | ✅ |
| | a thousand connections at once | 🟡 run on 2026-10-17, its page not published | ⬜ not run |
| **IPsec offload** | AES-GCM, both ways | ✅ | ✅ |
| | AES-CBC with an HMAC | ✅ | ✅ |
| | NAT traversal | ✅ | ✅ |
| | replay windows to 4,096 | ✅ | ✅ |
| | fragmentation before the envelope | ✅ | ✅ |
| | IPv6, outside and inside | ✅ | ⬜ |
| | extended sequence numbers | 🟡 outbound only, as the engine allows | ⬜ |
| | tunnel connections in the flow table | ✅ outbound | ⬜ |
| **The appliance** | temperatures in OPNsense | ✅ | ✅ |
| | fans | 🟡 read; the board gives the host no register to set them | 🟡 read over the management link |
| | front panel | ✅ LCD and keys | ➖ none |
| | hardware watchdog | ✅ armed | ✅ armed |
| | PoE | ➖ none | 🟡 read, not controlled |
| | fail-to-wire relay | 🟡 read, deliberately not driven | ➖ none |
| **Keeping it running** | boot hooks and the kernel followed through an update | ✅ | ✅ |
| | updated from the Updates page | ✅ | ✅ |
| | problem reports | ✅ | ✅ ~~🟡 in the plugin; this unit not updated since~~ - running since the update to v0.1.0-alpha.1.1 (2026-10-18) |
| | an image from the builder, booted | ✅ | ⬜ |

## What closes the gaps, in order

On the XGS 136, by what each gap costs an owner:

0. ~~**The interface entries opened by the driver** when the flow maker is on - found on 2026-10-18:
   with every entry shut, as the driver leaves them, the fast path names no connection and
   "automatic acceleration" has nothing to make.~~ ~~A connection made between a LAN bridge member
   and a routed port stopped carrying traffic (#483), which has to be understood first.~~ **Done the
   same day:** every next hop had named an empty cage, fixed; and the flow maker now opens the
   entries itself ([the finding](../findings/every-next-hop-named-an-empty-cage-on-the-armada.md)).
1. **IPv6 measured through the box**, then IPv6 inside and outside a tunnel - the flows are built; the
   bench path through the 136 is set up and waits on the PC's side.
2. **Flow writes posted on the high-priority ring** the board already asks for, as on the XGS 3300 -
   the ring is declared and carries nothing yet.
3. ~~**A connection handed back, revived in place**, and~~ **Tunnel connections in the flow table** - the
   XGS 3300's code, ported the way the rest of the flow table was. A connection handed back is
   revived in place since 2026-10-18; attaching a direction that comes back under a new identity to
   the live connection, as the XGS 3300 does, is what is left of it.
4. **Receive by interrupt** instead of a poll - the doorbells exist; the coprocessor has not been seen
   to raise one for a received frame.
5. **The transceivers**, with a module in a cage.

On the XGS 3300: **a thousand connections, published**; then the copy per frame, which both families
share.
