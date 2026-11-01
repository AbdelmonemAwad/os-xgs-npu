<!-- reviewed: 2026-11-02 -->
# A ring's memory given back only once the coprocessor is known to have stopped with it, on the first XGS 3300 and the XGS 136, 2026-11-01

[962](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/962), found by reading the code while answering a
review: octep cleared a ring's enables and freed its memory straight after, with no read-back and no wait for
the ring's IDLE bit; its management link waited a fixed two seconds after GOING_DOWN and freed whatever the
target was doing; the RPC buffers went whatever was still on the command rings. And the coprocessor latches the
output rings' addresses when its port opens and wrote the old ones again after a restart of the rings
([OCTEON TX, "Rings before the handshake, and never restarted"](../families/octeon-tx.md#rings-before-the-handshake-and-never-restarted)),
so even a ring known stopped could be written into once its memory had gone back. Nothing of this had been
seen on an appliance.

## What changed

- **A ring is stopped and known stopped before its memory goes**: its enables cleared, then read back with its
  control word, and its IDLE bit awaited - the bit the driver already waited for before programming a ring
  (`octep_dp_ring_quiet`). A ring that will not stop keeps its memory, and says so.
- **The output rings' memory is kept while the coprocessor still holds their addresses** - `SLI_EPF_SCRATCH`
  not zero (`octep_dp_target_holds`) - and freed once it has restarted and waits for the host, or is off the
  bus, in its reset. Kept memory is lost until the host restarts, where it used to be pages the coprocessor
  could write after they were given back.
- **The management link** frees its buffers once the target answers GOING_DOWN by leaving RUNNING; a target
  still running at three seconds keeps them.
- **The RPC buffers** go once both command rings' done counts have reached their posted counts.
- `dev.octep.0.dp.kept_bytes` counts what was kept, `dp.quiet_us_max` how long the slowest ring took to stop;
  `dp.stop_check` and `dp.keep_test` are test switches, and `contrib/octep/test-stop-writes.sh` drives the
  first.

## Measured

The first XGS 3300 (vendor version 23), OPNsense 26.7.8_1, FreeBSD 15.1-RELEASE-p6. octep built on it with every
warning fatal - 529,624 bytes, 0 warnings - and booted from `/boot/modules`; the release module of
0.1.0-alpha.10.6 (518,928 bytes) before and after. XGS: Settings' restart of the fast path was set to 0 and
problem reporting to manual for the test, and both put back.

| what was done | what was found |
|---|---|
| booted on the test build | 12 front ports; 3 of 3 pings to each of three addresses; verify.sh 8 passed, 0 failed |
| `contrib/octep/test-detach-refusal.sh` (#961) on this build | every check passed, exit 0; traffic 3 of 3 after it |
| `dp.stop` with `dp.stop_check=1`, under a broadcast stream of 200 frames a second from the PC into the LAN bridge | **14,904 words written by the coprocessor in the second before the rings stopped, 0 in the second after**; every ring idle at the first read (`dp.quiet_us_max` 0) |
| ... the output rings' memory | kept, 15,630,336 bytes: the coprocessor held their addresses |
| `kldunload octep` | the management target answered GOING_DOWN in 20 ms (`ready`); the RPC rings drained; unloaded in under a second, where the fixed wait took two |
| the recovery path: the fast path stopped on the coprocessor (`kill -STOP` of its process), `recover.sh` run once the driver had taken it as gone (29 s later) | the coprocessor rebooted from its console and waited for the host 177 s in; the unload found it holding nothing - "every ring known stopped before its memory went", nothing kept; taken up in place in 280 s; traffic 3 of 3 to each address, verify.sh 8/0 |
| `dp.keep_test=1`, then `dp.stop` | every ring taken as one that will not stop: 36,720,640 bytes kept, a line for each transmit ring, the 8 receive siblings and ring 1; the host ran on; `kldunload`: the target answered in 20 ms |
| the release module put back | 518,928 bytes, checksum matching; restarted onto it: 12 front ports, 3 of 3 pings to each address; the reports the tests had queued kept from being sent, and both settings put back |

`contrib/octep/test-stop-writes.sh` was not run on the appliance: the stop above is its steps, run by hand
before the script was written. Its reading of the result was checked against stand-in commands - a stop
with writes before and none after passes, a write after it fails, and no write before it fails as a check
that proved nothing.

## A host that stopped - a test run wrongly

Before the recovery above, `recover.sh` was started by hand with the fast path healthy (20:13 UTC). After its
two-minute wait for a boot of the coprocessor's own it asked the coprocessor's console to reboot, while the
driver - which had not taken the coprocessor as gone - was still reading it. The host stopped and its hardware
watchdog restarted it (up again 20:18:20); no host line after 20:13 reached the disk. The unload under test was
never reached. In service `recover.sh` is started only once the driver has taken the coprocessor as gone or its
fast path has stopped, and the driver takes it as gone as the heartbeat dies, so the run in the table made that
happen first. That `recover.sh` does not refuse a coprocessor the driver still reads is a defect of its own,
kept apart from this change.

## npuep, on the XGS 136

npuep cleared bus mastering last on its way out, after every facility had freed its rings; the management
link kept its memory when the coprocessor did not agree to stop, and the GIU when the endpoint read all ones,
but the RPC buffers went whatever was still on the command rings.

- **The facilities' frees are held while the detach runs** (`npuep_dma_free`, `npuep_dma_tag_destroy`), bus
  mastering goes off, one read of the device lets whatever the coprocessor wrote before land, and only then is
  the memory given back. Anywhere else they free at once, as before.
- **The RPC buffers** wait until both command rings' done counts have reached what was posted - one more on
  the low ring when the last command went unanswered; otherwise they are kept.

The XGS 136 (vendor version 21), OPNsense 26.7.8_1, FreeBSD 15.1-RELEASE-p6: npuep built on it against the
running kernel's own sources, with every warning fatal - 436,600 bytes, 0 warnings - and loaded in place by its
own `reload.sh` from a test directory, twice: its release module (430,456 bytes, from 0.1.0-alpha.10.6) to the
test build, then the test build to itself, so that the second unload was the changed one. Each reload resets the
coprocessor after the unload. XGS: Settings' restart of the fast path at 0 and problem reporting manual for the
two reloads, both put back.

| what was done | what was found |
|---|---|
| reload 1: the release module unloaded | its own lines only - the GIU's tally and HOST_INIT and HOST_ALIVE withdrawn; bus mastering cleared last, as it always had |
| reload 2: the test build unloaded | **4,662 DMA allocations held while the detach ran and given back once bus mastering was off**; nothing kept - the RPC rings drained, the management link agreed, the endpoint answered |
| after each reload | the coprocessor reset and the test build attached again: 14 front ports, the heartbeat alive, the LAN bridge with the PC on it at once; 3 of 3 pings to the PC and to a lab peer |
| the appliance restarted onto its release module, `/boot/modules` untouched throughout | 430,456 bytes, checksum matching; 14 front ports, the heartbeat alive, 3 of 3 pings to the PC and the peer |

## Not run

The coprocessor off the bus at the moment of the unload (reading all ones) - OCTEON TX frees its memory then,
ARMADA keeps it; a command left unanswered on npuep's ring at the unload; the second
XGS 3300, the XGS 2100, 2300 and 3100, and the XGS 107 and 126.
