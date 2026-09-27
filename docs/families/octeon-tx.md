# OCTEON TX - Cavium CN83XX

    PCI id     177d:a300   (VF 177d:a303, 64 of them; SR-IOV present but disabled)
    driver     octep       (contrib/octep)
    platform   xgs1us
    hardware   Sophos XGS 3300, assembly AMDA0202-0004, 12 ports - ON THE BENCH
    state      the management link is up and carries IP traffic.
               The twelve front ports are UNTOUCHED.

A different protocol from ARMADA, not the same protocol with another id. Everything below was read
from the vendor's published source and then confirmed on the hardware. Which source, under which
licence, and what was taken from it: [../octeontx/provenance.md](../octeontx/provenance.md) - kept
separate from the ARMADA page because it is a different upstream component.

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

## Building and running it

`octep` is not built by the installer and not packaged, for the same reason `npuep` is not: an
out-of-tree module has to be compiled against the headers of the kernel that is actually running, and
OPNsense ships no kernel sources. The fetch script is shared - there is nothing driver-specific in it.

    sh contrib/npuep/fetch-sources.sh
    make -C contrib/octep SYSDIR=/usr/src-26.7-<sha>/sys
    kldload contrib/octep/octep.ko

Loading it binds the endpoint and reads the map. It does **not** touch the coprocessor's state. The
handshake is a separate, deliberate step, because the coprocessor validates the rings once and goes to
`TARGET_FATAL` if it does not like them:

    sysctl dev.octep.0                  # everything the endpoint published
    sysctl dev.octep.0.mgmt_start=1     # the handshake; octep0 appears and comes up
    sysctl dev.octep.0.mgmt_stop=1      # announce GOING_DOWN and release the rings

`dev.octep.0.ring_dbell=<spi>` rings a doorbell by hand; numbers outside the advertised ranges are
refused. `dev.octep.0.rescan=1` re-reads the map if the coprocessor published late.

`build.sh` stamps `octep.ko.kernel` with `uname -v` beside the module, because a module built against
a different 15.x kernel **loads without complaint** and then reads any structure that moved at the
wrong offset. Compare the stamp after any kernel change.

## What was measured

Addresses were put on both ends by hand - the host side on the new interface, the coprocessor side
on its own `mvmgmt0` - and then:

    ping -c 4 10.0.0.2         (mvmgmt0 on the coprocessor)
    4 packets transmitted, 4 packets received, 0.0% packet loss

ARP resolved to the coprocessor's own hardware address, the one implied by `mvmgmt0`'s link-local
IPv6, which is how the reply was confirmed to come from the coprocessor and not from anything else
on the host.

Latency is tens of milliseconds because receive is polled at 50 Hz, not because of the link.

## SDP, and why the front ports wait on it

The management link above carries exactly one interface. The appliance's twelve front ports are
behind a different mechanism, and the coprocessor's own resource manager names the difference in one
place - `octeontx_main.c`, filling a domain's configuration:

    dcfg->net_port_count  = domain->bgx_count;    the twelve FRONT ports (BGX MACs)
    dcfg->virt_port_count = domain->lbk_count;    internal loopback
    dcfg->pci_port_count  = domain->sdp_count;    the HOST-facing ports (SDP)

So **BGX is the front ports and SDP is the PCIe packet interface to the host.** The vendor's
user-space fast path is launched by a script that blocks on
`/sys/module/slipf/parameters/pci_port` - the **SDP** count - and sleeps until it is non-zero. With
the management link fully up, `host_status 2` and `target_status 2` and frames flowing, that file
still reads five empty slots. That measurement is what settles the order of the work: **the
management handshake is not what SDP counts, and the fast path unblocks only when the host brings
SDP rings up.**

`octep_sdp.c` is the first step of that, and it only reads. It reports what the endpoint says about
its own datapath budget, and it is bounded twice: a ring is read only if the hardware advertised it
in `SDP_EPF_RINFO`, and only if the ring's whole register block lies inside BAR0.

    sysctl dev.octep.0.sdp          # the budget, as four decoded fields
    sysctl dev.octep.0.sdp.rings    # every advertised ring, read fresh

What the hardware answers:

    RINFO 0x0000000000400000  srn 0  trs 64  rpvf 0  nvfs 0   (BAR0 holds 64 rings)

    ring  IN_CONTROL          en    baddr  rsize   OUT_CONTROL         en    baddr  rsize  state
       0  0x0000000014000000   0        -      0   0x0000001000000000   0        -      0  in-idle out-idle
       .
      63  0x0000000014000000   0        -      0   0x0000001000000000   0        -      0  in-idle out-idle

    0 of 64 rings carry any host configuration

**Sixty-four rings, starting at ring 0, with no virtual functions carved out, and every one of them
idle and unconfigured.** Both control words read the same value on all 64: `IN_CONTROL` has `IDLE`
set with `RDSIZE` 2 and `IS_64B` clear, `OUT_CONTROL` has only its `IDLE` bit. Every enable, base
address and ring size is zero. Nothing has ever brought SDP up on this board.

The two bounds agree, which is worth stating because it was not arranged: `SDP_EPF_RINFO` reports 64
rings, and BAR0's 8 MB divided by the 128 KiB ring stride holds exactly 64 - ring 63's last register
ends at `0x7f0198` and ring 64 would begin past the end of the BAR. The register decode and the BAR
geometry were derived separately and arrive at the same number.

Reading all 64 rings is the vendor's own access pattern, not an invention:
`cn83xx_reset_input_queues` and `cn83xx_reset_output_queues` loop over `rings_per_pf` doing exactly
these reads. The ladder the host would have to climb to make any of them live is named in full in
`cn83xx_pf_device.c` - soft reset, the global input and output register setup, per-ring IQ and OQ
setup, the mailbox registers, then the enables - and it is a substantially bigger piece than the
management link was. **None of it is written here. This file reads and reports; it configures
nothing.**

## What is not done

The twelve front ports are untouched, and that is the large remaining piece. The chain is now named
end to end - host brings SDP rings up, `slipf` counts a PF, `pci_port` becomes non-zero, the vendor's
launcher proceeds and provisions the coprocessor's accelerator blocks, the fast path runs, and only
then does it publish the `nw_agent` facility for a host driver to talk to. Today `nw_agent` reads as
a megabyte of zeroes, which is consistent with every step of that chain being unstarted.

There is no MSI-X, one queue each way, a copy per frame, no offload, and nothing persistent: no rc
script and no package.
