<!-- reviewed: 2026-10-20 -->
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
([octeon-tx-reference.md](octeon-tx-reference.md#where-this-stands)) and ~~**91%**~~ ~~**92%**~~ **93%** for the XGS 136
([armada.md](armada.md#where-this-stands)).

## The table

| | | XGS 3300 &middot; OCTEON TX | XGS 136 &middot; ARMADA |
|---|---|:-:|:-:|
| **Ports** | every front port an interface | ✅ 12 of 12, and the module's four | ✅ 14 of 14 |
| | assigned at the first boot, by the chassis names | ✅ | ✅ |
| | jumbo frames | ✅ MTU 72 to 9,000 per port, the switch at 10,240 | ✅ MTU 9,000, measured at 989 Mbit/s |
| | transceiver diagnostics | ✅ the Transceivers page | ~~🟡 the cages have never held a module~~ ✅ the Transceivers page; its two copper modules carry no diagnostics |
| | the medium a cage reports, read from its module | ✅ [since 2026-10-19](../measurements/cage-medium-2026-10-19.md) | ✅ [since 2026-10-19](../measurements/cage-medium-2026-10-19.md) |
| | expansion module | ✅ 4x10G, ports A1 to A4 | ➖ the bay is the host's: modem or Wi-Fi |
| | port identify light | ✅ | ✅ |
| **Data path** | receive | ✅ an interrupt per ring | 🟡 polled every millisecond |
| | transmit | ✅ seven rings beside the first | ✅ four queues, [measured](../measurements/ipv6-and-queues-2026-10-18.md#the-xgs-136s-four-transmit-queues) |
| | without a copy per frame | ⬜ a copy each way | ⬜ a copy each way |
| **Flow offload** | connections made automatically | ✅ on | ✅ ~~🟡 built and measured, off in this unit's settings~~ ~~built and measured with two ports' interface entries opened by hand; the driver opens none, so turned on by itself it makes nothing~~ the maker opens the ports' interface entries itself since 2026-10-18 and shuts them when turned off - measured from a boot with it on, nothing by hand ([the finding](../findings/every-next-hop-named-an-empty-cage-on-the-armada.md#the-entries-opened-by-the-maker)); off in this unit's settings |
| | a table the board's size | ✅ 2,000,000 | ✅ 1,500,000 |
| | flow writes posted without a wait | ✅ the high-priority ring | ✅ ~~⬜ each command waited for, 138 microseconds~~ the high-priority ring, since 2026-10-18 - a thousand connections taken back in one sweep that held the table 8.3 ms, 27.8 waited ([measured](../measurements/xgs136-hardware-2026-10-18.md#a-thousand-connections-taken-out-at-once)) |
| | IPv4, with its translation | ✅ | ✅ ~~✅ LAN to WAN, [measured](../findings/one-next-hop-for-many-flows-on-the-armada.md)~~ ~~🟡 the table was measured then, not the delivery~~: every next hop named an empty cage until 2026-10-18 ([#483](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/483)); LAN to WAN through the outbound NAT, 941 and 919 Mbit/s with the 136's CPUs at 0.4 %, [measured](../measurements/xgs136-hardware-2026-10-18.md#translated-lan-to-wan) 2026-10-18 |
| | IPv6 | ✅ 9.24 and 9.27 Gbit/s, [measured](../measurements/ipv6-and-queues-2026-10-18.md#ipv6-through-the-xgs-3300) | ✅ ~~🟡 built, not measured~~ 929 and 919 Mbit/s in hardware, [measured](../measurements/xgs136-hardware-2026-10-18.md#ipv6) 2026-10-18 |
| | a connection handed back, revived in place | ✅ 6,100 revived, none refused | ✅ ~~⬜ taken back only when pf lets go~~ ~~🟡 made again from its frames' identities within a sweep~~ revived in place from the receive path, and made again when its microflows come back under new identities - 99.9 % of a download's frames in hardware, [measured](../measurements/xgs136-hardware-2026-10-18.md#revived-in-place-the-same-evening) 2026-10-18; given back soon after it was made, it waits with the host before it is made again, doubling - the remake had made it give the connection back again, 29 times in 15 seconds ([measured](../measurements/xgs136-giveback-2026-10-19.md)) 2026-10-19 |
| | a connection returned to the host by hand, a never-offloaded list | ✅ tested live, 2026-10-18 | ✅ ~~tested live with two entries opened by hand ([#483](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/483))~~ its takeout was refused by the coprocessor until 2026-10-18 and what came back stopped at pf ([the finding](../findings/a-takeout-took-nothing-out-on-the-armada.md)); since then a stream handed back goes on through the host at 941 Mbit/s ([measured](../measurements/xgs136-hardware-2026-10-18.md#handed-back-to-the-host)) |
| | a connection pf ends, taken out of hardware | ✅ ~~🟡 `CONN_RECLAIM_FP`, not measured with a killed state~~ a stream stopped within two seconds of its states being killed, 2026-10-19 ([measured](../measurements/lists-cut-2026-10-19.md)) | ✅ ~~two `MFLOW_INVALIDATE`s the coprocessor refused - a killed state went on at 942 Mbit/s~~ `CONN_RECLAIM_FP` since 2026-10-18: the stream stops within the second ([measured](../measurements/xgs136-hardware-2026-10-18.md#taking-a-connection-out)) |
| | a block list cuts the connections already open | ✅ within two seconds, or the minute's cron ([measured](../measurements/lists-cut-2026-10-19.md)) | ✅ the same ([measured](../measurements/lists-cut-2026-10-19.md)) |
| | a connection cut by its TLS fingerprint (JA4) | ✅ the listed one stopped, the one beside it ran on ([measured](../measurements/ja4-cut-2026-10-19.md)) | ✅ the same, both in hardware, [measured](../measurements/ja4-cut-2026-10-19.md#on-the-xgs-136) 2026-10-19; ~~🟡 the same script and cut; not measured~~ |
| | each connection's packets and bytes, counted by the coprocessor | ✅ read since 2026-10-19: with pf's count, what crossed the wire to 14 packets in 2.74 million; given to pf's state and rule on request, to the packet ([measured](../measurements/hw-counters-2026-10-19.md)) | ✅ ~~⬜ not looked at~~ read since 2026-10-19, the same messages: given to pf's state and rule, what crossed the wire to 0 packets in 1.37 million either way; a setting, on by default ([measured](../measurements/xgs136-counts-2026-10-19.md#the-reader-measured)) |
| | each port's traffic in hardware, counted by the coprocessor | ✅ ~~🟡 read and measured: what the interface's own counters miss, to 114 packets in 6.6 million; adding them to the interfaces is built, not yet measured~~ added to the interfaces' counters: the whole stream, to 327 packets in 5.1 million; a setting, on by default ([measured](../measurements/hw-counters-2026-10-19.md#added-to-the-interfaces-counters)) | ✅ ~~⬜ not looked at~~ added to the interfaces' counters: every frame both ways, bytes once four a frame of FCS are taken off what comes in; a setting, on by default ([measured](../measurements/xgs136-counts-2026-10-19.md#the-reader-measured)) |
| | unusual traffic reported: a scan, a sweep, a host beyond its own mean | ✅ ([measured](../measurements/anomaly-2026-10-19.md)) | ✅ [measured](../measurements/anomaly-2026-10-19.md#on-the-xgs-136) 2026-10-19: the scan, the sweep and a stream in hardware all reported, the stream's volume from pf's count of what was in hardware (the row above); ~~🟡 the same script; a connection in hardware stops adding to pf's counts there, so its volumes undercount; pf's counts include what is in hardware since 2026-10-19 (the row above); not measured~~ |
| | an address dropped by the coprocessor (its DoS table) | ❌ not in the v22 firmware: the three DoS commands answered "not dispatched" ([measured](../measurements/dos-probe-2026-10-19.md)); one connection can be dropped (a microflow with action 0) | ❌ ~~⬜ not tried~~ not in its firmware either: the three DoS commands answered rc 0x8006, not dispatched, and a stream from the listed address ran on ([measured](../measurements/dos-probe-2026-10-19.md#on-the-xgs-136) 2026-10-20) |
| | every flow voided by a ruleset reload | ✅ | ✅ |
| | a thousand connections at once | ✅ ~~🟡 run on 2026-10-17, its page not published~~ 1,032 in hardware at the 10 Gbit/s link's 9,837 Mbit/s, the CPUs 7.8 % busy, [measured](../measurements/xgs3300-many-2026-10-19.md) 2026-10-19 | ✅ ~~⬜ not run~~ 953 of 1,024 in hardware at the link's 996 Mbit/s, the host carrying 1,129 frames in ten seconds, [measured](../measurements/xgs136-hardware-2026-10-18.md#a-thousand-connections-at-once) 2026-10-18 |
| | an integrity soak: hours of TLS, every byte checked | ✅ 2,391 GiB, 0 bytes changed ([measured](../measurements/xgs3300.md#two-hours-of-tls-on-the-failures-own-path-translated-through-the-host)) | ✅ 795 GiB in two hours, 0 bytes changed ([measured](../measurements/xgs136-soak-2026-10-19.md)) |
| **IPsec offload** | AES-GCM, both ways | ✅ | ✅ |
| | AES-CBC with an HMAC | ✅ | ✅ |
| | NAT traversal | ✅ | ✅ |
| | replay windows to 4,096 | ✅ | ✅ |
| | fragmentation before the envelope | ✅ | ✅ |
| | IPv6, outside and inside | ✅ | ✅ ~~⬜~~ both on the engine, both ways: 951 to 982 Mbit/s with IPv4 inside an IPv6 tunnel, 962 to 979 with IPv6 inside, with GCM and with CBC ([measured](../measurements/ipv6-136-2026-10-20.md) 2026-10-20) |
| | extended sequence numbers | 🟡 outbound only, as the engine allows | ✅ ~~⬜~~ both ways on the engine: 4,305,432 frames decrypted on one association, 5,796,289 encrypted and checked by the peer's kernel with no failure, at the link's 984 Mbit/s ([measured](../measurements/esn-136-2026-10-20.md) 2026-10-20) |
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
1. ~~**IPv6 measured through the box**, then~~ **IPv6 inside and outside a tunnel** - ~~the flows are
   built; the bench path through the 136 is set up and waits on the PC's side~~ IPv6 through the box is
   measured since 2026-10-18, in hardware both ways.
2. ~~**Flow writes posted on the high-priority ring** the board already asks for, as on the XGS 3300 -
   the ring is declared and carries nothing yet.~~ **Done 2026-10-18**, and with it a takeout that takes
   a connection out and a connection handed back that goes on through the host - neither did before
   ([the finding](../findings/a-takeout-took-nothing-out-on-the-armada.md)).
3. ~~**A connection handed back, revived in place**, and~~ **Tunnel connections in the flow table** - the
   XGS 3300's code, ported the way the rest of the flow table was. A connection handed back is
   revived in place since 2026-10-18; ~~attaching a direction that comes back under a new identity to
   the live connection, as the XGS 3300 does, is what is left of it~~ attaching a direction alone was
   tried and measured worse than making the connection again ([the page](../measurements/xgs136-hardware-2026-10-18.md#tried-and-not-taken-attaching-one-direction-at-a-time)); one second in thirty of a download through the host is what is left.
4. **Receive by interrupt** instead of a poll - the doorbells exist; the coprocessor has not been seen
   to raise one for a received frame.
5. ~~**The transceivers**, with a module in a cage.~~ Both cages hold copper modules since 2026-10-19, read and reported; diagnostics wait for a module that has them.

On the XGS 3300: ~~**a thousand connections, published**;~~ a thousand connections measured and published on 2026-10-19; then the copy per frame, which both families
share.
