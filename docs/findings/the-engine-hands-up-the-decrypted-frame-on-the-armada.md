<!-- reviewed: 2026-10-14 -->
# The engine hands up the decrypted frame on the ARMADA: the OCTEON TX's receive model after all

2026-10-14, the XGS 136, step 5 of [the offload plan](../npuep/offload-plan.md). This corrects
[the decrypted frame is dropped on the ARMADA](the-decrypted-frame-is-dropped-on-the-armada.md),
written earlier the same day: on a front port whose interface entry is the driver's default, the
engine decrypts inbound ESP in place and hands the frame to the host with the association's handle
in the metadata - exactly what the OCTEON TX does, and what `octep_ipsec_rx` terminates.

## The bench

The lab tunnel was moved off the WAN onto a direct cable between a front port of the 136 and a port
of the peer firewall, with a point-to-point /30 on it (the port taken out of the LAN bridge first, so
nothing joins two LANs), AES-GCM-16, the peer's inner selector the 136's own LAN address so the
decrypted packet is for this host. The 136 initiates the IKE and the peer answers it - the 136's
filter has no rules on an unassigned port, so only its own outbound states let the exchange through,
and inbound ESP does not go through the filter at all: the engine takes it before the host sees it.
`ipsec.accept_decrypt` on before the tunnel came up, so the decrypt association was accepted at
install: `npup7 spi … decrypt | SA_ADD accepted by the engine`.

## What was measured, on a burst of twenty pings from the peer

- **All twenty ESP frames reached the host**: `tcpdump -ni npup7 esp` on the 136 captured each one,
  SPI and sequence 1..20 intact.
- **All twenty carried the marker**: `giu.sa_meta_frames` went 0 → 20, the tally of received frames
  whose metadata word at +16 (the OCTEON TX's `sa_index` offset) is non-zero. Not one frame without it.
- **The payload was decrypted in place.** `tcpdump -X` on one frame:

  ```
  outer IPv4 (proto 50) | ESP: SPI, seq | 8 bytes where the IV was - the peer's MAC and 0x0800, L2 residue |
  inner IPv4 header 45 00 00 54, proto 1 | ICMP 08 00 echo request, the standard 08 09 0a 0b … payload |
  pad 01 02, pad length 02, next header 04 (IPv4-in-IPv4: tunnel mode) | ICV, 16 bytes
  ```

  the same length as the frame on the wire. That is the OCTEON TX's shape byte for byte, down to the
  link-layer residue in the IV slot, as `octep_ipsec_rx`'s own comment describes it.
- **The counters** (`npuep-fpcnt.sh -d` over the burst): `RX_IPSEC +20`, `FROM_WIRE_TO_IPSEC_DECR +20`,
  and the frames went up through the entry's own gate - `FROM_WIRE_TO_KN_LIF_OFFLOAD_DISABLED +274`,
  `TX_KN +274` (the twenty among the management session's and the WAN's frames). Nothing dropped.
  `FROM_IPSEC_DECR_TO_KERNEL` (114) did not move: that is not the path a decrypted frame takes here.
- The peer got no replies, as expected: the host receives an ESP frame with a plaintext payload,
  which `esp_input` drops as a failed authentication. Terminating it is the driver's job - the next
  slice, a port of `octep_ipsec_rx`.

## Why the earlier page saw the opposite

The two measurements differ in one thing: the port. The earlier one was on the WAN, `npup9`, where the
flow work of steps 2 to 4 had opened the interface entry and the gate; this one is on `npup7`, a port
whose entry is the driver's default, `offload_disabled`. [Step 2](../npuep/offload-plan.md) measured
the chain on this family: the entry first, the gate second, the flow table third. ~~A decrypted frame
on a port with its entry open goes on to the flow table, where - with no microflow for the inner
tuple - it is dropped, and the host never sees it: which is what the earlier page measured, and why
it saw no marker.~~ Measured later the same evening (below): an open entry does not lose the frame
either. On a port with the entry shut, the decrypted frame leaves by the entry's gate with its
marker, as every other frame on that port does. Both measurements stand; the earlier page's
conclusion - that the family's receive model is a different shape - does not, and is struck there.
~~The one measurement that closes this - `npup7` with its entry opened, the same burst - is the next
thing to do after the termination.~~ Done, below; it closed the regime question and reopened the
WAN one.

So the two regimes, each measured on its own port:

| the port's entry | what the engine does with inbound ESP it has an association for |
| --- | --- |
| `offload_disabled` (the driver's default) | decrypts in place, hands the frame up with the handle in the metadata: the OCTEON TX model, terminated by the driver |
| offload enabled | decrypts, looks the inner packet up as a flow; ~~without a microflow for it, drops it~~ measured below: makes a microflow for it and punts the frame with its marker, as for any connection the host has not accelerated (a microflow carries `sa_index` and an action for exactly this: the flow-integrated path, not yet built here) |

## The regime test, measured the same evening

On `npup7` with the termination in place (build `8307c478`), a burst of ten from the peer in each
state, the entry written by hand (`LIF_ADD_UPDATE`, the offload bit only) and read back:

- **Entry opened, gate closed**: 10 of 10 answered. The decrypted frames reached the host by
  `FROM_WIRE_TO_KN_FORCED` (+14: the ten and the port's other frames) with their marker
  (`sa_meta_frames +10`, `receive: done +10`).
- **Entry and gate opened** - the earlier WAN port's state: 10 of 10 answered. The decrypted frames
  went to the flow table, which made a microflow for the inner packet and **punted them with their
  marker**: `FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE +10`, `sa_meta_frames +10`, `receive: done +10` - and
  the inner tuple appeared in the driver's candidate table (`cand_pushed +1`) with that microflow's
  identity. Nothing dropped.
- **Restored** (entry shut, gate closed): 10 of 10, by the entry's gate as before.

So an open entry does not lose a decrypted frame: the flow table treats it as any other frame of a
connection it has no active microflow for, and the host gets it with the marker - which is also the
hook for the flow-integrated tunnel path, a microflow carrying `sa_index`, as on the OCTEON TX. What
the earlier WAN measurement saw - no frame at the host, no marker, no candidate - therefore has
another cause. The likeliest is an ACTIVE microflow left on that port by the flow work, matching the
decrypted inner and forwarding or dropping it in hardware; it is to be re-measured on the WAN port,
and until then no explanation of it is claimed here.

## Two instrument notes

- `giu.wan_filter_tag` takes the frame's two-byte **port tag**, not the port's index: it compares
  against the tag the fast path put in front of the frame. A port's tag is `npugiu_port_tag()`.
- `npuep-fpcnt.sh` reads its names file as `index name`; the enum dump kept with the appliance's
  own records is `name index`, and fed to the tool it counts zero names and reads nothing. The copy
  next to the tool, `fpcntr-names-armada.txt`, is the right one - since 2026-10-25
`tools/fpcntr-names-sfos21.txt`, the list of the vendor version this appliance's coprocessor runs,
which the tool now picks by itself ([rpc-numbering.md](../families/rpc-numbering.md)).

## Where this leaves IPsec on the ARMADA

The engine takes a GCM association (#395), decrypts inbound ESP in hardware (#396), and - this page -
hands the decrypted frame to the host with the handle in the metadata, the OCTEON TX's shape. The
receive termination is therefore a port of `octep_ipsec_rx`, not a reading of the stripped fast-path
binary; it is built next and measured on the same bench. `ipsec.flows 2` is never set here.
