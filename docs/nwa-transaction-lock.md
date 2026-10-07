# The NetAgent transaction is not serialised, and `npuep` already fixed it

[#224](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/224) says `octep`'s NetAgent request
block is staged and read outside the lock, so two callers can interleave. That is right, and the
scope is larger than the staging block: **the window transaction itself is unserialised**, because
the mutex is dropped in the middle of the wait.

This page is the design of the fix, written before any of it was built. Citations name the
function, then the literal expression, then the file and line.

## Status

The four changes of section D are built, in two pull requests:

| change | where | state |
|---|---|---|
| 1. the wait sleeps on a channel, with the all-ones and detach exits | #235 | merged, on the hardware since |
| 2. `nwa_busy` gates the transaction | #235 | merged, on the hardware since |
| 3. the request block becomes arguments | #283 | built on the appliance, not yet installed |
| 4. `nwa_last_*` stays as the operator's record and nothing else reads it | #283 | built on the appliance, not yet installed |

After #283 the one function is `octep_nwa_request()`
(`contrib/octep/octep_nwa.c:524`): op, sub, port and the two payload words are arguments, and the
reply is copied into the caller's `struct octep_nwa_reply` (`contrib/octep/octep.h:2687`) before the
mutex is dropped. The five port functions call it with a local reply. The `nwa.request` sysctl is
served by `octep_nwa_do_request()` (`contrib/octep/octep_nwa.c:699`), which reads the staged `nwa.*`
fields under the lock and passes them on, so the staging block exists for the operator alone. The
table in section B is kept as it stood when this was written, with the citations moved to where the
functions are now.

**Tested on the appliance, 2026-10-07**, the case section B says had no test: three hundred hand
requests through `nwa.request` in four seconds (`GET` of the link state for port tag 1) with the
link poll running its own transactions underneath. `nwa.commands` 849 to 1153, `nwa.timeouts` 0
before and after, `nwa.releases` unchanged, every port's link state the same before and after,
`nwa.last` showing the hand request's op, sub and port with a one-word reply. Four of the three
hundred reads of `nwa.last` showed the poll's transaction instead of the hand one: that is the
operator's record being the record of the LAST transaction, which is what it is now documented
to be, and the poll and the hand tool interleaving on the window exactly as the `nwa_busy` gate
allows them to. The requests themselves cannot mix any more, because there is no longer a place
for them to mix in.

Sections A to D below are the design as written, before #235 and #283.

## The short answer

`npuep` has had the fix since it first carried a bridge, and it is two things and one field:

1. **the arguments are arguments**, not fields in the softc, so there is nothing to stage and
   nothing to race over;
2. **the lock belongs to the caller**, asserted rather than taken;
3. and **one field, `busy`**, with `msleep`/`wakeup`, which serialises the window across the mutex
   being dropped in the wait.

The issue proposes "a request mutex, or an `sx` around stage-issue-read". That closes the staging
race and leaves the window race open. The shape below closes both and adds no second lock.

## A. What is actually shared, and it is three things not one

### The request block

    uint32_t		 nwa_req_op;           contrib/octep/octep.h:1668
    uint32_t		 nwa_req_sub;
    uint32_t		 nwa_req_port;
    uint32_t		 nwa_req_param;
    uint32_t		 nwa_req_param2;       contrib/octep/octep.h:1672

Filled by each caller before the call, with no lock. `octep_nwa_do_request()` reads four of them
under the mutex at `op = sc->nwa_req_op;` (`contrib/octep/octep_nwa.c:412`) and the fifth later, at
`rq[OCTEP_NWA_RQ_PAYLOAD / 4 + 1] = sc->nwa_req_param2;` (`contrib/octep/octep_nwa.c:522`) - still
under the mutex, but the *writes* that put them there were not.

### The result block

    uint32_t		 nwa_last_op;          contrib/octep/octep.h:1657
    ...
    uint32_t		 nwa_last_status;      contrib/octep/octep.h:1664

Written under the mutex at `sc->nwa_last_op = op;` (`contrib/octep/octep_nwa.c:524`) and by
`octep_nwa_xfer()` (`:528`), and then read by every caller *after* the mutex has been dropped at
`mtx_unlock(&sc->mtx);` (`contrib/octep/octep_nwa.c:532`).

The bring-up script already found what that costs in practice, for a different reason:
*"nwa.last is the driver's one last-reply buffer, so a request the firmware answered with an error
leaves in it whatever was there before - and two payload words being present says only that some
transaction once put them there"* (`src/opnsense/scripts/octep/bringup.sh:295`). One buffer, and
nothing ties a reading of it to the request that produced it.

### And the window itself, which the issue does not name

`octep_nwa_wait()` (`contrib/octep/octep_nwa.c:187`) drops and retakes the mutex around each tick:

```c
mtx_unlock(&sc->mtx);                 /* contrib/octep/octep_nwa.c:194 */
pause("octepnwa", hz / 100);
mtx_lock(&sc->mtx);
```

So `sc->mtx` is **not held for the duration of a transaction**. A second caller can take it during
any of those gaps, walk through `octep_nwa_do_request()`'s validation, call `octep_nwa_release()`
and write a fresh request into the window while the first transaction is still waiting for its
reply. There is no guard: `nwa_busy` does not exist anywhere in `octep`.

That is a different and worse race than the staging one. The staging race corrupts a request. This
one corrupts the window, which both parties then read.

## B. The five callers, and why it has not been seen

All six are in `contrib/octep/octep_nwa.c`; the second column is the line inside each one that
issues the transaction - before #283 by staging the block and calling `octep_nwa_do_request`, now
by calling `octep_nwa_request` with arguments (the sysctl still goes through the staging wrapper).

| caller, at its definition | issues at line | context |
|---|---|---|
| `octep_nwa_port_mac` - `contrib/octep/octep_nwa.c:737` | 665 | the bring-up path |
| `octep_nwa_port_speed` - `contrib/octep/octep_nwa.c:782` | 710 | the link poll |
| `octep_nwa_port_filter` - `contrib/octep/octep_nwa.c:812` | 740 | the link poll |
| `octep_nwa_port_promisc` - `contrib/octep/octep_nwa.c:838` | 766 | the link poll |
| `octep_nwa_port_link` - `contrib/octep/octep_nwa.c:853` | 781 | the link poll, `taskqueue_thread` |
| `octep_sysctl_nwa_request` - `contrib/octep/octep_nwa.c:714` | 645 | a user process, via `nwa.request` |

The poll's four cannot overlap each other: one task, one thread, issued in sequence. So reaching
this needs a second thread, which in practice means writing `dev.octep.0.nwa.request` while the poll
runs, or the bring-up script running against a live poll - and `bringup.sh` is documented as
something to run twice, *"by hand afterwards when something is being changed"*.

**That is why it has not been seen, not why it is safe**, and the issue says so. Worth adding: the
bring-up path is a real second thread, not only the diagnostic sysctl, and it issues NetAgent
requests in a loop for twelve ports while the link poll is running.

## C. The fix, from `npuep`, which solved all of it

### One field, and the wait that makes it work

    int			 busy;		/* a transaction is in the window */
    - contrib/npuep/npunwa.c:125

Used as a gate around the whole transaction:

```c
static int
npunwa_command_n(struct npunwa_softc *sc, uint32_t op, uint32_t sub, uint32_t port,
    const uint32_t *pl, int npl, uint32_t *reply, int nreply)
{                                        /* contrib/npuep/npunwa.c:360 */
	mtx_assert(&sc->mtx, MA_OWNED);  /* :365 */

	while (sc->busy) {               /* :367 */
		if (!sc->running)
			return (ENXIO);
		msleep(&sc->busy, &sc->mtx, 0, "npunwaq", hz / 10);
	}

	sc->busy = 1;                    /* :373 */
	err = npunwa_transact(sc, op, sub, port, pl, npl, reply, nreply);
	sc->busy = 0;
	wakeup(&sc->busy);               /* :376 */

	return (err);
}
```

and its own comment says why it exists before it was ever needed:

> *"The window holds exactly one transaction, and now that the wait above releases the mutex there
> is a gap in which a second caller could start one. There is only one caller today - the task
> below - but a mailbox that is single-writer by luck rather than by construction is not worth the
> next person's afternoon."*
> - `contrib/npuep/npunwa.c:356`

`npuep` wrote that with **one** caller. `octep` has six.

### Why `msleep` and not `unlock`/`pause`/`lock`

This is the part that is not merely tidier, and `npuep` paid for it:

> *"msleep releases the mutex for the duration of the sleep. pause does not, and that difference is
> the whole of "panic: sleeping thread holds npunwa": any other thread that then blocks on this
> mutex walks into propagate_priority(), finds the owner asleep, and takes the machine down. It took
> an unload racing the link poll to show it, which is to say it took four hours of not showing."*
> - `contrib/npuep/npunwa.c:185`

`octep` does not have that panic: it unlocks by hand before `pause` at
`mtx_unlock(&sc->mtx);` (`contrib/octep/octep_nwa.c:194`). But the two are not equivalent in the
other direction. `msleep` drops and reacquires **atomically and on a wait channel**; unlock-pause-lock
drops, sleeps unconditionally for the full tick, and reacquires. Three consequences:

1. **The gap is a real window** that nothing can be gated on, which is section A's third race.
2. **Nothing can wake it early.** `npuep`'s teardown does
   `wakeup(&sc->busy);` (`contrib/npuep/npunwa.c:1027`), commented *"whatever is mid-wait gives up
   now"*, and the mid-wait caller returns at once. `octep` sleeps out its tick regardless, so a
   detach waits for the full remaining timeout - up to four seconds
   per outstanding request on a path whose cost is already recorded as *"about 12 ms when the far
   side is healthy and up to four seconds when it is not"* (`docs/netagent.md:166`).
3. **It cannot notice that it should stop.** `npuep`'s wait checks two things each tick that
   `octep`'s does not: `if (v == 0xFFFFFFFFU)` (`contrib/npuep/npunwa.c:178`), the endpoint having
   stopped decoding, and `if (!sc->running)` (`:180`) - *"detach is waiting; do not make it wait"*.

### And the other half: pass the arguments

`npuep`'s transaction takes everything as parameters -
`npunwa_transact(struct npunwa_softc *sc, uint32_t op, uint32_t sub, uint32_t port,`
(`contrib/npuep/npunwa.c:337`), with the payload as `const uint32_t *pl, int npl` and the reply as
`uint32_t *reply, int nreply`. There is no request block in its softc and no result block either.
**The staging race cannot be written.**

This is exactly what #224's own lesson asks for - *"Either the lock belongs to the caller or the
arguments do"* - and `npuep` does both: the arguments are arguments **and**
`mtx_assert(&sc->mtx, MA_OWNED)` (`contrib/npuep/npunwa.c:365`) puts the lock in the caller.

## D. The patch sketch

Design. Not built, not compiled, not measured. Four changes, in dependency order.

**1. `octep_nwa_wait()` sleeps on a channel instead of unlocking by hand.**
`contrib/octep/octep_nwa.c:187`. Replace the three lines at `:194` with
`msleep(&sc->nwa_busy, &sc->mtx, 0, "octepnwa", hz / 100)`, and add the two early exits `npuep`
has: the `0xFFFFFFFF` endpoint check and a `!sc->nwa_running` check so detach is not made to wait.
This change alone is a strict improvement and depends on nothing else.

**2. Add the one field** - `int nwa_busy;` beside the existing NetAgent state in
`contrib/octep/octep.h:1651` - and gate the transaction on it, in the shape at
`contrib/npuep/npunwa.c:367`. `wakeup(&sc->nwa_busy)` on teardown, as at
`contrib/npuep/npunwa.c:1027`.

**3. Turn the request block into arguments.** `octep_nwa_do_request(sc)` becomes
`octep_nwa_xact(sc, op, sub, port, param, param2, reply, nreply, &status)` or similar, and
`contrib/octep/octep.h:1668` through `:1672` are deleted. The five internal callers pass what they
already set; each of them currently writes five fields and then calls, so each becomes one call.

**4. Keep `nwa_last_*` as a diagnostic, and only that.** The `nwa.request` sysctl and `nwa.last`
exist to send a request by hand and read what came back, and that is worth keeping - it is how
`bringup.sh` reads a port's MAC, and the repository names it as the way to find out whether a
firmware refuses an attribute (`nwa.request`, `contrib/octep/octep_dp.c:4433`). So the sysctl path keeps writing the
`nwa_last_*` block under the lock, as *the last transaction*, which is what it is called and what it
honestly is. The five internal callers stop reading it and take their results through arguments. The
race disappears not because the block is locked but because **nothing that matters reads it any
more**.

### Which lock, explicitly

Not a new mutex and not an `sx`. `sc->mtx`, held by the caller across the whole transaction, with
`nwa_busy` as the serialiser.

The reasoning against the alternatives:

- **`sc->mtx` alone cannot do it**, and the issue is right about that: it is dropped inside the
  wait, which is where the four seconds go. But the conclusion is not "use a different lock" - it is
  "the mutex protects the flag, and the flag serialises the transaction". That is what a condition
  variable is, and `msleep` on an address is FreeBSD's cheapest form of one.
- **A second mutex** would have to be held across `octep_nwa_xfer()`, which sleeps - so it would be
  a sleepable mutex held across a sleep, and it introduces a lock order (`nwa_lock` before
  `sc->mtx`) that every future caller has to get right.
- **An `sx`** can be held across a sleep, and it still only covers callers that take it. The window
  is also touched by `octep_nwa_probe()` and `octep_nwa_release()`, the latter defined at `contrib/octep/octep_nwa.c:403`,
  which are reached from inside `octep_nwa_do_request()` and would need the same discipline. A flag
  checked by everything that touches the window is narrower and harder to get wrong.

## What is not here

- **Not built and not measured.** No claim that this compiles, and no number.
- **No reading of `octep_nwa_xfer()`'s internals.** The design assumes it is the only thing that
  writes the window during a transaction. That was not verified line by line, and it is the
  assumption the `busy` flag's placement rests on.
- **Nothing about `octep_nwa_release()`.** It is called at `(void)octep_nwa_release(sc, 1);`
  (`contrib/octep/octep_nwa.c:650`) before every request, and #227 is open about how long it waits.
  **That issue needs a measurement on a healthy mailbox and is deliberately not answered here.**
  (It has one now: [the-second-acknowledge.md](the-second-acknowledge.md) - tens of microseconds,
  and the wait moved inside the transaction.)
  Whether `release` should be inside or outside the `busy` gate depends on what it does, and it is
  the one ordering question this sketch leaves open.
- **No count of how long the bring-up path and the poll actually overlap.** Section B argues the
  overlap exists; it does not measure it.

## What this needs that is not in the repository

Nothing. This one is answerable entirely from the two drivers in this tree, which is why it is the
only page in this set with no questions for the vendor source - and it is worth saying, because it
means the cost of fixing it is review time rather than another round of reading binaries.

The one thing it needs from the appliance is the same thing every change here needs: a build on the
appliance against its own kernel, and a run with the link poll and a hand-issued `nwa.request`
overlapping on purpose, which is the case that currently has no test.

**Lesson.** A sibling driver's scar is cheaper than your own: `npuep` recorded a panic, a four-hour
hunt and a race it fixed while it still had one caller, in comments beside the code that fixes them -
and `octep` grew to six callers of the same mailbox without any of it, because nothing makes a reader
of one driver open the other.
