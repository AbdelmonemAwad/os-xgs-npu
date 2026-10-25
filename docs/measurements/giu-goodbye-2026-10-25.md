<!-- reviewed: 2026-10-25 -->
# The datapath's goodbye at unload, answered once it is said first, 2026-10-25

[741](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/741),
[742](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/742): when npuep.ko unloads with the
coprocessor running - `contrib/npuep/reload.sh` does it, and nothing else does - its goodbye to the data
path, `PF_DISABLE` and then `PF_CLOSE`, timed out now and then (`ETIMEDOUT`, 60). The reset that follows
in reload.sh hid it: every reload came back with all fourteen ports.

Three changes, measured on the XGS 136 on SFOS 21's software, the only board of the family on the bench,
by six reloads at a time (`images/deploy/lab-giu-goodbye.sh`, `lab-giu-goodbye-series.ps1`; every
goodbye read back from the kernel's log into `measurements/giu-goodbye-741-2026-10-25.log`):

1. **The host's datapath stops before the goodbye.** Receive, refills and transmit starts went on through
   both commands; the vendor's host stops its own before its `PF_DISABLE`.
2. **`PF_CLOSE` is not sent.** The vendor's host never sends it ([giu.md](../npuep/giu.md)), the
   coprocessor's handler for it frees the queues at once, and it released nothing a next load could use.
3. **The goodbye is said first** - `npugiu_goodbye`, from `npuep_detach` before the control-message
   channel is withdrawn and the ports are put down.

A goodbye that goes unanswered now says where the device's command consumer and notification producer
stood, and how many notifications arrived while it waited.

## The first two alone: four unanswered in five

| unload | npuep.ko | the goodbye |
|---|---|---|
| 1 | `873a6662` (before) | `PF_DISABLE failed (60)` |
| 2 | `384170c1` (1 and 2) | `PF_DISABLE failed (60)` - the command taken off the ring (consumer 21 of 21), no answer, 0 notifications while waiting |
| 3, 4, 6 | `384170c1` | `PF_DISABLE failed (60)` - the command never taken (consumer 20 of 21), 0 notifications while waiting |
| 5 | `384170c1` | `datapath disabled` |

The device's management side was silent while the goodbye waited - not a keep-alive in five seconds,
where the same unloads counted 30 or 31 of them over the module's two minutes, one every three or four
seconds. The network agent's commands had all been answered just before (`nwa: 673 commands, 0 refused,
0 timed out`). Something the unload did before the goodbye had silenced it; the goodbye came after the
control-message channel's withdrawal and the ports' downs.

## All three: five of five

![Six reloads of the XGS 136 with npuep.ko a20cdaf6 in /root/giu741: the first unloads the previous build and its PF_DISABLE fails, the command never taken; the other five say "giu: datapath disabled"; each new load has all fourteen ports confirmed and a ping to the second XGS 3300 answered](../images/measurements/giu-goodbye-2026-10-25/02-sfos21-six-reloads-goodbye-first.png)

The first unload of this series is the previous build's (`384170c1`, the goodbye after the channel and
the ports) and it failed as before; the other five are `a20cdaf6`'s, the goodbye said first, and each was
answered. Every new load had its fourteen ports confirmed and the link to the second XGS 3300 answering.
The build in the change, `ae68eeed`, differs from `a20cdaf6` by a comment.

## What this does not settle

- **What silences the management side.** Either the control-message channel's withdrawal or the
  ports' downs; which one was not separated.
- **The rest of the unload path.** [780](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/780): the
  inspector's function freed without telling the device, the flow table's takeouts sent after the
  channel is gone, and reload.sh, which says it resets before unloading and does the opposite.
