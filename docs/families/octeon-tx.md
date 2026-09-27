# OCTEON TX - Cavium CN83XX

    PCI id     177d:a300   (VF 177d:a303, 64 of them; SR-IOV present but disabled)
    driver     octep       (lands in a follow-up change; not in this repository yet)
    platform   xgs1us
    hardware   Sophos XGS 3300, assembly AMDA0202-0004, 12 ports - ON THE BENCH
    state      the management link is up and carries IP traffic.
               The twelve front ports are UNTOUCHED.

A different protocol from ARMADA, not the same protocol with another id. Everything below was read
from the vendor's published source and then confirmed on the hardware.

## The endpoint, as measured

    class 0x0b8000, rev 1, link x8 Gen3 trained, D0, FLR
    BAR0   8 MB at 0xf0000000   CSR space
    BAR2  64 MB at 0xec000000   the facility window - what the vendor's code calls "BAR1"
    BAR4   1 MB at 0xf4800000   not used
    MSI-X  80 messages, table at BAR0+0x0, PBA at BAR0+0x4000 - all 80 masked, unprogrammed
    two Cavium vendor capabilities: VSEC 0x0002 rev 4 len 256, VSEC 0x0001 rev 1 len 56

`mmio[n]` in the vendor's driver is **PCI BAR n*2**, so `mmio[0]` is BAR0 and `mmio[1]` is BAR2.
CN83XX reads its table from `mmio[1]`; CN9xxx reads it from `mmio[2]`. Getting that backwards is
easy, and the arithmetic settles it - only the 64 MB window can hold the offset the endpoint
publishes.

## The handshake

One 64-bit register, the vendor's `CN83XX_SDP_SCRATCH`, at **BAR0 + 0x20180**:

    (facility-table offset << 32) | 0xABCDABCD

The low half is the readiness magic and the high half says where the table is inside BAR2. Read off
the hardware: `0x02000000ABCDABCD`. A table `version` of all-ones means the DMA that writes it has
not landed yet - retry, do not fail.

## The facility table

`struct npu_bar_map`, from the vendor's `target/drivers/pcie_ep/src/barmap.h`:

    uint32_t version                                    (major << 16) | minor
    struct facility_bar_map facility_map[5]
        uint32_t offset, size, h2t_dbell_start, h2t_dbell_count
    uint32_t gicd_offset
    uint8_t  pem_num

Read off the appliance, at BAR2 + 0x02000000:

    version 0.2                                            pem_num 0
    control      offset 0x02000000  1 MB  doorbell 152  count 1
    mgmt_netdev         0x02100000  1 MB           153        1
    nw_agent            0x02200000  1 MB           154        1
    rpc                 0x02300000  1 MB           155        5
    giu                 0           0                0        0    <- ARMADA only
    gicd_offset 0x03c00040

The facility area begins 32 MB into the window; the first 32 MB is left for a PCI console and other
uses.

## Doorbells, and the one thing that must never be read

Host to target only. Every facility advertises `h2t` doorbells and **zero** target-to-host doorbells,
so receive is polled. To ring one, store the SPI number as 32 bits to `BAR2 + gicd_offset`, which is
`GICD_SETSPI_NSR` inside the GIC distributor window. Validate the number against the advertised
ranges first, as the vendor does, so a typo cannot become an arbitrary store into interrupt
controller space.

> [!CAUTION]
> **Never read at or past `gicd_offset`.** That window is the coprocessor's interrupt controller. A
> read of it from the host stalls the host with no panic and no console output, and only a power
> cycle recovers. Writing `GICD_SETSPI_NSR` in the same window is the designed doorbell and is safe.
> This was learned by doing it.

BAR2 is carved into **16 entries of 4 MB**. The coprocessor writes entries 8 to 14 to map its own
memory and entry **15 to map GICD**; entries 0 to 7 are never written, which is exactly why the first
32 MB reads back all-ones.

## The management link

`bar_space_mgmt_net.h` and `desc_queue.h` give the layout: status and mailbox registers at fixed
offsets in the facility window, a transmit ring at +1024 and a receive ring at +65536, each an
`otxcn_hw_descq` header followed by 16-byte `{hdr, ptr}` descriptors whose `ptr` is a **host physical
address**.

The target validates the rings the moment the host announces `HOST_READY`, and on any fault it sets
**`TARGET_FATAL`** and does not return from it without its module being reloaded. It requires
`num_entries` non-zero and a power of two, `cons_idx` zero, a non-zero `shadow_cons_idx_addr`, and a
non-zero `buf_size` on the receive ring. So publish the rings complete, and only then write
`HOST_READY`.

**Frames shorter than 60 bytes must be padded by the driver.** The target's receive path is
`if (len < ETH_ZLEN || is_frag)` and on that path it **does not consume the descriptor** - so one
undersized frame stops the ring permanently while the coprocessor spins re-reading it. A 42-byte ARP
request from FreeBSD does exactly that. Ethernet hardware pads to the 60-byte minimum; there is no
hardware here.

Descriptor header, the `s_mgmt_net` form, little-endian bitfields filling from the least significant
bit:

    rsvd:29 (bits 0-28)   is_frag:1 (29)   total_len:16 (30-45)
    ptr_type:2 (46-47)    ptr_len:16 (48-63)

A posted receive buffer has `hdr = 0`: `ptr_type` DIRECT and no length, because its capacity comes
from the queue's `buf_size` and the target overwrites the header with the real length when it fills
the buffer.

## What was measured

Addresses were put on both ends by hand - the host side on the new interface, the coprocessor side
on its own `mvmgmt0` - and then:

    ping -c 4 10.0.0.2         (mvmgmt0 on the coprocessor)
    4 packets transmitted, 4 packets received, 0.0% packet loss

ARP resolved to the coprocessor's own hardware address, the one implied by `mvmgmt0`'s link-local
IPv6, which is how the reply was confirmed to come from the coprocessor and not from anything else
on the host.

Latency is tens of milliseconds because receive is polled at 50 Hz, not because of the link.

## What is not done

The twelve front ports - the `nw_agent` facility - are untouched, and that is the large remaining
piece. There is no MSI-X, one queue each way, a copy per frame, no offload, and nothing persistent:
no rc script and no package.
