# A state that sees nothing runs out

Found and measured on an XGS 3300, 2026-10-08, while reading for something else. It is older than
anything else written this week: it has been true of the flow path since the first frame the
coprocessor forwarded by itself.

Once a connection is in hardware, `pf` sees none of its packets. That sentence is in this tree as
the reason the connection's states are marked sloppy. It has one more consequence and nobody
followed it there: **pf's state for the connection stops being restamped, and runs out under it.**
The driver's sweep then takes the connection out of hardware, exactly as it does a connection that
has ended. A UDP flow through NAT came back from that with **a different source port, once a
minute**. A TCP connection does not come back at all.

## How it was seen

The question being read was why a connection's first second still crosses the host, and the answer
wanted the moment at which `pf` holds a TCP connection as established. The driver reads `pf`'s two
peer states with everything else about the connection - and only prints them. So the next question
was what happens to a connection that is put in hardware before `pf` has seen its handshake
through, and `pf`'s source answers that in a dozen lines:

- a state keeps the timer class and the stamp of the last packet `pf` tracked. Nothing recomputes
  either for a state that stops seeing packets;
- a TCP state made by a SYN and a SYN-ACK and never shown the final ACK is on `tcp.opening`:
  thirty seconds. An established one is on `tcp.established`: a day. A UDP state is on
  `udp.multiple`, sixty seconds, only when both peers have been seen more than once, and otherwise
  on `udp.single`, thirty. An ICMP echo state is on ten;
- the purge thread removes a state the moment its stamp plus its timer is past;
- and this appliance's rules, like any OPNsense's, pass a TCP packet that has no state only if it
  is a SYN.

None of that needs a connection to be made early. It only needs it to stay in hardware longer than
the timer it was left on.

## Measured before

Main as it stood, a machine on the LAN, the appliance's own NAT.

**UDP.** One five-tuple, fifty datagrams a second each way. `pf`'s state for it counted down from
sixty seconds with nothing restamping it. At about the sixty-fifth second the sweep took the
connection out, the next datagram made a new state, and the poll made the connection again. And the
new state is a new translation:

| second | the translated source port `pf` holds |
|---|---|
| 6 to 66 | 42936 |
| 72 on | **47520** |

Every sixty-five seconds, for as long as the flow lasts. Question-and-answer traffic does not
notice: 3,837 of 3,842 were answered. Anything that keeps a session by address and port at the far
end does - a voice call, most games, a tunnel that does not roam.

**TCP.** A download in hardware, both of `pf`'s states for it `ESTABLISHED:ESTABLISHED`. The two
states were removed by hand, which is what expiry does. Within two seconds the sweep had taken the
connection out; its frames then came to the host, `pf` had no state for them, and the download
stopped: 176 MB of 1 GB in the twenty-six seconds it was given.

By itself that happens to a connection that stays busy in hardware for a day without one frame
being handed back - and, in thirty seconds, to one the poll happened to make between the SYN-ACK
and the final ACK. That second case was luck until now and would have been the rule the moment
connections were made sooner, which is what was being read for.

## Two changes

**A connection is not taken from `pf` before its states are on their long timer.** For TCP, every
state `pf` keeps for the connection - there is one per side of the appliance, and for a connection
that is not translated the one the driver reads first is the one that leads - shows both peers
established. For UDP, both peers seen more than once, which is one more datagram from the opener.
Until then the answer is "not yet", with no command sent, and the connection's next frame is asked
again. That also keeps a two-datagram exchange, which is most DNS, out of the flow table
altogether.

Two kinds of flow have nothing to wait for and are not made to. A stream in one direction - the
opener says one thing and the other end then sends for as long as it likes - never has its second
peer seen twice; `pf` by itself forwards it on its thirty-second timer for as long as it lasts. So a
UDP state that is still at that stage three seconds after it was made is taken as it is. And a
one-way stream into a tunnel with `ipsec.flows` 1 has no reply at all, and is made as before.

**`pf`'s states are kept alive for as long as the coprocessor is forwarding the connection, and
not a moment longer.** The coprocessor stamps a microflow's entry at every frame that hits it. So
every five seconds, for each connection, the driver reads each programmed direction's entry back:
still the one it programmed, and a stamp that has moved since the last reading, is traffic the
host did not see - and `pf`'s states for the connection are restamped as those frames would have
restamped them, with `pf`'s own clock, under the state's own lock. The timer class is left alone.
A connection that has gone quiet is not restamped and ends on `pf`'s timer as it always did; one
whose microflows the fast path has let go of is kept by nobody, and its next frame, which comes to
the host, restamps `pf` by itself. Restamping without asking would keep a dead connection's state,
and the driver's table entry with it, for ever.

`dev.octep.0.dp.keepalive` turns the second off, which is how it was measured.

## Measured after

Three flows through NAT at the same time, on the build this page describes:

| flow | before | after |
|---|---|---|
| UDP, a question and its answer back to back, 150 s | taken out every 65 s, a new port each time | one state, **one port**, 58 to 60 s to live at every look; gone a minute after the traffic stopped |
| UDP, one datagram every 25 s | | made at the first datagram after both peers had been seen twice; one port to the end; back to a minute after each datagram |
| a ping once a second, 150 s | | one state for the whole of it, never taken out |

- **A stream in one direction**, through the lab tunnel with `ipsec.flows` 2 because the far end
  had to be ours: one datagram out, then two hundred a second back for a hundred seconds. Made
  between the third and the eighth second; all 20,000 delivered, 19,295 of them by the coprocessor;
  both states read 26 or 27 seconds to live at every look, counted down when the stream stopped,
  and the connection was taken out some thirty-five seconds after its last frame. The build before
  this one never made it. Main makes it at once and then has nothing to restamp its state with,
  which by the reading above is half a minute's life; that was not run.
- Forty single DNS exchanges from forty ports: none of the forty was made.
- A 33-second TCP download, 952 MB, in hardware throughout: both states read a day less five
  seconds at every look.
- Ten eight-second downloads one after the other: all ten made within four seconds.

The three-flow run lost answers in a burst - 143 of 2,524 between its 37th and 67th second, the
ping beside it 7 of 150 - and the build before had done the same at another moment. It was the
path. The same flow a minute at a time, by turns in hardware and by the host, three turns each:
4,523 of 4,523 answered in hardware, 5,416 of 5,417 by the host.

Through the tunnel, the rows of [the page before](a-give-back-is-answered-at-once.md) on this
build, `ipsec.flows` 2 - TCP payload, the share of decrypted frames the coprocessor forwarded, and
the host's cores:

| | Mbit/s | in hardware | host |
|---|---|---|---|
| four streams down, 12 s | 959 | 88 % | 0.10 |
| four streams down, 30 s | 936 | 94 % | 0.18 |
| one stream down, 12 s | 791 | 99 % | 0.02 |
| one stream down, 30 s | 788 | 86 % | 0.12 |
| four streams up | 948 | 94 % | 0.10 |
| one stream up, 12 s and a minute | 785, 774 | 100 % | 0.01, 0.03 |
| eight streams up | 969 | 83 % | 0.22 |

- The twelve-second four-stream row has read 86 to 95 % on every build since that page, main
  included; the thirty-second one 94 to 97 %. The thirty-second single stream read 98 and 99 % on
  the three builds before and 86 here: 325,000 frames were handed up while the connection waited
  to be taken back, against 16,000 to 26,000 before. What made that one row different was not
  found.
- A rekey under a four-stream upload, twice: no replay drop at the peer.
- The clear-text probe, unchanged: 0 of 20 delivered with `ipsec.flows` 0 and 1, 15 of 20 with 2.
- An index reused: a five-second upload reported as 534,350,494 bytes on an index whose total
  read 14.2 GB.
- The tunnel taken down and brought up under a running connection: 2,990 of 3,000 answered.
- 10,760 commands in the session, none unanswered; twenty-four microseconds on average, six
  milliseconds at worst.

## Two readings that were not the driver's

- **A state that reads minutes to live in the moment before it disappears.** Twice in these runs
  a UDP state that had counted down to five seconds read `expires in 00:05:43` at the next look
  and was gone at the one after. It is how this kernel reports a state that is past its timer and
  not yet purged: the figure is the uptime at which it ran out. A state of the firewall's own, which
  no part of the driver touches, read `00:09:07` at uptime 547 s, two seconds after reading
  `00:00:02`.
- **One download that was never made a connection.** The first 33-second download on the last
  build crossed the host for all of its 33 seconds; the eleven after it were all made within four.
  Nothing in this change refuses an established TCP connection, and what stopped that one was not
  found. It is written into
  [#308](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/308), where connections being made
  is the subject.

## What review caught

Two readers after the first measurement and two more after the second, with a third to try to
refute them; nothing that panics or forwards wrongly.

- The first build waited for TCP only. A UDP connection was made after one datagram each way,
  which leaves its states on the thirty-second timer for good, and a restamp every ten seconds is
  not safely inside thirty for a flow with long gaps.
- Ten seconds was not inside `pf`'s shortest timer either: the comment said `udp.single` and the
  shortest is an ICMP echo's ten.
- A far side that had stopped answering cost a two-second wait with the transmit path's lock held
  in every poll.
- The second build then waited for ever: a stream in one direction never shows `pf` its opener
  twice, so it was never made at all. That is the three-second rule above.
- A state the purge thread has already marked was read as settled if its peers said so; a
  direction attached again under a new identity kept the old entry's stamp to compare with; a far
  side that stayed silent would have been told about in the log once every five seconds.
- A reader who said that two states could share one key, and only the first would be restamped,
  was refuted from the rules this appliance loads: none binds a state to an interface. A ruleset
  that does - `set state-policy if-bound` - would need the lookup to walk the list.

## Where it stands

- **Five seconds, against `pf`'s timers as they are set here.** A state survives a silence of its
  timer less about six seconds. A ruleset that sets a timer for traffic in use below that would
  need the period to follow it; it is a constant. Adaptive timeouts, which shorten every timer as
  the state table fills, are off on this appliance and are not followed either.
- **Two reads per connection every five seconds**, about twenty-five microseconds each with the
  transmit path's lock held. A full table of a thousand connections is four hundred reads a second.
- **A day has not been waited for.** That a state is restamped was watched; that a TCP connection
  busy for more than a day now survives follows from it and was not run.
- **A UDP flow made on the thirty-second timer stays on it.** If its opener does speak again later,
  `pf` does not see that and does not move the state to its sixty-second timer, so such a flow
  survives a silence of about twenty-four seconds where `pf` alone would have allowed fifty-four.
- **A stream in one direction was measured through the tunnel and not through NAT**: there was no
  host beyond the WAN port to send one.
- **A connection is still put off, not refused**, and `flow_unsettled` counts every asking, so it
  climbs with every half-open and closing connection the poll looks at.
- **States bound to an interface are not handled**, as the last point of the review says.

| under `dev.octep.0.dp` | meaning |
|---|---|
| `keepalive` | the setting |
| `ka_reads` | microflows read back for it |
| `ka_touched` | times a connection was found in use and `pf`'s states for it were restamped |
| `ka_idle` | times one was found quiet, or its microflows gone |
| `flow_unsettled` | askings put off because `pf`'s states were not on their long timer |

## Lessons

- **Ask the timers as well as the checks.** Taking traffic away from a component was followed to
  what that component would refuse, and not to what it would forget.
- **A value that is read and only printed is a question nobody asked.** The two peer states had
  been on a debug line for weeks.
- **A test that finishes inside the timer cannot see the timer.** Every download ever measured
  here was shorter than a day, and every UDP test shorter than a minute or indifferent to its port.
- **The fix for one protocol is a claim about the others.** The first build waited for TCP and
  named the wrong shortest timer; UDP and ICMP were in the same table.
- **A gate needs its own way out.** The second build closed the door on a flow the first had let
  through badly, and "wait until it is safe" had no answer for a flow that never becomes so.
- **An odd reading is checked against something the change cannot have touched** before it is
  called an artefact. The expiry that jumped to minutes was the kernel's; asking a state of the
  firewall's own took a minute and settled it.
