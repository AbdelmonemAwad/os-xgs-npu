<!-- reviewed: 2026-10-20 -->
# OCTEON TX - the reference

What is true now, with no history. Every number here has been read from the appliance or from
the vendor's own source, and the reasoning behind each one - including what was tried and
withdrawn - is in [octeon-tx.md](octeon-tx.md), which is a log rather than a reference.

If you only read one page about this family, read this one.

## The board

| | |
|---|---|
| coprocessor | Cavium OCTEON TX CN8365, PCI `177d:a300`, eight VFs at `177d:a303` |
| host | AMD Ryzen Embedded V1780B, 8 threads, 16 GB |
| cores | 20, with `isolcpus=1-19`; 17 of them are fast-path workers |
| assembly | `AMDA0202-0004`, platform `xgs1us` |
| front ports | 12: `Port1`-`Port8` and `PortF3`/`PortF4` behind an 88E6193X, `PortF1`/`PortF2` direct to BGX2 |

## Bring-up, in order

1. **Load the driver.** It finds the barmap at `BAR1 + 0x02000000` and reports the four
   facilities: control, mgmt_netdev, nw_agent and rpc, at 32/33/34/35 MB with doorbells
   152/153/154/155.
2. **Complete the endpoint handshake.** Until it does, the NetAgent window is entirely blank and
   every request returns `ENXIO`. The handshake is also where the coprocessor reports its tick
   rate - 800 per microsecond on this board - and it runs once per coprocessor boot and must
   never be re-armed.
3. **Read the port table.** NetAgent operation `0x01` returns 13 records of 20 bytes, each
   beginning with `tag | flags`.
4. **Raise a port.** Operation `0x03`, attribute `0x00`, payload 1.
5. **Read its link.** Operation `0x04`, attribute `0x00`. Allow about two seconds - a 10G
   bring-up is not instantaneous and an immediate read-back reports failure when it means
6. **Name each panel port's own address to the switch.** Operation `0x03`, attribute `0x03`,
   addressed by the port's switch tag - `0x8100` for panel label 1 - carrying the six bytes
   operation `0x04` answers for the same tag. Until this is sent the switch's TCAM entry for that
   port has its octet mask at `0x00`, "Never Hit", and the port passes broadcast and nothing else.
7. **Bind the tag to a logical interface.** `rpc` command 5, `PPORT_UPDATE`, mapping the same tag
   to `switch port - 1`, which is the board file's `lifport`; then command 3, `LIF_ADD_UPDATE`,
   with that address, MTU 1500, forwarding mode 2 and mask 255.
8. **Give it a host interface.** `dp.if_port` then `dp.if_add`, both carrying the tag, and the
   interface comes up with the panel port's own address. A tag with no interface is counted in
   `dp.rx_untagged`.
   unfinished.

## The port tags

| tag | what | filter table |
|---|---|---|
| `0x0001`, `0x0002` | the two coprocessor 10G MACs - `PortF1` and `PortF2` | 65535 |
| `0x8000` | the switch's uplink to the coprocessor | 0 |
| `0x8100` .. `0x8a00` | switch ports 1 to 10 | 12 |

Bit 15 marks a switch port and bits 13:8 carry its number. The filter-table size is the quickest
way to tell the two kinds apart. Note that `0x0003` answers requests without appearing in the
published table, so an answer is not evidence that a port exists.

**This table is also the answer to the receive path, and it was published here before the question
was asked.** The switch's uplink runs in DSA frame mode, so every frame it hands the coprocessor
carries the source port in a four-byte tag, and the fast path turns that into exactly the value in
the third row: `0x8000 | (switch port << 8)`. This driver bound tags 1, 2 and 3 instead, for
months, while its own reference page said what the tags were. **Read the reference before probing
for what it already holds.**


## The NetAgent operations that work from the host

Exactly four. Every other opcode returns an ACK with status 1, because the target registers no
handler for it - that includes MDIO and GPIO, which the target implements internally but does
not expose on this hop.

| op | what |
|---|---|
| `0x01` | switch init - returns the port table |
| `0x03` | set a port attribute |
| `0x04` | get a port attribute |
| `0x45` | combined all-port info |

### Attributes worth knowing

| attribute | what it returns |
|---|---|
| `0x00` | **the link** on a coprocessor MAC tag, and it is what a SET writes |
| `0x04` | the **nominal** speed - 10000 even for a port never brought up |
| `0x0d` | duplex. Static; it does not follow the link |
| `0x0e` | 264 bytes of 64 counters, **none of which are populated on this path** |
| `0x01`, `0x09`, `0x50` | refused with status 1 |

## The datapath frame

The 64-byte instruction: `dptr@0 ih3@8 pki_ih3@16 rptr@24 irh@32 exhdr[3]@40`. `rptr` and `irh`
are written byte-swapped; the rest are not. The three `exhdr` words are never written.

| field | value |
|---|---|
| `ih3.pkind` | 40 |
| `ih3.fsz` | 28, which is `16 + 4 + 8` |
| `pki_ih3.sl` | **94**, which is `fsz + TOTAL_TAG_LEN` |
| `pki_ih3` | `w = 1`, `utt = 1`, `tagtype` ordered, `pm = 0`; `qpg` and `uqpg` left unset |
| `irh.opcode` | `0x1220` |
| `irh.rlenssz` | **81**, the checksum offset, which is `TOTAL_TAG_LEN + sizeof(ethhdr) + 1` |
| `irh.param` | the interface index, and 0 on a usfp appliance |
| `irh.dport` | never written |

And the frame at `dptr` is not a bare Ethernet frame. It carries a **66-byte private header**:

    [ 2B port tag, network order ][ 64B metadata ][ dst MAC ][ src MAC ] ...

The metadata is **not** a walking pattern from `0xc0`. Filling it that way sets byte 3, which the
fast path reads as an egress security-association handle, so every frame was routed to IPsec
encryption and dropped before the wire. The vendor's form is what this driver sends now.

`TOTAL_TAG_LEN` is 66 - `PPORT_HLEN` 2 plus `CUSTOM_META_TAG_LEN` 64 - and both ends name the
same split. The metadata is validated: the fast path counts what fails in
`FPCNTR_FROM_KN_DROP_MISMATCH_METADATA_FIELDS`.

## The output queue

Each descriptor is **two** 64-bit words: a buffer pointer and an info pointer. Both must be real
memory - the device DMAs the packet to one and a 16-byte response header and length to the other.

## Where this stands

Frames go out and frames come back, and the panel ports are FreeBSD interfaces. What is left is
engineering rather than discovery, which is a recent state and worth stating plainly.

The table is weighted so that a number can be argued with rather than taken on trust. Each row says
what it covers; the last column is how many of the hundred points the row is still holding back,
which is a more useful ordering than the percentage itself.

| | component | done | weight | still missing |
|---|---|---|---|---|
| 1 | the receive rate - works; the limit is now this driver's own servicing | 85% | 20 | **2.91** |
| 2 | network interfaces - twelve, real addresses, assigned in OPNsense | 80% | 12 | **2.33** |
| 3 | all twelve front ports - bound, addressed, presented | 100% | 8 | - |
| 4 | interrupts - MSI-X, one vector per ring | 100% | 6 | - |
| 5 | performance - sufficient on the ten 1G ports, 65% of line rate on the two 10G | 40% | 7 | **4.08** |
| 6 | persistence - boot hook, bring-up, and a kernel stamp that is enforced | 85% | 5 | 0.73 |
| 7 | resilience - recovery without a host reboot, soak testing | 30% | 4 | **2.72** |
| 8 | protocol and hardware understanding | 99% | 15 | 0.15 |
| 9 | the control plane | 100% | 10 | - |
| 10 | the outbound datapath - confirmed against a machine off the appliance | 100% | 10 | - |
| 11 | the rest of the appliance - see below | 67% | 6 | **1.92** |

Weights sum to 103. **Weighted: 85%.**

**Re-weighed on 2026-10-12**, the table above left as it stood on 2026-10-02. The same rows, two
added for what did not exist then, and each row names the issue that holds its remainder, so the
figure can be argued with:

| | component | done | weight | still missing | what holds it |
|---|---|---|---|---|---|
| 1 | the receive rate - one stream through the host 755 to 797 Mbit/s, four 947 to 967 | 95% | 20 | 1.00 | three stalls at a rekey, unexplained ([320](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/320), [321](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/321)) |
| 2 | network interfaces - twelve, real addresses, link and media, assigned in OPNsense | 90% | 12 | 1.20 | the plugin pages and the kernel follower's lock, deferred |
| 3 | all twelve front ports - bound, addressed, presented | 100% | 8 | - | |
| 4 | interrupts - MSI-X, one vector per ring | 100% | 6 | - | |
| 5 | performance - a gigabit through the host on four streams; a connection the coprocessor forwards itself at 680 to 750 with the host a few hundredths of a core busy | 80% | 7 | 1.40 | a copy per frame each way; the two 10G cages not measured at 10G |
| 6 | persistence - boot hook, bring-up, a kernel stamp that is enforced, the kernel followed through an update | 95% | 5 | 0.25 | the follower's lock, deferred |
| 7 | resilience - ten hours at light load, three with the trigger, the resets under load closed | 60% | 4 | 1.60 | a soak under load ([279](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/279)); recovery still needs a host reboot |
| 8 | protocol and hardware understanding | 99% | 15 | 0.15 | |
| 9 | the control plane | 100% | 10 | - | |
| 10 | the outbound datapath - confirmed against a machine off the appliance | 100% | 10 | - | |
| 11 | the rest of the appliance - see below | 71% | 6 | 1.74 | the sensors ([164](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/164)), the panel ([165](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/165)) |
| 12 | flow offload - a connection `pf` has passed, forwarded by the coprocessor in both directions, with its translation, 99.99% of its frames in hardware | 85% | 10 | 1.50 | IPv6 flows, ~~jumbo~~ (a port's MTU is any value to 9,000 since 2026-10-12, [the MTU is the LIF's](../findings/the-mtu-is-the-lifs.md); the ten switch ports' own frame limit unread), [315](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/315), [317](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/317) |
| 13 | IPsec offload - the kernel's own contract, both directions through the engine, 951 down and 912 up, rekeys without a drop; NAT-T and outbound ESN since 2026-10-12 | 85% | 10 | 1.50 | the rest of [294](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/294) - IPv6, ~~ESN~~ an arriving ESN association (left to the kernel, measured), the other lengths, transport; the clear-text setting is a documented limitation since [304](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/304) closed, not a remainder |

Weights sum to 123. **Weighted: 92%.** The percentages are a judgement, as the first table's
were; the issues beside them are not.

**Re-weighed on 2026-10-17**, the two tables above left as they stood. One row added for the pages
that did not exist on 2026-10-12, and every remainder named:

| | component | done | weight | still missing | what holds it |
|---|---|---|---|---|---|
| 1 | the receive rate - 320 and 321 closed; one ring left with packets under a NAT soak, caught by the driver's own watchdog | 97% | 20 | 0.60 | [361](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361) |
| 2 | network interfaces - twelve, assigned by the first boot itself (measured on the appliance 2026-10-17), with their own section of the menu | 98% | 12 | 0.24 | |
| 3 | all twelve front ports - bound, addressed, presented | 100% | 8 | - | |
| 4 | interrupts - MSI-X, one vector per ring | 100% | 6 | - | |
| 5 | performance - the 10G cages measured at 10G: one stream 7.11 Gbit/s clear and 7.41 through IPsec into an XGS 3300 at MTU 9000, 3.40 out at 1,500 ([mtu-2026-10-17](../measurements/mtu-2026-10-17.md)) | ~~85%~~ 95% | 7 | ~~1.05~~ 0.35 | a copy per frame each way; ~~nothing yet measured the way the datasheet measures - many connections through the box at MTU 1,500~~ the datasheet's way measured on 2026-10-19 - a thousand connections at MTU 1,500, 1,032 in hardware, the 10 Gbit/s link full at 9,837 Mbit/s ([xgs3300-many](../measurements/xgs3300-many-2026-10-19.md)) |
| 6 | persistence - boot hook, bring-up, a kernel stamp that is enforced, the kernel followed through an update with the follower's lock, the switch chip's jumbo mode and the temperatures at every boot | 100% | 5 | - | |
| 7 | resilience - the soak under load closed ([279](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/279)); a stopped fast path taken up again in place, the host not restarted ([measured](../measurements/coprocessor-recovery-2026-10-26.md)); the coprocessor's watchdog feeder looked after from the host, so it no longer costs a reset ([measured](../measurements/coprocessor-feeder-2026-10-27.md)) | ~~65%~~ 85% | 4 | ~~1.40~~ 0.60 | [361](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361): a deaf ring now and then, found by the look within a tick or two, its cause not known ([measured](../measurements/deaf-ring-wakes-2026-10-27.md)); ~~recovery still needs a host reboot~~ |
| 8 | protocol and hardware understanding | ~~99%~~ ~~90%~~ 95% | 15 | ~~0.15~~ ~~1.50~~ 0.75 | **re-weighed 2026-10-22**: a sweep of the vendor's sources and the coprocessor's own log found capabilities this project had not understood - [the discoveries](../findings/discoveries-2026-10-22.md); **2026-10-25: every one of them measured and written down** ([680](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/680) closed); not yet understood: why a reset of the coprocessor by its own watchdog freezes the host ([768](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/768)), where Dragonfly's exports go, and why the engine drops ChaCha20 |
| 9 | the control plane | 100% | 10 | - | |
| 10 | the outbound datapath - confirmed against a machine off the appliance | 100% | 10 | - | |
| 11 | the rest of the appliance - see below | ~~79%~~ 93% | 6 | 0.42 | ~~the port identify light;~~ the fail-to-wire relay, read and deliberately not driven |
| 12 | flow offload - 315 and 317 closed, the switch ports' frame limit read and raised to 10,240 at every boot; since 2026-10-18 IPv6 too, 9.24 and 9.27 Gbit/s with all eighteen directions in hardware ([ipv6-and-queues-2026-10-18](../measurements/ipv6-and-queues-2026-10-18.md)), and a table the size the board states, 2,000,000 ([board-limits.md](board-limits.md)) | ~~88%~~ ~~96%~~ ~~100%~~ ~~95%~~ 98% | 10 | ~~1.20~~ ~~0.40~~ ~~-~~ ~~0.50~~ 0.20 | ~~**since 2026-10-22: a connection in hardware has its TCP window checked neither by pf, which marks its state sloppy, nor by the coprocessor, which could - [the TCP window, in hardware](../findings/discoveries-2026-10-22.md#the-tcp-window-in-hardware), [679](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/679)**~~ the coprocessor checks it since the same evening, filled from pf's own numbers, and a take-back gives pf the fast path's numbers back ([measured](../measurements/tcp-window-2026-10-22.md)); still not checked: a FIN or RST, which the fast path hands to the host, and a connection after the fast path gave it back; ~~IPv6 flows~~ ~~an IPv6 connection pf translates stays with the host~~ it does, and has to: the fast path's translation carries IPv4 addresses only (`struct usfp_nat_info`: `ipv4_orig_src`, `ipv4_nat_src` and their pairs, in the vendor's `conn_table.h`), so a translated IPv6 connection is not one its entry can describe - a limit of the coprocessor's firmware, counted in `dp.flow_v6_refused`, not of this driver (2026-10-20) |
| 13 | IPsec offload - [294](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/294) closed: IPv6 outside and inside, NAT-T, CBC with HMAC, windows to 4,096; GCM-8/12 and transport are not the engine's and are documented, ~~an arriving ESN association is left to the kernel~~ an arriving ESN association on the engine too since 2026-10-20 ([measured](../measurements/esn-3300-2026-10-20.md)) | 95% | 10 | 0.50 | ~~many tunnels measured the way the datasheet measures~~ sixteen tunnels at once measured on 2026-10-20, 9,019 Mbit/s from the PC's one 10G port ([measured](../measurements/many-tunnels-3300-2026-10-20.md)); the vendor's figures name 6,500 tunnels and 31.1 Gbps, which this bench cannot reach |
| 14 | the pages - one section of the menu: Overview, Settings, Ports, Transceivers, IPsec, Offloaded Flows, Updates, Problem Reports with a support bundle, Log File; three dashboard widgets | ~~70%~~ ~~87%~~ ~~90%~~ ~~95%~~ 97% | 6 | ~~0.78~~ ~~0.60~~ ~~0.30~~ 0.18 | ~~dashboard widgets, transceiver readings,~~ ~~port error counters,~~ the switch's port counters on XGS: Ports since 2026-10-20 for Port1 to Port8, F3 and F4 ([measured](../measurements/port-errors-136-2026-10-20.md)) - ~~F1, F2 and the module's ports are the coprocessor's own MACs and not read yet~~ and F1, F2 and the module's ports from the coprocessor's own MACs the same evening ([measured](../measurements/mac-counters-3300-2026-10-20.md)); ~~the coprocessor's health,~~ ~~the offloaded flows, a support bundle,~~ port mirroring and rate limits - the coprocessor's own uptime, load and memory on the Overview since 2026-10-20 |
| 15 | what the vendor's firmware does with this hardware and this project does not yet - **found 2026-10-22**: the coprocessor's own deep packet inspection and threat detection (Dragonfly, built on nDPI), a transmit rate limit for each front port, the TCP window checked in hardware, inspection that hands a connection back to hardware, live users, the fast path's heartbeat | ~~0%~~ ~~10%~~ ~~20%~~ 90% | 10 | ~~10.00~~ ~~9.00~~ ~~8.00~~ 1.00 | **2026-10-25**: inspection that hands a connection back to hardware ([measured](../measurements/inspect-2026-10-23.md)), live users ([measured](../measurements/luid-2026-10-23.md), the Captive Portal's [since 2026-10-24](../measurements/cp-hardware-2026-10-24.md)), and the transmit rate limit for each front port, which the coprocessor's own firmware refuses at its boot ([measured](../measurements/port-qos-2026-10-24.md)) - a limit of the firmware, as row 12's IPv6 translation is; left: Dragonfly, turned on and measured - it inspects, its exports reach nothing seen, and it loses a tunnel's traffic ([measured](../measurements/dragonfly-2026-10-25.md)). [The discoveries](../findings/discoveries-2026-10-22.md) - worked on one at a time; the TCP window in hardware since 2026-10-22, not yet after a give-back ([measured](../measurements/tcp-window-2026-10-22.md)); the heartbeat since 2026-10-22, answered every time, a stopped fast path seen in 30 seconds and said, ~~not yet acted on~~ reported, and the appliance restarted if the owner chose so ([measured](../measurements/heartbeat-2026-10-22.md#what-is-done-when-it-stops)) ([measured](../measurements/heartbeat-2026-10-22.md)) |

Weights sum to 139. **Weighted: ~~94%~~ ~~95%~~ ~~96%~~ ~~97%~~ ~~89%~~ ~~90%~~ ~~96%~~ 97%** (2026-10-17, after the identify light and four

> **Why this figure went down on 2026-10-22 - and why that is good news.** A sweep of the vendor's own sources, its board package and the coprocessor's own log found capabilities of this hardware that this project had not known about: a deep packet inspection and threat detection engine running on the coprocessor itself, a transmit rate limit for each front port, the TCP window checked in hardware, inspection that hands a connection back to hardware, live users, and a heartbeat with the fast path. It also showed that a connection in hardware has its TCP window checked nowhere today. These discoveries matter a great deal, so the table now counts them instead of leaving them out: row 8 (understanding) at 90%, row 12 (flow offload) at 95%, and a new row 15 at 0% for what the vendor's firmware does with this hardware and this project does not do yet. They will be worked on one at a time and arrive as updates; every one of them, with its evidence, is on [the discoveries page](../findings/discoveries-2026-10-22.md). **The first arrived the same evening:** the coprocessor checks the TCP window of a connection it holds, and a take-back gives pf its numbers back - row 12 at 98% and row 15 up for it ([measured](../measurements/tcp-window-2026-10-22.md)). **The second the same evening:** the heartbeat with the fast path - asked every five seconds, answered every time, a fast path stopped for 45 seconds seen late, then taken as stopped, then back - row 15 at 20%, the remainder 13.39 of 139, still 90% ([measured](../measurements/heartbeat-2026-10-22.md)). **2026-10-23:** the coprocessor reached over its PCIe management link instead of its console - one command in 0.77 seconds instead of 11 - which row 15 never weighed, so no figure moves for it ([measured](../measurements/mgmt-link-2026-10-23.md)). **The same day:** a connection a cut names, dropped on the coprocessor itself by its verdict (discovery 3) - a UDP flood cut sent the host 10 frames in ten seconds against 610,533 - which row 15 never weighed either ([measured](../measurements/verdict-2026-10-23.md)). **2026-10-24:** pf's DSCP mark kept on a connection in hardware (discovery 8) - a normalization rule's EF arrived as 0xb8 both ways, where a connection in hardware had left as 0x0 - which row 15 never weighed either ([measured](../measurements/flow-dscp-2026-10-24.md)). **The same day:** a connection a bridge carries made a bridged flow (discovery 9) - before, it was made as a routed one, whose frames went out of the WAN port when the bridge had no address ([723](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/723)) - which row 15 never weighed either ([measured](../measurements/bridged-2026-10-24.md)). **The same day:** a connection routed by a VLAN subinterface made in hardware (discovery 10), the coprocessor writing its tag - where every VLAN's frames had crossed the host - which row 15 never weighed either ([measured](../measurements/vlan-2026-10-24.md)). **2026-10-25, every discovery measured** ([680](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/680) closed): of the six row 15 named, five are done or settled - the TCP window, the heartbeat, inspection then hardware, live users, and the rate limit for each port, which the coprocessor's own firmware refuses - and Dragonfly is left, turned on and measured but not usable as it stands; row 15 at 90%, row 8 at 95%, the remainder 5.64 of 139, **96%**.

pages; 2026-10-18, after IPv6 flows were measured in hardware - row 12; 2026-10-20, row 5 at 95% once a thousand connections filled the 10 Gbit/s link - the remainder 4.84 of 129, still 96%; the same day row 14 at 90% once the coprocessor's health was on the Overview, and row 12 at 100% once an IPv6 translation was found to be beyond the firmware's connection entry - ~~the remainder 4.26 of 129, 97%~~; the same evening row 14 at 95% once the switch's port counters were on XGS: Ports - the remainder 3.96 of 129, still 97% - and at 97% once the coprocessor's own MACs were read for F1, F2 and the module's ports - the remainder 3.84 of 129, still 97%; **2026-10-22, re-weighed down on purpose** after the discoveries - row 8 at 90%, row 12 at 95% and a new row 15 at 0%: ~~the remainder 15.69 of 139, 89%~~ the remainder 15.69 of 139, 89%; the same evening row 12 at 98% and row 15 at 10% once the first of them, the TCP window, was checked in hardware - the remainder 14.39 of 139, 90%; **2026-10-25**, once the discoveries were all measured - row 8 at 95% and row 15 at 90%: the remainder 5.64 of 139, 96%; **2026-10-27**, row 7 at 85% once a stopped fast path was taken up again in place and the coprocessor's watchdog feeder was looked after from the host: the remainder 4.84 of 139, 97%).

**Four rows are finished** and they carry 34 of the weight between them. Row five used to read 0%
and hold back nearly seven points; measuring it is what changed that, because **for ten of the
twelve front ports the driver has six and a half times the headroom it needs** and only the two 10G
cages are capped. What is left of it is real but it is now bounded, and it is the same work as row
one: a copy per frame and one queue per direction.

Row 11 is itemised, because an average over six unequal things is the kind of number that deserves
to be shown rather than asserted:

| the rest of the appliance | |
|---|---|
| the CPLD, reached and driven | 100% |
| the SFP cages - `tx_disable` at the CPLD, and the SERDES after every insertion | 100% |
| the panel LEDs behind the switch | 100% |
| the front panel - ~~protocol read out of the vendor's daemon~~ (those bytes are another back end's: the panel's bridge carries out no command at all and writes round a ring of forty cells, measured 2026-10-17, issue [165](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/165)), ~~**not yet seen**~~ seen on 2026-10-02: two lines read off it at 9600, 20 columns by 2; what paints it is the separate os-frontpanel plugin, nothing in this repository; since 2026-10-17 drawn by that plugin itself, keys and menu working | 50%, 75% on 2026-10-12, 100% on 2026-10-17 |
| the thermal sensors and fans - ~~mapped, and three roads to them closed~~ read from the host bus ([164](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/164) closed), and the coprocessor's die and the inlet air in OPNsense's own sensor list since 2026-10-17; the bank's flags too since 2026-10-27 - the power supplies, fan 1's under-speed flag and the reset flags, said in the system log when one changes ([854](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/854)) | ~~0%~~ 100% on 2026-10-17 |
| the fail-to-wire relay - readable and read, deliberately not driven | 50% |
| the port identify light - ~~the command reaches the coprocessor, and the owner saw no port blink (2026-10-17)~~ the vendor's `xgs-led` and `xgs-led-identify`, reached ~~over the coprocessor's console~~ over its management link since 2026-10-23 by `npu-run.sh`, light Port4 - the owner watched it on 2026-10-17; Link Health's identify button takes the same road | ~~0% on 2026-10-17~~ 100% on 2026-10-17 |

**And one thing on this appliance cannot be done at all**: PortF1 and PortF2 have **no LED key in
the board file**, so they have no indicator by design rather than by omission. It is not counted
against anything.

## What it carries, in one table

Taken on one XGS 3300 - **the readings, the method and what each one does not mean are in
[docs/measurements/xgs3300.md](../measurements/xgs3300.md)**, because a measurement belongs to a
board rather than to a family.

| | |
|---|---|
| round trip over a copper panel port | 0.193 ms average, **0% loss** over 30,000 full-size frames |
| what the wire takes | **1,023 Mbit/s**, which is line rate for a gigabit port |
| what the driver accepts, one port | **535,142 pps** |
| what a 1G port needs | 81,486 pps - **6.5x headroom** |
| what a 10G port needs | 814,863 pps - **65% of line rate** |
| **what the whole appliance puts on the wire** | **1,023 Mbit/s**, all twelve ports together |
| what one port's load costs the other eleven | **nothing**, at every rate up to 6.5 Gbit/s offered |

**So for ten of the twelve front ports this driver is nowhere near the bottleneck**, and only the
two 10G SFP+ cages are capped. That is what row five above is now about, and it is a bounded
problem rather than an open one.

**Read the first three rows one port at a time.** The appliance's aggregate is the fourth row and it
is a different, smaller number: two ports offered 614 Mbit/s each pass 1,023 Mbit/s between them,
which is the same figure one port alone produces and the same again when the offered load is
quadrupled. That ceiling is the coprocessor's, not this driver's - it accepts 6.5 Gbit/s over PCIe
in the same runs and puts a gigabit of it on the wire.

**The fifth row was 29% loss until 2026-10-01.** Every interface copied its frame into one shared
buffer and posted an instruction pointing at it, so a frame could be overwritten before the
coprocessor read it; a ping on the WAN lost a third of its echoes whenever another port was busy,
with no counter anywhere reporting a drop. One buffer per instruction slot took it to zero at every
rate. See [the measurements](../measurements/xgs3300.md).

**Two cautions carried here because they change how a reading is read.** `Opkts` counts what the
driver accepted and posted, not what left the port - in the ceiling run it claimed 6,432 Mbit/s out
of a gigabit port, which was the 84% the coprocessor dropped being counted as sent. And the
received byte counter was doubled until #171, because the stack adds it too unless a driver claims
`IFCAP_HWSTATS`.


## Multicast, and therefore IPv6

A front port passes multicast only when the host asks for it. The switch's TCAM entry for a port
names its unicast address and nothing else, so a solicited-node group frame never arrives, and
without that there is no neighbour discovery and no IPv6 at all.

`SIOCADDMULTI` and `SIOCDELMULTI` used to be accepted and ignored. They now count the interface's
link-layer groups and record what the stack wants; the link poll, which already runs once a second
on a sleepable thread, tells the far side with NetAgent `op 0x03 sub 0x46`, all-multicast, once per
change rather than once per tick. A firmware that does not implement the attribute answers with a
clean failure, and the driver says so on the interface rather than asking again forever.

**Measured on the appliance**: enabling IPv6 on a front port logs `all-multicast on`, and a ping to
`ff02::1` on a cabled port comes back from more than twenty real neighbours with their addresses
learned into the neighbour table. Taking the groups away logs `all-multicast off`.

## Updating OPNsense without losing the WAN

**On an appliance whose WAN is one of the coprocessor's own front ports, a kernel update has a
circle in it**, and the obvious order walks straight into it:

```
update, reboot  ->  the module's stamp no longer matches uname -v
                ->  the bring-up refuses it, which is what it is for
                ->  no oxp interfaces
                ->  no WAN, because the WAN is one of them
                ->  no way to fetch the sources that would rebuild the module
```

The twelve ports that need the module are also the only road to the internet that would rebuild it.
On this appliance there is no USB network adapter to fall back on and no `/boot/kernel.old` to boot
back into, so the circle has to be broken before the reboot rather than after it.

**The order that works**, and each step says why:

1. **Install the update, and do not reboot.** The new kernel is now on disk and the old one is still
   running, which is the only moment when both are available.
2. **Ask the new kernel what it is.** It carries its own version string:

       strings -a /boot/kernel/kernel | grep -m1 'stable/'

3. **Fetch its sources while the old WAN is still up.** `fetch-sources.sh` takes that string as an
   argument rather than reading `uname -v`, precisely so it can pin to a kernel that has not booted:

       sh contrib/npuep/fetch-sources.sh "FreeBSD ... stable/26.7-nNNNNNN-<sha> SMP"

4. **Build against them and install, with the new stamp.** The module can be compiled for a kernel
   that is not running; it only has to match the headers.
5. **Then reboot once.** The stamp matches, the bring-up proceeds, and the twelve interfaces and the
   WAN come up together.

**If the build fails on the new kernel** - an API that moved, a header that split - stop before the
reboot. The old kernel is still the running one and nothing has been lost; the update can wait for
a driver change. That is the whole reason to do this in the order above: **the only step that
cannot be undone is the reboot**, so everything that can fail is made to fail before it.

Access to fix any of this does not run over the coprocessor: `igb0` is an ordinary Intel i210 on the
host's own PCIe and it keeps the LAN up whatever the datapath does.

## What does not work

~~**No offload is advertised and no jumbo frame is carried.**~~ ~~**No NIC offload is advertised,
and no jumbo frame is carried.**~~ **No NIC offload is advertised.** `if_setcapabilities(ifp, 0)` is
deliberate - no checksum, TSO or LRO is claimed, because none has been measured - and the IPsec
offload capability is advertised since [one encryptor per association](../findings/one-encryptor-per-association.md),
because that one has. ~~The buffer size caps the MTU at 1506, which is enough for 1500 and for a
VLAN tag and not enough for anything larger:
[measured](../measurements/xgs3300.md#jumbo-frames-are-not-available-and-the-number-is-1506).~~
Struck 2026-10-30: the measurements page struck it on 2026-10-12 and this page kept saying it - an
XGS 3100's owner read the 1506 here (issue 901). A port's MTU is any value from 72 to 9,000, the fast
path's own ceiling for a LIF, set on OPNsense's interface page with no reboot; a longer frame is
written over as many receive buffers as it needs ([the MTU is the LIF's](../findings/the-mtu-is-the-lifs.md)).

~~**Performance work has barely been started.** Every frame is copied, there is **one ring per
direction for all twelve interfaces** - not one per interface - and there is no offload of any kind:
no checksum, no TSO, no LRO, no distribution across queues. Each interface now has its own buffer
per ring slot, which is what makes the twelve independent of each other, but they still share the
ring, the lock and the doorbell.~~ That was 2026-10-02. **Every frame that crosses the host is
still copied once each way**, into an mbuf on receive and into the ring's buffer on transmit. The
host now takes **eight receive rings**, a connection hashed to one of them by its tuple, and one
transmit ring; one stream through the host reads 755 to 797 Mbit/s and four read 947 to 967
([a visit is not a pass](../findings/a-visit-is-not-a-pass.md),
[a ring that fills](../findings/a-ring-that-fills.md)); and a connection the coprocessor forwards
itself does not cross the host at all ([one connection, two microflows](../findings/one-connection-two-microflows.md)).
No checksum, TSO or LRO offload.

**Recovery needs a host reboot.** The datapath attaches once per coprocessor boot, so unloading the
module takes the ports with it and a reload cannot be answered - a host reboot is what restarts the
coprocessor. ~~Nothing has been soak tested for hours of continuous traffic either.~~ Ten hours at
light load and three with the offload trigger have been run since
([measured](../measurements/xgs3300.md#ten-hours-at-light-load)); a soak under load is
[issue 279](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/279).

**The module is built on the appliance** against the running kernel's headers, because OPNsense
ships no kernel sources. That is forced rather than chosen. A stale one is no longer loaded
silently: `build.sh` stamps it with the kernel it was built against, and the bring-up refuses to
load a mismatched module when none is running and warns when one is.

A working **vendor** host side is three modules: `octnic` creates `oct0`, `mv_nwa_host` calls
`register_pport_device` once per tag from the NetAgent port list, and `pport` creates a virtual
netdev per front port over `oct0`. **None of that reaches the coprocessor** - all three register
with the host's own pport layer and send nothing - so the gap was never a registration handshake.
What tells the coprocessor's fast path to hand a received frame to the host is the `rpc` facility:
`PPORT_UPDATE` maps a port tag to an interface and `LIF_ADD_UPDATE` installs the LIF. Promiscuous
mode was tried, is accepted with status 0, and is **not that gate** - a frame reaches the host
without it.

It is not therefore unnecessary. A front port's hardware filter admits unicast only for the address
that port owns, so a **bridged** port - where every reply carries the bridge's address - receives
broadcast and nothing else, which looks like a live port and is not. The driver follows
`IFF_PROMISC` and sends attribute `0x45` per port as the flag changes; see
[../measurements/xgs3300.md](../measurements/xgs3300.md).


## The channel that programs the fast path

The wire format of the `rpc` facility - the rings, the descriptors, the command numbers and the LIF
entry the wire-to-host gate consults - is in [octeon-tx-rpc.md](octeon-tx-rpc.md), read out of the
coprocessor's own `usfp_rh.ko` at the release this board runs.

## Registers whose reads are not what they look like

| register | what it does |
|---|---|
| `R_OUT_CNTS` `0x10100` | reads `0x2000000000000000` on an idle ring - flags in the upper half, not a count |
| `R_OUT_SLIST_RSIZE` | reads back 16 after being written 0; 256 sticks |
| `R_IN_INSTR_DBELL` | low half is the outstanding count; ~~a field at bit 38 accumulates~~ the high half, based at bit 32, is the fetch pointer's byte offset into the instruction ring, (instructions consumed mod RSIZE) x 64 - one post moves it by 64 << 32, which is the 1 << 38 that made it look like a field based at bit 38 |

`R_OUT_PKT_CNT` `0x10180` and `R_OUT_BYTE_CNT` `0x10190` were checked against the vendor's own
`cn83xx_pf_regs.h` and are correct, so a zero there is a true zero.

**Never read BAR1 entry 15.** It is the GICD window and reading it hangs the appliance hard.
