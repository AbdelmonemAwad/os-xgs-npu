<!-- reviewed: 2026-10-14 -->
# The control channel's payloads on the ARMADA, beside the OCTEON TX's

Step 1 of [the offload plan](offload-plan.md), done 2026-10-14: before any command is sent to
the XGS 136's fast path, every payload is read out of this family's own target-side handler,
`usfp_rh.ko`, which the vendor built with full debugging information, and set beside what the
XGS 3300's driver learned for the same commands by measurement. Nothing on this page was sent to
the appliance; it is what the module says it expects.

## How it was read

`usfp_rh.ko` for the ARMADA (13.5 MB, 1,194 DWARF sections) was walked with pyelftools: every
`DW_TAG_structure_type` and `DW_TAG_union_type` whose name belongs to the channel, with each
member's offset, type, size and bit width, and the enumeration `rpc_cmd_type`. The repository's
`tools/dwarf-struct.py` prints any one of them by name; the whole set, 982 lines, is kept with
the appliance's local inventory and not here. The OCTEON TX side is `contrib/octep/octep.h`,
whose layouts were measured on the XGS 3300 one command at a time and are recorded in its
comments.

## The commands: the same names, the same numbers up to 45

~~`rpc_cmd_type` on the ARMADA runs from `RPC_CMD_FW_STATE_REV_SET = 0` to `RPC_CMD_MAX = 49`, and
every number is the number `octep.h` carries for the command of that name:~~

**Corrected 2026-10-27.** `rpc_cmd_type` in the ARMADA host module, which is vendor version 21's, runs
from `RPC_CMD_FW_STATE_REV_SET = 0` to `RPC_CMD_MAX = 49`: forty-nine commands. `octep.h` carries vendor
version 22's numbering, fifty-one commands, 0 to 50. The two agree on every number up to 45. From 46 on
they part: vendor version 21 has no direct-forwarding commands, so its three clears are 46 to 48, where
`octep.h` has `DF_CNT_READ` at 46 and the four clears at 47 to 50. Vendor version 23 adds
`SA_IF_MTU_SET` at 36 and moves every command from 36 on one higher. Both drivers place each command by
the version the coprocessor runs ([rpc-numbering.md](../families/rpc-numbering.md)). The table is
vendor version 21's:

| numbers | commands |
|---|---|
| 0, 1, 2 | `FW_STATE_REV_SET`, `FW_L3_FWD_STATE_REV_SET`, `FW_CFG_PARAMS_SET` |
| 3, 4, 5 | `LIF_ADD_UPDATE`, `LIF_DELETE`, `PPORT_UPDATE` |
| 6, 7 | `NHOP_PROGRAM`, `NHOP_UPDATE` |
| 8, 9, 10 | `MFLOW_PROGRAM`, `MFLOW_INVALIDATE`, `FLOW_CREATE_FP` |
| 11 to 17 | `CONN_CREATE_FP`, `CONN_MODIFY_FP`, `CONN_MODIFY_FP_VERDICT`, `CONN_TRACK_FP`, `CONN_CFG_FP_TCP_SEQ_CHK`, `CONN_RECLAIM_FP`, `CONN_GET_FP_TCP_STATE` |
| 18, 19 | `ADD_LIVE_UID`, `DELETE_LIVE_UID` |
| 20 to 26 | the `QOS_*` meter, shaper and DWRR commands |
| 27, 28, 29 | `DOS_SET_POLICY`, `DOS_ADD_BLACKLIST_ENTRY`, `DOS_CLEAR_BLACKLIST` |
| 30 to 35 | `SA_ADD`, `SA_DEL`, `SA_GET_STATS`, `SA_REPLAY_UPDATE`, `SA_SEQ_UPDATE`, `SA_HOST_STAT_SYNC` |
| 36 | `PLATFORM_READ` |
| 37 to 42 | `LO_LIF_READ`, `LO_CONN_READ`, `LO_NHOP_READ`, `LO_MFLOW_READ`, `LO_LUID_READ`, `LO_SA_READ` |
| 43 to 48 | the three `LO_WORKER_*_CNT_READ` and the three `*_CLR` |

## The requests: the same bytes

| command | ARMADA, from the module | OCTEON TX, from `octep.h` | the same? |
|---|---|---|---|
| `LIF_ADD_UPDATE` | `usfp_rpc_req_add_update_lif`, 18 B: `usfp_lif_config` 16 (index 4 = vlan_id:12, iface_id:7, reserved:13; my_mac 6; mtu 2; a 16-bit word of fwd_mode:2, admin_disabled:1, offload_disabled:1, rep_pid_mlb:12) then `update_mask` 2 | 18 B, the same fields in the same order ([rpc.md](rpc.md)) | yes |
| `PPORT_UPDATE` | `usfp_rpc_req_update_pport`, 4 B: iface_id 1, rsvd 1, pport_tag 2 | 4 B, the same | yes |
| `NHOP_PROGRAM` | `usfp_rpc_req_program_nhop`, 32 B: fp_index 4, then `usfp_nhop_entry` 28 (is_resolved 1, nhop_rev_number 1, reserved 2, `usfp_nhop_core_info` 24: dst_mac 6, src_mac 6, eth_type 2, vlan_idn 2, pport_tag:16, nhop_flags 1, iface_id 1, mtu 2, resv 2) | `OCTEP_NHOP_REQ_LEN` 32, the same entry | yes |
| `MFLOW_PROGRAM` | `usfp_rpc_req_program_mflow`, 32 B: mf_ident 4 (mflow_id:25, mflow_rev_num:6, mflow_valid:1); `usfp_mflow_entry_opr` 24 (opr_fl: sa_index:16, action:4, rsvd:3, dir:1, bridge_control:4, state:4; opr_bf: l3_fwd_rev_num:16, dscp_override_val:8, dscp_override_en:1, rsvd:7; conn_index 4; fw_state_rev_num 2; conn_rev_num 2; nhop_index:24, nhop_rev_num:8; sa_rev_num 2; rsvd 2); mflow_timeout 4 | `OCTEP_MFLOW_REQ_LEN` 32 with the same offsets, +0 to +28 | yes - and the comment in `octep.h` that calls it "twenty-eight bytes" is counting the fields before the timeout; the request is thirty-two, as its own define says |
| `MFLOW_INVALIDATE` | `usfp_invalidate_mflow`, 20 B: invalidation_type 4 - by interface `0x1`, by IPv4 source or destination `0x2`, `0x4`, by IPv6 source or destination `0x8`, `0x10`, by revision `0x10000`; no type by microflow - then the interface, address or revision it names, 16 | ~~the driver's own~~ never sent by `octep` | ~~to be compared when it is sent~~ this driver sent type 0, which the handler refuses, until 2026-10-18 ([the finding](../findings/a-takeout-took-nothing-out-on-the-armada.md)); it is not sent now |
| `CONN_RECLAIM_FP` | `usfp_fpop_req_conn_reclaim`, 8 B: conn_idx 4, fp_conn_rev 2, rsvd 2; the answer `usfp_fpop_resp_conn_reclaim`, the connection's TCP sequence state and both directions' counters | `OCTEP_CONN_RECLAIM_REQ_LEN` 8, the same | yes - this driver's takeout since 2026-10-18 |
| `FLOW_CREATE_FP` | `usfp_rpc_req_flow_create`, 180 B: `usfp_rpc_req_conn_create` 112 at +0, mflow_valid 4 at +112, mflow_o 32 at +116, mflow_r 32 at +148 | `OCTEP_FLOW_OFF_VALID` 112, `OCTEP_FLOW_OFF_MFLOW_O` 116, `OCTEP_FLOW_OFF_MFLOW_R` 148, posted in a 256-byte buffer | yes |
| `CONN_CREATE_FP` | `usfp_rpc_req_conn_create`, 112 B: conn_idx 4, then `usfp_conn_entry` 108 (atomic 8, session_id 4, qos[2] 8, `usfp_tcp_info` 60, `usfp_nat_info` 24 at +80, entry_lock 4) | 112 B; `OCTEP_CONN_OFF_NAT` 84 = 4 + 80, which `octep.h` computed from two sums and here is a member offset | yes - the computed NAT offset is confirmed by the module's own layout |
| `CONN_RECLAIM_FP` | `usfp_rpc_req_conn_reclaim` 8 B, reply `usfp_rpc_resp_conn_reclaim` 64 B | `OCTEP_CONN_RECLAIM_REQ_LEN` 8 | yes |
| `LO_*_READ` | `usfp_rpc_req_table_read`, 12 B: s_index 4, num_entries 2, flags 2, e_index 4 | `OCTEP_RPC_REQ_LEN` 12, the same | yes |
| the firewall state | `fw_state`, 8 B: fw_cfg 4, rev_num 2, l3_fwd_rev_num 2 | the configuration word whose bit 0 is `FW_CFG_OFFLOAD`, and the two revisions | yes |
| `SA_ADD` | `usfp_rpc_req_sa_add`, 192 B: saidx 4, lif_index 4, `usfp_rpc_sa_entry_opr` 184; `usfp_ipsec_sa_cfg` 216 B holds cipher_key[8], the overhead, the hard lifetimes, starting counts, ip_src[4], ip_dst[4], spi, nat_dport, nat_sport, win_size, rev_num, lif_index, seq_init, sa_creation_time, version | the driver's own, measured on the 3300 | ~~to be compared field by field before the IPsec step~~ compared and confirmed byte for byte ([the engine takes an association](../findings/the-engine-takes-an-association-on-the-armada.md)); 200 bytes on vendor version 23 (#760) |

The microflow table's entries are the same as well: `usfp_mflow_entry` 20 B (timestamp, mflow_timeout, flags.mflow_id, `usfp_mflow_state` = lbinfo, rev, fw_valid, host_valid, then hash_index), `usfp_mflow_key` 64 B (lif_id, the Ethernet triple, ip_family, ip_proto, the L4 ports or ICMP pair or SPI, the two addresses as v4 or v6, an 8-byte pad - the 64 bytes the CRC-32C flow hash runs over), `usfp_mflow_rpc_rd_data` 108 B for `LO_MFLOW_READ`, `usfp_lif_entry` 12 B for `LO_LIF_READ`, `usfp_nhop_entry` 28 B for `LO_NHOP_READ`.

## What this settles, and what it does not

The two families speak one protocol, byte for byte, for every command the XGS 3300's driver
sends today. The XGS 3300's `octep_rpc.c`, `octep_pf.c`, `octep_nhop.c` and `octep_ipsec.c`
therefore describe the ARMADA's control channel as well, and step 2 can send the gate with the
bytes the 3300 sends. What is not settled: the counter names and their order on this family
(the three `LO_WORKER_*_CNT_READ` replies are arrays in the fast path's own enumeration, read
from the OCTEON TX binary for that family and to be read from this one), the per-frame marker
in the GIU header that chooses encryption, and whether the fast path's own startup here clears
the same tables the 3300's does.
