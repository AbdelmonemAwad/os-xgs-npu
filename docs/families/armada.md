<!-- reviewed: 2026-10-20 -->
# ARMADA - Marvell CN913x

    PCI id     11ab:7080
    driver     npuep  (contrib/npuep)
    platforms  xgsdt1, xgsdt2-116, xgsdt2-126136, xgsdt2-138
    hardware   Sophos XGS 136, assembly AMDA0201, 14 ports - ON THE BENCH
    state      all fourteen front ports carry traffic, connections forwarded in hardware -
               96% by the table below (re-weighed 2026-10-20)

This is the family the project started on and the only one where the datapath works. Everything in
the top-level README's "What works" section is this family, and the protocol documents describe it:

- [../facility-protocol.md](../npuep/facility-protocol.md) - the facility framework and the handshake
- [../giu.md](../npuep/giu.md) - the GIU datapath, which is what carries packets
- [../mvmgmt.md](../npuep/mvmgmt.md) - the management netdev
- [../netagent.md](../npuep/netagent.md) - the network agent and the switch
- [../rpc.md](../npuep/rpc.md) - the RPC facility
- [../npu-bring-up.md](../npuep/npu-bring-up.md) - what has to happen, in order

## Where this stands

First weighed on 2026-10-17, in the same form as [the XGS 3300's table](octeon-tx-reference.md#where-this-stands):
each row says what it covers, the percentage is a judgement, and the column beside it names what
holds the remainder.

| | component | done | weight | still missing | what holds it |
|---|---|---|---|---|---|
| 1 | the receive rate - one stream 773 Mbit/s in and 934 out with the 1 ms poll ([415](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/415)), 976 and 989 at MTU 9,000; a thousand connections at once through the box at MTU 1,500, the link full, in hardware ([measured](../measurements/xgs136-hardware-2026-10-18.md#a-thousand-connections-at-once), 2026-10-18) | 95% | 20 | 1.00 | ~~nothing yet measured the way the datasheet measures - many connections through the box at MTU 1,500~~ the datasheet's own method - many HTTP connections - not taken |
| 2 | network interfaces - fourteen, assigned by the first boot itself (measured 2026-10-17: LAN bridge 192.168.1.1, WAN on Port1); IPv6 neighbours found on a routed port since 2026-10-18 ([455](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/455)) | ~~95%~~ 100% | 12 | ~~0.60~~ - | ~~the bridge is not rebuilt after a reload of the module - three `configctl` calls by hand~~ `reload.sh` puts the bridge, its members, every address and the management link back since 2026-10-20 ([measured](../measurements/reload-136-2026-10-20.md)); ~~one reload in sixteen stopped the host ([603](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/603))~~ a reload while the engine held the tunnel's encrypting association, found and fixed the same evening ([measured](../measurements/reload-136-2026-10-20.md#why-one-reload-stopped-the-host-and-the-fix)) |
| 3 | all fourteen front ports - bound, addressed, presented; F1 with a ~~1000BASE-SX~~ copper module, an SFP-GE-T, since 2026-10-19 (read from the coprocessor's own log; ~~the host still calls it fibre~~ its port reports `1000baseT`, read from the module, since [#533](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/533) - [measured](../measurements/cage-medium-2026-10-19.md)), two streams of 2 GB through it counted to the frame ([measured](../measurements/xgs136-counts-2026-10-19.md#how-it-was-measured)) | ~~90%~~ 95% | 8 | ~~0.80~~ 0.40 | ~~the two SFP cages, F1 and F2, have never had a module in them~~ ~~F2 has never had a module in it~~ F2 holds a module too, an SFP-GE-T, with nothing on its other end yet |
| 4 | receive service - the rings polled every millisecond, 1,024 frames a pass | ~~90%~~ ~~95%~~ 100% | 6 | ~~0.60~~ ~~0.30~~ 0 | ~~receive is polled, not driven by the coprocessor's interrupts~~ the coprocessor's doorbells drive it since 2026-10-20 - 948 Mbit/s into the host with the poll at 10 ms, where the poll alone gave 270 ([measured](../measurements/rx-msix-136-2026-10-20.md)); ~~a loader tunable, off by default until a soak runs with it on~~ on by default since [an hour with it on](../measurements/rx-msix-136-2026-10-20.md#an-hour-with-the-doorbells-on) |
| 5 | performance - the gigabit link full from MTU 1,000 (908 to 961 clear, IPsec 2 to 8% below) | 80% | 7 | 1.40 | the 2.5G labels 11 and 12 not measured at 2.5G; the datasheet's way not taken |
| 6 | persistence - boot hook, a kernel stamp that is enforced, the kernel followed through 26.7 to 26.7.7 with nothing done by hand, the switch's jumbo mode, IPsec offload and the temperature at every boot | 100% | 5 | - | |
| 7 | resilience - two hours of TLS through the box with the flow maker on, 795 GiB and not a byte changed ([measured](../measurements/xgs136-soak-2026-10-19.md)); a connection the fast path gives back no longer loops ([measured](../measurements/xgs136-giveback-2026-10-19.md)) | ~~50%~~ ~~80%~~ ~~85%~~ ~~95%~~ 100% | 4 | ~~2.00~~ ~~0.80~~ ~~0.60~~ ~~0.20~~ - | ~~no long soak under load on this board yet~~ ~~a stream at a third of its speed for minutes after some boots ([531](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/531))~~ those slow minutes were the bench PC's, not the 136's ([measured](../measurements/xgs136-after-boot-2026-10-19.md#the-runs-that-settle-it-2026-10-20)); ~~no soak through the IPsec engine yet~~ an hour of TLS through the tunnel, 378 GiB and not a byte changed, 303 million frames decrypted on the engine and none bad ([measured](../measurements/soak-ipsec-136-2026-10-20.md)) - ~~an hour, where the clear-text soak ran two~~ and two hours the same evening, 756 GiB and not a byte changed, 605 million frames decrypted on the engine and none bad ([measured](../measurements/soak-ipsec-136-2026-10-20.md#two-hours-the-same-evening)) |
| 8 | protocol and hardware understanding | 95% | 15 | 0.75 | [383](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/383) - another board of this family whose facility table never appears |
| 9 | the control plane | 100% | 10 | - | |
| 10 | the outbound datapath - on all four transmit queues since 2026-10-18, measured with every queue in use ([ipv6-and-queues-2026-10-18](../measurements/ipv6-and-queues-2026-10-18.md#the-xgs-136s-four-transmit-queues)) | 100% | 10 | - | |
| 11 | the rest of the appliance - the coprocessor's temperature in OPNsense's sensor list, fan and PHY readings over the management link, PoE ~~read~~ read and switched from XGS: Ports ([measured](../measurements/poe-136-2026-10-20.md)), the port identify light (the owner saw it work) | 80% | 6 | 1.20 | the SFP cages' diagnostics, ~~unread without a module~~ ~~not read - both cages hold modules since 2026-10-19~~ read by the Transceivers page since 2026-10-19: the two SFP-GE-T modules carry none (their byte 92 is 0), so a module that does is still to be seen here |
| 12 | flow offload - ~~the driver's sweep put 11 connections in hardware over 2 next-hop entries~~ (every next hop named an empty cage until 2026-10-18, so none delivered - [the finding](../findings/every-next-hop-named-an-empty-cage-on-the-armada.md)); since 2026-10-18 the table is the size the board states, 1,500,000, its commands answered in 138 microseconds instead of ten milliseconds ([456](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/456)), IPv6 connections are made too, the flow maker opens the ports' entries itself, a connection given back is revived in place, and delivery is measured: 941 and 914 Mbit/s in hardware with the host's CPUs at 0.3 to 4 %, translated LAN to WAN as well ([measured](../measurements/xgs136-hardware-2026-10-18.md)); since 2026-10-19 what the coprocessor forwards is counted in pf and on the interfaces, to the packet ([measured](../measurements/xgs136-counts-2026-10-19.md#the-reader-measured)) | ~~85%~~ ~~92%~~ ~~95%~~ ~~97%~~ ~~98%~~ 100% | 10 | ~~1.50~~ ~~0.80~~ ~~0.50~~ ~~0.30~~ ~~0.20~~ - | ~~IPv6 flows built, not yet measured through the box~~ IPv6 in hardware measured too ([IPv6](../measurements/xgs136-hardware-2026-10-18.md#ipv6)); ~~flow writes each waited for~~ flow writes posted on the high-priority ring since 2026-10-18 - and the 95% did not know that a takeout took nothing out and a connection handed back stopped at pf until then ([the finding](../findings/a-takeout-took-nothing-out-on-the-armada.md)); ~~tunnel connections not in the table~~ tunnel connections in the table since 2026-10-20, their encrypting direction forwarded and encrypted by the fast path, set in XGS: Settings ([measured](../measurements/tunflows-136-2026-10-20.md)) |
| 13 | IPsec offload - AES-GCM and CBC with HMAC both ways in hardware, NAT-T, fragmentation before the envelope, replay windows to 4,096, every decrypted frame terminated; on by its setting since [418](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/418) | ~~85%~~ ~~95%~~ 100% | 10 | ~~1.50~~ ~~0.50~~ - | ~~IPv6, outside and inside, not yet taken on this board~~ IPv6 outside and inside, and extended sequence numbers both ways, taken on 2026-10-20 ([IPv6](../measurements/ipv6-136-2026-10-20.md), [ESN](../measurements/esn-136-2026-10-20.md)); ~~many tunnels not measured - tried on 2026-10-20, the lab's extra children were refused at the 3300's end ([the run](../measurements/many-tunnels-3300-2026-10-20.md#what-it-does-not-mean))~~ six tunnels at once the same evening, 6 of 6, 12 associations on the engine, 915 Mbit/s together ([measured](../measurements/many-tunnels-136-2026-10-20.md)) |

Weights sum to 123. ~~**Weighted: 91%** - re-weighed 2026-10-18 with row 12 at 92% (it was 85% on a delivery that was not happening): the remainder is 10.65 of 123, which is still 91%.~~ ~~**Weighted: 92%** - row 12 at 95% once IPv6 was measured through the box the same evening: the remainder is 10.35 of 123.~~ ~~**Weighted: 92%** - row 12 at 97% once the flow writes were posted and a takeout took a connection out, later the same evening: the remainder is 10.15 of 123.~~ ~~**Weighted: 92%** - re-weighed 2026-10-19 with row 3 at 95%, F1 carrying a module and measured, and row 12 at 98%, its counts reaching pf and the interfaces: the remainder is 9.65 of 123.~~ ~~**Weighted: 93%** - re-weighed 2026-10-20 with row 7 at 80%, once the integrity soak was run on this board: the remainder is 8.45 of 123.~~ ~~**Weighted: 94%** - re-weighed 2026-10-20 again, with row 12 at 100% once tunnel connections were in the table and row 13 at 95% once IPv6 and extended sequence numbers were taken: ~~the remainder is 7.25 of 123.~~ the remainder is 7.05 of 123 since row 7 went to 85% the same day, when the slow minutes of 531 were found to be the bench PC's - still 94%.~~ ~~**Weighted: 95%**~~ - row 7 at 95% the same day, once an hour of TLS crossed the IPsec engine without a byte changed: ~~the remainder is 6.65 of 123.~~ ~~the remainder is 6.45 of 123 since row 7 went to 100% the same evening, once the soak through the engine ran two hours - still 95%.~~ ~~The remainder is 5.85 of 123 since row 2 went to 100% too, once the reload that stopped the host was found and fixed - still 95%.~~ The remainder is 5.85 of 123 since row 2 went to 100% too, once the reload that stopped the host was found and fixed, and 5.55 since row 4 went to 95% once the coprocessor's doorbells drove the receive - still 95%. **Weighted: 96%** - row 13 at 100% the same evening, once six tunnels ran at once through the engine: ~~the remainder is 5.05 of 123.~~ the remainder is 5.05 of 123, and 4.75 since row 4 went to 100% once an hour of TLS crossed the host on the doorbells and they became the default - still 96%.

## What is specific to this family

The facility table sits at a **fixed offset** in the window and is guarded by a cookie -
`0xAFACAFAC` for the facility itself, `0xD0FAC10D` for the barmap - and its facility records are
`{bar, type, offset, size}`. Target-to-host doorbells are **MSI-X vectors**, which `npuep` allocates.

A fifth facility, **GIU**, exists here and is what carries the front ports. It does not exist on
OCTEON at all, and its absence there is the clearest single sign that these are different protocols
rather than one protocol with two PCI ids.

The reset polarity is a per-board table rather than a constant, so the module reads the assembly
number out of the bridge's EEPROM and looks it up. Values are carried for AMDA0200, AMDA0201,
AMDA0202-0205, AMDA0208 and AMDA0224. **Only AMDA0201 has been tested on real hardware.**

## What is held here, and what it allows

| material | detail |
|---|---|
| hardware | **XGS 136 on the bench**, assembly AMDA0201 |
| coprocessor rootfs | complete, including `NPU-BSP-slot1/rootfs/` |
| fast path | `usfp` for this family, 1.9 MB, with symbols |
| firmware | U-Boot for all four ARMADA platforms, from BSP 21.0.0.169 and 22.0.2.546 |
| source | the Sophos GPL release, 794 files, including the `pcie_ep_armada` patches |
| launcher | `armada_target_setup_usfp.sh`, selected by `DP_DT1/DT2_STARTUP_SCRIPT` |

This is the family with the most complete material and the only one whose ports all carry traffic.

## Building and running it

`npuep` is not packaged, for the same reason `octep` is not: an out-of-tree module has to be
compiled against the headers of the kernel that is actually running, and OPNsense ships no kernel
sources. So it is built on the appliance. `install/install.sh` lays the sources down in
`/usr/local/share/os-xgs-npu/npuep` and builds through `install/kernel-follow.sh` - the same
follower that rebuilds the OCTEON TX module after every kernel update, which since 2026-10-13
follows whichever of the two modules is installed - and `07-npuep` loads it at boot, after
reading the kernel stamp beside it and refusing a module built for another kernel (the previous
module is kept as `npuep.ko.prev` for the kernel it was built for). Before that date the follower
answered every tick on this appliance with "the driver is not installed on this machine", the
upgrade and stop hooks were gated on the OCTEON TX's PCI identity alone, and the boot hook read
no stamp: a kernel update here would have loaded the old module unchecked.

Measured on 2026-10-13, on the XGS 136, with the owner running the update from the GUI: OPNsense
26.7 to 26.7.7 brought the kernel from 15.1-RELEASE-p1 to p4. The reboot's stop hook ran the
follower; it read p4 from `/boot/kernel/kernel`, fetched that commit's sources, built the module
from the laid-down tree, kept the p1 module as `npuep.ko.prev` with its stamp, installed the p4
one and cleared the fall-back - 2 min 59 s in all - and the host came up on p4 with the module
loaded, all fourteen ports present and the WAN lease back, nothing done by hand.

    sh contrib/npuep/fetch-sources.sh
    sh install/install.sh

## The coprocessor from the host

The coprocessor's console is the host's second UART - `hint.uart.2.at="isa"`, `port="0x3E8"`,
`irq="3"` in `/boot/loader.conf.local`, so `/dev/cuau2` on OPNsense at 115200 - the same
arrangement as the OCTEON TX appliance's. A newline answers `marvell login:`; `root` with no
password gives a root shell on a Buildroot 2018.11 userland over Linux 4.14.207, with 3 GB of
memory and `usfp` running. An idle coprocessor prints nothing, so silence is not death. Read on
2026-10-14 on the XGS 136.

The helper that runs one command and returns its reply (as on the OCTEON TX box: a background
reader on the line, the command between two echoed markers) has one difference here: the marker
is sent in two quoted halves, `echo "---MARK$$""---"`, because this shell echoes the command line
back wrapped at 80 columns and bracketed-paste codes around it, and a marker that appears whole in
that echo is taken for the first marker. Keep each command short and plain; some pipes with
quoted field separators came back empty through the line and were not understood.

What the console gives on this appliance, all of it read-only:

- **The vendor's tools are on the coprocessor**, not on the host as they were under the
  original firmware on the OCTEON TX box: `xgs-platform` (the platform store: assembly
  AMDA0201-0003, revision 08, the base MAC and 16 addresses), `xgs-ports -b` (the real MAC of
  every label), `xgs-phy-temperature -l <label>`, `xgs-poe -m all`, `xgs-sff-decode -l <label>`,
  `xgs-eeprom -r`, `xgs-i2c-reg`, `xgs-mdio` (clause 22 and 45), `xgs-reg` (raw NPU registers),
  `xgs-led <phy|all> <left> <right>` and `xgs-led-identify -l <label> <seconds>` (the identify
  light this project's Link Health page has no path to from the host - here it has one, through
  the console; not wired), `xgs-sgpio`. Two are never run: `xgs-healthmond` and
  `xgs-1us-led-bus-test`.
- **There is no CPLD on the XGS 136.** `xgs-cpld -t` and every register read answer "Error
  opening cpld handle. No such device". The 1U sensor block - CPU, NPU and inlet temperatures
  and two fans behind a SMBus address, the one the OCTEON TX appliance reads from its host bus -
  is not on this board. Its temperatures are the host's own (`amdtemp`, the NCT6779D), the
  coprocessor's thermal zones (`ap-thermal-ic` and `cpu0..3`, and the two CP dies at
  `f2400078`/`f4400078`, 41 to 50 C idle), and the PHYs: `xgs-phy-temperature` reads 55 C on
  label 1 (an 88E6193X PHY) and 49 to 53 C on labels 9 to 12 (the AQR412C lanes).
- **The port map, from the vendor's own platform data** (`AMDA0201-0003R00.txt` in the GPL
  release, confirmed by the tools): labels 1 to 8 are the 88E6193X's ports 1 to 8; F1 and F2
  are its ports 9 and 10, SFP cages on `i2c1` (buses 2 and 1, 0x50 and 0x51) with GPIO lines
  for presence, TX fault, TX disable, RX LOS and rate select; labels 9 to 12 are the four
  lanes of the AQR412C on the CN9131's own ports, 9 and 10 rate-capped at 1G, 11 and 12 at
  2.5G and on the PoE module - one module, 62 W, status good, as `xgs-poe` reports.
- The fast path's syslog (tag `dpdk`) says `mrvl_mtu_set(): MTU too big, max MTU possible
  limited by current mbuf size` at start-up: the jumbo question for this family, unmeasured.
- `mvmgmt0`, the management link over PCIe, is up at the host and NO-CARRIER at the
  coprocessor and carries nothing (issue 374); the coprocessor runs `sshd`, so that link, once
  it works, is the fast channel. Until then the console is the only path.
- **Where to look, and what this was read from.** The parts named above are photographed on
  [the hardware page](../hardware/xgs136.md): the [88E6193X switch](../images/hardware/xgs136/88e6193x.jpg),
  the [AQR412C quad PHY](../images/hardware/xgs136/aqr412c.jpg), the [PD69104 PoE controller](../images/hardware/xgs136/pd69104.jpg),
  the [NCT6779D](../images/hardware/xgs136/nct6779d.jpg), the [CN9130 and its memory](../images/hardware/xgs136/cn9130.jpg),
  the [J1 LED header](../images/hardware/xgs136/j1-leds.jpg), with [the port chain](../images/hardware/xgs136/port-chain.svg)
  and [the board map](../images/hardware/xgs136/board-map.svg) drawn from them. The port map, the PoE
  module and the PHY addresses come from the vendor's own platform data file for this assembly,
  `data/AMDA0201-0003R00.txt` in the GPL release, read beside the tools' answers; the console
  helper and the link are on [mvmgmt.md](../npuep/mvmgmt.md) and
  [mvmgmt-in-opnsense.md](../npuep/mvmgmt-in-opnsense.md); the measurements of 2026-10-14 are
  on issues [374](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/374) and
  [372](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/372).
- **The plugins read and act over the link now.** With the management link in place, os-fanctl
  reads the coprocessor's seven thermal zones and the PHY behind every copper label in one
  command over it (nineteen readings in half a second, cached a minute; the groups `npu` and
  `nic` on its page), and Link Health identifies a front port by asking the coprocessor's own
  identify tool by the printed label - the chassis group says `identify: coprocessor`, and
  the capability is offered while the link's script is installed. Both measured on the XGS 136
  on 2026-10-14; the light itself was not yet watched.
- **The switch tool runs here unchanged.** `contrib/mvsw`, built for the OCTEON TX box, drives
  this board's 88E6193X through the same `mvmdio_uio` at bus 0, SMI address 2 (the board file's
  `mdio22:0:2`): `id` answers device 0x0193 revision 0; `dump` shows port 0 (the coprocessor's
  uplink) forwarding with link and a VLAN map to every port, every panel port forwarding with a
  VLAN map to port 0 alone - the same isolation as on the 3300 - and the two SFP cages at ports
  9 and 10; Port Control 2 reads jumbo mode 2 (10,240 bytes) on port 0 and mode 1 (2,048) on
  ports 1 to 10, the 3300's values exactly - as the coprocessor leaves them; since 2026-10-17 a
  start hook writes 10,240 into ports 1 to 10 at every boot (`scripts/xgs/switch-jumbo.sh`). It went over the console in base64, 500 characters
  a line, verified by checksum at both ends. One difference from the 3300: **about one read in
  ten returns another transaction's data** here - the coprocessor's own network agent polls the
  same MDIO bus, and the multi-chip access is a command write followed by a data read - so a
  register is read three times and the majority taken, and nothing here writes on the strength
  of a single read.
