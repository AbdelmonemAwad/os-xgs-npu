# The outbound half was programmed without its translation

Measured on an XGS 3300, 2026-10-06, on live downloads transiting the appliance, with a capture on the
receiving PC. This is the cause behind [accelerating-a-flow-breaks-it.md](accelerating-a-flow-breaks-it.md)
and issue #268, and it corrects a claim that page made.

## What the receiver saw

A download's frames were captured on the PC that was receiving them while the appliance accelerated
that connection, both directions, exactly as the automatic trigger does:

| | frames per second to the PC | TTL |
|---|---|---|
| before the flow was programmed | about 11,000 | 52 |
| from the moment it was programmed | **0, for ten seconds** | - |
| after the flow was discarded | a trickle, climbing | 37 |

Every frame that reached the PC had a correct IP header checksum and a correct TCP checksum - zero
failures across the capture. The PC's adapter discarded nothing and counted no errors. And the fast
path's own `RX_WIRE` went flat for the same ten seconds: the server had stopped sending.

So the frames the coprocessor builds are right, nothing is dropped anywhere that counts, and the
connection stops because the **server** stops - which a server does when its data is not
acknowledged.

## One direction at a time

Programming only the outbound half of a connection - the PC's acknowledgements, on their way to the
server - the driver reported it as:

    not translated

for a connection that is translated at the WAN like every other. The connection entry read back
from the coprocessor with `LO_CONN_READ` carried a NAT block of zeros. And the connection did **not**
collapse that time: the fast path forwarded 33 frames in hardware, then counted
`FROM_WIRE_TO_KN_CONN_RECLAIM_PENDING` 216 times and handed the rest back to the host, and the
download continued at a third of its rate.

That is the whole mechanism in one run. An acknowledgement forwarded with a zero NAT block leaves
the WAN with the PC's private source address and is lost. When the fast path noticed the
retransmissions it reclaimed the connection and the host took over; when both directions were
accelerated, as the trigger does, nothing took over, every acknowledgement was lost, and the server
stopped within one window. The inbound half - the data - was always right, which is why the one
forwarded frame ever read byte by byte, in
[reading-the-frame-settled-it.md](reading-the-frame-settled-it.md), was correct: it was a data frame.

## Why the driver found no translation

`pf` holds **two** states for a connection that crosses the appliance: one made by the rule on the
interface the connection entered, and one made by the rule on the interface it left, which is where
the NAT is applied. The first has identical wire and stack keys - nothing is rewritten on the way
in. The second has the translation.

`octep_pf_state_read()` asked `pf_find_state_all()` with `PF_IN`, which searches the list of states
whose **wire** key matches. For a frame punted from a LAN port on its way out, that list holds only
the LAN-side state, whose keys agree. The WAN-side state, the one with the translation, has this
tuple as its **stack** key, which only a `PF_OUT` search reaches - and, because `pf` stores an
outbound state's key with the packet's source at index 1, only with the addresses reversed. The
driver stopped at the first match and never looked there. Frames arriving from the WAN never had
the problem: their tuple is the WAN state's wire key, and that state carries the translation.

All of this is in the kernel's own source, read on the appliance after the capture had said where to
look: `pf_setup_pdesc()` sets `sidx` to 0 for `PF_IN` and 1 for `PF_OUT`; `pf_state_key_setup()`
stores the source at `sidx`, and for a translated inbound state builds the stack key reversed;
`pf_find_state_all()` picks the wire list for `PF_IN` and the stack list for `PF_OUT`.

## Three things fixed together

**The lookup prefers the state that carries the translation.** Every key arrangement is tried in both
of `pf`'s lists; a state whose two keys differ wins, and an untranslated one is used only when no
other exists.

**The NAT block is built from the connection's two ends, not from the frame.** `pf` records which
way a connection was opened, and that names the opener and the responder in each key. The block
is then the opener in its host and wire forms and the responder in its, with `do_snat` set when the
opener's forms differ and `do_dnat` when the responder's do. The earlier derivation compared the
keys index by index and read the flag off the frame in hand; it was right for the one case it was
measured on, an outbound source-translated connection seen from its reply, and wrong for the same
connection seen from its own direction - it would have said `do_dnat` - and for every translated
inbound state, whose reversed stack key it would have read as a connection rewritten at both ends.

**The direction field is the state's, not the key's.** #269 took it from the parity of the key
arrangement that matched, on the reading that `pf` keeps the opener's source at index 0. That is
true only of states created by an inbound packet. A frame received from the wire and found through
the wire list matches with its source first whichever end opened the connection, so that parity was
0 for every frame - the field had gone from one constant to another. It is now the frame's source
compared with the opener.

## A fourth change in the same merge, and the two knobs

**The merge that carried the three fixes above also marks `pf`'s states sloppy.** When a flow is
accelerated, `octep_pf_mark_sloppy()` sets `PFSTATE_SLOPPY` on both of the connection's states, for
the rest of their lives. The reasoning: while the coprocessor forwards a connection, `pf` sees none
of its frames, so its record of each side's sequence numbers stays where the last punted frame left
it while the real window moves on; the first frame to come back to `pf` - at the discard, or handed
back by the fast path - would be judged against a window `pf` never saw advance and dropped as a
bad state, counted in `pfctl -si` as `state-mismatch`. A sloppy state keeps tracking the handshake,
`FIN` and `RST` but not the window, which is what `keep state (sloppy)` does for a rule.

It was reasoned, not measured: the verification below ran with the translation fix and the marking
together, so it did not say whether the marking was needed. So it was made a knob and measured on
its own, 2026-10-06, on the same download, both directions accelerated by hand each time, the
receive rate read at the PC's adapter and the drops read from `pfctl -si`:

| `dp.pf_sloppy` | PC receive while offloaded | `state-mismatch` while offloaded | after the discard |
|---|---|---|---|
| 1 | 444, 382, 449 Mbit/s | +0 | 555, 492, 533 Mbit/s, connection running |
| 0 | 0, 0, 0 Mbit/s | **+786** | 0 Mbit/s, connection established and dead |

With the marking off the fast path forwarded about six thousand frames and the connection died:
786 of its frames reached `pf` while it was offloaded, were judged against the window `pf` had
stopped seeing, and were dropped. The fast path hands some of an accelerated connection's frames to
the host even while the flow is active - that is what those 786 were - so the marking is not a
precaution for the discard alone; without it the connection does not survive the offload itself.
The run with the marking off on a connection a few seconds old gave the same result, 878 drops and a
dead connection, so it is not a matter of the connection's age either.

**The marking stays, and it is documented here as the fourth part of the fix.** A sloppy state
gives up `pf`'s window check for that connection; on an offloaded connection the check was already
blind, and what it was dropping was the connection.

**And the bytes are untouched.** The same 1 GB file was downloaded to disk three times through the
appliance and hashed: once with nothing accelerated, once with the connection accelerated at the
start and held offloaded to the end, and once with the flow programmed and discarded every ten
seconds, five cycles. All three copies are 1,073,741,824 bytes with the same SHA-256, and every
transfer's TLS checks passed. Whatever the offload does to a connection's frames, it does not change
their contents, in flight or at the transitions.

A note on the instrument, found while measuring this: `sysctl -n dev.octep.0.dp.accelerate` invokes
the handler **twice** - sysctl(8) asks a string's length first and fetches it second - so one read
programmed two flows all day, the frame in hand and then whichever frame was being punted a
moment later, which on a busy connection is its other direction. That is why a single read showed
two table entries and why the printed line said `0 pf state(s) marked sloppy`: the first call had
marked both. The handler now acts only on the call that can deliver its output, and one read
programs one direction - which changes how the instrument is used, measured on the appliance:

- **One read offloads one direction, and the fast path reclaims a half-offloaded connection within
  a few frames.** With only the acknowledgements accelerated it forwarded 17 frames, then counted
  `FROM_WIRE_TO_KN_CONN_RECLAIM_PENDING` 7,277 times and handed the connection back to the host.
  The same was seen in the one-direction run above, at 33 and 216.
- **Two reads back to back offload both directions**, because once the first direction is in
  hardware the frame being punted is the other one: `146,124` frames forwarded in six seconds, the
  host's own interface counter at zero while they were. That is the hand instrument's procedure now.
- `CONN_RECLAIM_PENDING` still rose alongside a working two-direction offload, by about a tenth of
  the forwarded count, because the two directions were programmed as two connection entries where
  the vendor's `FLOW_CREATE_FP` keys both microflows to one. That was the cause: with one
  connection entry per connection it is zero, measured the next day in
  [one-connection-two-microflows.md](one-connection-two-microflows.md), which also replaces the
  two-reads procedure above - one read now programs both directions, the second identity coming
  from the candidate ring.

| sysctl | default | what it does |
|---|---|---|
| `dev.octep.0.dp.pf_sloppy` | 1 | mark the connection's `pf` states sloppy when a flow of it is accelerated; 0 leaves them as they are. Marked from both of the connection's tuples since #287 - from the frame's own tuple only, half the connections kept one strict state, see [one-state-strict.md](one-state-strict.md) |
| `dev.octep.0.dp.accel_dir` | 0 | which direction of a connection may be accelerated: 0 either, 1 only the original (opener to responder), 2 only the reply |

`accel_dir` is the instrument that isolated the cause above: with it set to 1 only the
acknowledgements were offloaded and the connection survived at a third of its rate; with 2 only the
data would be. It refuses the other direction at `octep_dp_flow_make()` with a line saying so, and
it applies to `dp.accelerate` and to the automatic trigger alike. Neither knob is a setting to leave
changed: both exist so that one half of a mechanism can be switched off and the difference measured.

## After the fix

Verified 2026-10-06 on three concurrent HTTPS downloads transiting the appliance, LAN to WAN, with
the rate read **at the receiving PC** and not on the appliance - see below for why that distinction
decides the result.

| | flows programmed | rate at the PC |
|---|---|---|
| `dp.auto` off | 0 | 331, 299, 274 Mbit/s |
| `dp.auto` on, ramping | 12 -> 62 | 275, 233, 315, 294, 300, 307, 305, 315, 315, 312, 312 Mbit/s |
| flows discarded | 0 | 275, 257, 254 Mbit/s |

**The trigger filled the flow table - all 62 entries - and held it full for twenty-two seconds at
the download's own rate.** The three samples after the flows were discarded are lower than the
samples taken while they were programmed. Before the fix, one flow halved this and three stopped it.

And the frames really were going through the coprocessor. With fifteen flows programmed by hand on
the busy connection, over one three-second window:

    RX_WIRE                 +122517
    FROM_WIRE_TO_WIRE       +102509     84% of what arrived
    host idle               97% -> 99%
    oxp3 bytes on the host  +0

The microflow reads back as the driver programmed it, with `rpc.cmd=40` and
`req_flags=OCTEP_TABLE_FLAG_READ_ALL`:

    rev 0  fw_valid 1  host_valid 1
    action 1  dir 0  brctl 14  state 2  sa 0  conn 2  nhop 2 rev 1

## The meter has to be downstream of the thing being measured

`oxp3 bytes on the host +0` in that table is the whole reason this took a day longer than it should
have. **A front port's byte counter on the host counts the frames the host is given, and an
accelerated frame is never given to it.** The counter does not fall because throughput fell; it
falls *because the offload started working*, and it reads exactly zero when the offload is working
perfectly.

Every dose-response figure this project measured against that counter - including the automatic
abort rule written to protect the appliance, which fires at a quarter of baseline - was measuring
its own success as a total collapse. Four runs on the fixed module were aborted that way before the
meter was moved to the PC's adapter, where the download had never slowed at all.

The original #268 finding stands, because it was *not* measured that way: it was measured as frames
per second on the receiving PC, which went to zero for ten seconds. That is the measurement that
found the defect and the same kind of measurement that now shows it gone. The counter on the
appliance was never evidence either way.


## Three lessons

**When every gauge on both ends is clean, read what you put in.** Content was checked - byte by
byte on the receiver - and it was right. The thing that was wrong was never on the wire to be
captured: it was the absence of a translation in a request, visible only by reading the request back.

**A meter that reads zero when the work succeeds is not a meter.** The appliance's own interface
counters cannot measure an offload, because the offload's purpose is to stop those counters moving.
The quantity has to be read where it is wanted - at the machine the bytes are for.

**And a module can link without half its sources.** The fix was first built in a checkout on the
appliance whose Makefile predated two of the source files; `make` compiled what it knew, linked a
module with its own symbols undefined, said BUILD OK, and the next boot came up with no driver, no
front ports and no bridge. `contrib/octep/build.sh` now fails a build that leaves a symbol of its own
undefined, which is where that check is cheap.
