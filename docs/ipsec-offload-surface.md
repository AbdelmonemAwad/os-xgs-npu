# `if_ipsec_accel_methods`: the six calls, their contracts, and the order that must not be got wrong

Step 5 of [#211](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/211) and the whole of
[#185](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/185) end at the same place: a security
association the coprocessor accepts and a frame it refuses, because the index is validated through a
flow. If this driver is ever to carry IPsec offload the way FreeBSD expects, it does it through
`struct if_ipsec_accel_methods`. This page is that surface read end to end.

**Nothing here has been run.** No module was built, no command was sent, no number was measured.
Every kernel fact is a citation that can be opened; everything else is a proposal marked as a
proposal. The appliance was not attached to the session this was written in.

Kernel citations are against **OPNsense `stable/26.7` at `083dc7025377`**, the commit named by the
kernel this appliance runs. Each names the function, then the literal expression, then the file and
line.

## 0. The gate: this may not exist in the appliance's kernel at all

Everything below is behind a kernel option.

    netipsec/ipsec_offload.c	optional ipsec ipsec_offload inet | \
    	ipsec ipsec_offload inet6
    - sys/conf/files:4516

So **both** `IPSEC` and `IPSEC_OFFLOAD` are required. `options IPSEC_OFFLOAD` is in amd64's
`GENERIC` - `options 	IPSEC_OFFLOAD		# Inline ipsec offload infra`
(`sys/amd64/conf/GENERIC:35`) - and it is declared as an option at
`IPSEC_OFFLOAD		opt_ipsec.h` (`sys/conf/options:463`). But this appliance's kernel identifies as
`SMP`, not `GENERIC`, and **OPNsense's kernel configuration is not in `opnsense/src`** - it lives in
the build tooling, which this session does not have. So whether the option is set cannot be answered
from the source.

It can be answered on the appliance in one command, because the subsystem registers a sysctl node
that exists only when the file is compiled in -
`SYSCTL_NODE(_net_inet_ipsec, OID_AUTO, offload, CTLFLAG_RW | CTLFLAG_MPSAFE, 0,`
(`sys/netipsec/ipsec_offload.c:229`):

```sh
sysctl net.inet.ipsec.offload
```

A node means the infrastructure is present. `unknown oid` means it is not, and then **none of this
page applies** and offload would need a kernel built with the option - which on an appliance that
ships no kernel sources is the same cost as the `pf` patch rejected in #229.

**Ask this first.** It is the cheapest question on the page and it decides whether the rest is work
or reading.

One more thing to read before relying on any of it, in the header itself:

> *"NB: The interface is not yet stable, drivers implementing IPSEC offload need to be prepared to
> adapt to changes."*
> - `sys/net/if_var.h:726`

## 1. The table

    struct if_ipsec_accel_methods {        /* sys/net/if_var.h:730 */
    	if_spdadd_fn_t		if_spdadd;
    	if_spddel_fn_t		if_spddel;
    	if_sa_newkey_fn_t	if_sa_newkey;
    	if_sa_deinstall_fn_t	if_sa_deinstall;
    	if_sa_cnt_fn_t		if_sa_cnt;
    	if_ipsec_hwassist_fn_t	if_hwassist;
    };

Registered by `void if_setipsec_accel_methods(if_t ifp, const struct if_ipsec_accel_methods *);`
(`sys/net/if_var.h:738`), whose entire implementation is one store:

    if_setipsec_accel_methods(if_t ifp, const struct if_ipsec_accel_methods *m)
    {
    	ifp->if_ipsec_accel_m = m;        /* sys/net/if.c:5202 */
    }

No lock, no barrier, no reference count, no validation. **Everything about ordering is the driver's
responsibility**, and section 3 is what that means.

## 2. The six, one at a time

### `if_sa_newkey` - install an association

    typedef int (*if_sa_newkey_fn_t)(if_t ifp, void *sav, u_int drv_spi,
        void **privp);
    - sys/net/if_var.h:136

Called at
`error = ifp->if_ipsec_accel_m->if_sa_newkey(ifp, tq->sav,`
(`sys/netipsec/ipsec_offload.c:304`), from `ipsec_accel_sa_newkey_cb()`, which runs on the
subsystem's own taskqueue - **not** in the caller's context. The chain is
`ipsec_accel_sa_newkey_impl()` (`:397`) queues `install_task` (`:419`), and the task runs the
callback.

Contract, as the code establishes it:

- **`drv_spi` is the kernel's handle, not yours.** It is allocated by the subsystem at
  `drv_spi = alloc_unr(drv_spi_unr);` (`:291`), out of a range
  `IPSEC_ACCEL_DRV_SPI_MIN` 3 to `IPSEC_ACCEL_DRV_SPI_MAX` 0xffff
  (`sys/netipsec/ipsec_offload.h:51`), with 2 reserved as
  `IPSEC_ACCEL_DRV_SPI_BYPASS` (`:50`). **This is a second numbering space**, and it is the one that
  appears in every later call and in the per-packet tag. The coprocessor's own `saidx` -
  `+0 saidx uint32_t the index this association takes` in
  `struct usfp_fpop_req_sa_add` (`docs/families/octeon-tx-rpc.md:609`) - is this driver's business
  and the kernel never sees it. A driver has to keep the map both ways.
- **`privp` is an out-parameter.** Whatever the driver stores there comes back as `priv` on every
  subsequent call for this association, and is the driver's only handle.
- **A non-zero return is a refusal, and the subsystem unwinds it.** On error the handle is released
  at `free_unr(drv_spi_unr, drv_spi);` (`:322`), and on a later failure in the same function the
  driver's own install is undone by calling `if_sa_deinstall` (`:331`) before
  `free_unr` (`:334`). So `if_sa_newkey` must either succeed completely or leave nothing behind.
- **It may sleep.** It runs on `ipsec_accel_tq`, created at
  `ipsec_accel_tq = taskqueue_create("ipsec_offload", M_WAITOK,` (`:173`) with
  `1 /* Must be single-threaded */` (`:176`). That is the one property that makes this surface usable
  from a driver whose RPC path costs up to two seconds, and it is also the reason not to be slow:
  one queue, single-threaded, shared by every association and policy on the machine.

### `if_sa_deinstall` - remove one

    typedef int (*if_sa_deinstall_fn_t)(if_t ifp, u_int drv_spi, void *priv);
    - sys/net/if_var.h:138

Two callers, and they are different situations:

| where | why |
|---|---|
| `error = ifp->if_ipsec_accel_m->if_sa_deinstall(ifp,` - `sys/netipsec/ipsec_offload.c:331` | rolling back a partially failed install, inside `if_sa_newkey`'s own path |
| `ifp->if_ipsec_accel_m->if_sa_deinstall(ifp,` - `sys/netipsec/ipsec_offload.c:489` | the real teardown, from `ipsec_accel_forget_handle_sav()` |

Note what it is **not** given: no `sav`. Only `drv_spi` and `priv`. So by the time the kernel asks
for removal, the driver cannot look anything up through the association - it must be able to undo the
install from its own stored handle alone. That is a design constraint on what `privp` has to carry,
and it is the half that the flow work in #229 would have to satisfy too.

The pre-allocation discipline behind this call is described in #229's section B and is the reason to
copy it: the teardown record is malloc'd at install time at
`ftq = malloc(sizeof(struct ipsec_accel_forget_tq), M_TEMP, M_WAITOK);`
(`sys/netipsec/ipsec_offload.c:258`) so that teardown cannot fail for want of memory.

### `if_spdadd` / `if_spddel` - install and remove a policy

    typedef int (*if_spdadd_fn_t)(if_t ifp, void *sp, void *inp, void **priv);
    typedef int (*if_spddel_fn_t)(if_t ifp, void *sp, void *priv);
    - sys/net/if_var.h:134 and :135

`if_spdadd` is called at
`error = ifp->if_ipsec_accel_m->if_spdadd(ifp, sp, inp, &i->ifdata);`
(`sys/netipsec/ipsec_offload.c:689`), also from the taskqueue (`ipsec_accel_spdadd_act()` at `:699`,
queued by `ipsec_accel_spdadd_impl()` at `:720`).

`if_spddel` has two call sites, `:761` and `:826`, the second from
`ipsec_accel_on_ifdown_sp()` - so **an interface going down tears policies down through this
method**, which is a path a driver must handle without the interface being usable.

These are the *policy* half: what traffic should be protected, as against `if_sa_newkey`'s *how*.
For this coprocessor there is no obvious counterpart - the vendor's RPC surface has
`SA_ADD` through `SA_HOST_STAT_SYNC` (30 to 35) and no policy command at all
(`docs/families/octeon-tx-rpc.md:597`). The selector lives in the flow, not in a policy table. So a
driver here would most likely accept `if_spdadd` and record it, with the actual selection happening
when a flow is programmed - which is the same conclusion #185 already reached from the other
direction.

### `if_sa_cnt` - report the counters back

    typedef int (*if_sa_cnt_fn_t)(if_t ifp, void *sa,
        uint32_t drv_spi, void *priv, struct seclifetime *lt);
    - sys/net/if_var.h:149

Two call sites, `sys/netipsec/ipsec_offload.c:1097` and `:1138`, and **this is the only one of the
six that is NULL-checked per method**: `p = ifp->if_ipsec_accel_m->if_sa_cnt;` then
`if (p != NULL)` at `:1098`, and `if (p == NULL)` then `continue` at `:1139`. So a driver may leave
this one unset; it may not leave the others unset, which section 3 is about.

This is the call that answers the warning already recorded in this repository: *"the host and the
coprocessor each keep a sequence number and a byte count ... An offload that installs associations
and never reconciles them will fail a rekey"* (`docs/families/octeon-tx-rpc.md:659`). The
coprocessor's side of it is `SA_GET_STATS` (32) and `SA_HOST_STAT_SYNC` (35). `if_sa_cnt` is where
the kernel asks for it, and the `IF_SA_CNT_WHICH` enumeration (`sys/net/if_var.h:141`) says which of
the four quantities is wanted, with `IF_SA_CNT_UPD` (`:140`) distinguishing a read from a
read-and-update.

### `if_hwassist` - per-packet, and the one with a trap in its arguments

    typedef int (*if_ipsec_hwassist_fn_t)(if_t ifp, void *sav,
        u_int drv_spi,void *priv);
    - sys/net/if_var.h:151

Called at `*hwassist = ifp->if_ipsec_accel_m->if_hwassist(ifp, sav,`
(`sys/netipsec/ipsec_offload.c:945`), on the output path, with **no NULL check on the member at
all** - unlike `if_sa_cnt`. A driver that registers a table with `if_hwassist` left NULL and ever
reaches this path calls through a NULL pointer.

And the argument trap, which matters for what a driver may do with `sav`: three lines earlier the
caller releases its reference, at `key_freesav(&sav);` (`sys/netipsec/ipsec_offload.c:941`). That
function takes a double pointer and sets `*psav = NULL;` (`sys/netipsec/key.c:1446`) - but only on
the last-reference path, returning early at `:1441` when other references remain. So at the call
site `sav` is either **NULL** or a pointer the caller no longer holds a reference to.

**So `if_hwassist` must not dereference `sav`, and must tolerate it being NULL.** Everything it
needs is in `drv_spi` and `priv`. This is read from the source rather than from documentation, and
it is the kind of thing the header's "not yet stable" warning is about.

## 3. The order, and the hazard

**`if_ipsec_accel_m` is never NULL-checked. Anywhere.** Every one of the eleven dereferences in
`sys/netipsec/ipsec_offload.c` assumes it is set. The three entry points are where it bites:

| gate | what it does |
|---|---|
| `ipsec_accel_sa_install_match()` | tests the capability at `if ((ifp->if_capenable2 & IFCAP2_BIT(IFCAP2_IPSEC_OFFLOAD)) == 0)` (`:266`), then dereferences at `if (ifp->if_ipsec_accel_m->if_sa_newkey == NULL) {` (`:268`) |
| `ipsec_accel_spdadd_match()` | the capability and the member in one expression: `ifp->if_ipsec_accel_m->if_spdadd == NULL)` (`:660`) |
| the output path | `ifp->if_ipsec_accel_m->if_hwassist(ifp, sav,` (`:945`), no check of either |

In the first two the capability test short-circuits, so the dereference happens **only when the
capability bit is set**. That is the entire protection. There is no second check, and
`if_setipsec_accel_methods()` is a bare store (`sys/net/if.c:5202`).

So the invariant the subsystem rests on is:

> if `IFCAP2_IPSEC_OFFLOAD` is set in `ifp->if_capenable2`, then `ifp->if_ipsec_accel_m` is non-NULL.

**Raising the capability bit before installing the methods is a kernel NULL dereference**, and it is
reached by the first association anyone adds - not at attach, so not in a test that only loads the
driver.

### The order, from the only driver in the tree that does it

`if_vlan` gets this right, and it is worth reading because the mechanism is not a comment:

```c
#ifdef IPSEC_OFFLOAD
	cap2 |= p->if_capabilities2 & IFCAP2_BIT(IFCAP2_IPSEC_OFFLOAD);   /* 2203 */
	ena2 |= mena2 & IFCAP2_BIT(IFCAP2_IPSEC_OFFLOAD);                 /* 2204 */
	ifp->if_ipsec_accel_m = &vlan_if_ipsec_accel_methods;             /* 2205 */
#endif

	ifp->if_capabilities2 = cap2;                                     /* 2209 */
	ifp->if_capenable2 = ena2;                                        /* 2210 */
```

`cap2` and `ena2` are **locals**. The method pointer is stored into the `ifnet` at
`ifp->if_ipsec_accel_m = &vlan_if_ipsec_accel_methods;` (`sys/net/if_vlan.c:2205`), and the
capability words are published to the `ifnet` afterwards at
`ifp->if_capenable2 = ena2;` (`sys/net/if_vlan.c:2209`). Accumulating the bits in a local is what
makes the publish last rather than incremental.

So, named as the task asks:

1. fill the `struct if_ipsec_accel_methods` - statically, `const`, with **every member a real
   function**, since only `if_sa_cnt` may be NULL;
2. `if_setipsec_accel_methods(ifp, &methods)`;
3. only then `ifp->if_capabilities2 |= IFCAP2_BIT(IFCAP2_IPSEC_OFFLOAD)` and, when the operator or
   the driver enables it, `if_capenable2`.

And on the way down, the reverse: **clear `if_capenable2`'s bit first**, then stop. Do not clear
`if_ipsec_accel_m`. There is nothing in this subsystem that waits for in-flight calls to finish -
`ipsec_accel_fini()` drains its own taskqueue at `taskqueue_drain_all(ipsec_accel_tq);`
(`sys/netipsec/ipsec_offload.c:221`) only on module unload of the subsystem itself, not per
interface - so a driver that nulls the pointer races every one of those eleven dereferences.
`if_vlan` never clears it, and that is the pattern to copy. On detach, the `ifnet` goes away with
the pointer; before that, the capability bit is the only switch.

A detaching interface should also expect `if_spddel` from
`ipsec_accel_on_ifdown_sp()` (`sys/netipsec/ipsec_offload.c:826`), driven by
`ipsec_accel_ifdetach_event()` (`:852`) off an `eventhandler`. So teardown calls arrive **during**
detach, through the method table, after the interface has gone down.

## 4. What this does and does not do for #185

It does not unblock it. That needs saying plainly, because wiring up six methods looks like
progress.

This surface installs **associations**. This project's own reading is that an association cannot be
*validated* without a flow, because the revision it is matched against lives in a flow entry:
*"A frame with no flow has no revision to be matched against"*
(`docs/families/octeon-tx-crypto-path.md:516`). The annotation it rests on is the vendor's own, in
`sa_table.h` - `uint16_t rev_num;   /* rev in mflow */`, quoted at
`docs/families/octeon-tx-crypto-path.md:493`.

So the order of work is the opposite of the order of the step numbers:

| | |
|---|---|
| `if_sa_newkey` reaching `SA_ADD` (30) | already proven to work - an association installs and reads back (#185) |
| a flow existing at all | **the blocker**, and #211's real subject |
| `MFLOW_PROGRAM` (8) carrying `opr_fl.sa_index` and `opr.sa_rev_num` | what actually makes a frame's association validate |
| `if_sa_cnt` reaching `SA_GET_STATS` / `SA_HOST_STAT_SYNC` | needed before a rekey, not before a first packet |

Which means this page's value is not "implement these six". It is: **when a flow exists, this is the
shape the kernel expects the SA half to be wired through, and this is the order that avoids
panicking it.** Building the method table before there is a flow to attach an `sa_index` to would
produce a driver that advertises IPsec offload and silently encrypts nothing - and the repository's
rule is to claim only what has been tested.

## What is not here

- **Whether `IPSEC_OFFLOAD` is in this kernel.** Section 0. One command, unanswered.
- **No mapping design for `drv_spi` to the coprocessor's `saidx`.** The two numbering spaces are
  named and the need for a two-way map is named; the table, its locking and its size are not
  designed.
- **Nothing about the input direction.** `ipsec_accel_sa_install_input_p` appears in the header
  (`sys/netipsec/ipsec_offload.h:55`) and is not in the method table; how an inbound association is
  installed, and how `ipsec_accel_fill_xh_p` relates to the receive path, was not followed.
- **No reading of `mlx5`'s implementation.** `dev/mlx5/mlx5_accel/mlx5_ipsec_offload.c`
  (`sys/conf/files:5081`) is the one real hardware driver using this surface, and reading it is
  the obvious next step for anyone implementing the six. It was not read here.
- **`if_hwassist`'s return value is not characterised.** It is assigned to `*hwassist` at
  `sys/netipsec/ipsec_offload.c:945` and what the caller does with it was not traced.

## What this needs that is not in the repository

1. **Is `IPSEC_OFFLOAD` set in the appliance's kernel configuration?** `sysctl
   net.inet.ipsec.offload` answers it. This decides whether the page is a design or an explanation
   of why there is nothing to design.
2. **Does the coprocessor validate `sa_rev_num` on the from-wire path, or only when programming?**
   The same question #231 asks about the firewall revision, for the same reason: it decides whether
   an association's revision is a fence or bookkeeping.
3. **What is in `ipsec_fpop.c` around `sadb_hw_entry_get`'s fourth check?** The three that pass and
   the one that does not are already identified (#185); the exact comparison is the last unread
   piece.
4. **Does the vendor's own host ever call anything resembling a policy install?** The RPC
   enumeration has no policy command, which suggests selection is entirely in the flow. If
   `usfp_ipsec.c` has a policy path that this project has not seen, `if_spdadd` has a real
   counterpart and this page's reading of it is wrong.

**Lesson.** A capability bit is a promise that a pointer has already been stored, and nothing in
this subsystem checks that promise - eleven dereferences of `if_ipsec_accel_m` and not one NULL
test - so the order of two stores at attach is the whole of what stands between a working offload
and a panic on the first association anyone adds.
