<!--
SPDX-License-Identifier: BSD-2-Clause
Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
-->

# Suricata's verdict, the application and pf's tag in the hardware flow (hw-08): a design

Status: a design, not code. Written 2026-11-14 from the drivers' source; nothing here has been run on
an appliance. The part that needed no driver change is built: Suricata's IPS drops are cut in hardware
by `scripts/xgs/ja4_cut.py` (XGS: Settings "Cut what the IPS drops", `general.cut_ips_drops`), through
the verdict the drivers already set. What is left is a driver change on both families, and it is
written here rather than made, because it changes what both drivers keep for every connection and has
to be built and measured on the appliances.

## What a flow carries today

Both drivers ask pf for a connection's state when the coprocessor punts its first frame
(`contrib/octep/octep_pf.c`, `octep_pf_copy`; `contrib/npuep/npupf.c`), and keep what they need of it
in their own record of the connection. What they copy is the addressing and the TCP tracking - the
direction, the timeout, both halves' states and sequence windows, the interface, both of pf's keys and
the translation between them (`struct octep_pf_state`, `contrib/octep/octep.h`), and on the ARMADA
whether the rule is sloppy. **Neither copies pf's tag (`pf_kstate.tag`), its rule, or anything of the
Intrusion Detection's.**

What the coprocessor's own connection entry can hold, as far as it has been read:

| field | width | what it is | how it is set |
|---|---|---|---|
| verdict | 2 bits | FWD, DROP, SEND_TO_IPS; the far side refuses a value above 2 | `RPC_CMD_CONN_MODIFY_FP_VERDICT` (13) in place; `dp.flow_verdict` / `flow.verdict` (discovery 3) |
| live user | 16 bits, the entry's +8 | a user's number in the table of live users (discovery 5) | `CONN_MODIFY_FP`, bit 0 of its field mask |
| QoS | two words | the shaper's | `CONN_MODIFY_FP`, bits 1 and 2 |
| DSCP override | 8 bits and an enable bit, per microflow | pf's DSCP mark (`dp.dscp_pf`) | at the flow's creation |
| `session_id` | in `usfp_conn_entry` | not known: never seen set or read | - |

So Suricata's verdict already has its field: DROP is the verdict "Cut in hardware" sets, and it is
what the new drop cut sets. There is no field for an application or for pf's tag that this project
knows the coprocessor to keep or act on; `session_id` is not one to use until it has been read on a
running coprocessor.

## The change

The application and the tag are kept with the host's record of the connection, not in the coprocessor:
nothing on the coprocessor would act on them, and what acts - the drop cut, the block lists, a page -
runs on the host.

1. **octep** (`octep_pf.c`, `octep.h`): `struct octep_pf_state` gains `uint16_t tag`, copied in
   `octep_pf_copy` from `s->tag` beside `s->direction` - under the same state lock, for the same state.
   The connection's record (`struct octep_conn`, beside `dropped` and `ips`) gains `uint16_t pf_tag`
   and `uint16_t app`, both 0 when unknown. `dp.flow_table` prints `tag N` (pf's tag name is not in
   the kernel's state; `pfctl -s Tags` names it, so the number is enough) and `app N`.
2. **npuep** (`npupf.c`, `npuflow.h`): the same two fields in its state copy (`struct npuep_pf_state`)
   and its flow record, the same words in `flow.flows`.
3. **Both**: a sysctl to set the application on a connection in place, `dp.flow_app` / `flow.app`,
   written `"<index> <revision> <app>"` as `dp.flow_verdict` is - so the revision guards against a slot
   reused by another connection. The application is Suricata's `app_proto` (eve `flow` and `alert`
   events), given a small number by `scripts/xgs/flows.py` from a fixed list (http, tls, dns, ssh,
   smtp, quic, ...); nothing on the coprocessor is told.
4. `scripts/xgs/ja4_cut.py` sets the application on a connection it reads in the log, and
   XGS: Offloaded Flows shows the tag and the application as columns. A cut by application (a list of
   them in XGS: Settings, cut as the fingerprints are) becomes possible once this is in.

What it costs: two 16-bit fields a connection on the host, one more line in each table, and the tag
read from a state the driver already holds locked. No new command to the coprocessor.

## The test

On the second XGS 3300 and the XGS 136, with offloading and the flow maker on:

- A rule with `tag web` passing TCP 443, a connection through it: `sysctl dev.octep.0.dp.flow_table`
  (`dev.npuep.0.flow.flows` on the 136) shows the connection with `tag` equal to the number
  `pfctl -s Tags` gives `web`; a connection through a rule without a tag shows `tag 0`.
- The new sysctl of step 3 written `"<index> <revision> 2"`, then the table: `app 2` on that connection;
  the same write with a wrong revision refused and nothing changed.
- The cut: with "Cut what the IPS drops" and "Cut in hardware" on and the Intrusion Detection in IPS
  mode, a rule that drops a test string, the string sent on a connection already in hardware: the XGS
  log says it cut, `dp.flow_table` shows the connection's verdict DROP, and its frames stop at the
  coprocessor.
- A soak of an hour with both fields set on every connection: the drivers' counters and the flow
  count as without them.
