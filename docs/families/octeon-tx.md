# OCTEON TX - Cavium CN83XX

> **Looking for what is true rather than how it was found?** Read
> [octeon-tx-reference.md](octeon-tx-reference.md). This page is a log: it keeps the reasoning,
> including the diagnoses that were wrong and what withdrew them, because that is what stops the
> same ground being covered twice.

    PCI id     177d:a300   (VF 177d:a303, 64 of them; SR-IOV present but disabled)
    driver     octep       (contrib/octep)
    platform   xgs1us
    hardware   Sophos XGS 3300, assembly AMDA0202-0004, 12 ports - ON THE BENCH
    state      the management link is up and carries IP traffic. The host programs an SDP
               datapath ring, and the fast path's own counters now account for every
               frame put on it, by name: taken from the host, forwarded to the wire,
               transmitted, received back off the wire, matched against a LIF and
               handed toward the host. Six counters, fifty frames, fifty each. The one
               hop that still fails is the last: nothing is written into the host's
               output ring, so there is still no usable interface.

**This page is long and it is chronological**, because the order the pieces were understood in is most
of what it has to teach. If you are looking for one thing:

- how the board is wired, and why "twelve ports" is about the panel - *How the ports are actually wired*
- what gates the front ports - *And the gate turned out not to be the rings at all*
- the SDP ring, its registers and its parameters - *One SDP ring, programmed by the host*
- the instruction format - *And the ring carries a packet*
- the order bring-up must happen in, and what a failure leaves behind - *The ordering rule*
- where it stands and what blocks - *All three pieces alive at once*, then *What is not done*

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

## What is held here

| material | detail |
|---|---|
| hardware | **XGS 3300 on the bench**, assembly AMDA0202-0004, platform `xgs1us` |
| coprocessor rootfs | `_shared/octeontx-device-rootfs/`, and a 591 MB raw ext4 image of the running 22.0.2.546 build taken off this appliance |
| fast path | `usfp` for this family in two builds - 2,888,072 bytes from BSP 21, and 2,906,688 from the device rootfs |
| firmware | `xgs1us-boot.img`, from both BSP versions |
| launcher | `usfp_startup_octtx.sh`, selected by `DP_1US_STARTUP_SCRIPT` in `etc/sophos/dp_startup.conf` |
| live capture | a full hardware inventory taken under the vendor firmware: MACs, CPLD map, the 280-key platform store, sensors, the LCD, acceleration counters |

The launcher is worth reading before anything else here. It states the core budget this board runs
with - `avail_cores=20`, one core each for the kernel, RPC and services, leaving **17 workers** - and
the hugepage reservation, `huge_pg_sz=2` with `huge_pg_cnt=1120`. Both were later confirmed by reading
the running appliance, which is the strongest evidence available that it is the right file.

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

**So `num_of_ports` reading 3 is correct and complete** - not a truncated table waiting for
something on the host to fill it, which was briefly suspected here and was wrong. It also explains
the fast path's link report line for line: DPDK port 0 up at 10 Gb/s is the switch uplink, which
is always up; ports 1 and 2 were down because the SFP+ cages were empty at the time - both now
hold a module and link at 10 Gb/s; port 3 up at 10 Gb/s is SDP to the host.

Two things follow, and both make the remaining work smaller than it looked:

- **One working SDP ring reaches all ten switch-side panel ports**, because the switch fans out behind
  a single coprocessor MAC. Ten rings and ten MACs are not needed and do not exist.
- **The switch is separate work** - VLANs and port mapping on the 88E6193X, which the vendor drives
  with CPSS and umsd. It has nothing to do with SDP, and nothing here touches it.

## SDP, and why the front ports wait on it

The management link above carries exactly one interface. The appliance's front ports are
behind a different mechanism, and the coprocessor's own resource manager names the difference in one
place - `octeontx_main.c`, filling a domain's configuration:

    dcfg->net_port_count  = domain->bgx_count;    the coprocessor's BGX MACs - three, not twelve
    dcfg->virt_port_count = domain->lbk_count;    internal loopback
    dcfg->pci_port_count  = domain->sdp_count;    the HOST-facing ports (SDP)

So **BGX is the front ports and SDP is the PCIe packet interface to the host.** The vendor's
user-space fast path is launched by a script that blocks on
`/sys/module/slipf/parameters/pci_port` - the **SDP** count - and sleeps until it is non-zero. With
the management link fully up, `host_status 2` and `target_status 2` and frames flowing, that file
still reads five empty slots. That measurement is what settles the order of the work: **the
management handshake is not what SDP counts. That much stands; the second half - that the fast
path unblocks only when the host brings SDP rings up - was the assumption, and the next section
shows it is the EP-mode handshake that unblocks it, not the rings.**

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

**Sixty-four rings, starting at ring 0, with no virtual functions carved out, and every one of
them idle and unconfigured.** Both control words read the same value on all 64: `IN_CONTROL` has
`IDLE` set with `RDSIZE` 2 and `IS_64B` clear, `OUT_CONTROL` has only its `IDLE` bit. Every
enable, base address and ring size is zero. Nothing had ever brought SDP up on this board at the
time of this survey. Ring 0 is programmed and enabled below.

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

- **It asks for 2 MB hugepages for this assembly deliberately** - it does not fall back to them.
  The launcher on the coprocessor's own root filesystem carries a case arm for `AMDA0202-0004`
  setting `huge_pg_sz=2` and `huge_pg_cnt=1120`, so 2.24 GB in 2 MB pages, along with
  `num_sp_txqs=8` and `avail_cores=20`. `RTE EAL: No available hugepages reported in
  hugepages-524288kB` is DPDK observing that the 512 MB pool is empty, which is the intended
  state, not a degradation. - **The fast path's own port table has three entries, not twelve.**
  `num_of_ports` comes from `/sys/kernel/nwa_ports_info/`, which SFOS normally populates through
  `curr_port`, and those three are the coprocessor's own MACs. NetAgent's switch-init reply, read
  later from the host, enumerates all fourteen.

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

**What that does and does not mean.** NetAgent is the **control** plane: port enumeration, link
state, MTU, MAC, administrative up and down. It is not the datapath. So this opens the way to
*seeing and configuring* the ports from the host, while carrying a packet still needs SDP rings.
Both were still ahead at this point; NetAgent was the cheaper and safer of the two to attempt
first, and both have since been done, and unlike SDP the protocol is already written down here.

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
programs the ring pair, enables it, and grants the output ring its credits. It does not yet read
received packets back out. Transmit came later - see "And the ring carries a packet" - so what
this step proves is narrower: the ring is accepted, not that a frame reaches a wire. What it
proves is narrower and worth proving alone: that the host can hand this silicon a ring and have
the silicon take it.

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

`fsz` is 16 + 4 (PKI header) + 8 (extra header) = 28. `pki_ih3.sl` was set to the same 28 here,
and that was wrong: it must be 94, `fsz` plus the 66-byte tag length. See "The frame needs a
66-byte private header". `pkind` is 40, which is what the vendor computes as 40 + num_vfs and we
published num_vfs = 0 in the handshake. And **`rptr` and `irh` are written byte-swapped while
`dptr`, `ih3` and `pki_ih3` are not**: the vendor swaps those two in software to save the far side
a swap, and `ESR` in `R_IN_CONTROL` is what turns on the hardware's own swap of the instruction
fetch.

`OUT_PKT_CNT` stays 0. The frame used here was inert by construction - broadcast destination,
locally administered source, EtherType `0x88B5`, reserved for local use - and also malformed,
which is what "The ordering rule" cost. The real reason nothing returns is in "What separates this
driver from the vendor's is no longer a field", and nothing is configured to forward anything back
to the PCI port.

One observation worth recording: **`R_IN_INSTR_DBELL` does not read back as a plain counter.** Its low
32 bits read zero once the hardware has taken the instructions, but a field based at bit 38
accumulates - it read `1 << 38` after one post and `4 << 38` after four. The low half is the
outstanding count and is what matters; do not read the whole register as a number.

## The ordering rule, and what a failed provisioning leaves behind

Two things were learned by getting this wrong several times, and both are about order rather than about
registers.

**A frame on this ring is a well-formed IPv4 packet with `irh.rlenssz` set, or it is a fault on the
other side of the link.** The first test frames carried a non-IP EtherType and left `rlenssz` at zero.
The hardware took all four - the byte counter was exact - and then the coprocessor's fast path died:

    usfp_startup_octtx.sh: line 341: 6496 Segmentation fault (core dumped)
    #0  sso_event_tx_adapter_enqueue_noff_l3l4csum ()
    #1  pmode_hwevt_worker_loop ()

Which is itself the proof that the frames reached the far side's worker. `irh.rlenssz` is a response
length in general, but the vendor's NIC path overloads it as the **checksum offset** -
`TOTAL_TAG_LEN + sizeof(ethhdr) + 1` - 15 while this driver had `TOTAL_TAG_LEN` as zero, and 81
once the 66-byte private header was added - and the outbound path computes an L3/L4 checksum
without guarding against having nowhere to find the headers. The test frame is now proper IPv4/UDP,
inert by construction rather than by being malformed.

**The host must bring its ring up BEFORE the handshake, not after.** The coprocessor is configured to
start the fast path at boot - `/usr/sbin/xgs_startup.sh` dispatches by assembly number to
`xgs_1us_startup.sh`, which sources `/etc/sophos/dp_startup.conf` and runs its
`DP_1US_STARTUP_SCRIPT`, piping the output to `logger` rather than to a file, which is why no log
appears under `/tmp`. That launcher parks in its `pci_port == 0` wait loop, and completing the
handshake is what releases it - so a handshake with no ring yet programmed sends it straight into
provisioning against a host that has nothing for it.

*Confidence, stated honestly:* the configuration above is certain, read from the coprocessor's own
root filesystem. Whether a parked launcher was present at any given moment was **not** established -
`ps | grep usfp_startup` is unreliable there, because busybox may show a shell script as `bash`. So
the recipe below kills any parked launcher as a cheap precaution rather than because one was observed. An earlier version of `octep_dp.c` even refused to program a
ring until the target reported a started port, which is backwards: the target sets that bit when the
fast path opens the port, so it is necessarily zero at the moment the ring is wanted. It is a warning
now, not a refusal.

**And a failed provisioning leaves state that only a reboot clears.** In order of discovery:

| left behind | how it fails next time |
|---|---|
| `/var/run/usfp.pid` | `Failed to get exclusive access to PID file` |
| `/var/run/dpdk/rte/config` | `Cannot create lock ... Is another primary process running?` |
| the resource domain | `eth_octeontx` will not probe: `No ethernet ports found` |
| a vfio/SMMU attachment | `cannot attach to SMMU ... whilst already attached to domain on` |

The first two are files and can be removed. **The resource domain cannot be destroyed**: the manager
has a `destroy_domain` sysfs attribute, but it refuses with `domain N on node 0 is in use`, and
`in_use` is set once in `octeontx_main.c` and never cleared anywhere in that file - there is no store
handler and no assignment back to false. `usfp_startup_octtx.sh` only ever creates. So a coprocessor
reboot is the way back, and `/sbin/reboot` does not exist on that rootfs - use `busybox reboot`.

**Rebooting the coprocessor is safe for the host if the host detaches first.** `dp.stop`, `mgmt_stop`,
then `kldunload octep`, so nothing is reading a BAR while the endpoint resets. Done that way the host
came back untouched every time: same BAR addresses, barmap parsed, all four facilities, `RINFO` read.

### A bonus: the coprocessor's own BGX enumeration confirms the port map

Printed during provisioning, and it was never consulted when the port topology above was worked out
from the platform database - so it is an independent check:

    BGX 0 LMAC 0-3  QLM 2 LANE 0-3   index 0-3
    BGX 1 LMAC 0-3  QLM 3 LANE 0-3   index 4-7
    BGX 2 LMAC 0    QLM 5            index 8      <- F1, direct SFP+
    BGX 2 LMAC 1    QLM 6            index 9      <- F2, direct SFP+
    BGX 3 LMAC 0    QLM 4            index 10     <- the switch uplink

QLM 4, 5 and 6 are exactly the three SoC ports NetAgent reports. BGX 3 on QLM 4 - the switch
uplink - is always up; BGX 2's two LMACs are F1 and F2, and both come up once the cages are
populated.

## All three pieces alive at once

The order that works, on a freshly rebooted coprocessor:

    1. host: dp.stop, mgmt_stop, kldunload octep      detach before the reset
    2. coprocessor: busybox reboot
    3. host: kldload octep                            attaches clean, same BARs
    4. host: sysctl dev.octep.0.dp.start=1            THE RING FIRST
    5. coprocessor: pkill -f usfp_startup_octtx       remove any parked launcher
    6. host: sysctl dev.octep.0.sdp.handshake=1       this is the starting gun
    7. coprocessor: rm -f /var/run/usfp.pid; rm -rf /var/run/dpdk
                    bash usfp_startup_octtx.sh -d -u .../usfp

Result - the first time the host ring, the handshake and the vendor fast path have all been up
together:

    host      dev.octep.0.sdp.hs_state: completed; target reports ports started: 0x1 (PF up)
              IN_ENABLE 1  OUT_ENABLE 1  OUT_SLIST_DBELL 256
    coproc    usfp running, no core dumped, five Link Up lines,
              no SMMU error and no "No ethernet ports found"

Eight IPv4/UDP frames of four different sizes then went across and **the fast path survived all of
them**:

    IN_CNTS 8   IN_PKT_CNT 8   IN_BYTE_CNT 2144

`2144` is twice `(64+28)+(128+28)+(256+28)+(512+28)`. Exact again, and this time with the far side
running and processing rather than merely counting - which is what confirms that the earlier SIGSEGV
was the malformed frame and nothing else.

### Why nothing comes back yet

The host-to-coprocessor direction is proven by the byte counter, twice, at four frame sizes -
proven as far as the ring, which is as far as that counter sees. `OUT_PKT_CNT` stays 0 and no
receive buffer is written.

This section used to say the cause was that nothing was plugged into F1 or F2, and that the fix was
either a cable or switch configuration. **Both cages now hold a module, a fibre joins them, and both
ports read link up.** The return direction still does not work, so that explanation is dead.

What is measured now: 5,567 frames and 8,223,298 bytes posted, every one consumed - `IN_PKT_CNT`
rises and `IN_BYTE_CNT` matches - and nothing comes back. A NetAgent statistics read on both 10G
tags returns the same single non-zero word before and after traffic, for every tag including the
switch uplink. It is a dead instrument - see "A dead instrument, recorded so it is not trusted" -
and is not evidence either way.

Both explanations that were open here have now been tested, and the frame format has been corrected.

### The frame needs a 66-byte private header, and it had none

Every frame across this link carries a private header ahead of the destination MAC:

    [ 2B port tag, network order ][ 64B metadata ][ dst MAC ][ src MAC ][ ethertype ] ...

Both ends name the same split independently - `PPORT_HLEN` 2 with `CUSTOM_META_TAG_LEN` 64 on the
host side, `PORT_TAG_SIZE` 2 with `METADATA_SIZE` 64 on the coprocessor - and the fast path counts
what arrives without it in a counter named `FPCNTR_FROM_KN_DROP_NO_METADATA`.

This driver had `TOTAL_TAG_LEN` as zero, which made two derived constants wrong by 66:

| | was | is |
|---|---|---|
| `pki_ih3.sl` | 28 | **94**, which is `fsz` plus the tag length |
| `irh` checksum offset | 15 | **81**, which is `TOTAL_TAG_LEN + sizeof(ethhdr) + 1` |

With `sl` 66 bytes short the parser was being told to start inside the private header.

### Three fields that are not the answer, each tested on hardware

Worth recording so nobody spends a day on them again.

| field | why it looked right | what the vendor path does |
|---|---|---|
| the three `exhdr` words at offset 40 | `fsz` is `16 + 4 + 8` and the 8 is described as an extra header, so eight bytes look reserved for something the host fills | never written. The eight bytes are TSO header space |
| `irh.dport`, bits 34-39 | a six-bit destination port in the instruction header, left zero, and zero is the switch uplink | never written |
| `irh.param`, bits 40-47 | this **is** the port field - `irh->param = setup->s.ifidx` | set to the interface index, not to a front-port number |

Ten to fifty frames were posted at each candidate value of the last two. `IN_PKT_CNT` and
`IN_BYTE_CNT` rose exactly every time and `OUT_PKT_CNT` stayed at zero throughout.

### What the return direction still needs

Transmit and receive are not symmetric here. Before the fast path will deliver anything into an SDP
ring, the host announces itself:

    OCT_NW_PKT_OP    0x1220   a data frame
    OCT_NW_CMD_OP    0x1221   a control command
    HOST_NW_INFO_OP  0x1222   the host describing itself to the coprocessor
    CORE_NW_INFO_OP  0x8004   the coprocessor's reply

This driver implements only `0x1220`. Whether the other three are what the return direction waits on
is **not established**: the host source prepares a `HOST_NW_INFO_OP` instruction in
`octnet_prepare_ls_soft_instr`, but every block that would send it is inside `#if 0`, and
`octnet_setup_io_queues` creates the queues without sending anything. So the opcodes exist and this
driver does not use them, and that is as far as the evidence goes.

What is known about the far side is narrower and worth stating on its own: it consumes every frame,
it writes nothing back, and the frame format it is given is now the one it expects.

### The frames were said to leave the appliance, and that is withdrawn

This page carried a section headed "confirmed visually". With a fibre between F1 and F2, 5,567
frames and 8,223,298 bytes were streamed in two windows separated by five seconds of silence, the
activity LEDs on both cages were reported to blink during the windows and stop together during the
silence, and egress was called proven.

**Those cages have no activity LED.** The appliance's own hardware inventory says so: the ten ports
behind the 88E6193X have their indicators driven by the switch, from `MVL6193LEDcontrol` and a GPIO
pair per port in the platform database, and **PortF1 and PortF2 have no LED key at all** because
they hang directly off BGX2 rather than behind the switch. The same page records why the whole panel
is dark under OPNsense: nothing programs the switch. A light on a cage under this operating system
is not an instrument, and whatever was seen was not an activity indication.

The instrument that does exist says the opposite. `RPC_CMD_LO_WORKER_PORT_CNT_READ` returns the fast
path's own per-DPDK-port counters, and after thousands of frames every one of them is zero - where
the vendor's firmware had port 0 at 54,039 received and 44,679 transmitted. They count real traffic
and they count none of ours.

> **That last paragraph is wrong, and it is left standing because this page is chronological.** The
> counters were not zero; they were being read at indices where nothing lives. The array is
> counter-major with a stride of 256, so a read of indices 0 to 63 sees one counter of the first
> sixty-four ports and none of the rest. `PORT_001_PORT_CNT_TX` is at index 257. See *The per-port
> counters were there all along* below, where the frames are counted leaving PortF1 and arriving on
> PortF2.

**So what is measured is this: a frame posted on the SDP ring is consumed by the coprocessor.**
`IN_PKT_CNT` rises and `IN_BYTE_CNT` matches, and three of the fast path's system counters track the
frames exactly. Nothing shows one reaching a connector, and the fault is therefore not narrowed to
delivery to the host: both directions are unproven on the wire.

### And then the counters were read by name, and the whole picture changed

Everything above was written while the only instruments were `IN_PKT_CNT` on the host's own ring and
a per-port array nobody could index. The fast path keeps a second array, `worker_sys_cnt`, and it is
the one that answers questions: **182 counters, each with a name**, and the names are in the shipped
module's DWARF as an anonymous enum whose order is the array's order. `FPCNTR_RX_WIRE` is 0,
`FPCNTR_TX_WIRE` is 1, `FPCNTR_RX_KN` is 3, `FPCNTR_TX_KN` is 12, and the from-host block runs from
92 to 100. Reading that enum is what turned a wall of integers into a diagnosis.

#### What it said first: the frames were being encrypted

The first read, taken before anything was changed, returned four non-zero counters and they were all
the same number - 9,560:

| index | the fast path's own name | value |
|---|---|---|
| 3 | `FPCNTR_RX_KN` | 9,560 |
| 98 | `FPCNTR_FROM_KN_TO_IPSEC_ENCR` | 9,560 |
| 125 | `FPCNTR_CRYPTO_DROP_SADB_PRE_ERR` | 9,560 |
| 8 | `FPCNTR_TX_DROP` | 9,560 |

Not one frame was lost or unaccounted for. **Every frame this driver had ever posted was taken off
the ring, routed into the IPsec encryption path, refused by the crypto engine for want of a security
association, and dropped.** `FPCNTR_FROM_KN_DROP_NO_METADATA` and
`FPCNTR_FROM_KN_DROP_MISMATCH_METADATA_FIELDS` were both zero, so the metadata was not rejected - it
was accepted and then read as an instruction to encrypt.

That also explains why the three metadata contents tested earlier all behaved identically. They did:
all three went the same wrong way.

#### What the code that writes that block actually does

From-host frames have exactly two destinations in the counter list, `FROM_KN_TO_WIRE` at 97 and
`FROM_KN_TO_IPSEC_ENCR` at 98, so something in the frame chooses between them. Rather than sweep 64
bytes against live hardware, the answer came out of the module that writes them.

The GPL `pport` driver does not write the metadata. `pport_dev_hard_start_xmit` calls a customer
hook first, asks it how many of the 64 bytes it claimed, and fills **only what is left** with the
walking pattern - under a comment in the vendor's own source that reads `FIXME: for debug, fill
0xC0 - 0xFF in metadata tag`. So the pattern this driver had been sending is filler for bytes the
real hook would have written, and when the hook is loaded it claims all 64.

The hook is `mrvl_cst_set_tx_meta` in the host-side `usfp_firewall.ko`. Disassembled, it pushes 64
bytes and its very first store is

    movb   $0x1,(%rax)          /* meta[0] = 1 */

followed by a one-bit flag at `meta[1]`, another at `meta[2]`, a `u16` at `meta[6]`, a 25-bit field
at `meta[8]` with flag bits in `meta[11]`, and a `u16` at `meta[12]`. Bytes 16 to 63 are never
touched. So the entire contract is sixteen bytes, and exactly one byte of it is a constant a driver
with no netdev can know: **`meta[0] = 1`**.

`dp.meta=3` sends that - byte 0 set to 1, the rest zero.

**A version note, because it matters.** That module is the v21 XGS 136 host copy, the only one held
in readable form; this appliance's coprocessor runs v22. The metadata is a host-to-fast-path
contract and the fast path is the same product on both families, so the layout was expected to
carry - but it was treated as a lead to measure, not as a v22 fact.

#### And then every frame went the right way

After rebuilding and reloading, a controlled burst of fifty frames with `dp.port_tag=1` and a fibre
between the two cages moved six counters, each by exactly fifty:

| index | the fast path's own name | before | after |
|---|---|---|---|
| 3 | `FPCNTR_RX_KN` | 9,565 | 9,615 |
| 97 | `FPCNTR_FROM_KN_TO_WIRE` | 4 | 54 |
| 1 | `FPCNTR_TX_WIRE` | 4 | 54 |
| 0 | `FPCNTR_RX_WIRE` | 4 | 54 |
| 37 | `FPCNTR_FROM_WIRE_TO_KN_LIF_OFFLOAD_DISABLED` | 4 | 54 |
| 12 | `FPCNTR_TX_KN` | 4 | 54 |

`IN_PKT_CNT` went 5 to 55 over the same burst. `TX_DROP`, `FROM_KN_TO_IPSEC_ENCR` and
`CRYPTO_DROP_SADB_PRE_ERR` did not move at all - they stayed frozen at 9,561 and have not moved
since.

Read that column downward and it is a complete circuit: the frame is taken off the host's ring,
routed to the wire rather than to the crypto engine, transmitted, received back off the wire,
matched against the LIF installed earlier - which reports offload disabled, which is correct,
because nothing has enabled it - and handed toward the host.

**And then it stops.** `OUT_PKT_CNT` is 0 and none of the 256 receive buffers has been written. The
fast path hands the frame to the host and the host's ring never sees it. That is one hop, and it is
the hop this driver owns.

#### The port tag decides, and only one value works

`dp.port_tag` was swept over 0, 1, 2 and 3 with twenty frames each, watching both arrays:

- **tag 1** transmits and the frame comes back. It is the only tag for which `RX_WIRE` moves.
- **tags 2 and 3** raise `FROM_KN_TO_WIRE` and `TX_WIRE` - the frame is transmitted - but nothing
  returns.
- **tag 0** raises `FROM_KN_TO_WIRE` and then `FPCNTR_TX_WIRE_ERR` and `FPCNTR_TX_DROP`. The
  transmit itself fails.

So the tag selects an egress, the fast path acts on it, and the failure modes are distinct and
named. Which physical connector each tag is was not settled by that reading - and the reason turned
out to be that the reading was in the wrong place. See below.

#### What is still open from this run

- **The last hop**, which is the whole remaining problem.
- **Why the first 9,560 frames were encrypted and these are not.** Between the two measurements the
  driver was rebuilt and reloaded, the rings were reconfigured, and a LIF was installed earlier in
  the same session. Counter 37 proves a LIF is matched on the return path, so one exists. Which of
  those changes moved the branch has not been isolated, and saying which one did would be a guess.
- **The LIF table reads back empty** at entries 0 to 7 even though counter 37 says a LIF matches.
  The table is indexed by `iface_id << 12 | vlan`, per the vendor's own dump, so the entry is
  probably not where it was looked for.

### The last hop, narrowed to one side of the PCIe link

`FPCNTR_TX_KN` rises and `OUT_PKT_CNT` stays at zero, which is one hop. Three readings taken with
`dp.peek` and one taken out of physical memory narrow it to one side of the link, and the narrowing
matters more than any single number.

**The output queue is correctly programmed.** Read fresh with the ring up:

| offset | register | value | |
|---|---|---|---|
| `0x10110` | `OUT_INT_LEVELS` | `0x8` | |
| `0x10120` | `OUT_SLIST_BADDR` | `0x48e7c000` | |
| `0x10130` | `OUT_SLIST_RSIZE` | `0x100` | 256 |
| `0x10140` | `OUT_SLIST_DBELL` | `0x100` | 256 credits outstanding |
| `0x10150` | `OUT_CONTROL` | `0x1004000642` | bit 36 `IDLE`, bit 26 `ES_P`, size 1602 |
| `0x10160` | `OUT_ENABLE` | `0x1` | |
| `0x10180` | `OUT_PKT_CNT` | `0` | |

**And the ring it points at is fully populated.** Reading host physical `0x48e7c000` through
`/dev/mem` shows sixteen-byte entries in pairs:

    +000  00 90 49 58 00 00 00 00   00 d0 52 4d 00 00 00 00
    +010  42 96 49 58 00 00 00 00   10 d0 52 4d 00 00 00 00
    +020  84 9c 49 58 00 00 00 00   20 d0 52 4d 00 00 00 00

The first pointer of each pair steps by **0x642, which is 1602** - exactly the buffer size in
`OUT_CONTROL`. The second steps by **0x10**. So each entry is a data buffer pointer and a sixteen
byte info block pointer, the driver is in info-pointer mode, and it has populated both arrays
contiguously. Entry 112 reads `0x584c4ce0` and `0x4d52d700`, which is `0x4d52d000 + 112 * 0x10`
exactly, so the consistency holds across the ring rather than only at its head.

**And the target can certainly write into host memory.** The RPC facility proves it several times a
minute: the command descriptor's first eight bytes are a host physical address, the driver poisons
that buffer before every command, and the poison comes back overwritten. Address translation, bus
mastering and the target's reach into host memory all work.

So: the host's queue is enabled with credits and valid buffers, the target can write to the host, and
`OUT_PKT_CNT` - a counter in the SDP output path itself - reads zero. **The coprocessor has never
asked its SDP engine to send a packet on ring 0.** The gap is upstream of the SDP engine on the
coprocessor's side, not in the host's programming of the queue and not in the target's ability to
reach us.

That is consistent with one thing this driver has never done. It has only ever sent data packets,
opcode `0x1220 OCT_NW_PKT_OP`. The vendor's host driver sends control instructions before any
traffic, and if one of those is what tells the target which output queue exists and what belongs in
it, the target would behave exactly as observed: it accepts everything we send, does the work,
raises its own counter for the hand-off, and has nowhere it believes it may write.

#### And the same driver already has a receive path that works

This is the comparison that makes the argument, because both halves run on the same machine, over
the same PCIe link, to the same coprocessor, at the same moment.

The management facility delivers into host memory and always has. Bringing `octep0` up gives

    dev.octep.0.host_status    2      (running)
    dev.octep.0.target_status  2      (running)
    dev.octep.0.rx_packets     2
    dev.octep.0.rx_bytes       180
    dev.octep.0.rx_cons_shadow 2

`rx_cons_shadow` is a consumer index **the target wrote into host memory**, and `rx_packets` counts
frames the target placed in the host's receive ring. So host-bound delivery is not a thing this
driver cannot do. It is a thing one of its two facilities does and the other does not.

The difference between them is not the ring and not the buffers. It is that the management path
**tells the target the host is running**: `octep_set_host_status` writes `OTXMN_HOST_STATUS_REG` in
the coprocessor's window and then sends `OTXMN_MBOX_HOST_STATUS_CHANGE` over the mailbox, and the
facility's own transmit path refuses to run until that status is `OTXMN_HOST_RUNNING`. The SDP path
publishes nothing of the kind. It programs registers, fills a ring, rings a doorbell for each frame
it sends, and never once says that it exists.

The ping in that run got no reply, and that is expected rather than a failure: the coprocessor's
`mvmgmt0` has no address on it at the moment and its console is the one recorded in issue #105. The
two frames are what the target sent anyway, and they are the point.

### The control channel is port tag 254, and the inventory said so twice

The last hop needed a mechanism, and the appliance's own capture named one. The fast path's counter
list has a pair for control traffic arriving from the host - `FPCNTR_FROM_KN_DROP_CMSG` at 93 and
`FPCNTR_FROM_KN_PROC_CMSG` at 94 - and the capture taken while the vendor's firmware was driving
this board reads

    FPCNTR_FROM_KN_PROC_CMSG : 748

against 49,966 data frames. So the host sends control messages **in band, on the same ring as
data**, and the fast path counts them separately. This driver had sent none.

The same capture names the channel in a second place, in a list nobody had read closely. The
vendor's interfaces include `pport_l0@oct0` and **`pport_l254@oct0`** - a logical port 254 alongside
the panel ports.

#### What did not work, recorded so it is not retried

The obvious reading was Marvell's: an instruction carrying `OCT_NW_CMD_OP`, opcode `0x1221`, whose
data is one 64-bit `octnet_cmd_t`. The vendor's own host driver builds exactly that, with
`ih.fsz = 16` and no PKI header, and sends `OCTNET_CMD_RX_CTL` from its interface open handler with
`param1` the interface index and `param2` the start flag.

Posted here, it was **taken as a data packet**: `FPCNTR_RX_KN` rose by one,
`FPCNTR_FROM_KN_TO_WIRE` rose by one, and then `FPCNTR_TX_WIRE_ERR` and `FPCNTR_TX_DROP`. Neither
control counter moved. The opcode is not what this fast path reads.

Nor is the metadata's type byte. `meta[0]` was swept over 2, 3, 4, 5, 6, 7, 8, 16 and 32 with two
frames each, and every one behaved exactly like the vendor's 1 - straight to the wire, with both
control counters flat.

#### What did work

Two frames posted with **`dp.port_tag = 254`**:

    [93] FPCNTR_FROM_KN_DROP_CMSG   0 -> 2

Exactly the two frames, on the counter for a control message the fast path could not use. Tag 253
and tag 255 do not even reach `FPCNTR_RX_KN` - they are discarded before the fast path sees them.

That reading - **the port tag separates a control message from a packet, and 254 is the channel** -
matches `pport_l254` in the vendor's own interface list, and what was sent on it was an IPv4/UDP
test frame rather than a command, which would explain a counter for a message understood as control
and then dropped.

It stopped reproducing an hour later, and the reason turned out to be worth more than the scare: the
fast path had stopped consuming host frames **of any kind**. `FPCNTR_RX_KN` froze for tag 1 as much
as for tag 254. The coprocessor was not dead - RPC answered `PLATFORM_READ` and SDP kept fetching
every instruction - but the from-host datapath inside the fast path took nothing, which is the state
issue #65 describes and only a coprocessor restart clears.

**A host reboot restarts the coprocessor.** That is new, and it replaces a worse belief. This page
and the notes behind it said a full power cycle was the only way, because the coprocessor's console
is dead and the MCP2210 bridge does not hold this board. But `shutdown -r now` on the host asserts
PCIe reset to the slot, and the appliance came back in twenty seconds with
`sdp.hs_state` reading `idle (scratch 0x0)` - exactly the post-power-cycle state. The handshake then
completed fresh and published **800 ticks/us**. The fast path took about four minutes to come up and
acknowledge an RPC ring configuration; before that `reconfig_done` stays 0 and it looks like a
protocol fault rather than a boot in progress.

**And with a healthy fast path the tag 254 result reproduces exactly.** Two frames on tag 254 move
`FPCNTR_FROM_KN_DROP_CMSG` by two, and `FPCNTR_RX_KN` by two, while `FROM_KN_TO_WIRE` does not move
at all. The observation stands.

### The body of a control message, extracted from the module that builds it

`usfp_firewall.ko` is the host-side half of the fast path, x86-64 and not stripped, and it carries
six functions whose names settle the question:

    usfp_cmsg_alloc  usfp_cmsg_xmit  usfp_firewall_cmsg_init
    usfp_firewall_cmsg_rx  usfp_firewall_cmsg_process_rx  usfp_firewall_cmsg_process_one_rx

**`usfp_cmsg_alloc(type, len, gfp)`** allocates `len + 0x46`, steps over `0x42` - the 66-byte
private header this driver already writes - and lays down four bytes:

    movw $0x0,0x42(%rax)      two zero bytes
    movb <type>,0x44(%rax)    the type
    movb $0x1,0x45(%rax)      the version

then calls `skb_put(skb, len + 4)`.

**`usfp_firewall_cmsg_process_one_rx`** agrees field for field from the other direction. It reads
byte 3 and refuses anything but 1; reads byte 2 as the type and refuses a value above 6; and jumps
through a seven-entry table. So:

```c
struct usfp_cmsg {
	uint16_t rsvd;		/* +0, always written zero */
	uint8_t  type;		/* +2, 0..6 are target-to-host */
	uint8_t  version;	/* +3, 1, and the receiver checks it */
	uint32_t count;		/* +4, how many entries follow */
	/* entries at +8 */
};
```

The count at +4 is not particular to one message: type 5's receive handler checks
`skb->len >= 8 + 72 * count` with the count read from +4 before calling `fw_fp_reclaim_conn_bulk`
on the bytes at +8.

**`usfp_pport_monitor_speed_work`** is the only message the host builds in that module, and it is
**type 7** - outside the 0..6 the receive side accepts, which is what makes it the host's direction:

    mov  $0x14000c0,%edx      GFP_ATOMIC
    mov  $0x44,%esi           len 68
    mov  $0x7,%edi            type 7
    call usfp_cmsg_alloc

It zeroes the count, walks every pport netdev filling entries, and sends only if the count came back
non-zero. **`usfp_pport_speed_changed`** writes each entry:

    lea  0x0(%rbp,%rcx,4),%rcx     /* rbp is &count, rcx is the count: stride 4 */
    mov  %dx,0x4(%rcx)             /* u16 port tag, from dev + 0x800 */
    mov  %dx,0x6(%rcx)             /* u16 speed */

and refuses a seventeenth entry with `cmpl $0xf,0x0(%rbp)`. Sixteen entries of four bytes is 64, and
`4 + 4 + 64` is 72, which is exactly `len + 4`. **The body is a fixed 72 bytes whatever the count
is**; only `count` says how many slots mean anything.

`usfp_cmsg_xmit` then sets `skb->dev` to a netdev stored at init - `usfp_firewall_cmsg_init` calls
`usfp_netdev_get_emux` under the rtnl lock - and calls `dev_queue_xmit`. So the channel is an emux
pport device, which is `mux_dev0` in the vendor's own interface list.

**Version, as everywhere:** that module is the v21 XGS 136 host copy, the only one held in readable
form. This appliance runs v22.

#### It was fourteen bytes short, and the coprocessor's own binary said so

Posted exactly as written above, every message was dropped: `FPCNTR_FROM_KN_DROP_CMSG` rose by one
each time, `FPCNTR_FROM_KN_PROC_CMSG` never moved, and a sweep of the type over 0 to 9 put all ten
on the drop counter. So the type was not what was being rejected.

The answer is in the **coprocessor's own v22 fast path**, which is the right copy to read and was on
this machine all along:

    426c28  cmp   w0, #0xfe        the port tag
    426c2c  b.eq  429030           and only then, the control branch
    429034  mov   w3, #0xefef
    429040  ldrh  w1, [x2, #12]    a u16 at offset 12
    429044  cmp   w1, w3
    429048  b.eq  42a430           a control message, or nothing
    42a430  ldrb  w4, [x2, #17]    and the version at 17
    42a434  cmp   w4, #1

**Offset 12 is an EtherType.** The same routine proves it two instructions later: for a frame that is
not on tag 254 it reads `[x1, #12]` and compares against `#0x8` and `#0x81` - 0x0800 and 0x8100 read
as little-endian off a big-endian wire - and it keeps `mov w27, #0xe`, fourteen, the Ethernet header
length, for that path.

So **a control message is an ordinary Ethernet frame with EtherType 0xEFEF**, and the four-byte
header is its first four payload bytes. Offset 16 is the type and 17 the version, which is why the
host's own parser reads them at `skb->data + 2` and `+ 3` after `eth_type_trans` has pulled the L2
header off. The two binaries agree exactly; the driver was writing the four bytes straight after the
metadata, so the fast path read its EtherType out of what was really the count field.

The frame is therefore:

| offset | | |
|---|---|---|
| `+0x00` | `u16` | port tag, `0x00FE`, big-endian |
| `+0x02` | 64 B | the metadata block, byte 0 set to 1 |
| `+0x42` | 6 B | Ethernet destination |
| `+0x48` | 6 B | Ethernet source |
| `+0x4E` | `u16` | **EtherType `0xEFEF`** |
| `+0x50` | `u16` | reserved, zero |
| `+0x52` | `u8` | type |
| `+0x53` | `u8` | version, 1 |
| `+0x54` | | the payload, `u32 count` then entries |

One more constraint from the same routine: at `426bd8` it strips a further 66 bytes only when what
remains after the 28-byte instruction header exceeds 0x41, so a control message much under 94 bytes
would have its EtherType read from inside the metadata instead. Type 7's fixed body puts the frame
at 152 bytes.

#### And the channel is open

With those fourteen bytes in place, on the same appliance in the same sitting:

| counter | before | after one message | after a second |
|---|---|---|---|
| `FPCNTR_FROM_KN_DROP_CMSG` | 14 | **14** | **14** |
| `FPCNTR_FROM_KN_PROC_CMSG` | 0 | **1** | **2** |

**The fast path processes them.** Nothing is dropped, and the counter that moves is the one the
vendor's firmware sits at 748 on.

What that has not done is deliver a frame. `OUT_PKT_CNT` is still 0 and no receive buffer is written,
and the round trip still ends at `FPCNTR_TX_KN` exactly as before. So the control channel being open
is not by itself the announcement; the right message on it has not been found. What is closed is the
question of how to speak on it at all.

### PPORT_UPDATE is what makes a returning frame find its LIF

A coprocessor restart clears the fast path's tables, which turned into an experiment worth more than
the state it lost. With the ports raised and a LIF installed at index 0 carrying PortF2's address,
twenty frames still produced

    [24] FPCNTR_FROM_WIRE_DROP_LIF_INDEX_INVALID   +20

so the frames came back off the wire and the LIF lookup failed anyway. The LIF was there; the index
was not.

`RPC_CMD_PPORT_UPDATE`, command 5, is four bytes - `{ u8 iface_id; u8 rsvd; u16 pport_tag; }` - and
its handler writes **both** directions of the map, `iface2pport[iface] = tag` and
`pport2iface[tag] = iface`. Binding interface 0 to port tag 2, which is the cage the frames arrive
on, changed the next twenty frames completely:

| counter | before | after |
|---|---|---|
| `FPCNTR_FROM_WIRE_DROP_LIF_INDEX_INVALID` | 40 | **40, it stopped** |
| `FPCNTR_FROM_WIRE_TO_KN_LIF_OFFLOAD_DISABLED` | 0 | **+20** |
| `FPCNTR_TX_KN` | 0 | **+20** |
| `FPCNTR_TX_DROP` | 45 | **45, it stopped** |

So the pport-to-interface binding is not optional and it is not implied by installing a LIF. A
returning frame's interface comes from `pport2iface[tag]`, the LIF index is `iface << 12 | vlan`,
and without the binding the index is whatever the table happens to hold. One four-byte command is
the whole of it.

This is the second time the answer was a table the host has to fill rather than a frame it has to
shape, and the counters named the failure both times.

### Type 6 is the return mechanism, and this fast path never sends one

Type 6 is the coprocessor-to-host direction, and the host's own handler says exactly what it expects:

```c
flag = data[4];                                        /* a u8 at +4 */
dev  = usfp_pport_find_dev(emux, *(u16 *)(data + 6));  /* the destination port tag at +6 */
skb_pull(skb, 8);                                      /* strip the eight-byte header */
proto = eth_type_trans(skb, dev);                      /* and the rest is a whole frame */
skb->dev = dev;
```

So a type 6 message is an eight-byte header wrapping a **complete Ethernet frame**, addressed to a
logical port by tag, and the host hands it to that port's netdev as an ordinary receive. `flag` at
+4 selects a second path when it is non-zero, and a payload EtherType of 0x8100 takes a third. That
is the exception path by which a coprocessor gives the host a frame it decided not to forward.

**And the v22 fast path never builds one.** Across the whole 2,906,688-byte binary there is exactly
one place the value 0xEFEF exists:

    429034  mov  w3, #0xefef

which is the **compare** in the receive gate. There is no second site, no store, and - checked
directly - **not one occurrence of the byte pair `EF EF` anywhere in the file**, so there is no
template in its data either. The counter list agrees from the other side: it has
`FPCNTR_FROM_KN_PROC_CMSG` and `FPCNTR_FROM_KN_DROP_CMSG`, both from-host, and **no to-host control
message counter at all**.

So the control channel on this platform is **one-way**: the host speaks and the fast path listens.
Whatever sends type 6 to a host is not `usfp`, at least not in this build.

#### Which means the return path is an ordinary frame, on a host port

The fast path reaches the host through DPDK ports, and its own startup script says how many it has -
`usfp_startup_octtx.sh`, which `dp_startup.conf` names as the entry point for this platform:

    pci_info=($(cat /sys/module/slipf/parameters/pci_port))
    num_pfs=${pci_info[0]}
    ...
    num_vfs=${pci_info[2]}
    num_hostports=$[num_pfs + num_vfs]

and it **blocks** until `num_pfs` is non-zero, printing "waiting for handshake with host". So the
host port count is not the coprocessor's decision. It is read out of a module parameter that the
host's own handshake fills, and the fast path will not start until the host has filled it.

**This driver publishes no VFs.** The info word is `OCTEP_SDP_INFO(NIC, srn, 8, 0, srn, 0)`, so
`num_vfs` is 0 and `num_hostports` is 1. The vendor's host publishes eight VFs and a PF starting
ring of 8, which makes it 9. That difference is now the most concrete thing between this driver and
a delivered frame, and it explains an earlier result that made no sense on its own: ring 8 was tried
and its frames were classified as arriving from the wire rather than from the host, which is what
would happen to a ring the target believes belongs to nobody.

#### The experiment was run, and the answer is that the claim is not free

`sdp.hs_nvfs`, `sdp.hs_pf_srn`, `sdp.hs_rppf`, `sdp.hs_vf_srn` and `sdp.hs_rpvf` publish whatever
topology they are set to, and default to what this driver has always published. With the coprocessor
freshly reset by a host reboot, the vendor's own topology went out:

    sdp: target asked; published 0x0000020808080001
         (app 2, pf_srn 8, rppf 8, 8 VFs, vf_srn 0, rpvf 1)
    sdp: target took the info and reports 800 ticks/us; announced HANDSHAKE_COMPLETED

The target accepted it. Then the far side **took itself down**. Seven minutes later there was no
fast path, and the readings were not those of something still starting:

| | |
|---|---|
| `rpc.state` | `0xffffffffffffffff` - an **unbacked** window |
| `mgmt_up`, `host_status`, `target_status` | 0, 0, 0 - the management link **dropped** |
| all four facility window sizes | 0 |
| `sdp.rinfo` | `0x400000`, a clean read, so BAR0 and the PCIe link are fine |

So the hardware was healthy and the coprocessor's software had gone. Recovery was another host
reboot: the facilities came back once its Linux had finished booting, the handshake was redone with
the default topology, and the round trip measured the same as before - `RX_WIRE`, `TX_WIRE`,
`RX_KN`, `TX_KN`, `FROM_WIRE_TO_KN_LIF_OFFLOAD_DISABLED` and `FROM_KN_TO_WIRE` all +20 for twenty
frames.

**What that establishes is worth as much as a success would have been.** The VF count in the
handshake is not a label the far side records and reads back later. It acts on it immediately: it
reconfigures its endpoint for nine host ports, and against a host that has eight VFs only on paper -
no VF ring sets, nothing at `177d:a303` that this driver has programmed - it does not survive the
attempt. Publishing a topology is publishing a promise.

So the host port count remains a real difference between this driver and the vendor's, and the way
to close it is not to claim VFs. It is either to implement them, or to find whether one PF host port
can be made to carry what nine do.

### The per-port counters were there all along, and the array is 256 wide

This page said twice that the per-port array's index-to-name map was not established, and that
exactly one entry moved, which could not distinguish a transmit on one port from a receive on
another. Both were true readings of the wrong indices.

The fast path builds those names from a format string, and the binary carries the pieces:

    PORT_%03d %s = %ld
    PORT_CNT_RX
    PORT_CNT_TX
    PORT_CNT_TX_DROP_QUEUE_FULL

**Three counters per port, not two**, and `usfp_rh.ko` defines `PLATFORM_PORT_MAX_NUM 255` and
`PLATFORM_PORT_MAX_SIZE 256`. So the array is counter-major with a **stride of 256**, and reading
indices 0 to 63 sees only the first counter of the first sixty-four ports. Tested on hardware, twenty
frames apart:

| index | decodes to | before | after |
|---|---|---|---|
| 2 | `PORT_002_PORT_CNT_RX` | 20 | **40** |
| 257 | `PORT_001_PORT_CNT_TX` | 20 | **40** |
| 513 | `PORT_001_PORT_CNT_TX_DROP_QUEUE_FULL` | 20 | **20, frozen** |

Three things at once.

**The frames are transmitted on PortF1 and received on PortF2**, counted by the fast path's own
per-port instrument - the instrument whose silence was the stated reason for withdrawing the egress
claim. It was not silent. It was being read at indices where nothing lives. `PORT_001` is PortF1 and
`PORT_002` is PortF2, which is exactly what the appliance's own capture measured under the vendor's
firmware with a 2000-frame run.

**And the queue-full drops are frozen at 20**, so nothing has been dropped since - but the
identification that once went with that, namely that those twenty were the frames posted before the
front ports were raised, does not hold and is withdrawn. The appliance's own capture has `PORT_002`
at **eight** queue-full drops against **six** transmits in the same snapshot, so the drop counter is
not a subset of the transmit counter and a drop count cannot be matched to a transmit count to date
it. Index 513 is an independent instrument.

So the egress claim is now carried by the right instrument. The withdrawal was still correct when it
was made - cage LEDs were never evidence - and what replaces it is a counter with a name.

### Three other corrections the inventory forced, and one new direction

**`npu0.bp0` is the internal backplane link, not a bypass pair in the empty slot.** The peripherals
page carries the detail. The short form is that `bp0.port0=0:8` is the coprocessor's port 8,
`bp0.port1=1:0` is switch port 0, the key store says 10G, and the two `bp0` MACs are exactly the
addresses on `pport_l0` and `pport_l0s0p0` - the two netdevs in the vendor's interface list this
project could not account for. `PORT_000`, the third entry in the per-port array, is that link seen
from the fast path.

**`FPCNTR_TX_KN` is the punt decision, not the delivery.** On the working unit the counter identities
close exactly: `TX_KN` equals the sum of the four `FROM_WIRE_TO_KN_*` reasons, and `RX_WIRE - TX_KN`
is a constant 14 across four separate captures. So `TX_KN` is bumped where the fast path decides to
hand a frame over, and **nothing downstream of that decision is counted anywhere in the table.** This
page had been treating `TX_KN` as evidence the frame reached the transport. It is evidence the
decision was taken.

**All eight VFs are bound to `vfio-pci` on the vendor's host**, which is why publishing eight VFs here
took the far side down. They are not kernel netdevs; they are handed to a userspace consumer. Claiming
them told the coprocessor to expect eight userspace ring owners that do not exist.

**And the new direction.** The coprocessor's own module list is `pcie_ep` depending on `dpi_dma`
depending on `octeontx2_npa`, with `usfp_rh`, `mv_nwa_target` and `mgmt_net` all sitting on `pcie_ep`.
So on the target side the facility transport is the **DPI DMA engine with NPA-allocated buffers**. A
host-bound payload delivered that way is a DMA write with no SDP output-queue descriptor - and
therefore **no `OUT_PKT_CNT` increment**, which is exactly the symptom this page has been chasing.
Before writing more output-queue code, the thing to establish is whether the vendor's return path for
this traffic is an SDP output queue at all.

### The vendor's bring-up has five steps, and this driver does two of them

`oct_init_base_module` in the vendor's host driver finishes its queues like this, and the order is
readable in the binary:

| | |
|---|---|
| `cn83xx_enable_pf_interrupt(pci_dev, 0xff)` | then a printk, "Interrupts set up completed" |
| `cn83xx_enable_io_queues(oct)` | writes `0xffffffff` to the ring's `0x10040` and polls it back to zero; "IQ/OQs Enable completed" |
| per output queue, `*pkts_credit_reg = droq->max_count` | a **thirty-two bit** store |
| `0x11223344` to BAR0 + `0x20180` | the comment calls it an indication that IOQ creation is complete |
| `oct->status = 9`, then `octeon_send_short_command(0x1004, 2)` | |

This driver already does the equivalent of `cn83xx_enable_output_queue` - `0xffffffff` into the
credit register then bit 0 into the enable - and then credits the ring with its depth. It does not
enable PF interrupts, it does not do the `0x10040` write-and-poll, and until now it had never
written the scratch word.

**The last of those five is dead, and knowing that closes a line of inquiry.**
`octeon_send_short_command` tests `oct->status == 9` - which was set three instructions earlier -
and jumps into a stub whose entire body is a printk saying the path is deprecated, followed by
`ud2`. The same neutering is in `octeon_process_instruction` and `octeon_send_noresponse_command`.
So on this firmware generation the host-to-target bring-up is register writes plus the scratch
handshake, and **there is no missing opcode to find**. The handshake this driver already has is the
right mechanism.

#### The announce was tried, and it changed nothing

`sdp.ioq_announce` saves what is at `0x20180` and writes `0x11223344`; `sdp.ioq_restore` puts the
old word back. That matters because `0x20180` holds the barmap word - `0x02000000abcdabcd`, the
facility table's offset and the ready magic - which is how the facilities were found in the first
place.

With the queues up, the ports raised and the announce made, twenty frames moved `IN_PKT_CNT` to 20
and left `OUT_PKT_CNT` at 0 with no receive buffer written. The word was restored immediately and
the facilities were re-checked: `rpc.size` and `nw_agent.size` both back at 1 MB, and
`PLATFORM_READ` answering `rc 0`.

So the announce is not what is missing.

#### Nor are the interrupt enables, and one of the five turned out to be already done

Of the vendor's five, the `0x10040` write-and-poll was **already there** - `cn83xx_enable_io_queues`
is only a loop calling `cn83xx_enable_input_queue` and `cn83xx_enable_output_queue` per ring, and
this driver does the equivalent of both, including writing all ones to the input doorbell and waiting
for the hardware to take it back to zero. A patch to add it was written and then reverted, which is
the right outcome for a change that duplicates what is there.

That left the interrupt enables. `cn83xx_enable_pf_interrupt` builds a mask of one bit per ring the
function owns, from RINFO's `srn` and `trs`, and writes it to four registers plus all ones to a
fifth:

    SDP_EPF_IRERR_RINT_ENA_W1S   0x200b0   the ring mask
    SDP_EPF_ORERR_RINT_ENA_W1S   0x20130   the ring mask
    SDP_EPF_OEI_RINT_ENA_W1S     0x20170   every bit
    SLI_EPF_MISC_RINT_ENA_W1S    0x28270   the ring mask
    SLI_EPF_PP_VF_RINT_ENA_W1S   0x282f0   the ring mask

`sdp.intr_enable` does exactly that. All five are write-one-to-set, so none of them can clear a
latched status, and the `_RINT` registers themselves are left alone because this driver reads them
as evidence.

OEI was the reason to try it. `SDP_EPF_OEI_RINT` at `0x20140` is the register the **target** sets to
signal the host, and it already read `0x2` - the target had signalled and nothing on this side had
ever enabled the delivery of that signal.

Enabled, with the mask covering all 64 rings, and thirty frames sent:

| | |
|---|---|
| `OEI_RINT` before | `0x2` |
| `OEI_RINT` after | `0x2` |
| `IN_PKT_CNT` | 30 |
| `OUT_PKT_CNT` | **0** |
| buffers differing from the poison | **0 of 256** |
| info blocks non-zero | **0 of 256** |

So the target neither signalled again nor wrote anything. That is now four separate things tried
against the last hop - the announce, the interrupt enables, the ring number and the topology - and
all four negative, with the raw memory scan behind each of them.

What is left is the question the coprocessor's module list raises and none of these touch: whether
the vendor's return path for this traffic is an SDP output queue at all, or a DPI DMA write that
would never increment `OUT_PKT_CNT` and would land wherever the host had told it to land. This
driver has never told it anywhere.

### Not one byte, and that is what settles it

Every reading so far about the output ring has come from a counter or from the driver's own check,
and that check looks for a length word at a fixed offset. If the coprocessor wrote a frame in a
shape the check does not expect, the check would say nothing had arrived. So the ring's memory was
read raw, byte by byte, from `/dev/mem`, against the poison the driver fills it with.

The ring at `OUT_SLIST_BADDR` was read first to find where the memory actually is:

    buffer[0] 0x181cf0000   buffer[1] 0x181cf0642   step 1602
    info[0]   0x2ac411000   info[1]   0x2ac411010   step 16

Then all 256 data buffers - 410,112 bytes - and all 256 sixteen-byte info blocks were compared, once
as a baseline and once after fifty frames had gone out PortF1, come back on PortF2, matched the LIF
and raised `FPCNTR_TX_KN`:

    buffers with any byte not 0xa5 : 0 of 256
    info blocks with any non-zero  : 0 of 256

**Identical both times. Not one byte.**

That closes a real possibility rather than confirming a guess. The frame was not written in an
unexpected format, not written into the info block instead of the buffer, not written short, and not
written to a descriptor the driver's own loop skips. The memory the host advertised is untouched.

#### Which sharpens the conclusion rather than repeating it

The host's queue is enabled, credited and populated; its ring is consistent across all 256 entries;
the target's ability to DMA into host memory is proven continuously by the RPC facility; `OUT_PKT_CNT`
- a counter inside the SDP output path - is zero; and now the advertised memory is provably untouched.
There is no reading left in which the target tried and something went wrong. **The target has nowhere
it believes it may write.**

And the driver already contains the shape of the answer, on its other facility. The management path
delivers because the host publishes descriptors carrying host physical addresses into the
coprocessor's window, and the target writes into them - `rx_cons_shadow` is a consumer index the
target itself wrote into host memory. The SDP datapath publishes its buffers only through SDP
registers. If the vendor's return path for this traffic is a DPI DMA write, as the coprocessor's
module list suggests - `pcie_ep` on `dpi_dma` on `octeontx2_npa` - then the target needs a host
address delivered the way the management facility delivers one, and this driver has never given it
one.

One loophole, stated because it is the only one left: if the target had cached a host address from an
earlier driver load, it would be writing somewhere this scan does not cover. The buffers are
reallocated on every load, so that address would be stale memory belonging to something else, and
nothing has misbehaved. It is not ruled out, only unlikely.

### The fifth path: there is nowhere to publish an address to

The management facility delivers and the SDP datapath does not, and the difference had been stated
as *management publishes host physical addresses into the coprocessor's window and the datapath
publishes only through SDP registers*. That was a description of this driver's own code. Whether the
datapath has somewhere to publish into was a separate question, and answering it needed an
instrument that did not exist: `fclt.peek` reads sixty-four bytes anywhere inside the four windows
the target itself announced, and refuses anything else.

**The control window is the facility table**, exactly as the coprocessor's boot log describes it -
four-word records of type, offset, size and tag:

    +00  0x0200000000000002     type 2, offset 0x02000000
    +08  0x0000009800100000     size 1 MB, tag 0x98
    +10  0x0210000000000001     type 1, offset 0x02100000
    ...                         and two more, tags 0x9a and 0x9b

**The nw_agent window is a mailbox**, and it declares an event region: `cafebabe`, a body offset of
0x34, a turn word, and an event area at offset `0x8000` running `0x8000` bytes. Thirty-two kilobytes
for the target to write into. **It is entirely zero**, at its head and forty bytes in.

**And the management window carries exactly the structure the datapath lacks.** Its receive
descriptor queue, at window + 65536, reads:

    +00  cons_idx 1, prod_idx 2
    +08  num_entries 0x100, buf_size 0x800
    +10  shadow_cons   0x18f74e040      a host physical address
    +28  descriptor 0  0x17374b000      a host physical address
    +38  descriptor 1  0x17374b800      and the next, 0x800 further on

That is the whole mechanism in one read: the host writes its own physical addresses into the
coprocessor's window, one per descriptor, spaced by the buffer size; it writes the producer index
last, because that is what makes them visible; and the target consumes them - `cons_idx` is 1, so it
has taken one.

**The datapath has no such structure anywhere in the four windows.** Not in the control window,
which is a table of the windows themselves; not in the nw_agent window, whose event region is empty;
and not in a fifth window, because there is no fifth window. So "publish addresses the way the
management facility does" cannot be done as a change to this driver: there is no agreed place to
write them, and inventing one would be writing into a live target's memory on a guess.

That is a negative, and it is the useful kind. It says the SDP datapath's only channel for telling
the target where the host's receive memory is, is the SDP registers - which this driver has
programmed correctly, and which the target is not acting on. Issue #115 is sharpened by it rather
than answered: either the target expects a structure that has not been found, or the vendor's
datapath receive does not work by publication at all.

The instrument stays, because the next thing anyone does here will need it.

### The target's own modules, and what none of them does

The four kernel modules on the coprocessor's side of the link came out of the v22 image with their
symbols intact, and between them they answer what the facility architecture is - and, more usefully,
what it is not.

**`mgmt_net` delivers by DPI DMA.** The calls out of its transmit path are
`do_dma_async_dpi_vector`, `host_iounmap`, `mv_facility_free_dbell_irq`. So the facility that works
does not write into a ring the host advertised through registers: it maps host memory, DMAs into it
through the DPI engine, and rings a doorbell. That matches the module dependency list exactly -
`dpi_dma` is used by `mgmt_net` and `pcie_ep`, and `octeontx2_npa` under it supplies the buffers.

**`pcie_ep` is the facility API**, and its exports name the whole mechanism:

    mv_get_bar_mem_map        mv_get_facility_conf      mv_get_facility_handle
    mv_pci_get_dma_dev        mv_pci_get_dma_dev_count  mv_pci_sync_dma
    mv_send_facility_dbell    mv_send_facility_event    mv_facility_register_event_callback

A facility module asks `pcie_ep` for a DMA device and for the window's map, writes through DPI, and
signals with a doorbell or an event.

**`mv_nwa_target` does none of it.** Its entire symbol table is sysfs handlers - `nwa_port_info_show`,
`nwa_switch_info_store`, `nwa_num_of_ports_show`, twenty-odd of them - plus
`nwa_get_pcie_mailbox_addr`. It references `mv_get_bar_mem_map` once, to find the mailbox, and
**nothing else**: no DMA call, no host mapping, no delivery path of any kind. The NetAgent facility
is a control mailbox and was never a datapath.

#### Which settles where the front-port receive path has to live

No kernel module on the coprocessor delivers front-port frames to the host. `mgmt_net` carries the
management netdev, `mv_nwa_target` carries port control, `usfp_rh` serves the RPC table handler. The
fast path's own host delivery is inside the **userspace** `usfp`, which reaches the host through
DPDK ports created from `pci_port`, which are SDP.

So the SDP output ring is the right mechanism after all, and the DPI-DMA reading - which this page
raised as the most promising new direction - is **how the facilities work and not how the datapath
works**. That is worth stating plainly because it was written here as the thing most likely to be
the answer.

### The LIF mask rule, completed, and a sweep that measured nothing

`LIF_M_MAC 0x01`, `LIF_M_MTU 0x02`, `LIF_M_FWD 0x04`, `LIF_M_ADMIN_DISABLED 0x08`,
`LIF_M_OFFLOAD_DISABLED 0x10`, **`LIF_M_REPPID 0x20`**.

This page already recorded that a **new** entry is refused with a partial mask. The complement is now
measured and it is the half that bites: an entry that already exists is refused with a **full** mask.

    iface 1, reppid 0      new     rc 0 ok
    iface 1, reppid 4095   update  rc 1 refused
    iface 2, reppid 4095   new     rc 0 ok
    iface 2, reppid 0      update  rc 1 refused

**A sweep of `reppid` over fourteen values from 0 to 4095 returned `rc 1` for every one, and that
measured nothing about `reppid`.** All fourteen were updates of an entry that already existed, so all
fourteen were refused for the same reason and none of them for the value. It is recorded because a
sweep that returns the same answer to every question looks like a finding and is not one.

Updating the live LIF with mask `0x20` alone - only the representor - is accepted, and `reppid` 4095
is a perfectly valid value. The appliance's own capture had 4095 on the interface that carried
traffic and 0 on the two that did not, which is what made it worth trying. Set on the working LIF, the
round trip continues unchanged and `OUT_PKT_CNT` stays 0. Six things now tried against the last hop,
six negative.

### The output path inside the fast path, and a false lead worth recording

`usfp` is mostly stripped, but not entirely, and three of the symbols that survive carry the shape of
the host-bound path.

**A false lead first, because it cost time.** `__sfp_handle_exceptions` looks exactly like a punt
handler and is not one: disassembled, it is `fpsr`, `fdiv`, `fadd`, `fmul` - a **floating point**
exception helper out of the C library. The "sfp" is soft float, not Sophos fast path.

**The real one is `worker_ordered`**, 27,536 bytes at `0x423e30`, which is the whole packet loop -
the port tag test at `0x426c28` and the control-message gate at `0x429030` are both inside it. It
makes 66 calls to 25 distinct targets, and among them is **`hash_sp_txq_get`**: get a **slow path**
transmit queue by hash, where the slow path is the host.

Disassembled, it is a CRC32C over the connection tuple - the addresses at +24 and +32 of the flow
record and the ports at +40 and +48 - finished with

    udiv  w0, w3, w2
    msub  w0, w0, w2, w3     /* w3 % w2 */

so the queue is `hash(tuple) % n`.

**And `n` is eight on this board specifically.** `usfp_startup_octtx.sh` passes it as
`-t $num_sp_txqs`, its default is 4, and the platform case sets it to 8 for assembly
`AMDA0202-0004` - which is this appliance. So a frame the fast path decides to hand the host goes to
one of eight queues, chosen by hashing the tuple.

**Every test frame this driver had ever sent carried the same tuple**: 0.0.0.0 to 255.255.255.255,
port 9 to port 9. All of them hashed to the same queue. If that queue were not the one ring this
driver programs, none could ever arrive - which would have explained every measurement at once.

`dp.sport` varies the source port so the tuple varies with it. Sixty-four frames, each with a
different source port:

    IN_PKT_CNT   64
    OUT_PKT_CNT  0
    0 of 256 receive buffers have been written

**So the hash is not the answer either.** With `pf_srn` published as 0 the target's queues would be
0 to 7, and a hash spread over eight of them should have put roughly an eighth of sixty-four frames
on ring 0. None arrived. Either the slow-path queue index is not the ring index, or the mapping runs
somewhere this driver cannot see.

That is eight things tried against the last hop and eight negatives. The structure found here is
real and worth having - the queue count, the hash, the tuple it hashes - but it did not open the
path.

### The metadata does not matter on this path, and that took three runs to establish

The 64 metadata bytes were first sent as zeros, then as a walking pattern from `0xc0` - so `0xc0` to
`0xff` - on the reading that the fast path validates them and counts failures in
`FPCNTR_FROM_KN_DROP_MISMATCH_METADATA_FIELDS`, which is a real counter in the shipped binary. The
pattern changed nothing.

The vendor's own target application then gave a third candidate, and it is not a pattern at all.
`common/apps_rxtx.h` in the DPDK application sources - the `v22.0.Maint.040.Luzon` drop, against a
board running Maint.060 - writes a **signature**:

    #define METADATA_SIGNATURE  0xa0a1a2a3a4a5a6a7
    #define PORT_TAG_SIZE       2
    #define METADATA_SIZE       64
    #define PRIV_TAG_SIZE       (METADATA_SIZE + PORT_TAG_SIZE)

    *((uint16_t *)data) = rte_cpu_to_be_16(tag);
    *((uint64_t *)(data + PORT_TAG_SIZE)) = rte_cpu_to_be_64(metadata);

Two things follow. **`PRIV_TAG_SIZE` is 66**, which is this driver's private header under the
vendor's own name rather than a length derived here. And only the first eight of the sixty-four
bytes are ever written by that code, as a big-endian signature.

So `dp.meta` was added to send any of the three, and all three were posted on the ring in one
sitting, 25 frames each, with the same fibre in place between F1 and F2:

| `dp.meta` | the 64 bytes | consumed | returned |
|---|---|---|---|
| 0 | walking pattern from `0xc0` | 25 of 25 | none |
| 1 | `0xa0a1a2a3a4a5a6a7`, rest zero | 25 of 25 | none |
| 2 | all zeros | 25 of 25 | none |

`IN_PKT_CNT` rose by exactly 25 each time and `IN_BYTE_CNT` matched, `OUT_PKT_CNT` stayed at 0, and
none of the 256 poisoned receive buffers was written in any of the three.

**The metadata content is not what decides anything here.** The earlier reading - that zeros were a
mismatch, that a mismatched frame is freed on arrival, and that this was why the transmit counters
were always exact - is withdrawn: zeros are consumed exactly as the pattern is, and the counters are
exact in all three cases. What that counter in the binary is fed by is still unknown, and it is not
this path.

One limit worth stating, and it is now the limit on all three: *consumed* means `IN_PKT_CNT` rose.
None of these runs shows any of the three reaching a wire - the observation that once seemed to show
it has been withdrawn, because the cages it rested on have no activity LED.

### What separates this driver from the vendor's is no longer a field

The host side of a working link is three modules, not one. `octnic` creates `oct0`; `mv_nwa_host`
asks NetAgent for the port list and calls `register_pport_device` once per tag; and `pport` creates
a virtual netdev per front port, stacked on `oct0`.

**None of that reaches the coprocessor, and an earlier reading of this page said it did.** Read as
source rather than inferred:

- `register_pport_device` registers a netdev with the **host's own** pport layer - `pport_main.c:322`
- `nwa_create_pports` walks the port list and calls it, and sends no message at all -
  `mv_nwa_host.c:1406`
- `octnet_open`, which is `ifconfig oct0 up`, sets local state and starts the transmit queue and
  sends nothing - `octeon_network.c:126`

So the three modules build the host's own interface tree, and there is no registration handshake
with the far side to be missing. That makes the gap narrower and stranger than "this driver is one
of three": nothing in Marvell's host stack ever tells the coprocessor that a host is ready to
receive, and the coprocessor still delivers to the vendor's host and not to this one.

What that leaves is the forwarding decision on the coprocessor itself, which is reached from
somewhere other than the GPL host module - see the operation space in
[../netagent.md](../netagent.md), most of which has no sender there at all.

This driver sends well-formed frames to a coprocessor that hands nothing back. That is the remaining
gap. It is not a header field, and nothing above
should be read as suggesting another byte will fix it. See issue #64.

### A dead instrument, recorded so it is not trusted

The NetAgent per-port statistics read - op `0x04`, attribute `0x0e`, a 264-byte reply of 64 counters -
returns the same single non-zero word for every tag, before and after traffic, including for the
switch uplink and for a switch port. It does not distinguish anything on this path and must not be
used as evidence that a frame did or did not reach a port.

### Promiscuous mode is accepted here and changes nothing

On ARMADA this exact attribute is the answer to this exact question. A front port that has not been
put in promiscuous mode receives broadcast and nothing else, so a bridged port forwards nothing;
`docs/netagent.md` records the measurement, 0 of 20 frames before and 20 of 20 after. The two
families share the framing and the attribute number, so it was the first thing to try here.

The target accepts it:

    nwa.op=3  nwa.sub=0x45  nwa.port=1  nwa.param=1   ->  status 0x00000000 (ok)
    nwa.op=3  nwa.sub=0x45  nwa.port=2  nwa.param=1   ->  status 0x00000000 (ok)

Both cages read link up through attribute `0x00` at the same moment, and both are `MNG` ports, so
this is the operation Marvell's own host module would issue for them rather than a guess at an
unhandled code. Forty frames were then posted with F1's tag:

    IN_PKT_CNT   40   IN_BYTE_CNT 24240      every one consumed
    OUT_PKT_CNT  0    OUT_BYTE_CNT 0
    0 of 256 receive buffers have been written

So promiscuous mode is **not** what is missing here, and the same attribute that fixes the ARMADA
case does not fix this one. Two things were eliminated alongside it, both by reading rather than by
trying: the output ring's credit is granted - `octep_dp.c` writes 256 into `R_OUT_SLIST_DBELL`,
which is the register Marvell's driver calls `pkts_credit_reg` - and the target's own scratch
register reads `0x1`, its started-port bitmap, so the coprocessor considers the host's SDP port up.

What is left is the forwarding decision itself: something has to tell the coprocessor's fast path
that a frame arriving at a front port belongs to the host. The two flow-configuration messages in
Marvell's attribute enum are the obvious candidates, and no caller in the GPL host module sends
either of them - which means they come from somewhere else in the vendor's stack. See issue #64.

## NetAgent answers the host

The control plane works. `contrib/octep/octep_nwa.c` carries the NetAgent transaction over the
`nw_agent` facility window, and it answered before anything was plugged in - which, on a bench where
the switch was unconfigured and both cages were empty, was the difference between a measurement and a
wait.

    sysctl dev.octep.0.nwa.header      # the five words the target published
    sysctl dev.octep.0.nwa.discover=1  # issue a request
    sysctl dev.octep.0.nwa.last        # what came back
    sysctl dev.octep.0.nwa.release=1   # release a window a previous host left held

The header, read off the hardware:

    +0x00 cookie      0xcafebabe (CAFEBABE, as ARMADA publishes)
    +0x04 body offset 0x00000034 (expected; also the version gate)
    +0x08 max request 0x00007fcc  32716 bytes
    +0x0c event off   0x00008000
    +0x10 event len   0x00008000
    doorbell for this facility: SPI 154

And a discover:

    op 0x01  marker 0x00000014 (expected)  status 0x00000000 (ok)  reply 2020 bytes
    payload 503 words

    dev.octep.0.nwa.commands: 2      dev.octep.0.nwa.timeouts: 0

**The payload is decoded.** The reply opens with `14` and continues in five-word records. The caution
that used to stand here - do not read 14 as a port count, because the board has twelve panel ports and
three coprocessor MACs and 14 matches neither - was right to be cautious and wrong in its conclusion.

`14` **is** the port count, and the board does have fourteen: twelve panel ports plus the two ports of
the expansion-slot bypass segment. The platform key store says so independently, `npu0.eth.macs=14`,
and allocates exactly fourteen addresses. Thirteen records come back non-zero; the fourteenth is the
unpopulated slot, `npu0.slotA.present=0`.

Each record is 20 bytes and its first word is `tag | flags`, not a port id - which is why
`0x00010001` looked like an identifier and is really tag `0x0001` with flags `0x0001`. The full table
is in [docs/netagent.md](../netagent.md).

### Three things this cost, all of them mine

Recorded because each was plausible while it was wrong.

**A function that returned either a word count or an errno.** `ETIMEDOUT` is 60, so the first timeout
printed sixty words of the other processor's leftovers as an answer. What gave it away was the counter
beside it reading `timeouts: 1` for a transaction that had apparently succeeded.

**A transaction inside a read handler.** `sysctl(8)` calls a string handler twice - once to size the
buffer, once for the data - so one `sysctl` issued two transactions, the second arriving while the first
was in flight. The request is a write now and the read only formats what was stored.

**No acknowledge.** The protocol is "write 1 to send, 2 to acknowledge", and the acknowledge was
omitted. That does not lose a reply, it wedges the window: `STATUS` stays at `REPLY` and everything
afterwards times out waiting for idle. A 2020-byte reply sat stranded until a release path existed:

    octep0: nwa: status 1 with a 2020 byte reply stranded in the window - acknowledging it

The acknowledge is a write to the host's own `TURN`. The target's `STATUS` is never written here -
clearing another processor's register to take a turn is how two drivers end up writing one slot.

### Asking a port about itself

`nwa.op`, `nwa.sub` and `nwa.port` hold the next request and `nwa.request=1` issues it, because
the port field carries a **TAG** rather than an ordinal - on ARMADA the front ports are `0x8100`,
`0x8200` and so on; OCTEON's are now known too, and are listed below, but the tool still asks
exactly what it is told to rather than assuming an encoding. So the tool asks exactly what it is
told to and reports exactly what came back, rather than assuming an encoding. **`op 0x03` is now
allowed for exactly one attribute**, `0x00`, attribute `0x00` - writing 1 to it raises a front
port, and reading the same attribute back returns that port's LINK state, not its administrative
state. Every other SET attribute - MTU, address, filtering - is still refused by name.

The tags the discover reply publishes are accepted. Two of them, `0x0001` and `0x0002`, are the
coprocessor's own 10G MACs and carry 65535-entry filter tables; `0x8000` is the switch uplink and
`0x8100` to `0x8a00` are the ten switch ports, with 12-entry tables. The filter-table size is the
quickest way to tell the two kinds apart.

    sysctl dev.octep.0.nwa.op=4 ; sysctl dev.octep.0.nwa.sub=4
    sysctl dev.octep.0.nwa.port=0x00010001 ; sysctl dev.octep.0.nwa.request=1
    sysctl dev.octep.0.nwa.last

    op 0x04  sub 0x04  port 0x00010001  marker 0x14 (expected)  status 0 (ok)  reply 12 bytes
    payload 1 word
      [ 0] 0x00002710  10000

**Ten thousand.** `docs/netagent.md` records op 0x04 sub 0x04 as the link query whose answer carries the
speed, and `0x3e8` = 1000 Mb/s on a 1 Gb ARMADA port; `0x2710` is 10000, so 10 Gb/s. All three port ids
answer the same.

**And it is a speed, not a carrier.** Two of those three are the empty SFP+ cages, which DPDK reports as
Link Down - so 10000 is what the port is configured or able to do, not what is plugged into it. Reading
it as "three ports up" would be exactly the sort of over-reading this page has had to correct before.

What else answered, on one port, all with the expected marker and an OK status:

    sub 0x00   1 byte  = 0        LINK state (0 = down; both cages were empty here)
    sub 0x03   8 bytes = 0, 0     MAC address, unset
    sub 0x04   1 word  = 10000    link speed
    sub 0x0a   1 word  = 2
    sub 0x01, 0x02, 0x05-0x09     status 0x01, cleanly refused

The nine-byte reply at sub 0x00 is worth pointing at: it is precisely the case the round-up-and-mask
handling exists for, and it returned one word rather than truncating to nothing. That truncation is the
bug that hid link state on ten ports for the whole life of the ARMADA driver, and here it did not happen.

### And then the sweep took NetAgent down

Continuing that sweep past the refusals, **sub 0x0b and everything after it timed out, and
afterwards even the known-good sub 0x04 timed out. The window stayed wedged until the coprocessor
was rebooted, after which NetAgent answered normally again - op 0x01, 0x03, 0x04 and 0x45 all
work.** The window reads only its three header words - `TURN`, `STATUS`, both lengths all zero -
so a request is accepted and simply never answered: the target cleared its side and stopped
servicing.

`usfp` did **not** crash; it is still running with no core dumped, so this is the NetAgent handler inside
it going quiet rather than the process dying - and nothing short of a coprocessor reboot is known to
bring it back. Nothing else is affected: the management link still carries IP at 0% loss.

**The lesson, and it is the second time on this board: do not sweep an input space whose far side is a
live service.** The clean refusals at 0x01 and 0x05-0x09 made it look as though the far side validated
its input and would simply say no - and then one value past them took the service down. A cleanly
rejected input is evidence about that input and about nothing else. Ask the sub-codes
[../netagent.md](../netagent.md) records as confirmed, and learn new ones from the vendor's own host
driver - whose source is in the GPL drop - rather than from the target's tolerance.

#### `0x0b` is `FEC`, and `ethtool` reaches it

That sweep was read at the time as a probe into unknown codes. It was not. Every value in it is a
named attribute in `enum nwa_msg_port_attr`, and the one that stopped the handler is `FEC`:

| sub | name | what happened |
|---|---|---|
| `0x00` | `STATE` | answered 0 |
| `0x01` | `OPER_STATE` | refused, status 1 |
| `0x02` | `MTU` | refused |
| `0x03` | `MAC` | answered, unset |
| `0x04` | `SPEED` | answered 10000 |
| `0x05` .. `0x09` | `ACCEPT_FRAME_TYPE`, `LEARNING`, `FLOOD`, `CAPABILITY`, `LINK_MODE` | refused |
| `0x0a` | `TYPE` | answered 2 |
| **`0x0b`** | **`FEC`** | **stopped the handler for good** |

**This is reachable from a shell, in one command, and the whole chain is source:**

    ethtool --show-fec pport_lX
      -> pport_ethtool_ops.get_fecparam = pport_get_fecparam   pport_dev.c:1536, :1270
      -> pport_hw_get_fecparam                                 pport.h:142
      -> nwa_pport_ext_port_ops.get_fecparam                   mv_nwa_host.c:1346
      -> nwa_port_get_fecparam, attr = NWA_MSG_PORT_ATTR_FEC    mv_nwa_host.c:1076

`get_fecparam` sits in the operation table a port gets when its switch-init record carries the `MNG`
flag, and on this appliance every panel port carries it. So a single `ethtool --show-fec` against a
front port takes NetAgent down until the coprocessor is rebooted, and nothing in the vendor's host
stack stands in the way. See issue #78.

It also explains the `0x00` label that had to be withdrawn. The target answers the getter for
`STATE` with the operational state, and registers no handler at all for `OPER_STATE`, which is the
attribute actually named that. Reading `0x00` therefore returns the link - and the earlier
"administrative state" reading was the enum's name for it rather than the target's behaviour.

## The target's side of NetAgent is source, and it settles three questions

`soc_agent` is the agent NetAgent talks to on the coprocessor, and it is in Sophos's GPL drop as
source: `sources-dpdk_app-SDK10.22.03/common/lib/soc_agent/`, 7,700 lines across a common file, an
ARMADA file, an OCTEON file, a LAG file and a rate-limit table. Reading it answers questions that
had been approached by asking the hardware.

> **Which version this is, because it is not the one on the appliance.** The source drop is
> `v22.0.Maint.040.Luzon`, and its components are pinned to `*-release-SDK10.22.03`. The coprocessor
> rootfs on this appliance says `rootfs-2026.0518-1247-1235-v22.0.Maint.060.Bali`, and its `usfp`
> carries `SDK10.22.03` too. So the same major release and the same Marvell SDK, and **twenty
> maintenance builds apart**. Every constant and every dispatch table below is Maint.040. Everything
> stated as measured is from the Maint.060 board. Where the two disagree - and one place below they
> do - the board wins and the gap is the explanation.

### Exactly six operations have a handler, and that closes off a whole hypothesis

`soca_init_dispatcher` is the entire dispatch table:

    _dispatcher[NWA_MSG_TYPE_INIT]               = soca_init_system;
    _dispatcher[NWA_MSG_TYPE_PORT_ATTR_SET]      = soca_process_port_attr_set;
    _dispatcher[NWA_MSG_TYPE_PORT_ATTR_GET]      = soca_process_port_attr_get;
    _dispatcher[NWA_MSG_TYPE_PORT_INFO_GET]      = soca_process_port_info_get;
    _dispatcher[NWA_MSG_TYPE_ALL_LINK_STATUS]    = soca_process_link_status_update;
    _dispatcher[NWA_MSG_TYPE_ALL_COMB_PORT_INFO] = soca_port_comb_info_get;

Four of those are the four this project had found by asking: `0x01`, `0x03`, `0x04` and `0x45`. Two
have never been tried from here - `0x05` PORT_INFO_GET, which the source shows simply ACKs and
ignores its request, and `0x40` ALL_LINK_STATUS, which ignores its request too and returns a
**bitmap of link state by DPDK port id**. That bitmap is the one thing published anywhere that would
tie a port identifier to a DPDK port number.

And **bridge create, bridge port add, the FDB operations and the VLAN operations have no handler at
all.** They are declared in the host header, nothing in the host module sends them, and nothing on
the target implements them. A reading that the return direction needs a bridge and a port in it is
therefore wrong, and it is ruled out without sending anything.

### The attributes, from the target rather than from a sweep

Implemented on SET: `STATE`, `MTU`, `MAC`, `AUTONEG`, `SPEED`, `DUPLEX`, `PROMISC`, `PAUSE`,
`FEATURES`, `ALLMULTI`, `MC_ADD`, `MC_DELETE`, `UC_ADD`, `UC_DELETE`, `RATE_LIMIT`, `KSETTINGS`.

Implemented on GET: `STATE`, `MTU`, `SPEED`, `AUTONEG`, `DUPLEX`, `TYPE`, `STATS`, `PAUSE`,
`KSETTINGS`.

`FEC` is in neither, and in this source an unimplemented attribute falls to a `default:` that
returns `NWA_MSG_ACK_FAILED` - a clean refusal, not a hang. So the incident in which `0x0b` stopped
the handler for good is **not** explained by this source, and that is the one place where the twenty
maintenance builds between Maint.040 and the board's Maint.060 have to be the answer: something in
that gap either implements `FEC` badly or changed what an unimplemented attribute does. The driver
refuses `0x0b` either way.

`soca_port_state_set` also settles what the state values mean, and it is more than up and down:
0 sets the link down, 1 sets it up, **2 stops the port's transmit queues and 3 starts them** - added
by a Sophos patch, `0006-soc_agent_add_start_stop_txqueues_message.patch`. A `2` seen on the wire
during a port-down is that, not a third spelling of "down".

### The host is a port type on the target, and the frame format is symmetric

`enum soca_port_type` is how the fast path classifies what it owns:

    SOCA_PORT_TYPE_SOC              a coprocessor MAC
    SOCA_PORT_TYPE_SOC_SWITCH       the uplink to the 88E6193X
    SOCA_PORT_TYPE_NPU_PF           the PCIe endpoint - the host
    SOCA_PORT_TYPE_NPU_VF           a virtual function of it
    SOCA_PORT_TYPE_SWITCH_LAG_MASTER / _SLAVE / NPU_PF_LAG_MASTER / OTHER

`common/apps_rxtx.h` then switches on that type in both directions, and the `NPU_PF` arms are this
driver's frame format seen from the other end: on receive it reads a big-endian tag at offset 0 and
a big-endian 64-bit word at offset 2 and then pulls `PORT_TAG_SIZE + METADATA_SIZE` off the front;
on send it prepends the same. A `SOC` port carries the tag out of band instead, in `m->udata64`.

### Forwarding is configured, not automatic

The sample application's port map defaults to `dst_port = src_port` - every port loops back to
itself - and a CSV overrides it, one row per source port:

    Source Port, source tag, Destination port, destination tag

So a frame arriving at a front port goes wherever the map says, and **nothing reaches the host
unless something has said so**. None of the six NetAgent operations can say it. Whatever does say it
in the vendor's system arrives by another road, and the one host-to-coprocessor channel this project
has never touched is the `rpc` facility - whose wire format is now read out of the coprocessor's own
module in [octeon-tx-rpc.md](octeon-tx-rpc.md) -  - 1 MB, five doorbells and four DMA devices against one and
one for the others, and `MV_FACILITY_RPC` is handled in the vendor host driver's `device_access.c`.

## What the appliance says about itself while the vendor's firmware is running

The strongest evidence on this question was not on the wire and not in the vendor's source. It was
in the sweep taken off this board while SFOS v22.0.2 was running it, which is the only state in which
the return direction has ever worked here.

`usfp_table_print.sh` writes to and reads from `/sys/kernel/debug/usfp/table/<name>`, and the fast
path publishes there: `conn`, `lif`, `mflow`, `nhop`, `luid`, `sa`, `qos`, `platform_info`,
`worker_dbg_cnt`, `worker_sys_cnt`, `worker_port_cnt`. Three of those settle the question.

### There is no DPDK port for the host

    PORT_000_PORT_CNT_RX  54039      PORT_000_PORT_CNT_TX  44679
    PORT_001_PORT_CNT_RX      6      PORT_001_PORT_CNT_TX      6
    PORT_002_PORT_CNT_RX      6      PORT_002_PORT_CNT_TX      6

**Three ports, and all three are wire.** Port 0 carries everything, which is the switch uplink with
ten panel ports behind it; 1 and 2 are the two SFP cages, empty at the time. The host is not a DPDK
ethdev on this platform, so the `SOCA_PORT_TYPE_NPU_PF` arms of `apps_rxtx.h` are not the path a
frame takes to reach it here.

### The host path is named, and it is gated on flow state

    FPCNTR_RX_WIRE                             61888
    FPCNTR_TX_WIRE                             49232
    FPCNTR_RX_KN                               49966
    FPCNTR_TX_KN                               61874
    FPCNTR_FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE    41072
    FPCNTR_FROM_WIRE_TO_KN_NON_ACCEL           20800
    FPCNTR_FROM_WIRE_TO_KN_CONN_RECLAIMED          1
    FPCNTR_FROM_WIRE_TO_KN_TCP_FIN_SYN_RST         1
    FPCNTR_FROM_KN_PROC_CMSG                     748
    FPCNTR_FROM_KN_TO_WIRE                     49218

`KN` is the kernel - the host. Almost everything the wire produced went up to it, and **every reason
recorded for sending a frame to the host is a statement about flow state**: no active mflow, not
accelerated, a connection reclaimed, a TCP flag that ends a connection. A fast path with no flow
state at all has nothing to consult, and nothing in these counters says "deliver to the host because
the host exists".

`FROM_KN_TO_WIRE` is what this driver already does: 49,218 frames went down and out. `RX_KN` and
`TX_KN` are both large, so the channel is symmetric under the vendor's firmware.

And `FROM_KN_PROC_CMSG` is 748. **The host sends control messages down the same channel**, and this
driver has never sent one.

### Half of a flow belongs to the host

    Mflow id: 11029
    Mflow fw valid: 1
    Mflow host valid: 0

A flow entry has a firewall-side validity and a **host-side** validity, kept separately. Whatever
makes `host valid` true is something the host does, and it is not any of the six NetAgent operations.

### The opcodes that are not `OCT_NW_PKT_OP`

`octeon-drv-opcodes.h` gives the whole space that rides the instruction ring:

| opcode | name | |
|---|---|---|
| `0x1220` | `OCT_NW_PKT_OP` | a network packet - **the only one this driver has ever sent** |
| `0x1221` | `OCT_NW_CMD_OP` | a network command, carrying an `octnet_cmd_t` |
| `0x1222` | `HOST_NW_INFO_OP` | host network info |
| `0x1223` | `HOST_PORT_STATS_OP` | |
| `0x1225` | `HOST_NW_STOP_OP` | sent on the way down |

A control instruction is shaped differently from a data one. `octnet_prepare_ls_soft_instr` builds
the `HOST_NW_INFO_OP` case:

    si->ih.fsz = 16;              /* not 28 - there is no PKI header */
    si->ih.tagtype = ORDERED_TAG;
    si->ih.tag = 0x11111111;
    si->ih.raw = 1;
    si->irh.opcode = HOST_NW_INFO_OP;
    si->irh.param = 32;
    si->dptr = NULL;
    si->ih.dlengsz = 0;           /* no data at all */

That call is live in a normal build - it is guarded by `#if !defined(ETHERPCI)`, and the instruction
is kept as `si_link_status`. Everything that would **post** it is inside `#if 0`, so the GPL host
driver never sends it; but that dead block is still the only written description of how a
request-response works on this ring:

    si->rptr = &(ls->resp_hdr);
    si->irh.rlenssz = (OCT_LINK_STATUS_RESP_SIZE - sizeof(ls->s));
    si->status_word = (uint64_t *)&(ls->status);
    *(si->status_word) = COMPLETION_WORD_INIT;

**`rptr` is a host buffer for the answer and `rlenssz` is how long the answer may be.** This driver
carries both fields already, and uses `rlenssz` for the one thing the NIC data path overloads it
with - the checksum offset - because on a data packet there is no response.

### So the next thing to try is a request, not a packet

Everything measured says the coprocessor can write into host memory and does so constantly under the
vendor's firmware, and that what reaches the host is decided by state the host installs. This driver
has only ever sent data. The cheapest test that separates "the target cannot write to us" from "the
target has nothing to say" is a control instruction with `rptr` pointing at a poisoned host buffer
and `rlenssz` set to a real length: if the poison is overwritten, the return direction exists and
only needs a reason.

## What is not done

**Host traffic reaches the coprocessor, and stops there.** One SDP ring - ring 0 - is programmed
and enabled by the host, and the coprocessor consumes every frame posted on it; the other 63 are
untouched and do not need to be. Nothing shows a frame leaving a front port, and nothing comes back.

**The ring is not what is missing.** It is programmed and accepted by the silicon - "One SDP ring"
above - the output half is enabled, 256 buffers of credit are granted through `R_OUT_SLIST_DBELL`,
and the coprocessor's own scratch register reads `0x1`, its started-port bitmap. The host has to do
all of that itself: the base addresses are host memory and the enables are host registers, and
nothing on the coprocessor's side writes either. `slipf` only ever writes the scratch register,
`SDP_OUT_WMARK`, the backpressure enables and `SDP_GBL_CONTROL`. The order matters and is the same
rule as for the management link - spin on `IDLE`, write `BADDR` and `RSIZE`, then the enables,
because `BADDR` cannot be written while the ring is busy, and nothing announces readiness with
incomplete rings.

~~**What is missing is the forwarding decision on the coprocessor.**~~ **Withdrawn.** Nothing was
missing on the coprocessor's side: it was deciding correctly all along. The frames it sent were
either asked to be encrypted before they could reach a wire, or landed on a ring this driver never
read. See "The loop closes" at the end of this page, which supersedes this paragraph and much of
what is above it about the last hop.

`nw_agent` is published, and it came live the moment the coprocessor's fast path started rather
than when the host datapath did - the handshake is what gates it. NetAgent transactions work from
the host; see "NetAgent answers the host".

There is no MSI-X, one queue each way, a copy per frame, no offload, and nothing persistent: no rc
script and no package.

## The loop closes

Everything above this heading was written while no frame had reached a wire. That is no longer the
state, and four separate faults had to be cleared to change it.

### The metadata chooses the destination, and it was asking for encryption

The 64 bytes in front of every frame are not filler and they are not validated - they are read.
`worker_ordered` steps over a 28-byte prefix, reads one byte, and when it is non-zero takes the
four bytes after it as an egress security-association handle:

```
426bf8  strh wzr, [x21, #0x26]     clear
426bfc  add  x2, x1, #2
426c00  ldrb w1, [x1, #3]          a metadata byte
426c04  cbz  w1, +0xc              zero: leave the destination alone
426c08  ldr  w1, [x2, #0xc]        the four bytes after it
426c0c  strh w1, [x21, #0x26]      the egress association handle
```

Its verdict table, built by `fp_state_init` - a 16 KB byte array indexed by a packed key of source,
destination and a detail code - names both outcomes. Decoding that table against the 182 ordered
`FPCNTR_` names reproduces all 122 of its entries exactly, which is how the decode was confirmed:

| `[0x19c]` source | `[0x1a0]` destination | counter |
|---|---|---|
| 2 FROM_KN | 1 TO_WIRE | 97 `FPCNTR_FROM_KN_TO_WIRE` |
| 2 FROM_KN | 5 TO_IPSEC_ENCR | 98 `FPCNTR_FROM_KN_TO_IPSEC_ENCR` |

The walking pattern from `0xc0` makes the byte in question `0xdf`, so **every frame asked to be
encrypted**, none could be, and all of them were charged to `FPCNTR_TX_DROP`. Measured: 272 frames
under the pattern gave `FROM_KN_TO_IPSEC_ENCR 272` and `TX_DROP 272` with `TX_WIRE 0`; 21 under the
vendor's form - byte 0 set to 1, the other 63 zero - gave `FROM_KN_TO_WIRE 21` and `TX_WIRE 21`.
Merged as #127.

**The counter that named the cause is index 98, and the window being read was 0..63.** Read the
whole of 0..181, and remember that the printed index is relative to `s_index`.

### The host states the ring split, and has to build what it states

`RINFO` at `BAR0+0x20190` is not only a description of how the endpoint is carved up. The vendor's
`cn83xx_setup_global_mac_regs` reads it, ORs in two fields and writes it back:

```
rpvf = dev+0x9d8, nvfs = dev+0x9dc
RINFO = RINFO | (rpvf << 32) | (nvfs << 48)
```

On the appliance's own firmware that gives `0x0008000100400000` - eight VFs at one ring each - so
the PF's rings begin at **8**, which is why its live rings are 8-15 while 0-7 sit idle.

Declaring the split is half of it. Booting the appliance onto its internal SFOS disk and reading its
PCI bus while its datapath carried traffic shows the other half:

```
lspci -d 177d:       9      (177d:a300 PF + eight 177d:a303 VFs)
sriov_numvfs         8 of 64 supported
```

**The functions are real.** An earlier attempt published eight in the handshake word without
building them, and the far side went down - it had been told about eight ring sets nothing on the
bus backed. So the order is create, declare, then hand over. FreeBSD has no `sriov_numvfs` control
and no IOV driver attaches here, so the capability is driven directly: NumVFs, then VF-Enable, with
VF memory space left disabled. Merged as #131.

With it, `RINFO` reads the vendor's value exactly, rings 0-7 gain `IN_CONTROL 0x0001000000000000`
and the PF can no longer program them, the target reports `poll_for_ep_mode rpvf 1 vf_srn 0
num_vfs 8 rppf 8 pf_srn 8` **and stays up**, and `usfp` builds twelve ethdev ports:

```
port00 : eth_octeontx_net_b3_l0 (SWITCH)
port01 : eth_octeontx_net_b2_l0 (SOC)
port02 : eth_octeontx_net_b2_l1 (SOC)
port03..port10 : eth_octeontx_pci_vf_0..7 (NPU_VF)
port11 : eth_octeontx_pci_pf_8 (NPU_PF)
```

The PF is last and its name carries its ring base - `pci_pf_8` where it used to read `pci_pf_0`.

### Rings before the handshake, and never restarted

The target latches the host's ring addresses when its port opens and does not look again.

```
rings programmed before the handshake, then left alone
  800 frames   TX_KN 800   FPCNTR_TX_DROP 0   TX_DROP_QUEUE_FULL 0

after two dp.stop / dp.start cycles
  400 frames               TX_DROP 192        TX_DROP_QUEUE_FULL 192, still climbing
```

A restart re-allocates the rings at new addresses, the far side keeps writing to the old ones, and
nothing reports it - no error, no backpressure at first, then the queue fills. **Every burst
measured earlier on this page was taken after at least one restart**, which is why the queue always
looked permanently stuck.

### The return prefix is 82 bytes, and it lands on a ring transmit does not choose

A frame that completed the loop, read byte for byte out of the host buffer that received it:

```
+0x00  00 00 00 00 00 00 00 86      the SDP info qword: length 0x86 = 134
+0x08  00 00 00 00 00 00 03 80      0x8003000000000000, prepended by worker_ordered
+0x10  00 02                        the pport tag, big-endian: 2 = PortF2
+0x12  a2 99 43 b4 00 .. 00         64 metadata bytes; the first four are 0xb44399a2
+0x52  ff ff ff ff ff ff            broadcast
       02 00 00 00 00 01            the driver's locally administered source MAC
       08 00                        IPv4
+0x60  45 00 00 2e .. 40 11 ..      0.0.0.0 -> 255.255.255.255, UDP 9 -> 9
+0x80  2a 2b .. 3a 3b               the driver's incrementing payload
```

`134 = 74 + 60`, and 74 is the `tx_offload.l2_len += 0x4a` the fast path sets. **The 66 recorded
elsewhere on this page is the target's own 2+64** - it is not what lands in a host buffer, because
two more qwords sit in front of it.

It arrived on **ring 12**, a sibling, while the driver reported "0 of 256 receive buffers have been
written" - because it only ever scanned the ring transmit uses. The fast path picks its host queue
by hashing the frame, so that choice is not the host's. Both fixed in #132.

### Reading the far side, which is how several of these were settled

The coprocessor's console is reachable from the host and gives a root shell. Set the line to 115200
raw with no echo, start exactly one reader on it, send `root` and an empty password. **Only one
reader may hold that line**; two interleave and the transcript is unreadable.

The datapath's own output is **not** on the console: `xgs_startup.sh` ends with
`$DP_1US_STARTUP_SCRIPT ... 2>&1 | logger -t dpdk`, so everything `usfp` prints is in the
coprocessor's syslog under the tag `dpdk`. That is where its port list, its `poll_for_ep_mode` line
and its one startup error are.

### What is still not right

**One frame of six hundred arrives.** The rest leave the coprocessor with no drop and no
backpressure recorded anywhere, and every test frame is identical, so `crc32c(tuple) % 8` ought to
send them all to the same ring. Nothing is presented as a netdev either. That is the open work, and
it is now a question about throughput rather than about silence.
