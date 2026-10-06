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

## After the fix

_To be filled from the verification run._

## Two lessons

**When every gauge on both ends is clean, read what you put in.** Content was checked - byte by
byte on the receiver - and it was right. The thing that was wrong was never on the wire to be
captured: it was the absence of a translation in a request, visible only by reading the request back.

**And a module can link without half its sources.** The fix was first built in a checkout on the
appliance whose Makefile predated two of the source files; `make` compiled what it knew, linked a
module with its own symbols undefined, said BUILD OK, and the next boot came up with no driver, no
front ports and no bridge. `contrib/octep/build.sh` now fails a build that leaves a symbol of its own
undefined, which is where that check is cheap.
