# `LO_MFLOW_READ`: a claim withdrawn, and the instrument decoded two days later

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

## Decoded, two days later, and the earlier reads were of empty slots

2026-10-06. **The instrument works.** The reason it looked like a non-table is that nothing had ever
written an entry at any index that was read, and a refused read leaves the previous reply in the
buffer.

### The two things that settle it

**A valid read returns content the host never sent.** `FLOW_CREATE_FP` carries no key - the fast path
makes the key itself from the packet - so the 64 bytes of key in the reply cannot be an echo of any
request. Two flows, read side by side, each returned its own:

| asked for | key says | the driver's table says |
|---|---|---|
| index 11918 | dport 80, sport 0xce35, and the two addresses | `slot 11918 ... :52789 -> ...:80` |
| index 12241 | dport 443, sport 0xc6d8, and two different addresses | `slot 12241 ... :50904 -> ...:443` |

0xce35 is 52789 and 0xc6d8 is 50904. Different indices, different contents, and the contents match a
table built from an independent source. That is the check this page asked for, and it passes.

**A refused read is marked, and its payload is stale.** Asked for index 7777, which no frame has ever
named, the first word came back `0xffffe19e` - which is **-7778**, or `-(index + 1)`. The rest of the
buffer was byte-identical to the reply before it.

That is the whole of the earlier mystery. This page recorded `0xfffffff8` for a request of indices 0
to 7: **-8 is `-(7 + 1)`**, the same encoding, for the same reason. Every index read on 2026-10-04 was
empty, every read was refused, and what was decoded as entry contents was the previous command's reply
sitting in a buffer nobody had cleared. *"Words 2, 3, 4 and 9 held the values of the last request"* was
exactly right, and it was the clue.

### So the rule for using it

**Check that the first word of the payload equals the index you asked for.** If it is negative, the
entry is not valid and everything after it belongs to the previous command. There is no other way to
tell the two apart, because the buffer is not cleared between them.

And the reason the entries were empty: `MFLOW_PROGRAM` may only update an entry that already exists -
the far side refuses one whose `mstate & 0xff0000` is zero - and nothing on this appliance created a
microflow until `FLOW_CREATE_FP` was built. Once a flow exists, programming it changes exactly the
field asked for: `sa_index` 0 to 1 to 0 and `sa_rev_num` 0 to 1 across three reads of one slot, with
everything else constant but a timestamp.

### What this page got right, and it is the part worth keeping

The withdrawal stands: the 2026-10-04 decode was wrong, and it was wrong in the way the lesson below
says. **The check that would have caught it is the one that confirmed the instrument today** - read a
second index - and today it caught something as well, because the stale buffer made an invalid index
look like a populated one until its first word was read.

An instrument that answers plausibly for every input is still the failure mode. The defence is not to
distrust the instrument; it is to know which byte says the answer is real.
