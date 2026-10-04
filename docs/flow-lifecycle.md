# Binding an offloaded flow to a `pf` state

Steps 2 and 3 of [#211](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/211) say to issue
`FLOW_CREATE_FP` when `pf` inserts a state and `MFLOW_INVALIDATE` when that state expires. This page
is the reading that has to come before either: where `pf` inserts and releases a state, what is held
there, whether anything may be hung off it, and whether `pf` is the right place at all.

**Nothing here has been run.** No module was built, no command was sent, no number was measured. The
kernel facts are citations into source that can be opened; the design is proposed and marked as
proposed wherever it appears. The appliance is not attached to the session this was written in.

**Which kernel.** Every `sys/...` citation is against **OPNsense `stable/26.7` at
`12334a596709`**, dated 2026-07-12, which is the commit named by the kernel the appliance booted -
`stable/26.7-n283674-12334a596709`. Line numbers are that tree's. The FreeBSD comparison in section
E is against `freebsd/freebsd-src` `releng/15.1`.

One correction before the answers. **There is no `pf_unlink_state` in this kernel.** The function
that unlinks a state is `pf_remove_state`, and `pf_unlink_state` appears nowhere in `sys/`. The name
in #211's step 3 is the older one.

## A. Where a state is inserted, and where it is released

### Inserted

`pf_state_insert()` - `sys/netpfil/pf/pf.c:1824`. It is the only function that puts a state in the
table, and the insertion itself is one line:

    LIST_INSERT_HEAD(&ih->states, s, entry);      sys/netpfil/pf/pf.c:1871

followed by the reference count being set to 2 - one for the keys, one for the ID hash - at
`sys/netpfil/pf/pf.c:1873`, and the `FCNT_STATE_INSERT` counter at `sys/netpfil/pf/pf.c:1875`. It is
declared for other files at `sys/net/pfvar.h:2364`.

It has exactly two callers:

| caller | where | what it is |
|---|---|---|
| `pf_create_state()` | `sys/netpfil/pf/pf.c:6360` | the data path: a packet matched a `keep state` rule |
| `pfsync_state_import()` | `sys/netpfil/pf/if_pfsync.c:898` | a state learned from an HA peer |

The second one matters for this work and is easy to forget: on an HA pair, states arrive from the
peer without any packet having been seen locally. An offload driven only from `pf_create_state`
would accelerate nothing on the standby member and would then be wrong the moment it took over.

`pf_create_state()` itself is at `sys/netpfil/pf/pf.c:6177`, and its failure path is worth naming
because it is a third way a state stops existing: on `csfailed` or `drop` it sets
`s->timeout = PFTM_UNLINKED` and calls `pf_free_state(s)` directly -
`sys/netpfil/pf/pf.c:6423-6428` - without going through `pf_remove_state`. A hook placed only in
`pf_remove_state` never sees these. They were never inserted either, so nothing is leaked, but code
that pairs an install with an uninstall has to know that this path does both and neither.

### Released

Three functions, and they are three different things:

| | where | what it does |
|---|---|---|
| `pf_remove_state()` | `sys/netpfil/pf/pf.c:2861` | unlinks: `LIST_REMOVE` at `:2890`, sets `PFTM_UNLINKED` at `:2898`, drops the two references at `:2912` |
| `pf_free_state()` | `sys/netpfil/pf/pf.c:2933` | frees the memory, only once `refs == 0` and `timeout == PFTM_UNLINKED`, both asserted at `:2935-2937` |
| `pf_purge_expired_states()` | `sys/netpfil/pf/pf.c:2949` | finds expired states and calls `pf_remove_state` on them, at `:2972` |

`pf_purge_expired_states` is called from `pf_purge_thread()` - `sys/netpfil/pf/pf.c:2683` - and its
own comment at `sys/netpfil/pf/pf.c:2946` says it is called only from there and is therefore
serialized.

**The point of interest is `pf_remove_state`, not `pf_free_state`.** Unlink is when the filter stops
permitting the connection. Free is an arbitrary amount of time later, when the last reference goes -
a reference the data path may be holding. An offload keyed on free would keep forwarding across that
gap.

## B. What is held there, and whether it may sleep

**It may not sleep at either site, and the reason is the same at both.**

Both functions assert the network epoch and the ID hash row:

| | epoch | hash row |
|---|---|---|
| `pf_state_insert` | `NET_EPOCH_ASSERT()` `sys/netpfil/pf/pf.c:1831` | `PF_HASHROW_ASSERT(ih)` `sys/netpfil/pf/pf.c:1854` |
| `pf_remove_state` | `NET_EPOCH_ASSERT()` `sys/netpfil/pf/pf.c:2865` | `PF_HASHROW_ASSERT(ih)` `sys/netpfil/pf/pf.c:2866` |

The row lock is a plain mutex - `PF_HASHROW_LOCK(h)` is `mtx_lock(&(h)->lock)` at
`sys/net/pfvar.h:362`, over the `struct mtx lock` in `struct pf_idhash` at `sys/net/pfvar.h:2247`,
initialised `MTX_DEF` at `sys/netpfil/pf/pf.c:1227`.

And the epoch is the hard constraint. `_epoch_enter_preempt()` calls `THREAD_NO_SLEEPING()` -
`sys/kern/subr_epoch.c:479` - which increments `curthread->td_no_sleeping`
(`sys/sys/proc.h:1048`). A thread inside the net epoch is marked non-sleepable for as long as it is
in there. This is not a debug-only restriction: the counter is incremented unconditionally.

Two further facts about the lock, because they decide what a hook may do rather than only what it
may not:

- `pf_state_insert` **returns with the row still locked** - its own comment says so at
  `sys/netpfil/pf/pf.c:1879`. The lock is not dropped until `pf_test` reaches
  `PF_STATE_UNLOCK(s)` at `sys/netpfil/pf/pf.c:11495`. So a hook at the insert site runs with the
  row held, and so does everything between it and there.
- `pf_remove_state` **returns unlocked**, and its comment at `sys/netpfil/pf/pf.c:2857` explains
  why: it has to take key hash locks, so it releases the row at `sys/netpfil/pf/pf.c:2905`. The
  hook site at `:2893` is before that, so it too runs with the row held.

### What this means for an RPC request, specifically

The driver's RPC path is disqualified twice over, and neither reason is "it sleeps":

- it asserts `sc->mtx` held - `contrib/octep/octep_rpc.c:288` - and that mutex is `MTX_DEF`
  (`contrib/octep/octep.c:460`), so taking it inside the epoch is taking a sleepable-class mutex in
  a non-sleepable section;
- it then **busy-waits**, not sleeps. The completion loop runs
  `DELAY(1000)` - `contrib/octep/octep_rpc.c:563` - as many times as
  `OCTEP_RPC_CMD_WAIT_MS` allows (`contrib/octep/octep_rpc.c:544`), and that constant is **2000**
  (`contrib/octep/octep.h:1320`).

So the worst case is two seconds of spinning on a CPU with a driver mutex and the net epoch both
held. Inside an epoch section that is worse than sleeping would be: it stalls every epoch writer on
the machine for two seconds while holding a core, and `epoch_wait_preempt` callers block behind it.
The four-second figure in #211's framing belongs to the NetAgent mailbox rather than to RPC -
`docs/netagent.md:166` - but the conclusion is the same and the RPC number is the one that applies.

**So the hook must queue deferred work. It must not issue the request.**

### Which deferral, and the example to copy

FreeBSD's answer for exactly this problem is `sys/netipsec/ipsec_offload.c`, and it is worth copying
in full rather than in outline:

| | where | what |
|---|---|---|
| a dedicated taskqueue | `sys/netipsec/ipsec_offload.c:173` | `taskqueue_create("ipsec_offload", ...)`, its own threads, not `taskqueue_thread` |
| one task per operation | `sys/netipsec/ipsec_offload.c:419`, `:546` | `TASK_INIT` then `taskqueue_enqueue` |
| the work record is **pre-allocated** | `sys/netipsec/ipsec_offload.c:250` | `ipsec_accel_alloc_forget_tq()` mallocs with `M_WAITOK` at install time and parks it on the object |
| claimed exactly once | `sys/netipsec/ipsec_offload.c:542` | `atomic_cmpset_ptr` on that parked pointer; a loser returns and does nothing |
| a reference held across the deferral | `sys/netipsec/ipsec_offload.c:545` | `refcount_acquire(&sav->refcnt)`, released by `key_freesav` in the task at `:526` |
| the barrier | `sys/netipsec/ipsec_offload.c:1163` | `sync` is `taskqueue_drain_all` |

The pre-allocation at install time is the load-bearing part and the reason to cite this rather than
describe a taskqueue in general terms. **The teardown path cannot fail for want of memory, because
its memory was taken when the thing was installed.** A teardown that can fail to even be scheduled
is not a teardown.

Two points of shape, not of mechanism, come from this tree rather than from FreeBSD. `npuep` says
why a taskqueue and not a callout - `contrib/npuep/npunwa.c:106` - and `octep` says the same at
`octep_dp_link_poll` (`contrib/octep/octep_dp.c:2691`), which is enqueued on `taskqueue_thread`
(`contrib/octep/octep_dp.c:2774`) because `octep_nwa_do_request()` sleeps
(`contrib/octep/octep_nwa.c:605`). A new consumer of the RPC path should have **its own**
taskqueue rather than adding two-second items to `taskqueue_thread`, which
is single-threaded and already carries the link poll.

## C. Whether `pf` has a hook already

**It has the right shape at exactly the right two places, and it is already taken.**

`pf` calls out at both sites, and only at these two:

    if (V_pfsync_insert_state_ptr != NULL)
            V_pfsync_insert_state_ptr(s);          sys/netpfil/pf/pf.c:1876

    if (V_pfsync_delete_state_ptr != NULL)
            V_pfsync_delete_state_ptr(s);          sys/netpfil/pf/pf.c:2893

They are single-owner `VNET` function pointers - declared `sys/net/pfvar.h:1327`, defined
`sys/netpfil/pf/pf_ioctl.c:294`, and claimed by `pfsync` at `sys/netpfil/pf/if_pfsync.c:3328` and
cleared at `:3342`. There is one slot each. Taking it displaces `pfsync`, which is what an HA pair
runs on, so it is not available.

`pf`'s only `eventhandler` use is for interfaces, not states - `sys/netpfil/pf/pf_if.c:171-182`
registers against `ifnet_arrival_event`, `ifnet_departure_event` and the group and address events.
Nothing publishes a state event.

So **a patch to `pf` is required** for a hook at these sites. The smallest one that is not a
displacement:

1. two declarations in `sys/net/pfvar.h`, beside the `pfsync` ones at `:1327`;
2. two `VNET_DEFINE`s in `sys/netpfil/pf/pf_ioctl.c`, beside `:294`;
3. two call sites, immediately after the `pfsync` calls at `sys/netpfil/pf/pf.c:1877` and `:2894`.

Following `ipsec_offload`'s shape rather than `pfsync`'s for the call itself: `static inline`
wrappers that `atomic_load_ptr` the pointer and NULL-check it, as at
`sys/netipsec/ipsec_offload.h:78`, with the externs as at `sys/netipsec/ipsec_offload.h:54`. That is
around twenty lines in a header and two lines at each call site. It changes no structure layout, no
lock, and no behaviour when nothing registers.

**And the cost of that patch is the reason section G does not recommend it.** OPNsense ships no
kernel sources - that is the first thing `CONTRIBUTING.md:8` says, and the whole of why
`kernel-sources/README.md` exists. A module is rebuilt against each kernel by `kernel-follow.sh` on
a five-minute cron (`src/etc/cron.d/octep`). A *patched kernel* is a different undertaking: it has
to be rebuilt, reinstalled and rebooted into for every OPNsense kernel update, on an appliance whose
WAN is one of the ports the module brings up. Two lines in `pf.c` are cheap. Carrying a kernel fork
on this appliance is not.

## D. Precedent in FreeBSD for binding hardware acceleration to kernel state

There is precedent, it is close, and its shape should be followed. Its **failure posture should
not**, and that is the part worth writing down.

### `ipsec_offload` - the closest analogue

Hardware IPsec offload, bound to the SA lifecycle. The user-visible name in the tree is
`ipsec_offload.c`; its functions are `ipsec_accel_*`.

- the hook set: `sys/netipsec/ipsec_offload.h:54-72` - `ipsec_accel_sa_newkey_p`,
  `ipsec_accel_forget_sav_p`, `ipsec_accel_spdadd_p`, `ipsec_accel_spddel_p` and the lifetime and
  sync calls, all as `extern` function pointers behind inline wrappers;
- the driver side: `struct if_ipsec_accel_methods` at `sys/net/if_var.h:728`, with
  `if_sa_newkey`/`if_sa_deinstall` typedefs at `:136-138` and `if_setipsec_accel_methods()` at
  `:736`;
- the capability gate: a driver is only asked if `IFCAP2_IPSEC_OFFLOAD` is set in `if_capenable2`,
  checked at `sys/netipsec/ipsec_offload.c:265`;
- **the de-install flag is set synchronously, and only the hardware notification is deferred**:
  `sav->accel_flags |= SADB_KEY_ACCEL_DEINST` at `sys/netipsec/ipsec_offload.c:537`, under the lock,
  before the task is queued at `:549`.

That last one is the design rule this whole page turns on. The *decision* is never deferred. Only
the hardware's knowledge of it is.

### `toecore` and `tcp_offload` - the other half of the pattern

A method table and a registration call: `struct toedev`'s `tod_*` members at
`sys/netinet/toecore.h:59-79`, `register_toedev()`/`unregister_toedev()` at
`sys/netinet/toecore.h:128`. The per-connection handoff is `sys/netinet/tcp_offload.c:62-223`.
It is a *handoff* - the TOE takes the connection and the stack stops handling it - which is the
same ownership transfer a flow offload makes, and it is done through a registered device with an
explicit method table rather than through a bare pointer. If a `pf` hook is ever written, this is
the shape to argue for upstream, not a second pair of `pfsync`-style pointers.

### Where the precedent stops

`ipsec_offload`'s failure mode is benign and ours is not.

| | if the hardware keeps what the host forgot |
|---|---|
| `ipsec_offload` | it keeps encrypting with a dead SA. The peer drops it. Traffic fails. |
| a `pf` flow offload | it keeps **forwarding traffic the filter has stopped permitting** |

So `ipsec_offload`'s mechanics transfer - defer, pre-allocate the teardown, hold a reference, flag
synchronously - and its tolerance for a late teardown does not. Section F is about that difference.

## E. OPNsense is not stock FreeBSD: does it patch these sites?

**No. All six functions are byte-identical, and this was checked rather than assumed.**

`opnsense/src` `stable/26.7` is the branch the appliance runs - it is that repository's default
branch - and the appliance's own kernel commit, `12334a596709`, is itself a `pf` change:
*"pf: pf_route() dst no longer holds the gateway in 15.x #294"*. So the question is a fair one.

`sys/netpfil/pf/pf.c` differs from `releng/15.1` by 129 lines. The hunks fall in:
`pf_icmp_mapping` (three), `pf_match_rule`, `pf_multihome_scan` (two), `pf_route` and `pf_route6`
(four), `pf_counters_inc` (two), `pf_test`, and a sysctl block.

Each of the six lifecycle functions was then extracted from both trees by brace matching and
compared directly:

| function | lines | result |
|---|---|---|
| `pf_state_insert` | 58 | identical |
| `pf_remove_state` | 53 | identical |
| `pf_free_state` | 11 | identical |
| `pf_purge_expired_states` | 56 | identical |
| `pf_purge_thread` | 56 | identical |
| `pf_create_state` | 255 | identical |

**But the patches are not irrelevant, because of where they are.** They cluster on the forwarding
and routing path - the other candidate site for a flow offload. OPNsense adds two sysctls at
`sys/netpfil/pf/pf.c:551-562`:

    net.pf.share_forward     "If set pf(4) will defer IPv4 forwarding to the network stack."
    net.pf.share_forward6

read at `sys/netpfil/pf/pf.c:9277` and `:9632`, inside the patched `pf_route` and `pf_route6`, with
a matching change in `pf_test` that pulls a forwarding tag off the mbuf.

Two consequences, and they point opposite ways:

- **For the state lifecycle:** a patch written against FreeBSD applies to OPNsense unchanged today,
  and a reader of FreeBSD's `pf.c` is reading the appliance's. That is the good news.
- **For the forwarding path:** OPNsense is actively changing who performs forwarding, under a
  runtime switch. Anything bound to that path is bound to a moving target, and to a target whose
  behaviour depends on a sysctl the operator can flip.

Neither is a reason to patch or not patch by itself. The operational cost in section C is.

## F. The binding has to be exact, and in which direction

An accelerated flow is a flow `pf` no longer sees. So the question is not symmetric, and treating it
as symmetric is how it goes wrong.

| | consequence |
|---|---|
| a flow outlives its state | the coprocessor forwards what the filter has stopped permitting - **a hole** |
| a state outlives its flow | frames are punted to the host and filtered normally - **a slow path** |

Only one direction is a safety property. The other is a performance bug. A design that spends equal
effort on both has misread the problem.

### What is actually available to enforce it

From this project's own reading of the coprocessor, all of it already recorded:

- **Programming a non-live flow is a silent no-op.** `mflow_fpop_prog_both` skips an entry whose
  `fw_valid` is zero or whose revision does not match, with a bare `continue` - no error, no return
  code, no counter (`docs/families/octeon-tx-crypto-path.md:584`).
- **The whole table can be thrown away in one command.** `fw_state_fpop_rev_set` calls
  `mflow_fpop_invalidate_issue` over the entire table as a side effect of storing the revision
  (`docs/families/octeon-tx-crypto-path.md:330`, and the driver comment at
  `contrib/octep/octep_rpc.c:385`).
- **Every entry carries a revision that is checked.** Which is what makes the bump above a flush
  rather than only a number.
- **A programmed flow carries a timeout.** `mflow_timeout` is a `uint32_t` at `+0x1c` of
  `struct usfp_fpop_req_program_mflow` (`docs/families/octeon-tx-crypto-path.md:132`), and
  the platform table reports a *Mflow timeout* of 10 (`docs/families/octeon-tx-rpc.md:473`). **The
  units are not known** and the field has never been sent - see the questions at the end.

### Proposed: the binding is a lease, not a message

`MFLOW_INVALIDATE` failing is not the interesting case, and designing around it is the trap. A flow
can outlive its state in ways no message can cover: the host panics, the module is unloaded, the
driver detaches, the RPC ring wedges, the command times out after two seconds
(`contrib/octep/octep_rpc.c:544`). None of those run a cleanup path, and a correctness argument that
depends on one of them running is not an argument.

So, proposed, and untested:

1. **Program flows with a short `mflow_timeout` and re-affirm them.** Then the hardware forgets a
   flow on its own if the host stops talking, and the worst case for any failure - a lost
   invalidate, a wedged ring, a dead host - is bounded by one lease period rather than by the
   firewall's uptime. This is the property that makes the rest safe, and it needs no message to
   arrive.
2. **Never let `FW_CFG_OFFLOAD` be set except by the code maintaining the flows**, and clear it on
   detach and on module unload. #211 says this already. The hazard it guards is specific: `fw_cfg`
   has no read command, so a write replaces a value nobody has seen
   (`docs/families/octeon-tx-crypto-path.md:307`), and the bit currently lives in a writable sysctl
   (`contrib/octep/octep_rpc.c:967`, surfaced at `src/opnsense/scripts/xgs/status.py:196`).
3. **Bump the firewall revision on every ruleset reload.** This is #211's step 4 rather than mine,
   but it is the same mechanism and it belongs in the same sentence: it is the correct global flush,
   it is what the revision field is for, and it is cheap.

### Is whole-table invalidation the right fail-closed answer?

**As a backstop, yes. As the routine response to a failed invalidate, no.**

It is right when the host has lost track of what the hardware holds - after an RPC timeout, after a
reconfiguration, on attach, on a detected inconsistency - because it is the only operation whose
result does not depend on knowing the flow identities, and the host *cannot* enumerate them
cheaply: the table holds four million entries and a read returns 35 at a time
(`docs/families/octeon-tx-crypto-path.md:173`).

It is wrong as the ordinary answer to one failed invalidate, for a reason that is operational rather
than theoretical. A firewall under load unlinks states constantly. If each failed invalidate flushes
the table, then one wedged ring turns into a permanent flush loop, and the offload degrades to
"never accelerates anything" while continuing to pay for the attempt. Worse, it is indistinguishable
from working: every frame reaches the host and the firewall behaves correctly. #210's lesson is
exactly this failure - a counter that had been equal to another since boot, in an array printed
routinely, for weeks.

The lease makes the distinction unnecessary: a failed invalidate needs no response at all, because
the lease expires anyway.

## G. Is `pf` the right layer?

**For the invalidate, yes in principle and no in practice. For the create, the question is void -
the operation does not exist.**

### The create side does not exist

This is settled in this repository already and it contradicts #211's own body, which a comment on
that issue corrects. `conn_fpop_flow_create` writes the *connection* into its slot and then calls
`mflow_fpop_prog_both`, so **even the command named `FLOW_CREATE_FP` does not create a flow**
(`docs/families/octeon-tx-crypto-path.md:586`). The division is absolute: the fast path creates
flows, the host programs them (`:588`).

So "issue `FLOW_CREATE_FP` when `pf` inserts a state" cannot mean what it says. At insert time the
flow does not exist, and the programming half of the command is a silent no-op against an entry that
is not live.

What the host *can* usefully do at insert time is publish the connection, so that a flow has
something to bind to when the fast path makes one. And that was tried: a connection was published,
read back correct, and **changed nothing** - the table stayed empty and every frame was still punted
`MFLOW_NOT_ACTIVE` (`docs/families/octeon-tx-crypto-path.md:611`, in the section beginning at
line 599). Recorded there so it is not tried again.

### Which makes the shape wrong, not just the location

Here is the argument this page exists to make.

**A one-shot hook at state insert is guaranteed to fire at the wrong moment.** The programming call
succeeds only once the fast path has made the entry live, which happens when traffic flows, which is
after the state was inserted. A hook that fires at insert and does not retry will silently do
nothing, forever, and report success - because a skipped entry returns no error.

That rules out the event-driven shape on its own, independent of where the event comes from. What is
needed is a **reconcile loop**: compare what the filter permits against what the hardware holds, and
act on the difference, repeatedly.

**And this tree already has that pattern, with the scar tissue to prove it.** The receive-filter
reconcile in `octep` keeps two pieces of state per attribute and no more - what is wanted, and what
the far side was last *successfully* told (`contrib/octep/octep_dp.c:2604`). The wanted value is
read fresh each pass rather than recorded from an event (`contrib/octep/octep_dp.c:2753`), so there
is no transition to miss and no second copy to go stale. A request that fails records nothing, so
the comparison still disagrees and the next sweep asks again - that is the whole of the retry, with
no counter, no budget and no latch. The comment at `contrib/octep/octep_dp.c:2761` records what the
alternative cost: a state machine of five fields per attribute, a retry budget and a refusal latch,
three rounds of review, wrong in a new way after each of the first two. It was replaced by the shape
`npuep` had used since it first carried a bridge.

The same reasoning applies here and more strongly, because here the far side *silently* declines
until it is ready. Reconciliation is not merely tidier; it is the only shape that can succeed.

### So: not in `pf`, and not in the forwarding path

Proposed, with the argument:

- **Not a `pf` patch.** The hook is buildable (section C) but the loop does not need one, and the
  cost is a kernel fork carried across every OPNsense update on an appliance that ships no kernel
  sources (`CONTRIBUTING.md:8`). Paying that for an event that fires at the wrong time is the worst
  trade on the page.
- **Not the forwarding path.** It is the part of `pf` OPNsense is actively changing, under an
  operator-flippable sysctl (section E).
- **A reconciler that reads `pf`'s state table and drives the existing RPC surface.** It is
  sleepable by construction, so the two-second RPC cost is a scheduling question rather than a
  correctness one. It needs no new hook, because the state table *is* the authoritative wanted
  state - which is the same choice as reading `IFF_PROMISC` off the ifnet rather than recording it
  from `SIOCSIFFLAGS`.

This repository already reaches OPNsense this way. `src/opnsense/scripts/xgs/status.py` reads driver
state over sysctls; `src/etc/inc/plugins.inc.d/xgs.inc` takes OPNsense's plugin hooks without
editing any of its files, and notes at line 37 that `plugins_configure('ipsec')` is available and
deliberately not taken. The ruleset-reload equivalent is where the revision bump belongs. Note that
it has to be a *configure* hook: the `rc.syshook` levels this appliance actually calls are facility,
stop, start, monitor, upgrade, import, early and carp, and there is no reload among them - the
comment in `src/etc/cron.d/octep` records how that was learned, by placing a hook in a directory
nobody calls.

**The honest counter-argument**, which a reconciler does not answer: it cannot be as prompt as a
hook. A state unlinked between two passes leaves its flow live until the next one. That is precisely
why section F puts the safety on the lease rather than on the promptness of the message - and if the
lease turns out not to be settable, this argument weakens and the case for an in-kernel hook gets
stronger. That is the single measurement this design depends on most.

### And the thing that makes all of it moot today

**As the appliance is wired there is nothing to accelerate.** LAN is `igb0`, an Intel NIC the
coprocessor cannot see; WAN is `oxp3`, a coprocessor front port. Every routed packet goes
coprocessor, host, `igb0`, and the coprocessor physically cannot carry it because the other end is
not its port (`docs/families/octeon-tx-crypto-path.md:626-646`). The one LIF that exists names
`oxp2`, a port with no cable in it.

So `MFLOW_NOT_ACTIVE` on every frame may be the correct answer to a question with no acceleratable
traffic in it, and no amount of host-side flow management changes that. **Testing any of this needs
traffic whose two ends are both coprocessor front ports, with a LIF on each.** That is a change to
how the appliance is wired and addressed, which is the owner's call and not something to try while
his internet is on the other end of it.

## What is not here

- **Nothing was run.** No build, no command, no counter. Section E's identity check was run, on
  source, in a container; the kernel citations can be opened; everything else is proposed.
- **No ordering proof for the insert hook.** `pf_state_insert` publishes the state into the ID hash
  at `sys/netpfil/pf/pf.c:1871` and the data path can find it from that instant, while the `pfsync`
  call is at `:1876`. Whether a deferred offload install can race a state that is already being
  matched was not worked through, and it matters for the create side if the create side ever exists.
- **`pfsync` interaction beyond the import path.** A state learned from a peer inserts without a
  local packet (`sys/netpfil/pf/if_pfsync.c:898`). What an HA failover should do about flows
  programmed on the other member is not addressed here at all.
- **No measurement of the RPC path under load.** The two-second figure is the driver's timeout
  constant, not an observed latency. The observed cost of a NetAgent request is 12 ms healthy and up
  to four seconds when it is not (`docs/netagent.md:166`); RPC has no equivalent measurement.

## What this needs that is not in the repository

The inventory files and the vendor source are deliberately absent, and four questions cannot be
closed without them. Asking them plainly rather than guessing:

1. **What are `mflow_timeout`'s units, and is it honoured on a host-programmed flow?**
   `platform_info` reports 10 and the field is at `+0x1c` of
   `struct usfp_fpop_req_program_mflow`. Seconds would make the lease in section F workable;
   milliseconds would not, and a field the fast path overwrites from its own policy would make the
   whole lease argument wrong. `mflow_table.h` and `mflow_fpop.c` should say. **This is the one
   answer the design turns on.**
2. **Does `mflow_fpop_prog_both` refuse a `host_valid` entry whose `fw_valid` has since gone?**
   The comment quoted at `docs/families/octeon-tx-crypto-path.md:567` says `fw_valid` is the fast
   path's and `host_valid` is the host's. What happens to a flow the fast path has reclaimed while
   the host still believes it owns it decides whether re-affirming a lease can resurrect a dead
   flow.
3. **What makes the fast path create a flow?** `mflow_alloc` is named at
   `docs/families/octeon-tx-crypto-path.md:161` as taking a hash and a count. Its preconditions -
   whether a connection, a next-hop, a LIF on both sides, or a forwarding decision is required -
   are the actual blocker for #211, and the connection has already been ruled out by measurement.
4. **Is there a per-flow invalidate that reports its outcome?** `MFLOW_INVALIDATE` is command 9 and
   its request layout is not in this repository. If it returns a code distinguishing "invalidated"
   from "was not live", the reconciler can tell a lost race from a stale belief. If it is silent
   like the programming call, it cannot, and the lease carries even more weight.

**Lesson.** The shape of a mechanism is decided by how the far side fails, not by where the event
is. Three rounds of review went on a state machine in this driver before the far side's silent
refusal was taken as the design input rather than as an error to handle, and the same silence is
what rules out an event hook here before any question of locking or layering arises.
