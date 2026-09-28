# OCTEON TX - the reference

What is true now, with no history. Every number here has been read from the appliance or from
the vendor's own source, and the reasoning behind each one - including what was tried and
withdrawn - is in [octeon-tx.md](octeon-tx.md), which is a log rather than a reference.

If you only read one page about this family, read this one.

## The board

| | |
|---|---|
| coprocessor | Cavium OCTEON TX CN8365, PCI `177d:a300`, eight VFs at `177d:a303` |
| host | AMD Ryzen Embedded V1780B, 8 threads, 16 GB |
| cores | 20, with `isolcpus=1-19`; 17 of them are fast-path workers |
| assembly | `AMDA0202-0004`, platform `xgs1us` |
| front ports | 12: `Port1`-`Port8` and `PortF3`/`PortF4` behind an 88E6193X, `PortF1`/`PortF2` direct to BGX2 |

## Bring-up, in order

1. **Load the driver.** It finds the barmap at `BAR1 + 0x02000000` and reports the four
   facilities: control, mgmt_netdev, nw_agent and rpc, at 32/33/34/35 MB with doorbells
   152/153/154/155.
2. **Complete the endpoint handshake.** Until it does, the NetAgent window is entirely blank and
   every request returns `ENXIO`. The handshake is also where the coprocessor reports its tick
   rate - 800 per microsecond on this board - and it runs once per coprocessor boot and must
   never be re-armed.
3. **Read the port table.** NetAgent operation `0x01` returns 13 records of 20 bytes, each
   beginning with `tag | flags`.
4. **Raise a port.** Operation `0x03`, attribute `0x00`, payload 1.
5. **Read its link.** Operation `0x04`, attribute `0x00`. Allow about two seconds - a 10G
   bring-up is not instantaneous and an immediate read-back reports failure when it means
   unfinished.

## The port tags

| tag | what | filter table |
|---|---|---|
| `0x0001`, `0x0002` | the two coprocessor 10G MACs - `PortF1` and `PortF2` | 65535 |
| `0x8000` | the switch's uplink to the coprocessor | 0 |
| `0x8100` .. `0x8a00` | switch ports 1 to 10 | 12 |

Bit 15 marks a switch port and bits 13:8 carry its number. The filter-table size is the quickest
way to tell the two kinds apart. Note that `0x0003` answers requests without appearing in the
published table, so an answer is not evidence that a port exists.

## The NetAgent operations that work from the host

Exactly four. Every other opcode returns an ACK with status 1, because the target registers no
handler for it - that includes MDIO and GPIO, which the target implements internally but does
not expose on this hop.

| op | what |
|---|---|
| `0x01` | switch init - returns the port table |
| `0x03` | set a port attribute |
| `0x04` | get a port attribute |
| `0x45` | combined all-port info |

### Attributes worth knowing

| attribute | what it returns |
|---|---|
| `0x00` | **the link** on a coprocessor MAC tag, and it is what a SET writes |
| `0x04` | the **nominal** speed - 10000 even for a port never brought up |
| `0x0d` | duplex. Static; it does not follow the link |
| `0x0e` | 264 bytes of 64 counters, **none of which are populated on this path** |
| `0x01`, `0x09`, `0x50` | refused with status 1 |

## The datapath frame

The 64-byte instruction: `dptr@0 ih3@8 pki_ih3@16 rptr@24 irh@32 exhdr[3]@40`. `rptr` and `irh`
are written byte-swapped; the rest are not. The three `exhdr` words are never written.

| field | value |
|---|---|
| `ih3.pkind` | 40 |
| `ih3.fsz` | 28, which is `16 + 4 + 8` |
| `pki_ih3.sl` | **94**, which is `fsz + TOTAL_TAG_LEN` |
| `pki_ih3` | `w = 1`, `utt = 1`, `tagtype` ordered, `pm = 0`; `qpg` and `uqpg` left unset |
| `irh.opcode` | `0x1220` |
| `irh.rlenssz` | **81**, the checksum offset, which is `TOTAL_TAG_LEN + sizeof(ethhdr) + 1` |
| `irh.param` | the interface index, and 0 on a usfp appliance |
| `irh.dport` | never written |

And the frame at `dptr` is not a bare Ethernet frame. It carries a **66-byte private header**:

    [ 2B port tag, network order ][ 64B metadata, 0xc0..0xff ][ dst MAC ][ src MAC ] ...

`TOTAL_TAG_LEN` is 66 - `PPORT_HLEN` 2 plus `CUSTOM_META_TAG_LEN` 64 - and both ends name the
same split. The metadata is validated: the fast path counts what fails in
`FPCNTR_FROM_KN_DROP_MISMATCH_METADATA_FIELDS`.

## The output queue

Each descriptor is **two** 64-bit words: a buffer pointer and an info pointer. Both must be real
memory - the device DMAs the packet to one and a 16-byte response header and length to the other.

## What does not work

Frames go out and frames come back - the fast path's own counters say so by name, six of them
rising by exactly fifty over a fifty-frame burst: `RX_KN`, `FROM_KN_TO_WIRE`, `TX_WIRE`, `RX_WIRE`,
`FROM_WIRE_TO_KN_LIF_OFFLOAD_DISABLED`, `TX_KN`. What does not happen is the last hop: `OUT_PKT_CNT`
stays at zero and no receive buffer is written, so nothing reaches the host. The earlier claim that
rested on watching the cage LEDs stays withdrawn - those cages have no LED key - and this replaces
it with the instrument rather than the eye.

A working host side is three modules: `octnic` creates `oct0`, `mv_nwa_host` calls
`register_pport_device` once per tag from the NetAgent port list, and `pport` creates a virtual
netdev per front port over `oct0`. **None of that reaches the coprocessor** - all three register
with the host's own pport layer and send nothing - so the gap is not a registration handshake. What
is missing is whatever tells the coprocessor's fast path to hand a received frame to the host.
Promiscuous mode was tried and is accepted with status 0 and changes nothing.

## The channel that programs the fast path

The wire format of the `rpc` facility - the rings, the descriptors, the command numbers and the LIF
entry the wire-to-host gate consults - is in [octeon-tx-rpc.md](octeon-tx-rpc.md), read out of the
coprocessor's own `usfp_rh.ko` at the release this board runs.

## Registers whose reads are not what they look like

| register | what it does |
|---|---|
| `R_OUT_CNTS` `0x10100` | reads `0x2000000000000000` on an idle ring - flags in the upper half, not a count |
| `R_OUT_SLIST_RSIZE` | reads back 16 after being written 0; 256 sticks |
| `R_IN_INSTR_DBELL` | low half is the outstanding count; a field at bit 38 accumulates |

`R_OUT_PKT_CNT` `0x10180` and `R_OUT_BYTE_CNT` `0x10190` were checked against the vendor's own
`cn83xx_pf_regs.h` and are correct, so a zero there is a true zero.

**Never read BAR1 entry 15.** It is the GICD window and reading it hangs the appliance hard.
