# The handlers were readable all along

2026-10-06. The far side of every RPC command this driver sends exists as C source, on the same
machine this project is developed on, and it was not being read. The owner of the appliance noticed
before the project did: *"our sources are an inventory and files, and I see you not going back to
them, so the problems compound."* He was right, and this page is the accounting.

## What it cost

Three findings published that day as discoveries are single lines of a handler. One of them was
obtained by bisection on a live firewall, over five writes that each broke a working connection.

| published as a finding | where it was already written |
|---|---|
| `SA_ADD` refuses an index already in use | one `if` and one `return` in the IPsec handler |
| a refused table read marks itself with a negative index | a comment and an assignment in the same file |
| the microflow state fields | named members, read straight out of the programming handler |

And a 98-line handler is the whole of a feature this project built the day before out of
disassembly.

## What the reading corrected

Twelve claims, of which these decide behaviour.

**A reply entry of `LO_MFLOW_READ` is 116 bytes, not 112.** The vendor computes it as an 8-byte table
header plus a 108-byte payload. The driver's constant said 112 while the three offsets beside it put
the last field at 92 with 24 bytes in it - 116 by addition, so the constant contradicted its own
neighbours, and it was the loop stride. Every entry after the first in a multi-entry read was decoded
four bytes early.

**`10` and `0xa000` are not two encodings of one interface.** The shift is 12 and the interface is the
high bits, so `0xa000` is interface 10 with no VLAN and the bare `10` is interface 0 with VLAN 10 - a
different logical interface. Two trials that were published as "both encodings work" had tested two
unrelated LIFs whose only common property was being non-zero.

**The programming handler does not check the firewall revision.** It compares the microflow's own
six-bit revision and takes a silent `continue` on a mismatch, answering 0 for having programmed
nothing. The mechanism that makes a revision bump a barrier is elsewhere and is better than what was
assumed: **the forwarding path compares the revision per packet**, and a mismatch is counted as
`FROM_WIRE_TO_KN_FW_REV_MISMATCH`, counter 42. That is why a bump was measured to stop hardware
forwarding dead even though the invalidation it issues is only a queued request. The measurement was
right; the explanation was not.

**The per-port counter array is counter-major.** The index is `counter * 256 + port`, not
`port * 3 + {RX, TX, TX_DROP}`. The driver had it right and a reference page had it wrong.

**`0xff` is a sentinel, not a mask.** The LIF update handler tests `update_mask != 0xff` to decide
whether a request is an update or an add. So this project's measured conclusion - that the handler
"wants 0xff" - was right, and its explanation, that the top bits were undocumented mask bits that
were "required anyway", was not. Nothing guards them; they spell "add".

**A connection read returns 116 bytes per entry, not 108**, by the same 8-byte header. And the
connection entry's members sum to 108 exactly, with no tail padding - two of the vendor's own inline
size comments are wrong, which is what the earlier arithmetic was reconciling against.

## Two open questions it answers

**An encrypt association's operations are reported where the host can read them.** `SA_GET_STATS`,
command 32, returns the fast path's own per-association byte and packet counters, and this driver
refuses to post command 32. Issue #185's remaining question - whether the operations succeeded, not
merely returned - has an instrument, and it is a read.

**`cfg.version` is bumped on every successful add**, and is readable. It is the one positive
confirmation that an `SA_ADD` took effect, as distinct from a reply code that says the transport
delivered the request.

## What the source does not contain

The userspace fast path. This tree is the kernel modules, so the forwarding loops, the crypto submit
and the counters' own increment sites are not in it - for those the instruments are the disassembly
and, better, `gdb` on the live coprocessor, which keeps a symbol table.

## Lesson

Every correction here has the same shape: a measurement was right and an explanation of it was wrong,
and the explanation was written because the measurement was all there was. Reading a handler is not a
faster way to get the same answers - it gets a **different kind** of answer, the kind that says why,
and an explanation invented to fit a measurement will fit it and still be false.

The practical rule the inventory now carries, beside the tree: **for question X, open file Y - before
the hardware, not after it.** The cost of not having it was five broken connections, three days of a
misdirecting negative, and a four-byte stride nobody added up.
