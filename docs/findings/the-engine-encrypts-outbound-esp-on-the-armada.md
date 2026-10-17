<!-- reviewed: 2026-10-14 -->
# The engine encrypts outbound ESP on the ARMADA: the transmit side, and the two doors to it

2026-10-14, the XGS 136, step 5 slice 5 of [the offload plan](../npuep/offload-plan.md). With
[the receive termination](the-host-answers-through-the-tunnel-on-the-armada.md) in place, this
slice gives the engine the encrypt association too and takes the kernel's cipher from it, so an
AES-GCM tunnel runs with both directions in hardware. Measured on the direct-link bench. Module
build `4d0fe64e`, from the working tree.

## The request the fast path reads: from the vendor's own host module

The receive metadata was measured; the transmit metadata was read. The appliance's own SFOS host
module of the fast path, `usfp_firewall.ko`, has no debugging information but keeps its symbols,
and among them are `mrvl_cst_set_tx_meta` and `mrvl_cst_set_rx_meta` - the callbacks `mv_pport`
calls to fill and read the 64 bytes in front of every frame. Disassembled on the appliance:

- **Transmit**, host to fast path: `+0` = 1 (md_valid), `+1` = sa_is_out, `+6` a 16-bit MTU (the
  route's, only on the IPsec path), `+11` the queue, `+12` a 32-bit sa_index = the association's
  offload handle. No magic word. That is the OCTEON TX's `struct usfp_sp_md`, byte for byte.
- **Receive**, fast path to host: the magic `0xb44399a2` at `+0` (a frame without it is ignored),
  md_valid at `+5` (bit 0 says the flow identity at `+8` is valid), the queue at `+11`, sa_index as
  16 bits at `+16` and its revision at `+18` - exactly what the driver measured and decodes.

So the two directions carry different layouts on this family, and both are now known from the
vendor's code rather than from bisection.

## The two doors, and the one that was found by its absence

The driver follows the OCTEON TX's rule, one encryptor per association: an ESP sequence number may
be used once, so once the engine has an association every packet of it must go there. The kernel's
ESP transform for the association is interposed (`npuipsec_xf_output` in place of `esp_output`) at
the moment the engine takes it - the IV counter moved to the upper half of its space under the
association's lock, the engine started a million numbers past the kernel's counter, ready, taken and
the transform swapped in one hold. That is the door a FORWARDED packet comes through.

The first build had only that door, and the first measurement with both associations accepted was
every ping from the appliance **leaving in the clear**: `transmit: taken 0`, no ESP on the port, no
software ESP either - the fast path's `FROM_KN_TO_WIRE` had them. A packet this host generates and
routes out of the association's own port never reaches the transform: the kernel's own offload path,
`ipsec_accel_output`, tags the plaintext packet with the association's handle and hands it to the
port's transmit as it is. The OCTEON TX driver meets that in `octep_ipsec_tx_prepare`; this one now
does the same at the port's transmit, wrapped around the stack's own (`npuipsec_tx_prepare`, called
without the driver lock, since resolving the next hop can send an ARP request back through the same
transmit): a tagged frame becomes the envelope - the tunnel's next hop, an outer IPv4 header the
engine replaces with its template, the ESP header with the SPI and a zero sequence, the IV's room,
the inner packet, the trailer, the ICV's room - or, when its association is not here, is dropped and
counted (`tx_nosa`), never sent in the clear. The envelope carries a tag that `npugiu_encap` turns
into the four metadata fields above.

## Measured

- **Appliance to peer**, ten pings: **10 of 10**, 9 ms average. `transmit: generated 10`,
  `giu.tx_ipsec 10`, `FROM_KN_TO_IPSEC_ENCR +10` on the fast path, and `netstat -s -p esp` unchanged:
  nothing was encrypted in software. The peer's child counted them in. The replies came back as ESP,
  were decrypted by the engine and terminated: `receive: done +10`.
- **Peer to appliance**, twenty pings: **20 of 20**, 5.8 ms average; the twenty replies left by the
  engine (`generated 30` cumulative, `FROM_KN_TO_IPSEC_ENCR +20`), the twenty requests decrypted by
  it (`FROM_WIRE_TO_IPSEC_DECR +20`). The peer's child: 30 in, 30 out.
- Both directions of the tunnel in hardware, with `ccp` unloaded for the session as the receive page
  says; nothing dropped, nothing counted on `nosa`, `drop`, `nonhop`, `toobig` or `nobuf`.
- A capture of the port's own transmit shows the outbound ESP with `seq=0`: that is the envelope as
  the host handed it over, before the engine wrote the sequence, the IV and the ICV in place. The peer
  accepted every one, so the wire carried the engine's numbers.
- **Forwarded, both ways** - the other door, measured later the same evening. A PC behind the
  appliance's bridge pinged an inner address of the peer that lies inside the appliance's own LAN
  prefix: the peer holds it on its loopback, the appliance publishes it with a proxy-ARP entry and
  routes it through the tunnel, so the PC's packets have to go through the appliance and nothing on
  the PC changes. **10 of 10**, 14 ms, TTL 63. On the appliance `transmit: forwarded 10` - the
  requests, through the interposed transform - and `receive: done +10` - the replies, terminated and
  forwarded back; `FROM_KN_TO_IPSEC_ENCR +10`, `FROM_WIRE_TO_IPSEC_DECR +10`; `netstat -s -p esp`
  still at zero packets out since boot. The peer's child: 10 in, 10 out.

## Not done, and said so

- The encrypt accept requires the far end's neighbour to be resolved already; the OCTEON TX
  tolerates one that has just been asked for (`EWOULDBLOCK`). A small follow-up, not in this build.
- Fragmentation before the envelope: an inner packet the envelope cannot fit is dropped and counted
  (`tx_toobig`); the OCTEON TX fragments it first. The ICMP replies here are small.
- NAT-T (the UDP header in the envelope), CBC with HMAC, IPv6 outer and inner, extended sequence
  numbers: declined to the kernel, as on the OCTEON TX before each was taken.
- The open-entry regime on this bench, and the flow-integrated tunnel path, are still to measure.
- `ipsec.flows 2` is never set here.
