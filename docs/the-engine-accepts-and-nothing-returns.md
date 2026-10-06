# The engine accepts the frame, and no completion path is armed

Measured on an XGS 3300, 2026-10-06, after [the association belongs to an
interface](the-crypto-engine-is-fed.md). The result there reproduces -
`FPCNTR_FROM_WIRE_TO_IPSEC_ENCR` 13 to 18 on a second flow - and this page is about where those
frames go.

## The fast path has a log, and this is where it is

Nobody had read it. On the coprocessor, `usfp`'s standard output and standard error are both one
pipe, and the process on the other end is:

    logger -t dpdk -p user.info

So every line the fast path writes is in `/var/log/messages` **on the coprocessor**, tagged `dpdk`.
The format is the source file and line that wrote it:

    info dpdk: usfp_main.c[997] launching worker 15 on core 17

That is a general instrument, not a crypto one, and it had been missing for the whole project.

**And the coprocessor's kernel module logs IPsec events by name.** Proven by accident, from this
project's own writes:

    error: ipsec_fpop_sa_del Attempted to free a valid SA, saidx=5

So `usfp_rh.ko` reports an IPsec refusal with the function and the index. If an association or a
submit were being refused inside the module, it would say so, in those words.

## What that rules out

`crypto_pkt_submit` calls exactly two functions - the SA lookup and `esp_pre_crypto` - and
`esp_pre_crypto` contains **six `fp_log` call sites**. Through every trial in which frames were
accepted by the engine, **not one of them fired**. Six log sites in a 13 KB function are error paths;
none firing says the ESP preparation ran to its end.

So the frame is prepared and handed to the hardware. The question is no longer whether it is refused.

`FP_PKT_DUMP`, bit 4 of `fw_cfg`, **produces no log output at all.** It was set for one window with
frames going to the engine, and the `dpdk` log gained nothing. Worth recording so that it is not
tried again as a way to see inside the fast path.

## What is not armed

| symbol | direct call sites in `usfp` |
|---|---|
| `sadb_hw_entry_get` | 3 |
| `esp_pre_crypto` | 1 |
| `rte_event_crypto_adapter_create_ext` | 1 |
| `eca_crypto_adapter_run` | 1 |
| **`rte_event_crypto_adapter_queue_pair_add`** | **0** |

And its address appears exactly once in the whole binary - in its own symbol header - so it is not
called indirectly either. **No crypto queue pair is ever linked to the event device.**

On this chip a CPT completion comes back through the event device, which is the only thing the
workers dequeue from. A frame enqueued to a queue pair that was never added to the adapter has
nowhere to come back to - which is exactly the shape of what is measured: the engine is fed, no
counter refuses the frame, and `FPCNTR_RX_IPSEC` never moves.

The method behind that table is worth stating because a zero is only as good as the search: the same
grep finds 3 call sites for `sadb_hw_entry_get` and 1 for `esp_pre_crypto`, both of which are known
to be called from the path under test.

## Where this leaves the question

It has moved twice. It was *the engine refuses the frame before consulting the association*; then
*the association's interface was wrong*; and now **the engine accepts the frame and nothing is set
up to give it back**.

That is consistent with the oldest hypothesis on the issue - that the crypto queues are set up by
the vendor's own startup when its configuration asks for IPsec - and it now has a specific missing
call rather than a suspicion. What it does not yet say is whether the setup is skipped because the
platform configuration has IPsec off, or because it happens somewhere this binary does not show.

## Lesson

Two negatives here were worth more than a positive would have been, and both came from asking what
*should* have been noisy. Six log sites in the function under test, silent, says the function
succeeded - no instrument on the host could have said that. A symbol with no call site and no
reference anywhere says a path does not exist - which is a stronger statement than any measurement of
a path that does. **When a stage neither refuses nor completes, stop measuring the stage and go
looking for its other end.**
