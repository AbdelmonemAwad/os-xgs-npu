# A microflow is inert until its state says `MF_ACTIVE`

Measured on an XGS 3300 running OPNsense 26.7.5, 2026-10-05.

This answers the question `#211` was opened for: the offload gate was open, the flow table had an
entry for the traffic, the host could write to that entry - and not one frame was ever accelerated.

## What looked like success and was not

`FLOW_CREATE_FP`, RPC command 10, is accepted. Programming the slot a punted frame names does
everything it is asked to do, and reading the same slot back shows it:

    before   lbinfo 0x87  rev 0  fw_valid 1  host_valid 0
             action 0  dir 0  brctl 0  state 0  sa 0  conn 0  nhop 0 rev 0
    after    lbinfo 0x87  rev 0  fw_valid 1  host_valid 1
             action 1  dir 0  brctl 0  state 1  sa 0  conn 7  nhop 0 rev 0

`host_valid` went from 0 to 1, the action, the state and the connection are the values written. The
entry's own key confirms it is the right flow - `198.51.100.120 -> 198.51.100.1`, protocol 1, the
live ping. And the counters did not move at all:

    FPCNTR_FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE   +92  (861 -> 953)   in six seconds
    FPCNTR_TX_WIRE                            +92

Every frame still punted, at exactly the rate it had been punted before. Command 8 behaves
identically, which is the same observation from the other end: both commands reach
`mflow_fpop_prog_both`, so neither could ever have differed.

## Three negatives that cost nothing and ruled out a lot

**The command number is not being ignored.** An unknown command is refused locally, by this driver,
and `rpc.last` then shows the previous reply unchanged - `magic_seed` does not advance. For
commands 8 and 10 it does advance. The far side received the request and answered it.

**`rc 0x0000` is not a courtesy here.** `flow_create` in `conn_rpc.c` returns
`conn_fpop_flow_create`'s value directly. Zero is that function's own answer.

**The action is not the gate.** Every value from 0 to 7, each written and read back, each with
`host_valid` at 1: the counter stayed on `MFLOW_NOT_ACTIVE` for all eight.

## The gate

`mflow_fpop_prog_both` has one silent exit:

```c
if (mstate.fw_valid == 0 || (mstate.rev & MFLOW_REV_NUM_MASK) != cmd_data->mf_ident.mflow_rev_num)
        continue;
```

It was worth reading for its own sake - a request can be skipped with no error and no counter - but
it was not firing. The live entry had `fw_valid 1` and `rev 0`, and the request carried rev 0.

The answer was two lines further into the vendor's own host, in
`sp2fp_mflow_microflow_populate`:

```c
req->mf_opr.opr_fl.state = MF_ACTIVE;
```

One value, never varied, in the function that builds every microflow request SFOS sends. And the
counter that was firing says, in the vendor's own text for it, "the microflow entry matched by this
packet is **disabled for offload**".

So the state is the gate, and `state` is four bits. Sweeping all sixteen with everything else held
fixed, one value behaves differently:

    state=0    MFLOW_NOT_ACTIVE +22
    state=1    MFLOW_NOT_ACTIVE +21
    state=2    NHOP_UNRESOLVED  +18    MFLOW_NOT_ACTIVE +5
    state=3    MFLOW_NOT_ACTIVE +22
    ...
    state=15   MFLOW_NOT_ACTIVE +23

**`MF_ACTIVE` is 2.** At that value the traffic leaves `MFLOW_NOT_ACTIVE` and lands on
`FROM_WIRE_TO_KN_NHOP_UNRESOLVED`, which is the next test in the fast path's own order - the sweep
left the next hop at index 0, and index 0 is not resolved. The five frames still counted
`MFLOW_NOT_ACTIVE` in that row are the ones that arrived before the write took effect; the same
tail appears as a single `NHOP_UNRESOLVED` in the row after.

A flow is therefore only used when **all** of this holds: the offload gate is on, the fast path has
made the entry (`fw_valid`), the host has loaded the operation (`host_valid`), the revision in the
request matches the entry's, and the state is 2. Four of those five were already true for days.

## The action enum, as far as it is settled

With the state at 2, action 0 raises `FROM_WIRE_DROP_MFLOW_ACTION` on the first frame. So
`MF_ACT_DROP` is 0 and the declaration order in the vendor's printer is at least right at its head.

`MF_ACT_FWD`, `MF_ACT_IPS` and `MF_ACT_AUX` are **not** settled. Without a resolved next hop every
non-dropping action lands on `NHOP_UNRESOLVED` and they are indistinguishable. The sweep that tried
anyway is worth recording as a failure: the first iteration left an active DROP entry behind, so
every later row showed that entry's drops, and because the dropped frames no longer reached the
host the slot index read from the last received frame began wandering. A sweep over a field that
changes behaviour has to undo each step before the next.

## What is next, and what is not

The next hop is the remaining gate, and `NHOP_PROGRAM` already works and is already verified field
by field against `LO_NHOP_READ`. Pointing an active microflow at a resolved next hop is the step
that puts a frame through the fast path.

It is not the step that finishes `#211`. The flow used here is addressed to the appliance itself,
so forwarding it is meaningless; proving the path needs traffic that transits the box, which means
the next hop has to be a real gateway and a mistake is visible on the network. The order is: a
resolved next hop on this throwaway ICMP flow first, to see a frame accelerated at all, and only
then a transiting flow.

## Lesson

A write that lands, reads back correctly, and changes nothing is not a mystery to be probed - it is
a field that has not been written yet. Four separate measurements were spent on the length of the
request, the number of the command and the value of the action, all of them on fields that were
already right, while the one field that decides everything sat at a value no instrument complained
about. The vendor's own populate function names it in one line, and that line had been three
screens from code already read twice. Reading the caller is not optional once the callee has been
read: the callee says what is possible, and only the caller says what is required.
