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
| 5 | `RPC_CMD_PPORT_UPDATE` | binds a port tag to an interface, both ways - see below |
| 6, 7 | `RPC_CMD_NHOP_PROGRAM`, `RPC_CMD_NHOP_UPDATE` |
| 8, 9 | `RPC_CMD_MFLOW_PROGRAM`, `RPC_CMD_MFLOW_INVALIDATE` |
| 10-17 | the flow and connection commands |
| 18, 19 | `RPC_CMD_ADD_LIVE_UID`, `RPC_CMD_DELETE_LIVE_UID` |
| 20-26 | the QoS commands |
| 27-29 | the DoS commands |
| 30-35 | the IPsec SA commands - **named and specified below** |
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

## Every handler has the same signature, and that fixes the payload rules

The handlers in each `*_rpc.c` are registered with `rpc_cmd_cb_reg(u8 cmd, rpc_cmd_cb cb, void *ctx)`,
and `rpc_cmd_cb` is:

    int (*)(void *ctx, void *data, uint16_t *payload_len, uint16_t max_len)

`data` is the payload area, which is the command buffer past its eight-byte header. `payload_len` is
in **and** out - the request's length on the way in, the response's on the way out - so a command
answers into the same buffer it arrived in. `max_len` is the `resp_buff_sz` the host declared.

Every one of them is that shape: `lif_add_update`, `lif_delete`, `lif_read`, `fp_sys_cnt_read`,
`fp_port_cnt_read`, `fp_dbg_cnt_read`, `fp_df_cnt_read` and their four `_clr` counterparts. The
counter reads funnel into one `read_common(rpc_h, cntr_type, data, payload_len, max_len)`, where
the counter type comes from **which command it was** rather than from the payload. The payload itself
is a range, and its shape is settled one level down:

    int cntrs_fpop_read(struct cntrs_fpop_handle *fpop_h, int cntr_type,
                        struct usfp_fpop_req_table_read *req,
                        void *out_data, unsigned int out_data_len);

    struct usfp_fpop_req_table_read {   /* 12 bytes */
      +0    uint32_t  s_index          first index wanted
      +4    uint16_t  num_entries
      +6    uint16_t  flags            the I / C / D options usfp_table_print.sh passes
      +8    uint32_t  e_index          last index wanted
    }

which is `usfp_table_print.sh`'s own interface - `idx`, `start-`, `start-end`, and the `I`, `C` and `D`
options - written as a structure.

**The answer has no header of its own.** `out_data` takes the raw entries and
`rpc_resp_buf_desc.payload_len` says how many bytes came back. There is no
`usfp_fpop_resp_table_read` anywhere in the module, and the `usfp_fpop_req_*` / `usfp_fpop_resp_*`
pairs that do exist - for the IPsec SA operations, for a connection reclaim - show that a response
structure is named when there is one.

## The one write that matters: `RPC_CMD_LIF_ADD_UPDATE`

Its payload is `struct usfp_lif_config`, 16 bytes:

    struct usfp_lif_config {   /* 16 bytes */
      +0    struct usfp_lif_index    index      /* 4 */
      +4    struct fp_lif_info       lif_info   /* 10 */
      +14   struct usfp_lif_fp_priv  fp_priv    /* 2 */
    }

    struct usfp_lif_index {   /* 4 bytes, one u32 */
      vlan_id  : 12      bits 0-11
      iface_id : 7       bits 12-18
      reserved : 13
    }

    struct fp_lif_info {   /* 10 bytes */
      +0    struct eth_addr  my_mac        uint8_t mac_addr[6]
      +6    uint16_t         mtu
      +8    uint16_t         fwd_mode          : 2      bits 0-1
      +8    uint16_t         admin_disabled    : 1      bit 2
      +8    uint16_t         offload_disabled  : 1      bit 3
      +8    uint16_t         rep_pid_mlb       : 12     bits 4-15
    }

    struct usfp_lif_fp_priv {   /* 2 bytes */
      +0    uint8_t  update_mask         the LIF_M_* bits
      +1    uint8_t  df_enabled : 1
    }

**`usfp_lif_index` is a fourth independent confirmation of the addressing.** Its `vlan_id` occupies
bits 0-11 and `iface_id` bits 12 upward, which is `IFACE_VLAN_2_LIF_IDX(iface_id, vlan) =
(iface_id << 12) | vlan` written as a bitfield, and `LIF_IFACE_ID_SHIFT` is 12. The seven bits for
`iface_id` also agree with `PLATFORM_MAX_IFACES` 128.

And `update_mask` runs in the same order as the fields it guards - `LIF_M_MAC` 0x0001 for `my_mac`,
`LIF_M_MTU` 0x0002 for `mtu`, then `LIF_M_FWD`, `LIF_M_ADMIN_DISABLED`, `LIF_M_OFFLOAD_DISABLED`,
`LIF_M_REPPID` for the four bitfields in order.

## So a command, end to end

A counter read, which changes nothing:

    buffer, in host memory, DMA-mapped:
      +0   u16  resp_buff_sz  = however much room is given for the answer
      +2   u8   reserved      = 0
      +3   u8   cmd           = 44     RPC_CMD_LO_WORKER_SYS_CNT_READ
      +4   u32  unused        = 0
      +8   struct usfp_fpop_req_table_read, 12 bytes: s_index, num_entries, flags, e_index

    descriptor, in the rpc window at RPC_CMD_BAR_DESC_OFFSET_Q(ring, ring->posted):
      +0   u64  dma_buff_addr = that buffer
      +8   u16  payload_len   = 12
      +10  u16  flags         = RPC_DESC_POST_FLAG
      +12  u32  reserved      = 0

    then ring->posted++ and the doorbell named in ring->r_cfg.dbell

    the answer, written back over the same buffer:
      +0   u16  rc                RPC_RC_ERRNO_BIT (1<<15) marks an errno
      +2   u8   descriptor_done
      +4   u16  magic_seed
      +6   u16  payload_len       how many counter bytes follow
      +8   the counters

and the LIF install, which does not:

      +3   u8   cmd           = 3      RPC_CMD_LIF_ADD_UPDATE
      +8   struct usfp_lif_config, with offload_disabled set and update_mask covering it
           payload_len = 16

**One reading here is not certain**: whether `rpc_cmd_bar_desc.payload_len` counts the eight-byte
header or only the payload. It is taken to be the payload alone, because the callback receives `data`
already past the header together with `payload_len`, and `rpc_cmd_put(ctx, host_dma_buf, payload_len,
done, rc)` passes the same quantity back. That is a reading of two signatures, not a measurement.

## The configuration handshake, from the code

This needed the one thing DWARF does not give: control flow. It was read from a disassembly of the
same module, with objdump resolving the call targets from the module's own relocations, so every
function named below is the module's own name for it.

### What triggers a reconfiguration

`irq_handler`, the doorbell handler, opens with a three-instruction test:

    ae0:  ldr  x1, [x19, #4080]     the host-visible struct rpc_state
    ae4:  ldr  x3, [x19, #4088]     the target's CACHED copy of the cfg word
    ae8:  ldr  x2, [x1]             the LIVE cfg word - rpc_state +0, the whole u64 union
    aec:  cmp  x3, x2
    af0:  b.eq c18                  unchanged: go and process rings

**The trigger is one 64-bit word.** The union at the head of `rpc_state` - `cfg_magic`,
`cfg_revision`, `active_hi_rings`, `reconfig_done` - is read as a single `u64` and compared against a
cached copy. Any difference takes the handler down the reconfiguration path, which ends in
`queue_work_on` for `refresh_cfg`.

**That fixes the host's ordering, and it is the ordinary publish-last rule.** Nothing but the cfg word
is compared, so each ring's `ring_offset`, `desc_offset`, `desc_count` and `r_cfg` must be in place
**before** the cfg word is written, and the cfg word must be written as one store.

### What the target then does, in order

`refresh_cfg`, by its call sequence:

| | |
|---|---|
| `printk` | announces the reconfiguration |
| `__ll_sc_atomic_add`, then a spin on a bit | per ring: takes a reference and waits for that ring to go quiet |
| `cancel_work_sync` | stops the low-priority worker |
| `rpc_free_hi_prio_buffers` | releases the old high-priority buffers |
| `ldrb w0, [x0, #6]` | **reads `active_hi_rings` out of the live state** and takes it as the new ring count |
| `mv_pci_get_dma_dev_count`, `mv_pci_get_dma_dev` | per ring, chosen from that ring's `r_cfg.dbell` |
| `dma_alloc_from_dev_coherent` | allocates that ring's buffers, on the target side |
| `mv_free_dbell_irq` / `mv_request_dbell_irq` | releases and re-requests the doorbell interrupt, on the path a ring takes when its `r_cfg.shared` is clear |
| `__ll_sc_atomic_sub` | drops the reference taken at the start |
| `queue_work_on` | restarts the low-priority worker |
| `rpc_handler_enable` | `mv_dbell_enable(facility, dbell)` for the low ring and each high ring, unwinding with `mv_dbell_disable` if one fails |

and then, only if `rpc_handler_enable` returned zero:

    2598:  ldr  x0, [x20]
    259c:  ldr  x1, [x0, #4080]
    25a0:  strb w23, [x1, #7]        reconfig_done = 1, written into the host's window
    25a4:  ldr  x1, [x0, #4080]
    25a8:  ldr  x1, [x1]             re-read the live cfg word
    25ac:  str  x1, [x0, #4088]      and cache it
    25c0:  ret

**`reconfig_done` is written by the target and never by the host.** It is the acknowledgement that a
configuration was applied, and the host polls it. `rpc_handler_init` sets it to 1 at module load and
seeds the cache in the same breath, which is why a freshly booted coprocessor with nothing configured
reads `cfg_magic 0` with `reconfig_done 1` - not "waiting for something" but "up, nothing pending".

### A confirmation of the layout, from the code rather than the debug information

`rpc_handler_init` does:

    29ec:  str  x0, [x25, #4080]     remember where the state is
    29f0:  strb w3, [x0, #7]         reconfig_done = 1
    29fc:  ldr  x4, [x3], #72        read the cfg word, then advance the pointer by 72
    2a00:  str  x4, [x25, #4088]     cache the cfg word
    2a04:  str  x3, [x25, #4192]     remember where ring_lo is

The post-increment is **72**, which is `offsetof(struct rpc_state, ring_lo)` exactly as DWARF gives
it. The disassembly and the debug information agree, and neither was used to derive the other.

### One thing worth flagging in the vendor's code

`refresh_cfg` writes `reconfig_done` and *then* re-reads the live cfg word to cache it. A cfg word the
host wrote while the refresh was running would be cached as applied when it was not, and the next
doorbell would find no difference. The window is narrow and it is the vendor's to worry about, but a
host implementation should not lean on back-to-back reconfigurations: wait for `reconfig_done` before
writing a new cfg word.

## Run on the appliance

`contrib/octep/octep_rpc.c` is the client, and it issues only the commands that read. It was run on
the XGS 3300 on 2026-09-28, and what follows is what came back rather than what was expected.

### The configuration is accepted in seven milliseconds

    octep0: rpc: wrote cfg 0x00000001d7d3ab00; ring_lo descs 8 at window+0x1000, dbell 0, shared 0
    octep0: rpc: the target acknowledged after 7 ms; cfg now 0x01000001d7d3ab00

and the window then reads back:

    cfg 0x01000001d7d3ab00
      magic 0xd7d3ab00 (as expected)  revision 1  active_hi_rings 0  reconfig_done 1
    ring_lo
      posted 0  done 0
      ring_offset 0x0  desc_offset 0x1000  desc_count 8
      r_cfg 0x00000000: ring_num 0  facility 0  dbell 0  shared 0

So the publish-last ordering is right, doorbell index 0 is right, and `shared` clear is right. The
target cleared nothing and acknowledged everything: `reconfig_done` was zeroed by the host with the
cfg word and came back as 1.

### `RPC_DESC_POST_FLAG` means posted, and a command that wants an answer must not set it

This cost two runs and is the one thing the names got wrong on first reading. All three combinations,
same command, same buffer poisoned to `0x5a` beforehand:

| descriptor flags | bytes in the buffer that are no longer poison |
|---|---|
| 1, `POST` | 20 - only the request this driver wrote |
| **2, `NO_AGG_DMA`** | **24 - the request, plus a response header and a done magic** |
| 3, both | 20 - as with 1 |

`POST` is posted in the PCIe sense: fire and forget, no completion. The target consumes the
descriptor and advances `done` and writes nothing back. Leave it clear and the answer arrives.

### The response, laid out by measurement

Asking for four entries returned 32 bytes; asking for two hundred returned 1600. So an entry is a
64-bit word, `num_entries` in the request decides how many, and the reply is:

    +0                  struct rpc_resp_buf_desc, 8 bytes
    +8                  payload, payload_len bytes
    +8 + payload_len    the four-byte done magic

which is `RPC_DONE_MAGIC_SIZE` where the module says it should be. `magic_seed` in the header
increments by one per command - 2, 3, 4, 5 across four commands - and `rc` is 0 on success, with
`RPC_RC_ERRNO_BIT` set when it is an errno.

### `RPC_CMD_PLATFORM_READ`, checked against the board's own firmware

232 bytes came back. Decoded, against what `usfp_table_print.sh platform_info` printed on this same
board while SFOS was running it:

| offset | what came back over RPC | what SFOS printed |
|---|---|---|
| 0 | `XGS_1US` | `Platform Name : XGS_1US` |
| 64 | `UNKNOWN_VERSION` | `Version : UNKNOWN_VERSION` |
| 128 | `AMDA0202-0004` | `Assembly Partno : AMDA0202-0004` |
| 192 | `02`, `14`, `80`, `01`, `0a` | ID 2, Proc_cores 20, Max interfaces 128, Num PFs 1, Mflow timeout 10 |
| 200 | `05`, `0x1e8480` | Conn not usable timeout 5, Max conn entries 2000000 |
| 208 | `0x10000`, `0x10000` | Max nhop entries 65536, Max firewall rule IDs 65536 |
| 224 | `0x3d0eaa` | Num mflows 4001450 |

**Field for field.** That is the check this whole reading needed: the same numbers, by a channel this
project wrote from a binary, against a capture taken from the vendor's own firmware months of work
earlier.

### The counters, which are the point

`RPC_CMD_LO_WORKER_SYS_CNT_READ` with two hundred entries returns 1600 bytes, `rc` 0, and **every
counter zero**.

That is the right answer and it is worth saying why. Under the vendor's firmware the same array had
`FPCNTR_RX_WIRE` at 61,888 and `FPCNTR_TX_KN` at 61,874. Here the fast path is forwarding nothing,
which is what a fast path with no LIF does, and the counters say so. The instrument now exists and
reads zero for a reason this project understands.

## What is not here

**Whether `cfg_magic` is validated, and whether `cfg_revision` has to move.** The comparison is on
the whole 64-bit word, so any change at all triggers a refresh and the revision is not needed to make
that happen. `refresh_cfg` was read end to end and never tests `cfg_magic` against
`RPC_STATE_CFG_MAGIC`, so either something above it does or the magic is there for whoever reads a
memory dump. Writing it is free either way.

**Where the buffers come from.** `rpc_alloc_lo_prio_buffers` and `rpc_alloc_hi_prio_buffers` are on
the target, and `struct ring_context` holds `buf_dma_addr` and `prefetch_dma_base` - so the target
allocates its own side. Whether the host's `dma_buff_addr` is read by the target's DMA engine or
prefetched into those buffers changes nothing about the descriptor, but it does decide whether a host
buffer has to stay mapped after the doorbell.

**Nothing has been sent.** Every line above is read from a binary. The first thing to send should be
a counter read, because it changes nothing and its answer is checkable against the numbers already
captured from this board under the vendor's firmware.

Neither gap needs hardware to close. Both are in the same module.

## `PPORT_UPDATE` binds a port tag to an interface, in both directions

This one is worth its own section because it is the join between the two halves of the datapath, and
it is four bytes:

```c
struct usfp_fpop_req_update_pport {     /* 4 bytes */
  +0 u8  iface_id;
  +1 u8  rsvd;
  +2 u16 pport_tag;
};
```

The handler keeps **two** tables, and their names say what each is for:

```c
struct pport_fpop_handle {
  +0  struct ushmem_entry *iface2pport_shmem;
  +8  struct ushmem_entry *pport2iface_shmem;
  +16 u8  *pport2iface_tbl;      /* one byte per port tag  */
  +24 u16 *iface2pport_tbl;      /* one u16 per interface   */
  +32 unsigned int max_ifaces;
};
```

So a single four-byte command populates both directions of the map: `iface2pport` is what the
from-host path needs to turn an interface into a port tag, and `pport2iface` is what the from-wire
path needs to turn an arriving frame's port into the interface whose LIF it should be matched
against. The LIF table is indexed `iface_id << 12 | vlan`, which the vendor's own dump shows
directly - LIF IDs 0, 4096 and 8192 for interfaces 0, 1 and 2 - so the interface id from this table
is what reaches the LIF lookup.

Both tables live in shared memory (`ushmem_entry`), which is how the fast path's polling workers see
an update without an interrupt.

This driver has never sent this command. On the evidence it does not have to for the outbound
direction to work, and the inbound direction resolves a LIF as far as
`FPCNTR_FROM_WIRE_TO_KN_LIF_OFFLOAD_DISABLED`, so whatever default is in place is enough to be
matched. It is recorded here because it is the structure that joins a port to an interface, and any
attempt to give this driver more than one port will need it.


## The crypto engine, specified

The coprocessor's crypto units are the reason this chip exists, and nothing in this project had used
them deliberately. One thing had used them by accident: early on, every frame this driver posted was
routed into IPsec encryption and dropped for want of a security association, because a filler byte
in the metadata happened to ask for it.

So the engine answers. What was missing was the association. This section is that, read out of
`usfp_rh.ko`'s DWARF and its own disassembly - the vendor's names, offsets and values, not inferred
shapes.

### The whole command enum, by value

`enum rpc_cmd_type`, which replaces the partial table above:

```
 0 FW_STATE_REV_SET          18 ADD_LIVE_UID             36 PLATFORM_READ
 1 FW_L3_FWD_STATE_REV_SET   19 DELETE_LIVE_UID          37 LO_LIF_READ
 2 FW_CFG_PARAMS_SET         20 QOS_ADD_METER            38 LO_CONN_READ
 3 LIF_ADD_UPDATE            21 QOS_REMOVE_METER         39 LO_NHOP_READ
 4 LIF_DELETE                22 QOS_QUERY_METER          40 LO_MFLOW_READ
 5 PPORT_UPDATE              23 QOS_SET_SHAPER_RATE      41 LO_LUID_READ
 6 NHOP_PROGRAM              24 QOS_GET_SHAPER_RATE      42 LO_SA_READ
 7 NHOP_UPDATE               25 QOS_SET_DWRR_WEIGHT      43 LO_WORKER_DBG_CNT_READ
 8 MFLOW_PROGRAM             26 QOS_GET_DWRR_WEIGHT      44 LO_WORKER_SYS_CNT_READ
 9 MFLOW_INVALIDATE          27 DOS_SET_POLICY           45 LO_WORKER_PORT_CNT_READ
10 FLOW_CREATE_FP            28 DOS_ADD_BLACKLIST_ENTRY  46 LO_WORKER_DF_CNT_READ
11 CONN_CREATE_FP            29 DOS_CLEAR_BLACKLIST      47 LO_WORKER_DBG_CNT_CLR
12 CONN_MODIFY_FP            30 SA_ADD                   48 LO_WORKER_SYS_CNT_CLR
13 CONN_MODIFY_FP_VERDICT    31 SA_DEL                   49 LO_WORKER_PORT_CNT_CLR
14 CONN_TRACK_FP             32 SA_GET_STATS             50 LO_WORKER_DF_CNT_CLR
15 CONN_CFG_FP_TCP_SEQ_CHK   33 SA_REPLAY_UPDATE         51 MAX
16 CONN_RECLAIM_FP           34 SA_SEQ_UPDATE
17 CONN_GET_FP_TCP_STATE     35 SA_HOST_STAT_SYNC
```

**And which handler each SA command reaches**, read out of `ipsec_rpc_init`, which calls
`rpc_cmd_cb_reg(number, handler, ctx)` seven times:

| command | handler |
|---|---|
| 30 `SA_ADD` | `ipsec_add` |
| 31 `SA_DEL` | `ipsec_del` |
| 32 `SA_GET_STATS` | `ipsec_get_stats` |
| 33 `SA_REPLAY_UPDATE` | `ipsec_replay_update` |
| 34 `SA_SEQ_UPDATE` | `ipsec_seq_update` |
| 35 `SA_HOST_STAT_SYNC` | `ipsec_host_stat_sync` |
| 42 `LO_SA_READ` | `sa_read` |

### What SA_ADD carries

`struct usfp_fpop_req_sa_add`, 192 bytes, and the offsets here are from the start of the request
payload rather than from the inner structure:

```
  +0    saidx               uint32_t    the index this association takes, and the handle a frame names
  +4    lif_index           uint32_t    which logical interface it belongs to
  +8    cipher_key          __be32[8]   32 bytes
  +40   auth_key            __be32[16]  64 bytes
  +104  ctrl                uint32_t    bitfields, below
  +108  opt                 uint32_t    bitfields, below
  +112  win_size            uint32_t    the anti-replay window
  +116  spi                 __be32
  +120  sequence            uint64_t
  +128  ip_src              __be32[4]   tunnel source, four words so IPv6 fits
  +144  ip_dst              __be32[4]
  +160  nat_dport           __be16      UDP encapsulation, when opt.udp_enable is set
  +162  nat_sport           __be16
  +168  hard_lifetime_byte  uint64_t
  +176  hard_lifetime_pkt   uint64_t
  +184  rev_num             uint16_t
```

`ctrl`, from the low bit up: `hash` 4 bits, `cimode` 4, `cipher` 4, `mode` 2, `proto` 2, `dir` 1,
`ena_arw` 1, `ext_seq` 1, 12 spare, `valid` 1.

`opt`: 16 spare, then `inline_support` 1, `frag_check` 1, `bypass_DSCP` 1, `df_ctrl` 2, `ipv6` 1,
`udp_enable` 1.

The other two requests are small: `SA_DEL` takes `{ uint32_t saidx; unsigned int free; }` and
`SA_GET_STATS` takes a bare `saidx`.

### The algorithm numbers, which are the vendor's own enums

```
ipsec_sa_dir          0 ENCRYPT / OUTBOUND      1 DECRYPT / INBOUND
ipsec_sa_mode         0 TRANSPORT               1 TUNNEL
ipsec_sa_cipher       0 NULL   1 3DES   2 AES128   3 AES192   4 AES256
                      5 AES128_NULL   6 AES192_NULL   7 AES256_NULL   8 CHACHA20
ipsec_sa_cipher_mode  0 ECB    1 CBC    2 CFB      3 OFB      4 CTR
ipsec_sa_hash_type    0 NONE   1 MD5_96   2 SHA1_96   3 SHA256_96   4 SHA384_96
                      5 SHA512_96   6 MD5_128   7 SHA1_80   8 SHA256_128
                      9 SHA384_192  10 SHA512_256  11 GF128_128  12 GF128_96
                     13 POLY1305_128
```

So AES-GCM is `cipher` AES128/256 with `cimode` CTR and `hash` GF128_128, and the 64-byte `auth_key`
field is a union: `auth_key` for a separate authentication key, or `aead_salt` as a single 32-bit
word when the algorithm is combined.

### What the live half holds, and why it matters to a host

`struct usfp_ipsec_sa` is 1320 bytes: the 216-byte configuration above, then `usfp_ipsec_sa_live` -
`sequence`, `byte_count`, `packet_count`, **`host_byte_count` and `host_packet_count`**, the replay
window and its lock. Two of those names are the reason commands 33, 34 and 35 exist: the host and
the coprocessor each keep a sequence number and a byte count, and `SA_SEQ_UPDATE`,
`SA_REPLAY_UPDATE` and `SA_HOST_STAT_SYNC` are how they are reconciled. An offload that installs
associations and never reconciles them will fail a rekey.

### And the whole path is already instrumented

`tools/fpcntr-names.txt` indices 98 to 140 are the crypto path, and every failure has its own
counter: `CRYPTO_DROP_SA_UNAVAILABLE` 121, `CRYPTO_DROP_SADB_PRE_ERR` 125, `CRYPTO_DROP_REPLAY_OOW`
127, `CRYPTO_DROP_AUTH` 133, `CRYPTO_DROP_HW_ERR` 135. Both directions exist:
`FROM_WIRE_TO_IPSEC_DECR` 71 and then `FROM_IPSEC_DECR_TO_KERNEL` 115 or
**`FROM_IPSEC_DECR_TO_WIRE` 114**, which is the inline case - a frame decrypted and forwarded
without the host seeing it at all.

**Nothing in this section has been sent to a coprocessor.** It is a specification read from a
binary, and the bring-up that tests it is a separate step.

### What happened when it was

An association installs and reads back, and a frame asked to use one is still refused before any
association is consulted. Why, and what is left, is
[octeon-tx-crypto-path.md](octeon-tx-crypto-path.md): the index the engine uses lives in a
per-packet field written only from a flow, and a flow's identity is neither told to the host nor
findable by reading.
