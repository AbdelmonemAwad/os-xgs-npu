<!-- reviewed: 2026-11-01 -->
# A marker in each receive buffer, on the XGS 136 first, 2026-11-01

[890](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/890): the XGS 107w loses its downloads to frames
whose header is zeros, at real frame lengths. The [third look](npuep-third-look-2026-10-31.md) left three
answers open - the device wrote zeros into the buffer, or wrote the frame later still, or wrote it somewhere
else - and its counters can tell the second only by waiting, and the first not quite: a buffer holds whatever
was last written into it, and the host never clears one, so zeros found there could be an earlier write's.

A descriptor and the producer index say a frame is in its buffer; nothing says the frame has landed. On the
coprocessor's side, MUSDK's GIE has the coprocessor's CPU write the producer index as soon as its DMA engine
reports the copies done, and MUSDK opens that engine with bufferable writes, so "done" may come before the
data ([the GIU notes](../npuep/giu.md)). So this build marks each buffer before the device has it, and looks.

## What changed

- **Each receive buffer carries a marker from when it is given to the device:** `0xfefafefa` twice where its
  frame will start - the word MUSDK's own test applications stamp into a buffer before giving it back,
  calling one that still holds it "Illegal buffer content". No header begins so: `0xfefa` is no port's tag.
- **A filled descriptor whose buffer still holds the marker is left for the next pass**, the producer index
  not moved, as an unfilled descriptor has been since [816](https://github.com/AbdelmonemAwad/os-xgs-npu/pull/816):
  `giu.rx_marker_waits` counts the passes, `giu.rx_marker_late` the frames that had landed by a later one,
  and `giu.rx_marker_skipped` the ones still marked two ticks on, skipped and their buffers given back.
- **A header of zeros in a buffer that held the marker is counted in `giu.rx_zero_written`:** the marker was
  there when the device was given the buffer and is gone, so the zeros are the device's, written that time.
- **`giu.rx_marker`** says whether it is on; **`hw.npuep.giu.rx_marker=0`** at the loader stamps and looks
  for nothing. And **`giu.rx_marker_test`** (0-100) makes that many more frames look not landed for one
  pass - the marker over their first bytes, put back on the next pass as a late frame's would land - to try
  the wait where no frame is ever late; those are counted in `giu.rx_marker_tested`, not as late.

`giu.stats` has a line for it, the fourth.

## Measured on the XGS 136

The XGS 136 (vendor version 21, platform XGS_DT2), FreeBSD 15.1-RELEASE-p6, its release module
0.1.0-alpha.10.3 in `/boot/modules`:

| | |
|---|---|
| build | on the appliance, against its own kernel's sources: clean with `-Werror`, no warning |
| load | in place from a test directory by that directory's `reload.sh`, `/boot/modules` untouched, behind a script that would have restarted the appliance onto the release module had its front ports, its heartbeat or its LAN bridge not come back in 150 s: back each time `reload.sh` finished, 14 ports, heartbeat alive |
| the test hold | `rx_marker_test` 5, then 100, under 50 and 300 pings from the appliance to the PC: `rx_marker_tested` 105, every ping answered; `rx_double_return` and `rx_stale` 0 |
| the setting | `rx_marker_test` takes 0 and 100 and refuses 101 and -1 |
| traffic | 200 MiB pushed into its LAN over ssh, in 3 s as in the third look's run, and 30 s of a download on its WAN |
| frames received | 221,097, 219,858 of them during the run |
| the marker | `rx_marker_waits` 105 - each the test's - `rx_marker_late` 0, `rx_marker_skipped` 0, `rx_zero_written` 0 |
| the rest | `rx_bad`, `rx_untagged`, `rx_nobuf`, `rx_unfilled_waits` 0 |
| the marker off | `hw.npuep.giu.rx_marker=0` at the loader and the module loaded again in place: `giu.rx_marker` 0; 100 pings answered and 100 MiB pushed into its LAN in 1 s, 73,500 frames, the marker's counters 0, and `rx_bad`, `rx_untagged`, `rx_stale` 0 |
| a reload that hung | the first reload for that, from the module with the marker on just after the run above: the appliance stopped answering, and its hardware watchdog restarted it onto the release module - its reset reason, read after: the watchdog expired. Nothing was logged after the run and there is no crash dump. Loaded the same way from that restart, it was back in 88 s, and taken back to the marker on after 73,500 frames, in 87 s |

So on a board whose receive path is right the marker is never found under a filled descriptor; it costs a
buffer one eight-byte write when it is given to the device and a frame one eight-byte compare.

## What it can tell on an XGS 107w

- `rx_marker_waits` and `rx_marker_late` climbing: frames that land after their descriptor. The wait takes
  them, so the loss falls by as many.
- `rx_zero_written` climbing: buffers the device wrote zeros into - the frame left the coprocessor empty, and
  the host cannot have it back. ~~That is on the coprocessor's side, in an image this project does not build.~~
  **Corrected 2026-11-01 (my error):** the zeros are the coprocessor's, but what made it write them was this
  driver - four receive queues set up where the XGS 107's coprocessor is set up for two; forcing the same
  mismatch on the XGS 136 made it there, and the count fixed by the CPUs takes it away
  ([measured](npuep-queues-2026-11-01.md)).
- `rx_marker_skipped` climbing: frames that had not landed two ticks on.
- None of them: the zeros come from somewhere this cannot see - another buffer (`rx_addr_mismatch`) or the
  frame's tail.

## What this does not settle

- **Only the frame's first eight bytes are marked.** A frame whose start lands and whose tail does not is not
  seen; the zero headers are at the start.
- **Nothing here ran on a 107.** On the 136 no frame was ever late; the wait and its counters were tried only
  by the test hold.
- **Why the one reload hung.** Of the day's five reloads it was the third; the two after the watchdog's
  restart worked. `contrib/npuep/reload.sh` warns of this very thing - a read of the endpoint's memory while
  the coprocessor is being reset hangs the machine with no panic and no log - and with nothing logged, which
  step it was cannot be said.
