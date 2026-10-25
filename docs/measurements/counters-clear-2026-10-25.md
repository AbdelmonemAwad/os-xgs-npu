<!-- reviewed: 2026-10-25 -->
# The coprocessor's counters cleared, on the XGS 136 and both XGS 3300s, 2026-10-25

Discovery 13 of [the discoveries](../findings/discoveries-2026-10-22.md#counters-cleared), taken from
the source to a tool. Four commands clear the fast path's counter arrays - debug, fast path, port and
direct forwarding - so a measurement can start from zero: the far side pushes a clear onto that
array's ring and its workers zero it (`cntrs_fpop_clr` in the vendor's source). Neither driver sent
them; now both do, as `counters_clear` - `dev.octep.0.rpc.counters_clear` and
`dev.npuep.0.rpc_channel.counters_clear`, written 0 to 3 - and `tools/octep-fpcnt.sh -z` and
`tools/npuep-fpcnt.sh -z` clear the fast-path counters before they read them. Nothing in either
driver or the plugin reads the counters, so a clear changes nothing but the counts.

**Their numbers are the version's, and they showed a wrong claim.** The hosts' own clear functions
give them: SFOS 21's `usfp_cntrs_clr` sends 46, 47 or 48 by the counter type, SFOS 23's `cntrs_clr`
the type plus 48, and SFOS 22's are its source's enumeration, 47 to 50. SFOS 22 put the
direct-forwarding read at 46, where SFOS 21 has its first clear - so ~~every number in both drivers
is SFOS 21's and 22's~~ the two agree only below 46, and SFOS 21 has no direct-forwarding commands at
all. The drivers now number every command as SFOS 22 does and place one from 46 on by the version
as well as the shift ([rpc-numbering.md](../families/rpc-numbering.md)); a command SFOS 21 does not
have is not sent, and says so. Nothing either driver sent had gone past 45, so nothing was ever sent
under the wrong number - but `LO_WORKER_DF_CNT_READ`, 46, which the OCTEON TX driver counts as a
read, would have cleared the debug counters on an OCTEON TX board running SFOS 21's software.

octep.ko `0c7fd827` on both XGS 3300s and npuep.ko `629caf6f` on the XGS 136, built from this change
against the appliances' own kernel. The change's octep.ko is `fdc4e281`, which differs from `0c7fd827`
only in the description of `rpc.allow_write`; with it, `tools/octep-fpcnt.sh -z` took the fast-path
counters' sum from 5,807 to 63 on the second XGS 3300 and from 23,274 to 141 on the first. On each board the fast-path counters were read with the tool -
how many are not zero and their sum - cleared, read at once and again two seconds later, and the
other three clears sent (`images/deploy/lab-counters-clear.sh`).

![The three boards: on the second XGS 3300 (SFOS 22) the fast-path clear went out as 48 and the counters fell from a sum of 4,443 to 312 two seconds later; on the first (SFOS 23) as 49, from 12,349 to 2; on the XGS 136 (SFOS 21) as 47, from 43,920 to 21; the other clears 47, 49, 50 and 48, 50, 51; on the XGS 136 the direct-forwarding clear not sent, SFOS 21's software having none](../images/measurements/counters-clear-2026-10-25/01-three-boards.png)

| board | its coprocessor's software | the fast-path clear went out as | the counters' sum before | at once | two seconds later | debug, port, direct-forwarding clears went out as |
|---|---|---|---|---|---|---|
| the second XGS 3300 | SFOS 22 | 48, rc 0 | 4,443 | 4,443 | 312 | 47, 49, 50 |
| the first XGS 3300 | SFOS 23 | 49, rc 0 | 12,349 | 12,349 | 2 | 48, 50, 51 |
| the XGS 136 | SFOS 21 | 47, rc 0 - shown by the same clear sent by hand | 43,920 | 43,930 | 21 | answered, at 46 and 48 by its numbering (this driver does not show the number) - and the third not sent: SFOS 21's software has no such command |

- **A clear is taken on the workers' own time.** Read at once, every counter still held its count;
  two seconds later each board's had started again from zero - what is left is what the boards
  forwarded in those two seconds. Read by hand on the second XGS 3300 beforehand: zero within one
  second, then counting again. The tools wait two seconds after a clear.
- **The far side answered every clear with rc 0**, and with nothing back, as the vendor's host sends
  it: no request and no reply.
- A command sent by hand goes through the same placing: on the XGS 136, `48` went out as 47, and `50`
  was not sent, the driver saying SFOS 21's software has no such command.

## What it does not say

- Whether the debug, port and direct-forwarding arrays went to zero was not read; their clears were
  answered rc 0. Only the fast-path array, which the tools read, was.
- What a clear does to a counter a worker is incrementing at that moment.
