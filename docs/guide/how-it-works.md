<!-- reviewed: 2026-10-17 -->
# How it works, briefly

*Moved here from the [README](../../README.md) on 2026-10-18, word for word, when the README was
made shorter. Struck claims stay struck, with what replaced them beside them; links were only
re-pointed to work from this folder.*

## 🧭 How it works, briefly

The coprocessor publishes a map of five *facilities* in a PCIe BAR, each one a different
conversation:

| Facility | What it is | Where |
|---|---|---|
| `ctrl` | the handshake and the doorbells | [docs/npuep/facility-protocol.md](../npuep/facility-protocol.md) |
| `mvmgmt` | a management NIC between host and coprocessor | [docs/npuep/mvmgmt.md](../npuep/mvmgmt.md) |
| `giu` | the datapath: a full NIC with queues, buffer pools and offloads | [docs/npuep/giu.md](../npuep/giu.md) |
| `nwa` | a mailbox for per-port state — link, speed, media, admin up | [docs/npuep/netagent.md](../npuep/netagent.md) |
| `rpc` | the control channel that fills in the forwarding tables | [docs/npuep/rpc.md](../npuep/rpc.md) |

The datapath carries every front port on **one** pair of DMA queues. What separates them is a
two-byte tag in front of each frame: the driver writes it on transmit to choose the egress port,
and reads it on receive to decide which interface a frame belongs to. Ports come in two families
and they do not follow one rule — ten behind an internal switch carry `0x8000 + n*0x100`, four
that are separate MACs on the SoC carry `0x0001`..`0x0004`.

**OCTEON TX differs on the receive side and it matters.** There the host owns eight output rings,
the fast path chooses between them by hashing the frame, and the host does not get to pick - so all
eight have to be read, not just the one transmit uses. The tag is in the same place, but two more
qwords sit in front of it: the prefix a host buffer receives is 82 bytes, not 66.

Receive needs the coprocessor's own forwarding tables filled in, which is what the control channel
is for: a logical interface per port, and a binding from the port tag to it. Two commands each,
and nothing else.
