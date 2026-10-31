<!-- reviewed: 2026-10-31 -->
# A third look at the XGS 107w's zero headers, on the XGS 136 first, 2026-10-31

[890](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/890): the [second look](npuep-second-look-2026-10-31.md)
ran on the XGS 107w. Its owner ran two two-minute downloads: at 119 kB/s with the second look on, 1,644,130
frames received and 87,724 with tag 0 and all-zero metadata that it did not put right; at 122 kB/s with it
off, 1,810,949 and 95,210. Not one zero header was right on a second look, and turning the look off did not
change the speed. The lengths that cannot be a frame, 201,155 of them and all under a header, did not grow
during either download. So the loss the downloads meet is the zero headers, and it is not a write landing a
few microseconds late.

What the second look kept of four of them says more. Each descriptor is a real frame's: IPv4 at byte 14 of
the frame with a 20-byte header, 132 or 168 bytes with the sixty-six in front - 132 is a TCP
acknowledgement with timestamps - written at offset 0. The buffers hold zeros from their first byte to byte
104, 168, 188 or 288: over the whole frame in three of them, over the first 104 of 168 bytes in the fourth.
And three of the four descriptors' own buffer addresses lie exactly where the cookie's buffer would be if the
buffers were laid out one after another, 16 KiB apart - so as far as these show, the device was told the
right buffer, and either wrote zeros into it, or wrote the frame later still, or wrote it somewhere else.

This build asks which.

## What changed

- **A zero header's buffer is held back a pass and looked at again.** Instead of going back to its pool at
  once, the buffer of a frame whose header is still zero after the second look is kept out of it, one at a
  time, and looked at again on the next receive pass at least 500 us later - a millisecond, where the second
  look waits ten microseconds - then given back. What it holds by then is counted, `rx_late_ok` for a header
  there and `rx_late_bad` for still zero, and the last one kept in `dev.npuep.0.giu.rx_late_last`: how long
  it was held, and its shape.
- **The descriptor's own buffer address is compared with its cookie's buffer,** for every frame - the device
  repeats in the descriptor the address the pool descriptor it took gave it. One that is neither that buffer
  nor that buffer plus the frame's offset is counted in `rx_addr_mismatch`, and in `rx_addr_other` when the
  address is another of the receive buffers; the last is kept in `dev.npuep.0.giu.rx_addr_last` with both
  buffers' shapes, so a frame written into another buffer would be seen there.
- **A buffer's shape says more:** besides the tag, the metadata and the EtherTypes at bytes 12 and 78, which
  blocks of 16 bytes of the first 512 are all zeros, and at the first byte that is not zero what a frame
  beginning there would have - the front port whose tag those two bytes are, and the EtherTypes 12 and 78
  bytes on. A frame written whole at another offset would show as a port's tag there.
- **`rx_refused_last` keeps the last slot of each kind,** a length that cannot be a frame and a zero header,
  so a download's zero headers no longer hide the lengths' record.
- **`dev.npuep.0.giu.rx_good_last`** keeps the descriptor of the last frame delivered to a front port, to read
  a refused one's beside.
- **`dev.npuep.0.giu.rx_late_test`** holds back the next frames' buffers, up to 100, whatever they hold - so the
  third look can be tried where no header is ever zero; those are counted apart, in `rx_late_tested`.

`giu.stats` has a line for the third look.

## Measured on the XGS 136

The XGS 136 (vendor version 21, platform XGS_DT2), OPNsense 26.7.8_1 on FreeBSD 15.1-RELEASE-p6, its
release module 0.1.0-alpha.10.2 in `/boot/modules`:

| | |
|---|---|
| build | on the appliance, against its own kernel's sources: clean with `-Werror` |
| load | in place from a test directory by that directory's `reload.sh`, `/boot/modules` untouched, behind a script that would have restarted the appliance onto the release module had its front ports, its heartbeat or its LAN bridge not come back in 150 s: back after 87 s, 14 ports, heartbeat alive |
| traffic | 30 s of a download on its WAN - 147,860 frames on Port1 - and 200 MiB pushed into its LAN over ssh in about 3 s - 147,569 frames, most on Port3 |
| frames received | 296,619 |
| third looks | none: `rx_addr_mismatch` and `rx_addr_other` 0 - every descriptor named its cookie's buffer; `rx_late_ok` and `rx_late_bad` 0; `rx_untagged`, `rx_bad`, `rx_stale`, `rx_nobuf` 0; `rx_refused_last`: no slot of either kind stayed wrong |
| a good frame's descriptor | `0e 05 00 12 00 80 7e 00 ...`, on Port3: the same first four bytes - IPv4 at byte 14 of the frame, a 20-byte header - as the XGS 107w's zero headers had; nothing in the descriptor tells them apart |
| the hold, tried | `rx_late_test` 5, then 100, under pings: `rx_late_tested` 105, each buffer looked at again 678 to 1,351 us after it was held and given back - `rx_double_return` and `rx_stale` 0, no ping lost; `rx_late_last` showed the header there, Port3's tag at byte 0 |
| the setting | `rx_late_test` takes 0 and 100 and refuses 101 and -1 |

So on a board whose receive path is right the third look never runs; it costs a frame one comparison of its
buffer address, and a good frame a copy of its 32-byte descriptor. The search of the buffers for an address
that is not the cookie's did not run here: no descriptor gave it one.

## What it can tell on an XGS 107w

- `rx_late_ok` climbing - headers there a millisecond later - is a frame written after its descriptor by more
  than the second look waits; the fix is then to wait for it, and the loss goes with it.
- `rx_addr_mismatch` climbing, with `rx_addr_last` showing a header in the address's buffer, is a frame written
  into another buffer than its cookie names; the fix is then on the pool's side.
- `rx_late_bad` climbing with `rx_addr_mismatch` at 0 is a buffer the device wrote zeros into: the frame's
  bytes were zeros when the coprocessor sent them. The shape in `rx_late_last` and `rx_refused_last` - which
  blocks are zeros, and what follows them - is then the lead.

Not run on an XGS 107 or 107w: there is none here.
