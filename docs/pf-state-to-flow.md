# Where a flow has to come from

Read out of the kernel sources on the appliance, 2026-10-05, after
[a-frame-was-accelerated.md](a-frame-was-accelerated.md) settled what to write. This page is about
**who writes it**, which is the open half of `#211`.

Nothing is implemented here. It is the inventory taken before writing, and it changed the answer
twice - the second time after this page had already been published.

## What a flow needs from the host

A microflow is a five-tuple plus an interface, and `pf` already has exactly that for every
connection it is tracking. `struct pf_kstate` carries two keys:

```c
struct pf_state_key {
	struct pf_addr	 addr[2];
	u_int16_t	 port[2];
	sa_family_t	 af;
	u_int8_t	 proto;
	...
};
```

with `s->key[PF_SK_WIRE]` and `s->key[PF_SK_STACK]` - `enum { PF_SK_WIRE, PF_SK_STACK, PF_SK_BOTH }`
in `netpfil/pf/pf.h:62`. The wire key is the one the coprocessor sees. Beside it,
`s->direction`, `s->kif` and `s->orig_kif` for the interface, and `s->timeout`.

So the mapping is direct: the wire key gives the microflow key, the two directions give the two
microflows `FLOW_CREATE_FP` carries, and `s->kif` gives the LIF.

## The hook exists, and it is already taken

`pf.c` calls a function pointer on every state insert and every state delete:

```c
	if (V_pfsync_insert_state_ptr != NULL)
		V_pfsync_insert_state_ptr(s);
```

at `pf.c:1876`, and the same shape for delete at `pf.c:2893`. The pointers are VNET variables
defined in `pf_ioctl.c:294` and declared in `net/pfvar.h:1327`:

```c
typedef	void		pfsync_insert_state_t(struct pf_kstate *);
typedef	void		pfsync_delete_state_t(struct pf_kstate *);
```

This is precisely the notification a flow offload needs, and it is **not available**. Six of these
pointers belong to `pfsync`, which claims them in `pfsync_pointers_init()`:

```c
	PF_RULES_WLOCK();
	V_pfsync_state_import_ptr = pfsync_state_import;
	V_pfsync_insert_state_ptr = pfsync_insert_state;
	...
```

**and sets every one of them to NULL in `pfsync_pointers_uninit()`.**

Two things follow, and the second is the one that matters:

- a module that assigns them **overwrites** pfsync, because pfsync does not check whether anything
  is there and neither would we - high availability would stop replicating states, silently
- a module that **chains** - saving the old pointer and calling it - is undone the moment pfsync's
  uninit runs, because that writes NULL over the chain rather than restoring what it found

So chaining is not a safe design here. It is not a matter of being careful; the other side of the
chain actively destroys it.

**And this is not hypothetical on this appliance.** `pfsync` is loaded on a stock OPNsense 26.7
with no HA configured at all - `kldstat` lists it and `pfsync0` exists, flags 0, `maxupd: 128`. So
the pointers are claimed on the machine this driver runs on, today.

## What that leaves

**Not an option: patching `pf`.** A core patch is replaced at every update, and this project has
already paid for that lesson once with the kernel follower.

**The option that does not need a hook at all, and is the one to build.**

The question was never "how does the driver learn that a connection exists". **The driver already
knows.** The coprocessor punts the frame to it, and `usfp_kn_md` carries that frame's microflow
slot and revision - so the forward slot arrives for free, with no hash and no notification. What
the driver is missing is only `pf`'s verdict on the frame it just handed up.

And `pf` exports exactly that lookup. `net/pfvar.h:2420`:

```c
extern struct pf_kstate		*pf_find_state_byid(uint64_t, uint32_t);
extern struct pf_kstate		*pf_find_state_all(
				    const struct pf_state_key_cmp *,
				    u_int, int *);
extern bool			pf_find_state_all_exists(
				    const struct pf_state_key_cmp *,
				    u_int);
```

`pf` is a loadable module here - `pf.ko`, in `kldstat` - and all three are global text symbols in
it, confirmed with `nm`:

    0000000000003aa0 T pf_find_state_all
    0000000000003d50 T pf_find_state_all_exists
    0000000000003990 T pf_find_state_byid

So the shape is: a frame is punted, the driver hands it up, `pf` decides; on a later punted frame
of the same flow the driver asks `pf_find_state_all()` whether a state exists for the tuple, and if
one does, programs the slot the metadata already gave it. On a punted frame whose state has gone,
`pf_find_state_all_exists()` says so and the flow is invalidated. Read-only with respect to `pf`,
no hook taken from anybody, nothing for `pfsync` to collide with, and no polling: it is driven by
the frames the driver is already being handed.

**The locking contract matters and is easy to get wrong.** With `more == NULL`,
`pf_find_state_all` returns the state **with `PF_STATE_LOCK(s)` held** - it takes the hashrow lock,
finds the key, takes the state lock and drops the hashrow lock before returning. The caller must
`PF_STATE_UNLOCK(s)`. `pf_find_state_all_exists()` is the variant that returns a bool and holds
nothing, which is the right one for the invalidate path. The key passed in is compared with `bcmp`
over `sizeof(struct pf_state_key_cmp)`, so every byte of it including the padding has to be
initialised.

**The three options considered before this one**, kept because the reasoning is the record and
because this one may yet fail on something:

1. **Read the state table from userland.** FreeBSD 15 has a netlink interface to `pf` -
   `netpfil/pf/pf_nl.h`, 36 commands - so states can be enumerated without parsing `pfctl`. Costs
   tens of milliseconds and a daemon; the in-kernel lookup above costs neither.
2. **Ask FreeBSD for a hook that is not pfsync's.** The right shape is a registration list rather
   than a single pointer - the same argument that makes `pfil` a list. Still worth saying upstream,
   and no longer on this project's critical path.
3. **Take the pointers and refuse to run when pfsync is active.** Lowest latency, and it trades
   somebody else's HA for our throughput on a machine where `pfsync` is loaded by default. Written
   down because it is the obvious idea and because the reason it is last should be on the record.

## What is still unknown

**Whether the reverse microflow can be found at all.** The reverse direction only exists for
traffic that transits the appliance - the host-to-wire path has no microflow gate, confirmed by its
counters: there is no `FROM_KN_*_MFLOW_*`, no `NHOP`, no `CONN` among them, so a frame the host
sends goes straight out. The slot of a reverse flow therefore has to be computed, from the hash,
rather than read from a punted frame the way the forward slot is. That hash is recorded and has
never been used to place a flow.

**Whether a state's timeout and a microflow's timeout can be reconciled.** `pf` ages a state and
the coprocessor ages a microflow, on their own clocks. A flow that outlives its state forwards
traffic the firewall has stopped tracking, which is the one failure mode here that is worse than no
offload at all.

## Lesson

The hook was found in ten minutes and the reason it cannot be used took another ten. Both were in
the same two files, and the second half is the part that would have been discovered three days into
an implementation instead - with a working driver, a broken `pfsync`, and no obvious connection
between the two. **Read the uninstall path, not just the install path**: `pfsync_pointers_init` says
the hook is available and `pfsync_pointers_uninit` says it is not, and only one of those two
functions is the one you go looking for.

**And the second lesson overturned the first page of this one.** Three options were written down,
ranked, and committed before anybody asked whether a hook was needed at all - and it is not,
because the driver is already handed the frame and already told its slot. The question had been
framed as "how does the driver get notified", which is the shape of the problem when you start from
the hook; framed as "what does the driver not already know", the answer is one exported function.
Half an hour was spent ranking three ways to solve a problem that a different sentence dissolved.
**When the options are all awkward, re-read the question.**
