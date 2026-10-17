<!-- reviewed: 2026-10-17 -->
# What works

*Moved here from the [README](../../README.md) on 2026-10-18, word for word, when the README was
made shorter. Struck claims stay struck, with what replaced them beside them; links were only
re-pointed to work from this folder.*

## ✅ What works

*This section is the **ARMADA** family - the XGS 136. For OCTEON TX and the XGS 3300, see
[Families](families-and-hardware.md#-families) and [docs/families/octeon-tx.md](../families/octeon-tx.md).*

**All fourteen front ports carry traffic, in both directions, as fourteen ordinary FreeBSD
interfaces.**

```
npuep0: giu: datapath enabled - 14 interfaces, 256 descriptors each way
npuep0:   Port1   npup1 tag 0x8100      Port2   npup2 tag 0x8200
npuep0:   Port7   npup7 tag 0x8700      Port8   npup8 tag 0x8800
npuep0:   Port9   npup9 tag 0x0001      Port10  npup10 tag 0x0003
npuep0:   PortF1  npup13 tag 0x8900     PortF2  npup14 tag 0x8a00
npuep0: rpc: all 14 front ports have an interface and a binding, read back and confirmed
npuep0: nwa: 14 of 14 ports up
```

Measured with three cables in, frames arriving on three ports at once and each landing on its own
interface:

```
dev.npuep.0.giu.rx_port.Port9: 34      dev.npuep.0.giu.rx_port.Port7: 27
dev.npuep.0.giu.rx_port.Port8: 10
```

Port 8 is behind the coprocessor's internal switch and Port 9 is a separate MAC on the SoC, so
both port families are proven together. A full ARP exchange completes over either — request in,
reply out — which is the smallest thing that requires both directions to work.

**The ports behind the switch chip carry jumbo frames.** On both appliances Port1 to Port8 and the two
cages beside them take their frame limit from the 88E6193X, which the coprocessor leaves at 2,048
bytes on every port at every boot; a start hook writes 10,240 into each port's Port Control 2 with
the switch tool it keeps on the coprocessor's `/persistent` partition, reading every port three
times because the coprocessor polls the same bus. After a reboot of both, with nothing set by hand,
a gigabit link between an XGS 3300 and an XGS 136 at MTU 9000 ran at 982 and 952 Mbit/s through an
IPsec tunnel ([the MTU page](../measurements/mtu-2026-10-17.md)).

**IPsec runs on the coprocessor's engine, both ways, from OPNsense's own IPsec configuration.** One
row in System > Settings > Tunables, `hw.npuep.ipsec_on` = 1 - the XGS 3300's `hw.octep.ipsec_on` on
this family, read when the driver loads - and the associations strongSwan installs on a front port
are encrypted and decrypted by the engine: 898 and 890 Mbit/s through a tunnel to an XGS 3300, after
a boot with nothing set by hand ([the bench](../measurements/bench-2026-10-17.md),
[issue 418](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/418)). The host's AMD `ccp(4)`
failed every software AES-GCM operation it was given, so on this appliance the boot hook holds
IPsec's sessions to software providers (`aesni`): a tunnel the engine does not take runs at the same
rate, 903 Mbit/s with `ccp` loaded ([issue 399](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/399)).

### And on OCTEON TX — the XGS 3300, which is a different and earlier story

A frame now goes in at a panel port and comes out on a FreeBSD interface carrying that port's own
address. **All twelve front ports are interfaces**, each with its own address read from the
coprocessor rather than invented:

```
oxp0   PortF1   the two 10G SFP+ cages, direct to BGX2
oxp1   PortF2
oxp2   Port1    the eight copper panel ports, behind the 88E6193X
...             every one at a gigabit, with its PHY powered and its LED lit
oxp9   Port8
oxp10  PortF3   the two 1G SFP cages, also behind the switch
oxp11  PortF4
```

Twelve interfaces, twelve distinct addresses, in the board file's own `lifport` order. **What is done and what is not** is a weighted table in [octeon-tx-reference.md](../families/octeon-tx-reference.md#where-this-stands) - the short version is 85% - and every number behind it, with how it was taken, is in [docs/measurements/xgs3300.md](../measurements/xgs3300.md). **Traffic is
proven on the three that have cables in them** - a 10G cage and two copper panel ports - and the
other nine are bound, addressed and presented the same way.

**IPsec, driven by OPNsense.** The kernel's own offload contract, `if_ipsec_accel_methods`, is
answered on every front port: an association strongSwan installs reaches the coprocessor's crypto
engine through the kernel, with no command typed, and both directions of a tunnel then run through
that engine - the inbound frames it decrypts are terminated by the driver, and for the outbound
association the driver stands where the kernel's own cipher would be called, so the coprocessor is
the **only** encryptor on it: a packet too big for the tunnel is answered or fragmented first, never
handed back to the kernel. An association behind NAT - ESP inside UDP 4500 - is taken too, since
2026-10-12, at the plain tunnel's rates
([the UDP header is the host's to write](../findings/esp-behind-udp.md)). Measured on a real
tunnel between two OPNsense appliances joined at
10 Gbit/s, four TCP streams from a machine on a 1 Gbit/s LAN port: **951 Mbit/s down and 912 up with
the coprocessor's cipher, 149 and 256 with the kernel's**, a rekey under load without a lost
sequence number. (The 149 and 256 were this driver's receive ring and not the kernel's cipher: 947
to 967 since the two changes that mended it - [a visit is not a pass](../findings/a-visit-is-not-a-pass.md),
[a ring that fills](../findings/a-ring-that-fills.md).) And a connection that runs
through the tunnel can be put in the coprocessor's flow
table **with its association**, so the host leaves its data path too: one TCP stream went from 75 to
195 Mbit/s across the host to **680 to 750 with both directions in hardware**, the host a few
hundredths of a core busy. (The 75 to 195 was a defect of the host's receive path, not what the
host can do: it carries that stream at 755 to 797 since
[a visit is not a pass](../findings/a-visit-is-not-a-pass.md), as fast as the flow table does. What the
setting buys now is the host's processor - most of a core at a gigabit against a few hundredths
of one - and not a connection's rate.) That is `dev.octep.0.ipsec.flows`, off by default, and its top
value is
a decision rather than a default: the fast path forwards a frame that arrives *in the clear* on the
tunnel's port if it matches a connection that is in hardware - read in its code, then measured,
15 of 20 - while the value below it keeps the kernel's check on everything that arrives and takes
only the direction that leaves encrypted. Honest limits: ~~IPv4~~ IPv4 or IPv6 inside ([IPv6 inside the tunnel](../findings/ipv6-inside-the-tunnel.md)); outside IPv4 or, since 2026-10-12, IPv6 ([the outer header can be IPv6](../findings/the-outer-header-can-be-ipv6.md)), ~~AES-GCM-16~~ AES-GCM-16 or AES-CBC with an HMAC (SHA-1/256/384/512) since 2026-10-12, the block cipher at two thirds of GCM's rate ([AES-CBC with an HMAC](../findings/a-block-cipher-at-two-thirds.md)); no GCM-8/12 and no transport mode, which are the kernel's and the vendor's refusals ([GCM-12 and transport](../findings/gcm-12-and-transport-are-not-ours.md)); ~~no ESN~~ ESN on the direction that leaves since 2026-10-12, the arriving one under ESN left to the kernel ([under ESN, what arrives stays with the kernel](../findings/esn-inbound-stays-with-the-kernel.md)), ~~no NAT-T~~
NAT-T since 2026-10-12 (issue 294); replay windows that are powers of two up to 4,096, measured from 64 to 4,096 ([a window wider than 256](../findings/a-window-wider-than-256.md)); once an association has been mirrored the module cannot be unloaded without a
reboot. A front port's MTU is any value from 72 to 9,000 since 2026-10-12, changed at any time
([the MTU is the LIF's](../findings/the-mtu-is-the-lifs.md)); the ten ports behind the switch
chip ~~have the switch's own frame limit still to read~~ are capped by the switch itself at 2,048
bytes, read the same evening (Port Control 2's jumbo mode; the uplink to the coprocessor takes
10,240), and raising a port is one write of that field, ~~not made yet~~ made on all ten at every boot since 2026-10-17 - see the ARMADA section above. Behind `dev.octep.0.ipsec.on` and the loader tunable `hw.octep.ipsec_on`, off by default.
The pages are [the kernel drives the coprocessor](../findings/the-kernel-drives-the-coprocessor.md),
[one encryptor per association](../findings/one-encryptor-per-association.md) and
[the flow carries the association](../findings/the-flow-carries-the-association.md).

**Two XGS 3300s, the engine at both ends.** On a bench of three appliances (2026-10-17) the two XGS
3300s were joined by a 10G fibre and a tunnel between them configured through OPNsense's own IPsec
pages, `hw.octep.ipsec_on` set as a tunable on both: one TCP stream from a PC into the far 3300's
host ran at 1.85 Gbit/s at MTU 1500 - the same as without the tunnel, where software on the near
end gave 1.25 - and at 7.41 Gbit/s at MTU 9000. The other direction is bounded by the measuring
PC's receive queue, not by the appliances: four streams carried 9.91 Gbit/s. The pages are
[the bench](../measurements/bench-2026-10-17.md) and [the MTU](../measurements/mtu-2026-10-17.md),
every run photographed.

**A connection the fast path gives back is answered at once.** The coprocessor hands a TCP
connection back to the host at the twelfth frame in a row with the same acknowledgement - which is
what every lost segment of a fast download produces - and the driver used to notice at its next
one-second poll. The receive path now kicks a task the moment a punted frame belongs to a
connection it holds, the connection is put back in service with one command, and a command's
answer is waited for in steps of twenty microseconds instead of a thousand. Four streams
downloading through the tunnel went from 23 to 42 % forwarded by the coprocessor to **95 to 97 %**,
the host from half a core to under a tenth. It is in
[a give-back is answered at once](../findings/a-give-back-is-answered-at-once.md), for plain connections
as for tunnelled ones.

**And `pf`'s state no longer runs out under a connection that is in hardware.** `pf` sees none of
an accelerated connection's packets, so nothing restamped its state: a UDP flow through NAT was
taken out about once a minute and came back with a different source port, and a TCP connection
whose state went was cut. A connection is now made only when `pf`'s states for it are on their long
timer, and every five seconds the coprocessor is asked which connections it is still forwarding and
`pf`'s states for those are restamped. Found by reading for something else; measured before and
after in [a state that sees nothing runs out](../findings/a-state-that-sees-nothing-runs-out.md).

**And a connection is made when its frames ask for it, not at the next poll** - with two older
faults found on the way that were worth more than the poll's second. The poll's eight attempts a
second were being spent on flows that can never be accelerated, and a connection behind them was
never made; and the table a connection's second direction is read from took the wrong bits of its
hash, so connections from one machine to one server shared a slot and were made one a second.
Four streams down through the lab tunnel for twelve seconds: 88 % forwarded by the coprocessor
before, 99 % now; eight streams up, 83 % and 100 %. Four streams that last two seconds: 87 % with
the poll alone, 100 % made between polls.
[A connection made between polls](../findings/made-between-polls.md).

**And one connection in sixty-four was never made at all**: the one whose two directions land in
the same slot of the table its second direction is read from. Found by a counter, shown on demand
by opening a few hundred flows and computing which of them collide, and ~~fixed with one more entry
that the missing direction is kept in~~ given one more entry that the missing direction is kept in
- which makes such a connection when nothing else wants the entry, and not otherwise. Measured on
main the same day and the next: with a held connection in each of the colliding flow's two buckets
of the table of live tuples, 0 of 7 were made; six that collide at once were made one a second,
the last after 5.9 and 8.0 s; and with no test running the entry was held, time after time, by
connections that can never be made. The entry is gone: **every slot now holds four tuples**, each
with a place of its own behind one busy word, and nothing is armed and nothing is timed.
[Issue 322](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/322); the rows on main are in
[the appliance's measurements](../measurements/xgs3300.md#the-candidate-table-with-more-than-one-user).
[A connection that collides with itself](../findings/a-connection-that-collides-with-itself.md), and then
[a slot holds both directions](../findings/a-slot-holds-both-directions.md).

**And a rekey under load no longer drops what is sent while the new association installs.** The
driver used to take the kernel's cipher away, wait, read the kernel's sequence counter and only
then give the coprocessor the association - and for those milliseconds every packet was dropped:
308, 368 and 540 of them in three of six rekeys. Now the coprocessor is given the association
first, started a million numbers past the kernel's counter, with the kernel's IVs moved to a half
of their space the coprocessor cannot reach; and the cipher is taken second. Twenty-three rekeys
under a four-stream upload: nothing dropped by the appliance, one packet dropped by the peer in two
of them. [Installed first, taken second](../findings/installed-first-taken-second.md).

**And the host carries one stream as fast as four.** One TCP stream through the tunnel ran at
75 Mbit/s where four filled the gigabit, and it was not the tunnel. The receive watchdog - a timer
that looks at every ring twenty times a second in case an interrupt is lost - made one pass over a
ring its handler was working; the handler found the ring held and left; and the ring then waited
for the timer, fifty milliseconds at a time. A servicer now holds a ring for its whole visit and
goes round until it is empty, and one that is turned away leaves a note the holder reads. One
stream across the host: 755 to 797 Mbit/s up and 769 to 793 down, where it was 74 to 76 and 88 to
362 - the same as with the flow table carrying it.
[A visit is not a pass](../findings/a-visit-is-not-a-pass.md).

**And a full ring is where the block stops.** With the kernel's cipher a tunnel on a 10 Gbit/s port
ran at 149 Mbit/s and the kernel dropped ESP frames by the thousand as replays, where the
coprocessor's own check of the same frames refused 44 in a million. The driver had been granting
each receive ring credit for sixteen rings, so when a ring filled the block went round it, over
frames nobody had read, and the host then read the newest lap first. The rows themselves came back
with the change before this one, which stopped rings being left to fill - 947 to 967 Mbit/s. This
one is what a ring does when it does fill: it is granted its own size above the coprocessor's
watermark, the block stops there, what does not fit is dropped by the coprocessor and counted, and
what the host reads is in order. An upload whose ring was made to fill had 802 frames dropped by
the appliance's kernel as replays before, and one or none after.
[A ring that fills](../findings/a-ring-that-fills.md).

**And a packet can be more than one buffer.** After nine hundred connections had been held in the
flow table, about one new connection in eight timed out until the appliance was rebooted, with
every counter the driver had at rest. The coprocessor pushes each connection's packet and byte
counts to the host in control messages, eighteen bytes for every connection that moved since the
last, and one that covers more than about eighty is longer than a receive buffer: the block
writes it on into the next buffer and counts it once. The driver took one buffer for each packet
counted, so each such message left ring 0 a buffer further behind the block, for good - 73 after
one run on main - and every frame on that ring was handed up that many arrivals after it had been
written. A packet is now taken as the buffers its length says it used, and a ring whose next
buffer has held a packet with no count for two ticks is taken without one. The same run:
sixty-two such messages taken whole, no buffer left behind, and every tuple delivered at once.
[A packet is not a buffer](../findings/a-packet-is-not-a-buffer.md).

**And the way back keeps up.** That page left two things unmeasured: a ring put behind by its
whole grant, and whether a busy ring ever gets the two quiet ticks the way back needs. Both were
put to the fault made on purpose - an instrument that acknowledges a packet and does not take it -
on one ring, under a slow stream, a stream as fast as the sender makes them, and a 780 Mbit/s
download that lost three thousand frames to it in bursts of sixty-four: every one came back, the
ring was never more than forty-two buffers behind at a reading, and the download kept its rate. A
ring behind by its whole grant is not reachable with TCP, which pauses for its acknowledgements
and hands the ring back in each pause. The one count that should not move, the watchdog's stalls,
now keeps a log of its last eight: three in six rekeys, each on a different ring with hundreds
waiting, none anywhere else, and not yet explained.
[The way back keeps up](../findings/the-way-back-keeps-up.md).

**And one of them is the appliance's WAN.** Panel port 2, assigned in OPNsense and asked for a
lease, gets one from the upstream router and installs the default route through itself:

```
DHCPDISCOVER on oxp3 ... DHCPOFFER ... DHCPACK ... bound
default            <upstream>         UGS            oxp3
```

That is the whole path working as a firewall would use it: a DHCP exchange is broadcast out, a
unicast reply back, through the switch, the coprocessor's fast path and the PCIe ring, and into the
host's own network stack. Everything below is how, and each step was a separate fault.

**The management link carries IP.** `octep0` is an ordinary FreeBSD interface and ping across PCIe
runs at 0% loss. That is one interface, not the front ports.

**The host programs an SDP datapath ring and frames cross it.** The ring pair is allocated and
programmed, the coprocessor's own fast path runs beside it, and frames posted by the host arrive:

```
IN_CNTS 8   IN_PKT_CNT 8   IN_BYTE_CNT 2144
```

`2144` is twice `(64+28)+(128+28)+(256+28)+(512+28)` — the hardware's own byte counter agreeing with
every `tlen` the driver wrote, at four frame sizes, which is what confirms the instruction format
rather than an inspection of it. The fast path survives the traffic: no core dumped.

**And NetAgent answers.** That is the front ports' control plane rather than their datapath - the
same handshake gates both, and before it the NetAgent window is blank and every request returns
`ENXIO` - but once it is up NetAgent answers with nothing plugged in:

```
op 0x01  marker 0x00000014 (expected)  status 0x00000000 (ok)  reply 2020 bytes
payload 503 words        commands 2   timeouts 0
```

The protocol was already in this repository, described in [docs/npuep/netagent.md](../npuep/netagent.md) and
implemented for ARMADA in `contrib/npuep/npunwa.c` — and the header this coprocessor publishes is
identical to the ARMADA one word for word, which is the first evidence from silicon that the
NetAgent **framing** is family-independent rather than merely looking it. The operation set is
not: from the host only four opcodes have a handler here, and every other one comes back with
status 1. The transport is proven and the reply is decoded: fourteen ports, thirteen of them
populated, each a 20-byte record whose first word is `tag | flags`.

A front port can be raised from here and the link read back, and a 10G fibre between the two SFP+
cages trains under OPNsense - proven by taking one end down and watching the other end's link follow.

**The whole loop now completes, and a frame has been read out of host memory at the end of it.**
The fast path keeps 182 named counters, and reading them around a controlled burst accounts for
every frame. With a fibre between the two SFP+ cages, six hundred frames posted on the host's ring:

| the fast path's own name | what it means | delta |
|---|---|---|
| `FPCNTR_RX_KN` | taken off the host's ring | **+600** |
| `FPCNTR_FROM_KN_TO_WIRE` | and routed to the wire, not to encryption | **+600** |
| `FPCNTR_TX_WIRE` | and transmitted | **+600** |
| `FPCNTR_RX_WIRE` | it arrives back on the other cage | **+600** |
| `FPCNTR_FROM_WIRE_TO_KN_FORCED` | no offloaded connection matches, so it is forced to the host | **+600** |
| `FPCNTR_TX_KN` | and the decision to hand it over is taken - this counts the decision, not the delivery | **+600** |
| `FPCNTR_TX_DROP`, `FPCNTR_TX_DROP_QUEUE_FULL` | nothing dropped anywhere, at this burst size | **0** |

and one of them was then found sitting in a host receive buffer, decoded:

```
ring 12 buf   0  len 134  tag 2  meta 0xb44399a2 - the vendor's
  ff:ff:ff:ff:ff:ff <- 02:00:00:00:00:01  type 0800
```

That is the driver's own test frame, back from PortF2, in memory this host owns.

**What it took, and each of these was a separate fault.**

*The metadata chooses the destination.* The 64 bytes in front of every frame are not filler. The
fast path reads one of them and, when it is non-zero, takes the four bytes after it as an egress
security-association handle and routes the frame to encryption instead of to the wire. The walking
pattern from `0xc0` makes that byte `0xdf`, so **every frame asked to be encrypted**, none could be,
and all of them were charged to `FPCNTR_TX_DROP`. Sending what the vendor's own hook writes - byte 0
set to 1, the other 63 zero - is what first put a frame on a wire.

*The host has to state the ring split, and build it.* `RINFO` is not only a description of how the
endpoint is carved up; it is how the host **states** the carving, and this driver only ever read it.
The vendor writes `RINFO |= (rpvf << 32) | (nvfs << 48)`, which on this appliance's own firmware
gives `0x0008000100400000` - eight VFs at one ring each, so the PF's rings begin at 8. Declaring
that is half of it: the functions have to exist too, in the SR-IOV capability, or the far side is
told about ring sets nothing on the bus backs and it goes down. Create, declare, then hand over -
and the far side's transmit queue drains for the first time.

*The rings must be programmed before the handshake, and never restarted.* The target latches the
host's ring addresses when its port opens and does not look again. Programmed first and left alone,
600 frames drop nothing; after two `dp.stop` / `dp.start` cycles, 400 frames give 192 queue-full
drops and it never recovers. Every earlier burst in this project was measured after a restart, which
is why the queue always looked permanently stuck.

*And the return prefix is 82 bytes, not 66.* Eight bytes of SDP info carrying the length, eight more
holding `0x8003000000000000`, then the 2-byte port tag and 64 metadata bytes, and only then the
Ethernet header. The 66 is the target's own 2+64; it is not what lands in a host buffer.

*The doorbell counts bytes of the scatter list, not entries.* This is the one that had held the
project for eighteen months, and it was never a missing mechanism. `R_OUT_SLIST_DBELL` counts
**sixteen per buffer** - the size of one scatter-list entry - so a grant of one credit per entry
granted a sixteenth of the ring, and the block's fetch pointer sat inside a descriptor rather than
on one. Granted in the block's own unit, 300 paced frames give `rx_done +308` and the far side's
`TX_DROP_QUEUE_FULL` stops moving. Nineteen negatives had been recorded before this, every one of
them a plausible missing mechanism; three were genuinely missing, were implemented, and changed
nothing. **When a number is off by a constant factor, that is the finding.**

*And the factor was not the unit.* What was measured stands - a grant of one per entry delivered
one packet, and sixteen times that ran. But the block spends one unit on a buffer, not sixteen. It
stops sending to a ring whose doorbell reads under the coprocessor's output watermark - 1,024 on
this firmware, a register the coprocessor's own driver writes and the host cannot reach. A ring of
256 granted 256 is under that, and sixteen times the ring was simply over it - with credit to
spare for fifteen rings more, so the block wrote round a full ring over frames nobody had read. A
ring is now granted its own size above that mark, less the sixteen the block fetches at a time.
[A ring that fills](../findings/a-ring-that-fills.md).

*The panel ports are behind a switch, and the switch had never been programmed.* Ten of the twelve
hang off a Marvell 88E6193X reachable only from the coprocessor, and every one of its panel ports
was left disabled by the vendor's own init. `contrib/mvsw` reaches it over `/dev/mvmdio-uio` and
brings them up; all eight copper ports run at a gigabit with their PHYs powered and the panel LEDs
lit to the board file's own scheme.

*The two SFP cages were held dark by one bit each on the CPLD.* `tx_disable` for the 1G cages lives
in CPLD register `0x25`, bits 4 and 10, and both read set while the two 10G cages' equivalents read
clear - which is why those two had always worked. Clearing them brings a cage up at a gigabit. A
module swap needs the SERDES woken again; the CPLD bit itself survives both that and a reboot.

*And the port tag is what the switch's DSA tag says it is.* Frames from a panel port reached the
coprocessor and died at `FROM_WIRE_DROP_LIF_INDEX_INVALID`, and the index was never the problem. The
switch's uplink runs in **DSA frame mode**, so every frame it sends the coprocessor carries a 4-byte
tag at offset 12 naming the source port; the fast path turns that into
`0x8000 | (src_port << 8) | (src_dev << 5)` and looks the result up. This driver had been binding
tags 1, 2 and 3 - values the far side never produces. Binding the tags it does produce, after naming
each port's own address to the switch so its TCAM entry stops being "Never Hit", empties both drop
counters:

```
RX_WIRE +10   FROM_WIRE_TO_KN_FORCED +10   TX_KN +10
FROM_WIRE_DROP_LIF_INDEX_INVALID +0   RX_BAD_PORT_TYPE +0
```

**And the frames land.** `oxp3` is panel port 1, with panel port 1's own MAC read from NetAgent
through that same tag, and it counts the packets that arrive on it. See
[docs/families/octeon-tx.md](../families/octeon-tx.md) for the measurements and the order the
bring-up has to happen in, which turns out to matter a great deal.


### Back on ARMADA - the XGS 136, port by port

**Twelve of the fourteen are verified port by port, with loopback cables.** An ARP exchange with
an outside device proves one path; it says nothing about the other eleven, and nothing at all when
the far end declines to answer. `contrib/npuep/portmap.sh` removes the far end from the question:
it transmits out of each port in turn and records which port hears it.

```
  sent on   heard on
  ...
  npup3      npup4(+1)        0x8300 → 0x8400   switch
  npup4      npup3(+1)
  npup5      npup6(+2)        0x8500 → 0x8600   switch
  npup7      npup8(+1)        0x8700 → 0x8800   switch
  npup9      npup10(+1)       0x0001 → 0x0003   SoC
  npup11     npup12(+2)       0x0004 → 0x0002   SoC
  npup13    - nothing -       SFP cage, no fibre to hand
```

Every pair symmetric, both tag families, and **the sending port's own counter never moved** — so
the coprocessor's switch does not forward between front ports behind the host's back. That last
one is not a detail: if it did, traffic would pass between two ports without `pf` ever seeing it.
The host is the only forwarder here, which is what a firewall needs.

It also settles the one part of the port table that was inferred rather than read. The four SoC
ports are tagged `0x0001, 0x0003, 0x0004, 0x0002` in connector order — not sequentially — and that
ordering came out of a disassembly. If two of those were swapped, a frame leaving `npup10` would
have arrived on `npup11` instead of `npup9`. It arrived on `npup9`.

`PortF1` and `PortF2` are the SFP cages and are **untested**: a copper patch lead cannot loop them.

The driver loads itself at boot, creates the interfaces, programs the coprocessor and verifies the
programming by reading it back. Nothing is typed.
