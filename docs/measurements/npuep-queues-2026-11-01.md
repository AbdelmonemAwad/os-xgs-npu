<!-- reviewed: 2026-11-01 -->
# As many queues as the coprocessor is set up for: the XGS 107w's empty frames made on the XGS 136, 2026-11-01

[890](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/890): with the [marker](npuep-marker-2026-11-01.md) in
place, the XGS 107w showed that the frames it loses are written empty by the coprocessor itself - about one in
twenty, every one in a marked buffer, none late, and every frame on one receive queue. Then its owner ran the
appliance once on the vendor's own firmware, version 22, and the same appliance downloaded 349 MB at about
4.6 MB/s, some thirty times what it reached here, with no receive errors
([his readings](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/890#issuecomment-6557495216)). Its PCIe link
was x2 at 8 GT/s, as the board is made. And the vendor's host driver had logged this:

    agnic: Warn: Qs per TC (4) must be <= number of available CPUs. Reducing to 2
    agnic: Multiqueue Enabled: Rx Queue count = 2, Tx Queue count = 2

The vendor's host driver takes as many queues as the host has CPUs, at most four, and the coprocessor's own
setup matches it: two queues on the XGS 87 and 107, whose host has two cores, four on the larger models. This
driver registered four on every model.

## What changed

- **npuep sets up as many queues each way as there are CPUs, at most four** - two on a host with two cores,
  four on the XGS 136 - and allocates the receive and transmit buffers for those alone.
- **`dev.npuep.0.giu.queues`** says how many; **`hw.npuep.giu.queues`** at the loader forces 1 to 4, to try
  a count where there are more CPUs. The attach says it too: `giu: 4 queues each way, 4 CPUs`.

## Measured on the XGS 136

The XGS 136 (vendor version 21, platform XGS_DT2, four CPUs, its coprocessor set up for four queues), FreeBSD
15.1-RELEASE-p6, its release module 0.1.0-alpha.10.5 in `/boot/modules`. The build made on the appliance against
its own kernel's sources, clean with `-Werror`, and loaded in place from a test directory, `/boot/modules`
untouched, behind the script that restarts the appliance onto the release module if its ports, its heartbeat
or its LAN bridge are not back in 150 s. The same traffic each time: 100 pings from the appliance to the PC,
50 MiB pushed into its LAN over ssh, and 20 s of a download on its WAN.

| | two queues forced - its coprocessor set up for four | four, the rule |
|---|---|---|
| receive queues used | q0 alone | all four |
| frames received | 370,613 | 49,304 |
| written empty by the coprocessor (`rx_zero_written`) | **77,204 - 20.8 %** | **0** |
| refused (`rx_bad`) | 13,017 more | 0 |
| 50 MiB into its LAN | **50 s** | **1 s** |
| pings answered | 96 of 100 | 100 of 100 |

The two forced queues made on the XGS 136 the XGS 107w's own picture - one queue, about a fifth of the frames
written empty and more refused outright, and transfers crawling - and its own count of four
took it away again. A host that sets up a different number of queues than its coprocessor is set up for gets
empty frames from it; the XGS 107 is set up for two, and this driver gave it four.

Afterwards the XGS 136 was taken back as it was: the loader setting cleared, the test directory removed, and
restarted onto its release module.

## What this does not settle

- **Nothing here ran on a 107.** The rule gives an XGS 107 or 87 two queues - their host has two cores - which
  is what the vendor's driver takes on them; the 107w is where it is measured next. The XGS 87 shares the
  107's board and coprocessor setup and has not been on a bench.
- **Why the coprocessor writes empty frames on a mismatch** is on its side, and not read here.
