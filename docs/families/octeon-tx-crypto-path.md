# The crypto path, and why a host frame is refused

A security association can be installed on this coprocessor and read back - that much was settled
in [octeon-tx-rpc.md](octeon-tx-rpc.md#the-crypto-engine-specified). A frame asked to use one is
still refused, and this page is why. Everything here is read out of the appliance's own binaries or
measured on the appliance; where a reading was wrong it is left in, struck through, because the
wrong readings are the expensive part.

Issue [#185](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/185).

## The symptom, in counters

A frame with the metadata that asks for encryption moves exactly two counters:

```
FROM_KN_TO_IPSEC_ENCR      +N      the fast path accepted the request
CRYPTO_DROP_SADB_PRE_ERR   +N      and then refused the frame
CRYPTO_DROP_SA_UNAVAILABLE  0      without ever consulting an association
```

The third line is the one that matters. `SA_UNAVAILABLE` is the counter for "the association you
named is not there"; it does not move. The failure is earlier than that.

## The chain, function by function

From `usfp`'s own disassembly - 2,411 functions, the fast path that runs on the coprocessor:

```
pmode_hwevt_worker_loop
  -> crypto_pkt_submit.lto_priv.124     .text+0x4477d8
     -> sadb_hw_entry_get               .text+0x421f48
        on NULL -> .text+0x434940, which increments counter 125
```

**`crypto_pkt_submit` takes the association index out of the mbuf, not out of the packet:**

```
ldrb w6, [x21, #3]      the dynamic-field offset, registered at start-up
ldrh w1, [x19, x6]      x19 is the rte_mbuf - a 16-bit read at that offset
bl   sadb_hw_entry_get
```

**`sadb_hw_entry_get` wants four things, and the first explains the counter:**

```
saidx != 0                            else -22 EINVAL   <- the index is 1-based
saidx - 1 < [ctx + 0x1a0]             the table size
entry[+0x88] != 0                     the valid flag
entry[0] == other[idx*0x528 + 0xd0]   a revision match
```

Entry stride is `0x570`; a parallel table has stride `0x528`.

## The field, decoded from the code that writes it

The symbol is `usfp_ipsec_dynfield`. `worker_ordered` builds the 64-bit word at `.text+0x4256ec`
and stores it with `str x0, [x20, x4]`, where `x4` is the same registered offset:

| bits | source | meaning |
|---|---|---|
| 0..15 | flow entry `+0x26` (u16) | **the association index** |
| 16..23 | flow entry `+0x25` (u8) | |
| 24, 27 | constant `0x9000000` | flags |
| 32..47 | flow entry `+0x22` (u16) | a second index, passed on as submit's arg 5 |
| 48..63 | `[x9 + 0xb0]`, byte-swapped | |

Every source is a **flow entry**. `proc_from_ips` is the one path that does not look a flow up: at
`.text+0x420f40` it takes the whole word from its caller's metadata at `+8`. That is the IPS return
path, not the host path.

## What the host can and cannot say

**It cannot put the index in the metadata.** Measured, with an association really installed at
index 1 - `SA_ADD` answering `rc 0x0000` and `LO_SA_READ` returning its 1,328 bytes with 1 in the
first word - five isolated test frames gave `FROM_KN_TO_IPSEC_ENCR +5`,
`CRYPTO_DROP_SADB_PRE_ERR +5`, `CRYPTO_DROP_SA_UNAVAILABLE 0`. Unchanged, which is what the code
above says must happen: nothing on the host path writes the dynamic field, so it reads zero and the
lookup fails on `saidx != 0`.

**The coprocessor does not tell it which flow a frame belongs to.** `dp.rx_prefix` prints the whole
82-byte prefix the far side writes in front of every delivered frame:

```
+00  00 00 00 00 00 00 00 ac     length 172, big-endian
+08  00 00 00 00 00 00 03 80
+10  82 00                        the port tag
+12  a2 99 43 b4                  the vendor's signature
+16..+51                          sixty bytes of zero
```

A `usfp_mflow_ident` is four bytes. It is not in there.

## Programming a flow: the request is specified, the identifier is not available

`RPC_CMD_FLOW_CREATE_FP` is **10** and `RPC_CMD_MFLOW_PROGRAM` is **8**. Read with
`tools/dwarf-struct.py` from the appliance's own `usfp_rh.ko`, which runs on the coprocessor's Linux
and is where `rpc_cmd_cb_reg` lives:

```
struct usfp_fpop_req_flow_create {   /* 180 bytes */
  +0x00  conn (112)    conn_idx + usfp_conn_entry {atomic 8, session_id, qos 8, tcp 60, nat 24, lock 4}
  +0x70  mflow_valid   a TWO-BIT MASK: bit 0 programs the forward flow, bit 1 the reverse
  +0x74  mflow_o (32)  usfp_fpop_req_program_mflow
  +0x94  mflow_r (32)
}

struct usfp_fpop_req_program_mflow {   /* 32 bytes */
  +0x00  usfp_mflow_ident      mf_ident    /* mflow_id:25, mflow_rev_num:6, mflow_valid:1 */
  +0x04  usfp_mflow_entry_opr  mf_opr
  +0x1c  uint32_t              mflow_timeout
}

struct usfp_mflow_entry_opr {   /* 24 bytes - so sa_index is at the request's +0x04 */
  +0x00  opr_fl { sa_index:16, action:4, rsvd:3, dir:1, bridge_control:4, state:4 }
  +0x04  opr_bf { l3_fwd_rev_num:16, dscp_override_val:8, dscp_override_en:1, rsvd:7 }
  +0x08  conn_index   +0x0c fw_state_rev_num  +0x0e conn_rev_num
  +0x10  nhop_index:24, nhop_rev_num:8
  +0x14  sa_rev_num   +0x16 rsvd
}
```

`conn_fpop_flow_create` writes the connection straight into its slot -
`slot = [tbl+0x10] + conn_idx * 0x6c` - after taking the entry lock at `+0x68` and checking the
revision at `+0x6a`; a mismatch returns 7, an out-of-range index returns 1. `flow_create` refuses a
buffer shorter than `0xb3`, confirming the 180 bytes exactly.

`mflow_fpop_prog_both` indexes just as plainly - `entry = [[tbl+8]] + (ident & 0x1ffffff) * 0x14`,
bounds-checked against `[tbl+0x14]` - **and then refuses to do anything unless the entry is already
live**:

```
w5 = [entry + 0x0c]          mstate
tst w5, #0xff0000 ; b.eq     not live -> this direction is skipped silently
rev_req = ident >> 25 & 0x3f
rev_now = mstate >> 8 & 0x3f
cmp ; b.ne                   revision mismatch -> skipped
```

So a flow cannot be conjured. It is created by the fast path when it sees traffic - `mflow_alloc`,
2,196 bytes, takes a hash and a count - and the host may only **update** one it already knows the
identity of.

## And the identity cannot be found by reading

`RPC_CMD_LO_MFLOW_READ` is **40** and takes `usfp_fpop_req_table_read {s_index, num_entries, flags,
e_index}`. Its handler `mflow_read` refuses a response buffer of 11 bytes or less; the inner
`mflow_fpop_read` then computes what it needs as **116 bytes per entry**:

```
needed = num_entries * 116
if (resp_size < needed)        -> error       35 entries is the most that fits in 4,088
if (s_index + num_entries > table_size) -> error
if (num_entries != 0 && s_index > e_index) -> error
```

Asked for 37, it answers `rc 0x0001` with no payload - which is the arithmetic above and not a fault.
Asked for 35 within the limit it answers `rc 0x0000` with 116 bytes whose first word is **-35**, and
asked for 8 it answers **-8**: the first word is the negated request, which is this handler's way of
saying *none of the entries you asked for are live*.

The table holds **4,001,450** flows at hash positions. Thirty-five at a time is 114,000 round trips
to walk it, and the position is a hash of the 64-byte key, which the fast path computes with DPDK's
`rte_hash` - `rte_hash_crc_init_alg` is linked in, and `usfp_hash_cuckoo_make_space_mw` with it. A
`rte_hash` position comes out of its own key store, not out of the key.

## Where this leaves it

> An association can be installed. A frame can be made to ask for encryption. The index that joins
> the two lives in a per-packet field written only from a flow, and a flow's identity is neither
> told to the host, nor returned by any command, nor findable by reading the table.

One door is left: that the host computes the position the way the fast path does. That is a reading
of `rte_hash`'s key store as this build configures it, and it is the next thing to do. If it closes,
the honest statement is that IPsec offload cannot be driven from the host with what this appliance
publishes - and that is a result too, as long as it is said plainly rather than left as an open
"not yet".

## The measurement traps this cost

**`dp.meta_tpl` applies to every frame the interface path transmits, and not to the frame `dp.xmit`
posts.** A sweep that set a template and sent one test frame per offset was measuring the
appliance's own WAN traffic: one row moved `FROM_KN_TO_IPSEC_ENCR` by 11. The isolated instrument is
`dp.meta=0`, which puts the asking pattern in the test frame itself. Leaving a template set asks the
coprocessor to encrypt the appliance's live traffic, so set it, measure, and clear it again.

**The index is 1-based**, and an association installed at 0 is refused for that alone - which can
look exactly like the fault being investigated.
