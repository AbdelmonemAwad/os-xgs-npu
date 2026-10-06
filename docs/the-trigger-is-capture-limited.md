# The encrypted frames are not lost, and the trigger cannot keep up

Measured on an XGS 3300, 2026-10-06, on a live download at 186 Mbit/s through the appliance - which is
the one ingredient every earlier attempt at this lacked.

## The transmit side shows no deficit

Three flows were accelerated, each given association 6, and one five-second window measured with the
download at full rate:

| counter | moved |
|---|---|
| `RX_WIRE` | **+57,218** |
| `TX_WIRE` | **+57,225** |
| `FROM_WIRE_TO_IPSEC_ENCR` | **+5** |
| `FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE` | +57,212 |

A frame consumed by the crypto engine is received and never transmitted, so if the five classified
frames had been swallowed, `TX_WIRE` would sit about five *below* `RX_WIRE`. It sits seven *above*.

**So nothing went missing on the transmit side.** With
[`crypto_issued` and `crypto_processed` equal](the-crypto-path-completes.md), the whole chain is
accounted for: classified, built, submitted, harvested, and transmitted.

**And five frames against a seven-frame drift is suggestive, not conclusive.** The identity needs the
encrypted count to be large next to the noise, and it was not - for the reason the rest of this page is
about. What is fair to say is that the deficit this test was looking for is **absent**, not that its
absence is proven to five decimal places.

## Why only five, and this is the finding that matters

In the same five seconds, **57,212 frames punted to the host** and three flows were accelerated. The
offload was covering about one frame in eleven thousand.

That is not the flow table running out - it holds 62 and held 3. It is the **candidate capture**. The
receive path keeps exactly one punted frame for the host to act on: one tuple, one slot, one sequence
number, overwritten by every frame that follows. The automatic trigger runs on the link poll, once a
second, and acts on whatever is in that one slot - so **it can learn at most one flow per second**, and
only if the frame that happened to be last was parseable IPv4 whose sequence still matched.

At 11,000 punted frames a second that is a rounding error, and it will never catch up: a connection is
finished long before the trigger reaches it, and the slot it would have been learned from was
overwritten eleven thousand times.

| | |
|---|---|
| flows the table can hold | 62 |
| flows accelerated in eight seconds | 3 |
| frames punted in five seconds | 57,212 |
| frames forwarded in hardware in three seconds | 3 |

**So the acceleration measured at 305 Mbit/s in the ring work was never the acceleration doing the
work** - the host was. Every throughput number this project has is a host-forwarding number.

## What to build instead

A ring of candidates in the receive path rather than a single slot, drained by the poll, with the
tuple, slot and sequence captured together as they are now. The size is the only question: enough that
a busy link's flows survive one poll interval, and bounded so the poll's own work stays bounded. The
receive path must still take no lock it does not already hold and must still never take a route
lookup - which is the reason the single slot was chosen in the first place, and the reason a ring and
not a list.

Nothing here argues for doing the work in the receive path. The division is right; the queue between
the two halves is one entry deep.

## Lesson

The flow table was sized, the sweep was written, the panic was fixed and the bridged next hop was
solved - and the thing that decides how much traffic is ever accelerated was a single struct field
nobody had measured the consequence of. **Every piece of that path was tested in isolation and the
rate at which it can be fed was not a piece**, so it was never on the list. A pipeline's throughput is
a property of its narrowest stage, and the narrowest stage here was the one that looked like
bookkeeping.
