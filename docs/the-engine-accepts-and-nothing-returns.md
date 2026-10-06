# The engine accepts the frame, and no completion path is armed

Measured on an XGS 3300, 2026-10-06, after [the association belongs to an
interface](the-crypto-engine-is-fed.md). The result there reproduces -
`FPCNTR_FROM_WIRE_TO_IPSEC_ENCR` 13 to 18 on a second flow - and this page is about where those
frames go.

**One conclusion on this page was wrong and is retracted below**, within the hour and by the same
method that produced it. The section that drew it now says why it does not follow.

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

## The event crypto adapter is not used at all, and that is not evidence about the completion

**This section replaces a wrong conclusion**, drawn first and corrected an hour later. It said that
`rte_event_crypto_adapter_queue_pair_add` having no call site meant no completion path was armed.
The call count is a fact; the conclusion does not follow, and the reason it does not is worth more
than the claim was.

| symbol | direct call sites in `usfp` |
|---|---|
| `sadb_hw_entry_get` | 3 |
| `esp_pre_crypto` | 1 |
| `rte_event_crypto_adapter_caps_get` | 2 |
| `rte_event_crypto_adapter_create_ext` | 1 - **from `rte_event_crypto_adapter_create`** |
| `eca_crypto_adapter_run` | 1 - **from `eca_service_func`** |
| `rte_event_crypto_adapter_create` | **0** |
| `rte_event_crypto_adapter_start` | **0** |
| `rte_event_crypto_adapter_stop` | **0** |
| `rte_event_crypto_adapter_service_id_get` | **0** |
| `rte_event_crypto_adapter_queue_pair_add` | **0** |

The two non-zero rows in that family are the library calling itself. **The application never touches
the event crypto adapter**, so nothing about it is half-configured - it is linked-in dead code, which
a statically linked DPDK is full of.

And the path that *is* used cannot be counted this way: `rte_cryptodev_enqueue_burst` and
`rte_cryptodev_dequeue_burst` are **static inline in DPDK's own header**, so they are not functions in
this binary and a symbol search cannot see them. **An absence of symbols says nothing about an inline
call.** That is what the first reading got wrong.

What the search does give, and these are solid:

- `rte_cryptodev_sym_session_create` is called from **`sadb_hw_entry_get`** - the association lookup
  creates the crypto session lazily, which is why an association has to exist before a frame can be
  prepared.
- `rte_cryptodev_configure`, `rte_cryptodev_queue_pair_setup` and `rte_cryptodev_start` each have
  exactly one call site, all inside **`fp_state_init`**, in a loop whose bound comes from
  `rte_cryptodev_count`. So crypto is configured and started at startup - or skipped entirely and
  silently, if that count is zero.
- the count is not zero for want of hardware. On the coprocessor there are **two CPT physical
  functions on the kernel's `octeontx-cpt` driver and seventeen virtual functions, all seventeen bound
  to `vfio-pci`** - available to userspace, which is what DPDK needs. `usfp`'s command line carries no
  device allowlist, so they are probed.

## Lesson

Two negatives were worth keeping here and one was worth retracting, which is the same lesson from
both sides. Six log sites in the function under test, silent, says that function succeeded - no
instrument on the host could have said so. But **a symbol with no call site says a path does not
exist only if that path would have been a symbol**, and in a statically linked DPDK the hot paths are
inline headers and the cold ones are dead library code. The first reading of that table had it exactly
backwards: it treated a dead library function as a missing step, and could not see the live inline one
at all.

When a stage neither refuses nor completes, go looking for its other end - and check whether the end
you are looking for is the kind of thing your instrument can see.

## Where this leaves the question

It has moved twice and the second move is the solid one. It was *the engine refuses the frame before
consulting the association*; then *the association's interface was wrong*; and now **the engine
accepts the frame, the ESP preparation runs to its end, and nothing comes back.**

What is *not* established is why. The crypto device is configured and started by `fp_state_init`, the
hardware is bound to `vfio-pci` and probed, and the enqueue and dequeue are inline calls this project
cannot find by symbol. So the next step is to read `esp_pre_crypto` and the worker loop as code rather
than as a symbol table, and to find whether anything polls the cryptodev for completions at all.

## Lesson

Two negatives here were worth more than a positive would have been, and both came from asking what
*should* have been noisy. Six log sites in the function under test, silent, says the function
succeeded - no instrument on the host could have said that. A symbol with no call site and no
reference anywhere says a path does not exist - which is a stronger statement than any measurement of
a path that does. **When a stage neither refuses nor completes, stop measuring the stage and go
looking for its other end.**
