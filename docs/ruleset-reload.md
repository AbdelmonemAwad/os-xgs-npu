# Where OPNsense reloads `pf`, and where a revision bump has to attach

Step 4 of [#211](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/211) says to issue
`FW_STATE_REV_SET` on every ruleset reload, because `fw_state_fpop_rev_set` invalidates the whole
flow table as a side effect and that is exactly the semantics a reload needs. This page is the
reading of where "every ruleset reload" actually is.

**Nothing here has been run.** No module was built, no command was sent, no number was measured.
Everything is a citation that can be opened, or a proposal marked as a proposal. The appliance was
not attached to the session this was written in.

Citations into other trees name the tree. `opnsense/core` is at `92974f0`; the kernel references are
`opnsense/src` `stable/26.7` at `083dc7025377`. Citations name the function, then the literal
expression, then the file and line, so the site survives a line moving.

**And a line has already moved.** Every `filter.inc` expression below was located on the appliance's
own installed copy - OPNsense **26.7.5**, `/usr/local/etc/inc/filter.inc` - and the three in the
loading path sit **two lines earlier** there than in `92974f0`:

| expression | in `92974f0` | installed 26.7.5 |
|---|---|---|
| `mwexecfm('/sbin/pfctl -f %s > %s', ...)` | 401 | **399** |
| `mwexecf('/sbin/pfctl -f %s', '/tmp/rules.debug.old');` | 424 | **422** |
| the `return;` that ends the failure path | 438 | **436** |

Every expression was found, every one in the function this page says it is in, and the offset is a
constant two across all three - so the page is right about `92974f0` and a reader working on the
appliance should search for the expression rather than count to the line. That is the whole reason
the citation form names the expression first.

## The short answer

There is **one** attachment point that covers every reload, it already exists, and it is a plugin
hook this package is already shaped to use: `<plugin>_firewall`, dispatched from
`plugins_firewall($fw)` inside `filter_configure_sync()`.

It fires *before* `pfctl -f` rather than after, and that turns out to be the correct direction of
error rather than a compromise.

The part that needs care is not the hook. It is that the bump is an `rpc` write, and the write gate
is deliberately shut.

## A. The reload chain, end to end

From the GUI or the API:

| step | where |
|---|---|
| `filter_configure()` defers to configd | `configd_run('filter reload');` - `opnsense/core` `src/etc/inc/filter.inc:108` |
| the configd action runs a script | `[reload]` / `command:/usr/local/etc/rc.filter_configure` in `actions_filter.conf` |
| which calls the real work | `filter_configure_sync(true, $event_arg != 'skip_alias');` - `src/etc/rc.filter_configure:40` |
| rules are generated, and plugins contribute | `plugins_firewall($fw);` - `src/etc/inc/filter.inc:189` |
| the ruleset is loaded | `$load_failed = mwexecfm('/sbin/pfctl -f %s > %s', ...)` - `src/etc/inc/filter.inc:401` |
| on failure, the previous ruleset is reloaded | `mwexecf('/sbin/pfctl -f %s', '/tmp/rules.debug.old');` - `src/etc/inc/filter.inc:424` |
| and the function returns early | `return;` - `src/etc/inc/filter.inc:438` |

`filter_configure()` itself says why it defers: *"Defer this to configd which will avoid this call on
bootup when this should not be triggered. The reason is that rc.bootup calls
filter_configure_sync() directly which does this too"* (`src/etc/inc/filter.inc:104`). And
`rc.filter_configure` guards the same case with `exit_on_bootup();`.

### `filter_configure()` is not the reload. It is one of ten.

This is the finding that decides the whole page. `filter_configure()` has **one** caller in the
tree. `filter_configure_sync()` has **ten**, in eight different files:

| caller | why it reloads |
|---|---|
| `src/etc/rc.filter_configure:40` | the configd action - GUI, API, `configctl` |
| `src/etc/rc.bootup:88` | *"apply default policy before interface setup"* |
| `src/etc/rc.bootup:92` | |
| `src/etc/rc.bootup:99` | |
| `src/etc/rc.reload_all:55` | |
| `src/etc/rc.newwanip:125` | **a WAN address changed** - a DHCP lease renewal |
| `src/etc/rc.newwanipv6:122` | the same, for IPv6 |
| `src/etc/rc.routing_configure:57` | a routing change |
| `src/opnsense/scripts/shell/setports.php:45` | the console port-assignment menu |

So "hook the reload" cannot mean wrapping a caller. Wrapping `filter_configure()` catches the GUI
and misses nine paths - including `rc.newwanip`, which on this appliance fires on every WAN lease
renewal, unattended, with the WAN being a coprocessor front port.

**A missed reload is a hole, not a slow path.** The whole purpose of the bump is to discard flows
the old ruleset permitted. A reload that bumps nothing leaves the coprocessor forwarding under a
ruleset that no longer exists - which is the failure section F of #229 is about, arriving by a
different route.

## B. The one hook that covers all ten

`plugins_firewall($fw)` is called from inside `filter_configure_sync()`
(`src/etc/inc/filter.inc:189`), so it fires on every one of the ten paths above by construction.

Its dispatch is discovery, not registration:

```php
function plugins_firewall($fw)                         /* src/etc/inc/plugins.inc:244 */
{
    foreach (plugins_scan() as $name => $path) {
        include_once $path;
        $func = sprintf('%s_firewall', $name);         /* src/etc/inc/plugins.inc:252 */
        if (function_exists($func)) {
            $func($fw);
        }
    }
    return $fw;
}
```

So a function named `xgs_firewall()` in `src/etc/inc/plugins.inc.d/xgs.inc` is called on every
ruleset reload, with nothing to register and nothing an OPNsense update can undo. That is the
property `xgs.inc` already relies on for its two existing hooks - *"`plugins_scan()` picks up every
file in this directory and calls the functions named after it, so everything here is discovered
rather than registered"* - and it already declines a third deliberately, noting that
`plugins_configure('ipsec')` exists and *"a hook that did nothing would read like a feature"*.

There is no `plugins_configure('filter')` anywhere in `opnsense/core`, and
`filter_configure_sync()` ends with no post-load plugin call at all - the last thing it does is
`configd_run('template reload OPNsense/Filter')` and `filter refresh_aliases`
(`src/etc/inc/filter.inc:459`). `plugins_firewall` is the only hook in the function.

And `rc.syshook` is not an option: the levels this appliance actually calls are facility, stop,
start, monitor, upgrade, import, early and carp, with no reload among them - recorded in
`src/etc/cron.d/octep`, which exists because a hook was once placed at a level nobody calls.

## C. It fires before the load, and that is the right direction of error

`plugins_firewall` runs at `src/etc/inc/filter.inc:189`; `pfctl -f` runs at `:401`. So a bump from
this hook discards the flow table while the **old** ruleset is still the live one.

Work through the window:

| | |
|---|---|
| before the hook | hardware holds flows the old ruleset permitted; `pf` sees nothing of them |
| hook bumps the revision | the table is invalidated; every frame now punts to the host |
| between hook and load | the host filters every frame with the **old** ruleset - correct, just slower |
| `pfctl -f` loads the new ruleset | the host filters with the new ruleset; still no hardware flows |
| the reconciler re-programs | flows reappear, now only for states the **new** ruleset permitted |

There is no moment in that sequence where the coprocessor forwards traffic under a ruleset that has
stopped permitting it. The cost is a window of host-rate forwarding whose length is however long
rule generation plus `pfctl -f` takes.

**A post-load hook would be worse**, and this is worth stating because it is the obvious thing to
want. Between `pfctl -f` and a post-load bump, the new ruleset is live and the hardware still holds
flows authorised by the old one. That window is exactly the hole. Firing early is not a compromise
forced by what hooks exist; it is the side to be on.

The failed-load path confirms it. On an error OPNsense reloads `rules.debug.old`
(`src/etc/inc/filter.inc:424`) and returns at `:438`. A hook that already fired has discarded the
flows for a ruleset that was then rolled back - which costs a re-reconcile and is otherwise
correct, because `pfctl -f` ran either way and the ruleset did change.

## D. What the hook has to tolerate

Three cases, all of which this hook will meet in normal operation:

1. **No device.** Three of the ten callers are in `rc.bootup`, and the first is before interface
   setup. `octep` may not be loaded, may have failed its kernel-stamp check
   (`src/opnsense/scripts/octep/bringup.sh:128`), or may be loaded with no coprocessor handshake
   yet. The hook must do nothing, quietly, and must not log once per reload forever - the same
   reasoning `octep_dp_filter_one()` records for a silent failure
   (`contrib/octep/octep_dp.c:2619`).
2. **Offload not enabled.** If `FW_CFG_OFFLOAD` is clear there are no flows, so there is nothing to
   invalidate and the bump is pure cost. `fw_cfg` has no read command, so the only thing available
   is what the host last asked for - stated plainly where it is surfaced today
   (`src/opnsense/scripts/xgs/status.py:201`).
3. **Reloads in bursts.** `rc.newwanip`, `rc.routing_configure` and the GUI can land within seconds
   of each other, and three of them happen during one boot. Each bump throws away the whole table
   (`docs/families/octeon-tx-crypto-path.md:330`), so a burst is a burst of full flushes. That is
   correct and cheap only because the table is empty between them.

## E. The real problem is not the hook. It is the write gate.

`FW_STATE_REV_SET` is command 0, at `#define OCTEP_RPC_CMD_FW_STATE_REV_SET` (`contrib/octep/octep.h:1298`), a two-byte request carrying the
revision at `le16enc(p + 0, (uint16_t)sc->rpc_fw_rev)` (`contrib/octep/octep_rpc.c:389`). The
driver's own comment says what it does: *"it calls mflow_fpop_invalidate_issue over the whole
table. Bumping this revision THROWS AWAY every offloaded flow, which is exactly what a ruleset
reload has to do, and is the reason the field exists"* (`contrib/octep/octep_rpc.c:385`).

But it is a write, and writes are gated. `octep_rpc_post()` refuses one unless the gate is open, at
`if (!octep_rpc_cmd_is_read(sc->rpc_cmd_num) && sc->rpc_allow_write == 0)`
(`contrib/octep/octep_rpc.c:306`). And the bring-up shuts that gate on the way out, at
`sc ${S}.rpc.allow_write=0` (`src/opnsense/scripts/octep/bringup.sh:438`), with its reason beside
it: *"the gate exists so that nobody writes the coprocessor's forwarding state by accident, and a
gate left open after the one job that needed it is not a gate"*
(`src/opnsense/scripts/octep/bringup.sh:244`).

So a PHP hook cannot simply post the command. It would have to:

    sysctl dev.octep.0.rpc.allow_write=1
    sysctl dev.octep.0.rpc.fw_rev=<n> dev.octep.0.rpc.cmd=0
    sysctl dev.octep.0.rpc.post=1
    sysctl dev.octep.0.rpc.allow_write=0

**That is the wrong shape and this page recommends against it**, for four reasons that are not
style:

- It opens the gate on a firewall's forwarding state from a PHP hook, on every reload, including
  three times per boot. The gate's whole purpose is that this does not happen.
- `allow_write` is sticky and shared. A reload landing while something else holds the gate open
  leaves it open or shuts it under that other user - and the driver's own hazard list says
  *"`rpc.allow_write` is sticky. Clear it when done"* (quoted in #211).
- `rpc.fw_rev`, `rpc.cmd` and `rpc.post` are three separate sysctls with no transaction between
  them. Two concurrent reloads interleave into one post with the other's revision.
- Nothing reads the result. `rpc.post` returning is not the command succeeding, which is the same
  defect the LIF installs have (#230).

### Proposed instead: the driver owns the revision

Untested, and proposed:

- The driver keeps the current `fw_state` revision as its own state, because **it has to**: there is
  no command that reads `fw_state` back, so whoever bumps it must remember what it last was. That is
  the same property that makes `fw_cfg` write-only and it has the same consequence - a value nobody
  has seen cannot be incremented by anyone but its last writer.
- One write-only sysctl - call it `rpc.fw_rev_bump`, which does not exist yet - that increments the driver's counter and posts
  `FW_STATE_REV_SET` itself, from the driver's own taskqueue, and checks the reply. It does not need
  the general `allow_write` gate because it is not a general write: it is one command with no
  operand, which cannot be aimed at anything else.
- `xgs_firewall()` writes that one sysctl and nothing else. No gate, no sequence, no operand, and
  nothing to interleave.

This is the same conclusion the LIF page reaches for the same reason: the writes that maintain the
coprocessor's forwarding state belong inside the driver, and the gate exists to keep them there.
What comes from outside should be a notification, not a sequence of register writes.

### Two details that will bite

**The revision is sixteen bits.** `fw_state` is `{uint32_t fw_cfg; uint16_t rev_num; uint16_t
l3_fwd_rev_num;}` (`docs/families/octeon-tx-crypto-path.md:278`), and the setter reads a halfword.
So the counter wraps at 65536 bumps. A flow entry carries
`+0x0c fw_state_rev_num` (`docs/families/octeon-tx-crypto-path.md:138`), and
`mflow_fpop_prog_both` refuses a mismatch on it (`docs/families/octeon-tx-crypto-path.md:340`) - so
after a wrap, a stale entry's revision can match again. Reaching that needs 65536 reloads without
the table being emptied by anything else, which is not a realistic path to a hole, but the counter
should start somewhere unpredictable rather than at zero and the wrap should be written down rather
than discovered.

**`FW_L3_FWD_STATE_REV_SET` is a different command and does not invalidate.** It is command 1
(`contrib/octep/octep.h:1263`), stored at `strh w1,[x0,#6]` with *"no invalidate"*
(`docs/families/octeon-tx-crypto-path.md:335`). Bumping that one on a ruleset reload would change a
number and discard nothing. The two are one off from each other in the enumeration and one letter
apart in the driver's sysctls - `fw_rev` at `contrib/octep/octep_rpc.c:1133` and `fw_l3_rev` at
`:979`.

## F. The patch sketch

Design, not code, and not built. Three files, and the second two are the ones that matter.

**`src/etc/inc/plugins.inc.d/xgs.inc`** - one new function, discovered by name:

    function xgs_firewall($fw)
    {
        # Called from filter_configure_sync() on every ruleset reload - the GUI, the API,
        # rc.bootup three times, a WAN lease renewal, a routing change. Bump the coprocessor's
        # firewall revision, which discards every offloaded flow, because the ruleset those
        # flows were authorised under has just been replaced.
        #
        # Before pfctl -f rather than after, deliberately: see docs/ruleset-reload.md section C.
        # Returns $fw untouched - this hook contributes no rules.
        #
        # Silent when there is no coprocessor, which is the common case at boot.
        ...write the proposed rpc.fw_rev_bump leaf, ignore its absence...
        return $fw;
    }

**`contrib/octep/octep_rpc.c`** - a `fw_rev_bump` sysctl handler that increments
`sc->rpc_fw_rev`, posts command 0 on the driver's own taskqueue rather than inline, and records the
reply. It must not take `sc->mtx` and then busy-wait in the caller's context: the two-second
`OCTEP_RPC_CMD_WAIT_MS` (`contrib/octep/octep.h:1373`) would be charged to a PHP process holding a
file lock on `/tmp/rules.debug` (`src/etc/inc/filter.inc:172`), stalling every other reload behind
it.

**`docs/`** - this page, and a line in the status output so the revision the host believes in is
visible, beside the `fw_cfg` note that already says it is write-only
(`src/opnsense/scripts/xgs/status.py:201`).

## What is not here

- **No measurement of the window in section C.** How long rule generation plus `pfctl -f` takes on
  this appliance is unmeasured, and it is the length of time forwarding runs at host rate on every
  reload. It is measurable without the coprocessor and should be.
- **Nothing about `pfsync`.** On an HA pair a ruleset reload happens on each member separately.
  Whether a bump on one should imply anything on the other is not addressed.
- **Nothing about `filter_configure_xmlrpc()`** (`src/etc/inc/xmlrpc/legacy.inc:137`), which is the
  HA sync path into a reload. It was found and not followed.
- **No check of whether `plugins_firewall` is called on an alias-only refresh.** `filter
  refresh_aliases` runs `update_tables.py` without going through `filter_configure_sync`, so a table
  update that changes what the ruleset matches may not bump anything. This is the most likely gap in
  the design and it was not resolved.
- **`opnsense/core` is at `92974f0`**, this repository's shallow clone of its default branch, and
  the appliance's installed version was not checked against it.

## What this needs that is not in the repository

1. **Does `fw_state_fpop_rev_set` invalidate synchronously, or queue?** The disassembly shows
   `bl mflow_fpop_invalidate_issue` (`docs/families/octeon-tx-crypto-path.md:330`) and *issue*
   suggests queueing. If it queues, there is a window after the command returns in which flows are
   still live, and the ordering argument in section C needs that window to be shorter than rule
   generation.
2. **Is the revision checked on the from-wire path, or only when programming?** This page assumes a
   bump makes existing entries unusable. What is read is that `mflow_fpop_prog_both` refuses a
   revision mismatch when *programming*. If the forwarding path does not also check it, then
   invalidation depends entirely on `mflow_fpop_invalidate_issue` having completed, and the revision
   is bookkeeping rather than a fence. **This is the question that decides whether the bump is a
   barrier or a hint.**
3. **What does `FW_STATE_REV_SET` return on a revision the far side considers stale or equal?** If
   bumping to a value it already holds is accepted silently, a wrapped or restarted counter fails
   quietly.
4. **Is there a reload path in OPNsense that changes what `pf` matches without calling
   `filter_configure_sync()`?** `refresh_aliases` is the candidate named above. A list of them is
   the difference between this hook being complete and being nearly complete.

**Lesson.** The place to hook a thing is wherever its callers all pass through, not wherever its
name appears: `filter_configure()` reads like the reload and is one of ten ways in, while
`plugins_firewall()` reads like a rule-generation detail and is the only point all ten cross.
