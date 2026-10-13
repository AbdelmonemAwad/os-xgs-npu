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
[the hardware page](../hardware/xgs136.md) - [the port chain](../images/xgs136/port-chain.svg),
[the board map](../images/xgs136/board-map.svg), [the CN9130](../images/xgs136/cn9130.jpg),
[the switch](../images/xgs136/88e6193x.jpg) and [the quad PHY](../images/xgs136/aqr412c.jpg).

## The order of work

1. **Read the payloads before sending any.** `usfp_rh.ko` of this family, with its debugging
   information, gives every command's structure, field and offset; the 3300's `octep.h` gives the
   meaning. A table of the two side by side, on a page of its own, closes this step. Nothing is
   sent until the table exists.
2. **The gate.** `FW_CFG_PARAMS_SET` with the offload bit, read back, on the bench with the
   console open; then one interface entry with `offload_disabled` cleared and the counters
   watched. The 3300's finding that nothing creates a flow by itself is re-measured here before
   it is assumed.
3. **One microflow by hand.** `MFLOW_PROGRAM` for one connection the host already carries,
   `LO_MFLOW_READ` to confirm it, the frame counted in hardware, the state
   `MF_ACTIVE`, then `MFLOW_INVALIDATE`. The 3300's "a frame was accelerated" repeated here, with
   the same instrument reading the same counter names.
4. **The driver's flow table on `npuep`.** The 3300's `octep_pf.c` (pf as the oracle, the
   revision barrier on reload, the lease) and `octep_nhop.c` (next hops, the bridge's answer)
   moved into `npuep` behind the same sysctl names (`dp.auto`, `dp.flows`, `dp.accelerate`,
   `dp.fw_rev_bump`), the receive path's give-back handled the way the 3300's is, and the whole
   measured with the same three suites: the clear-text path at a gigabit, the translated path,
   and the trigger on and off for an hour each.
5. **IPsec, driven by OPNsense.** `SA_ADD`/`SA_DEL` through the kernel's own offload contract as
   on the 3300 (`if_ipsec_accel_methods`), the per-frame marker found, GCM first, then the same
   list: NAT-T, ESN, the replay window, CBC with HMAC, IPv6 outer and inner, measured in the
   same order and recorded the same way.
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
