<!-- reviewed: 2026-10-31 -->
# The coprocessor's state kept the moment its fast path stops, and a recovery that waits for the new boot, 2026-10-31

[819](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/819): on 2026-10-31 the first XGS 3300 (vendor
version 23) was cold-started after a night switched off, and 182 s into its coprocessor's boot the
coprocessor's console printed one line - `Unable to handle kernel NULL pointer dereference at virtual
address 00000001` - and nothing after it. The heartbeat was taken as stopped 30 s later and the
recovery took the coprocessor up again in place, about three minutes from the oops to an answering
heartbeat. Which function failed is not known: the trace never reached the console, and after the reset
the coprocessor's pstore held no record of it - its kernel never finished the oops.

Two things on the host made that harder than it had to be, and two more were found in testing what was
changed for them.

## What changed

- **Nothing printed on the console is thrown away any more.** `npu-run.sh` falls back to the coprocessor's
  console when the management link does not answer - as it does for a coprocessor that has stopped - and
  read the console into a temporary file it then deleted, with the console's logger standing aside the
  whole time. What a caller reads is now kept in the console's log (`console_log.py keep`), by
  `npu-run.sh`, `switch-jumbo.sh` and the new `npu-sysrq.sh`.
- **The coprocessor's kernel is asked for its state the moment the driver takes it as gone.**
  `feeder.py` reads the driver's gone latch every 2 s - a number in the driver's memory, read without its
  lock - and when it turns to 1 runs `npu-sysrq.sh lwp`: a serial BREAK on the coprocessor's console and
  then SysRq l (every CPU's backtrace), w (blocked tasks) and p (registers), what they print kept in the
  console's log. Only keys that print are taken, and only while the coprocessor's own watchdog cannot yet
  have reset a hung one into its boot firmware (55 s since its fast path last answered).
- **The recovery starts at once** (`fastpath.py check` from `feeder.py`), not at the next minute of cron:
  on the cold start the coprocessor's watchdog would have reset it five seconds after the cron-started
  recovery did.
- **Crash records are read back.** Once a coprocessor that booted again answers, `feeder.py` reads the
  `dmesg-ramoops` records in its pstore - the ones an oops that runs to its end leaves - and keeps each
  new one in the console's log.
- **`recover.sh` waits for this boot's line.** It takes the device away and back once the coprocessor's
  console prints `Waiting for USFP application to connect`, and it looked for that line in the log's
  last 40 lines: a coprocessor that printed nothing after its fast path stopped left the line of its
  previous boot there, and the device was detached eight seconds after its console was asked to reboot
  it, before the new boot had begun. Only what was written to the log after the recovery began counts
  now.
- **The driver's sysctls go before its lock.** Its sysctls lived in the device's own context, which
  newbus frees only after detach returns - after `octep_detach` has destroyed `sc->mtx`, which most of
  their handlers take. They are in the driver's own context now, freed first thing in detach;
  `sysctl_ctx_free` waits for a handler already running.

## The runs

The first XGS 3300, OPNsense 26.7.8 (26.7.8_1 from 08:08), an IPsec pair to the second XGS 3300 on its
coprocessor (indexes 1 and 2) in every run, and its fast path stopped on purpose (`kill -STOP` of the
coprocessor's `usfp`), its watchdog feeder left running - so the console is asked to reboot it.

| UTC | what ran | outcome |
|---|---|---|
| 07:42 | the first form of this change: `feeder.py` reading the heartbeat's state every 2 s, and the bench reading it every 4 s | the kernel's state kept at 07:43:41, 20 s after the stop was declared; the console asked at 07:45:53; the host froze at about 07:48:45, at the detach, and its hardware watchdog restarted the appliance |
| 08:09 | 0.1.0-alpha.10.1 as released | taken up in place; the device detached at 08:12:08, eight seconds after the console's reboot - the previous boot's line |
| 08:32 | the scripts as merged, the released driver | the kernel's state kept at 08:33:33, the recovery begun with it; this boot's line at 08:36:26, the device detached at 08:36:30; the fast path answered again at 08:38:12, 279 s in |
| 08:41 | the same | taken up in place, the host not restarted |
| 08:51 | with the driver as merged | not a run: the recovery limit, six in 24 hours, restarted the appliance as it is meant to; the count was cleared for the next |
| 08:55 | the scripts and the driver as merged, and the heartbeat's state and `since_answer_ms` read ten times a second throughout | the kernel's state kept at 08:55:55; this boot's line, the device detached at 08:58:53, the fast path answering again at 09:00:37, 281 s in; 5,599 reads, 8 of them refused while the device was away, the host never stopped |

The freeze at 07:48 was not seen again. It was a single run, with a reader of the heartbeat's state that
locks the mutex the detach destroys; the driver's change takes that window away whatever reads it.

## What these do not show

- The kernel's state of a coprocessor that has really hung: these stopped its fast path, and its kernel
  answered SysRq with the stopped process and its idle CPUs. Whether a hung kernel takes a BREAK is what
  the next real stop will say.
- What failed on 2026-10-31. Only a cold start has shown it so far, once in 28 boots of this
  coprocessor's console record; the second XGS 3300 (vendor version 22) has shown nothing in 14.
- The XGS 136: SysRq here is the XGS 3300 family's console alone.
