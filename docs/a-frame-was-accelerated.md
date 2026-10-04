# A frame was accelerated

Measured on an XGS 3300 running OPNsense 26.7.5, 2026-10-05, minutes after
[the-state-is-the-gate.md](the-state-is-the-gate.md).

    FPCNTR_FROM_WIRE_TO_WIRE   +55   (0 -> 55)

That counter had read zero for the whole life of this project. Fifty-five frames arrived from the
wire and left by the wire, forwarded by the coprocessor, without ever reaching the host.

## The whole chain, and nothing is optional

A microflow is used only when **all five** of these hold. Each one was a separate week.

1. **The offload gate is on** - `FW_CFG_OFFLOAD`, bit 0 of `fw_cfg`, RPC command 2. It is not
   remembered across a reboot. See [offload-is-a-setting.md](offload-is-a-setting.md).
2. **`fw_valid` is set**, which means the fast path has made the entry. The host cannot conjure
   one; it programs a slot a punted frame has already named.
3. **The revision in the request matches the entry's.** `mflow_fpop_prog_both` skips a mismatch
   silently and returns success.
4. **`state` is 2**, `MF_ACTIVE`. At any other value the write lands, reads back correctly, and
   the fast path punts every frame regardless.
5. **The next hop is resolved.** An unresolved next hop is where the traffic goes the moment
   condition 4 is satisfied, and it is the counter that pointed the way: `NHOP_UNRESOLVED`.

## The measurement

A live ICMP flow, its slot read from the punted frame's metadata, with the offload gate open:

    slot: 1446
        lif 40960  <the port's MAC> <- <the sender's MAC>  ethertype 0x0800
        timeout 244141  lbinfo 0x83  rev 0  fw_valid 1  host_valid 0
        action 0  dir 0  brctl 0  state 0  sa 0  conn 0  nhop 0 rev 0

A next hop programmed at index 9 - resolved, MTU 1500, the sender's MAC as the destination, the
port's own MAC as the source, egress interface 10, which is the interface the frame arrived on - so
an accelerated frame goes straight back to its sender and nothing else on the appliance is
disturbed by the test. `NHOP_PROGRAM` returned `rc 0x0000` and `LO_NHOP_READ` read the entry back.

Then `FLOW_CREATE_FP` for that slot with `state 2`, `action 1`, `nhop 9`:

    FPCNTR_RX_WIRE                          +69
    FPCNTR_TX_WIRE                         +124
    FPCNTR_FROM_WIRE_TO_WIRE                +55   (0 -> 55)
    FPCNTR_TX_KN                            +14
    FPCNTR_FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE +14

69 frames in; **14** punted to the host, which are the ones that arrived before the write took
effect and are counted by `MFLOW_NOT_ACTIVE` exactly; **55** forwarded by the coprocessor. The two
numbers add up to the frames that arrived, which is the arithmetic that makes this a measurement
rather than a coincidence.

## What this settles about the action enum

`MF_ACT_DROP` is 0 and `MF_ACT_FWD` is **1**. Action 0 with `state 2` raised
`FROM_WIRE_DROP_MFLOW_ACTION` on the first frame; action 1 with a resolved next hop forwards. The
declaration order in the vendor's own printer holds for the first two, which is evidence for
`MF_ACT_IPS` 2 and `MF_ACT_AUX` 3 and not proof of them - neither has been exercised.

## What this does not settle

**This is not an accelerated firewall.** Every one of those five conditions was satisfied by hand,
for one flow, from a sysctl. Nothing in OPNsense creates a flow, and nothing removes one: the host
side has to come from `pf`'s own state lifecycle - a state created means a flow created, a state
torn down means a flow invalidated - and that is the open half of `#211`.

The flow used here is also addressed to the appliance itself, so forwarding it is semantically
wrong; it was chosen precisely because a mistake could only break a ping that this session had
started. The next measurement is traffic that transits the box, where the next hop has to be a real
gateway and the LAN is in the path.

**Only one direction was programmed.** `mflow_valid` was 1, the originating side alone. A
connection has two microflows and `FLOW_CREATE_FP` carries both; the reverse direction's slot has to
be found by its own key, and the hash that gives its position is recorded but has not been used to
place a flow yet.

## Lesson

Five conditions, each of which produces the same symptom when it is the one that is wrong: every
frame goes to the host. A counter that says which condition failed is worth more than any amount of
reading, and the fast path had one for each - `FORCED`, `MFLOW_NOT_ACTIVE`, `NHOP_UNRESOLVED`,
`CONN_INVALID`, `FW_REV_MISMATCH`. The breakthrough was not finding the fifth condition; it was
noticing that the counter had changed from the fourth one to the fifth, which is the fast path
telling you where it got to. **Read the counter that moved, not the counter you expected.**
