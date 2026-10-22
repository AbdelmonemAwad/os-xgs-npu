<!-- reviewed: 2026-10-22 -->
<!-- board: TCP window checked | XGS 3300 and XGS 136 | out-of-window: dropped -->
# The TCP window of a connection in hardware, checked there. Measured, 2026-10-22

Until this, a TCP connection the coprocessor forwarded had its window checked by nobody: the driver
sent its connection entry with the TCP block at zero and the check off, and marked pf's state sloppy,
because pf would otherwise drop the connection's frames against a window it no longer saw move
([679](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/679), [the
discovery](../findings/discoveries-2026-10-22.md#the-tcp-window-in-hardware)).

Now, on both families:

- **A connection is made with its window from pf's own numbers** - each direction's `seqlo`,
  `seqhi`, `max_win` and window scale - and the fast path checks every segment against it, the way
  the vendor's own conntrack hands it over. A segment outside the window is dropped in hardware and
  counted (`FPCNTR_FROM_WIRE_DROP_TCP_SEQ_CHK_INVALID`).
- **A take-back gives pf the fast path's numbers back** and clears the sloppy mark the driver set, so
  pf checks the connection again on the host.
- **A connection pf rewrites in every packet is not made at all** - `modulate state`,
  `synproxy state`, `scrub reassemble tcp` - since the fast path would forward its sequence numbers
  or timestamps unchanged. A rule's own `sloppy` is kept, and the check stays off for it; so is a
  tunnel's connection, which has one direction in hardware.

New counters, `dev.octep.0.dp.*` and `dev.npuep.0.flow.*`: `tcp_checked`, `tcp_unchecked`,
`tcp_rewrites`, `tcp_handback`, `tcp_kept`, `tcp_lost`, `tcp_revived`, and `tcp_last`, which says
what the last take-back of a checked connection did with its window. `tcp_window` 0 sends every
connection as before. `tcp_revive`, `tcp_from_segs`, `tcp_seg_waits` and `tcp_segs_last` belong to
the instruments [below](#after-the-fast-path-gives-a-connection-back). The modules measured:
**octep.ko 7cd98513** on the second XGS 3300, **npuep.ko 0806653b** on the XGS 136.

## How it was measured

Each run in a window of its own, photographed (`images/deploy/lab-tcp-window-series.ps1`); the text of
every run, these and the ones before them, is kept in the run log.

- **XGS 3300**: iperf3 from the PC, on the second XGS 3300's F1, through the second XGS 3300 to the
  first, which runs the iperf3 server. The lab tunnel between the two 3300s covers that pair, and
  is taken down for each run and brought up after.
- **XGS 136**: iperf3 from the PC on the 136's LAN (Port3) through the 136's Port8 to the second
  XGS 3300, which runs the server.
- **Segments outside the window** (`lab-tcp-inject.sh`, `lab-136-tcp-inject.sh`): at second 8 of a
  stream, the server's end puts on the wire, by BPF and past its own pf, copies of the stream's own
  segment with the sequence moved (`tcp-inject.py`): five data segments 64 MiB ahead - outside any
  window, inside the fast path's liberal span of 2^30 - five 2 GiB ahead, and one RST 64 MiB ahead.
  The DUT's fast-path counters are read around each.
- **The take-back** (`lab-tcp-handback.sh`, `lab-136-tcp-handback.sh`): at second 8 of a stream - and
  on the XGS 3300 a second time at second 3 - its connection is handed back with the Flows page's
  "hand back" and kept with the host; pf's states are printed before, just after, and ten seconds
  later, and pf's `state-mismatch` count is read across those ten seconds. The line after "handing
  back" is `tcp_last`: the last take-back of a *checked* connection - when it names another
  connection, the one handed back was no longer checked.
- **Ordinary streams** (`lab-3300-tcp-run.sh`, `lab-136-tcp-run.sh`): four streams down for 20
  seconds, the check on and off, and what the fast path counted over the run.

## Segments outside the window

![XGS 3300, octep.ko 7cd98513, the check on: five segments 64 MiB ahead and five 2 GiB ahead each counted FROM_WIRE_DROP_TCP_SEQ_CHK_INVALID +5 and TX_DROP +5; the RST handed to the host, TCP_FIN_SYN_RST +1, and the connection given back](../images/measurements/tcp-window-2026-10-22/01-3300-segments-outside-the-window-check-on.png)

![XGS 3300, the check off (tcp_window 0, as before the fix): none of the eleven dropped](../images/measurements/tcp-window-2026-10-22/02-3300-segments-outside-the-window-check-off.png)

![XGS 136, npuep.ko 0806653b, the check on: 5 and 5 dropped, FROM_WIRE_DROP_TCP_SEQ_CHK_INVALID +5 each time; the RST handed to the host; 938 Mbit/s](../images/measurements/tcp-window-2026-10-22/06-136-segments-outside-the-window-check-on.png)

![XGS 136, the check off: none dropped, 934 Mbit/s](../images/measurements/tcp-window-2026-10-22/07-136-segments-outside-the-window-check-off.png)

| | 64 MiB ahead (5) | 2 GiB ahead (5) | RST 64 MiB ahead (1) |
|---|---|---|---|
| XGS 3300, check on | **5 dropped** | **5 dropped** | to the host, the connection given back |
| XGS 3300, check off (as before) | none dropped | none dropped | to the host |
| XGS 136, check on | **5 dropped** | **5 dropped** | to the host, the connection given back |
| XGS 136, check off (as before) | none dropped | none dropped | to the host |

## The take-back

![XGS 3300, the PC uploading: the stream's connection handed back at second 8; pf's two states for it lose their sloppy mark and take the fast path's numbers, and ten seconds later have moved on, state-mismatch +0; 1.76 Gbit/s](../images/measurements/tcp-window-2026-10-22/03-3300-take-back-upload.png)

![XGS 3300, the PC downloading: the connection had been given back and revived unchecked before second 8 (TCP_MAX_RETRANS), so pf's states stay sloppy; state-mismatch +0](../images/measurements/tcp-window-2026-10-22/04-3300-take-back-download.png)

![XGS 3300, both ways at once: handed back, two pf states took the fast path's numbers, state-mismatch +0; 7.28 and 6.24 Gbit/s](../images/measurements/tcp-window-2026-10-22/05-3300-take-back-both-ways.png)

![XGS 3300, uploading, the take-back at second 3: the connection had been revived unchecked already, left sloppy, state-mismatch +0](../images/measurements/tcp-window-2026-10-22/18-3300-take-back-upload-at-second-3.png)

![XGS 3300, downloading, at second 3: given back already, left sloppy, state-mismatch +0](../images/measurements/tcp-window-2026-10-22/19-3300-take-back-download-at-second-3.png)

![XGS 3300, both ways, at second 3: handed back, two pf states took the numbers, state-mismatch +0](../images/measurements/tcp-window-2026-10-22/20-3300-take-back-both-ways-at-second-3.png)

![XGS 136, the PC uploading: handed back, two pf states took the fast path's numbers, the sloppy mark gone, state-mismatch +0 over ten seconds with the host; 929 Mbit/s](../images/measurements/tcp-window-2026-10-22/08-136-take-back-upload.png)

![XGS 136, the PC downloading: handed back, two states took the numbers, state-mismatch +0; 937 Mbit/s](../images/measurements/tcp-window-2026-10-22/09-136-take-back-download.png)

![XGS 136, both ways: handed back, two states took the numbers, state-mismatch +0](../images/measurements/tcp-window-2026-10-22/10-136-take-back-both-ways.png)

| | pf took the fast path's numbers | pf's state-mismatch over 10 s with the host |
|---|---|---|
| XGS 136 - up, down, both ways | **yes, all three** | +0 each |
| XGS 3300 at second 8 - up, down, both ways | **yes**, no (revived unchecked before), **yes** | +0 each |
| XGS 3300 at second 3 - up, down, both ways | no (revived unchecked before), no (given back before), **yes** | +0 each |

pf never refused a frame of a connection it took back - neither with the fast path's numbers nor
left sloppy.

## Ordinary streams

![XGS 3300, four streams down, the check on: 9.91 Gbit/s, no segment dropped by the check](../images/measurements/tcp-window-2026-10-22/11-3300-four-streams-down-check-on.png)

![XGS 3300, four streams down, the check off: 9.78 Gbit/s](../images/measurements/tcp-window-2026-10-22/12-3300-four-streams-down-check-off.png)

![XGS 136, four streams down, the check on: 941 Mbit/s, no segment dropped by the check](../images/measurements/tcp-window-2026-10-22/13-136-four-streams-down-check-on.png)

![XGS 136, four streams down, the check off: 933 Mbit/s](../images/measurements/tcp-window-2026-10-22/14-136-four-streams-down-check-off.png)

![XGS 3300, a connection through the lab tunnel with ipsec.flows 1: made unchecked - tcp_unchecked +2, the stream's and iperf3's own - and encrypted on the coprocessor, 1,719,926 frames, 8.15 Gbit/s](../images/measurements/tcp-window-2026-10-22/21-3300-a-connection-through-the-tunnel.png)

| four streams down, 20 s | check on | check off | ordinary segments dropped by the check |
|---|---|---|---|
| XGS 3300 | 9.91 Gbit/s | 9.78 Gbit/s | **0** |
| XGS 136 | 941 Mbit/s | 933 Mbit/s | **0** |

With the default settings no ordinary segment was dropped by the check in any run of the day: these,
and the others on both families before the series.

## After the fast path gives a connection back

The fast path gives a connection back to the host by its own rule - ten segments in a row with the
same acknowledgement, end and window (`USFP_MAX_RETRANS` in the vendor's `conn_table.h`), which a
loss recovery's duplicate acknowledgements are - and the driver revives it in place with the command
it made it with (`FLOW_CREATE_FP`). That copy is the state the fast path works from, so **a revived
connection goes back unchecked** (`tcp_revive` 0, the default), as every revival did before, and its
take-back leaves pf sloppy. Under four streams at 10 Gbit/s that is about 150 revivals in 20
seconds; a connection the fast path never gives back stays checked to its end.

Two ways of sending a revival back checked were built and measured on the XGS 3300, and both dropped
ordinary segments, so both are instruments and neither is the default:

- `tcp_revive` 1: the window the fast path answers the reclaim with.
- `tcp_revive` 2: a window made from the latest segment the host saw in each direction, with room
  for what the host still forwards after reading them, waited for up to 200 ms.

![XGS 3300, tcp_revive 2, four streams down: 9.79 Gbit/s, 100 windows made from segments, and 11 ordinary segments dropped by the check](../images/measurements/tcp-window-2026-10-22/15-3300-four-streams-down-revival-from-segments.png)

![XGS 3300, tcp_revive 2, segments outside the window: 5 and 5 dropped - in a run in which the connection was not given back before them](../images/measurements/tcp-window-2026-10-22/16-3300-segments-outside-the-window-revival-from-segments.png)

![XGS 136, tcp_revive 2, four streams down: 949 Mbit/s, four connections made with a window from segments, none dropped](../images/measurements/tcp-window-2026-10-22/17-136-four-streams-down-revival-from-segments.png)

Every run of four streams down on the XGS 3300, 20 seconds each, by how a revival was sent back:

| a revival sent back | runs | ordinary segments dropped by the check, each run |
|---|---|---|
| unchecked (`tcp_revive` 0, the default) | 7 | 0 in every one |
| with the fast path's own window (`tcp_revive` 1) | 4 | 0, 0, 14, 151 |
| from the host's latest segments, no room | 1 | 233 |
| the same, room of twice the window or 4 MiB | 3 | 5, 310, 43 |
| the same, and segments younger than 50 ms and the last revival | 5 | 0, 19, 233, 0, 0 |
| the same, room of 16 MiB | 5 | 0, 0, 0, 0, 0 |
| the same, and every revival trying again after one sent back unchecked | 6 | 510, 36, 68, 51, 321, and 11 (photograph 15) |

The injected segments were dropped with `tcp_revive` 2 in a run of 25 revivals, as well as in
photograph 16. And `dtrace` on `octep_tcp_segs` showed the numbers each window was made from to be
consistent - about 1.7 MB in flight, the PC's window 2 MiB - while some revivals found one direction
silent for a quarter of a second during the give-back: its acknowledgements were not reaching the
host. That is where this stops, open in [679](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/679).

## What it does not mean

- **A connection after its first give-back is not checked**, as above - and on a busy link that is
  most of a long connection's life; a take-back of it leaves pf sloppy.
- **A FIN or RST is not checked in hardware**: the fast path hands it to the host
  (`TO_KN_TCP_FIN_SYN_RST`) and gives the connection back, and pf, sloppy for it, takes it - so a
  blind reset is judged by nobody, as before.
- **A rule's `sloppy` is kept**, and **a tunnel's connection is sent unchecked**, on both families.
- **IPv6 was not measured** with the check, nor a translated connection.
- **The XGS 3300's upload figures are the far end's**: the first XGS 3300, the iperf3 server, had
  been up 29 hours with 90,762 receive-watchdog rescues ([361](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361)),
  its receive running at 0.4 to 2.5 Gbit/s and the PC's uploads given back by the fast path all the
  time - so that the runs taken then found every connection given back before the segments or the
  take-back came. It was rebooted - three times in all - and the XGS 3300's runs here were taken
  from its last fresh start, the upload take-back first; downloads, where it sends, ran at 9.8 to 9.9 Gbit/s.
- **The XGS 3300's module changed once during the series**: from octep.ko 6e3320d7 to 7cd98513, which
  sends a tunnel's connection unchecked, as the XGS 136's driver does - with ipsec.flows 1 only one of
  its directions is in hardware, and a take-back would have given pf the other's numbers as they
  were when it was made. Every XGS 3300 photograph here is of 7cd98513.
