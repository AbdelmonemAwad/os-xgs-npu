<!-- reviewed: 2026-10-31 -->
# A second look at a receive slot that cannot be a frame, on the XGS 136 first, 2026-10-31

[890](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/890): an XGS 107w downloads at 98 to 194 kB/s on
a line that gives its owner's other machines 8 MB/s, and its driver refuses or drops about one received frame in five. Its support bundle of
2026-10-31: 158,624 receive descriptors carried a length that cannot be a frame, and 77,721 frames arrived
with tag 0 and all-zero metadata where the port tag and the fast path's metadata belong - 236,345 of
1,150,874. Around a two-minute download its owner counted 52,698 more such lengths and 110,185 more frames
on no port, of 894,279 received, at 194 kB/s; before it, 85,049 such lengths beside 18,131 good frames.
An XGS 107 showed the same two counters in [748](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/748),
on the same host processor; the XGS 126 and the XGS 136 show neither. Clearing PCIe relaxed ordering and
no-snoop ([its page](pcie-order-2026-10-25.md)) ended an earlier symptom there and not these two.

Whether those are frames that land in memory after their descriptor, or something the coprocessor writes
there in another form, the counters could not say. This build asks.

## What changed

- **A slot that cannot be right is read again before it is refused.** A length under the sixty-six byte
  header and an Ethernet header, or an offset and length past the 9,304-byte buffer, and a tag of 0 with
  all-zero metadata, are read again for up to `dev.npuep.0.giu.rx_reread_us` microseconds - 10 by default,
  0 to 100, `hw.npuep.giu.rx_reread_us` at the loader; 0 looks once, as before. The wait is spun with the
  receive lock held: the time a write takes to land, not a tick, so a slot that stays wrong costs the
  queue microseconds. The descriptor's cookie is read with the length, and a slot that changes buffers
  under the second look is refused rather than followed.
- **What the second look found is counted:** `rx_reread_len_ok` and `rx_reread_hdr_ok` for slots right on
  it; `rx_bad_len_short` and `rx_bad_len_long` for the lengths still refused, by side; `rx_reread_hdr_bad`
  for zero headers still zero, which go on as before to the frames on no port. `giu.stats` has a line
  for them.
- **The last slot still wrong is kept,** in `dev.npuep.0.giu.rx_refused_last`: its descriptor whole, and
  of its buffer only the shape - the tag, whether the metadata is zero, the EtherType where a frame with
  no header (byte 12) and one behind the header (byte 78) would have it, and the first byte that is not
  zero. No address leaves the buffer, so its owner can post it as it is.

## Measured on the XGS 136

The XGS 136 (vendor version 21, platform XGS_DT2), OPNsense 26.7.8_1 on FreeBSD 15.1-RELEASE-p6, its
release module 0.1.0-alpha.10.1 in `/boot/modules`:

| | |
|---|---|
| build | on the appliance, against its own kernel's sources: clean with `-Werror` |
| load | in place from a test directory by that directory's `reload.sh`, `/boot/modules` untouched, behind a script that would have restarted the appliance onto the release module had its front ports, its heartbeat or its LAN bridge not come back in 150 s: back after 46 s, 14 ports, heartbeat alive |
| traffic | 400 MiB pushed into its LAN over ssh - 410,913 frames, at about 30 Mbit/s, which is the PC's own pipe into ssh (the same to a plain Intel port) - and 30 s of a download on its WAN, 172,991 frames on Port1 |
| frames received | 584,006 |
| second looks | none of either kind: `rx_reread_len_ok`, `rx_reread_hdr_ok`, `rx_reread_hdr_bad`, `rx_bad_len_short`, `rx_bad_len_long` all 0; `rx_bad` 0, `rx_untagged` 0; `rx_refused_last`: no slot has stayed wrong |
| the setting | `rx_reread_us` takes 0 and 100 and refuses 101 |

So on a board whose receive path is right the second look never runs, and costs one comparison a frame -
a tag of 0, which no port has.

## What it can tell on an XGS 107

- Second looks that come out right - `rx_reread_len_ok` or `rx_reread_hdr_ok` climbing - are frames that
  land after their descriptor; the fix is then to wait for them, and the loss goes with it.
- Lengths and headers still wrong after 10 us, with `rx_refused_last` showing the same shape each time -
  an EtherType at byte 12, a frame with no header in front of it, or the first non-zero byte at a fixed
  offset - are the coprocessor writing those slots in another form, and the fix is to read that form.

Not run on an XGS 107 or 107w: there is none here.

It has since run on the XGS 107w of #890, in 0.1.0-alpha.10.2: not one zero header was right on a second
look, and turning the look off did not change the download's speed. What it found there, and the third
look that followed in 0.1.0-alpha.10.3: [its page](npuep-third-look-2026-10-31.md).
