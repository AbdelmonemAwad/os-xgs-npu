# Installed first, taken second

Measured on an XGS 3300, 2026-10-08.
[Issue 299](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/299), left open by
[one encryptor per association](one-encryptor-per-association.md): a rekey under load lost what was
sent while the new association installed.

OPNsense has the kernel send on a new outbound association the moment it exists, and the driver is
offered it a little later. The driver then put a record in its table, took the kernel's cipher
away, waited out every packet that was already inside it, read the kernel's sequence counter -
which could then no longer move - gave the coprocessor the association, and marked it ready. From
the second of those steps to the last a packet of that association had no cipher, and the rule of
the design is that it is dropped and never handed back. Six rekeys under a four-stream upload, on
main, with the host handing every packet over:

| rekey | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| dropped while the association installed | 308 | 368 | 540 | 0 | 0 | 4 |

Up to 1,100 when the issue was written.

## Why it was that order

Two numbers must never repeat under one key: the ESP sequence number, which a peer's replay window
drops, and the eight-byte IV of AES-GCM, which nothing drops and which gives away more than a
packet. The coprocessor uses the sequence number, zero-extended, as its IV. So the coprocessor had
to start past the kernel's last number, and the kernel's last number is only known once its cipher
has stopped. Stop it, wait, read, install: safe, and the wait is the loss. The wait is the network
epoch - two or three milliseconds on this appliance under load.

## The order now

The coprocessor is given the association **first**, while the kernel's cipher is still running on
it, and the cipher is taken **second**, when there is something to take it.

1. **The kernel's IV counter is moved into the upper half of its space** - under the association's
   write lock, which is the lock the kernel's cipher takes its numbers under, so no packet is half
   way through taking one. From there every IV the kernel uses is one the coprocessor, whose IV is
   a 32-bit number, cannot use. An association the kernel has cloned or let go is refused here.
2. **The coprocessor is started a million numbers past the kernel's sequence counter.** The kernel
   takes a sequence number and then an IV for each packet, so every lower-half IV it ever used is
   below its sequence counter: the coprocessor's first IV is past all of them whatever the margin.
   The margin is for the sequence numbers the kernel goes on using until its cipher is taken.
3. **`SA_ADD`.** The kernel is still encrypting. Nothing is dropped. If this fails nothing was
   taken, and there is nothing to give back.
4. **In one hold of the driver's lock: ready, taken, and the transform swapped.** From that line
   new packets go to the coprocessor. The ones already inside the kernel's cipher finish there,
   with the kernel's numbers.
5. **Those are waited out**, and the kernel's counter, which now cannot move, is read once more
   and compared with where the coprocessor was started.
6. **Settled.** Only now may anything raise the kernel's counter to the coprocessor's position,
   attach a connection to the association, or choose it as another's successor.

A record that has not taken the cipher is the kernel's still: a packet that reaches the driver for
it is passed to the kernel's cipher. Once taken, nothing goes back.

## What the peer sees

A capture at the far end of the lab tunnel, of the ESP the appliance sends at a rekey under a
four-stream upload - the first numbers of the new association, and the numbers just past a
million:

| rekey | the kernel's cipher sent | the last of them | the coprocessor's first | arrived after it |
|---|---|---|---|---|
| A | 530 packets, numbered 1 to 530 | one with an upper-half IV, `8000000000000211` | sequence `0x100212`, IV `0000000000100212` | 0 |
| B | 1,267, numbered 1 to 1,267 | all lower-half, the highest `00000000000004f2` | sequence `0x1004f4`, IV `00000000001004f4` | 0 |
| C | 4 captured, numbered 4 to 7 | three with upper-half IVs, `...04` to `...06` | sequence `0x100005`, IV `0000000000100005` | **1** |

In A the kernel had numbered 529 packets when the driver read its counter, and one more before its
cipher was taken: that one carries an upper-half IV, and the coprocessor starts at 529 plus a
million plus one. In every capture the coprocessor's first two hundred packets carry their sequence
number as their IV. In C one packet the kernel had numbered reached the peer after the
coprocessor's first, a million numbers behind it, and the peer's replay window dropped it: that is
what the new order costs.

The coprocessor had not been started from a number that large before. It starts from the number
it is given plus one, as it does from zero.

## Measured

| | dropped while the association installed | dropped by the peer's replay window | install, of which waiting |
|---|---|---|---|
| main, six rekeys | 308, 368, 540, 0, 0, 4 | 0 | |
| the first build, nine rekeys | **0** every time | 0 | 0.07 to 2.5 ms, all but 30 µs waiting |
| the build after review, fourteen rekeys | **0** every time | 1 in two of them, 0 in the rest | 0.03 to 3.5 ms |

Six of those fourteen were watched second by second, the upload's rate and every counter that can
be read on both ends: 925 to 936 Mbit/s over sixteen seconds, no second of it below 870, and at
the rekey nothing moved but the two counts of associations installed and removed - no drop on the
appliance, none on the coprocessor, no retransmission at the peer.

The tunnel suites of the pages before, on this build: as they read before it. Thirty-eight
associations installed and removed in the session, none failed, `out_drop` at 0 from boot.

Some sixteen-second uploads in these runs carried a quarter of the usual rate - 219 Mbit/s where
the rest read 900 - and it is not the rekey: a twelve-second row of the suites with no rekey in it
read 166. It is the load through the host's hand-over path with `ipsec.flows` 0, which is
[issue 298](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/298), and none of the six uploads
made by a second tool did it. (It was a receive ring left by its servicers, and the tool was not
the difference: [a visit is not a pass](a-visit-is-not-a-pass.md). Four rekeys on the build that closes it read 933 to 951.)

## What was found before it was written

A reader of the kernel's side and a critic of the design, before any of it existed. The critic
could not make the two numbers repeat on the path a rekey takes, and found what the design had not
thought of at its edges:

- **A clone.** When a tunnel's address changes the kernel copies the association and frees the
  original; the copy carries the key on under the kernel's cipher with its own IV counter. Made
  before the counter is moved, it would keep a lower-half counter while the coprocessor used the
  same numbers. The old order could not do that, because it waited before anything was ready. So a
  cloned or dead association is refused inside the lock the clone is made under.
- **The poll and the flow path raise the kernel's sequence counter** to where the coprocessor is.
  Done while the kernel's cipher was still running, that would hand the kernel the coprocessor's
  numbers. That is what settled is for.
- **Ready and the swap in two holds of the lock** would leave a moment in which the record is ready
  and the kernel is still the only encryptor. One hold.
- **The gate could shut between the check and the swap.** Installs in hand are counted, and the
  device does not agree to detach while there is one.

## What review caught

Two readers and a verifier on the first build. No repeat of either number on any path that was
exercised.

- A clone made *after* the locked test went on to be installed, from a seed that had not seen its
  sequence counter. It is refused where it is seen, and looked at once more inside the lock
  immediately before the cipher is taken.
- The association's lock was taken before asking whether it had been let go - and a clone that is
  deleted takes the lock with it. Asked first now. That narrows it and does not close it.
- Not from this change: a clone resumes on the kernel's cipher from the kernel's sequence counter,
  which the poll kept up with the coprocessor once a second and with no margin unless flows had
  used the association - so a clone started inside up to a second of the coprocessor's numbers.
  The poll keeps the counter a million ahead for every mirrored outbound association now.

## Where it stands

- **The packets inside the kernel's cipher at the swap are lost at the peer** when they arrive
  after the coprocessor's first: one packet, in two rekeys of twenty-three.
- **The clone has never been exercised.** Everything above about it is read. An address change
  under a mirrored association has not happened on this appliance, and four readers in one day
  each found a different thing wrong on that path:
  [issue 317](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/317).
- **An association more than half way through its numbers is left to the kernel**: the coprocessor
  would be started past 2^31 and its register is 32 bits.
- **The inbound direction is as it was.** It had no window to close: until the coprocessor has
  the association, the ESP is handed to the host and the kernel decrypts it.

| under `dev.octep.0.ipsec` | meaning |
|---|---|
| `out_drop` | packets dropped rather than encrypted by anyone; no longer an association installing |
| `seq_overlap` | installs at whose end the kernel's counter had reached the coprocessor's start; should read 0 |
| `install_us`, `settle_us` | the last outbound install in microseconds, and how much of it was the wait |
| `sa_let_go` | installs given up because the kernel had cloned the association or let it go meanwhile |

## Lessons

- **The safe order had a price that was paid at every rekey, and it was counted in a counter
  nobody read at a rekey.** The first test of the old order lost nothing, and one test was not a
  distribution.
- **Read the number where it is safe to, not when.** The counter could only be trusted once the
  cipher had stopped - but it did not have to be trusted. A million numbers of room, and an IV
  space the other cipher cannot reach, do what the wait did.
- **The edge decides the design.** On the path a rekey takes, the new order was right as first
  drawn. Every change to it came from asking what happens to an association the kernel copies in
  the middle - a thing that has never happened here.
- **Look at the wire.** That the coprocessor starts where it is told, and uses that number as its
  IV when the number is large, was an assumption until a capture at the peer showed the bytes.
