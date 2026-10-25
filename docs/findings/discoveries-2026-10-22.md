<!-- reviewed: 2026-10-24 -->
# What the hardware can do that was not known - the discoveries of 2026-10-22

> [!IMPORTANT]
> **These discoveries matter a great deal, and they lowered the completion figures on purpose.** A
> sweep of the vendor's own sources, its board package and the coprocessor's own log found
> capabilities of this hardware that this project had not known about - among them a deep packet
> inspection and threat detection engine running on the XGS 3300's coprocessor, a transmit rate limit
> for each front port, and the TCP window checked in hardware, which also showed that **a connection
> in hardware has its TCP window checked nowhere ~~today~~** - the first of them closed the same day:
> [the window is checked in hardware now](../measurements/tcp-window-2026-10-22.md). The family tables now count them: the XGS
> 3300 went from ~~97%~~ to **89%** ([its table](../families/octeon-tx-reference.md#where-this-stands))
> and the XGS 136 from ~~96%~~ to **91%** ([its table](../families/armada.md#where-this-stands)).
> **They will be worked on one at a time and arrive as updates**, ticked off in
> [680](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/680); each section below says where it stands.

Every section is marked with how far it has been taken:

| mark | means |
|---|---|
| **source** | read in the vendor's own source or headers; not yet tried on an appliance |
| **read** | read on the appliance itself - its coprocessor's log, its files, its registers - and photographed |
| **measured** | tried on the appliance, with the result |

The sources are the vendor's GPL release of SFOS v22 (SDK 10.22.03): the fast path's headers and the
coprocessor side of its RPC, the vendor's own host module that drives them, the board package and
the network agent's host module. Only names and facts are taken from them here - which structure
field, which command, which message - and no code; the project's position on that is in
[the README's licence section](../../README.md#licence). The readings were taken on 2026-10-22 on the
second XGS 3300 and on the XGS 136, read only.

## Where each capability lives

![Where each capability found on 2026-10-22 lives: the host, the coprocessor's fast path and its own software, the switch and the front ports, with what is on both appliances and what on the XGS 3300 only](../images/measurements/discoveries-2026-10-22/where-each-capability-lives.svg)

## All of them, in one table

| | discovery | where | XGS 3300 | XGS 136 | os-xgs-npu today |
|---|---|---|---|---|---|
| 1 | [The TCP window, in hardware](#the-tcp-window-in-hardware) | fast path | ~~source~~ **measured** | ~~source~~ **measured** | ~~**a gap**: checked nowhere~~ **checked in hardware since 2026-10-22** |
| 2 | [The window handed back](#the-window-handed-back) | fast path | ~~source~~ **measured** | ~~source~~ **measured** | ~~the take-back's TCP state not used~~ **given to pf at a take-back since 2026-10-22** |
| 3 | [A connection dropped by its verdict](#a-connection-dropped-by-its-verdict) | fast path | ~~source~~ **measured** | ~~source~~ **measured** | ~~verdict always "forward"~~ **a cut drops the connection on the coprocessor since 2026-10-23** ("Cut in hardware") |
| 4 | [Inspection, then hardware](#inspection-then-hardware) | fast path | ~~source~~ **measured** | ~~source~~ **measured** | ~~none - the frames go to a virtual function OPNsense does not make~~ **XGS 3300: made 2026-10-23, Suricata inspects a connection in hardware and the hardware carries the rest - off by default**; ~~the XGS 136 next~~ **XGS 136: made the same evening, the same way, the link full**; **a setting, "Inspect, then hardware", with OPNsense's own Intrusion Detection, the same evening** |
| 5 | [Live users](#live-users) | fast path | ~~source~~ **measured** | ~~source~~ **measured** | none - **a logout takes the user's connections out of hardware on both, measured 2026-10-23; ~~not used yet~~ the Captive Portal's clients in hardware since 2026-10-24** |
| 6 | [A priority bit and a meter for each connection](#a-priority-bit-and-a-meter-for-each-connection) | fast path | ~~source~~ **measured**: held, no effect seen | source | zero; an instrument since 2026-10-24 |
| 7 | [The QoS commands](#the-qos-commands) | fast path | ~~source~~ **measured**: not dispatched | source | none; ~~expected refused~~ refused, all seven |
| 8 | [DSCP rewritten for each flow](#dscp-rewritten-for-each-flow) | fast path | ~~source~~ **measured** | ~~source~~ **measured** | ~~zero~~ **pf's DSCP mark kept on a connection in hardware since 2026-10-24**; before, it was lost |
| 9 | [Bridged flows](#bridged-flows) | fast path | ~~source~~ **measured** | ~~source~~ **measured** | ~~routed flows only~~ **a connection a bridge carries is made a bridged flow since 2026-10-24, its frames left as they are**; before, it was made as routed - out of the WAN when the bridge had no address ([723](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/723)) |
| 10 | [VLAN interfaces](#vlan-interfaces) | fast path | ~~source~~ **measured** | ~~source~~ **measured** | ~~none made~~ **a connection routed by a VLAN subinterface made in hardware since 2026-10-24, the coprocessor writing its tag**; before, every VLAN's frames crossed the host |
| 11 | [A heartbeat with the fast path](#a-heartbeat-with-the-fast-path) | fast path | ~~source~~ **measured** | ~~source~~ **measured**, dropped - the beat taken is its answer | ~~counted, not sent~~ **sent since 2026-10-22, answered on the XGS 3300; on the XGS 136 since 2026-10-23** |
| 12 | [Mirrored packets](#mirrored-packets) | fast path | ~~source~~ **measured** | ~~source~~ **measured** | ~~counted only~~ **shown to the port's packet capture since 2026-10-25** (XGS: Settings "Copies for packet capture", off by default) |
| 13 | [Counters cleared](#counters-cleared) | fast path | ~~source~~ **measured** | ~~source~~ **measured** | ~~read only~~ **cleared by the tools' `-z` since 2026-10-25** |
| 14 | [IPsec state kept in step](#ipsec-state-kept-in-step) | fast path | ~~source~~ **measured** | ~~source~~ **measured** | none - **not needed by the design, measured 2026-10-25**; on the way: SFOS 23 refused every association (#759, fixed) and its counts read 0 (#761) |
| 15 | [Ciphers nobody has tried](#ciphers-nobody-has-tried) | IPsec engine | ~~source~~ **measured** | ~~source~~ **measured** | GCM and CBC with HMAC - **SHA-384 among them measured on all three versions, 2026-10-25; ChaCha20 refused by the datapath** |
| 16 | [Public-key operations](#public-key-operations) | coprocessor's engine | source, **read further 2026-10-25** | source, **read further 2026-10-25** | none - **the intrusion-prevention stage's, with no request format in the source; not built** |
| 17 | [A packet dump in the fast path](#a-packet-dump-in-the-fast-path) | fast path | ~~source~~ **measured** | ~~source~~ **measured** | ~~named, not used~~ **the bit that sends 12's copies, used since 2026-10-25** |
| 18 | [Frames injected from the host](#frames-injected-from-the-host) | fast path | source, **read further 2026-10-25** | source, **read further 2026-10-25** | not used - **the intrusion-prevention stage's way back into a flow; nothing here needs it; not built** |
| 19 | [The TCAM: 254 of 256 entries are the vendor's](#the-tcam-254-of-256-entries-are-the-vendors) | switch | **read** | not read | - |
| 20 | [Queue scheduling up to strict priority](#queue-scheduling-up-to-strict-priority) | switch | ~~**read**~~ **measured**: every frame from the coprocessor in queue 0 | ~~not read~~ **read**, the same | not used - **orders nothing: measured 2026-10-25** |
| 21 | [Two front ports never switch to each other in the chip](#two-front-ports-never-switch-to-each-other-in-the-chip) | switch | ~~**read**~~ **measured**: none crossed; routed, the host's rules decide | ~~likely~~ **measured**, the same | - **nothing to build: measured 2026-10-25** |
| 22 | [Dragonfly: deep packet inspection and threat detection](#dragonfly-deep-packet-inspection-and-threat-detection) | coprocessor | ~~**read**, running~~ **measured**: on, inspects; exports seen nowhere; a tunnel's traffic lost | **read**, absent | not enabled - **not to be: measured 2026-10-25** |
| 23 | [A transmit rate limit for each front port](#a-transmit-rate-limit-for-each-front-port) | coprocessor | ~~**read**, on~~ **measured**: refused at boot, the message goes no further | **read**, off | ~~port speed sent as the link's~~ never sent; does nothing when sent |
| 24 | [The coprocessor's own watchdog, memory reports, clock and randomness](#the-coprocessors-own-watchdog-memory-reports-clock-and-randomness) | coprocessor | ~~**read**~~ **measured**: fed every 5 s, 60 s; unfed, the host froze too; EDAC 0 | **read**: the same feeder, no EDAC | not used - **the host's watchdog brings the appliance back ([768](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/768))** |
| 25 | [The network agent's polling mode](#the-network-agents-polling-mode) | network agent | source | source | never sent |
| 26 | [VLANs, bridges and address entries through the agent](#vlans-bridges-and-address-entries-through-the-agent) | network agent | source | source | never sent |
| 27 | [A logger for the coprocessor's console](#a-logger-for-the-coprocessors-console) | board package | source | source | none |
| 28 | [The XGS 3300's management link, built and not used](#the-xgs-3300s-management-link-built-and-not-used) | host | ~~measured once~~ **measured** | - (in use) | ~~console used instead~~ **the way to the coprocessor since 2026-10-23** |
| 29 | [The fail-to-wire relay's watchdog](#the-fail-to-wire-relays-watchdog) | board | source | - | relay read, not driven |

## The fast path

The coprocessor's fast path understands 54 commands - SFOS 22's; SFOS 23 adds one, `SA_IF_MTU_SET`, and
numbers every command from 36 on one higher ([rpc-numbering.md](../families/rpc-numbering.md)). Its RPC module dispatches every one of them
except the seven QoS commands and the three DoS ones - which is why the DoS commands answered "not
dispatched" when they were tried ([dos-probe-2026-10-19](../measurements/dos-probe-2026-10-19.md)).
os-xgs-npu sends about thirty; these are what the rest do, and what the fields it leaves at zero do.

### The TCP window, in hardware

**~~source - and a gap today~~ measured, and done the same day, on both families** - the
coprocessor now checks the window of a connection it holds, filled from pf's own numbers: segments
outside the window are dropped in hardware and counted, an ordinary stream loses none
([measured](../measurements/tcp-window-2026-10-22.md)). What it found on the way, and what it does not
cover yet, is on that page. As it was written that morning:

**source - and a gap today.** Every connection entry on the fast path carries the TCP state the
vendor's own conntrack keeps: for each direction the end, the maximum end, the maximum window and the
maximum acknowledgement, the window scales, and a switch, `seq_chk_on`, that makes the fast path check
every segment against them; the vendor's host module fills them from its conntrack when it hands a
connection over, and the fast path counts what fails the check
(`FPCNTR_FROM_WIRE_DROP_TCP_SEQ_CHK_INVALID`). os-xgs-npu sends a connection with its first word only
and the rest at zero (`contrib/octep/octep_rpc.c`), so the check is off - and because pf would drop a
connection's frames against a window it no longer sees move, it marks the connection's state sloppy
(`contrib/octep/octep_pf.c`, `contrib/npuep/npupf.c`; [why](the-outbound-half-was-untranslated.md)).
So today **an offloaded TCP connection's window is checked neither by pf nor by the coprocessor**
([679](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/679)).
Filling that state from pf's own peers - `seqlo`, `seqhi`, `max_win`, `wscale` - would put pf's
window check into hardware. The global switch for it is already on (`OCTEP_FW_CFG_TCP_SEQ_CHK`).

### The window handed back

**~~source~~ measured, and used since the same day.** A take-back now gives pf the fast path's numbers
and clears the sloppy mark, and pf checks the connection again on the host - when the fast path still
held the connection up to the take-back; one it had given back already, its frames reaching the host
past those numbers, stays sloppy ([measured](../measurements/tcp-window-2026-10-22.md)). As it was
written that morning:

**source.** When a connection is taken back (`CONN_RECLAIM_FP`, which os-xgs-npu sends), the answer
carries the fast path's TCP state and both directions' counts; a separate command reads the state of
a connection still in hardware (`CONN_GET_FP_TCP_STATE`), and another tells the fast path about a
segment the host handled (`CONN_TRACK_FP`). With the section above, pf's state can be brought to
where the wire is when a connection comes back, and the sloppy marking is no longer needed.

### A connection dropped by its verdict

**~~source~~ measured, and done on both families on 2026-10-23.** A connection's verdict has three
values: forward (`OCTEP_CONN_VERDICT_CUT_THRU`, ~~the only one os-xgs-npu uses~~ 2), drop (1), and
send to inspection (0) - the numbers read from the vendor's host module - and
`CONN_MODIFY_FP_VERDICT` (command 13) changes it in place. ~~A block list or a JA4 fingerprint could
stop a connection on the coprocessor itself, its frames dying there, instead of taking it back to the
host.~~ The fast path asks it only of a microflow made with the action IPS, and only while its IPS
switch is on: with XGS: Settings **Cut in hardware**, on by default, both are, and a block list or a
JA4 fingerprint drops a connection on the coprocessor itself before its pf state is killed, keeping
it there while its sender goes on - a UDP flood at 400 Mbit/s, cut, sent the XGS 3300's host 10
frames in ten seconds against 610,533 without it, at no measured cost
([measured](../measurements/verdict-2026-10-23.md)).

### Inspection, then hardware

**~~source~~ measured on both families on 2026-10-23: the fast path hands the frames over, and
nothing on an OPNsense host receives them.** A connection set to inspection had every frame counted
as sent to the inspector with no error - 167,677 in six seconds of a UDP stream on the XGS 3300,
151,867 on the XGS 136 - and none reached the far end, came back, or arrived on the host's rings.
The inspector is a PCIe virtual function that the vendor's host makes (eight on the XGS 3300, four on
the XGS 136) and reads from user space; OPNsense's drivers make none, so the verdict is a drop by
another way until they do. What using it would take, and a nearer way to the same end, are in
[the measurement](../measurements/inspect-2026-10-23.md). **The same afternoon the XGS 3300's driver made
one**: the coprocessor took it, the frames arrived with the vendor's metadata, and Suricata in IPS mode
on the inspector's interface `oxips0` blocked a connection in hardware and let another through at 3.2
Gbit/s; a new connection inspected for its first megabyte then ran in hardware at 3.8 Gbit/s. Off unless
asked for, while ~~the XGS 136 and~~ ~~a setting~~ ~~follow~~ ~~follows~~
([measured](../measurements/inspect-2026-10-23.md#then-made-on-the-xgs-3300)). **The XGS 136's driver
made its function the same evening** - the first of the four its coprocessor sets up - and the same
inspection then hardware ran there: Suricata dropped a UDP stream by its rule from the moment it was in
hardware, and a new TCP stream ran through it, then in hardware, with the gigabit link full
([measured](../measurements/inspect-2026-10-23.md#then-made-on-the-xgs-136)). **And the setting**, later
that evening: XGS: Settings **Inspect, then hardware**, with OPNsense's own Intrusion Detection in IPS
mode reading `oxips0` through a template overlay - a rule's alert and its drop both taken, the TCP
stream at 941 Mbit/s on the XGS 136 and 3.98 Gbit/s on the XGS 3300
([measured](../measurements/inspect-2026-10-23.md#the-setting-and-the-intrusion-detection-service)).

**source.** The "send to inspection" verdict hands a connection to an inspector on a virtual function
(`ips_vf`), with a metadata block that lets the inspector rewrite the packet and ask for new
checksums, and a counter for frames forwarded once the verdict is in
(`FPCNTR_FROM_WIRE_TO_WIRE_VERDICT_CUT_THRU`). It is the shape an IDS such as Suricata needs: it sees
the start of a connection, and the rest runs in hardware.

### Live users

**~~source~~ measured on both families on 2026-10-23:** a connection whose user is not in the table
is given back to the host at once, and a user's logout took that user's connection out of hardware on
every run - up to 81,076 frames to the host on the XGS 3300 and 62,629 on the XGS 136 before the
driver made it again, with user 0, as both drivers make every connection today. A user number of 0 is
not checked. ~~Not used yet: the drivers must keep a connection's user, and the Captive Portal give each
session one~~ ([measured](../measurements/luid-2026-10-23.md)). Used since 2026-10-24: the drivers keep
each connection's user and count what it carries, and XGS: Settings **Captive Portal clients in
hardware** gives each portal session one ([measured](../measurements/cp-hardware-2026-10-24.md)). What the source says: `ADD_LIVE_UID` and `DELETE_LIVE_UID` keep a table of logged-in users, and a connection
carries its user's number: when the user is gone, the fast path stops forwarding it
(`FPCNTR_FROM_WIRE_TO_KN_LIVE_UID_INVALID`). OPNsense's Captive Portal sessions could live in hardware
and end the moment a user logs out.

### A priority bit and a meter for each connection

**source.** Each direction of a connection has a QoS word - a priority bit, a valid bit and a meter
number - set by `CONN_MODIFY_FP`, whose handler is present. It is the hook for priority in hardware
([os-trafficpriority](https://github.com/AbdelmonemAwad/os-trafficpriority)). **Measured 2026-10-24**: the
fast path takes the words and holds them, read back exactly, but two connections sharing a congested
port kept their shares with the bit on one; what it would decide is not reachable with this firmware
([measured](../measurements/port-qos-2026-10-24.md)).

### The QoS commands

**source.** Seven commands - add, remove and query a meter, set and read a shaper rate and a DWRR
weight - are declared with no handler in the coprocessor's RPC module, like the DoS ones; they are
~~expected to answer "not dispatched"~~ **measured 2026-10-24**: each answered `rc 0x8006`, not
dispatched ([measured](../measurements/port-qos-2026-10-24.md)).

### DSCP rewritten for each flow

~~**source.**~~ **measured 2026-10-24.** A flow can carry a DSCP value to write into every frame
(`dscp_override_en`, `dscp_override_val`). A mark pf or the shaper sets would survive the connection's
move into hardware. It did not before: a connection in hardware lost the mark a normalization rule's
**TOS / DSCP** gives it, its frames leaving as they came. Both drivers now give each microflow pf's
mark, and the frames keep it in hardware, ECN bits as they came - both ways on the XGS 3300, the
original direction read on the XGS 136; a mark that sets the ECN bits itself, and an IPv6 or
tunnelled connection with a mark, stay with the host
([measured](../measurements/flow-dscp-2026-10-24.md)) - the IPv6 one because neither appliance's
fast path writes an IPv6 traffic class, whatever the microflow says
([measured](../measurements/flow-dscp6-2026-10-24.md)). What the shaper names stays with the host
already, where ipfw decides, so a mark it set would be written there.

### Bridged flows

~~**source.**~~ **measured 2026-10-24.** A flow's bridge control says separately whether to rewrite
the source MAC, the destination MAC and the VLAN, and whether to lower the TTL; os-xgs-npu ~~writes~~
wrote "routed", all of them (`OCTEP_BRCTL_ROUTED`). Without the rewrites it is a bridged flow - the
XGS 136's thirteen-port LAN bridge carrying LAN-to-LAN traffic in hardware. Measured, the routed way
was wrong for a bridged connection, not only slower: on the XGS 3300 its next hop came from the
route - for a bridge with no address the default route, so its frames went out of the WAN port to
the gateway and nothing arrived; for one with an address every frame arrived from the bridge's MAC
with its TTL lowered - and on the XGS 136 it was not made at all
([723](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/723)). Both drivers now make it bridged:
each direction out of the port the other comes in on, nothing rewritten
([measured](../measurements/bridged-2026-10-24.md)).

### VLAN interfaces

~~**source.**~~ **measured 2026-10-24.** A logical interface is a port and a VLAN together - 4,096 to
a port - and a next hop carries a VLAN; os-xgs-npu ~~makes~~ made only each port's VLAN 0
(`contrib/octep/octep_dp.c`). ~~VLAN subinterfaces could be offloaded.~~ Without a VLAN's own logical
interface the fast path hands its tagged frames to the host unlooked-up, so every connection by a
VLAN subinterface crossed the host - measured on both appliances. Both drivers now give each VLAN
subinterface of a front port its logical interface, name the VLAN in the next hop, and have the
microflow write the tag - added, replaced or taken off: a connection routed by a VLAN is made in
hardware, the host's processors left nearly idle where they had carried it
([measured](../measurements/vlan-2026-10-24.md)). A bridge over a VLAN subinterface stays with the
host's bridge.

### A heartbeat with the fast path

**~~source~~ measured, and done the same day on the XGS 3300** - the host asks every five seconds and
the fast path answers every time, in about 130 microseconds; a fast path stopped for 45 seconds was
seen late, then taken as stopped at 30 seconds, then answering again, each said once in the system log.
**The XGS 136's fast path does not answer it**: it reads the message and drops it, measured with its own
counters, so ~~the heartbeat is off by default there~~ since 2026-10-23 its answer there is the beat
being taken off the host's queue by the fast path's own process ([measured](../measurements/heartbeat-2026-10-22.md#the-answer-on-this-family-the-beat-taken)). ~~It says so; it
does not act on it yet.~~ The same evening it was acted on: a problem report of its own, the Overview's
Fast path line, and - when the owner chooses so - the appliance restarted after 2, 5 or 10 minutes
without an answer ([measured](../measurements/heartbeat-2026-10-22.md#what-is-done-when-it-stops)). As it was written that morning:

**source.** The vendor's host module sends a heartbeat message to the fast path every five seconds
and expects it echoed; with none for 60 seconds it warns, at 120 it declares the fast path dead. os-xgs-npu
counts both message types and sends neither. A host that knew within seconds that the fast path had
stopped could say so and take its connections back - the questions of
[361](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361) and
[454](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/454).

### Mirrored packets

~~**source.**~~ **measured on all three SFOS versions on 2026-10-25.** A control message carries a copy of a packet from the fast path to the host - offloaded
traffic made visible for a capture or an IDS. With the bit of 17 set, the fast path sends a copy of
every frame it forwards itself, two to a frame: one as it arrived at a port and one as it left the
other, each naming that port. Both drivers now show each copy to that port's packet capture and to
nothing else, so tcpdump on either port of a connection in hardware sees it both ways, as if the
host had forwarded it - on the XGS 136 and the first XGS 3300 every frame, to 1 Gbit/s; on the second
XGS 3300, over SFOS 22's software, only part of them, which part changing from one boot to the next;
forwarding unchanged
([measured](../measurements/mirror-2026-10-25.md)). Whether an Intrusion Detection in IDS mode sees
them was not tried.

### Counters cleared

~~**source.**~~ **measured on all three SFOS versions on 2026-10-25.** Four commands clear the fast path's counter tables, so a measurement can start from zero.
Both drivers send them now (`counters_clear`), and `tools/octep-fpcnt.sh -z` and `npuep-fpcnt.sh -z`
start a reading from zero: the fast-path counters went from sums of 4,443, 12,349 and 43,920 to
what the next two seconds brought - a clear is taken on the workers' own time, within a second. Their
numbers are the version's - 46 to 48 on SFOS 21, which has no direct-forwarding clear, 47 to 50 on
22, 48 to 51 on 23 - and showed that SFOS 21 and 22 number every command from 46 on differently,
which the drivers now follow ([measured](../measurements/counters-clear-2026-10-25.md)).

### IPsec state kept in step

~~**source.**~~ **measured on all three SFOS versions on 2026-10-25.** `SA_SEQ_UPDATE`, `SA_REPLAY_UPDATE` and `SA_HOST_STAT_SYNC` move an association's sequence
number, its replay window and its counts between the host and the engine. The vendor's host uses them
for packets of an offloaded association it sends or decrypts itself; this project's design has none -
every packet of such an association is the engine's - so nothing is built on them. Measured:
`SA_SEQ_UPDATE` gives the engine's counter exactly on all three versions, and its atomic counter cost
nothing seen; `SA_REPLAY_UPDATE` took the association it was asked about out of the fast path on both
XGS 3300s; `SA_HOST_STAT_SYNC` answered rc 0. On the way it showed that SFOS 23's coprocessor had
refused every association - its `SA_ADD` is 200 bytes (#759, fixed in #760) - and that its
`SA_GET_STATS` reads 0, as the XGS 136's does (#761)
([measured](../measurements/ipsec-step-2026-10-25.md)).

### Ciphers nobody has tried

~~**source.**~~ **measured on all three SFOS versions on 2026-10-25.** The engine's cipher list names ChaCha20, AES with no encryption (GMAC), counter mode and
3DES besides what was measured; ~~SHA-384 was not run~~. SHA-384 now has: AES-CBC with HMAC-SHA-384,
each engine against a kernel, every packet through. ChaCha20-Poly1305, given to both drivers as an
experiment laid out as GCM is, was taken by every engine's `SA_ADD` and then dropped by every
datapath (`CRYPTO_DROP_SADB_PRE_ERR`), so it stays with the kernel. GMAC and counter mode are not
among the proposals OPNsense's page offers, and 3DES is not among what the vendor's host offloads;
none was tried ([measured](../measurements/cipher-2026-10-25.md)). GCM with an 8- or 12-byte ICV and transport mode
are settled ([gcm-12-and-transport-are-not-ours](gcm-12-and-transport-are-not-ours.md)).

### Public-key operations

**source.** The fast path counts signing, algorithm and session errors of a public-key path
(`FPCNTR_CRYPTO_PKI_*`) - the coprocessor's engine doing RSA and ECDSA work, which the vendor uses for
TLS inspection. **Read further on 2026-10-25:** the way in is the intrusion-prevention stage's, not
the host's - a frame from it whose metadata has `USFP_META_FP2IPS_PKI_VALID` set
(`FROM_IPS_TO_PKI_CRYPTO`, and the answer back `FROM_PKI_CRYPTO_TO_IPS`), and the metadata carries
only a pointer to hand back with the answer (`struct usfp_pki_md`, the vendor's `metadata_dpi.h`).
How the operation itself is laid out is not in the source, and no RPC command reaches it. Nothing in
OPNsense would hand it a signature to make either. Not built.

### A packet dump in the fast path

~~**source.**~~ **measured 2026-10-25.** A bit of the fast path's configuration word turns on a packet dump
(`OCTEP_FW_CFG_FP_PKT_DUMP`) - a flow in hardware made visible for diagnosis. The dump is 12's copies:
the bit, 0x010, is what has the fast path send them, on both families and all three versions, and
XGS: Settings **Copies for packet capture** writes it with the offload gate
([measured](../measurements/mirror-2026-10-25.md)).

### Frames injected from the host

**source.** The host can inject a frame into the fast path with its connection's context and have it
sent on, encrypted where its connection is a tunnel's. **Read further on 2026-10-25:** the "host" is
the intrusion-prevention stage - a frame from it with `USFP_META_FP2IPS_INJ_VALID` and the injection
metadata (the microflow it joins, its revision and direction; `struct inject_md`), which the fast
path checks against that microflow, its next hop and its connection (`FROM_INJECT_DROP_*`) before
sending it on; the vendor's own host module sends none, and only reads the stage's injection errors
(control message type 4). On all three boards every injection counter stood at 0. In this project the
inspector's frames go back to the fast path the way they came, "Inspect, then hardware" works without
it, and a packet of a tunnel's connection the host makes itself is already encrypted by the engine.
Not built.

## The switch

### The TCAM: 254 of 256 entries are the vendor's

**read.** The 88E6193X has a TCAM of 256 rules that match a frame's first 48 bytes and can replace
where it goes, its queue or its DSCP. On the XGS 3300 it is on for the ten front ports and **254 of
its 256 entries are already the vendor's**: broadcast and multicast, each port's frames and each of
the appliance's own addresses sent to port 0 - the coprocessor - and a last entry that drops
whatever else arrives. Entries 3 and 255 are free. So the TCAM is not free to use for a plugin's own
rules.

![The XGS 3300's switch read through the coprocessor: the TCAM on for ports 1 to 10, 254 of 256 entries in use, entries 0, 1 and 254 decoded, every port's queues weighted](../images/measurements/discoveries-2026-10-22/01-xgs3300-switch-tcam.png)

### Queue scheduling up to strict priority

**read.** Each port has eight queues, scheduled anywhere from weighted on all eight to strict
priority on all of them; all eleven ports are weighted today. ~~That is free to use, through
os-xgs-npu's switch tool.~~

**Measured 2026-10-25** ([qsched-2026-10-25](../measurements/qsched-2026-10-25.md)): the scheduling
is written and read back through the switch tool, but every frame the coprocessor sends to a front
port waits in that port's queue 0 - a connection in hardware or the host's own, marked EF or not,
with the connection's priority bit or without - so strict priority orders nothing: a marked stream
did not win under it. The three versions' firmwares leave the switch alike. Not used.

### Two front ports never switch to each other in the chip

**read.** Because the vendor's TCAM sends every front port's frames to the coprocessor, two devices
on two front ports never reach each other inside the switch - their frames always pass the
coprocessor. ~~Whether the host's rules then see them is to be measured.~~

**Measured 2026-10-25, on SFOS 22 and SFOS 21** ([frontports-2026-10-25](../measurements/frontports-2026-10-25.md)):
a frame addressed to the device on the other port went in at one switch port and out of the other
none of ten times, on the second XGS 3300 and on the XGS 136 alike. Routed between two switch ports,
traffic meets the host's rules: a block rule refused every new connection while the one already
running went on on its pf state, as on any pf firewall, and killing that state stopped it and took it
out of hardware. The TCAM is on for the front ports on all three versions.

## The coprocessor, read on the appliances

### Dragonfly: deep packet inspection and threat detection

**read: running on the XGS 3300, absent on the XGS 136.** The XGS 3300's coprocessor starts, at every
boot, a DPI and threat-detection engine inside its fast path: nDPI for the applications, settings for
DGA domains (the throwaway names malware makes up), risky top-level domains and autonomous systems, trusted
domains, block and allow lists, an "alerts only" mode, reputation rules, an export template, and "init
for external ML". Its flow table holds 500,000 flows. A logical interface turns it on
(`df_enabled`); os-xgs-npu never does. ~~Where its alerts leave the coprocessor is the next question.~~

**Measured 2026-10-25, on SFOS 22 and 23** ([dragonfly-2026-10-25](../measurements/dragonfly-2026-10-25.md)):
one `LIF_ADD_UPDATE` with a core mask of 0 and the DF bit turns it on, and it inspects - 1.6 million
packets of a port's traffic, flows exported, the traffic not slowed. Its exports are built as UDP
broadcasts and counted sent, and none reached the host's interfaces or rings, the coprocessor's own
interfaces, the other board or the PC: where they go was not found. And a port with it on loses the
traffic of a tunnel that arrives there - decrypted, through Dragonfly, then nothing - while plain
traffic passes. So it stays off, as the vendor's own SFOS leaves it; the XGS 136's SFOS 21 has none.

![The XGS 3300's coprocessor log: Dragonfly starting with nDPI and its rules, its settings directory, its flow settings, and the transmit rate limit set for each front port](../images/measurements/discoveries-2026-10-22/02-xgs3300-coprocessor-log.png)

### A transmit rate limit for each front port

**read: on for the XGS 3300's ten switch ports, off on the XGS 136.** The XGS 3300's fast path gives
each front port behind the switch its own transmit queue with a rate limit, set from the host by the
port-speed message (`OCTEP_CMSG_TYPE_PORT_SPEED`), ~~which os-xgs-npu sends as the link's own speed~~
which os-xgs-npu never sends. ~~A lower figure is an egress shaper in hardware - the WAN held to the
line's upload.~~ **Measured 2026-10-24**: at the coprocessor's boot every queue's limit is refused
("bigger than link speed= 0") and nothing sets it again; the message is taken and goes no further, and
a stream through a port stayed at its link's rate with the port at 100 ([measured](../measurements/port-qos-2026-10-24.md)). On the XGS 136 its
log says the feature is off ("softnic port not found").

![The XGS 136's coprocessor log: no Dragonfly and no /tmp/df_etc, the rate limit disabled, a random number generator and a watchdog](../images/measurements/discoveries-2026-10-22/03-xgs136-coprocessor-log.png)

### The coprocessor's own watchdog, memory reports, clock and randomness

**read.** Both coprocessors have a watchdog of their own (`sbsa-gwdt`), ~~memory error reporting (EDAC)~~
and a hardware random number generator; the XGS 3300's has a PTP clock too, **and memory error
reporting (EDAC) - the XGS 136's registers no EDAC controller (measured 2026-10-25)**. ~~Whether
anything feeds its watchdog is not known.~~

**Measured 2026-10-25, on all three versions** ([copro-health-2026-10-25](../measurements/copro-health-2026-10-25.md)):
busybox's `watchdog -t 5` feeds it every 5 s and sets it to 60 s. On the XGS 3300 its EDAC counts
memory and cache errors - none on either board. Left unfed on the first XGS 3300 (SFOS 23), the
coprocessor's watchdog ran out at 60 s and the host froze with it; the host's own watchdog, which
os-xgs-npu arms by default, restarted the appliance 220 s after the feeding stopped - three runs
alike. A host that freezes there is a fault of its own
([768](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/768)).

## The vendor's stack and board package

### The network agent's polling mode

**source.** The network agent takes a command to change how it polls (`SET_POLLING_MODE`) - documented
in [netagent.md](../npuep/netagent.md) and never sent. Its polling of the switch's bus is what spoils
os-xgs-npu's own reads there and made the XGS 136 report linked ports down
([639](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/639)).

### VLANs, bridges and address entries through the agent

**source.** The agent's commands for VLANs, bridges and static address entries inside the switch, and
the port attributes for the frame types a port accepts, learning and flooding, are declared and never
sent.

### A logger for the coprocessor's console

**source.** The board package has a script that writes everything the coprocessor's console prints to
a file, "to catch NPU crashes". os-xgs-npu uses the console only for commands, so a crash's last words
are lost ([454](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/454)).

### The XGS 3300's management link, built and not used

**~~measured once~~ measured, and in use since 2026-10-23.** The XGS 3300's PCIe management link is
built (`contrib/octep/octep_mgmt.c`) and answered a ping once ([octeon-tx.md](../families/octeon-tx.md)),
~~but the coprocessor is still reached through its console - slow, and shared with its boot
firmware's menus~~ and it is the way to the coprocessor now: `oxmgmt0`, made when the driver
attaches, started and addressed at every boot by the plugin's boot hook, and taken first by every
reader of the coprocessor - one command in 0.77 seconds instead of 11 over the console, the
Transceivers page's read in 1.80 instead of 36.19
([measured](../measurements/mgmt-link-2026-10-23.md)).

### The fail-to-wire relay's watchdog

**source.** The board's description names a watchdog and a trigger for the fail-to-wire relay - a
relay that closes by itself when the host stops. The relay is read and, by the owner's decision, not
driven.

## Beyond the scope

These are in the hardware and are not used by the vendor's own firmware on these appliances either,
so they are not counted in the completion figures - a row is added for one when the owner decides to
build it.

| | what | why it is beyond the scope |
|---|---|---|
| | VLANs, bridges and address entries inside the switch through the agent | the vendor's host module never sends them |
| | ciphers the engine names and nobody uses here | not what the vendor's firmware offers for these appliances |
| | the rate limit on the XGS 136 | off in the vendor's own configuration |
| | the PTP clock, the RNG, the eMMC's RPMB | no use in a firewall found |

The full working list, with every file and line read, is kept locally beside the sources it cites.
