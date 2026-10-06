# The association belongs to an interface, and that was the whole refusal

Measured on an XGS 3300, 2026-10-06. `FPCNTR_FROM_WIRE_TO_IPSEC_ENCR` had been **zero since this
project began**. It is not zero any more.

## What changed

Nothing was built. The flow path finished in #256 and #257 made the experiment possible, and the
experiment was four reads and one write per trial. A real transiting flow was accelerated, then its
own entry was reprogrammed with `MFLOW_PROGRAM` to name a security association, and the far side's
counters were read across two windows.

| the association's `lif_index` | what the far side did with the flow's frames |
|---|---|
| no association at all | `FROM_WIRE_TO_WIRE +17` - forwarded in hardware, in the clear |
| **0** | nothing. Not forwarded, not encrypted, every frame punted |
| **10** - *not* interface 10; see below | **`FROM_WIRE_TO_IPSEC_ENCR +7`** |
| **0xa000** - oxp0's own LIF, `(iface 10 << 12) \| vlan 0` | **`FROM_WIRE_TO_IPSEC_ENCR +6`** |

**A zero `lif_index` is refused and a non-zero one is accepted. That, and only that, is what these
trials prove** - and the sentence that used to stand here, that the value must be the flow's ingress
interface and that 10 and 0xa000 are two ways of writing it, is withdrawn.

The vendor has one encoding, and the interface is the **high** bits: the shift is 12, so
`lif_index = (iface << 12) | vlan`. Under it, `0xa000` is iface 10 with no VLAN - which is oxp0, and
is the form the vendor's own host driver builds. But the bare `10` is **iface 0 with VLAN 10**, a
different logical interface altogether, and not the flow's ingress port either. The two successful
trials were not one interface written twice; they were two unrelated LIFs whose only shared property
is being non-zero. No value naming a *wrong* non-zero interface has been tried.

And the reason cannot be anything this project can read, because for an **encrypt** association the
far side's handler stores `lif_index` and never looks at it again. Its one interface-keyed structure,
an SPI hash, is written only for a **decrypt** association - where a non-zero VLAN is refused
outright, so the bare `10` would have failed on the inbound direction. Write the encoded form always:
`rc 0x0000` on an egress association says nothing about whether the LIF means what was intended.

## Why this was not found before

Because of a refusal that reads like success. **`SA_ADD` is not idempotent: it answers `rc 0x0002`
for an index that is already occupied, and installs nothing.**

| | |
|---|---|
| index 1, already in use | `rc 0x0002` |
| index 5, free | `rc 0x0000` |
| index 6, free, `lif 10` | `rc 0x0000` |
| index 7, free, `lif 0xa000` | `rc 0x0000` |
| index 5 again, now in use | `rc 0x0002` |

The LIF had been tried before, in both encodings, and recorded as ruled out. It was tried by
re-installing the same index - which answered `rc 0x0002` and left the first association exactly
where it was. **The negative was measuring the association it was trying to replace.** Every trial
here uses a fresh index for that reason, and the first thing to check about any `SA_ADD` is whether
it said 0 or 2.

## The flow entry was never the problem, and the entry itself says so

`LO_MFLOW_READ` - command 40 - answers cleanly when it is given a real `mflow_id`. It had been
recorded as answering inconsistently; it was being asked for indices 0 to 7, which are not flows.
Asked for the id a punted frame named, it returns 116 bytes: an 8-byte preamble, the 64-byte key,
the 20-byte entry and the 24-byte operational block.

The operational block, read back after naming association 1:

    opr_fl = 0x2e810001
             sa_index 1   action 1 FWD   dir 1   bridge_control 0xe   state 2 ACTIVE

**The far side stored the association index and kept the flow ACTIVE.** So when every frame was
punted under `FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE`, the entry was not inactive - that counter is where
a flow the fast path declines to use is charged, whatever the reason. Reading the entry back is what
told the two apart, and no counter would have.

And the key confirms an encoding that had only been inferred. Addresses below are from the
documentation ranges in place of the appliance's own:

    key +0x00  lif_id      0x0000a000      (iface 10 << 12) | vlan 0  - oxp0
        +0x04  dst_mac     02:00:00:00:00:01    the bridge's address, the frame's gateway
        +0x0a  src_mac     02:00:00:00:00:42    the machine behind it
        +0x10  ethtype     0x0800
        +0x12  ip_family   2
        +0x13  ip_proto    6
        +0x14  dport 443   sport
        +0x18  sip4 198.51.100.120   dip4 192.0.2.10
        +0x20..0x3f        zero

## What the counter means, corrected

**`FROM_WIRE_TO_IPSEC_ENCR` means a frame was classified for encryption, not that it reached the
engine.** Its only increment site is in `worker_ordered`, which has zero references to the crypto
globals page and runs long before anything is submitted - see
[both ends exist](both-ends-exist.md). The table above is unaffected: the association's `lif_index`
is what decides whether a frame is classified at all, and that is what was measured. The phrase
"reaches the engine" below was too strong when it was written and is kept so the correction has
something to point at.

## Where it stops now

The frames go into the crypto engine and nothing comes out of it. `FPCNTR_RX_IPSEC` - which is what
a frame returning from the engine is charged to - did not move in either trial, and neither did any
`CRYPTO_DROP_*` counter.

That is a different place from where this has been stuck. The earlier route - a host-injected frame
marked for encryption in its metadata - reached `crypto_pkt_submit` and died at
`FPCNTR_CRYPTO_DROP_SADB_PRE_ERR`, because the SA index the submit reads out of an mbuf dynamic
field was never written. The flow route writes it from the entry, reaches the engine, and **is not
dropped by it**: no drop counter moved at all. So the question is no longer *why is the frame
refused* but *where does an accepted frame go*.

Worth noting what the counter list does and does not have: there is `FROM_IPSEC_DECR_TO_WIRE` for
the inbound direction and **no outbound completion counter at all**. The outbound path's evidence is
`RX_IPSEC` and then `TX_WIRE`, not a counter named for it - so an absence here has to be read
carefully, and `RX_IPSEC` is the one to watch.

## Lesson

A negative result is only as good as the write that produced it. `SA_ADD` answering `rc 0x0002`
looks like a completed command to anything that does not check, and it turned a parameter that
*was* the answer into a parameter that had been *ruled out* - on this page's own evidence, for three
days. **Check the return of a write before believing anything measured after it**, and prefer a
fresh index to a re-used one, because a write that refuses to overwrite cannot be told from one that
overwrote identically.
