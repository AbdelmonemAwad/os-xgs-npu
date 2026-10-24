<!-- reviewed: 2026-10-14 -->
# The decrypted frame is dropped on the ARMADA: ~~the receive model is not the OCTEON TX's~~

**Corrected later the same day, 2026-10-14.** ~~What this page measured is one regime, not the family:
on the WAN port, whose interface entry the flow work of steps 2 to 4 had opened.~~ (Measured later the
same evening: an opened entry does not drop the frame either; what dropped it here is still open.) On
a port whose entry is the driver's default (`offload_disabled`) the engine hands the decrypted frame to the host
with the handle in the metadata, twenty of twenty, decrypted in place - the OCTEON TX's shape byte
for byte: [the engine hands up the decrypted frame on the ARMADA](the-engine-hands-up-the-decrypted-frame-on-the-armada.md).
So ~~the receive path is NOT a port of `octep_ipsec_rx`~~ - it is exactly that, and ~~it needs the
stripped `usfp` fast-path binary read~~ it does not. The measurements below stand for the open-entry
regime; the conclusions drawn from them about the family are struck where they appear.

2026-10-14, the XGS 136, step 5 of [the offload plan](../npuep/offload-plan.md). The engine
[decrypts inbound ESP in hardware](the-engine-decrypts-inbound-esp-on-the-armada.md); this is where
the decrypted frame goes, measured ~~- and it does not go to the host~~ - on a port with its entry
open, and there it does not go to the host.

## What was measured, on a live tunnel

With a decrypt association accepted (`ipsec.accept_decrypt`) and a peer driving ESP through the
tunnel:

- The fast path decrypts each frame (`FPCNTR_FROM_WIRE_TO_IPSEC_DECR` moves with the burst), and its
  own packet dissector counts both the arriving ESP and the decrypted inner packet: reading the
  debug counter set (RPC command 43, a separate 177-entry `PD_DEBUG_CNT_*` enum from the system set
  command 44 reads; 44 and 45 on SFOS 23, [rpc-numbering.md](../families/rpc-numbering.md)) over a clean burst, the only counters that moved by exactly the burst were
  `PD_DEBUG_CNT_ESP_PKT` and `PD_DEBUG_CNT_ICMP_PKT` (the inner was an ICMP ping). So the inner
  packet is produced and dissected.
- But it never reaches the host. No `FROM_IPSEC_DECR_TO_KERNEL` or `..._TO_WIRE`; the inner tuple
  never appears in the flow candidate table (only the tunnel's outer IKE/ESP packets and ordinary
  WAN traffic do); the peer gets no reply. Opening the offload gate does not change this - it only
  makes ordinary WAN traffic flow-processed, not the decrypted inner packets.
- And there is no per-frame crypto marker where the OCTEON TX driver reads one. A port-filtered
  metadata instrument (below) and a tally of frames whose metadata word at the OCTEON TX `sa_index`
  offset is non-zero both came up empty across the burst: no received frame carried that marker.

## So ~~the ARMADA's receive path is a different shape~~ a port with its entry open takes the flow path

On the OCTEON TX the coprocessor decrypts in place and hands the host the frame, with the
association's handle in the metadata, for the driver to terminate (`octep_ipsec_rx`). ~~On this family~~
On a port whose entry is open, the coprocessor decrypts and dissects the inner packet and then
**drops it** in the fast path - it is neither delivered to the host nor carried to the wire, and it
carries no marker the host could key on ~~. Whatever disposes of a decrypted inner packet lives in the
stripped `usfp` fast-path binary (no debug info, unlike `usfp_rh.ko`), so what it is waiting for - a
microflow with an action, a next hop, an option bit in the association that is not in `SA_ADD`'s 192
bytes - is the next thing to find, and finding it means reading that binary~~ ~~- because it never
reaches the host at all: past an open entry a decrypted frame is looked up in the flow table, and
without a microflow for the inner tuple it is dropped there.~~ Measured later the same evening: an
opened entry punts the decrypted frame to the host with its marker too (`MFLOW_NOT_ACTIVE`), so the
cause of what this page saw is still open - the likeliest is an active microflow left on this port by
the flow work. With the entry shut, the default, the frame leaves by the entry's gate with its marker
([the correction](the-engine-hands-up-the-decrypted-frame-on-the-armada.md)).

## The instrument added

`dev.npuep.0.giu.wan_filter_tag` (set to a port tag, e.g. the WAN's) makes `giu.last_wan_meta` keep
the metadata of the last frame on that port, so a frame on the port where decrypted ESP surfaces is
not overwritten by the management session's own LAN frames - the problem that made `giu.last_meta`
useless here. `giu.sa_meta_frames` counts frames whose metadata word at the OCTEON TX `sa_index`
offset is non-zero. Read-only instruments, off by default (`wan_filter_tag` 0).

## Where this leaves IPsec on the ARMADA

Settled: the kernel offers associations to the driver (#394), the engine accepts a GCM `SA_ADD`
(#395), and it decrypts inbound ESP in hardware (#396). ~~Open, and larger than first thought: the
decrypted inner packet is dropped in the fast path with no host-visible marker, so the receive path
is not a port of `octep_ipsec_rx` - it needs the stripped `usfp` binary read to learn how a decrypted
packet is meant to be delivered on this family.~~ Withdrawn: on a port with its entry shut the frame
reaches the host with its marker, so the receive path is a port of `octep_ipsec_rx`
([the correction](the-engine-hands-up-the-decrypted-frame-on-the-armada.md)); ~~what this page
measured is the open-entry regime, where the flow path takes the decrypted frame~~ - an open entry was
later measured not to drop it; what did here is still to be found. The transmit side
waits behind the termination. `ipsec.flows 2` is never set here.
