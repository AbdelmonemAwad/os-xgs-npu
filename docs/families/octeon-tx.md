# OCTEON TX - Cavium CN83XX

    PCI id     177d:a300   (VF 177d:a303, 64 of them; SR-IOV present but disabled)
    driver     octep       (contrib/octep)
    platform   xgs1us
    hardware   Sophos XGS 3300, assembly AMDA0202-0004, 12 ports - ON THE BENCH
    state      the management link is up and carries IP traffic.
               The front ports carry no host traffic. See "How the ports are actually wired".

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

## How the ports are actually wired

This needs saying before anything else about the datapath, because "the twelve front ports" is a
phrase about the **panel**, not about the coprocessor. The CN83XX has **three** SerDes ports. Ten of
the twelve panel ports are not its ports at all.

From the BSP's own platform database for this assembly:

    npu0.device0 = CN8365        the OCTEON TX coprocessor
    npu0.device1 = 88E6193X      a Marvell switch, present=1

    label  type   phy        npu0.ethN.port
    -----  -----  ---------  --------------
      1-8  RJ45   MVL6193      1:1 .. 1:8
    F3,F4  SFP    -            1:9, 1:10
    F1,F2  SFP+   MVL5113      0:10, 0:12

`npu0.ethN.port` is `<module>:<port>`, **module 1 is the switch and module 0 is the coprocessor**. The
RJ45 ports' "phy" *is* the switch - MVL6193 is the 88E6193X. So ten ports (1-8, F3, F4) are switch
ports, and only **F1 and F2 attach directly to the coprocessor.**

Which is exactly what the coprocessor reports. NetAgent's port table, read live, as
`<qlm> <lane> <num_lanes> <port_type> <switch_id>`:

    port 0:  qlm 4  lane 0  lanes 1  type 2  switch_id 0      the coprocessor-to-switch uplink
    port 1:  qlm 5  lane 0  lanes 1  type 1  switch_id 255    F1, direct SFP+
    port 2:  qlm 6  lane 0  lanes 1  type 1  switch_id 255    F2, direct SFP+

Type 2 with a real switch id is the uplink; type 1 with 255 is a direct port.

**So `num_of_ports` reading 3 is correct and complete** - not a truncated table waiting for something
on the host to fill it, which was briefly suspected here and was wrong. It also explains the fast
path's link report line for line: DPDK port 0 up at 10 Gb/s is the switch uplink, which is always up;
ports 1 and 2 down are the empty SFP+ cages; port 3 up at 10 Gb/s is SDP to the host.

Two things follow, and both make the remaining work smaller than it looked:

- **One working SDP ring reaches all ten switch-side panel ports**, because the switch fans out behind
  a single coprocessor MAC. Ten rings and ten MACs are not needed and do not exist.
- **The switch is separate work** - VLANs and port mapping on the 88E6193X, which the vendor drives
  with CPSS and umsd. It has nothing to do with SDP, and nothing here touches it.

## SDP, and why the front ports wait on it

The management link above carries exactly one interface. The appliance's front ports are
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
management link was. **None of it is written here. The survey reads and reports; it configures
nothing.**

## And the gate turned out not to be the rings at all

The assumption behind that ladder was that the coprocessor would notice a host datapath by watching
the ring registers. It does not. `slipf` polls **one scratch register** - a second one, distinct from
the readiness register that carries the barmap pointer - and when a four-step exchange over it
completes, it creates an `octtx_sdp_port` and adds it to the list that `sli_get_num_ports()` counts.
That is what fills `pci_port`.

So the gate on the front ports is a handshake, and it is far smaller than the ladder would have
been:

    host    writes HOST_LOADED                              "I am here"
    target  writes GET_HOST_INFO, then spins                 "how did you split the rings?"
    host    writes the info word: app_mode, pf_srn, rppf, num_vfs, vf_srn, rpvf
    target  reads it, writes (HOST_INFO_RECEIVED << 16) | ticks_per_us, then spins
    host    writes HANDSHAKE_COMPLETED
    target  marks the handshake done, writes 0, and creates the SDP port

**Both of the target's waits are busy loops with no timeout** - literally `while (read == x) ;`
inside a workqueue. A host that starts this and stops answering leaves a coprocessor core spinning
until it is rebooted. So the host side here is a state machine on its own callout, it is armed only
by an explicit write, and every state it can wait in has a deadline and a defined way out. It is
also the only thing in this driver that writes BAR0.

    sysctl dev.octep.0.sdp.handshake=1   # announce HOST_LOADED and drive the exchange
    sysctl dev.octep.0.sdp.hs_state      # where it stands, and the register as it reads now

### It completed, and the gate opened

Run on an XGS 3300, all three host writes and both target replies inside the same second:

    octep0: sdp: SLI_EPF_SCRATCH was 0x0000000000000000; writing HOST_LOADED
    octep0: sdp: target asked; published 0x0000020008000000 (app 2, pf_srn 0, rppf 8, no VFs)
    octep0: sdp: target took the info and reports 800 ticks/us; announced HANDSHAKE_COMPLETED

    dev.octep.0.sdp.hs_state: completed (scratch 0x0000000000000000)
    dev.octep.0.sdp.hs_cleared: 1

`hs_cleared` is the confirmation that matters: the target zeroes that register **only** after it has
marked the handshake done and created its port. And the coprocessor's own log echoes the exact fields
that were published - `poll_for_ep_mode rpvf 0 vf_srn 0 num_vfs 0 rppf 8 pf_srn 0`.

**Then the file the vendor's fast path has been sleeping on changed:**

    before   /sys/module/slipf/parameters/pci_port    0 0 0 0 0
    after                                             1 8 0 0 1

Read as the launcher reads it - `num_pfs` is slot 0 and `num_vfs` is slot 2 - that is one PF with
eight rings, no VFs, one host-facing port. **The launcher's wait condition is satisfied**, which is
the first time anything on this appliance has got past it.

Two things fell out of the same run:

- **The coprocessor runs at 800 MHz**, which it reports as `ticks_per_us` during step four. The
  vendor's host driver prints this as `(reg >> 16) & 0xffff`, which picks up the low half of the
  marker word rather than the rate and yields 44510; the rate is in the low sixteen bits. Their
  "Copro clock" line has always been wrong, and it never mattered because nothing uses the value.
- **The published source and the running kernel disagree about the polling window.** In
  `slipf_main.c`, `poll_for_ep_mode` gives up after eleven misses, so the window would shut about
  eleven seconds after the coprocessor boots - and `slipf` is built into its kernel, not a module,
  so only a coprocessor reboot would reopen it. But the handshake was answered on the first attempt
  on a coprocessor that had been up more than five hours, its own log timestamping the exchange at
  18336 seconds. The running kernel is 4.14.207-10.22.03 against a published 4.14.76. The deadlines
  in the driver stay regardless; they cost nothing, and what they guard against is a spinning core.

### And the register has a second life, which is why the handshake runs once

After the exchange the target zeroes that register, and from then on `sdp_port_start()` uses it as a
**bitmap of started ports** - bit 0 for the physical function, bit n for VF n. So a second handshake
would not merely be redundant, it would overwrite live state belonging to the target; arming refuses
once the exchange is done. The same read becomes a status report instead:

    dev.octep.0.sdp.hs_state: completed; target reports ports started: 0x0000000000000001 (PF up)

That value appeared by itself, with nothing on the host writing it, the moment the coprocessor's fast
path started its port.

## What the open gate led to

With `pci_port` non-zero, the vendor's own launcher was run from the coprocessor's shell - the
unmodified `usfp_startup_octtx.sh`, with `-d` so every step landed on the console. It no longer waits.
It read the handshake straight off the host:

    NUMPORTS 3 : NUMPFS 1 : NUMVFS 0
    Configuring platform: AMDA0202-0004

then provisioned the coprocessor's own silicon - `modprobe octeontx`, hugepages, SR-IOV on the
accelerator blocks, a resource domain - and launched the fast path with the number the handshake
supplied on its own command line:

    usfp ... --vdev=event_octeontx
             --vdev=eth_octeontx,nr_port=3,pci_port=1,dsa_port=0,sec_pko_vfid=4

**And the front-port MACs came up:**

    thunder-BGX 0000:01:10.3: BGX3.0 GSER RX adaptation completed
    thunder-BGX 0000:01:10.3: BGX 3 LMAC 0 is already UP
    RTE PMD: Port 3: Link Up - speed 10000 Mbps - full-duplex
    RTE PMD: Port 0: Link Up - speed 10000 Mbps - full-duplex
    usfp_main.c[997] launching worker 0 on core 2      ... through worker 16 on core 18
    fp_state.c[404] Service app started on core 19

Seventeen workers across the isolated cores, the SDP port up at 10 Gb/s, and a BGX front port with a
live link. This is the first time the vendor fast path has run on this appliance with OPNsense as the
host.

Two things it exposed:

- **It asks for 2 MB hugepages for this assembly deliberately** - it does not fall back to them. The
  launcher on the coprocessor's own root filesystem carries a case arm for `AMDA0202-0004` setting
  `huge_pg_sz=2` and `huge_pg_cnt=1120`, so 2.24 GB in 2 MB pages, along with `num_sp_txqs=8` and
  `avail_cores=20`. `RTE EAL: No available hugepages reported in hugepages-524288kB` is DPDK observing
  that the 512 MB pool is empty, which is the intended state, not a degradation.
- **NetAgent offers three ports, not twelve.** `num_of_ports` comes from
  `/sys/kernel/nwa_ports_info/`, which SFOS normally populates through `curr_port`. So the fast path
  is running over three of the twelve, and filling that table is its own piece of work.

Throughout all of it the management link was unaffected: `host_status 2`, `target_status 2`, ping
across PCIe at 0% loss afterwards.

## nw_agent is published now, and it is the protocol we already have

Every earlier reading of the `nw_agent` window found **nothing**: 0 of 512 words non-zero in the first
4 KiB. The conclusion drawn then was right - that facility is published by the coprocessor's user-space
fast path, not by its kernel, so until the fast path ran there was nothing on the far side at all.

The fast path is running now. The same window reads:

    +0x0000  0x00000034cafebabe
    +0x0008  0x0000800000007fcc
    +0x0010  0x0000000000008000

    3 of 512 words non-zero in the first 4096 bytes at BAR2+0x02200000

As little-endian 32-bit words that is `0xcafebabe, 0x34, 0x7fcc, 0x8000, 0x8000` - and that is
**exactly** the header this project already documents for ARMADA in
[../netagent.md](../netagent.md), down to the last digit:

    +0x00  u32  cookie              0xCAFEBABE
    +0x04  u32  0x34                command mailbox length
    +0x08  u32  0x7fcc              event buffer length
    +0x0c  u32  0x8000
    +0x10  u32  0x8000

So NetAgent really is Sophos's own and family-independent, which had been stated on the strength of
where its source sits rather than from a second family's silicon. Now it is measured on one.
`contrib/npuep/npunwa.h` carries those offsets under the names `NWA_COOKIE`, `NWA_BODY_OFF`,
`NWA_MAX_REQ`, `NWA_EVT_OFF`, `NWA_EVT_LEN`, and `NWA_COOKIE_VALUE` is `0xCAFEBABE`.

`turn` and `status` are still zero, which is correct: no host has sent a request. And the doorbell to
ring for it is already known and already validated by this driver - SPI 154, the one `nw_agent`
advertises.

**What that does and does not mean.** NetAgent is the **control** plane: port enumeration, link state,
MTU, MAC, administrative up and down. It is not the datapath. So this opens the way to *seeing and
configuring* the ports from the host, while carrying a packet still needs SDP rings. Both are still
ahead; this is the cheaper and safer of the two to attempt first, and unlike SDP the protocol is
already written down here.

### How to ask whether a facility is published

    sysctl dev.octep.0.nw_agent.probe
    sysctl dev.octep.0.control.probe      # and mgmt_netdev, rpc, giu

Reads only, and bounded three ways: the facility must have advertised a non-zero size, the read stays
inside what it advertised, and it refuses anything at or past `gicd_offset`, because entry 15 of this
window maps the coprocessor's GIC distributor and reading that stalls the host with no panic and no
console output.

What the five windows say today:

    control       10 of 512 words   the barmap itself lives at the start of it
    mgmt_netdev   29 of 512 words   our own management link, running
    nw_agent       3 of 512 words   the NetAgent header above
    rpc                             usfp_rh attached to this one: "rpc: found 1 RPC facilities"
    giu           not published     correct - GIU is ARMADA's NIC and does not exist here

## One SDP ring, programmed by the host and accepted by the silicon

`contrib/octep/octep_dp.c` allocates one instruction ring and one scatter list with its buffers,
programs the ring pair, enables it, and grants the output ring its credits. It does **not** transmit
and does not yet read received packets back out, so nothing here can put a frame on a wire. What it
proves is narrower and worth proving alone: that the host can hand this silicon a ring and have the
silicon take it.

    sysctl dev.octep.0.dp.start=1     # allocate and program
    sysctl dev.octep.0.dp.state       # the registers, read fresh
    sysctl dev.octep.0.dp.stop=1      # disable and release

Before, as the hardware rests, and after:

    IN_CONTROL   0x0000000014000000  idle 32B          ->  0x0000000017000002  idle 64B ESR
    IN_ENABLE    0                                     ->  1
    IN_BADDR     0x0000000000000000  RSIZE 0           ->  0x00000000b350f000  RSIZE 256
    OUT_CONTROL  0x0000001000000000  idle BSIZE 0      ->  0x0000001004000600  idle BSIZE 1536
    OUT_ENABLE   0                                     ->  1
    OUT_BADDR    0x0000000000000000  RSIZE 0 DBELL 0   ->  0x00000002d93dd000  RSIZE 256 DBELL 256

`IN_CONTROL` landing on `0x17000002` is the whole of the input policy in one number: the resting
`0x14000000` ORed with RDSIZE, IS_64B and ESR. **IS_64B had to be set** - the vendor's source comment
claims it is "by default enabled" and on this board it is not. `OUT_CONTROL` gains ES_P and BSIZE
1536 while IMODE stays clear. The 256 outstanding credits in `OUT_SLIST_DBELL` are buffers the
coprocessor may write into; `CNTS` stays 0 both ways because nothing is forwarding to the PCI port
yet, which is the correct resting state for an armed ring.

### Where the parameters came from, and why not from the source

Every load-bearing choice sits behind an `#ifdef` in the vendor's tree, so the source cannot say how
the shipped driver was built. They were read out of the shipped v22 binary instead - see
[../octeontx/provenance.md](../octeontx/provenance.md) for the method, which matters as much as the
answers: a value proves a build flag only if the source makes that value conditional on it, and one
of the three readings failed that test and had to be settled another way.

### Two observations worth recording

- **The coprocessor's tick rate does not survive a module reload.** It is published once, during the
  EP-mode handshake, and the handshake runs once per coprocessor boot; the register it arrived in has
  since been zeroed and repurposed. So `dev.octep.0.sdp.coproc_ticks_per_us` is writable, and the
  driver says so when it is missing. On this board it is 800, which makes the output time threshold 1.
- **`R_OUT_SLIST_RSIZE` reads back 16 after being written 0.** Writing 256 reads back 256, so the
  field works; zero simply does not stick. Harmless here - the ring is disabled and its base address
  is zero - but worth knowing before treating a read of that register as authoritative.

### And the ring carries a packet

`sysctl dev.octep.0.dp.xmit=<len>` builds one 64-byte instruction, writes it into the instruction
ring and rings `R_IN_INSTR_DBELL`. Four frames of different sizes:

    posted a 64 byte frame, pkind 40, fsz 28
    posted a 60 byte frame  /  128  /  512

    IN_CNTS 4   IN_PKT_CNT 4   IN_BYTE_CNT 876

**876 is exactly (64+28) + (60+28) + (128+28) + (512+28).** Each instruction's `tlen` was the frame
length plus the 28-byte front data, and the hardware's own byte counter agrees with all four - so the
`ih3` header, its `tlen` and `fsz` fields, and the 64-byte entry layout are right, confirmed by
arithmetic rather than by inspection.

The instruction is built the way the vendor's NIC path builds it for this chip:

    dptr@0   ih3@8   pki_ih3@16   rptr@24   irh@32   exhdr[3]@40

`fsz` is 16 + 4 (PKI header) + 8 (extra header) = 28, and `pki_ih3.sl` is the same 28 - the skip
length steps over exactly the front data. `pkind` is 40, which is what the vendor computes as
40 + num_vfs and we published num_vfs = 0 in the handshake. And **`rptr` and `irh` are written
byte-swapped while `dptr`, `ih3` and `pki_ih3` are not**: the vendor swaps those two in software to
save the far side a swap, and `ESR` in `R_IN_CONTROL` is what turns on the hardware's own swap of the
instruction fetch.

`OUT_PKT_CNT` stays 0, which is expected - the test frame is deliberately inert (broadcast
destination, locally administered source, EtherType `0x88B5` which is reserved for local use) so
nothing has a reason to answer it, and nothing is configured to forward anything back to the PCI port.

One observation worth recording: **`R_IN_INSTR_DBELL` does not read back as a plain counter.** Its low
32 bits read zero once the hardware has taken the instructions, but a field based at bit 38
accumulates - it read `1 << 38` after one post and `4 << 38` after four. The low half is the
outstanding count and is what matters; do not read the whole register as a number.

## What is not done

**No front port carries host traffic**, and the reason is precise: every one of the 64 SDP rings is
still idle, because the host has configured none of them. The coprocessor cannot do it for us - the
base addresses are host memory and the enables are host registers, and nothing on its side writes
either. `slipf` only ever writes the scratch register, `SDP_OUT_WMARK`, the backpressure enables and
`SDP_GBL_CONTROL`.

So what remains is the host half of the datapath: `cn83xx_setup_iq_regs` and `cn83xx_setup_oq_regs`
for one ring - allocate the instruction ring and the scatter list, write `BADDR` and `RSIZE` while
`IDLE` is set, because the vendor spins on `IDLE` before touching `BADDR` and it cannot be written
while the ring is busy - then the enables. One ring before any port, and the same rule as the
management link: nothing announces readiness with incomplete rings.

After that, `nw_agent`: it still reads as a megabyte of zeroes, and it is published by the fast path
rather than by the kernel, so it should become live once the host datapath gives the fast path
something to carry.

There is no MSI-X, one queue each way, a copy per frame, no offload, and nothing persistent: no rc
script and no package.
