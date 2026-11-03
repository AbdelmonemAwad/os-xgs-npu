<!-- reviewed: 2026-11-03 -->
# The TCP window checked in hardware on IPv6 and on a translated connection, XGS 3300, 2026-10-27

[679](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/679): the check of a connection's TCP window in
the fast path was measured on 2026-10-22 on plain IPv4 connections
([tcp-window-2026-10-22.md](tcp-window-2026-10-22.md)), and that page left two kinds out: IPv6, and a
connection pf translates. Both are measured here on the XGS 3300, with the same segments. The check holds
on both: every segment outside the window was dropped in hardware with the check on, and none with it off.

## How it was measured

As on 2026-10-22 (`images/deploy/lab-679-v6nat.sh`, made from `lab-tcp-inject.sh`):

- **The stream:** iperf3 from the PC, through the second XGS 3300 (vendor version 22, the box under
  test), to the first XGS 3300 (vendor version 23), which runs the server. Both run `octep.ko 211004b5`
  (main as of #842).
- **The segments:** at second 8 of the stream, the server's end puts on the wire, by BPF and past its own
  pf, copies of the stream's own segment with the sequence moved (`tcp-inject.py`, which handles IPv6
  since this measurement): five data segments 64 MiB ahead, five 2 GiB ahead, and one RST 64 MiB ahead.
  The second XGS 3300's fast-path counters are read around each.
- **IPv6:** the PC's IPv6 address to one put on the first XGS 3300's F1 for the run, routed by the second
  XGS 3300 - no translation.
- **Translated:** the PC's IPv4 address to one put on the first XGS 3300's F1 for the run, with the
  second XGS 3300 translating the PC to its own A4 address, as the soak of
  [361](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/361) does.
- **Each run:** with `dev.octep.0.dp.tcp_window` 1 (the check on, the default), then 0 as the control.
  Every address, route and rule was added at runtime and removed after, and each report says so.

## IPv6

![IPv6, the check on: five segments 64 MiB ahead and five 2 GiB ahead each counted FROM_WIRE_DROP_TCP_SEQ_CHK_INVALID +5 and TX_DROP +5; the RST handed to the host, TCP_FIN_SYN_RST +1, and the connection given back](../images/measurements/tcp-window-v6-nat-2026-10-27/01-second-3300-ipv6-check-on.png)

![IPv6, the check off: none of the eleven dropped; the RST handed to the host](../images/measurements/tcp-window-v6-nat-2026-10-27/02-second-3300-ipv6-check-off.png)

## Translated

![Translated, the check on: 5 and 5 dropped, FROM_WIRE_DROP_TCP_SEQ_CHK_INVALID +5 each time; the RST handed to the host, and pf's state-mismatch count up by one; 9.01 Gbit/s](../images/measurements/tcp-window-v6-nat-2026-10-27/03-second-3300-nat-check-on.png)

![Translated, the check off: none dropped; the RST handed to the host; 8.54 Gbit/s](../images/measurements/tcp-window-v6-nat-2026-10-27/04-second-3300-nat-check-off.png)

| | 64 MiB ahead (5) | 2 GiB ahead (5) | RST 64 MiB ahead (1) |
|---|---|---|---|
| IPv6, check on | **5 dropped** | **5 dropped** | to the host, the connection given back |
| IPv6, check off | none dropped | none dropped | to the host, the connection given back |
| translated, check on | **5 dropped** | **5 dropped** | to the host, the connection given back, refused by pf |
| translated, check off | none dropped | none dropped | to the host, the connection given back |

The IPv6 run with the check on was taken twice, the second time for the photograph, and dropped 5 and 5
both times. The first time ran at 8.15 Gbit/s, the second at 2.34.

## What it does not mean

- **The RST still goes to the host** and takes the connection out of the fast path with it, as on
  2026-10-22. In these runs, about 190,000 to 350,000 of the stream's frames went through the host
  after it - given back, then not yet active - before the connection was made again.
- **pf's state-mismatch count did not move the same way each time.** It went up by one for the blind
  RST in the translated run and in the first IPv6 run, as the closing check of 2026-10-24 has it
  ([closing-check-2026-10-24.md](closing-check-2026-10-24.md)). In the second IPv6 run it did not. That
  run also counted 8 retransmissions handed to the host around the RST. Whether that connection had
  been given back first, so that pf saw the RST another way, is not shown by these counters.
- ~~**The XGS 136 was not run.** Its driver checks the window the same way on IPv4
  ([tcp-window-2026-10-22.md](tcp-window-2026-10-22.md)); IPv6 and a translated connection through
  it are still to be measured.~~ Measured on 2026-11-03, below.

## The XGS 136, 2026-11-03

The same segments through the XGS 136 (vendor version 21, npuep.ko `d0d9952c`, main as of #996),
`images/deploy/lab-679-v6nat-136.sh`: the PC to the second XGS 3300 over the XGS 136's lab link, the
second XGS 3300 running the server and putting the segments on that link (its oxp9). IPv6 from the PC's
address on the XGS 136's LAN to one of the second XGS 3300's own, routed by the XGS 136; translated from
the PC on the XGS 136's Port13 to the second XGS 3300's end of the link, the XGS 136 translating the PC to
its own end by one rule added for the run and taken away after. `dev.npuep.0.flow.tcp_window` 1, then 0.

| | 64 MiB ahead (5) | 2 GiB ahead (5) | RST 64 MiB ahead (1) |
|---|---|---|---|
| IPv6, check on | **5 dropped** | **5 dropped** | to the host, the connection given back, refused by pf |
| IPv6, check off | none dropped | none dropped | to the host, the connection given back |
| translated, check on | **5 dropped** | **5 dropped** | to the host, the connection given back, refused by pf |
| translated, check off | none dropped | none dropped | to the host, the connection given back |

- **The check holds on both here too:** every segment outside the window was dropped in hardware with it
  on (`FROM_WIRE_DROP_TCP_SEQ_CHK_INVALID` +5 each time), none with it off.
- **pf counted the blind RST both times the check was on** - its state-mismatch count up by one in the
  IPv6 run and in the translated one - and not with it off. The stream ran its 24 s in all four runs.
- **The RST took the connection out of the fast path**, as on the XGS 3300: 26 to 1,049 of the stream's
  frames reached the host as `CONN_RECLAIM_PENDING` after it.
- **Before that, at the stream's start,** 17,367 to 55,743 frames reached the host in the first nine
  seconds while its flow was not yet active (`FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE`) - what
  [885](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/885) is about.

![The XGS 136, IPv6, the check on: five segments 64 MiB ahead and five 2 GiB ahead each counted FROM_WIRE_DROP_TCP_SEQ_CHK_INVALID +5; the RST handed to the host, TCP_FIN_SYN_RST +1, CONN_RECLAIM_PENDING +377; pf state-mismatch 0 to 1; 300 Mbit/s for 24 s](../images/measurements/tcp-window-v6-nat-2026-10-27/05-136-ipv6-check-on.png)

![The XGS 136, IPv6, the check off: none dropped; the RST handed to the host; pf state-mismatch unchanged](../images/measurements/tcp-window-v6-nat-2026-10-27/06-136-ipv6-check-off.png)

![The XGS 136, translated, the check on: 5 and 5 dropped; the RST handed to the host, CONN_RECLAIM_PENDING +1,049; pf state-mismatch 1 to 2; the translation gone after](../images/measurements/tcp-window-v6-nat-2026-10-27/07-136-nat-check-on.png)

![The XGS 136, translated, the check off: none dropped; the RST handed to the host; pf state-mismatch unchanged; 844 Mbit/s](../images/measurements/tcp-window-v6-nat-2026-10-27/08-136-nat-check-off.png)
