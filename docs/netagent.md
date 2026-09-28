# The NetAgent mailbox, and per-port control

Bringing a front port up, setting its MTU, reading its link state - none of that goes through
[GIU](giu.md). It goes through a separate facility, `nwa`, served on the coprocessor by Sophos's
NetAgent daemon, and its message set is published nowhere: the header the vendor's own source
includes is absent from the GPL drop, and the target end is a binary. The host end is source, and
reading it is what settled the transaction below.

What follows was read off a running system, with ports being toggled one at a time and the shared
window polled in a tight loop. It is enough to describe the messages, and enough to send them.
From the host exactly four operations answer - `0x01` switch-init, `0x03` port-attribute-set,
`0x04` port-attribute-get and `0x45` all-port-info. Every other opcode returns an ACK with status
1, because the target registers no handler for it.

## Where it lives

`nwa` is at **BAR0 + 0x4000, 64 KB**, and it has no doorbells in either direction. The
coprocessor's own facility dump is unambiguous:

```
2) nwa : type 2 dbell [h2t=0 t2h=0] dma eng 0 flags=0x0
```

Both sides poll. Which means a packet capture sees nothing at all: `tcpdump` on the management
interface across twenty seconds of live port activity caught **zero packets**. The protocol is
not IP, and it is not the AGNIC command ring either.

The window is device memory, so it can be read from the host even on a kernel that refuses
`/dev/mem` for ordinary RAM.

## The header

```
+0x00  u32  cookie        0xCAFEBABE
+0x04  u32  0x34          command mailbox length
+0x08  u32  0x7fcc        event buffer length
+0x0c  u32  0x8000
+0x10  u32  0x8000
```

The names come from the host module's own log strings - `Initialized cmd_mbox_len(%zu),
evt_buf_len(%zu)`, and a version check behind `HOST-TARGET MailBox versions are different`. The
two `0x8000` values are most likely the boundary between the two halves of the 64 KB window;
`0x8000 + 0x7fcc` is `0xFFCC`, which lands just inside it.

These words are present and correct as soon as the coprocessor's side is up. A host that finds
them zero should wait rather than write them - the module's `waiting for facility config
availability` is doing exactly that.

**Confirmed on a second family, 2026-09-27.** Everything on this page was derived on ARMADA. The same
five words, with the same values, have now been read out of an **OCTEON TX** CN83XX's `nw_agent`
window on an XGS 3300 - `0xcafebabe, 0x34, 0x7fcc, 0x8000, 0x8000` at `BAR2+0x02200000`. That is the
first evidence from silicon that NetAgent is family-independent rather than merely *looking* it from
where its source sits. See [families/octeon-tx.md](families/octeon-tx.md).

One difference worth knowing before reusing any of this: on OCTEON the window is empty until the
coprocessor's **user-space** fast path runs, because that is what publishes the facility - not its
kernel. Reading zeroes there says nothing about the protocol. The window stays blank until the SDP
endpoint handshake completes and the coprocessor's fast path publishes the facility, so zeroes
mean the handshake has not been done - or that `usfp` is not up - and nothing more. See "The
handshake gates this window, not only the datapath".

## The message

Only thirty-nine bytes in the whole 64 KB change during port activity, all of them in the first
0x60:

```
+0x18  u8   turn      0x01 when a request is present, 0x02 once it has been answered
+0x1c u32 len request length in bytes, unrounded - 0x20 on a port operation, 0x88 on the periodic
status
+0x34 u32 op 0x01 switch-init, 0x03 set, 0x04 query, 0x45 all-port-info
+0x38  u32  sub       see below
+0x3c  u32  port_id   and on the answer this field carries the port INDEX instead
+0x44  u32  payload0
+0x48  u32  payload1
+0x5c  u32  speed on a link answer, 0x3e8 = 1000 Mb/s
```

| op | sub | meaning | how it was confirmed |
|---|---|---|---|
| 0x03 | 0x00 | set administrative state | payload0 = 2 on down, 0 or 1 on up |
| 0x03 | 0x02 | set MTU | payload0 = `0x2328` = 9000, and `0x5dc` = 1500 on a port whose MTU is 1500 |
| 0x03 | 0x03 | set MAC address | payload0/1 hold the six bytes, and they matched the toggled port's own address every time |
| 0x03 | 0x4a | multicast or filtering | payload0 contained `0x3333` |
| 0x04 | 0x04 | query link | the answer carries the speed |
| 0x45 | 0x10 | periodic status | class `0x88`, roughly every 1.1 s |

The MAC is the strongest evidence in the whole exercise: every port toggled produced an
`op=03 sub=03` carrying that port's own hardware address and no other's.

#### The operation space, named

Those op numbers were read off the wire. `enum nwa_msg_type`, in the NetAgent host module's header,
names all of them, and every value measured here falls where it puts them:

| op | name | |
|---|---|---|
| `0x01` | `SWITCH_INIT` | answers; it is what publishes the port table |
| `0x02` | `AGEING_TIMEOUT_SET` | |
| `0x03` | `PORT_ATTR_SET` | answers |
| `0x04` | `PORT_ATTR_GET` | answers |
| `0x05` | `PORT_INFO_GET` | |
| `0x06` .. `0x09` | `VLAN_CREATE`, `VLAN_DELETE`, `VLAN_PORT_SET`, `VLAN_PVID_SET` | |
| `0x0a` .. `0x0e` | `FDB_ADD`, `FDB_DELETE`, and three `FDB_FLUSH_*` | |
| `0x0f` | `LOG_LEVEL_SET` | |
| `0x10` .. `0x13` | `BRIDGE_CREATE`, `BRIDGE_DELETE`, `BRIDGE_PORT_ADD`, `BRIDGE_PORT_DELETE` | |
| `0x14` | `ACK` | **this is the reply marker** |
| `0x40` | `ALL_LINK_STATUS` | |
| `0x41` | `MDIO_OPERATION` | refused, status 1 |
| `0x42`, `0x43` | `GPIO_OPERATION`, `GPIO_BLOCK_OPERATION` | refused, status 1 |
| `0x44` | `SET_POLLING_MODE` | |
| `0x45` | `ALL_COMB_PORT_INFO` | answers |

**`0x14` is not a magic number.** It is `NWA_MSG_TYPE_ACK`, an ordinary member of the same
enumeration, which is why every reply begins with it: a reply is an ACK message.

**Most of that space has no sender in the host module.** Only `SWITCH_INIT`, `PORT_ATTR_SET` and
`PORT_ATTR_GET` are issued from `mv_nwa_host.c`. The bridge, FDB and VLAN operations - a whole
switchdev-shaped control surface - are declared and never sent from there, exactly like the two
flow-configuration attributes. Something else in the vendor's stack drives them, and the `rpc`
facility, the one with five doorbells and four DMA devices, is where to look for it.

Note that `0x45` appears in both columns and means two unrelated things: as an **op** it is
all-port-info - the coprocessor sends it unprompted as a periodic status message, and the host can
issue it too, as one of the four operations that answer - and as a **sub** it is the promiscuous
attribute described below. The two spaces are separate and the collision is a coincidence.

### The attributes this driver drives

These are the ones this driver sends deliberately, rather than watching them go by. Each of the
ones actually driven was confirmed by a `status = 0` reply and by the behaviour changing on the
wire; `0x01` is read-only, and `0x46` is listed for completeness and has not been sent.

| sub | name | what it does |
|---|---|---|
| `0x00` | state | administrative up and down |
| `0x01` | oper state | read-only; what the port is actually doing |
| `0x02` | MTU | bytes |
| `0x03` | MAC | sets the port's own address **and arms the switch to accept unicast for it** |
| `0x04` | speed | megabits; only meaningful while carrier is up |
| `0x45` | promiscuous | opens the switch's catch-all for this port |
| `0x46` | all-multicast | not driven; named for completeness |

Two of them reach the switch rather than the port, and that is the part worth knowing.

**`0x03` is not only cosmetic.** The switch drops every unicast frame whose destination it has not
been told belongs to that port, so until the address is sent the port receives broadcast and
nothing else - which looks like a working port until something tries to talk to it directly.

**`0x45` is what a bridge needs.** A bridge's function is to receive frames addressed to other
machines and forward them, so a bridged front port that accepts only its own address forwards
broadcast and nothing else. Measured on a loopback between two front ports, with frames addressed
to an address the receiving port does not own: 0 of 20 arrived before, 20 of 20 after.

It sleeps - the mailbox is a round trip - so it is sent from the ioctl path with no driver lock
held, never from the datapath.

**Where these numbers come from, now.** They were read off a running system first, one attribute at
a time. They are no longer only that: Sophos's GPL drop carries the NetAgent host module as source,
and `module-host/mv_nwa_host.h` declares the enum they are drawn from. It starts at 0 and jumps to
64 at `SUPP_LINK_MODES`, which places every value this project had already measured exactly where
the measurement had put it - `0x0d` duplex, `0x0e` statistics, `0x50` PHY id, `0x55` link settings,
`0x45` promiscuous, `0x46` all-multicast. That agreement is the check that this is the right enum,
and it also names the ones nobody here has sent: `0x4c` unicast-filter add, `0x4b` multicast flush,
`0x51` rate limit, and `0x56`/`0x57`, the two flow-configuration messages, which no caller in the
host module sends at all.

## The port identifiers

**`port_id = 0x8000 + N * 0x100`** for the switch ports, confirmed one port at a time:

| toggled | port_id | answer field |
|---|---|---|
| port 1 | `0x8100` | `0x0001` |
| port 3 | `0x8300` | `0x0003` |
| port 7 | `0x8700` | `0x0007` |
| first fibre port | `0x8900` | `0x0009` |
| second fibre port | `0x8A00` | `0x000a` |

The numbering is sequential by **physical position** - not the printed label and not the internal
port index. The eight copper ports are 1 to 8, then the two fibre ports are 9 and 10.

Only ports with a cable in them are polled with `op=04 sub=04`. That is a useful signal in itself:
the coprocessor does not ask about a port with no carrier.

**Four of the fourteen are not switch ports at all.** The coprocessor's own interface list shows
four `mvpp2` interfaces carrying the addresses of four of the front ports, so those are SoC ports
reached directly rather than through the switch. That is what the table in the window has been
saying from the start - five SoC entries numbered `0` to `4`, then the `0x8x00` block - and what
NetAgent's own log says: `soc_ports=5, switches=1`. Their identifiers are most likely the small
SoC indices, not a continuation of the `0x8x00` sequence, and that has not been confirmed by
toggling one.

## What the switch is

NetAgent names it when it refuses a request: `FEC not supported on Soho ports`. It is a Marvell
Soho-family switch, driven on the coprocessor by `UMSD_NPU` over MDIO - the register trace in its
log shows the two global register banks. `UMSD_NPU` is also what drives the front-panel link
LEDs, which is why they stay dark until a port is brought up through this mailbox: the LEDs are
not on the host's side of the link at all.

## The transaction

The field map above came from watching the window change. It was correct, and messages built from
it were still ignored, because two things were missing - both of them plain in the host module's
own code.

```
    wait for  +0x20 == 0                the previous transaction has finished, 1 s
    write     +0x1c  = request length   in bytes, unrounded
    write     +0x34  = the request body
    write     +0x18  = 1                the signal, and the LAST write
    wait for  +0x20 == 1                the reply is in the window, 30 s
    read      +0x24  = reply length
    read      the reply body at 0x34 + request_length
    write     +0x18  = 2                acknowledge
```

**`+0x20` is the target's status register**, and nothing else is. Polling `+0x18` waits forever
on a field the host itself owns and the target never writes. And a request with no length at
`+0x1c` is a request the far side has no reason to look at.

There is no sequence number, no per-message magic, no checksum and no doorbell. Four stores into
the window are the entire host side of it: the length, the body, the signal, and the
acknowledgement that releases the next transaction.

Two constraints on the reply worth checking rather than assuming:

- the reply body starts at `0x34 + request_length` using the **unrounded** length, so a 32-byte
  request is answered at `+0x54`;
- its first word must be `0x14` - `NWA_MSG_TYPE_ACK`, a member of the same message-type
  enumeration as the request opcodes - and a first word that is anything else is a malformed reply and
should be refused rather than parsed. The second word is the target's status: zero is success, and
a non-zero status is a well-formed refusal rather than a malformed reply - status 1 is what an
opcode or attribute with no registered handler returns.

And one on the header: `+0x04` is both the version handshake and the body offset. The host waits
for it to read exactly `0x34` before it will use the window at all, and then uses that same value
as the offset it writes the body to. A different value means a mailbox this code does not
understand, which is what the module's `HOST-TARGET MailBox versions are different` is about.

`+0x08` is the maximum request length - `0x7fcc` here - and a request longer than that is refused
before the window is touched. `+0x0c` and `+0x10` describe an event buffer that the host module
reads once, logs, and then never uses again.

## Confirmed on the hardware

Ten ports, ten replies, `status = 0` on every one:

```
op=3 sub=0 port=0x8100 payload=1
ANSWERED  reply_len=8  hdr=0x00000014  status=0x00000000
```

The front-panel link LEDs came on.

## On OCTEON TX: reading the link

Everything above was learned on ARMADA, where the ports are switch ports. The XGS 3300 has two
coprocessor MAC ports that are not behind the switch, and they behave differently enough to be
worth their own section. Measured 2026-09-28 on OPNsense 26.7.

### The handshake gates this window, not only the datapath

After a cold boot the SDP scratch register reads zero and the NetAgent window is **entirely
blank** - no `0xcafebabe`, no offsets, every request `ENXIO`. Completing the endpoint handshake
makes the fast path publish the window within seconds. That is worth knowing before concluding a
NetAgent problem: check the handshake first.

### The port table this appliance publishes

The reply opens with a count and then carries one five-word record per port. The first two words
of a record pack two fields each:

    word 0   flags << 16 | tag
    word 1   max_unicast << 16 | mtu

which reads out, in full, as:

| tag | what | flags | mtu | `max_unicast` |
|---|---|---|---|---|
| `0x0001` | coprocessor MAC, panel port F1 | `MNG` | 9182 | 65535 |
| `0x0002` | coprocessor MAC, panel port F2 | `MNG` | 9182 | 65535 |
| `0x8000` | the switch's uplink to the coprocessor | - | 9182 | 0 |
| `0x8100` .. `0x8a00` | switch ports 1 to 10: Port1-Port8, PortF3, PortF4 | `MNG` | 9182 | 12 |

Thirteen records are populated and the fourteenth is all zeros, which is where the count of 14 and
the thirteen populated records both come from.

The filter-table size separates the two kinds at a glance. The switch-port numbering agrees with
the 88E6193X's own per-port registers, read independently.

**`MNG` is `NWA_MSG_PORT_FLAG` bit 0, and it decides which operations a port accepts.** Marvell's
host module hands a port one of two operation tables depending on it: a flagged port gets
`nwa_pport_ext_port_ops`, which carries `state_set`, `promisc_set`, `mac_set` and the unicast and
multicast filter calls, and an unflagged one gets `nwa_pport_int_port_ops`, which is getters only.
On this appliance every panel port is flagged and only the switch's own uplink is not - so the
uplink is the one port the host is not expected to configure, which is also the one with no filter
table.

Note `0x0003` answers requests but is **not** in the published table. An unlisted tag being
answered is not evidence that it exists as a port.

### Attribute 0 reports the link here, because the target has the two the wrong way round

On these coprocessor MAC tags:

| attribute | what it actually returns |
|---|---|
| `0x00` STATE | **the link**, 1 up 0 down - see the experiment below |
| `0x01` OPER_STATE | `status 1`, refused |
| `0x04` SPEED | the **nominal** speed. Returns 10000 for every MAC tag including one never brought up |
| `0x0d` DUPLEX | 1 on a coprocessor MAC, `0xff` on a switch port. Static - it does not follow the link |
| `0x55` KSETTINGS | three words, `0 / 10000 / 1`. Also static |
| `0x0e` STATS | 264 bytes, 64 per-port counters - but a **dead instrument**: the same value comes back for every tag, before and after traffic, including the switch uplink. Do not measure with it |
| `0x09` LINK_MODE, `0x50` PHY_ID | `status 1`, refused |
| `0x0b` FEC | **do not send it.** It stops the NetAgent handler for good - see issue #78 |

`NWA_MSG_TYPE_ALL_LINK_STATUS` (64) exists in the enum and **nothing issues it** - there is no
struct for it in the host header and no caller in the host source. Neither is the event buffer
read. Link state on this path is a GET of attribute 0.

### The experiment that settles it

The two SFP+ cages were connected to each other with a fibre, so each is the other's link partner.
That makes it possible to separate a port's administrative state from its physical link, by taking
away the partner while leaving the port enabled:

```
A. both ports administratively down     tag 0x0001 = 0   tag 0x0002 = 0
B. only F1 brought up                   tag 0x0001 = 0   tag 0x0002 = 0
C. F2 brought up as well                tag 0x0001 = 1   tag 0x0002 = 1
```

**Step B is the proof.** F1 is administratively up and still reads 0, because nothing is
answering on the far end of its fibre. Only when F2 is enabled too does either port read 1.

An administrative-state register would have read 1 in step B. A link register reads 0. So
attribute 0 is the link.

The same experiment proves the other half: **a 10G link trains on this hardware under a
non-vendor operating system.** A reading that rises only when both ends are enabled, and falls
when either is disabled, is a real trained link and not a stored flag.

### Two traps this path sets

**A bring-up is not instantaneous.** After enabling a port the link still reads 0 for a second or
two while the SerDes trains. Reading back at once reports failure when it means unfinished.

**Do not reach for a plausible-looking register.** Speed, duplex and ksettings all carry sensible
10G values on a port whose link is down, because they describe what the port can do rather than
what it is doing. This is the same mistake as reading a PHY status register as a link indicator,
which cost this project a published claim.
