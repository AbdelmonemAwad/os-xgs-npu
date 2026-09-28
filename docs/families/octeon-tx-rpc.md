# The RPC facility, and the LIF gate behind the return direction

This is the channel the host uses to program the OCTEON TX coprocessor's fast path: what lives in the
`rpc` window, how a command is posted, and the one table that decides whether a frame arriving at a
front port is ever handed to the host.

Everything here was read out of `opt/sophos/usfp/octtx/usfp_rh.ko` as shipped on the appliance -
13,487,888 bytes, taken from `npu-artifacts/rootfs.tar`, release
`rootfs-2026.0518-1247-1235-v22.0.Maint.060.Bali`, which is **the board's own build** rather than the
`v22.0.Maint.040.Luzon` source drop. The module carries full DWARF, including `.debug_macro`, so the
structures and the constants are the vendor's own names and values rather than anything inferred:
1,212 `.debug_macro` sections, `.debug_info`, `.debug_abbrev`, `.debug_str`.

Nothing in this page has been sent to a coprocessor. It is a specification read from a binary, and
the checks at the end are the reason to believe it.

## Why it matters

A frame that arrives from a **front port** passes five gates that can drop it outright:

    FPCNTR_FROM_WIRE_DROP_LIF_INDEX_INVALID
    FPCNTR_FROM_WIRE_DROP_LIF_LU_NULL
    FPCNTR_FROM_WIRE_DROP_LIF_INVALID
    FPCNTR_FROM_WIRE_DROP_LIF_ADMIN_DISABLED
    FPCNTR_FROM_WIRE_DROP_LIF_NOT_MY_MAC

A frame that arrives from the **host** passes none of them. Every `FPCNTR_FROM_WIRE_TO_KN_*` name is
a specific exception reason - `MFLOW_NOT_ACTIVE`, `NON_ACCEL`, `LIF_OFFLOAD_DISABLED`,
`CONN_INVALID` and so on - and there is no catch-all. So a received frame whose ingress port tag does
not resolve to a LIF is dropped before any transmit to the host is considered, and this driver's
`OUT_PKT_CNT == 0` is correct behaviour rather than a fault. See issue #64.

LIFs are installed over this facility. They cannot be installed over SDP, and NetAgent has no
operation that touches one.

## The window

`dev.octep.0.rpc`, measured on the appliance:

    size          1048576        (NPU_BARMAP_RPC_SIZE, 1 MB)
    dbell_start   155
    dbell_count   5

Five doorbells, against one for each of the other facilities, and the module says why:

    RPC_LO_RINGS_MAX   1
    RPC_HI_RINGS_MAX   4

One low-priority ring and four high-priority ones.

## `struct rpc_state` - 232 bytes, at the head of the window

    +0    u32              cfg_magic          RPC_STATE_CFG_MAGIC = 0xD7D3AB00
    +4    u16              cfg_revision
    +6    u8               active_hi_rings
    +7    u8               reconfig_done
    +8    u64              zero_pad[8]
    +72   struct rpc_ring  ring_lo
    +104  struct rpc_ring  rings[4]

`RPC_STATE_SIZE` is 232, which is exactly `104 + 4 * 32`.

Read on a freshly booted coprocessor, with the fast path running, the window holds one non-zero
word - `+0x0000 0x0100000000000000`, little-endian - which is `cfg_magic = 0`, `cfg_revision = 0`,
`active_hi_rings = 0`, **`reconfig_done = 1`**. Nothing is configured, and the far side is not
waiting on anything it has been asked for.

## `struct rpc_ring` - 32 bytes

    +0    u64                  posted
    +8    u64                  done
    +16   u32                  ring_offset
    +20   u32                  desc_offset
    +24   u32                  desc_count
    +28   union ring_hw_cfg    r_cfg

and the configuration word carries the doorbell number, which is how a ring is tied to one of the
five:

    union ring_hw_cfg {   /* 4 bytes */
      +0  u8  ring_num
      +1  u8  f_index        RPC_FACILITY_PRIM_IDX = 0, RPC_FACILITIES_MAX = 2
      +2  u8  dbell
      +3  u8  shared
    }

`posted` and `done` are free-running counters, not indices: the descriptor slot for a given count is

    RPC_CMD_BAR_DESC_OFFSET_Q(ring, counter) =
        (RPC_CMD_BAR_DESC_SIZE * (counter & (ring->desc_count - 1)) + ring->desc_offset) / 8

so `desc_count` is a power of two and the mask wraps the ring.

## `struct rpc_cmd_bar_desc` - 16 bytes, in the window

One of these per posted command, at the offset above:

    +0    u64  dma_buff_addr        host physical address of the command buffer
    +8    u16  payload_len
    +10   u16  flags                RPC_DESC_POST_FLAG 1, RPC_DESC_NO_AGG_DMA_FLAG 2
    +12   u32  reserved

`RPC_CMD_BAR_DESC_SIZE` is 16.

## The command buffer, in host memory

The descriptor points at a buffer whose head is:

    struct rpc_cmd_buf_desc {   /* 8 bytes, then the payload */
      +0    u16  resp_buff_sz
      +2    u8   reserved
      +3    u8   cmd                 <- enum rpc_cmd_type
      +4    u32  unused
      +8    u8   payload[]
    }

and whose answer is written back as:

    struct rpc_resp_buf_desc {   /* 8 bytes, then the payload */
      +0    u16  rc                  RPC_RC_ERRNO_BIT (1 << 15) marks an errno
      +2    u8   descriptor_done
      +3    u8   unused
      +4    u16  magic_seed          feeds rpc_generate_done_magic
      +6    u16  payload_len
    }

Sizes: `RPC_BUF_DESC_SIZE` 8, `RPC_DATA_MAX_SIZE` 4096, `RPC_DATA_RESERVED_SIZE` 32,
`RPC_USERDATA_MAX_SIZE` 4064, `RPC_DMA_FROM_HOST_LIMIT` 32768, `RPC_DONE_MAGIC_SIZE` 4,
`RPC_DONE_MAGIC_SEED_MASK` `U16_MAX`.

## `enum rpc_cmd_type` - 51 commands

The read-only ones are the place to start, because they change nothing on a live fast path:

| value | command | |
|---|---|---|
| 36 | `RPC_CMD_PLATFORM_READ` | what `usfp_table_print.sh platform_info` prints |
| 37 | `RPC_CMD_LO_LIF_READ` | the LIF table |
| 38 | `RPC_CMD_LO_CONN_READ` | |
| 39 | `RPC_CMD_LO_NHOP_READ` | |
| 40 | `RPC_CMD_LO_MFLOW_READ` | |
| 41 | `RPC_CMD_LO_LUID_READ` | |
| 42 | `RPC_CMD_LO_SA_READ` | |
| 43 | `RPC_CMD_LO_WORKER_DBG_CNT_READ` | |
| 44 | `RPC_CMD_LO_WORKER_SYS_CNT_READ` | the `FPCNTR_*` counters |
| 45 | `RPC_CMD_LO_WORKER_PORT_CNT_READ` | the per-DPDK-port counters |
| 46 | `RPC_CMD_LO_WORKER_DF_CNT_READ` | |
| 47-50 | the four matching `_CLR` commands | these **do** change state |

The writing ones, for later:

| value | command |
|---|---|
| 0-2 | `RPC_CMD_FW_STATE_REV_SET`, `RPC_CMD_FW_L3_FWD_STATE_REV_SET`, `RPC_CMD_FW_CFG_PARAMS_SET` |
| 3 | **`RPC_CMD_LIF_ADD_UPDATE`** |
| 4 | `RPC_CMD_LIF_DELETE` |
| 5 | `RPC_CMD_PPORT_UPDATE` |
| 6, 7 | `RPC_CMD_NHOP_PROGRAM`, `RPC_CMD_NHOP_UPDATE` |
| 8, 9 | `RPC_CMD_MFLOW_PROGRAM`, `RPC_CMD_MFLOW_INVALIDATE` |
| 10-17 | the flow and connection commands |
| 18, 19 | `RPC_CMD_ADD_LIVE_UID`, `RPC_CMD_DELETE_LIVE_UID` |
| 20-26 | the QoS commands |
| 27-29 | the DoS commands |
| 30-35 | the IPsec SA commands |
| 51 | `RPC_CMD_MAX` |

## The LIF, which is what the gate consults

    struct usfp_lif_entry {   /* 14 bytes */
      +0    struct eth_addr  my_mac
      +6    uint16_t         mtu
      +8    uint16_t         fwd_mode          : 2
      +8    uint16_t         admin_disabled    : 1
      +8    uint16_t         offload_disabled  : 1
      +8    uint16_t         rep_pid_mlb       : 12
      +10   uint16_t         df_enabled
      +12   uint16_t         reserved          : 15
    }

    LIF_FWD_MODE_ENTRY_INVALID 0x0   LIF_FWD_MODE_L2 0x1
    LIF_FWD_MODE_L3            0x2   LIF_FWD_MODE_BOTH 0x3

and the field mask an add-or-update carries, so a caller can set one field without the others:

    LIF_M_MAC 0x0001   LIF_M_MTU 0x0002   LIF_M_FWD 0x0004
    LIF_M_ADMIN_DISABLED 0x0008   LIF_M_OFFLOAD_DISABLED 0x0010   LIF_M_REPPID 0x0020

**`offload_disabled` is the bit that makes the fast path punt.** It is the difference between
`FPCNTR_FROM_WIRE_TO_KN_LIF_OFFLOAD_DISABLED` and a frame that stays on the coprocessor.

## How a port tag becomes a LIF

    usfp_pport2iface_table[tag]  ->  iface_id      65536 bytes, one u8 per tag
    IFACE_VLAN_2_LIF_IDX(iface_id, vlan) = (iface_id << 12) | vlan
    usfp_lif_table[lif_idx]      ->  struct usfp_lif_entry

with `LIF_IFACE_ID_SHIFT` 12, `PLATFORM_MAX_IFACES` 128, `PLATFORM_MAX_PPORT_VAL` 65535 and
`MAX_LIF_ENTRIES(max_ifaces) = max_ifaces * MAX_NUM_VLANS`. The reverse maps are
`usfp_iface2pport_table` and `usfp_lif_idx2tag_map`.

Every table the fast path publishes has a shared-memory symbol name, which is also how
`usfp_table_print.sh` reaches them: `usfp_lif_table`, `usfp_conn_table`, `usfp_sys_cntrs`,
`usfp_port_cntrs`, `usfp_dbg_cntrs`, `usfp_df_cntrs`, `usfp_fw_state`, `usfp_ipsec_sadb`, and a
`_clr_ring` beside each counter array.

## Why this reading should be believed

Three independent checks, none of which was used to derive it.

**The doorbell count.** The window advertises 5. The module defines `RPC_LO_RINGS_MAX 1` and
`RPC_HI_RINGS_MAX 4`, and `struct rpc_state` holds exactly one `ring_lo` and four `rings`.

**The structure size.** `RPC_STATE_SIZE` is a literal 232 in a macro, and the members add to 232.

**The LIF dump.** `usfp_table_print.sh lif`, captured from this board while SFOS was running it,
prints per entry: MAC, MTU, **Representor PID MLB**, **FWD mode**, **Admin disabled**, **Offload
disabled**, **DF enabled** - which is `struct usfp_lif_entry` field for field and in order. And its
entry numbering is `LIF ID: 0`, `4096`, `8192` for `iface_id` 0, 1 and 2, which is
`(iface_id << 12) | vlan` exactly.

## What is not here

**The payload of each command.** The eight-byte `rpc_cmd_buf_desc` header is known; what follows it
for, say, `RPC_CMD_LO_WORKER_SYS_CNT_READ` is not, and neither is how a counter array comes back -
the counters live in shared memory reached through `/dev/ushmem`, so a read command plausibly returns
a handle rather than the data, and that is a guess until it is read.

**The ring configuration sequence.** Who writes `cfg_magic`, in what order, and what `reconfig_done`
acknowledges. On this board the magic is zero and `reconfig_done` is 1, and which side set that has
not been established.

Neither gap needs hardware to close. Both are in the same module.
