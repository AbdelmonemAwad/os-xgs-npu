# Offloading, as a setting the owner decides

Written 2026-10-05, when the acceleration gate stopped being a sysctl and became a page.

## Why it could not go in System: Settings: Miscellaneous

That is where an OPNsense user looks for hardware acceleration, and it is where this was asked
for. It cannot go there, and the reason is worth writing down once so nobody spends an afternoon
on it twice.

`system_advanced_misc.php` is a core page. Its two hardware controls are built from two hardcoded
arrays:

```php
function crypto_modules()
{
    $modules = [
        'hifn' => gettext('Hifn 7751/7951/7811/7955/7956 Crypto Accelerator'),
        'padlock' => gettext('Crypto and RNG in VIA C3, C7 and Eden Processors'),
        'qat' => gettext('Intel QuickAssist Technology'),
        'safe' => gettext('SafeNet Crypto Accelerator'),
    ];
```

and each entry survives only if `/boot/kernel/{$name}.ko` exists. `thermal_modules()` is the same
shape with two entries. The page calls **no plugin hook at all** - not one of the eleven that
`plugins.inc` offers - so there is nothing for an out-of-tree package to register with. Adding an
entry means editing a file the `opnsense` package owns, which an update replaces and `pkg check`
reports as modified.

And it would be the wrong entry anyway. Both of those settings are kldload lists:
`$config['system']['crypto_hardware']` and `['thermal_hardware']` are read in `system.inc` and
appended to the list of modules to load. One wants a `crypto(9)` provider, which this driver is not
- the IPsec work that might one day make it one is a separate, unfinished question. The other wants
a temperature sensor module, and this appliance's sensors are on the host's SMBus and not reachable
from here at all.

So the setting goes **beside** Miscellaneous instead of inside it: `System: Settings: Hardware
Offloading`. The children of System: Settings carry no `order` attribute, so they sort by name, and
that name lands between General and Miscellaneous without a number being invented for it.

## The one setting

`FW_CFG_OFFLOAD`, bit 0 of `fw_cfg`, written with RPC command 2. It decides whether the fast path
may forward a frame instead of handing it to the host - which means a frame it handles does not
reach `pf`. That is why it is off by default and why it has an ACL tag of its own rather than
sharing the status page's.

Nothing else from the `rpc` sysctl tree is exposed, deliberately. Those are instruments: they stage
a request, they have no idea what the firewall is doing, and a wrong value in one of them is a
dropped LAN rather than a rejected form. A settings page is the wrong shape for them.

## The part that is easy to get wrong

**The coprocessor does not remember it.** Every boot it comes up at `0x2` - `TCP_SEQ_CHK` alone,
offloading off - whatever `config.xml` says. A setting that is written once and not re-applied is
therefore a setting that lasts until the next reboot and then silently reverts, which is the worst
way for a switch in a GUI to behave.

So `bringup.sh` applies it, and the position in that script is not free:

- **after** `LIF_ADD_UPDATE`, because opening the gate with the LIFs unprogrammed drops a bridged
  LAN outright - measured, 0 of 20 pings and 35 `LIF_NOT_MY_MAC` drops
- **before** `allow_write` is shut, because writing `fw_cfg` needs that guard open
- and **not fatal**: a refused apply is logged and the bring-up continues. Twelve working
  interfaces with the gate shut is a working appliance; a bring-up that aborted over an accelerator
  would turn a missed optimisation into a dead firewall.

## Two things the script does not assume

**It does not write a constant to `fw_cfg`.** That word is written whole by command 2, so a
constant would mean this script silently deciding every other bit in it, including `TCP_SEQ_CHK`,
which is a safety check and is on by default. The current value is read, bit 0 is set or cleared in
it, and nothing else is touched.

**It does not read `rpc.state` to decide whether the facility is up.** `state` is a multi-line dump
of the cfg word, the ring offsets and a command tally - useful to read, impossible to test against.
`rpc.ready` is the one boolean that says the target has acknowledged a configuration, and an absent
sysctl means the driver is not loaded at all. Guessing at the shape of `state` would have produced
a check that passed for every input, which is this repository's most frequent kind of bug.

## What it reports, and what that is worth

The reply is the script's own, not a status word:

    {"applied":true,"offload":1,"message":"offloading is on: the coprocessor accepted fw_cfg 0x3"}

"Accepted" is the exact claim. `rc` is the transport's answer and not the operation's, so a zero
means the coprocessor took the request - not that anything is being accelerated. As of this
version nothing is: the gate opens, the request is accepted, and every frame still reaches the
host, because no flow is ever activated. See [the-state-is-the-gate.md](the-state-is-the-gate.md).
The page says so in a warning box rather than letting a switch imply a speed-up it cannot deliver.

## Two defects found while doing this

**`rm -f /tmp/opnsense_menu_cache.xml`** has been in `install.sh` since the status page was added
and has never removed anything. The cache goes in `MenuSystem`'s `tempDir`, which `config.php` sets
to `/var/lib/php/tmp`, so every install has quietly waited out the TTL instead of taking effect at
once. Both paths are now given.

**`uninstall.sh` removed none of the GUI integration** - not the MVC tree, not the configd action,
not the plugin hook, not `status.py`. Uninstalling left a menu offering two pages for a driver that
was no longer installed. It removes them now.

## Lesson

A request to put something in a particular page is a request to be found in a particular place, and
the two are not the same thing. The page was closed; the place was not. Reading the core page first
settled in ten minutes what a plugin can and cannot do there, and reading what the two controls
actually *do* - kldload a module - showed that the entry would have been wrong even if it had been
possible. The cheapest step in both halves was reading the code that was already on the disk.

## A third defect, found by testing the boot path

The design above said "a refused apply is logged". It is not, and nothing else this script logs
reaches a log either.

`bringup.sh` runs from an early syshook, **before syslogd**, so `logger` writes to a socket nothing
is reading and the message is discarded without an error. Measured after a reboot: not one line of
that script's output is in any log file on the appliance - not the offload line, not even
`up: 12 front-port interfaces`, which has been printed at every boot for weeks. The three offload
lines that *are* in the log are from running the script by hand minutes earlier.

This is why the settings page reads the gate's value back out of the driver on every page load
instead of trusting a message. `rpc.fw_cfg` is the value the host last asked for - the coprocessor
will not read it back - but after a boot that value is whatever `bringup.sh` staged, which is
exactly the question being asked. A page with a switch on it that cannot show the switch's effect
is worse than no page.

The general problem - that every `log()` call in `bringup.sh` is lost at boot - is bigger than this
change and is filed separately.
