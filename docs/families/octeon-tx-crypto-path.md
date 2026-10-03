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

**CORRECTED - the metadata layout was read out of the vendor's own source.** The claim below, that
the host cannot put the index in the metadata, is **wrong**. `SFOS_OSS-22.0.1_MR-1-490.iso` ships
`WARHOL_usfp.tar.gz`, 190 files of Sophos's own GPL source for `usfp_firewall`, `usfp_rh` and
`mv_pport`, and `include/metadata.h` in it names the fields:

```c
struct usfp_sp_md {            /* Metadata to USFP from slowpath/kernel */
    uint8_t  md_valid;                  /* +0  */
    uint8_t  sa_is_out;                 /* +1  IPsec offload direction is output (encrypt) */
    uint16_t _unused_0;
    uint32_t _unused_1;
    struct usfp_mflow_ident flow;       /* +8  */
    uint32_t sa_index;                  /* +12 IPsec offload SA index, 0 means no offload */
    struct usfp_dos_md dos;             /* +16 */
};
```

The two bytes this project found by bisection - 1 and 12 - are `sa_is_out` and `sa_index`, and the
index is a 32-bit field rather than the byte the driver was writing. So the index **is** carried in
the metadata, by the vendor as well as by us, and the paragraph below describes a dead end that was
entered because the archive on disk was never opened.

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

## The hash is CRC-32C over the whole key, seeded with zero

And it is not DPDK's `rte_hash` after all. `usfp` links its own cuckoo table -
`usfp_hash_cuckoo_make_space_mw` - and `rte_hash_crc_init_alg`, which only selects the CRC
implementation. The hash itself is eight instructions in `worker_ordered` at `.text+0x4241c8`:

```
mov     w27, w28                     the seed, and w28 is zero on this path
crc32cx w27, w27, [x29+0x158]        eight 64-bit words
crc32cx w27, w27, [x29+0x160]
crc32cx w27, w27, [x29+0x168]
crc32cx w27, w27, [x29+0x170]
crc32cx w27, w27, [x29+0x178]
crc32cx w27, w27, [x29+0x180]
crc32cx w27, w27, [x29+0x188]
crc32cx w27, w27, [x29+0x190]        0x158..0x197 is 64 bytes - sizeof(usfp_mflow_key)
lsr     w3, w27, #0x10               and the position is the top sixteen bits
```

`crc32cx` is the ARMv8 CRC-32C instruction, the Castagnoli polynomial, taking a 64-bit word at a
time. The seed is zero because the instruction two before this is `cbnz w28, ...` - this path runs
only when `w28` is zero - and `w27` is initialised from it.

**So the position is computable by a host**, from a key whose layout is already published above:

```
position = crc32c(0, key[0..63]) >> 16
```

Nothing about it needs the coprocessor's cooperation. A host that knows a five-tuple can work out
where the flow for it sits, read that position with command 40 to confirm the key matches, and then
program it with command 8 - which is the whole of what was missing.

## And then every hashed position was empty, which was the real answer

Sixteen candidate keys were built over the layout above - varying the MAC order, the port order and
the address order, since the published layout fixes the offsets but not which of a pair goes first -
for a five-tuple that was certainly live, and that `pfctl -ss` showed in both directions. Every one
of the sixteen positions answered empty, and so did indices 0 to 34.

The first word of a `LO_MFLOW_READ` reply is **`-(s_index + num_entries)`** - a read of two entries
at 16210 answers `-16212` - which corrects the earlier reading of it as "the negated request": it is
the exclusive end of the range asked for, and it means *nothing in this range is live*.

Sixteen empty positions is not sixteen wrong keys. **The table is empty.**

## The counters say why, and it is one bit

`FPCNTR_FROM_WIRE_TO_KN_FORCED` has been **exactly equal to `FPCNTR_RX_WIRE` since boot** - 4,474
for 4,474. Every frame this coprocessor has ever taken off the wire was pushed to the host. Over a
measured 25-second window of ordinary routed traffic:

```
RX_WIRE                         +60
FROM_WIRE_TO_KN_FORCED          +60      every one of them
FROM_WIRE_TO_KN_MFLOW_LU_ERR      0      and not one flow lookup was even attempted
```

The second counter array - `RPC_CMD_LO_WORKER_DBG_CNT_READ`, command **43**, the `PD_DEBUG_CNT_`
names - says the frames themselves were fine:

```
PD_DEBUG_CNT_IPV4_PKT           +55
PD_DEBUG_CNT_UDP_PKT            +55
PD_DEBUG_CNT_NA_NOT_IP           +2      the only "not accelerated" reasons, and they
PD_DEBUG_CNT_NA_IPV6_MCAST       +2      account for the other five frames
PD_DEBUG_CNT_NA_IPV6_EXT         +1
```

Fifty-five frames parsed as clean IPv4/UDP with no refusal recorded against them, and were pushed to
the host anyway. The whole punt vocabulary was checked, and every other reason stands at zero:
`LIF_OFFLOAD_DISABLED` 37, `LIF_UNKNOWN_VLAN_L3_MODE` 38, `NON_ACCEL` 40, `MFLOW_LU_ERR` 41, the
four `NHOP_` counters 44-47, `EG_ERR` 60. The LIF table agrees - `LO_LIF_READ` gives LIF 0 with
flags `0x0002`, so `fwd_mode` is L3 and **`offload_disabled` is clear**.

So it is not the interface. It is global, and `rpc_cmd_type` names it in its first three entries -
three commands this driver had never issued. They are not a prerequisite for the rest: nothing in
the coprocessor's modules reads `fw_state` except these three setters, and every other command this
driver issues was already working. What they gate is acceleration.

```
0  RPC_CMD_FW_STATE_REV_SET
1  RPC_CMD_FW_L3_FWD_STATE_REV_SET
2  RPC_CMD_FW_CFG_PARAMS_SET
```

They write the three fields of an eight-byte structure that lives in shared memory, which
`fw_state_fpop_init` looks up with `ushmem_lookup` and accepts only if its size is 8:

```
struct fw_state {   /* 8 bytes */
  +0x0  uint32_t fw_cfg
  +0x4  uint16_t rev_num
  +0x6  uint16_t l3_fwd_rev_num
}
```

And `fw_cfg`'s bits are the vendor's own macro definitions, read out of the module's debug
information rather than inferred:

| bit | name |
|---|---|
| `0x001` | **`FW_CFG_OFFLOAD`** |
| `0x002` | `FW_CFG_TCP_SEQ_CHK` |
| `0x004` | `FW_CFG_IPS` |
| `0x008` | `FW_CFG_FINTRACK` |
| `0x010` | `FW_CFG_FP_PKT_DUMP` |
| `0x020` | `FW_CFG_INJ_RECOVERY` |
| `0x800` | `FW_CFG_DROP_IF_IPS_OFF` |

`FW_CFG_DEFAULT` is defined there as `FW_CFG_TCP_SEQ_CHK` alone, and **a coprocessor that has just
started has offload off** - which is measured, not inferred from that macro: `FORCED` equals
`RX_WIRE` from the first frame. Nothing but the host can turn it on.

What the word holds *besides* that bit at start-up has not been read, and cannot be: `rpc_cmd_type`
has no command that returns `fw_state`, and the region it lives in belongs to the data-plane
application. The macro says `TCP_SEQ_CHK`; `fp_state_init` in `usfp.elf` allocates the region from
zeroed memory and nothing in `usfp_rh.ko` writes a default into it, so zero is at least as likely.
The consequence is practical and belongs in any code that touches this word: **a write replaces a
value nobody has seen**, so set every bit you want rather than the one you are changing.

That is the answer to this page's question. The crypto path was never refusing a frame on its own
account: the association index rides on a flow entry, a flow entry exists only where the fast path
accelerates, and the fast path accelerates nothing while bit 0 of `fw_cfg` is clear.

### The three requests, decoded

All three are a few instructions each, and the host side of them is now in the driver. Each one is
two functions: a `set_*` wrapper that validates the request, and an `fpop_*` callee that does the
store - which is worth keeping straight, because disassembling the wrapper and looking for the store
will not find it.

```
fw_state_set_cfg_params     0x352c  cmp w4,#3; b.ls -> refuse    request >= 4 bytes
                            0x353c  ldrh w1,[x1]                  a HALFWORD at offset 0
 -> fw_state_fpop_cfg_params_set
                            0x7e2c  str  w1,[x0]                  stored as the 32-bit fw_cfg

fw_state_set_rev            0x3670  cmp w4,#1; b.ls -> refuse    request >= 2 bytes
                            0x367c  ldrh w1,[x1]
 -> fw_state_fpop_rev_set
                            0x7df8  strh w1,[x2,#4]               rev_num
                            0x7e0c  bl mflow_fpop_invalidate_issue   AND THROWS AWAY EVERY FLOW

fw_state_set_l3_fwd_rev     0x35c8  cmp w4,#1; b.ls -> refuse    request >= 2 bytes
                            0x35d8  ldrh w1,[x1]
 -> fw_state_fpop_l3_fwd_rev_set
                            0x7e1c  strh w1,[x0,#6]               l3_fwd_rev_num, no invalidate
```

The invalidate on the firewall revision is worth noticing on its own: it is the mechanism by which a
ruleset reload discards hardware offload, and it is why the flow, connection and next-hop entries
carry revision numbers that `mflow_fpop_prog_both` refuses a mismatch on.

### Measured: the gate is the gate, and opening it changes one counter for another

Issued on the appliance, with the same traffic either side of it - a ping flood and an HTTP fetch,
so the frames arrive on the WAN front port and are genuinely from the wire. `FW_CFG_PARAMS_SET`
answered `rc 0x0000` both times.

| counter | `fw_cfg = 0x2` (control) | `fw_cfg = 0x3` |
|---|---|---|
| `RX_WIRE` | +65 | +101 |
| `TX_KN` - delivered to the host | **+65** | **+101** |
| `FROM_WIRE_TO_KN_FORCED` | **+65** | **0** |
| `FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE` | 0 | **+101** |

One bit moves every frame from *forced to the host without a lookup* to *looked up, no active flow,
punted to the host*. `TX_KN` equals `RX_WIRE` in both columns: **every frame still reached the
host**, none was dropped and none bypassed the packet filter, which went on inserting states
throughout at 29 searches a second.

That is the gate identified and the risk of opening it measured rather than argued. It is also the
next blocker named by the hardware itself: `MFLOW_NOT_ACTIVE`. The flow table is reached now, and
it is empty.

The appliance was left working - twelve ports up, the bridge intact, the default route on the WAN
port, no packet loss - and `fw_cfg` was set to `0x2` afterwards. Not *restored* to `0x2`: as the
section above says, the value before the first write was never read, and `0x2` is the macro's
default rather than an observed one. With `OFFLOAD` clear the other bits have nothing to act on, so
the two candidates are indistinguishable here; the honest record is that the word now holds `0x2`
because a write put it there.

### Which makes this a host-layer problem, as it had to be

With offload enabled the fast path looks for a flow; the table is empty, so it misses and punts, and
the host still sees every packet - which is the table above, measured. Nothing accelerates until a
flow is **created**, and a flow is
created by the host saying `FLOW_CREATE_FP` for a connection it has already decided to permit. On
this appliance the thing that decides is the host's own packet filter. So the remaining work is not
a coprocessor question at all:

1. `FW_CFG_PARAMS_SET` with `FW_CFG_OFFLOAD` - one halfword, and the gate is open.
2. `FW_STATE_REV_SET` on every ruleset reload, so stale flows are discarded.
3. A LIF per interface that forwards - exactly one exists today.
4. `FLOW_CREATE_FP` when the filter creates a state, `MFLOW_INVALIDATE` when it expires.
5. `MFLOW_PROGRAM` with `sa_index` for the flows matched to an IPsec policy - which is the original
   question on this page, and the last step rather than the first.

Opening the gate is a change with a security boundary in it, because an accelerated flow is a flow
the host's filter no longer sees. It belongs behind the host's own decision, never behind a sysctl
left set.

## Where this leaves it

> An association can be installed. A frame can be made to ask for encryption. The index that joins
> the two lives in a per-packet field written only from a flow, and a flow's identity is neither
> told to the host, nor returned by any command, nor findable by reading the table.

That last clause is no longer true twice over. The position **is** computable, and the reason no
read of one ever matched is that there was nothing anywhere to match: the flow table is empty
because `FW_CFG_OFFLOAD` has never been set, so the fast path has never accelerated a frame.

The question this page opened with is answered, and step 1 of the five is done: the driver issues
`FW_CFG_PARAMS_SET`, the gate has been opened and closed on the appliance, and the punt counter
moved from `FORCED` to `MFLOW_NOT_ACTIVE` with every frame still delivered to the host.

What is left is steps 2 to 5, and none of them is a coprocessor question. They are the host
deciding, which means they belong to the host's packet filter and not to a sysctl.

## Where it actually stands, with the source in hand

Every offset in this driver's `SA_ADD` request matches the vendor's `struct usfp_fpop_req_sa_add`
in `include/sa_table.h`, field for field - the keys, the SPI, the addresses, the lifetimes. One
field differed: `rev_num` at request offset 184, which this driver sent as a hard zero. The vendor
does not:

```c
lx->rev += 1;                        /* usfp_ipsec.c, before every install */
rev = lx->rev;                       /* so a fresh index carries 1, never 0 */
```

and `sa_table.h` annotates the field `uint16_t rev_num; /* rev in mflow */`, which is the fourth
and last check in `sadb_hw_entry_get`.

**It was tried, and it is not the answer.** With `rev_num = 1`, `md_valid = 1`, `sa_is_out = 1` and
`sa_index = 1` as a 32-bit field, against an association installed at index 1 and read back with
its valid bit set (`ctrl = 0x80001212`, bit 31), the counters are unchanged:

```
FROM_KN_TO_IPSEC_ENCR      +2
CRYPTO_DROP_SADB_PRE_ERR   +2
```

So the first three checks in `sadb_hw_entry_get` pass - the index is non-zero, within the table, and
the entry is valid - and either the revision is compared against something other than what
`SA_ADD` carries, or the index never reaches the dynamic field at all. The next reading is the
bound check the metadata-supplied index passes through before it is stored, which is where a
failure silently zeroes the field rather than reporting itself.

## The measurement traps this cost

**`dp.meta_tpl` applies to every frame the interface path transmits, and not to the frame `dp.xmit`
posts.** A sweep that set a template and sent one test frame per offset was measuring the
appliance's own WAN traffic: one row moved `FROM_KN_TO_IPSEC_ENCR` by 11. The isolated instrument is
`dp.meta=0`, which puts the asking pattern in the test frame itself. Leaving a template set asks the
coprocessor to encrypt the appliance's live traffic, so set it, measure, and clear it again.

**The index is 1-based**, and an association installed at 0 is refused for that alone - which can
look exactly like the fault being investigated.

**An absent cause is still a cause.** Every measurement on this page was taken against a fast path
that was not accelerating anything, and that was read for a long time as a fault in the crypto
stage. The counter that said otherwise - `FROM_WIRE_TO_KN_FORCED` standing exactly equal to
`RX_WIRE` - had been readable the whole time. What was missing was the comparison: a punt counter
that tracks the receive counter one for one is not a statistic, it is a mode. Checking whether one
counter equals another costs nothing, and it should have been the first question.

**One `FORCED` reading was over-generalised.** An earlier run found every frame accounted for by
`PD_DEBUG_CNT_NA_IPV4_BMCAST` and concluded that `FORCED` meant "broadcast cannot be offloaded". It
does cover that case, but it is the catch-all punt: 55 clean unicast frames with no `NA_` reason
against them are counted as `FORCED` too. `FPCNTR_FROM_WIRE_TO_KN_NON_ACCEL` is the counter for a
parse refusal, and it stays at zero.
