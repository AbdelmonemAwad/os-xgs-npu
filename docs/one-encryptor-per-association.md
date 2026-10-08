# One encryptor per association

Measured on an XGS 3300, module `22ff96a7`, 2026-10-07, the evening of the day
[the kernel drove the coprocessor](the-kernel-drives-the-coprocessor.md) for the first time. With the
coprocessor's cipher switched on, **an upload through the tunnel stopped dead**. The cause was in
that page's design, the fix replaced its outbound half, and the measurement that followed withdrew
its conclusion: on a path whose ceiling is not somebody else's router, four streams through the
tunnel run at **912 to 951 Mbit/s with the coprocessor's cipher and 149 to 256 with the kernel's**.
(The second pair was this driver's receive ring: with the receive path mended the kernel's cipher
runs the same rows at 947 to 967 - [a visit is not a pass](a-visit-is-not-a-pass.md),
[a ring that fills](a-ring-that-fills.md).)

## The upload that stopped

| four streams, twelve seconds | download | upload |
|---|---|---|
| kernel's cipher | 924 Mbit/s | 320 Mbit/s |
| coprocessor's cipher, as merged in the morning | 927 Mbit/s | **11 Mbit/s** |

One upload stream: 2.8 Mbit/s and a timeout (issue 300). The counters said why in two numbers that were the same
number: the forward hook had passed **14** packets per stream back to the kernel, and the peer had
dropped **14** as replays.

An ESP sequence number may be used once. The coprocessor keeps its own counter for an association it
encrypts on; the kernel keeps another, which stands still at wherever it was when the coprocessor
took over. The pfil hook of the morning handed the coprocessor every forwarded packet it could and
*passed the awkward ones back to the kernel* - fragments, packets with no next hop yet, and packets
too big for the tunnel. An upload is made of full-size packets, a full-size packet plus the tunnel's
overhead does not fit the port's MTU, so every data packet of an upload was awkward: the kernel
encrypted it, numbered it from its own stale counter, and the peer's window had seen that number
long ago. A download only sends acknowledgements up the tunnel, which fit, which is why the
morning's tests never saw it.

And the hook could not have been repaired case by case, because it only ever saw forwarded packets.
A packet the appliance generates itself takes the kernel's own offload path when it leaves by the
association's interface, and the kernel's cipher when it leaves by any other. Every road that does
not end at the coprocessor is a second encryptor.

## Where the kernel's cipher stood

So the driver stopped filtering packets and took the one place they all pass. An association's
cipher is called through a table, `sav->tdb_xform`: input, output, cleanup. For an outbound
association the coprocessor has taken, that pointer now leads to a copy of the kernel's ESP table
whose **output** entry is the driver's (`octep_ipsec_interpose`). The kernel does everything it
always did - the policy lookup, the choice of association, enc0's capture and firewall rules, the
outer header - and at the step where it would call `esp_output` it calls `octep_ipsec_xf_output`,
which hands the inner packet to the coprocessor. Forwarded, generated here, a fragment, too big:
there is no other way to that association's cipher.

| what arrives at the output step | what happens to it |
|---|---|
| the association is not mirrored | the kernel's own `esp_output`, which is then the only encryptor on it |
| mirrored, and it fits | to the coprocessor |
| too big, DF set | answered with ICMP *fragmentation needed* carrying the size that fits, and dropped. The sender shrinks its segments and nothing is ever fragmented |
| too big, DF clear | fragmented **before** the envelope, each fragment its own ESP packet - the coprocessor emits one frame per packet and cannot fragment what it has encrypted |
| no next hop toward the far end yet | dropped; the lookup has just sent the ARP request |
| the association installing or leaving, a policy with a bundle, IPv6 inside | dropped, and counted. Never passed on. (An association *installing* is no longer in this row: the kernel's cipher carries it until the coprocessor has it - [installed first, taken second](installed-first-taken-second.md)) |

The other way in is the kernel's own offload path, which tags a plaintext frame and sends it to the
interface. The transmit strips the link header the stack put on it - it is for wherever the *inner*
destination routes - and sends the packet down the same road, so the two ways share one answer to an
oversize packet. They have to: the kernel measures a tagged packet without the outer header, and
eight sizes it calls fitting do not fit.

The largest inner packet at an MTU of 1500 is **1446 bytes**: 20 of outer header, 8 of ESP, 8 of IV,
the packet, two trailer bytes padded to a four-byte boundary, 16 of ICV.

Three things about the sequence number, each of which is a line in `octep_ipsec.c`:

- **It is seeded.** `SA_ADD` carries the kernel's counter as it stood, so a peer that has already
  seen packets from the kernel's cipher is not shown their numbers again.
- **It is read after the stragglers.** The record goes into the table not ready, the kernel's cipher
  is taken away, every packet already inside `esp_output` is waited out (`NET_EPOCH_WAIT`), and only
  then is the counter read. (That order cost every packet sent during the wait, and it has been
  turned round: the coprocessor is started a million numbers ahead while the kernel's cipher is
  still running, and the counter is read after the stragglers only to check - [installed first, taken second](installed-first-taken-second.md).)
- **It is handed back.** An association can outlive its mirror - the interface goes, or the kernel
  clones the association for a changed address and frees the original. The kernel's cipher then
  resumes on a counter that has not moved since the coprocessor took over. So when an association is
  taken out, between the two `SA_DEL` stages, the kernel's counter is put past the last number the
  coprocessor can have used: one per frame it was handed.

## One nonce space, too

The sequence number is not the only number two encryptors must not share. Under AES-GCM the eight
bytes after the ESP header are the nonce, and a nonce used twice under one key gives away more than
a dropped packet. Read off the wire at the peer, the two ciphers fill them differently:

| who encrypted | sequence | the eight IV bytes |
|---|---|---|
| the coprocessor | `000ea8e8` | `00000000 000ea8e8`: the sequence number |
| a FreeBSD kernel | `000eb66c` | `00000000 000eb66b`: its own counter, `sav->cntr`, which starts at zero |

Going in, that is harmless: the kernel has used the IVs below its count and the coprocessor starts
above it. Coming back it is not. If the kernel's cipher ever runs again on a mirrored association,
it resumes from an IV the coprocessor used long ago, under the same key and salt - and moving the
kernel's *sequence* counter, which is what the hand-back above does, does not touch it. For a
cloned association it could not: a clone shares the replay state by pointer and takes its IV
counter by value, when it is made.

So the kernel's IV counter is moved when the association is *taken*, not when it is given back:
into the top half of its 64-bit space, which a 32-bit sequence number cannot reach, while the
kernel's cipher is stopped and before any clone can exist. (It is moved while the kernel's cipher
is still running now, under the lock that cipher takes its numbers under; and "before any clone can
exist" was never true - a clone can exist by then, and is refused: [installed first, taken second](installed-first-taken-second.md).)

    octep0: ipsec: outbound association spi 0xc348bffb on oxp0: coprocessor index 2, handle 3, rev 1, drv_spi 15, after sequence 0, kernel IV counter moved to 0x8000000000000000

**That line is the whole of the measurement.** The kernel's cipher resuming on a mirrored
association has not been made to happen on this appliance - it takes an address change, or an
interface leaving under a live tunnel - so the IVs it would then send have not been seen on a wire.

The two rows also say what the upload that stopped had cost besides the upload. The kernel numbered
its IVs from zero and the coprocessor from one, on one key: every packet the kernel was passed that
evening but its first left under a nonce the coprocessor used as well. The key was a lab tunnel's
and was replaced at the next rekey.

## What review caught before it ran

The change was reviewed by four readers with one lens each before the module was installed, and
five of their twenty-four findings were checked by a second reader. Two were panics. None was the
nonce: the review followed the sequence number, which the peer's window had made visible.

- **The command buffer was wiped through a null pointer.** The key is cleared from the RPC command
  buffer after `SA_ADD`; with the RPC facility never configured there is no buffer, and 200 bytes
  were written at address zero with the driver's mutex held.
- **The module could be unloaded from under the kernel.** An association's `tdb_xform` points into
  `octep.ko`, the kernel calls its cleanup through it when the association is freed, and nothing
  stopped `kldunload`. Putting the kernel's table back is not enough, because a cloned association
  carries the pointer and the driver never hears of it. **The device now refuses to detach** once
  any association has been interposed - `kldunload: can't unload file: Device busy`, measured with a
  tunnel up - and the host reboots to unload. The documented coprocessor reboot began with that
  unload; whether stopping without unloading is enough for it has not been measured.
- The softc the output step uses was forgotten by `dp.stop` and never found again by `dp.start`:
  forwarded packets would have fallen through to the kernel's cipher while tagged ones went to the
  coprocessor.
- Wiping the whole buffer after an `SA_ADD` that timed out would have turned the command still on
  the ring into command 0 - `FW_STATE_REV_SET`, revision 0. Only the two key fields are cleared,
  and only after an answer.
- An outbound association was accepted by owning its source address alone; with the route to the far
  end leaving by another port, every packet would have been dropped for the association's life. The
  route is checked when the association is offered.

## Measured

Correctness, with the gate opened by the boot tunable and nothing typed after that:

| | |
|---|---|
| the tunnel comes up | both associations mirrored, `after sequence 0` |
| a full-size packet with DF from the LAN | `Reply from <the appliance>: Packet needs to be fragmented but DF set` |
| 1446 bytes with DF | answered by the peer, three of three |
| 1447 bytes with DF | refused at the sender, which has learned the size |
| three 3000-byte UDP datagrams, no DF | **9000 bytes** at the peer's listener; `out_fragmented` +3 |
| three 3000-byte pings, no DF | the requests arrive the same way; the replies do not come back **in this lab**: the peer's link MTU is 9000, it sends each reply as one 3 KB ESP frame, and the appliance's port drops it - `FROM_WIRE_DROP_MTU_EXCEEDED` +3. A mismatch of the lab's cabling, named so the row above is not read as luck |
| a ping the appliance itself sends into the tunnel | three of three, by the kernel's tagged path: traced, `ipsec_accel_output` returned 1 three times and the output step was not entered |
| three pings forwarded from the LAN, same trace | `ipsec_accel_output` returned 0 three times and the output step was entered three times |
| a rekey five seconds into a four-stream upload, five times | **828 to 933 Mbit/s** across it, new associations at fresh indices, the peer's replay drops **0** every time |
| the same five rekeys, at the handover | the kernel's cipher had sent 0, 1, 955, 0 and 1 packets on the new association before the driver was offered it, and the coprocessor went on from there; 0, 370, 0, 1,100 and 740 packets were dropped while it installed |
| packets passed to the kernel's cipher on a mirrored association | 0, all evening |
| `kldunload octep` with a tunnel up | `can't unload file: Device busy`; the module, the gate and the twelve interfaces as they were |

Throughput. The LAN machine has a 1 Gbit/s link; the two appliances are joined at 10 Gbit/s through
a switch; twelve seconds, four streams, TCP payload:

| | download | upload | the appliance's host |
|---|---|---|---|
| kernel's cipher, strongSwan's default replay window of 32 | 149 Mbit/s | 256 Mbit/s | 1,966 and 13,279 replay drops |
| kernel's cipher, window 1024 | 219 Mbit/s | 501 Mbit/s | no replay drops; the peer retransmits 1,469 |
| **coprocessor's cipher**, window 32 | **951 Mbit/s** | **912 Mbit/s** | 0.85 and 1.23 of 8 cores |
| coprocessor's cipher, window 1024 | 932 Mbit/s | 914 Mbit/s | eight streams: 989 and 965 |

With the coprocessor's cipher the ceiling is the LAN machine's own link. Rebuilt and rebooted twice
more that night, the module repeated it in five pairs of runs - 942 to 963 Mbit/s down, 865 to 940
up - and in one it did not: the first pair after a reboot ran at 657 and 557 with the same counters
as the runs that filled the link and nothing on the appliance to account for it. The load generator
is the LAN machine itself.

**Why the kernel's cipher is slow here** is a reading of those counters, not a separate
measurement. On a 10 Gbit/s front port the ESP frames reach the kernel out of order: with a window of
32 the kernel drops thousands as replays, and with a window of 1024 it drops none and the sender
still retransmits, which is TCP meeting reordered segments. The coprocessor checks the window and
decrypts in the order the frames arrived, before anything is spread over the host's receive rings,
and what it hands the host is plaintext hashed by its own flow. Its own window of 32 lost 44 frames
in a million.

(That reading was wrong in its middle. An association's ESP frames are not spread over the host's
receive rings: they arrive on one. They were put out of order on that ring, by the block writing
round it when it had filled and the host reading the newest lap first - [a ring that fills](a-ring-that-fills.md).)

One stream alone is slow on this cabling in *both* modes - 75 to 360 Mbit/s, with next to no
retransmissions - while each of four streams together runs faster than one does alone. That is not
the cipher and it is not explained here; it is issue 298. The disorder the kernel's cipher meets is
issue 297. (Explained since, for the stream the host hands to the coprocessor: the receive watchdog
took the ring from its handler and left it. That stream reads 755 to 797 Mbit/s now - [a visit is not a pass](a-visit-is-not-a-pass.md).
The rows with the kernel's cipher were not run again.) (They have been since: [a ring that fills](a-ring-that-fills.md).)

## The morning's number, withdrawn

[The kernel drives the coprocessor](the-kernel-drives-the-coprocessor.md) measured 358 against
355 Mbit/s and concluded that the offload bought no throughput because every packet still crosses
the host. **The measurement was right and the conclusion was not.** That morning both appliances'
WAN ports sat on a home router's LAN, and the peer sent its half of the tunnel *through* that
router, so the ceiling of every run was the router. The appliance's host was 95 % idle during the
software run, which was on the page and was not read as what it was: a host that is idle is not the
bottleneck. On a direct cable between the two appliances, the same day, the same tunnel ran at
924 Mbit/s with the kernel's cipher and 927 with the coprocessor's - the cable's own gigabit - and
the coprocessor's cost the host a quarter less.

Every packet does still cross the host, and [the flow path carrying the association](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/293)
is still what takes the host out of it. But the cipher alone is not worth nothing. (It did, the
next night: [the flow carries the association](the-flow-carries-the-association.md).)

## Where it stands

- **One encryptor**, by construction, for every packet the kernel would have encrypted.
- **Refused, and left to the kernel whole:** UDP-encapsulated associations. They were accepted in the
  morning and neither direction of the data path could carry them.
- **Dropped on a mirrored association:** IPv6 inside an IPv4 tunnel, both ways. A tunnel that carries
  both families in one association loses its IPv6 when it is mirrored.
- **The ESP frame leaves the port straight from the coprocessor.** It does not pass pf on the way
  out and makes no state there; the inner packet was filtered on enc0 like any other.
- **The association's byte and packet counts** are right because every packet still crosses the
  host, which counts it. `if_sa_cnt` answers from `SA_GET_STATS`, and nothing in this kernel asks
  it; when a flow carries the association in hardware the driver will have to push the counts.
  It does now: [the flow carries the association](the-flow-carries-the-association.md).
- **The gate is a loader tunable**, `hw.octep.ipsec_on`. The kernel offers an association once, when
  it is installed, so a sysctl set after boot is too late for every tunnel that came up before it.
- **A rekey under load loses what is sent while the new association installs.** This kernel
  prefers the newest association the moment it exists (`net.key.preferred_oldsa` is 0 on OPNsense),
  the driver takes the cipher away before the coprocessor has answered `SA_ADD`, and for those
  milliseconds a packet is dropped rather than handed back: up to 1,100 at 900 Mbit/s in five
  tries. Installing first and taking the cipher second would close it; issue 299. (It did: [installed first, taken second](installed-first-taken-second.md).)
- **The module cannot be unloaded** once an association has been interposed.
- **Nothing counts packets against the 32-bit sequence space.** The kernel forces a rekey at 80 % of
  it by watching its own counter, which no longer moves, and the association is installed on the
  coprocessor without a packet lifetime. With strongSwan's one-hour default the space is out of
  reach; a day-long lifetime at a sustained 50,000 packets a second is not. Closed with
  [the flow carries the association](the-flow-carries-the-association.md): the driver moves the
  kernel's counter once a second to where the coprocessor has got to. On an earlier build of that
  change it read 1,078,901 against 1,078,901 frames handed over.

| counter, under `dev.octep.0.ipsec` | meaning |
|---|---|
| `out_taken` | packets handed to the coprocessor where the kernel's cipher stood |
| `out_orig` | packets passed on to the kernel's cipher there: their association is not mirrored |
| `out_needfrag`, `out_fragmented` | too big for the tunnel: answered, or fragmented before the envelope |
| `out_nonhop`, `out_drop` | dropped rather than encrypted by anyone |
| `tx_encrypt`, `tx_nosa`, `tx_bypass`, `tx_toobig` | the transmit path: frames laid out as ESP; the kernel's tag naming an association not held here; marked as needing none; the backstop, which should stay at zero |

## Lessons

- **A fallback is a second implementation.** "Pass it back to the kernel" read as the safe choice
  and was the bug: on a shared sequence space the safe choice is to drop.
- **Take the choke point, not the traffic.** A filter on forwarded packets could never see the
  packets that were not forwarded; the cipher's own call site sees them all.
- **Name the ceiling before reading the number.** An idle host and a number that did not move meant
  the limit was somewhere else. It took a cable to find out where.
- **Test the direction that carries the data.** A download exercises the tunnel's upload path with
  acknowledgements only.
- **List every number that must not repeat, then look at each on the wire.** The sequence number
  was guarded because a counter showed it failing. The nonce fails silently; it was found by
  printing five frames.
