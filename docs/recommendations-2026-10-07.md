# What the next session should do, and in what order

**Written 2026-10-07 from the repository alone**, after reading the 47 commits that landed between
2026-10-04 and 2026-10-07, the five open issues that concern the XGS 3300, the fifteen that closed,
and two places in the driver. No appliance was attached. Everything here is a recommendation, and
each one says what it rests on, so that a session with the appliance can check the premise before
spending the time.

The order is by what moves the most for the least, with the cheapest premise-check first.

## 1. Install the current module, then re-take the light soak

The appliance is running the module from #273 (`cd6d2a0`). Seven commits since then change
`contrib/octep/`, and the appliance has none of them:

| commit | what the appliance is therefore without |
|---|---|
| #282 `38abb8a` | both directions re-attached at once; `SA_ADD` retried when the far side defers it |
| #283 `27d2253` | the NetAgent request as arguments, the reply as the caller's |
| #284 `8cf4eb4` | the second acknowledge measured rather than logged |
| #286 `2556063` | the microflow timeout as a setting |
| **#288 `3db28c1`** | **both of a connection's pf states marked sloppy, from both tuples** |
| #291 `9c595d7` | the handle is the index plus one, in the two places that send it |
| #295 `f3246ad` | IPsec through the kernel's offload contract, 1,441 lines |

The one that matters first is #288. The three-hour light soak recorded 167 pf state-mismatch drops,
and the module that recorded them predates the fix whose commit message says it took that counter
from 16,104 to 1 under heavy load. Whether the 167 are the same symptom at a lighter load is not
knowable from here. It is knowable from one soak on the new module, and that soak is the first thing
to run - it also checks every other row of this table at once.

**Premise to check first:** that the installed module really is #273. The handoff summary said so on
2026-10-07; nothing in the repository can confirm it. `kldstat -v | grep octep` and the stamp the
install writes will.

## 2. Bring the two pages the README points at up to the week

The README says the short version of the 3300's state is 85% and points at the weighted table in
`docs/families/octeon-tx-reference.md`, and says every number behind it is in
`docs/measurements/xgs3300.md`. Both pages were last changed in `727dcb9` on 2026-10-04 at 01:08 -
before every one of the 47 commits.

So the table has no row for flow acceleration and none for IPsec, which are the two largest things
that happened; its row 7, "resilience - recovery without a host reboot, soak testing, 30%", predates
a three-hour soak with zero refusals and an hour of heavy load whose every exit was recorded; and
its row 5, "performance, 40%", predates #249's ring change (a download from about 84 to 288 and
305 Mbit/s, the commit says) and the hour at a median of 353 Mbit/s in `docs/one-state-strict.md`.

Two honest ways to fix it, and the second is less work:

- re-weight the table and add the two rows, and move this week's numbers into the measurements page;
- or decide that the numbers now live in the per-topic pages (`a-frame-was-accelerated.md`,
  `one-state-strict.md`, `the-kernel-drives-the-coprocessor.md` and the rest) and change the two
  README sentences to say so, retiring the measurements page's claim to be where every number is.

Either is fine. What is not fine is a README that points a reader at a page three days older than
the thing it is describing, in a project whose house rule is that a stale claim is a wrong claim.

## 3. #293, which is where the tunnel's throughput is

#295 put every cipher operation of the lab tunnel on the coprocessor and measured that it bought
nothing: 358 against 355 Mbit/s, with the host working harder, because every packet still crosses
the host. The gain is where the plain forwarding gain was - frames that never reach the host - and
for tunnel traffic that means the microflow carrying `sa_index` and `sa_rev_num`, with the next hop
pointing at the tunnel's far end. The issue lays out the three parts.

The design decision that has to be made first, and that the issue leaves open: the guard from #290
refuses to accelerate a connection the kernel's security policy covers. #293 wants that refusal to
become the attach - the trigger looks the connection up in the SPD and the SA table at flow-making
time. That lookup already exists in `octep_ipsec.c` for the forward hook (`key_havesp`, then
`key_allocsp`, then `key_allocsa_policy`), so the question is not how to look it up but where the
result goes into the connection entry, and whether the far side's `FROM_WIRE_TO_IPSEC_ENCR` path
accepts a microflow made that way. One hand-programmed flow with `sa_index` set, on the existing
tunnel, before any trigger code is written, answers that - and #258 did exactly that for the
encryption counter, so the method is on record.

## 4. #164, which is a probe and not a driver change

The issue's own last comment narrowed it to one experiment: the primary SMBus controller has four
selectable ports, FreeBSD's `intpm` drives whichever one the firmware left selected, and the only
sweep on record of the other three was taken without aborting the stuck transaction - so it reports
`HOST_BUSY` four times and carries no verdict. The experiment: write PM register `0x02` through the
`0xcd6`/`0xcd7` index pair, read it back to prove the write took, and run the seven-bit scan on each
of the four values with a KILL and a status clear before each attempt. Only if all four refuse
`0x60` does it become driver work on the auxiliary controller.

It is cheap, it needs the appliance, and it is the only peripheral on the page that needs nothing
from the coprocessor. Do it on the same visit as item 1.

## 5. #165, the keypad

The display half shipped in `os-frontpanel` and the issue says exactly what remains: the key map is
copied from the XG 330 rev 2 and its own `confirmed` field says it is a guess on this model. One
person pressing six keys with the daemon's debug on settles it. Same visit.

## 6. #279: nothing to do, and that is the recommendation

Two TLS decryption failures, one with no flow programmed, reproduced never, and a full day of load
on 2026-10-07 - eight consecutive gigabyte downloads with one SHA-256 - did not bring it back. The
issue has its closing criterion written in: a month of the heavy soak's exits without it. Keep
recording the exits. Do not spend a session on it.

## 7. Three things in the tooling, each small

**`backticked()` still reads identifiers only.** #285 fixed a real gap - a sentence that wraps put
the symbol on one line and the citation on the next, and that citation was never checked - and the
confirmed count went from 2 to 41 of 130. But the function at `tools/check-doc-references.py:88`
is unchanged: a quoted phrase has spaces and a path has a slash, and both are rejected, so a
citation whose only anchor is a quoted sentence is skipped rather than checked. The two citations
of `docs/rpc.md:145` in `docs/lif-provisioning.md` - at lines 136 and 303 - are the test case: the
quoted words are at that line, and the checker does not look. Accepting a quoted phrase as an
anchor, compared literally against the lines around the cited one, closes it. This is the citation
form `CONTRIBUTING.md` asks for, so it is odd that the checker discards exactly that.

**`checks.yml` pastes three values into shell source**, at lines 113, 114 and 118 - `base`,
`before`, and `${{ github.sha }}` in the `check-attribution.py` call. All three are commit SHAs and
not exploitable. They are the same shape that killed the first build in `xgs-image-builder` on
2026-10-04, on a commit subject with an apostrophe in it, and that repository now has
a checker, `check-no-interpolation.py` under its own `tools` directory, refusing the shape with
no exceptions. Passing the three through
`env:` is three lines; porting the checker is one file.

**#249 changed two constants in one step** - `OCTEP_DP_OQ_DESCS` to 1024 and the interrupt
coalescing to 32 packets / 50 us - and its own message says the gain is not attributed between
them and separating them is two builds that have not been done. Do the two builds when a build is
being made anyway; the answer decides whether 4096 is ever worth its 51 MiB.

## 8. Review the driver that landed this week

6,228 lines were added to `contrib/octep/` in three days, by one person, measured on one appliance,
and reviewed by nobody else. This page checked two things and found both sound:

- The NetAgent transaction is serialised the way `docs/nwa-transaction-lock.md` asked. On entry to
  the wait, `mtx_assert` (`contrib/octep/octep_nwa.c:193`).
  The sleep is `msleep` on the busy channel (`contrib/octep/octep_nwa.c:222`),
  so it drops and reacquires atomically and can be woken.
  The gate spins on `nwa_busy` (`contrib/octep/octep_nwa.c:483`),
  and the release is a `wakeup` on the same channel (`contrib/octep/octep_nwa.c:501`).
- Every call into pf, the routing table or the SADB runs with a vnet set. The sweep and the drain are
  inside `CURVNET_SET` at `contrib/octep/octep_dp.c:4569`; the receive termination has its own at
  `contrib/octep/octep_ipsec.c:542`; the remaining callers are sysctl handlers, which run in a
  process context that has one. #292 was the one that did not, and it took the appliance down.

What was not looked at, and should be, in this order: the error paths of `octep_ipsec.c` after
`key_allocsa` (`key_freesav` on every exit, and no key byte left in a buffer that is freed - #292
was a fault in exactly that function); every place a coprocessor handle is sent, since #291 changed
two and the rule is "index plus one" everywhere; the forward hook's handling of fragments, expiring
packets and the MTU after the tunnel's overhead is added, which #295 says it passes to the kernel
untouched; and the 1024 and 256 limits of #273 at their boundaries.

## 9. The image, which has never been booted

`xgs-image-builder` produced its first complete image on 2026-10-04 - run 37202729674, eleven
minutes, a 687 MB artifact named `xgs-opnsense-26.7-serial-115200`. **It expires on 2026-10-11.**
Nothing in that repository has been written to an appliance. Burning it to a stick and watching the
serial console at 115200 is the one test that repository has been waiting for, and it is the kind
that is better done before the artifact expires than after it has to be rebuilt.

## What this page is not

It is not a measurement, and nothing in it was tested. Items 1, 3, 4, 5 and 9 need the appliance;
items 2, 7 and 8 need only the repository. The handoff summary this page checked against was itself
stale in three places by the time it was read - it called #283 and #284 drafts when both were
merged, and put `main` twelve commits behind where it was - so the next session should read `git
log origin/main` before reading this.
