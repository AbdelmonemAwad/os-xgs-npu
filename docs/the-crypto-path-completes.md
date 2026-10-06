# The crypto path completes, and the counter that said otherwise was never the one to read

Measured on an XGS 3300, 2026-10-06. **This page overturns
[the engine accepts the frame and nothing returns](the-engine-accepts-and-nothing-returns.md),**
which was written the same afternoon and whose title is simply wrong.

## The instrument that settled it

The coprocessor has `gdb`, and `usfp` keeps a symbol table. So the fast path's own globals can be read
by name from a live process, which is a better instrument than every address-arithmetic argument this
project has made about that binary.

    (gdb) info variables crypto
    0x000000000068b1d0  rte_cryptodevs
    0x000000000068e688  crypto_issued
    0x000000000068e748  crypto_processed
    0x000000000068e808  crypto_wait
    0x000000000068fb3c  g_cryptodev_cnt
    0x0000000000743a80  rte_crypto_devices

Two routes to the same memory were closed first, and both are worth recording so nobody spends the
afternoon on them again:

| route | what happens |
|---|---|
| `/proc/PID/mem` | `EIO` at every offset, although the addresses are mapped and the binary is not position-independent |
| `/proc/PID/pagemap` | all zeros - **including a `.text` page the process is executing from**, which is the control that proves the zeros mean nothing |

**Attaching stops the datapath** for about a second. It was done three times, with the LAN and the
route out checked at 0% loss after each.

## What it says

    g_cryptodev_cnt                 17
    rte_cryptodevs                  0x743a80 <rte_crypto_devices>

    crypto_issued    [6] = 12   [7] = 6
    crypto_processed [6] = 12   [7] = 6

Seventeen crypto devices, which is exactly the seventeen CPT virtual functions bound to `vfio-pci` on
the coprocessor. And the two accounting arrays are **equal**.

**The totals account for every frame.** Three trials had put a working association on a flow and
measured `FPCNTR_FROM_WIRE_TO_IPSEC_ENCR` rising by 7, then 5, then 6 - eighteen frames, the first two
on one association and the third on another. `crypto_issued` holds 12 and 6 on two different queue
pairs: **7 + 5 = 12, and 6.** One operation per classified frame, no more and no fewer.

And `crypto_processed` equals `crypto_issued` in both. **Every operation submitted to the crypto
engine was harvested.**

## So the chain runs end to end

| | |
|---|---|
| crypto devices configured and started | 17 |
| a frame classified for encryption | needs the association's `lif_index` to be the flow's ingress interface |
| an operation built | one per classified frame |
| submitted to the engine | `crypto_issued` |
| completion harvested | `crypto_processed`, equal to it |

Nothing in that chain is refusing anything, and nothing is losing anything.

## What `RX_IPSEC` was, and why it proved nothing

`FPCNTR_RX_IPSEC` stayed at zero throughout, and two pages read that as *nothing comes back from the
engine*. It is not the completion counter. Its single increment site is in `worker_ordered`, on a
branch about a halfword of event context - and `worker_ordered` has
[zero references to the crypto globals page](both-ends-exist.md), so it cannot observe a completion at
all. The completion evidence was always a pair of named globals, and they say the opposite.

That is the fourth time in one session that a claim rested on a **name** rather than on what the thing
does: a symbol absent from a table, a store attributed to its adjacent predecessor, a counter whose
offset is never a constant, and now a counter whose name contains the right word and whose site is in
the wrong function.

## What is still not established

- **The status of the harvested operations.** Equal counts prove they came back, not that they
  succeeded. The status byte is read and the operation freed before anything here could see it.
- **Whether the encrypted frame reached the wire.** After harvesting, the frame is re-injected - to
  the event device or to `rte_eth_tx_burst`, on one bit of the IPsec dynamic field - and whether it
  left is a question for `TX_WIRE` against the classified count.
- **Whether the ESP frame is well formed.** Nothing has looked at one.

The remaining measurement is the same one in all three cases and it needs one ingredient this
appliance did not have when it was tried: **a transiting flow carrying steady traffic**, so that a
window with an association and a window without can be compared on the port counters. The association
at index 6 is left installed for exactly that.

## Lesson

The three hard facts on this page came from a debugger reading two named variables, after an afternoon
of inference from a disassembly. The inference was not wasted - it is what said *which* two variables
to read, and its addresses were right to the byte. But **the order was wrong**: the symbol table was
available on the live machine the whole time, and one `info variables` would have replaced most of a
day's argument.

Look for the instrument before building the inference. When a binary is on a machine you have a shell
on, ask the machine.
