# The LIF table: what has to be created, in what order, and why one entry was found

Step 1 of [#211](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/211) says a LIF is needed per
forwarding interface, and that `LO_LIF_READ` returns exactly one populated entry, LIF 0. This page
is the reading of that claim.

**This page was written without the appliance.** No module was built, no command was sent, no
number was measured; everything was a citation into this repository or a proposal marked as one.

**Sections A and B have since been measured, and both hold.** The read this page asked for was run
on 2026-10-04 and is recorded in
[#211](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/211#issuecomment-5982549591): twelve
populated entries at the indices below, each carrying its own port's address. What is still
proposed - sections C, D and E - is still marked as proposed, and section C is now known not to
arise on this board.

Citations name the function or script, then the literal expression, then the file and line, so the
site is findable by searching for the expression if the line has moved.

## The short answer

**The premise is wrong in two independent ways, and this repository already contains the correction
for both.** Twelve LIFs are installed at boot, not one. And the read that found one could not have
found more than one, because it swept the wrong indices.

Neither finding needed the appliance to reach. Both were confirmed by one afterwards, and the
measurement is at the end of this page.

## A. Twelve LIFs are already installed, by the bring-up script

`bringup.sh` installs a LIF and a port binding for every front port. Twice, in two loops.

For the two SFP cages, at `sc ${S}.rpc.cmd=3; sc ${S}.rpc.post=1`
(`src/opnsense/scripts/octep/bringup.sh:340`) followed by `cmd=5` on the next line. For the ten
ports behind the switch, the same pair at
`sc ${S}.rpc.cmd=3; sc ${S}.rpc.post=1` (`src/opnsense/scripts/octep/bringup.sh:398`).

Command 3 is `LIF_ADD_UPDATE` and command 5 is `PPORT_UPDATE`
(`contrib/octep/octep.h:1925`, `contrib/octep/octep.h:2139`). The flags are set once before both loops, at
`sc ${S}.rpc.lif_mtu=1500 ${S}.rpc.lif_fwd=2 ${S}.rpc.lif_mask=255`
(`src/opnsense/scripts/octep/bringup.sh:249`).

That work landed on 2026-10-01 in `e4871f7`, *Bring the front ports up at boot, and give them a link
state so they are configured* (#160). **The "one populated entry" reading was written on 2026-10-03,
two days later**, in `e498c11` (#221). So the measurement was taken against a system whose
bring-up had been installing twelve for two days.

### Which index each one lands on

The LIF table is addressed `(iface_id << 12) | vlan`, which the driver writes at
`le32enc(p + 0, ((uint32_t)(sc->rpc_lif_iface & 0x7f) << 12) |`
(`contrib/octep/octep_rpc.c:425`), and which
`docs/families/octeon-tx-rpc.md:540` confirms four independent ways.

Interface units are handed out in `dp.if_add` order, at
`if_initname(ifp, "oxp", sc->dp_nif)` - `contrib/octep/octep_dp.c:2946`. The bring-up script adds
the two cages first and then the ten switch ports, so:

| interface | front port | `iface_id` | LIF index |
|---|---|---|---|
| `oxp0` | cage, tag `0x0001` | 10 | 40960 |
| `oxp1` | cage, tag `0x0002` | 11 | 45056 |
| `oxp2` | switch port 1, tag `0x8100` | 0 | **0** |
| `oxp3` | switch port 2, tag `0x8200` | 1 | 4096 |
| `oxp4` .. `oxp11` | switch ports 3 to 10 | 2 .. 9 | 8192 .. 36864 |

The cages take 10 and 11 because those are their lifports in the board file; the ten switch ports
take `p - 1`. Both loops in `bringup.sh` set `rpc.lif_iface` accordingly.

## B. The read could not have found more than one

**The twelve entries are 4096 apart. A sweep of indices 0 to 34 contains exactly one of them.**

The reading in question swept the low indices: *"at indices 0..34"*
(`docs/families/octeon-tx-crypto-path.md:226`), and reported
*"One entry, and its MAC is `...f5:02`, which is **oxp2** - a port with no cable in it. The interface
the traffic actually arrives on, oxp3, has no LIF at all"*
(`docs/families/octeon-tx-crypto-path.md:639`).

Put the table above beside that sentence:

- the one entry found is at index 0, and index 0 is `oxp2`. **The MAC it carries is the MAC the
  reading says it carries.** The entry is not a leftover; it is the correct entry for that port.
- `oxp3` is `iface_id` 1, so its LIF is at index **4096**. It was never read. "No LIF at all" is a
  statement about indices 0 to 34.

So the finding is consistent with all twelve being present, and inconsistent with none of them. One
entry in that range is what a correctly provisioned table looks like when you read that range.

### The table cannot be swept contiguously, which is why this is easy to get wrong

A read's answer is bounded by `OCTEP_RPC_DATA_MAX_SIZE` (`contrib/octep/octep.h:1875`), which is
4096 bytes, less the eight-byte response header and the four-byte done magic. At twelve to sixteen
bytes per entry that is a few hundred entries at most, against a span of 0 to 45056. **There is no
range that contains all twelve.** They have to be probed at the twelve computed indices, one read
each, or in twelve small ranges.

`rpc.s_index` and `rpc.num_entries` are plain sysctls - `"first index wanted"`
(`contrib/octep/octep_rpc.c:1053`) and `"how many entries"` (`:1057`) - so the range is whatever the
reader types, and nothing in the driver or the request computes an index from an interface number.

### This project has already made this exact mistake once, on the other family

On ARMADA, recorded at the time:

> *"**The LIF table reads back empty** at entries 0 to 7 even though counter 37 says a LIF matches.
> The table is indexed by `iface_id << 12 | vlan`, per the vendor's own dump, so the entry is
> probably not where it was looked for."*
> - `docs/families/octeon-tx.md:1144`

And `docs/rpc.md` says it as a rule, in the imperative, for whoever reads next:

> *"`s_index` is a **LIF index**, not an interface number - `(iface_id << 12) | vlan`."*
> - `docs/rpc.md:167`

Both were written before the OCTEON TX reading. The rule was known, recorded twice, and the sweep
was done on interface-sized numbers anyway.

## C. A second cause was possible, and it did not happen

> **Measured: this does not occur on this board.** The twelve entries were present and correct when
> read, so nothing had cleared them. The section is kept because the mechanism is real on the other
> family and because it is what to look at if the table is ever found empty later - not because it
> is happening here.

### Why it was worth suspecting

Even with the read corrected, the entries may genuinely not be there - and `npuep` found out why,
the expensive way.

> *"The coprocessor's own userspace fastpath starts **in response to this host's handshake**, and as
> part of its startup it zeroes the entire logical-interface table - about thirteen seconds after
> the handshake on this board. The driver opens this channel a fraction of a second after that same
> handshake, so its first pass writes into a table that is about to be cleared. Every command is
> accepted and answered `rc 0`, and nothing survives."*
> - `docs/rpc.md:145`

And the detail that makes it look like something else entirely:

> *"The port bindings **do** survive, because the fastpath maps those two tables and clears neither.
> That asymmetry is what made it look like a driver bug."*
> - `docs/rpc.md:152`

So after the clear, `PPORT_UPDATE`'s two tables are intact and the LIF table is empty. A tag still
resolves to an interface; that interface has no LIF behind it. Which is a precise description of
`FPCNTR_FROM_WIRE_DROP_LIF_LU_NULL` and the four gates beside it
(`docs/families/octeon-tx-rpc.md:21`).

`npuep`'s answer was to stop believing the acknowledgement:

> *"So programming runs on its own thread - not a callout, because every command waits for an answer
> and a callout may not sleep - and it believes only the read-back."*
> - `docs/rpc.md:156`

### `octep` does none of that

Three things are missing, and they compound:

1. **The result is never read.** `bringup.sh` posts and moves on - `rpc.post=1` at
   `src/opnsense/scripts/octep/bringup.sh:340` and `:398`, with no read of `rpc.last` after either.
   Every other far-side call in that script checks its status; these two do not. The script's own
   cage loop says why that matters, at
   `# THE STATUS IS PART OF THE ANSWER. nwa.last is the driver's one last-reply buffer`
   (`src/opnsense/scripts/octep/bringup.sh:295`) - the same reasoning, applied to NetAgent and not
   to RPC.
2. **There is no read-back.** `LIF_ADD_UPDATE` in `octep` is reachable only from the sysctl path; the
   driver has no LIF programming thread and nothing re-checks the table.
3. **It happens once.** `bringup.sh` runs from `08-octep` at boot and by hand, and its LIF loops are
   straight-line.

Whether the single install wins the race depends on how long the coprocessor's fast path took that
boot. the bring-up waits for `reconfig_done 1` with
`: ${FASTPATH_WAIT:=140}` (`src/opnsense/scripts/octep/bringup.sh:39`) and then for NetAgent with
`: ${NWA_WAIT:=40}` (`src/opnsense/scripts/octep/bringup.sh:46`), so the LIF loops run some tens of
seconds after the handshake - sometimes after the
thirteen-second clear and sometimes not. **That is a race whose outcome varies by boot**, which is
the worst shape for a thing nobody reads back.

## D. What would have to be created, if the table were ever found empty

> **Not work to do now.** The table is populated and correct, so nothing below needs building for
> step 1 of #211. It stands as the design for the case section C describes, should it ever arrive.

### The two commands, and nothing else

> *"Two commands per front port, and nothing else - there is no enable bit, no start command, no
> queue index anywhere in either of them."*
> - `docs/rpc.md:109`

`LIF_ADD_UPDATE` (3) then `PPORT_UPDATE` (5), in that order. `bringup.sh` already gets the order
right and says why: *"The logical interface first, then the tag that resolves to it - the vendor's
order, from `usfp_netdev_mv.c`, which adds the LIF and only then updates the port tables. Posting
the tag first leaves a window in which a frame off the wire resolves to an interface that has no
usable logical interface behind it yet"* (`src/opnsense/scripts/octep/bringup.sh:335`).

### Creating is not idempotent

> *"Creating is **not idempotent** - `update_mask 0x00FF` on an entry that is still valid is refused
> with `rc 1` - so the driver reads before it writes."*
> - `docs/rpc.md:139`

`octep` carries the same rule in its own header, measured rather than quoted: a new entry needs the
full mask and an existing one a partial mask, with `OCTEP_LIF_M_ALL` as 0xff
(`contrib/octep/octep.h:1354`), and `bringup.sh` passes `lif_mask=255` unconditionally
(`src/opnsense/scripts/octep/bringup.sh:249`). So **a second run of `bringup.sh` against a surviving
table is refused `rc 1` on every port** - and nothing notices, because nothing reads the result.
That is not a defect today only because the first run's entries are usually gone.

### Proposed order, and it is a loop rather than a sequence

Untested, and proposed:

1. Handshake completes. Fast path starts.
2. Fast path zeroes the LIF table, about thirteen seconds later on `npuep`'s board. Unmeasured on
   this one.
3. For each forwarding interface: read index `(iface_id << 12) | vlan` with `LO_LIF_READ` (37).
4. If the entry is absent, `LIF_ADD_UPDATE` with mask 0xff. If it is present but wrong, update with
   a partial mask. If present and right, do nothing.
5. `PPORT_UPDATE` for the tag, after the LIF.
6. Read back. Believe only the read-back.
7. Repeat on a slow period, forever - not a retry budget and not a one-shot.

Step 7 is the part that differs from a retry. The clear is not a one-time boot event to be waited
out: nothing in this repository establishes that the fast path clears the table only once. It
clears it *as part of its startup*, and the fast path can restart. A loop that re-reads and
re-installs costs two reads per interface per period when nothing is wrong, which is the same trade
the receive-filter reconcile already makes in this driver
(`contrib/octep/octep_dp.c:2604`).

### Where it should run

Not from `bringup.sh`. A shell loop cannot read a reply and branch on it without parsing
`rpc.last`, and the write gate is deliberately shut at the end of the script, at
`sc ${S}.rpc.allow_write=0` (`src/opnsense/scripts/octep/bringup.sh:438`) - so a reconciler driven
from outside would need that gate held open for the life of the machine, which defeats it.

That argues for the driver, on its own taskqueue, for the same reason `npuep` put it on a thread:
every command waits for an answer. This is the opposite conclusion to the flow reconciler
proposed in #229, and the difference is the gate: the LIF loop needs `rpc` writes
continuously, and `rpc.allow_write` exists precisely so that nothing writes the coprocessor's
forwarding state from outside the code that owns it.

## E. One policy decision that is not a bug, and must not be "fixed"

`npuep` and `octep` set the two LIF flags to opposite values, deliberately.

| | `npuep` | `octep` |
|---|---|---|
| `fwd_mode` | 1, L2 | 2, L3 - `lif_fwd=2` at `src/opnsense/scripts/octep/bringup.sh:249` |
| `offload_disabled` | 1 | 0 (left at the driver default) |

`npuep`'s reason is explicit: *"setting it hands every frame to this end rather than letting the
coprocessor route - which is the whole point of running the firewall on the host"*
(`docs/rpc.md:131`). That is correct for a driver that is not trying to accelerate anything.

`octep` wants the opposite, because #211 is about acceleration. So `offload_disabled` must stay 0 on
a LIF whose traffic is to be offloaded - and **that is the per-interface half of the same safety
property `FW_CFG_OFFLOAD` is the global half of.** A LIF with `offload_disabled` clear is an
interface whose frames the coprocessor may keep. It belongs under the same rule: set by the code
that maintains the flows, cleared on detach.

The `fwd_mode` choice has a consequence worth stating because it already cost a day. In L3 the
destination-MAC check runs; in L2 it does not (`docs/rpc.md:133`). `octep` is in L3, so a LIF's MAC
is that port's hardware filter - which is exactly the failure the cage loop was fixed for on
2026-10-03: a LIF installed with `rpc.lif_mac` still at zero *"held a logical interface whose
address was 00:00:00:00:00:00 and matched nothing"*
(`src/opnsense/scripts/octep/bringup.sh:290`).

### And an MTU hazard this page will not assert

`bringup.sh` installs every LIF with `lif_mtu=1500`
(`src/opnsense/scripts/octep/bringup.sh:249`), while the NetAgent port table reports an MTU of
**9182** for every port (`docs/netagent.md:312`).

`docs/rpc.md` lists, for ARMADA: *"Silent killers, all of them DROP rather than punt: `fwd_mode == 0`,
`admin_disabled == 1`, and a frame longer than `mtu`"* (`docs/rpc.md:136`).

If that holds on OCTEON TX, every frame between 1501 and 9182 bytes arriving on a front port is
dropped by the LIF gate with no counter naming the LIF. **It is not asserted here**, because it was
read on the other family and never tested on this one, and because a 1500-MTU network never
produces such a frame. It is written down because the day something does - a jumbo path, a tunnel,
a VLAN-tagged 1504-byte frame - the symptom is a port that works for small frames and not large
ones, and that is a long way from the LIF table unless somebody wrote this sentence first.

## What is not here

Three things were missing when this page was written. The read it asked for was then run, on
2026-10-04, and settled two of them outright and a third by accident.

- ~~**No confirmation that the twelve entries exist.**~~ **Settled: they do.** Twelve reads at the
  computed indices, every one `rc 0x0000` with a populated entry. Three were cross-checked against
  addresses read independently from the interfaces the same day - indices 40960 and 45056 are the
  cages `oxp0` and `oxp1`, which `bringup.sh` installs as interfaces 10 and 11, and index 24576 is
  interface 6, `oxp8`. They agree exactly, which is what makes the decode below trustworthy rather
  than merely plausible.
- ~~**No entry-size resolution.**~~ **Settled: fourteen.** Every one of the twelve replies reported
  `payload 14 bytes` for `num_entries=1`, so `struct usfp_lif_entry` is fourteen bytes on this
  family, as `docs/families/octeon-tx-rpc.md:169` says and not the twelve in `docs/rpc.md:166`.
  That was not what the read was for; it came out of it because the reply states its own length.
  The first word decodes as six bytes of address followed by `0x05dc`, which is 1500 - the MAC and
  the MTU, in that order.
- **No measurement of the clear on this board**, and this one still stands. The ARMADA number is
  recorded as `about thirteen seconds after` the handshake, at `docs/rpc.md:145`, and it is that
  board's rather than this one's. What the read establishes is narrower than a
  measurement of the clear: the entries were present and correct at an uptime of ten hours, so
  nothing clears them *and leaves them cleared*. Whether the OCTEON TX fast path zeroes the table at
  startup, before the bring-up script installs them, is a different question and is not answered
  here - the evidence is consistent with a clear that happens before anything this project writes.
- **Nothing about egress.** Step 1 of #211 says a forwarding flow needs its ingress *and egress*
  interfaces to resolve. This page is about the table; which LIF an egress resolves through, and
  whether `nhop` is what carries it, is not addressed.

## What settled it, on the appliance

One read, cheap and changing nothing:

```sh
sysctl dev.octep.0.rpc.cmd=37
for i in 0 4096 8192 12288 16384 20480 24576 28672 32768 36864 40960 45056; do
    sysctl dev.octep.0.rpc.s_index=$i dev.octep.0.rpc.num_entries=1 dev.octep.0.rpc.e_index=$i
    sysctl dev.octep.0.rpc.post=1
    sysctl -n dev.octep.0.rpc.last
done
```

Twelve reads, each one entry. `LO_LIF_READ` is a read command, so `rpc.allow_write` is not needed
(`contrib/octep/octep_rpc.c:2010`).

Three outcomes were possible, and the first is the one that happened.

| what comes back | what it means |
|---|---|
| **twelve entries, each with its port's MAC** | **the table is fine, #211's step 1 is done, and the blocker is elsewhere** |
| one entry at index 0 and eleven absent | the clear in section C is real on this board, and the fix is the loop in section D |
| twelve entries but wrong MACs or flags | the installs land and the bring-up's values are wrong - check `lif_mac` ordering per port |

### What came back, 2026-10-04

| index | entry | index | entry |
|---|---|---|---|
| 0 | `…:02`, MTU 1500 | 24576 | `…:08`, MTU 1500 |
| 4096 | `…:03` | 28672 | `…:09` |
| 8192 | `…:04` | 32768 | `…:0c` |
| 12288 | `…:05` | 36864 | `…:0d` |
| 16384 | `…:06` | 40960 | `…:0a` |
| 20480 | `…:07` | 45056 | `…:0b` |

Each word decoded as six bytes of MAC followed by `0x05dc`, which is 1500. Three were cross-checked
against addresses read independently from the interfaces the same day: 40960 and 45056 are the two
SFP cages `oxp0` and `oxp1`, which `bringup.sh` installs as interfaces 10 and 11, and 24576 is
interface 6, `oxp8`. They agree exactly.

So **section A is confirmed and section B is confirmed**: twelve LIFs are installed, and the read
that found one could only ever have found one. The table was never sparse. #211's step 1 is struck,
and the measurement is recorded
[there](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/211#issuecomment-5982549591).

## What this needs that is not in the repository

The measurement answered the question this page was written to settle. These remain open, and two
of them matter less than they did.

1. **Does the OCTEON TX fast path zero the LIF table during its startup, and when?** The entries
   were present when read, so if it clears at all it had finished before that read - or it does not
   clear. `fp_state_init` and whatever calls `lif_fpop_init` in `usfp.elf` would say. Section C
   stands on the ARMADA precedent alone, not on anything observed here.
2. **Does it clear only at startup, or on any fast-path restart?** Only matters if the answer to 1
   is that it clears at all.
3. **Is the LIF MTU checked against the wire frame on this family?** Named as a hazard above and
   deliberately not asserted. Every entry read carried 1500.
4. **What is `struct usfp_lif_entry` on OCTEON TX?** The read decoded as six bytes of MAC followed
   by a two-byte MTU, which is enough to read the table and not enough to describe the struct.
   `include/lif_table.h` would say whether anything follows those eight bytes.

**Lesson.** A sweep of a table whose index is computed is a sweep of whatever the computation sends
you to, and this project wrote that rule down twice - once as a diagnosis on ARMADA and once as an
imperative in `docs/rpc.md` - and then read indices 0 to 34 of a table whose entries are 4096 apart
and recorded the result as a fact about the table.


## What was measured, and when

| | |
|---|---|
| 2026-10-04 | Written from the repository alone, with no appliance attached. |
| 2026-10-04 | The read at the end was run. Twelve entries, each with its port's own address and MTU 1500, at the indices this page computed. Sections A and B confirmed; section C shown not to arise; section D not needed. |

The page corrected a published premise before anything was measured, from what was already
committed. The measurement agreed with it. That order is worth keeping: the citation came first and
the instrument came second, which is why the null reading was recognised as a fact about the sweep
rather than about the table.
