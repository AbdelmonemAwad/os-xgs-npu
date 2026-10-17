# The MTU is the LIF's, and a frame is as many buffers as it needs

Measured on an XGS 3300, 2026-10-12, modules `dac136a1`, `d93f152c`. Until this day a front port's
MTU could not pass 1,506, and the reason given was the receive ring: one buffer size, 1,602 bytes
less the 82-byte prefix and the Ethernet header, published to the coprocessor once per boot
([the measurements](../measurements/xgs3300.md#jumbo-frames-are-not-available-and-the-number-is-1506),
now struck). Both halves of that were true and neither was the limit. The limit was two things the
driver had never asked for, and after this day a port's MTU is any value from 72 to 9,000, set
with `ifconfig` or OPNsense's interface page, changed at any time, no reboot and no re-arming.

## What was in the way, measured first

**The fast path refuses a frame longer than its LIF's MTU before the ring ever sees it.** With
the peer's jumbo bridge sending DF pings of 1,480 to 8,972 bytes of data at the 10G port, every one
above 1,472 was lost and the coprocessor's `FROM_WIRE_DROP_MTU_EXCEEDED` rose by one per frame; the
port's 10G MAC had passed a 9,014-byte frame to it. The LIF's `mtu` is a field in the vendor's
`lif_table.h` (`MAX_MTU 9000`), the bring-up script installs every LIF at 1,500, and the vendor's
host sets it from `ndo_change_mtu`. Raised to 9,000 through the driver's own instruments (a
selective `LIF_ADD_UPDATE` with the MTU bit alone), the drops stopped and the frames reached the
ring.

**The coprocessor writes a frame longer than a buffer over as many buffers as it needs.** That is
the rule the driver already encoded for the coprocessor's own statistics messages
([a packet is not a buffer](a-packet-is-not-a-buffer.md)): the first buffer holds the length word,
the prefix and what is left, every later one is all frame. The 9,014-byte frame arrived in six
buffers (`dp.rx_spans` 12, `rx_span_bufs` 24, `rx_span_max` 9,088 after twelve pings of six
sizes), was counted, and was dropped - "taken whole and read by nothing".

## What changed in the driver

- **Receive**: a frame of more than one buffer is gathered into one jumbo cluster (9 or 16 KB,
  taken only for such a frame) in the order the buffers were written, so everything downstream
  sees the one contiguous copy it has always seen - the termination of a decrypted frame, the
  flow candidate, the stack. The extra buffers are poisoned and credited as before.
  `dp.rx_span_nobuf` counts a frame dropped for want of a cluster.
- **Transmit**: a slot's buffer holds a 9,000-byte frame with its VLAN tag and the private tag in
  front (9,344 bytes a slot, 256 slots: 2.3 MB), and the two length checks use it. The receive
  ring's buffer size is untouched.
- **The port**: `ifconfig oxpN mtu` takes 72 to 9,000, tells the port's LIF first with the same
  selective update, and keeps the old MTU if the LIF cannot be told.

The first build had the slot's virtual address on the new stride and its physical address on the
old one; the coprocessor read every transmit from the wrong slot and the appliance's WAN was gone
for three minutes until the second build. The one line is in the record.

## The measurements, module `d93f152c`

The 10G port at 9,000, the peer's bridge at 9,000, frames in the clear on the 10G path:

| | |
|---|---|
| DF pings from the peer, data bytes | 1,472 to 8,972 all answered; **8,973 refused** - a 9,015-byte frame, one over the MTU, at the LIF |
| gathered frames over the run | `rx_spans` 2,029,355, `rx_span_nobuf` 0, `rx_span_max` 9,088; the coprocessor's drops: `TX_DROP_QUEUE_FULL` +7 |
| TCP from the appliance to the peer, one stream 8 s | **7,386.6 up / 7,178.4 down Mbit/s** |
| four streams 10 s | **8,703.6 up / 8,876.7 down** |
| the same port put back to 1,500, one stream | 1,853.3 / 2,323.5; a 1,522-byte frame refused again |

So a host-sourced stream on the 10G path goes four times as fast at 9,000 as at 1,500, which is
the per-frame cost of this receive path shown plainly, and the limit moves both ways on the spot.

Through the tunnel, the pair mirrored, the port at 9,000:

| | |
|---|---|
| the largest DF ping through it | 8,918 bytes of data answered, 8,922 refused: an inner packet of 8,946, which is 9,000 less the envelope's 54, to the byte |
| the appliance's own TCP, one stream / four streams | 2,007.9 up / 1,502.8 down; 2,366.4 / 1,856.8 (one of the four up streams timed out) |
| the same at 1,500 | 1,974.6 / 1,467.5 |
| the driver over it | `rx_done` +6,117,326, `rx_bad` 0, `tx_encrypt` +6,605,231 = `FROM_KN_TO_IPSEC_ENCR`; `rx_spans` +2 |

The tunnel carries a jumbo inner packet both ways and the pair's counters stay clean, but TCP
through it did not grow its segments - two gathered frames in six million - because the peer
clamps the MSS of every connection crossing its bridge to 1,406 (its own fix for
[the first connection's stall](../measurements/xgs3300.md#the-first-connection-through-the-tunnel-and-where-the-mss-clamp-has-to-sit)),
and a decrypted connection crosses it. That is the peer's setting, not this path's; the ping says the path is there.

## What is not established

- **The ten ports behind the switch.** Their frame limit is the 88E6193X's per-port jumbo mode,
  ~~not read yet (the `mvsw` tool over the coprocessor's console)~~ read the same evening with
  `mvsw` over the coprocessor's console: Port Control 2 bits [13:12] are **1 = 2,048 bytes** on
  all ten (the eight RJ45s and the two cages), and **2 = 10,240** on port 0, the coprocessor's
  uplink - so a frame behind the switch is capped at 2,048 by the switch itself, whatever the LIF
  and the host allow, and raising a port is one write of that field, ~~not made yet~~ made at every
  boot since 2026-10-17, on all ten ports of both appliances (`scripts/xgs/switch-jumbo.sh`). The machine on
  the bench behind one of them keeps a 1,500 MTU that only its owner can raise. The cage ports of
  the measurements above are the two 10G ones on the coprocessor itself; the driver's side is the
  same for all twelve.
- **The bridge.** FreeBSD's bridge sets its members' MTU when its own is set, so OPNsense's LAN
  bridge at 9,000 would tell every member port and every member's LIF; not run, for the reason
  above.
- **Memory under a jumbo flood.** 9 KB clusters are taken per gathered frame and freed with it;
  3,302 were in the cache after the run; a sustained inbound jumbo flood at 10 Gbit/s was not run.
- **The coprocessor's flow path at 9,000**: `dp.auto` was on and the flows made were the usual
  host-path ones; whether an accelerated flow forwards a 9,000-byte frame port to port was not
  measured.

## Lessons

- **The number was right and the reason was wrong.** 1,506 was the arithmetic of one buffer; the
  coprocessor never needed one buffer, and said so in a counter the driver had kept for a year.
- **Two addresses for one slot are one bug waiting.** The virtual and the physical address of a
  transmit slot were computed in two places with one constant; changing the constant in one of
  them took the appliance off its WAN. One macro for the stride, used in both, is the fix and the
  rule.

## Where it sits

The gathering is in [octep_dp.c](../../contrib/octep/octep_dp.c) (`octep_dp_oq_pass`, the span
branch); the MTU ioctl in the same file; the LIF update in
[octep_rpc.c](../../contrib/octep/octep_rpc.c) (`octep_rpc_lif_mtu`); the sizes in
[octep.h](../../contrib/octep/octep.h) (`OCTEP_DP_IF_MTU_MAX`, `OCTEP_DP_TXBUF_SIZE`). The rows are
in [the measurements](../measurements/xgs3300.md#the-mtu-from-72-to-9000-on-the-spot).
