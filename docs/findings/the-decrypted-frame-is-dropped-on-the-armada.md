# The decrypted frame is dropped on the ARMADA: the receive model is not the OCTEON TX's

2026-10-14, the XGS 136, step 5 of [the offload plan](../npuep/offload-plan.md). The engine
[decrypts inbound ESP in hardware](the-engine-decrypts-inbound-esp-on-the-armada.md); this is where
the decrypted frame goes, measured - and it does not go to the host.

## What was measured, on a live tunnel

With a decrypt association accepted (`ipsec.accept_decrypt`) and a peer driving ESP through the
tunnel:

- The fast path decrypts each frame (`FPCNTR_FROM_WIRE_TO_IPSEC_DECR` moves with the burst), and its
  own packet dissector counts both the arriving ESP and the decrypted inner packet: reading the
  debug counter set (RPC command 43, a separate 177-entry `PD_DEBUG_CNT_*` enum from the system set
  command 44 reads) over a clean burst, the only counters that moved by exactly the burst were
  `PD_DEBUG_CNT_ESP_PKT` and `PD_DEBUG_CNT_ICMP_PKT` (the inner was an ICMP ping). So the inner
  packet is produced and dissected.
- But it never reaches the host. No `FROM_IPSEC_DECR_TO_KERNEL` or `..._TO_WIRE`; the inner tuple
  never appears in the flow candidate table (only the tunnel's outer IKE/ESP packets and ordinary
  WAN traffic do); the peer gets no reply. Opening the offload gate does not change this - it only
  makes ordinary WAN traffic flow-processed, not the decrypted inner packets.
- And there is no per-frame crypto marker where the OCTEON TX driver reads one. A port-filtered
  metadata instrument (below) and a tally of frames whose metadata word at the OCTEON TX `sa_index`
  offset is non-zero both came up empty across the burst: no received frame carried that marker.

## So the ARMADA's receive path is a different shape

On the OCTEON TX the coprocessor decrypts in place and hands the host the frame, with the
association's handle in the metadata, for the driver to terminate (`octep_ipsec_rx`). On this family
the coprocessor decrypts and dissects the inner packet and then **drops it** in the fast path - it
is neither delivered to the host nor carried to the wire, and it carries no marker the host could key
on. Whatever disposes of a decrypted inner packet lives in the stripped `usfp` fast-path binary (no
debug info, unlike `usfp_rh.ko`), so what it is waiting for - a microflow with an action, a next hop,
an option bit in the association that is not in `SA_ADD`'s 192 bytes - is the next thing to find, and
finding it means reading that binary.

## The instrument added

`dev.npuep.0.giu.wan_filter_tag` (set to a port tag, e.g. the WAN's) makes `giu.last_wan_meta` keep
the metadata of the last frame on that port, so a frame on the port where decrypted ESP surfaces is
not overwritten by the management session's own LAN frames - the problem that made `giu.last_meta`
useless here. `giu.sa_meta_frames` counts frames whose metadata word at the OCTEON TX `sa_index`
offset is non-zero. Read-only instruments, off by default (`wan_filter_tag` 0).

## Where this leaves IPsec on the ARMADA

Settled: the kernel offers associations to the driver (#394), the engine accepts a GCM `SA_ADD`
(#395), and it decrypts inbound ESP in hardware (#396). Open, and larger than first thought: the
decrypted inner packet is dropped in the fast path with no host-visible marker, so the receive path
is not a port of `octep_ipsec_rx` - it needs the stripped `usfp` binary read to learn how a decrypted
packet is meant to be delivered on this family. The transmit side waits behind it. `ipsec.flows 2`
is never set here.
