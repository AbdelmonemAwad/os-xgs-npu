# Accelerating a flow breaks it, and it always has

Measured on an XGS 3300, 2026-10-06, on live downloads transiting the appliance. Issue #268.

## What happens

With the automatic trigger on, throughput falls in step with the number of flows programmed and
reaches zero. Four runs, each with its own baseline, each aborted automatically when the rate fell
below a quarter of its baseline for two consecutive samples:

| baseline | collapsed at | first visible fall |
|---|---|---|
| 80 Mbit/s | 19 flows | 6 flows |
| 93 Mbit/s | 12 flows | 7 flows |
| 297 Mbit/s | 9 flows | 9 flows |
| 78 Mbit/s, sequence check off | 11 flows | 8 flows |

Twice the rate came back to its baseline within seconds of the flows being discarded, on the same
transfer - so this is cause and effect and not a download ending.

## It is not new, and it is not the trigger

The appliance was reverted to the module as it stood nineteen hours earlier, before any of this day's
work - no bridged next hop, no revision bump, no candidate table, no direction fix. Same download,
same test:

| | rate | flows |
|---|---|---|
| baseline | **218 Mbit/s** | 0 |
| t+3 | **92 Mbit/s** | **1** |
| t+6 | **0** | 3 |

**One flow halved the throughput. Three stopped it.** Whatever this is, it predates every change made
that day, and the day's work did not cause it - it made it visible, by being the first thing that
ever programmed more than a handful of flows onto traffic anybody was watching.

That is the whole explanation for why this was not found earlier. Every previous verification
programmed **one** flow, by hand, and checked that the frame arrived or that the connection was still
established. None of them watched throughput. A flow that carries two frames and then kills its
connection passes every one of those checks.

## What the far side says, which is the strange part

Across a window containing a full collapse, with every one of the 182 counters printed unfiltered:

    RX_WIRE                      +194260
    TX_WIRE                      +194262
    FROM_WIRE_TO_WIRE                +85
    FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE +194175

**Not one drop counter moved.** The coprocessor received 194,260 frames and transmitted 194,262. It
is not refusing anything, it is not losing anything, and it reports no error.

And `FROM_WIRE_TO_WIRE` rose by 85 while about forty flows were programmed - **roughly two frames per
flow.** So a flow forwards a frame or two in hardware and then its connection's traffic ceases. The
failure is at the first frame, not at the fortieth.

## Four explanations tested and eliminated

| | how it was eliminated |
|---|---|
| the egress port for a bridged destination is wrong | every flow's recorded ingress port was checked against the bridge's own address cache and the host's ARP table. All correct |
| the direction field | it really was wrong - a constant - and was fixed. The collapse is unchanged |
| the TCP sequence check | cleared from `fw_cfg` and measured again. The collapse is unchanged |
| frames are being dropped | no drop counter moves, and received equals transmitted |

**The direction field was a real defect** and is worth recording even though it is not the cause. The
vendor's `conn_dir` is an array index, not a label: per-direction TCP window state is `seen[dir]`, the
QoS block is `qos[dir]`, and the window scale is chosen by it. This driver sent the constant 1 - the
reply - for every flow it ever programmed, including the ones going the other way. The right value
was already in hand and being discarded - though not where this page first said. The rule published
here, that an even key arrangement is the connection's own direction, was wrong: a frame received
from the wire and found through `pf`'s wire list matches with its source first whichever end opened
the connection, so that parity is 0 for every frame and the field was still a constant. The direction
is the state's own: `pf` records which way the connection was opened, and the frame is the original
direction when its source is the opener. The correction, and the actual cause of the collapse, are in
[the-outbound-half-was-untranslated.md](the-outbound-half-was-untranslated.md).

## Where it stops

An attempt to read a forwarded frame, by the method that settled an earlier content bug - point the
next hop at the host's own port and capture what arrives - produced nothing. Fourteen flows were
repointed and no frame came back in fifteen seconds, and the capture path was quiet throughout: not
one untagged frame in six seconds, not even the control channel that uses it.

That is consistent with the two-frames-then-silence finding rather than contrary to it: by the time a
flow's next hop is repointed, its connection has already stopped sending. **Catching the frame
requires arming the capture before the flow is programmed, on a connection known to be carrying
traffic, and reading within the same second.**

## Resolved, and one note on the meter

Fixed, and verified on hardware the same day: see
[the-outbound-half-was-untranslated.md](the-outbound-half-was-untranslated.md). The trigger now
fills the flow table and holds it full at the download's own rate.

That page also shows that the meter used for the tables above - a front port's byte counter on the
host - reads zero when the offload is working, because an accelerated frame never reaches the host.
**The tables above survive that, and it is worth saying why rather than leaving it to be guessed:**
across the window they were measured in, `FROM_WIRE_TO_WIRE` rose by 85 out of 194,260 frames. The
host was carrying all but a rounding error of the traffic, so its counter was measuring the traffic.
The meter only goes blind once the offload actually forwards, which on that module it never did.

## Lesson

A correctness result has a scale attached to it, and this project kept taking results at a scale of
one. One flow, programmed by hand, verified by a counter and a connection state - repeated for weeks,
and never once with a throughput meter in the frame. The defect was always there, in plain sight of
any measurement that watched the thing the offload exists to improve.

**Measure the quantity the work is for, not the mechanism the work is made of.**
