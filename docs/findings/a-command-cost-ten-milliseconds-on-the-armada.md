<!-- reviewed: 2026-10-18 -->
# Every command cost ten milliseconds on the ARMADA

Measured on an XGS 136, 2026-10-18.
[Issue 456](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/456): the control channel answered in
a little over a tenth of a millisecond, and the driver waited ten for every answer. Beside it, the
flow table was a bench's size on a board that states its own.

## The wait

`npurpc_command` writes a command into host memory, posts a descriptor, rings the doorbell and then
waits for the far side's done index to pass it. It read the index once, straight after the doorbell,
and then slept `hz / 100` between reads. Nothing answers in the instant after a doorbell, so every
command slept at least once: ten milliseconds each, whatever the command was.

The flow maker posts one to three commands for a connection - a next hop or two if they are new, and
`FLOW_CREATE_FP` - and was held to eight a sweep, one sweep a second.

The OCTEON TX driver had measured its own channel's answer long before - 20.7 microseconds on average
over 358,598 commands - and waits for it by spinning in 20-microsecond steps for the first two
milliseconds. The ARMADA's channel is the same protocol (`docs/npuep/rpc-payloads.md`). The wait is now
the same: spun for two milliseconds, then slept as before, so a command that is slow still costs what
it did.

Measured after, over the first 399 commands after a boot, from the two counters the driver now keeps:

    dev.npuep.0.rpc_channel.wait_n: 399
    dev.npuep.0.rpc_channel.wait_us_sum: 55240      138 microseconds on average
    dev.npuep.0.rpc_channel.wait_us_max: 12000      one command went past the spin

## The table

The flow table was 256 connections and 128 next hops in fixed arrays, read whole on every lookup,
"bounded for a bench". The board states its own: `PLATFORM_READ` - command 36 (37 on vendor version 23, [rpc-numbering.md](../families/rpc-numbering.md)), which changes nothing on
the far side - answers with the vendor's `struct platform_info`, and on the XGS 136 it reads

    npuep0: rpc: platform XGS_DT2 (id 1, assembly AMDA0201-0003): 4 cores, 128 interfaces,
            1 high ring(s), 1500000 connections, 65536 next hops, 16384 associations, microflow timeout 10 s

The driver now reads it at startup and allocates the table at that size once it has - a sixteenth of
the host's memory caps it, and on the XGS 136's eight gigabytes the board's figure fits:

    npuep0: flow table: 1500000 connections, 2097152 chains, 65536 next hops, 262223616 bytes -
            this host's own size (the board's 1500000; a 16th of 8098 MB of memory holds 3085905)

A connection is found through a chain of a hash table as large as the table, an index is handed out
from a stack of free ones, and the passes walk a list of the connections that exist - the OCTEON TX
driver's table since 2026-10-17. The candidates the
receive path keeps are 1,024 slots by the tuple's hash, where they were 32 searched in order, and the
sweep makes up to 256 a second (`flow.make_per_sweep`). What each figure is on each board, and how to
set them, is [board-limits.md](../families/board-limits.md).

## What it does not say

How many connections a second the 136 now hands to its coprocessor: nothing has been run through the
box with the flow maker on since the change. The table's size is the board's statement and the log's,
not a table that has been filled.
