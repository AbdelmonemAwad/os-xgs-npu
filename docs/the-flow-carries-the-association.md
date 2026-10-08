# The flow carries the association

Measured on an XGS 3300, 2026-10-08, the night after
[one encryptor per association](one-encryptor-per-association.md). Until now the coprocessor did a
tunnel's arithmetic and the host did its forwarding: every packet of every tunnel crossed the host.
This page puts a connection that runs through a tunnel in the coprocessor's flow table **with its
association**, so the host leaves that connection's data path the way it left a plain one's. One TCP
stream through the tunnel went from **75 to 195 Mbit/s across the host to 680 to 750 with both
directions in hardware**, the host between a hundredth and a fifth of one core busy. It is behind a
setting with three values, and the third has a price that was read in the fast path's code first and then measured.

## Read before it was written

Five questions, one reader each, four of them checked by a second reader, before a line of driver
code. What came back decided the design.

- **How the fast path treats a frame it has just decrypted.** It looks the inner packet up in the
  ordinary flow table with the ordinary key: the port the ESP frame arrived on, that frame's link
  addresses, the inner five-tuple. Nothing in the key says "this was decrypted", and on a hit the
  fast path asks neither whether the frame was decrypted nor by which association.
- **How a flow encrypts.** A microflow carries an association handle and revision. A stale or
  invalid association *punts* the frame to the host; it is never forwarded in the clear. Sequence
  numbers come from one ordered queue per association, shared by frames the host hands over and
  frames the flow table encrypts.
- **What the vendor's own host programs.** Exactly that shape: one connection, the direction that
  leaves encrypted naming the association with a next hop at the tunnel's far end, the other
  direction a plain microflow learned from the decrypted frame.
- **What the kernel needs back.** `ipsec_accel_drv_sa_lifetime_update` is the only way traffic the
  host never sees reaches an association's lifetime, and nothing but the kernel's own cipher
  advances the counter it watches to ask for a rekey before the sequence space runs out.
- **What this driver already had**, and one latent fault in it: the guard of
  [the morning before](the-kernel-drives-the-coprocessor.md) asked the policy about a connection in
  one orientation only, so a tunnel connection opened from the far side was not recognised as
  covered.

## One connection, two microflows that are not alike

A tunnelled connection is [one connection entry with two microflows](one-connection-two-microflows.md)
like any other. The two differ:

| | the direction that leaves encrypted | the direction that arrives decrypted |
|---|---|---|
| learned from | the plaintext frame punted from the LAN port | the frame the driver terminates after decryption: its metadata names the inner flow and the association that decrypted it |
| names an association | yes: the handle is [the index plus one](the-handle-is-the-index-plus-one.md), with its revision | no. It must carry 0; a handle here means *encrypt again* |
| next hop | the tunnel's far end, on the association's port, with the port's own MTU. The fast path takes the envelope off that itself | the inner destination, like a plain flow |
| when its association goes | the fast path punts its frames | **nothing happens by itself** |

Which direction is which comes from the kernel's policy database and never from who opened the
connection: four policy lookups the way `ipsec4_forward` makes them, then the kernel's own choice of
association for the outbound policy. A connection is programmed only if that association is one the
coprocessor holds, the tunnel is one ESP tunnel over IPv4 with matching ends, nothing translates the
connection, the encrypting direction's next hop leaves by the association's port and the other
direction's does not, the plaintext frame did not arrive on the tunnel's port, and - for the
decrypted direction - the frame in hand was decrypted by an inbound association of the *same*
tunnel. Both directions or neither, as for plain connections, with one exception that is the
operator's.

## The setting, and what its third value costs

`dev.octep.0.ipsec.flows`, also the loader tunable `hw.octep.ipsec_flows`:

| value | in hardware | |
|---|---|---|
| 0, the default | nothing of a connection a policy covers | every packet crosses the host, as before |
| 1 | the direction that leaves encrypted | the decrypted direction still crosses the host, where the kernel's inbound policy check sees every packet |
| 2 | both directions | the host sees neither |

The first bullet of the reading is why 2 is a separate choice. The decrypted direction's microflow
matches on port, link addresses and inner tuple, and the fast path does not ask whether the frame
was decrypted - so **a frame that arrives in the clear on the tunnel's port, from the link address
the tunnel's own frames come from, carrying the inner addresses and ports of a connection that is
in hardware, is forwarded to the LAN as if it had come through the tunnel.** In software the kernel
refuses it. It was measured rather than argued: a UDP flow through the tunnel, and the far end
sending twenty datagrams of the same five-tuple *outside* the tunnel through a socket with a bypass
policy.

| `ipsec.flows` | clear-text datagrams delivered to the LAN machine | refused by the kernel's policy check |
|---|---|---|
| 0 | 0 of 20 | 20 |
| 1 | 0 of 20 | 20 |
| 2 | **15 of 20** | **0** |

Who can do that: the tunnel's peer, a machine on the segment the tunnel's port is on that can borrow
the upstream router's link address, or the upstream router itself. It reaches only connections that
are in hardware at that moment, and only toward the LAN. That is narrow, and it is still a packet
entering a network the policy says only ESP may enter, so the driver never chooses it for anyone.
Nothing was found in the fast path that closes it; what has not been tried is in
[issue 304](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/304).

## Keeping it true while things change

A microflow outlives the reasons it was made for unless somebody takes it out. Each of these was a
separate way to be wrong.

- **An association is removed.** Before the `SA_DEL`, every connection that names it is moved to
  its successor - another association under the same head in the kernel, which is what a rekey
  leaves, same direction, same port, ready, the newest - with one `MFLOW_PROGRAM`; with no successor
  the connection is taken out. The order matters for the decrypted direction: its microflow is tied
  to nothing on the far side, and once no inbound association of the tunnel exists it would go on
  forwarding that tuple *in the clear*.
- **The policy changes.** A connection in hardware sends the host nothing to ask about. So when the
  kernel's policy generation moves, the once-a-second sweep asks the policy about every connection
  again: a plain one that a tunnel now covers goes, a tunnelled one whose tunnel is gone or whose
  policy now names another tunnel goes, and the next frame makes it again the way things stand.
  The same test is applied to the one connection a punted frame names, before anything else is
  said about that frame.
- **`ipsec.flows` is lowered.** The connections that have more in hardware than it now allows are
  taken out, and the places that write a connection read it again under the lock.
- **A command is not answered.** That is "not yet", not "no": the far side may still carry it out.
  The entry stays, marked, counted as programmed, and an audit takes it out as soon as the far side
  answers - for a plain connection too, since a plain connection that exists in hardware with no
  record here would forward in the clear after a tunnel came up over it. The same audit asks of
  every tunnelled connection, every second, whether the associations it rests on still exist.
- **The fast path gives a connection back.** Ten packets in a row in one direction with the same
  acknowledgement, end and window is its sign of a retransmission storm; it stops using the
  connection and hands every frame to the host until the host reclaims it. A tunnelled connection
  is asked about at the first poll that sees it punted and made again from the same frame. With
  only the encrypting direction in hardware a *download* trips that rule on every lost segment -
  the direction in hardware is the acknowledgements - so such a connection is left with the host
  for a minute instead of being made again every few seconds.

Measured on the final module, one UDP flow of fifty datagrams a second for a minute, `ipsec.flows` 2:

| second | what was done | what the appliance did |
|---|---|---|
| 0 to 12 | tunnel up | 505 of 505 frames encrypted by the flow, 505 decrypted and forwarded by the flow |
| 12 | tunnel taken down from the far end | `flow_gone` +1: the connection left with its associations |
| 12 to 30 | no tunnel, so no policy: ordinary routed traffic, in software too | accelerated as a plain connection, 1,328 frames forwarded plain |
| 30 | tunnel brought up again | `flow_reval` +1: the sweep took the plain connection out; `flow_made` +1 |
| 36 to 60 | | plain forwarding fell to the 4 frames in ten seconds an idle table shows; 520 of 520 by the flow again. 2,997 of 3,000 datagrams answered over the minute |

## The counters belong to the index

While every packet crossed the host the host counted it. A packet a microflow forwards is one the
host never sees; the engine counts those, and only those, so the two counts are disjoint: the host
goes on recording what it handles, and once a second the driver reads the engine's totals for the
associations that connections name - two associations a pass - and hands them to the kernel. It
also moves the kernel's own sequence counter to where the coprocessor has got to, because that
counter is what asks for a rekey at 80 % of the space and it no longer moves by itself.

It passed every test, and then one line of the status page was read instead of glanced at: an
association **three minutes old had told the kernel it had carried 6.4 GB**. The engine's counters
are per *index*, which the code knew - it took a baseline when an association was installed. What
it did not know is what the counters read at that moment. Watched eight times a second across a
re-establishment:

| | index 1 reads |
|---|---|
| the old association, removed | 6,441,061,920 bytes |
| straight after `SA_ADD` of the new one | **0** |
| after the first packet the engine takes through the new one | **6,441,061,920**, and counting on from there |

So the baseline was 0 and everything every earlier occupant of the index had ever carried arrived
as the new association's. With a byte lifetime configured that is an association that expires the
moment its first connection is accelerated. The fix: the baseline is what the index read *before*
the `SA_ADD`, or failing that the last thing the driver ever read there; every reading is accounted
as a difference from the one before; and a difference that runs backwards, or is more than the port
could have carried in the time, becomes a new starting point and is counted in `stat_rebase`
instead of being told to the kernel. After it, on indexes whose earlier occupants had carried 1.5
and 4.0 GB, a five-second upload at 776 Mbit/s: the new associations reported **498,493,188 and
13,792,760 bytes** - the upload and its acknowledgements - while the engine's own totals for the
two indexes read 4.54 and 1.55 GB.

One inexactness is left ([issue 302](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/302) has the
whole account), and it is the engine's own grain. Watched a hundred times a second under
a four-stream upload, an index's counts move in steps of about 16,380 packets: the engine publishes
them in batches. The last partial batch of an association that is removed under load is not in its
final reading, and it stays unpublished - the removed index read the same for as long as it was
watched - until the engine next uses that index, when it arrives with whichever association is
there. At most one batch, about 24 MB of full-size packets. Seen once: 16,148 packets and 23 MB on
an association that had carried 33,160 small datagrams. The margin kept on the sequence counter is
sixty-four times a batch.

## What three reviews caught

Each round of the change was read by other readers before or beside the hardware runs, and each
round found things the runs had passed. The ones that would have hurt:

- a write through a null pointer in the path that attaches a direction, older than this change and
  reachable from the hand instrument;
- the kernel's replay state used after it was freed, when the kernel clones an association for a
  changed address: the clone owns that state, and the removal path wrote to it;
- connections matched to a departing association by its handle alone, and a successor chosen by
  the tunnel's two ends alone, so that another child's association could inherit them; and "the
  same tunnel" decided by the address of a kernel structure the driver held no reference on;
- the encrypting direction's egress port taken from wherever the frame had come from, which a
  frame arriving on the tunnel's own port could steer;
- a rekey taking every connection out, because for a moment the kernel's choice of association is
  one the coprocessor does not have yet;
- an unanswered command leaving a microflow that may exist with no record of it; and each further
  candidate in the same poll waiting its own two seconds with the transmit path's lock held;
- connections left with the host still spending the poll's eight attempts, every second, for their
  minute.

## Measured

Two OPNsense appliances joined at 10 Gbit/s, a tunnel between them, a machine on a **1 Gbit/s** LAN
port of the XGS 3300, TCP payload in Mbit/s, the appliance's host in busy cores of eight. Twelve
seconds a row unless it says otherwise; several figures in a cell are separate runs. One module for
the whole table, one kept log.

| | `ipsec.flows` 0 | 1 | 2 |
|---|---|---|---|
| one stream up | 75, 76, 147 | 194, 275; 220 over a minute | **744, 750; 731 over a minute** |
| one stream down | 100, 168, 195 | 271, 622 | **681, 740; 723 over 30 s** |
| four streams up | 215, 743, 913, 918 | 935 | 929 |
| four streams down | 956 | 951; 939 over 30 s | 909; 904 over 30 s |
| eight streams up | | | 965 |
| host, four streams up | 0.89 to 1.30 cores at 743 to 918 | 0.55 | **0.18** |
| host, one stream up | 0.10 to 0.17 | 0.10 to 0.14 | **0.01 to 0.08** |
| host, four streams down | 0.69 | 0.75 | 0.58 |

What was in hardware, from the far side's own counters:

| row | encrypted by a flow | decrypted and forwarded by a flow |
|---|---|---|
| 1, four streams up | 945,677 of 998,122 frames, 95 % | none, by design |
| 1, four streams down | 6 %: the acknowledgement direction is given back and left with the host | none |
| 2, one stream up | 793,719 of 793,724 | 793,907 of 793,948 |
| 2, four streams up | 85 % | 85 % |
| 2, one stream down | 90 % | 690,807 of 761,970, 91 % |
| 2, four streams down | 40 % | 376,126 of 1,017,102, 37 % |

- **A rekey under a four-stream upload**, the child rekeyed from the far end five seconds in: 923
  and 924 Mbit/s over the sixteen seconds with `ipsec.flows` 2 and 1, `flow_repoint` +12 and +4,
  **no replay drop at the peer**, nothing dropped by the driver.
- **Packets too big for the tunnel**, inside an accelerated connection, `ipsec.flows` 2, 900
  datagrams each. With DF the fast path punts them and the host answers *fragmentation needed*
  (seen on the build before the last of this change, and not run again on the last). Without DF, at 1,447 bytes - the first size that does not fit - and at 1,488 and
  1,500, **the fast path fragments before the envelope**: 882, 847 and 882 of 900 in hardware, the
  first few by the host while the connection was being made, all 900 answered. A datagram the
  sender itself fragmented is never a flow: 900 of 900 by the host.

The four-stream rows sit at the LAN port's gigabit whatever carries them; what the setting changes
there is who does the work. The one-stream rows are where the host path is the limit, and why it is
so far below the port is not known ([issue 298](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/298)):
the host is a tenth of a core busy at 75 Mbit/s. The first four-stream upload after a boot ran at
215 with nothing dropped anywhere and the next three at 743 to 918, also across the host, also
unexplained. The rows with both directions in hardware did not do that.

## Where it stands

- **Off by default.** `ipsec.flows` 1 is the one to turn on without a second thought: the
  encrypting direction in hardware, the kernel still checking everything that arrives. 2 is faster
  and is the exposure above; it is a decision, not a default.
- **Only with the trigger on** (`dp.auto`), and only for what the trigger takes: TCP and UDP over
  IPv4. One ESP tunnel per connection, AES-GCM, no translation inside the tunnel, no
  UDP-encapsulated associations, no IPv6 inside.
- **A download at the port's capacity is given back every few seconds** by the fast path's rule for
  retransmissions - every lost segment produces the ten duplicate acknowledgements it counts - and
  made again by the next poll, a second later. Throughput holds at the port's rate because the host
  carries the connection meanwhile; a third to nine tenths of it is in hardware. Turning the fast
  path's sequence checking off does not stop the rule. The poll's one second is the bound on
  recovery, and that is the next thing to change
  ([issue 303](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/303)). Changed the next
  morning: [a give-back is answered at once](a-give-back-is-answered-at-once.md), and 95 to 97 %
  of such a download is in hardware.
- **The inbound anti-replay window matters at these rates.** With strongSwan's default of 32
  packets the coprocessor dropped 28 to 43 frames of a four-stream download as outside the window;
  with 1,024 it dropped none, and the share of that download in hardware was 57 % against 37, one
  run each.
  The coprocessor takes whatever window the kernel was given.
- **After a tunnel is taken down the appliance has no policy for its addresses**, and traffic to
  them is ordinary routed traffic, accelerated like any other. That is what the policy database
  says and what software does; a tunnel that must never fall back to the clear needs a policy that
  survives it, which is the key daemon's to install.
- **The kernel's sequence counter for an association that flows use is kept a million numbers
  ahead** of the driver's own reckoning, so that anything that resumes from it starts past the
  coprocessor. That is how it is written; the number was not read back on hardware, and the path
  that needs it - the kernel cloning a mirrored association for a changed address - has not been
  exercised. A last read of the code found the margin missing for a second or two after a
  connection first names an association, and for a flow shorter than one of the engine's batches:
  [issue 306](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/306). Closed with the same
  change, and the counter read back.
- **What an association reports while flows use it is a second or two behind**, and more with many
  associations in use: two are read per pass. It is also exact only to the engine's batch, as above.

| under `dev.octep.0.ipsec` | meaning |
|---|---|
| `flows` | the setting |
| `flow_made` | tunnelled connections put in the flow table |
| `flow_policy`, `flow_nosa`, `flow_shape`, `flow_clear` | a covered connection left with the host, and why: the setting, an association the coprocessor does not hold, a shape it does not carry, a frame in the clear where the policy wants ESP |
| `flow_wait`, `flow_backoff` | waiting for the other direction to be seen; left with the host after a give-back |
| `flow_repoint`, `flow_gone` | directions moved to a successor association; connections taken out with theirs, or because the setting was lowered |
| `flow_reval`, `flow_audit` | taken out because the policy over them changed; taken out by the once-a-second audit |
| `stat_polls`, `stat_pushed`, `stat_rebase` | the engine asked for an association's counts; counts handed to the kernel; readings that could not have been the association's own |
| `table` | per association: connections that name it, and what its flows have added since it was installed |

## Lessons

- **Read the number that is not being tested.** Every test passed with an association claiming
  6.4 GB it had never carried. It was on the screen the whole time.
- **A baseline is a claim about a counter's past.** Reading it once says nothing about what it
  reads next; watch it across the event before trusting it.
- **What the far side does not check, it will not refuse.** The exposure was in the disassembly as
  an absence. An absence is measured by trying it.
- **Not answered is not refused.** Freeing the record of a command that timed out is how a flow
  comes to exist that nobody will ever take out.
- **A fix for the common case makes a new common case.** Probing a connection at its first punted
  frame recovered downloads and made every probe a two-second wait when the far side is silent.
- **Other readers find what the hardware forgives.** Three rounds, and the hardware had passed
  each of them before the reading was done. A fourth, after the last measurement, found two more
  on paths no test reaches; they are written down rather than fixed blind into a module that had
  just been measured.
