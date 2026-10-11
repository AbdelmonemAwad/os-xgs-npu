# Both ends exist, and the counter means something smaller than it was read as

Read out of the device's own fast-path binary, 2026-10-06, and every claim here was re-derived with
`tools/disasm-func.py` before it was written down - which caught one wrong attribution on the way, and
the wrong one was mine.

## The enqueue and the dequeue are both in one function

`pmode_hwevt_worker_loop` [`.text +0x432a78`], 11,788 bytes, 2,947 of 2,947 instructions read. It has
**exactly two** references to the page the crypto globals live on, `0x68b000`, and they are the two
ends of the path:

| | where | shape |
|---|---|---|
| enqueue | `blr` at `0x4339dc` | function from `dev+8`, `data` from `dev+0x10`, `queue_pairs` from `data+0x50`, `mov w2,#1` - **one operation** |
| dequeue | `blr` at `0x433078` | function from `dev+0`, a burst of **32** into a 0x110-byte frame slot |

Both compute the device as `base + id*128` from a pointer at `0x68b1d0`. Neither is a symbol: these
are `rte_cryptodev_enqueue_burst` and `rte_cryptodev_dequeue_burst`, which are `static inline` in
DPDK's own header - which is why
[the earlier symbol search could not see them](the-engine-accepts-and-nothing-returns.md).

**Which slot is which is proven from this binary, not from a header.** `otx_cpt_dev_create` stores the
pair with one instruction, `stp x1, x0, [x19]` at `0x4d6e20`, and two different predecessors branch
into it:

    0x4d6e50   x1 = 0x4c9248  otx_cpt_dequeue_sym     x0 = 0x4cd288  otx_cpt_enqueue_sym
    0x4d6e0c   x1 = 0x4c98f0  otx_cpt_dequeue_asym    x0 = 0x4cbf88  otx_cpt_enqueue_asym

So `[dev+0]` is dequeue and `[dev+8]` is enqueue, twice over.

**And that nearly went in backwards.** Reading the lines immediately above the `stp` gives the
asymmetric pair, because the symmetric path jumps into the same tail from ten instructions further
down. A store with two predecessors cannot be attributed from the lines above it, and the first
reading of it here did exactly that and had to be thrown away. The two pairs agreeing is what makes
the conclusion safe.

## The caller decides three ways

`crypto_pkt_submit` has exactly one caller: `bl #0x4477d8` at `0x433994`, in that same worker loop. Its
return value is tested three ways immediately afterwards:

| return | what happens |
|---|---|
| `0` | falls through to the enqueue ten instructions later |
| `> 0` | the frame leaves by the event device's Ethernet-transmit slot, **unencrypted** |
| `< 0` | dropped |

The operation it built is handed back through its fifth argument, lands in `[x29+0x300]`, is copied
into a one-element array at `[x29+0x110]`, and is what the enqueue sends.

Completions are consumed rather than merely counted: each one's status is read, four bytes are trimmed
from the last segment's length, the operations are returned to their pool in bulk, and the frame is
re-injected - to the event device or to `rte_eth_tx_burst`, depending on one bit of the IPsec
dynamic field.

## What the counter actually means, and this corrects two pages

`FPCNTR_FROM_WIRE_TO_IPSEC_ENCR` is counter 70. There is **one** increment site for a whole class of
counters, and it is table-driven:

    425f30  add  x19, x26, w19, uxtb #3     ; base + (index & 0xff) * 8
    425f34  ldr  x0, [x19, #8]
    425f38  add  x0, x0, #1
    425f3c  str  x0, [x19, #8]

So counter N lives at `base + 8 + 8N`, and the index is a byte out of a 16 KB verdict table. That is
why searching for a literal offset found nothing: no offset is ever written as a constant.

That site is in **`worker_ordered`** [`.text +0x423e30`], 27,536 bytes, 6,884 of 6,884 instructions
read - and `worker_ordered` has **zero** references to the crypto globals page. It cannot touch a
crypto device. It is called at `0x432c2c`, long before the enqueue at `0x4339dc`.

**So `FROM_WIRE_TO_IPSEC_ENCR` rising means a frame was classified for encryption. It does not mean an
operation was built, and it does not mean anything was handed to the engine.**
[The crypto engine is fed](the-crypto-engine-is-fed.md) and
[the engine accepts the frame](the-engine-accepts-and-nothing-returns.md) both read it as the stronger
thing. The finding those pages rest on is unaffected - with the association's `lif_index` at 0 the
counter does not move and with a non-zero one it does. **Not** that the value must be the ingress
interface: the vendor's shift is 12, so 10 is iface 0 / VLAN 10 and 0xa000 is iface 10 - two
different LIFs, and what was measured is only that zero is refused. Corrected in
[the association belongs to an interface](the-crypto-engine-is-fed.md). Only the words "reaches the engine" were too strong.

## Both possibilities below are now closed

`crypto_issued` and `crypto_processed`, read by name from the live process, are **equal** - 12 and 6 on
two queue pairs, totalling the eighteen frames these trials classified. So neither possibility holds:
the operations were submitted *and* harvested. See
[the crypto path completes](the-crypto-path-completes.md). The section is kept because the reasoning
that produced the two candidates is what said which two variables to read.

## What that left at the time, and it was two possibilities, not a mystery

Every failure exit on this path increments a counter of its own - 123 `ENQ_FULL`, 124
`SADB_POST_ERR`, 125 `SADB_PRE_ERR` - and on the hardware **not one `CRYPTO_DROP_*` counter moved at
all**. So neither `crypto_pkt_submit` nor the enqueue reported a failure. Two possibilities remain:

1. **`crypto_pkt_submit` returned `> 0`** and the frame left unencrypted by the Ethernet-transmit slot.
   Nothing was ever submitted.
2. **It returned `0`, the enqueue succeeded, and the completion was never harvested.**

### The measurement that separates them

The loop keeps its own accounting in the data segment: `issued[]` at `0x68e688`, `processed[]` at
`0x68e748`, 24 slots of eight bytes each. All of `issued[]` zero means nothing was ever submitted and
the answer is (1); non-zero means (2), and `processed[]` beside it says whether anything came back.

**It cannot be read the obvious way.** The data segment is mapped - `/proc/PID/maps` shows
`00670000-00690000 rw-p`, so the addresses are in it, and the binary is not position-independent - but
`/proc/PID/mem` answers `EIO` for every offset on this kernel, with no `yama` knob present to explain
it. So the reads have to come from somewhere else, and that is the open end of this page.

A second, independent test needs no memory access at all: a frame consumed by the crypto engine is
received and not transmitted, so **`RX_WIRE` must exceed `TX_WIRE` by exactly the number of frames
classified for encryption** if possibility (2) holds, and the gap must not change if (1) holds. That
needs a flow carrying steady traffic for two windows, which is the one ingredient this appliance did
not have when it was tried.

## Lesson

Three things here were nearly published wrong, and all three were attribution rather than arithmetic:
a store reached from two places, attributed to the predecessor that happened to be adjacent; a counter
whose offset is never written down, searched for as a constant; and a counter name read as a statement
about what the hardware did rather than about what the software decided. **A name, an address and an
adjacency are all claims, and each one needs its own check** - the arithmetic was right every time.
