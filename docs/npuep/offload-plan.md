<!-- reviewed: 2026-10-14 -->
# Offload on the ARMADA family: the plan

Written 2026-10-14, for review before anything is built. The XGS 3300's coprocessor carries the
firewall's flows and its IPsec associations today, and every step of that work is on the
findings pages. The XGS 136's coprocessor runs the same vendor fast path - a different build for
a different silicon - and this page says how the same capability is brought to it, in what order,
and what is already known.

## What is already known, from the material on hand

- **The control channel is the same channel.** `contrib/npuep/npurpc.c` opens the RPC facility,
  fills the interface and port tables, and reads them back; the three commands it uses today are
  `LIF_ADD_UPDATE`, `PPORT_UPDATE` and `LO_LIF_READ` ([rpc.md](rpc.md)).
- **The command set is the OCTEON TX's command set.** The vendor's target-side handler for this
  family, `usfp_rh.ko`, names fifty commands, and they are the ones the XGS 3300's driver learned
  one by one: `FW_CFG_PARAMS_SET` (the gate), `FW_STATE_REV_SET` and `FW_L3_FWD_STATE_REV_SET`
  (the revision barrier), `CONN_CREATE_FP`, `CONN_MODIFY_FP`, `CONN_MODIFY_FP_VERDICT`,
  `CONN_RECLAIM_FP`, `CONN_TRACK_FP`, `CONN_GET_FP_TCP_STATE`, `CONN_CFG_FP_TCP_SEQ_CHK`,
  `FLOW_CREATE_FP`, `MFLOW_PROGRAM`, `MFLOW_INVALIDATE`, `NHOP_PROGRAM`, `NHOP_UPDATE`, the
  `LO_*_READ` readers for connections, microflows, next hops, associations and the three
  counter sets, the `SA_*` family for IPsec, `QOS_*`, `DOS_*`, the live-UID pair and
  `PLATFORM_READ`. The same names, so the same protocol; the payload layouts are read from this
  family's own module, which was built with debugging information, before any of them is sent.
- **The gate is the same gate.** A frame reaches the host because the interface entry says
  `offload_disabled` - the fast path's own counter for it is
  `FPCNTR_FROM_WIRE_TO_KN_LIF_OFFLOAD_DISABLED` - and the microflow counters
  (`MFLOW_NOT_ACTIVE`, `MFLOW_REV_MISMATCHED`, `MFLOW_LU_ERR`, the `MFLOW_DEBUG_CNT_*` set) are
  the ones the 3300's instrument reads by name.
- **The datapath is not the same datapath.** The ARMADA's GIU rings and 66-byte header
  ([giu.md](giu.md)) are not the OCTEON TX's SDP rings and 82-byte return prefix; the per-frame
  marker that chooses encryption on the 3300 has to be found again here. The crypto engine is
  the EIP-197 (`FIRMWARE_EIP207_CS_FLOW_TR_*` in the fast path), not the OCTEON's.
- **The appliance is reachable at the speed of the work.** The console on `cuau2` is the path
  today; the management link over PCIe is the fast channel once
  [its plan](mvmgmt-in-opnsense.md) is done, and that plan comes first.

## Sources, and the pictures

The command names and counter names are read out of the vendor's target-side handler for this
family, `usfp_rh.ko`, and its fast path, `usfp`, both from the appliance's own coprocessor
image (the GPL release, `kmods-xgs-gr-drivers`, carries the sources of the kernel side). The
XGS 3300's side of every command is on the OCTEON TX pages: [flow-lifecycle.md](../octep/flow-lifecycle.md),
[lif-provisioning.md](../octep/lif-provisioning.md), [ruleset-reload.md](../octep/ruleset-reload.md)
and the findings from [the sweep and the panic](../findings/the-sweep-and-the-panic.md) to
[a frame was accelerated](../findings/a-frame-was-accelerated.md), all listed on
[the docs index](../README.md). This family's datapath is on [giu.md](giu.md) and its control
channel on [rpc.md](rpc.md); the board itself, part by part and with its photographs, is on
[the hardware page](../hardware/xgs136.md) - [the port chain](../images/hardware/xgs136/port-chain.svg),
[the board map](../images/hardware/xgs136/board-map.svg), [the CN9130](../images/hardware/xgs136/cn9130.jpg),
[the switch](../images/hardware/xgs136/88e6193x.jpg) and [the quad PHY](../images/hardware/xgs136/aqr412c.jpg).

## The order of work

1. ~~**Read the payloads before sending any.**~~ **Done 2026-10-14** - [rpc-payloads.md](rpc-payloads.md): the same bytes. As planned: `usfp_rh.ko` of this family, with its debugging
   information, gives every command's structure, field and offset; the 3300's `octep.h` gives the
   meaning. A table of the two side by side, on a page of its own, closes this step. Nothing is
   sent until the table exists.
2. ~~**The gate.**~~ **Done 2026-10-14** - [the entry first, then the gate, then the flow table](../findings/the-entry-then-the-gate-on-the-armada.md): on this family the entry holds the frames before the gate, and past both the fast path makes a microflow and waits for the host. As planned: `FW_CFG_PARAMS_SET` with the offload bit, read back, on the bench with the
   console open; then one interface entry with `offload_disabled` cleared and the counters
   watched. The 3300's finding that nothing creates a flow by itself is re-measured here before
   it is assumed.
3. ~~**One microflow by hand.**~~ **Done 2026-10-14** - [a flow the host named, dropped by the fast path](../findings/a-flow-dropped-by-the-armada.md): the identity in the metadata, the entry read with READ_ALL, state 2 and the drop action taken in hardware. As planned: `MFLOW_PROGRAM` for one connection the host already carries,
   `LO_MFLOW_READ` to confirm it, the frame counted in hardware, the state
   `MF_ACTIVE`, then `MFLOW_INVALIDATE`. The 3300's "a frame was accelerated" repeated here, with
   the same instrument reading the same counter names.
4. **The driver's flow table on `npuep`.**
   - *Slice 1 done 2026-10-14* - [the candidates, and the vnet that killed the sweep](../findings/the-flow-table-on-the-armada-begins.md): `octep_pf.c` and `octep_nhop.c` ported, the receive path feeds a candidate table, the sweep reads pf state and the next hop (and must `CURVNET_SET(vnet0)`).
   - *Slice 2 done 2026-10-14* - [the driver accelerates a flow](../findings/the-driver-accelerates-a-flow-on-the-armada.md): `FLOW_CREATE_FP` by hand, the microflow reads back active and forwarding.
   - *Slice 3 done 2026-10-14* - [the sweep accelerates flows on its own](../findings/the-sweep-accelerates-on-its-own-on-the-armada.md): `flow.auto` makes the sweep hand over every settled, resolved candidate itself, over a real connection and next-hop table, and a keepalive takes each flow back when pf lets go of its state (the 3300's `dp.auto`/`dp.flows`). The panic this exposed, and its fix, are on [a sleeping thread held the rpc lock](../findings/a-sleeping-thread-held-the-rpc-lock.md).
   - *Slice 4 done 2026-10-14* - [the revision barrier](../findings/the-revision-barrier-on-the-armada.md): `flow.fw_rev_bump` raises a firewall revision and posts `FW_STATE_REV_SET`, which voids the whole microflow table at once; the OPNsense firewall hook writes it on every ruleset reload, so a rule change discards every offloaded flow (the 3300's `rpc.fw_rev_bump`).
   - *Slice 5 done 2026-10-14* - [one next hop for many flows](../findings/one-next-hop-for-many-flows-on-the-armada.md): the next-hop table is keyed by the resolved answer and reference-counted, so flows that leave the same way share one entry (the 3300's `octep_nhop_get`); measured eleven connections over two next-hop entries. The flow ceiling is now the connection table, not the next-hop table.
   - *Slice 6 done 2026-10-18* - [every command cost ten milliseconds](../findings/a-command-cost-ten-milliseconds-on-the-armada.md): the control channel's answer waited for by spinning, 138 microseconds instead of ten milliseconds; the board's platform block read, and the flow table allocated at the size it states - 1,500,000 connections, 65,536 next hops - with hash chains, a free stack and a live list (the 3300's table); 1,024 candidates by hash, up to 256 made a second. The rate connections are made has not been measured since.
   - *Slice 7 built 2026-10-18, not yet measured* - IPv6 connections: the tuple carries both addresses in full, pf is asked with IPv6 keys, the next hop comes from `fib6_lookup` and `nd6_resolve`, a translated IPv6 connection is left with the host ([board-limits.md](../families/board-limits.md)). On the way: [the switch dropped IPv6's neighbour solicitations](../findings/the-switch-drops-multicast-on-the-armada.md), fixed and measured.
   - Still open: the full measurement with the three suites (the clear-text path at a gigabit, the translated path, and the trigger on and off for an hour each) on a quiet single-flow bench; IPv6 traffic through the box.
5. **IPsec, driven by OPNsense.** `SA_ADD`/`SA_DEL` through the kernel's own offload contract as
   on the 3300 (`if_ipsec_accel_methods`), the per-frame marker found, GCM first, then the same
   list: NAT-T, ESN, the replay window, CBC with HMAC, IPv6 outer and inner, measured in the
   same order and recorded the same way.
   - *Slice 1 done 2026-10-14* - [the kernel offers an association](../findings/the-kernel-offers-an-association-on-the-armada.md): the front ports advertise `IFCAP2_IPSEC_OFFLOAD` and the method table is installed, so the kernel offers every installed association to the port that owns an end; the driver reads the direction, records the offer, and declines it for now (the data path is the slices below). Measured with a manual association.
   - *Slice 2 done 2026-10-14* - [the engine takes an association](../findings/the-engine-takes-an-association-on-the-armada.md): the AES-GCM `SA_ADD` (192 bytes, the OCTEON TX layout) and `SA_DEL`, proven by a probe (`ipsec.probe`) that hands each offered GCM association to the engine, reads it accepted, and takes it back - still declined to the kernel, so no data path is needed to measure it.
   - *Slice 3 done 2026-10-14* - [the engine decrypts inbound ESP](../findings/the-engine-decrypts-inbound-esp-on-the-armada.md): with `ipsec.accept_decrypt` set, the driver accepts an offered GCM decrypt association for real (keeps the `SA_ADD`, returns success), and on a live tunnel the engine decrypts the inbound ESP in hardware (`FROM_WIRE_TO_IPSEC_DECR` moved by the burst).
   - *Investigated 2026-10-14* - [the decrypted frame is dropped](../findings/the-decrypted-frame-is-dropped-on-the-armada.md): ~~the decrypted inner packet is produced and dissected (`PD_DEBUG_CNT_ICMP_PKT`) but then dropped in the fast path - it never reaches the host, carries no marker at the OCTEON TX `sa_index` offset, and the gate does not change this. So the receive path is NOT a port of `octep_ipsec_rx`; it needs the stripped `usfp` fast-path binary read~~ - corrected later the same day: that was the WAN port with its interface entry opened by the flow work ~~, where a decrypted frame goes on to the flow table and is dropped for want of a microflow~~ (measured later the same evening on `npup7`: an opened entry punts the decrypted frame with its marker too, `MFLOW_NOT_ACTIVE`, and the inner tuple becomes a flow candidate - so the WAN result's cause is still open, likeliest an active microflow left there by the flow work). On a port whose entry is the driver's default the engine hands the frame up with the handle in the metadata, the OCTEON TX's shape byte for byte: [the engine hands up the decrypted frame](../findings/the-engine-hands-up-the-decrypted-frame-on-the-armada.md), measured on a direct link to the peer firewall.
   - *Slice 4 done 2026-10-14* - [the host answers through the tunnel](../findings/the-host-answers-through-the-tunnel-on-the-armada.md): `npuipsec_rx`, a port of `octep_ipsec_rx`, terminates the decrypted frame off a chain of its own walked with the GIU lock dropped; pings through the tunnel answered both ways, 20/20 and 10/10, with inbound ESP decrypted in hardware (build `8495d091`, working tree). Found on the way: [AMD's platform crypto engine breaks IPsec AES-GCM on this host](../findings/amd-ccp-breaks-ipsec-gcm-on-the-xgs-136-host.md) - `ccp.ko` took the software side's sessions and failed every operation; unloaded for the measurement, an issue for the fix.
   - Instruments for it: `giu.wan_filter_tag` (takes the port's two-byte TAG, e.g. `0x8700` for npup7, not its index) + `giu.last_wan_meta`, `giu.sa_meta_frames`, and now `ipsec.rx_*` with the `receive:` line on `ipsec.sas`.
   - *Slice 5 done 2026-10-14* - [the engine encrypts outbound ESP](../findings/the-engine-encrypts-outbound-esp-on-the-armada.md): the transmit metadata read from the vendor's own host module (the OCTEON TX's `usfp_sp_md`), the encrypt association taken with the kernel's cipher interposed, and the kernel's own offload path met at the port's transmit (the door the first build missed: the appliance's pings left in the clear); both directions in hardware, 10/10 and 20/20 for the appliance's own packets and 10/10 forwarded for a host behind it, nothing in software (build `4d0fe64e`, working tree).
   - *Slice 6 done 2026-10-14* - [fragmentation before the envelope, and a neighbour not yet resolved](../findings/fragmentation-before-the-envelope-on-the-armada.md): one send path for both doors fragments an inner packet the envelope cannot fit (each fragment its own ESP frame) and answers a DF packet with "fragmentation needed" or the sender's error; 1500-byte pings 5/5 from the appliance and 10/10 forwarded, the PC told on DF; the encrypt accept takes a neighbour just asked for (build `8307c478`, working tree).
   - *Slice 7 done 2026-10-14* - [ESP behind UDP on the ARMADA](../findings/esp-behind-udp-on-the-armada.md): `udp_enable` and the two ports in `SA_ADD`, the UDP header in the envelope, both directions taken; measured with `encap = yes` on both ends - 5/5, 3/3 fragmented, 10/10 from the peer, 10/10 and 5/5 forwarded, every frame `UDP-encap: ESP` on the wire, `tx_natt 41` = every envelope and fragment (build `71abd87e`, working tree). The open-entry regime was measured on the way (it punts with the marker; the hands-up page).
   - *Slice 8 done 2026-10-14* - [AES-CBC with an HMAC on the ARMADA](../findings/a-block-cipher-on-the-armada.md): the association's algorithms read into one set (GCM-16, or AES-CBC with SHA-1/256/384/512 as the vendor's host names them), the record carrying IV, ICV and block, the termination and the envelope sized from them, a random IV for CBC; all four hashes measured both ways - 5/5, 3/3 fragmented, 10/10 from the peer each, SHA-256 also forwarded 10/10 and 5/5 - every frame on the engine (186728-byte build, working tree).
   - *Slice 9 done 2026-10-14* - [a replay window on the ARMADA](../findings/a-window-on-the-armada.md): the decrypt association's window sent in `SA_ADD` with the anti-replay bit, as packets, a power of two up to the engine's 4,096, wider or odd ones left to the kernel (`ipsec.win_refused`); measured at 32, 1,024 and 4,096 on the engine and 48 and 8,192 refused, and with a replayed frame from the peer - `CRYPTO_DROP_REPLAY_DUP +2`, `_OOW +1`, the host never seeing them. Found on the way: until this build the association carried no window and no bit, so an offloaded inbound association had no replay check at all (build `fbb844c5`, working tree).
   - Next: IPv6 outer and inner; the tunnel's throughput on a quiet bench (GCM and CBC); and the WAN result's cause (an active microflow?) re-measured.
6. **Jumbo.** The fast path says `MTU too big, limited by current mbuf size` at start-up here; the
   LIF's MTU and the GIU buffer size decide what this family carries, measured after the flows.

## What is not done

- Nothing is sent to the coprocessor on a guess: every payload comes from the module's own
  structures and is read back before the next step.
- `ipsec.flows 2` is never set on this family either; the clear-text exposure measured on the
  3300 holds until measured otherwise here.
- The XGS 3300 is not the bench for any of this: the 136 is, with its console open and the
  coprocessor's counters read before and after every step.
- Nothing above is committed until it is measured on the 136 and the owner says so.
