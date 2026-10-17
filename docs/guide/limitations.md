<!-- reviewed: 2026-10-17 -->
# What it cannot do

*Moved here from the [README](../../README.md) on 2026-10-18, word for word, when the README was
made shorter. Struck claims stay struck, with what replaced them beside them; links were only
re-pointed to work from this folder.*

## ⚠️ What it cannot do

*Also ARMADA. The OCTEON TX limits are different and are listed on
[its own page](../families/octeon-tx.md). ~~There both directions now work and the panel ports are
interfaces, but nothing is optimised - every frame is copied, there is one queue per direction per
interface and no offload - and the whole bring-up is a sequence of sysctls that has to be repeated
after every reboot.~~ That was 2026-10-01. There the bring-up runs at boot, connections and IPsec
are offloaded to the coprocessor, and the host takes eight receive rings; what remains is that
every frame crossing the host is copied once each way.*

**The datapath attaches once per coprocessor boot.** The device waits for `HOST_MGMT_READY`
once, answers once, and then spends the rest of its life in its command loop. **A module reload on
its own cannot be answered** — three remedies were tried and measured not to help: closing the
datapath down with `PF_DISABLE`/`PF_CLOSE`, which the device accepts and which changes nothing;
retracting the stale handshake; and waiting thirty seconds instead of four.

What restores it is a coprocessor reboot, and the host can cause one itself. `06-npuctl` pulses the
reset line, and that pulse is a real reset rather than a release — so **a pulse followed by a
reload brings everything back with no power cycle.** Verified, twice in a row: fourteen
interfaces, the forwarding tables read back and confirmed, `nwa: 14 of 14 ports up`, and a front
port brought up with no address counted forty frames off the wire in twelve seconds. A cold power
cycle works too. A warm reboot runs the same hook, so it very probably does as well — still
untested end to end, but no longer resting on an untested mechanism.

This file used to say a power cycle was the only way. That was wrong, and both reasons looked like
hardware rather than like bugs: `reload.sh` parsed the PCI selector with an `awk` field separator
that left out the tab `pciconf` prints, so it waited its whole timeout and reported a dead
endpoint that had been answering all along; and the barmap parser read an entry the coprocessor
had not written yet as a real one — every field zero, and zero is the control facility's id — so a
half-published table failed attach outright. Both fixed.

**The facility table takes about fourteen seconds to appear after a reset, and is not published
atomically.** The cookie lands before the entries do, so validating the cookie and reading on gets
a partial table. The driver now waits for the facilities it actually needs. Measured at exactly
fourteen seconds on two consecutive cycles.

Loading the driver too soon after a reset pulse **hangs the host**: a PCIe read to an endpoint
still in reset neither returns nor times out, so there is no panic and no log — the machine simply
stops. Both the driver and `reload.sh` now ask **config space** first, which is the safe question:
a configuration read to a device that is not answering comes back as all-ones instead of being
left outstanding. Measured after the fix, the endpoint answers config space the instant the pulse
ends, so the guard costs nothing and only the facility table needs waiting for.

**The interfaces are assigned in OPNsense, and they have to be.** All twelve carry traffic and can
be bridged; one of them is this appliance's WAN. Until a front port is assigned it is outside the
firewall's own configuration and pf has no rules for it, so an unassigned port that looks up and
carries nothing is usually that and not the driver.

**A bridged front port needs promiscuous mode, and gets it by itself.** The port's hardware filter
admits unicast only for the address it owns, and a bridge member receives replies addressed to the
**bridge** — so without it the port forwards broadcast and nothing else, which looks like a working
port right up to the point where something tries to talk through it. The driver follows
`IFF_PROMISC`: on when `if_bridge` adds the port, off when it leaves, nothing to configure.

**The device's own packet counters are unavailable.** `GET_STATISTICS` is answered, at full
length, with zeros: Marvell's header labels that member `CC_PF_PP2_STATISTICS`, the physical
packet processor's counters, and a function with no physical port has none. `GET_GP_STATS`, which
is the GIU port's own, is not implemented by this firmware at all. The driver reports its own
counts and says so plainly, because an instrument that prints a zero reading like a measurement is
worse than one that admits it cannot see.
