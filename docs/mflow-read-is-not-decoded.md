# `LO_MFLOW_READ` is not decoded, and a claim made from it is withdrawn

Measured on the appliance, 2026-10-04, the same evening as the claim it withdraws.

## The claim

`#238` said that programming a microflow worked, and gave this as the evidence - the same slot read
with `LO_MFLOW_READ` before and after `MFLOW_PROGRAM`:

    word 10  0x000100850000013b  ->  0x010100850000013b
    word 11  0x000000008e77e723  ->  0x1e010000 8e77e723
    word 13  absent              ->  0x0100000200000000

and read word 11's high half as `opr_fl` built field for field, word 13 as the next hop, and word
10's top byte as `host_valid` moving from 0 to 1.

**That reading is withdrawn.** The decode was plausible and the numbers did match what had just
been written, which is exactly why it was believed.

## What disproves it

Three reads, each cheap, each taken after the claim was already published.

**Every index returns the same bytes.** Slots 957, 315, 190 and 60 - all of them reported by punted
frames - read identically to slots 1, 2, 500, 1500, 3000 and 7777, which no frame has ever named:
`mstate` is `0x00010083000003bd` in every one. A table where every entry is identical is not a
table being read.

**Programming a slot changes nothing, in any slot.** `MFLOW_PROGRAM` for index 200 with distinctive
values - action 5, `bridge_control` 3, state 9, next hop 7 revision 3 - left indices 100, 200 and
300 unchanged. Repeated with live traffic running, in case the write needed the fast path to pick
it up: unchanged again.

**The fields that did look right are not per-entry.** Reading 315 and 200 side by side, word 1 is
the index asked for - so something is honoured - but words 2, 3, 4 and 9 are byte-identical across
both, and words 2 and 3 held `nhop_index 7, revision 3` and `timeout 60`: the values of the **last
request**, not of either entry. Then, after another `MFLOW_PROGRAM`, those two words read as zero.
They are neither a stable echo of the request nor the contents of an entry.

## So what is known

- `MFLOW_PROGRAM` is accepted, with `rc 0x0000`. **That is the transport's answer and not the
  operation's**, which is the second time today that distinction has mattered.
- `LO_MFLOW_READ` returns a reply whose word 1 is the index requested. The rest is **not decoded**.
- Whether any microflow entry has ever been written by this driver is **unknown**. The evidence
  offered for it does not support it.

Nothing about the next hop changes: `NHOP_PROGRAM` was verified against `LO_NHOP_READ` field by
field, including a bitfield packing that would have shifted every value had it been wrong, and that
read behaves (different indices, different contents).

## What would settle it

Decode `LO_MFLOW_READ` from the handler that serves it, in the coprocessor's `usfp_rh` source,
rather than from the shape of its output. The repository already holds that tree. Reading the reply
first and the table second is the order that was skipped here.

**Lesson.** A decode that matches the value you just wrote is the most persuasive kind of wrong
answer, because the coincidence it needs is the one you are least likely to test for. The check
that would have caught it costs one line: read a second index. Every instrument in this repository
that has lied has lied by returning something plausible for every input, and this project has now
written that sentence three times - for `ethtool` on a pport, for the sweep of a table whose
entries are 4096 apart, and for this.
