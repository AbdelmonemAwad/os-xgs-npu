/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * octep_dp - bring up one SDP datapath ring pair on the OCTEON TX endpoint.
 *
 * WHAT THIS IS FOR. The management facility carries one interface and was never meant to carry more;
 * the appliance's front ports are reached across SDP, the PCIe packet interface, and SDP needs rings
 * that only the host can create. The coprocessor cannot do it for us and does not try: the base
 * addresses are host memory and the enables are host registers, and `slipf` writes neither - its only
 * writes are the EP-mode scratch register, `SDP_OUT_WMARK`, the backpressure enables and
 * `SDP_GBL_CONTROL`. That was checked before running any vendor code against half-configured
 * hardware, and it is also why doing so was safe.
 *
 * WHAT IT DOES, AND WHAT IT STILL DOES NOT. It allocates one instruction ring and one scatter list
 * with its buffers, arms as many receive-only sibling rings beside it, programs each ring through
 * the sequence below, enables it, and grants the output ring its buffer credits. It transmits, and
 * it reads received packets back out of every armed ring - both halves of that sentence were once
 * the opposite, and this header said so for longer than it was true. Frames posted here reach a
 * front port and have been counted arriving on a machine at the other end of the cable.
 *
 * What it still does not do is present an interface, and each armed ring delivers one packet and
 * then stops.
 *
 * WHERE THE PARAMETERS COME FROM, AND WHY NOT FROM THE SOURCE. Every load-bearing choice below sits
 * behind an #ifdef in the vendor's source, so the source cannot say how the shipped driver was built.
 * They were read out of the shipped v22 binary instead:
 *
 *   64-byte instructions   `default_cn83xx_pf_conf.instr_type` reads 64, and that field is inside
 *                          `#ifndef IOQ_PERF_MODE_O3`. So the host must SET IS_64B - the hardware
 *                          reads it CLEAR, and the vendor's own comment claiming it is "by default
 *                          enabled" is wrong on this board.
 *   buffer-pointer only    could NOT be read from that object - its info_ptr field is the constant 1
 *                          in every build. Settled by a string that exists only in the other branch,
 *                          "OCTEON: Cannot allocate memory for info list.", absent from both shipped
 *                          modules. So: no info list, IMODE stays clear, and ISIZE stays zero.
 *   1536-byte buffers      `buf_size` reads exactly 1536 in the two shipped variants read there.
 *                          This driver uses 1602, which is 1536 plus the 66-byte port-extender
 *                          header, because that is what the appliance's own OUT_CONTROL carries:
 *                          the live register reads 0x1004000642, whose low 23 bits are 1602.
 *
 * THE ONE ORDERING RULE THAT IS NOT OPTIONAL. `BADDR` cannot be written while a ring is busy, so both
 * halves spin on their control register's IDLE bit first. The vendor's loops have no timeout; these
 * do, because a wedged host helps nobody.
 *
 * Provenance, including which source said what and which claim rests on a binary rather than on the
 * published tree, is docs/octeontx/provenance.md.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/smp.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/epoch.h>
#include <sys/mbuf.h>
#include <sys/taskqueue.h>
#include <sys/endian.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/hash.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_vlan_var.h>
#include <net/bpf.h>
/*
 * For ifp->if_bridge, which is how the kernel itself asks whether an interface is a bridge
 * member and has no accessor in the if_t KPI. See octep_dp_if_filters() for the argument; the
 * short of it is that this module is built against the running kernel's own sources and refuses
 * to load against any other, so the layout it compiles against is the one it runs on.
 */
#include <net/if_private.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/vnet.h>

/* IPPROTO_TCP and IPPROTO_UDP, for reading a punted frame's ports and nothing else. */
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>

#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/resource.h>

#include <sys/pciio.h>		/* pcivar.h defines struct pci_devinfo only after it: the MSI-X table and PBA a stall record reads (#361) */
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "octep.h"

#define	OCTEP_DP_IDLE_TRIES	1000		/* x 10 us */

static int octep_dp_msix_setup(struct octep_softc *sc);
static uint32_t octep_dp_service_all(struct octep_softc *sc, int watchdog);
static void octep_dp_rxwd(void *arg);
static void octep_hb_tick(void *arg);
static void octep_dp_rxwd_arm(struct octep_softc *sc);
static void octep_dp_rxpeek(struct octep_softc *sc);
static void octep_dp_msix_teardown(struct octep_softc *sc);
static int octep_dp_if_attach(struct octep_softc *sc, uint16_t tag);
static void octep_dp_if_detach_all(struct octep_softc *sc);
static struct octep_dp_if *octep_dp_if_by_tag(struct octep_softc *sc, uint16_t tag);

static bus_size_t
octep_dp_reg(struct octep_softc *sc, bus_size_t base)
{

	return (base + (bus_size_t)sc->dp_ring * OCTEP_SDP_RING_STRIDE);
}

static uint64_t
octep_dp_rd(struct octep_softc *sc, bus_size_t base)
{

	return (bus_read_8(sc->bar0, octep_dp_reg(sc, base)));
}

static void
octep_dp_wr(struct octep_softc *sc, bus_size_t base, uint64_t v)
{

	bus_write_8(sc->bar0, octep_dp_reg(sc, base), v);
}

/* The same two, for a ring that is not sc->dp_ring. */
static uint64_t
octep_dp_ring_rd(struct octep_softc *sc, uint32_t ring, bus_size_t base)
{

	return (bus_read_8(sc->bar0,
	    base + (bus_size_t)ring * OCTEP_SDP_RING_STRIDE));
}

static void
octep_dp_ring_wr(struct octep_softc *sc, uint32_t ring, bus_size_t base, uint64_t v)
{

	bus_write_8(sc->bar0, base + (bus_size_t)ring * OCTEP_SDP_RING_STRIDE, v);
}

/*
 * Spin until the ring reports itself idle. The vendor does this with an untimed loop and a comment
 * saying BADDR cannot be configured while IDLE is 0; the timeout is ours.
 */
static int
octep_dp_wait_idle(struct octep_softc *sc, bus_size_t ctl, uint64_t idle_bit, const char *what)
{
	uint64_t v;
	int i;

	for (i = 0; i < OCTEP_DP_IDLE_TRIES; i++) {
		v = octep_dp_rd(sc, ctl);
		if ((v & idle_bit) != 0)
			return (0);
		DELAY(10);
	}
	device_printf(sc->dev, "dp: ring %u %s never went idle (0x%016jx)\n",
	    sc->dp_ring, what, (uintmax_t)octep_dp_rd(sc, ctl));
	return (EBUSY);
}

/*
 * Put one ring back to the state the hardware powers up in. There is no per-ring reset on this part,
 * so every register is cleared by hand - and the enables go first, because nothing else may be
 * touched while the ring is live.
 */
static void
octep_dp_reset_ring(struct octep_softc *sc)
{

	octep_dp_wr(sc, OCTEP_SDP_R_IN_ENABLE, 0);
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_ENABLE, 0);

	octep_dp_wr(sc, OCTEP_SDP_R_IN_INSTR_BADDR, 0);
	octep_dp_wr(sc, OCTEP_SDP_R_IN_INSTR_RSIZE, 0);
	octep_dp_wr(sc, OCTEP_SDP_R_IN_INSTR_DBELL, 0xffffffffULL);
	octep_dp_wr(sc, OCTEP_SDP_R_IN_CNTS, 0);
	octep_dp_wr(sc, OCTEP_SDP_R_IN_INT_LEVELS, 0);
	octep_dp_wr(sc, OCTEP_SDP_R_IN_PKT_CNT, 0);
	octep_dp_wr(sc, OCTEP_SDP_R_IN_BYTE_CNT, 0);

	octep_dp_wr(sc, OCTEP_SDP_R_OUT_SLIST_BADDR, 0);
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_SLIST_RSIZE, 0);
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_SLIST_DBELL, 0xffffffffULL);
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_CNTS, 0);
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_INT_LEVELS, 0);
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_PKT_CNT, 0);
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_BYTE_CNT, 0);
}

/*
 * The policy pass. On the input side three bits are ORed in and nothing else is touched. On the
 * output side every ordering and snoop attribute is cleared except ES_P, and IMODE is cleared
 * because this build is buffer-pointer only.
 */
static void
octep_dp_policy(struct octep_softc *sc)
{
	uint64_t v;

	v = octep_dp_rd(sc, OCTEP_SDP_R_IN_CONTROL);
	v |= OCTEP_R_IN_CTL_RDSIZE | OCTEP_R_IN_CTL_IS_64B | OCTEP_R_IN_CTL_ESR;
	octep_dp_wr(sc, OCTEP_SDP_R_IN_CONTROL, v);

	v = octep_dp_rd(sc, OCTEP_SDP_R_OUT_CONTROL);
	v &= ~OCTEP_R_OUT_CTL_IMODE;
	v &= ~(OCTEP_R_OUT_CTL_ROR_P | OCTEP_R_OUT_CTL_NSR_P);
	v &= ~(OCTEP_R_OUT_CTL_ROR_I | OCTEP_R_OUT_CTL_NSR_I | OCTEP_R_OUT_CTL_ES_I);
	v &= ~(OCTEP_R_OUT_CTL_ROR_D | OCTEP_R_OUT_CTL_NSR_D | OCTEP_R_OUT_CTL_ES_D);
	v |= OCTEP_R_OUT_CTL_ES_P;	/* the pointer fetch is swapped; the vendor calls this
					 * required on 83xx */
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_CONTROL, v);
}

/*
 * The output interrupt threshold is a packet count in the low half and a time in the high half, and
 * the time is expressed in units of 1024 coprocessor clocks rather than microseconds. The clock came
 * from the coprocessor itself during the EP-mode handshake, which is the only place it is published.
 */
static uint32_t
octep_dp_oq_ticks(struct octep_softc *sc, uint32_t usec)
{
	uint32_t per_us = sc->sdp_coproc_ticks_per_us;

	if (per_us == 0)
		return (0);
	per_us *= 1000;		/* clocks per millisecond */
	per_us /= 1024;		/* output-queue ticks per millisecond */
	per_us *= usec;
	per_us /= 1000;
	return (per_us);
}

/*
 * Bring an output ring up, in the vendor's own order and on its own side of the enable.
 *
 * `cn83xx_enable_output_queue` is 108 bytes and does exactly two things, in this order:
 *
 *	[base + ring*0x20000 + 0x10140] = 0xffffffff        R_OUT_SLIST_DBELL
 *	[base + ring*0x20000 + 0x10160] |= 1                R_OUT_ENABLE
 *
 * and `cn83xx_enable_io_queues` inlines the same pair for every ring. **Neither writes a credit** -
 * the first real count comes later, from the refill path, after an `sfence`.
 *
 * So the `0xffffffff` is written to a ring that is **configured and not yet enabled**: base address
 * and size already set by `cn83xx_setup_oq_regs`, the block not yet consuming. This driver used to
 * write it after the ring was enabled, where it is a grant of 4,294,967,295 buffers rather than a
 * reset - measured, every ring read `R_OUT_SLIST_DBELL 0x100000000f1` - and then it was moved in
 * front of the base address instead, which cleared the register but is not what the vendor does
 * either. This is the vendor's placement: after the configuration, before the enable.
 *
 * Whether the difference matters is a measurement, and the reason to think it might is that the
 * block has a base address and a size to latch at the moment the doorbell is rung.
 */
static void
octep_dp_oq_enable(struct octep_softc *sc, uint32_t ring)
{
	uint64_t v;

	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_SLIST_DBELL, 0xffffffffULL);
	v = octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_ENABLE);
	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_ENABLE, v | 1ULL);
}

/*
 * The first credit a ring is granted, and the ceiling its credit is held to afterwards: the
 * ring, the coprocessor's output watermark, less one fetch.
 *
 * It was `entries * 16`. Granting `entries` made a ring deliver exactly one packet for weeks;
 * granted sixteen times that, it delivered 37 in a single burst, with 511 interrupts where there
 * had been 8. Less than a batch of credit fetches a batch anyway and writes nothing: a ring
 * granted 2 read back 0xfffffff2, which is 2 - 16. That fits a watermark of 1,024 - see
 * OCTEP_DP_OQ_WMARK - which a ring of 256 granted 256 is under; it was not measured again. And
 * sixteen times the ring is fifteen rings more credit than there are buffers.
 *
 * WHAT THAT COST. With credit for sixteen rings the block does not stop at a full one. Measured
 * with the host held away for a tenth of a second under 120,000 datagrams a second: 12,910
 * packets written into 1,024 buffers, round and round over frames nobody had read. The host came
 * back and read the buffers in index order, which after a lap is the newest frames first and the
 * older ones after them - 363 of that first ring's worth behind a higher number - and 11,950 were
 * never delivered at all, with no drop counted anywhere. The count register was left 11,180 too
 * high, so every visit after it ended on a count with no buffer behind it. This is what an ESP
 * tunnel's kernel saw as replays, thousands a row, whenever a ring had been left to fill.
 *
 * With the ring, the watermark, less one fetch, the block has credit for exactly the buffers the
 * host has taken: it fetches while the doorbell reads the watermark or more, so the last fetch it
 * can make is the one that leaves the doorbell sixteen under it, and that is a ring's worth. The
 * same tenth of a second: 1,009 and 1,000 packets waiting, taken in order, none behind, the count
 * right, and the rest dropped by the coprocessor's own queue - where a full ring is counted.
 */
static uint32_t
octep_dp_oq_first_grant(struct octep_softc *sc)
{

	if (sc->dp_oq_grant != 0)
		return (sc->dp_oq_grant);
	return (sc->dp_oq_rsize + sc->dp_oq_wmark - OCTEP_DP_OQ_FETCH);
}

/*
 * Hand back what a ring is owed, as far as its grant allows, and keep the rest.
 *
 * One unit for each buffer taken is exact, and exact has no slack: nothing tops a ring up any
 * more, so a unit that is not returned when it is owed is a buffer the block never gets back.
 * Lower the ceiling on a live ring - dp.oq_grant is read at every pass - and every pass returned
 * nothing; the block spent what it had, stopped under its watermark, and raising the ceiling
 * again could not revive it, because credit was only ever written by a pass that had taken
 * something and a stopped ring delivers nothing to take. Measured, on a ring of 512 with the
 * ceiling at 1,008: nothing delivered. And with what follows, under 119,000 datagrams a second:
 * 12 frames in half a second with the ceiling down, 61,156 in a half second that began a fifth
 * of a second after it was given back.
 *
 * So what is owed is carried, as the vendor's host carries the buffers it has not yet refilled:
 * each pass adds what it took, gives what the ceiling has room for, and keeps the difference, and
 * a pass that takes nothing still gives what is owed. The watchdog reaches every ring every
 * period, so a ring that was cut is whole again within one of them.
 *
 * Called with the ring held. The doorbell's low half is signed: a ring that fetched a batch it
 * had no credit for reads below zero.
 */
static void
octep_dp_oq_credit(struct octep_softc *sc, uint32_t ring, uint32_t taken)
{
	int64_t have, room;
	uint32_t give, owed;

	owed = sc->dp_oq_carry[ring] + taken;
	if (owed == 0)
		return;
	have = (int32_t)(octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_SLIST_DBELL) &
	    0xffffffffULL);
	if (have < 0)
		have = 0;
	room = (int64_t)octep_dp_oq_first_grant(sc) - have;
	if (room < 0)
		room = 0;
	give = owed;
	if ((int64_t)give > room)
		give = (uint32_t)room;
	if (give != owed)
		sc->dp_credit_capped++;
	sc->dp_oq_carry[ring] = owed - give;
	if (give != 0)
		octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_SLIST_DBELL, give);
}

/*
 * Drain an output ring's scatter-list doorbell against a dead ring. Kept for the teardown path,
 * where the base address and the size are being zeroed anyway.
 */
static void __unused
octep_dp_oq_dbell_drain(struct octep_softc *sc, uint32_t ring)
{
	uint32_t i;

	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_SLIST_BADDR, 0);
	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_SLIST_RSIZE, 0);
	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_SLIST_DBELL, 0xffffffffULL);
	for (i = 0; i < OCTEP_DP_IDLE_TRIES; i++) {
		if ((octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_SLIST_DBELL) &
		    0xffffffffULL) == 0)
			return;
		DELAY(10);
	}
	device_printf(sc->dev, "dp: ring %u output doorbell would not drain, it reads 0x%jx\n",
	    ring, (uintmax_t)octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_SLIST_DBELL));
}

/*
 * Fill the scatter list. Each descriptor is a buffer pointer and an info pointer, and both have to be
 * real memory: the far side DMAs the packet to one and a 16-byte response header and length to the
 * other. This used to leave the info pointer at zero on the assumption that it was unread, which
 * asked the coprocessor to write a header to physical address zero.
 */
static void
octep_dp_fill_slist(struct octep_softc *sc)
{
	uint64_t *e = (uint64_t *)sc->dp_slist.vaddr;
	uint32_t i;

	/*
 * The arrival flag is a non-zero length word at the head of a receive buffer, and the coprocessor
 * zeroes nothing - so a buffer that was never touched and a buffer the far side wrote zeros into
 * look identical. Poison them instead of zeroing them: the length word is then obviously not a
 * length until something overwrites it, and a report of "nothing arrived" means it.
 */
	memset(sc->dp_bufs.vaddr, OCTEP_DP_BUF_POISON,
	    (size_t)OCTEP_DP_OQ_DESCS * OCTEP_DP_BUF_STRIDE);
	bus_dmamap_sync(sc->dp_bufs.tag, sc->dp_bufs.map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bzero(sc->dp_info.vaddr, (size_t)OCTEP_DP_OQ_DESCS * OCTEP_DP_OQ_INFO_SIZE);
	for (i = 0; i < OCTEP_DP_OQ_DESCS; i++) {
		e[i * 2] = (uint64_t)sc->dp_bufs.paddr +
		    ((uint64_t)i * OCTEP_DP_BUF_STRIDE);
		e[i * 2 + 1] = (uint64_t)sc->dp_info.paddr +
		    ((uint64_t)i * OCTEP_DP_OQ_INFO_SIZE);
	}
	bus_dmamap_sync(sc->dp_info.tag, sc->dp_info.map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->dp_slist.tag, sc->dp_slist.map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

/*
 * Arm one receive-only ring. Everything the output side of octep_dp_start does, for a ring this
 * driver will never transmit on: allocate, publish the buffers, wait for IDLE, set BADDR and RSIZE,
 * write the attributes and the buffer size, quieten the interrupt, enable, then credit every
 * descriptor. No input side is touched.
 */
static int
octep_dp_arm_sibling(struct octep_softc *sc, struct octep_dp_oq *oq, uint32_t ring)
{
	uint64_t *e;
	uint64_t v;
	uint32_t i;
	int err;

	oq->ring = ring;
	err = octep_dma_alloc(sc, &oq->slist,
	    (bus_size_t)OCTEP_DP_OQ_DESCS * OCTEP_DP_SLIST_ENTRY, PAGE_SIZE, "dp sib slist");
	if (err != 0)
		return (err);
	err = octep_dma_alloc(sc, &oq->bufs,
	    (bus_size_t)OCTEP_DP_OQ_DESCS * OCTEP_DP_BUF_STRIDE, OCTEP_DP_BUF_ALIGN,
	    "dp sib buffers");
	if (err != 0)
		return (err);
	err = octep_dma_alloc(sc, &oq->info,
	    (bus_size_t)OCTEP_DP_OQ_DESCS * OCTEP_DP_OQ_INFO_SIZE, 128, "dp sib info");
	if (err != 0)
		return (err);

	memset(oq->bufs.vaddr, OCTEP_DP_BUF_POISON,
	    (size_t)OCTEP_DP_OQ_DESCS * OCTEP_DP_BUF_STRIDE);
	bzero(oq->info.vaddr, (size_t)OCTEP_DP_OQ_DESCS * OCTEP_DP_OQ_INFO_SIZE);
	e = (uint64_t *)oq->slist.vaddr;
	for (i = 0; i < OCTEP_DP_OQ_DESCS; i++) {
		e[i * 2] = (uint64_t)oq->bufs.paddr + ((uint64_t)i * OCTEP_DP_BUF_STRIDE);
		e[i * 2 + 1] = (uint64_t)oq->info.paddr + ((uint64_t)i * OCTEP_DP_OQ_INFO_SIZE);
	}
	bus_dmamap_sync(oq->bufs.tag, oq->bufs.map, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(oq->info.tag, oq->info.map, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(oq->slist.tag, oq->slist.map, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	for (i = 0; i < 1000; i++) {
		if ((octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_CONTROL) &
		    OCTEP_R_OUT_CTL_IDLE) != 0)
			break;
		DELAY(1000);
	}
	if (i == 1000) {
		device_printf(sc->dev, "dp: ring %u never reported output idle\n", ring);
		return (ETIMEDOUT);
	}

	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_SLIST_BADDR, oq->slist.paddr);
	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_SLIST_RSIZE, sc->dp_oq_rsize);
	v = octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_CONTROL);
	v &= ~(OCTEP_R_OUT_CTL_SIZE_MASK | OCTEP_R_OUT_CTL_ATTR_MASK);
	v |= OCTEP_R_OUT_CTL_ES_P;
	v |= (uint64_t)(OCTEP_DP_BUF_SIZE & 0xffff);
	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_CONTROL, v);
	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_INT_LEVELS,
	    ((uint64_t)sc->dp_time_threshold << 32) | OCTEP_DP_OQ_INTR_PKT);
	octep_dp_oq_enable(sc, ring);
	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_SLIST_DBELL, octep_dp_oq_first_grant(sc));
	oq->armed = 1;
	device_printf(sc->dev,
	    "dp: sibling ring %u armed - oq %u x %u B every %u B at 0x%jx, slist at 0x%jx, "
	    "out_control 0x%jx\n",
	    ring, OCTEP_DP_OQ_DESCS, OCTEP_DP_BUF_SIZE, OCTEP_DP_BUF_STRIDE,
	    (uintmax_t)oq->bufs.paddr, (uintmax_t)oq->slist.paddr,
	    (uintmax_t)octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_CONTROL));
	return (0);
}

static void
octep_dp_free_siblings(struct octep_softc *sc)
{
	struct octep_dp_oq *oq;
	uint32_t i;

	for (i = 0; i < OCTEP_DP_SIBLINGS_MAX; i++) {
		oq = &sc->dp_sib[i];
		if (oq->armed != 0) {
			octep_dp_ring_wr(sc, oq->ring, OCTEP_SDP_R_OUT_ENABLE, 0);
			octep_dp_ring_wr(sc, oq->ring, OCTEP_SDP_R_OUT_SLIST_BADDR, 0);
			octep_dp_ring_wr(sc, oq->ring, OCTEP_SDP_R_OUT_SLIST_RSIZE, 0);
			oq->armed = 0;
		}
		octep_dma_free(&oq->info);
		octep_dma_free(&oq->bufs);
		octep_dma_free(&oq->slist);
	}
}

/*
 * Service every ring on a timer, whatever the interrupts are doing.
 *
 * This exists because of a failure that had no recovery at all. The output rings were served from
 * one place, a ring's MSI-X handler, and the block raises its interrupt on R_OUT_CNTS crossing the
 * level rather than on its sitting above it. Miss one edge and nothing in the driver ever looked at
 * that ring again: the far side keeps writing frames into host memory, the count climbs, and the
 * host has an interface that transmits and receives nothing. Measured here with the WAN at 100%
 * loss, 1,532 frames already written, and all eight vectors at a standstill.
 *
 * So recovery does not depend on the interrupt. A pass over an idle ring is one register read, and
 * octep_dp_oq_service() is safe against a handler running beside it - it takes the ring's busy
 * flag for its visit, and a servicer that finds it held leaves a note for the holder and goes.
 *
 * THE NET TORE WHAT IT WAS UNDER. Its visit was one pass. On a ring the handler was working it
 * could take the flag between two of the handler's passes; the handler's next pass found the ring
 * held, took that for a ring with nothing in it and left; the one pass ended with the packets that
 * had arrived during it still in the ring, the count above the level and nothing to cross it
 * again; and the ring then waited for this timer, which did the same thing again. Measured as a
 * single TCP stream through the host arriving in bursts 53 ms apart - 37 to 39 Mbit/s second after
 * second, where the same stream ran at 769 and 798 with this timer at one tick - and as
 * dp.rxwd_runs moving 196 times in ten seconds, which is every period. So a visit is no longer a
 * pass, and a servicer turned away says so: see octep_dp_oq_service.
 */
static void
octep_dp_rxwd(void *arg)
{
	struct octep_softc *sc = arg;
	u_int soon;

	/*
	 * No re-arm while a quiesce is set, which is what makes callout_drain() in
	 * octep_dp_rx_quiesce() final instead of a race against this line.
	 */
	if (sc->dp_rxwd_on == 0 || atomic_load_acq_int(&sc->dp_rx_quiesce) != 0)
		return;

	/*
	 * The request this run answers is taken down before the rings are read, so one made while
	 * they are being walked is still standing afterwards - octep_dp_rxwd_arm looks.
	 */
	soon = atomic_readandclear_int(&sc->dp_rxwd_soon);
	/* The longest from one run's start to the next's: the period, the run before, any lateness. */
	if (sc->dp_rxwd_prev != 0 && (uint32_t)(ticks - sc->dp_rxwd_prev) > sc->dp_rxwd_gap_max)
		sc->dp_rxwd_gap_max = (uint32_t)(ticks - sc->dp_rxwd_prev);
	sc->dp_rxwd_prev = ticks;
	if (sc->dp_up != 0) {
		/*
		 * A sweep when one was asked for and once a period; between them, the look (#361),
		 * which reads no register and visits only a ring an interrupt has failed.
		 */
		if (soon != 0 || sc->dp_rxpeek_ticks == 0 ||
		    ticks - sc->dp_rxwd_last >= (int)sc->dp_rxwd_ticks) {
			sc->dp_rxwd_last = ticks;
			if (octep_dp_service_all(sc, 1) != 0)
				sc->dp_rxwd_runs++;
		} else
			octep_dp_rxpeek(sc);
	}
	octep_dp_rxwd_arm(sc);
}

/*
 * Arm the watchdog for its period - and then for the next tick, if that has been asked for.
 *
 * In that order, and in every place that arms it. A servicer that asks for the next tick resets
 * the callout to one tick; a re-arm for the period made after that puts it back, and the ring that
 * asked waits the whole period after asking not to. The watchdog re-arming itself on its way out
 * did that, and so did octep_dp_rx_resume. So the request is a flag as well as a reset, and
 * whoever arms for the period reads the flag afterwards.
 */
static void
octep_dp_rxwd_arm(struct octep_softc *sc)
{
	uint32_t t;

	t = sc->dp_rxwd_ticks;
	if (sc->dp_rxpeek_ticks != 0 && sc->dp_rxpeek_ticks < t)
		t = sc->dp_rxpeek_ticks;
	callout_reset(&sc->dp_rxwd, t, octep_dp_rxwd, sc);
	if (atomic_load_acq_int(&sc->dp_rxwd_soon) != 0)
		callout_reset(&sc->dp_rxwd, 1, octep_dp_rxwd, sc);
}

/*
 * Ask for a visit on the next tick, because a servicer has left work behind in this ring.
 *
 * The ring is recorded as well as the request: the watchdog judges what it finds in a ring by
 * whether that ring asked, and a request from one ring says nothing about the one beside it.
 *
 * And a servicer can get here after a quiesce has drained the callout - it read the quiesce flag
 * a moment before it was set. The quiesce writes its flag and then drains; this arms and then
 * reads the flag; so one of the two always sees the other, and what is armed here is taken back
 * here. Resume arms the watchdog again and reads the request.
 */
static void
octep_dp_rxwd_kick(struct octep_softc *sc, uint32_t ring)
{

	if (sc->dp_rxwd_on == 0)
		return;
	atomic_store_rel_int(&sc->dp_oq_asked[ring], 1);
	atomic_store_rel_int(&sc->dp_rxwd_soon, 1);
	callout_reset(&sc->dp_rxwd, 1, octep_dp_rxwd, sc);
	if (atomic_load_acq_int(&sc->dp_rx_quiesce) != 0)
		(void)callout_stop(&sc->dp_rxwd);
}

/*
 * Stop every servicer, and wait until none is inside a ring.
 *
 * WHAT THIS PROTECTS, and it is not the ring. A servicer reads dif->ifp, puts it in each mbuf's
 * rcvif, and hands the chain to if_input(). Detaching an interface calls ether_ifdetach() and then
 * if_free() on that same ifnet. Nothing connected the two, so a detach could free an ifnet while a
 * servicer still held mbufs pointing at it - inside the kernel of a machine carrying traffic.
 *
 * Every path that reached it was deliberate - dp.stop, dp.if_del, a device detach, kldunload - and
 * "you have to ask for it" is not the same as safe.
 *
 * HOW IT IS CLOSED. Two halves, and both are needed. A servicer that has not started yet is turned
 * away: it takes the ring's busy flag, sees the quiesce and gives the flag straight back, so the
 * check is after the acquire and not before it - before it, one could pass the check and then take
 * the flag after this function had finished looking. A servicer already inside is waited for, which
 * is what the busy flag is read for here; and for that wait to mean anything, if_input() had to move
 * back inside the flag. It used to be called after releasing it, deliberately, so the ring was free
 * while the stack worked - a throughput choice that cost nothing visible and opened this window. It
 * costs nothing now either: the buffers are re-poisoned and the doorbell is credited before the
 * frames go up, so the block has its credits back either way, and only another servicer of the same
 * ring is kept out - which is the one thing that must be kept out.
 *
 * The watchdog is drained rather than flagged, and its handler declines to re-arm while a quiesce is
 * set, so callout_drain() is final rather than racing a re-arm. A servicer still inside a ring can
 * ask for the next tick after the drain; octep_dp_rxwd_kick takes that back when it sees this flag.
 *
 * The flag is set with a locked operation and not a plain store. This function writes the flag and
 * then reads each ring's busy flag; a servicer writes its busy flag and then reads this one; and
 * with a plain store on this side each can read the other's old value - a servicer inside a ring
 * that the wait below has just found empty.
 */
static void
octep_dp_rx_quiesce(struct octep_softc *sc)
{
	uint32_t i;
	int spins;

	(void)atomic_swap_int(&sc->dp_rx_quiesce, 1);
	if (sc->dp_rxwd_on != 0)
		callout_drain(&sc->dp_rxwd);

	for (i = 0; i <= OCTEP_DP_SIBLINGS_MAX; i++) {
		for (spins = 0; spins < OCTEP_DP_QUIESCE_SPINS; spins++) {
			if (atomic_load_acq_int(&sc->dp_oq_busy[i]) == 0)
				break;
			DELAY(10);
		}
		/*
		 * Whether the wait ever does anything is the question this mechanism lives or dies on, and
		 * a sample of the busy flags cannot answer it - a servicer is inside a ring for
		 * microseconds, so a reading taken next to one will almost always be clear. These two
		 * count the times it was not.
		 */
		if (spins != 0) {
			sc->dp_quiesce_waits++;
			if ((uint32_t)spins * 10 > sc->dp_quiesce_max_us)
				sc->dp_quiesce_max_us = (uint32_t)spins * 10;
		}
		if (atomic_load_acq_int(&sc->dp_oq_busy[i]) != 0)
			device_printf(sc->dev, "dp: ring %u was still being serviced a second "
			    "after servicing was stopped; going on without it\n", i);
	}
}

/*
 * Let the servicers back in - and service, before anything else gets a turn.
 *
 * dp.if_del removes the interfaces and leaves the datapath up, so a quiesce has to be undoable. A
 * frame arriving afterwards finds no interface for its tag and is counted as untagged, which is the
 * right answer rather than a problem.
 *
 * THE SERVICE HERE IS NOT AN OPTIMISATION. A quiesce turns a servicer away before it clears
 * R_OUT_INT_STATUS, so every interrupt that arrives while it is set is taken and thrown away with
 * the status still latched - and the block raises its interrupt on the count crossing the level, not
 * on its sitting above it, so once the quiesce lifts there is nothing left to raise one. Clearing the
 * status on the way out instead would lose the edge just the same. The only way back is for the host
 * to look, so the host looks here.
 *
 * This was measured rather than reasoned about, and the first version of this function did not have
 * it: resume left the ring to the watchdog's next tick, 50 ticks away. 5,285 quiesce and resume
 * cycles over ten seconds, with a download offering 1,510 frames a second underneath, delivered
 * FOUR FRAMES - each resume armed a timer that the next quiesce drained two milliseconds later, so
 * nothing ever looked at the rings and the receive path was dead for the whole run. It recovered
 * afterwards, on the first tick that was allowed to fire, which is the watchdog doing its job and
 * not an excuse for needing it.
 *
 * Bounded as the handler's visit is, because a quiesce taken under load leaves a backlog and no
 * interrupt is coming to finish it: sixteen sweeps, each of them a visit of one ring's worth to
 * every ring. A sweep that has to leave a ring with work asks for the watchdog's next tick, so the
 * watchdog is armed here the way it arms itself - for the period, and then for that tick if it was
 * asked for. Arming it for the period alone threw the request away.
 */
static void
octep_dp_rx_resume(struct octep_softc *sc)
{
	uint32_t i, rounds;

	/*
	 * Every ring is owed a look after a quiesce, and says so: the sweeps below take the rings
	 * one after another, a full ring is milliseconds of handing up, and the watchdog - asked
	 * for its next tick by the first visit that ends on its bound - can reach a ring before
	 * they do. What it finds there was held by the quiesce and not left by a servicer, and it
	 * was being counted as a stall.
	 */
	for (i = 0; i <= OCTEP_DP_SIBLINGS_MAX; i++)
		atomic_store_rel_int(&sc->dp_oq_asked[i], 1);
	atomic_store_rel_int(&sc->dp_rx_quiesce, 0);
	for (rounds = 0; rounds < OCTEP_DP_OQ_DRAIN_ROUNDS; rounds++) {
		if (sc->dp_up == 0 || octep_dp_service(sc) == 0)
			break;
	}
	if (sc->dp_rxwd_on != 0)
		octep_dp_rxwd_arm(sc);
}

static int	octep_dp_txq_up(struct octep_softc *sc, struct octep_dp_txq *q);
static void	octep_dp_txq_down(struct octep_softc *sc, struct octep_dp_txq *q);
static void	octep_ips_if_create(struct octep_softc *sc);
static void	octep_ips_if_destroy(struct octep_softc *sc);
static void	octep_ips_mark(struct octep_softc *sc, struct octep_conn *c);
static int	octep_sysctl_dp_vlan_lif_table(SYSCTL_HANDLER_ARGS);
static void	octep_dp_closing_task(void *, int);
#define	OCTEP_CLOSING_QLEN	256	/* FINs and RSTs held for pf: octep_dp_closing_hold */

int
octep_dp_start(struct octep_softc *sc)
{
	uint64_t v;
	uint32_t i;
	int err;

	mtx_lock(&sc->mtx);
	if (sc->dp_up != 0) {
		mtx_unlock(&sc->mtx);
		return (EALREADY);
	}

	/*
	 * A ring may be published smaller than the buffers behind it and never larger. Every ring's
	 * buffers, scatter list and info blocks are OCTEP_DP_OQ_DESCS long whatever dp.oq_rsize
	 * says, and the service walks, re-poisons and resynchronises by dp.oq_rsize: a ring
	 * published at 1,024 over 512 buffers had this driver writing its poison over the kernel's
	 * own memory, and the block writing packets through whatever lay after the scatter list.
	 * Measured, on a throwaway build, as two panics, 153 and 279 seconds after booting, with
	 * 0xa5a5a5a5a5a5a5a5 in the registers.
	 *
	 * And in whole fetches: the block takes scatter-list entries sixteen at a time, and what it
	 * does with a fetch that straddles the end of the list has not been measured.
	 */
	if (sc->dp_oq_rsize < OCTEP_DP_OQ_FETCH || sc->dp_oq_rsize > OCTEP_DP_OQ_DESCS ||
	    sc->dp_oq_rsize % OCTEP_DP_OQ_FETCH != 0) {
		device_printf(sc->dev, "dp: a ring of %u entries cannot be published over %u "
		    "buffers; dp.oq_rsize must be %u to %u in steps of %u\n", sc->dp_oq_rsize,
		    OCTEP_DP_OQ_DESCS, OCTEP_DP_OQ_FETCH, OCTEP_DP_OQ_DESCS, OCTEP_DP_OQ_FETCH);
		mtx_unlock(&sc->mtx);
		return (EINVAL);
	}

	/*
	 * And a ring this driver has somewhere to keep. Every per-ring word - the busy flag, the
	 * read index, the note, the credit carried - is an array indexed by the ring's own number.
	 */
	if (sc->dp_ring > OCTEP_DP_SIBLINGS_MAX) {
		device_printf(sc->dev, "dp: ring %u is beyond the %u this driver keeps state for\n",
		    sc->dp_ring, OCTEP_DP_SIBLINGS_MAX + 1);
		mtx_unlock(&sc->mtx);
		return (EINVAL);
	}
	for (i = 0; i <= OCTEP_DP_SIBLINGS_MAX; i++) {
		sc->dp_oq_seen[i] = ticks;
		sc->dp_oq_carry[i] = 0;
		sc->dp_oq_ahead[i] = 0;
	}

	octep_sdp_read_rinfo(sc, 0);
	if (sc->sdp_trs == 0 || sc->dp_ring >= sc->sdp_rings_mappable) {
		device_printf(sc->dev, "dp: ring %u is not available\n", sc->dp_ring);
		mtx_unlock(&sc->mtx);
		return (ENXIO);
	}

	/*
	 * The coprocessor has to know a host exists before its side of SDP means anything, and that is
	 * what the EP-mode handshake does. But this is a WARNING and not a refusal, and an earlier
	 * version of it being a refusal was simply a bug of mine: the vendor's order is
	 * host-programs-rings THEN fast-path-starts, and the target only sets its started-port bit
	 * when the fast path opens the port - so a zero here is the normal state at exactly the moment
	 * the rings are wanted. Worse, after a module reload the host cannot tell a completed handshake
	 * from an absent one, because the target zeroes the register when it finishes. Saying so is
	 * honest; refusing is not.
	 */
	v = bus_read_8(sc->bar0, OCTEP_SLI_EPF_SCRATCH);
	if (sc->sdp_hs_state != OCTEP_HS_DONE && v == 0)
		device_printf(sc->dev, "dp: this instance has not seen the EP-mode handshake and the "
		    "target reports no started port - normal before the fast path runs, and "
		    "unknowable after a reload, so carrying on\n");

	err = octep_dma_alloc(sc, &sc->dp_iq,
	    (bus_size_t)OCTEP_DP_IQ_DESCS * OCTEP_DP_INSTR_SIZE, PAGE_SIZE, "dp iq");
	if (err != 0)
		goto fail;
	err = octep_dma_alloc(sc, &sc->dp_slist,
	    (bus_size_t)OCTEP_DP_OQ_DESCS * OCTEP_DP_SLIST_ENTRY, PAGE_SIZE, "dp slist");
	if (err != 0)
		goto fail;
	err = octep_dma_alloc(sc, &sc->dp_bufs,
	    (bus_size_t)OCTEP_DP_OQ_DESCS * OCTEP_DP_BUF_STRIDE, OCTEP_DP_BUF_ALIGN,
	    "dp buffers");
	if (err != 0)
		goto fail;
	err = octep_dma_alloc(sc, &sc->dp_info,
	    (bus_size_t)OCTEP_DP_OQ_DESCS * OCTEP_DP_OQ_INFO_SIZE, 128, "dp oq info");
	if (err != 0)
		goto fail;
	err = octep_dma_alloc(sc, &sc->dp_txbuf, PAGE_SIZE, PAGE_SIZE, "dp txbuf");
	if (err != 0)
		goto fail;
	/*
	 * One transmit buffer per instruction slot, and the reason is a defect rather than a
	 * preference.
	 *
	 * An instruction carries a physical address; the coprocessor reads that memory itself, when
	 * it gets to it. Every interface used to copy its frame into the same single buffer, post an
	 * instruction pointing at it, and release the mutex - so the next frame overwrote the
	 * previous one while the coprocessor may not have read it yet. At 525,000 packets a second
	 * that window is 1.9 microseconds wide, and a frame posted for one front port could be read
	 * back as a different port's frame, tag and all.
	 *
	 * It was invisible for as long as everything measured sent identical frames. It became
	 * visible the moment two ports were busy at once: a ping on the WAN lost 29% of its echoes
	 * while another port was loaded at 50,000 packets a second, with nothing dropped anywhere
	 * that any counter could see - because those echoes were not dropped. They were overwritten
	 * before the coprocessor read them, and what went out in their place was the other port's
	 * traffic.
	 *
	 * With a buffer per slot a frame is only at risk once the ring has wrapped all the way
	 * round, which is 256 frames rather than one.
	 */
	err = octep_dma_alloc(sc, &sc->dp_txbufs,
	    (bus_size_t)OCTEP_DP_IQ_DESCS * OCTEP_DP_TXBUF_STRIDE, OCTEP_DP_BUF_ALIGN,
	    "dp tx buffers");
	if (err != 0)
		goto fail;

	octep_dp_fill_slist(sc);

	octep_dp_reset_ring(sc);
	octep_dp_policy(sc);

	/* Input side. BADDR only while idle. */
	err = octep_dp_wait_idle(sc, OCTEP_SDP_R_IN_CONTROL, OCTEP_R_IN_CTL_IDLE, "input");
	if (err != 0)
		goto fail;
	octep_dp_wr(sc, OCTEP_SDP_R_IN_INSTR_BADDR, sc->dp_iq.paddr);
	octep_dp_wr(sc, OCTEP_SDP_R_IN_INSTR_RSIZE, OCTEP_DP_IQ_DESCS);
	/* Keep the input interrupt quiet: the threshold is the maximum the field holds. */
	octep_dp_wr(sc, OCTEP_SDP_R_IN_INT_LEVELS, 0xffffffffULL);

	/* Output side. */
	err = octep_dp_wait_idle(sc, OCTEP_SDP_R_OUT_CONTROL, OCTEP_R_OUT_CTL_IDLE, "output");
	if (err != 0)
		goto fail;
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_SLIST_BADDR, sc->dp_slist.paddr);
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_SLIST_RSIZE, sc->dp_oq_rsize);

	/*
	 * BSIZE and ISIZE share the low 23 bits; ISIZE stays zero in buffer-pointer-only mode.
	 *
	 * The ordering and snoop attributes are set in the same word, the way the vendor's
	 * cn83xx_pf_setup_global_oq_reg does it: clear IMODE and all nine _P/_I/_D bits, then set
	 * ES_P alone. Until now this driver defined those bits and never wrote them, so ES_P was
	 * left at whatever reset had put there - and ES_P is the endian swap on the packet-data
	 * path, which is the side the target writes.
	 */
	v = octep_dp_rd(sc, OCTEP_SDP_R_OUT_CONTROL);
	v &= ~(OCTEP_R_OUT_CTL_SIZE_MASK | OCTEP_R_OUT_CTL_ATTR_MASK);
	v |= OCTEP_R_OUT_CTL_ES_P;
	v |= (uint64_t)(OCTEP_DP_BUF_SIZE & 0xffff);
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_CONTROL, v);
	if (bootverbose)
		device_printf(sc->dev, "dp: out_control 0x%jx\n",
		    (uintmax_t)octep_dp_rd(sc, OCTEP_SDP_R_OUT_CONTROL));

	sc->dp_time_threshold = octep_dp_oq_ticks(sc, OCTEP_DP_OQ_INTR_TIME);
	if (sc->dp_time_threshold == 0)
		device_printf(sc->dev, "dp: the target's tick rate is unknown, so the output "
		    "interrupt time threshold is 0 - harmless while nothing uses interrupts. It "
		    "arrives with the handshake and a module reload loses it; set "
		    "dev.%s.%d.sdp.coproc_ticks_per_us to restore it\n",
		    device_get_name(sc->dev), device_get_unit(sc->dev));
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_INT_LEVELS,
	    ((uint64_t)sc->dp_time_threshold << 32) | OCTEP_DP_OQ_INTR_PKT);

	/*
	 * Enable. The input doorbell is drained first and waited on - a large write there is how the
	 * vendor clears it, and it reads back zero once the hardware has taken it.
	 */
	octep_dp_wr(sc, OCTEP_SDP_R_IN_INSTR_DBELL, 0xffffffffULL);
	{
		int i;

		/*
		 * Only the low 32 bits are the outstanding count. This used to compare the whole
		 * register against zero, and on this board it reads 0x98000000000 with the count
		 * already clear - so the condition was never true, the loop always ran out its
		 * tries, and the wait reported nothing either way. Same shape as the output
		 * doorbell, whose high half is a byte offset and not a credit.
		 */
		for (i = 0; i < OCTEP_DP_IDLE_TRIES; i++) {
			if ((octep_dp_rd(sc, OCTEP_SDP_R_IN_INSTR_DBELL) &
			    0xffffffffULL) == 0)
				break;
			DELAY(10);
		}
	}
	octep_dp_wr(sc, OCTEP_SDP_R_IN_ENABLE,
	    octep_dp_rd(sc, OCTEP_SDP_R_IN_ENABLE) | 1ULL);

	/*
	 * The doorbell then the enable, which is the whole of the vendor's
	 * cn83xx_enable_output_queue - see octep_dp_oq_enable.
	 */
	octep_dp_oq_enable(sc, sc->dp_ring);

	/* Now grant the output ring the buffers it may write into: one ring's worth above the mark. */
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_SLIST_DBELL, octep_dp_oq_first_grant(sc));

	sc->dp_sa_if = -1;
	sc->dp_iq_prod = 0;
	sc->dp_tx_posted = 0;

	/*
	 * The other transmit rings, after ring 0 is up: as many of the port's rings 1 to 7 as
	 * hw.octep.txq asks for (all seven unless it says otherwise) and there are CPUs to use them.
	 * One that will not come up ends the list there, and what came up is used.
	 */
	{
		uint32_t want = sc->dp_txq_want, i;

		if (want > OCTEP_DP_TXQ_MAX)
			want = OCTEP_DP_TXQ_MAX;
		if (want > (uint32_t)mp_ncpus)
			want = (uint32_t)mp_ncpus;
		sc->dp_ntxq = 0;
		for (i = 0; i < want; i++) {
			struct octep_dp_txq *q = &sc->dp_txq[i];

			q->ring = sc->dp_ring + 1 + i;
			if (!mtx_initialized(&q->mtx))
				mtx_init(&q->mtx, "octep txq", NULL, MTX_DEF);
			if (octep_dp_txq_up(sc, q) != 0) {
				octep_dp_txq_down(sc, q);
				break;
			}
			sc->dp_ntxq = i + 1;
		}
		device_printf(sc->dev, "dp: %u transmit ring%s besides ring %u\n", sc->dp_ntxq,
		    sc->dp_ntxq == 1 ? "" : "s", sc->dp_ring);
	}
	sc->dp_rx_seen = 0;
	if (sc->dp_pkind == 0)
		sc->dp_pkind = OCTEP_DP_PKIND;
	if (!sc->dp_meta_mode_set) {
		sc->dp_meta_mode = OCTEP_META_MODE_VENDOR;
		sc->dp_meta_mode_set = 1;
	}
	sc->dp_up = 1;
	sc->dp_closing_off = 0;
	if (sc->dp_rxwd_on == 0) {
		if (sc->dp_rxwd_ticks == 0)
			sc->dp_rxwd_ticks = OCTEP_DP_RXWD_TICKS;
		callout_init(&sc->dp_rxwd, 1);
		sc->dp_rxwd_on = 1;
		callout_reset(&sc->dp_rxwd, sc->dp_rxwd_ticks, octep_dp_rxwd, sc);
	}
	/* The heartbeat: its first beat a second from now, under this lock (octep_hb_tick). */
	if (sc->dp_hb_running == 0) {
		callout_init_mtx(&sc->dp_hb_co, &sc->mtx, 0);
		sc->dp_hb_running = 1;
		sc->dp_hb_state = OCTEP_HB_OFF;
		callout_reset(&sc->dp_hb_co, hz, octep_hb_tick, sc);
	}
	if (sc->dp_siblings > OCTEP_DP_SIBLINGS_MAX)
		sc->dp_siblings = OCTEP_DP_SIBLINGS_MAX;
	/*
	 * The siblings normally follow the datapath ring, which is what the mainline driver does and
	 * what every measurement here used. dp.sib_base exists because of one thing the appliance's
	 * own firmware does: under SFOS, rings 0-7 are programmed and idle while rings 8-15 carry
	 * every packet. Moving the datapath ring itself to 8 does not work - the fast path then reads
	 * host-posted frames as wire ingress and drops them at FPCNTR_FROM_WIRE_DROP_IG_ERR, so the
	 * input ring belongs on 0. This knob arms the output siblings elsewhere while the input ring
	 * stays where it works.
	 */
	for (i = 0; i < sc->dp_siblings; i++) {
		uint32_t r = (sc->dp_sib_base != 0 ? sc->dp_sib_base : sc->dp_ring + 1) + i;

		if (r == sc->dp_ring) {
			device_printf(sc->dev,
			    "dp: sibling %u would be the datapath ring itself - stopping at %u "
			    "siblings\n", r, i);
			sc->dp_siblings = i;
			break;
		}

		if (r >= sc->sdp_rings_mappable || r > OCTEP_DP_SIBLINGS_MAX) {
			device_printf(sc->dev,
			    "dp: ring %u is beyond the %u that fit in BAR0 - stopping at %u "
			    "siblings\n", r, sc->sdp_rings_mappable, i);
			sc->dp_siblings = i;
			break;
		}
		if (octep_dp_arm_sibling(sc, &sc->dp_sib[i], r) != 0) {
			sc->dp_siblings = i;
			break;
		}
	}
	/*
	 * The inspector's ring (discovery 4). With a virtual function published, the first ring is
	 * that function's, and the fast path sends it every frame of a connection whose verdict is
	 * "send to inspection". Armed from here as one more receive ring, so those frames can be read.
	 */
	if (sc->hs_nvfs != 0 && sc->dp_siblings < OCTEP_DP_SIBLINGS_MAX &&
	    sc->sdp_srn != sc->dp_ring &&
	    octep_dp_arm_sibling(sc, &sc->dp_sib[sc->dp_siblings], sc->sdp_srn) == 0) {
		struct octep_dp_txq *q = &sc->dp_ips_txq;

		sc->dp_siblings++;
		/* And its input side, for what the inspector hands back. */
		q->ring = sc->sdp_srn;
		if (!mtx_initialized(&q->mtx))
			mtx_init(&q->mtx, "octep ips txq", NULL, MTX_DEF);
		if (octep_dp_txq_up(sc, q) != 0)
			octep_dp_txq_down(sc, q);
		if (sc->dp_ips_pkind == 0)
			sc->dp_ips_pkind = OCTEP_DP_IPS_PKIND;
		sc->dp_ips_ring = sc->sdp_srn;
		sc->dp_ips_on = 1;
		device_printf(sc->dev, "dp: ring %u, the inspector's function's, armed to receive%s\n",
		    sc->sdp_srn, q->up ? " and to hand back" : "; its input side would not come up");
	}

	device_printf(sc->dev,
	    "dp: ring %u up - iq %u x %u B at 0x%jx, oq %u x %u B every %u B at 0x%jx, "
	    "slist at 0x%jx, oq time threshold %u\n",
	    sc->dp_ring, OCTEP_DP_IQ_DESCS, OCTEP_DP_INSTR_SIZE,
	    (uintmax_t)sc->dp_iq.paddr, OCTEP_DP_OQ_DESCS, OCTEP_DP_BUF_SIZE,
	    OCTEP_DP_BUF_STRIDE, (uintmax_t)sc->dp_bufs.paddr,
	    (uintmax_t)sc->dp_slist.paddr, sc->dp_time_threshold);
	/*
	 * The stride is what decides whether every published buffer address is 64-byte aligned, and
	 * a misaligned one is the kind of thing the block refuses without latching an error - so say
	 * outright whether the ring came out aligned rather than leaving it to be worked out.
	 */
	device_printf(sc->dev, "dp: buffer addresses are %s\n",
	    ((sc->dp_bufs.paddr | OCTEP_DP_BUF_STRIDE) & (OCTEP_DP_BUF_ALIGN - 1)) == 0 ?
	    "64-byte aligned, every one" : "NOT all 64-byte aligned");
	mtx_unlock(&sc->mtx);
	/* The inspector's interface, made outside the lock because making an interface may sleep. */
	if (sc->dp_ips_on)
		octep_ips_if_create(sc);
	return (0);

fail:
	octep_dp_reset_ring(sc);
	octep_dp_free_siblings(sc);
	octep_dma_free(&sc->dp_txbufs);
	octep_dma_free(&sc->dp_txbuf);
	octep_dma_free(&sc->dp_info);
	octep_dma_free(&sc->dp_bufs);
	octep_dma_free(&sc->dp_slist);
	octep_dma_free(&sc->dp_iq);
	mtx_unlock(&sc->mtx);
	return (err);
}

/*
 * Take it down. The enables go first and the base addresses are cleared before the memory behind them
 * is released, so there is never a window where the hardware holds a pointer to freed pages.
 */
void
octep_dp_stop(struct octep_softc *sc)
{

	/* Take the interfaces and the vectors away before the rings they point at go. */
	/*
	 * The handlers first, then the interfaces. octep_dp_if_detach_all() quiesces the receive path
	 * itself, so this order is no longer what makes the detach safe - but there is no reason to
	 * leave eight interrupt handlers hooked across it, and the order this was in is how the window
	 * they could drive got there.
	 */
	/* The FINs and RSTs held for pf (issue #679): none more, the held ones up while the interfaces are */
	mtx_lock(&sc->mtx);
	sc->dp_closing_off = 1;
	mtx_unlock(&sc->mtx);
	taskqueue_drain(taskqueue_thread, &sc->dp_closing_task);
	mtx_lock(&sc->mtx);
	mbufq_drain(&sc->dp_closing_q);
	mtx_unlock(&sc->mtx);
	octep_dp_msix_teardown(sc);
	octep_dp_if_detach_all(sc);
	octep_ipsec_detach(sc);
	if (sc->dp_rxwd_on != 0) {
		sc->dp_rxwd_on = 0;
		callout_drain(&sc->dp_rxwd);
	}
	/* The heartbeat stops asking: cleared under the lock, so a beat under way does not re-arm. */
	mtx_lock(&sc->mtx);
	if (sc->dp_hb_running != 0) {
		sc->dp_hb_running = 0;
		sc->dp_hb_state = OCTEP_HB_OFF;
		callout_stop(&sc->dp_hb_co);
		mtx_unlock(&sc->mtx);
		callout_drain(&sc->dp_hb_co);
	} else
		mtx_unlock(&sc->mtx);
	/* Nothing receives any more, so nothing kicks: the kicked task's queue can go. */
	if (sc->dp_fast_tq != NULL) {
		struct taskqueue *tq = sc->dp_fast_tq;

		sc->dp_fast_tq = NULL;
		taskqueue_drain(tq, &sc->dp_fast_task);
		taskqueue_free(tq);
	}
	mtx_lock(&sc->mtx);
	if (sc->dp_up == 0) {
		mtx_unlock(&sc->mtx);
		return;
	}
	octep_dp_reset_ring(sc);
	sc->dp_up = 0;
	mtx_unlock(&sc->mtx);

	/*
	 * The other transmit rings: each lock taken and let go first, so a transmit that read dp_up
	 * before it went to zero has finished with its ring before the ring goes.
	 */
	for (uint32_t i = 0; i < sc->dp_ntxq; i++) {
		mtx_lock(&sc->dp_txq[i].mtx);
		mtx_unlock(&sc->dp_txq[i].mtx);
		octep_dp_txq_down(sc, &sc->dp_txq[i]);
	}
	sc->dp_ntxq = 0;
	if (sc->dp_ips_on) {
		sc->dp_ips_on = 0;
		mtx_lock(&sc->dp_ips_txq.mtx);
		mtx_unlock(&sc->dp_ips_txq.mtx);
		octep_dp_txq_down(sc, &sc->dp_ips_txq);
	}

	octep_dp_free_siblings(sc);
	octep_dma_free(&sc->dp_txbufs);
	octep_dma_free(&sc->dp_txbuf);
	octep_dma_free(&sc->dp_info);
	octep_dma_free(&sc->dp_bufs);
	octep_dma_free(&sc->dp_slist);
	octep_dma_free(&sc->dp_iq);
	device_printf(sc->dev, "dp: ring %u down\n", sc->dp_ring);
}


/* ---------------------------------------------------------------- transmit */

/*
 * Build one 64-byte instruction in place. dptr, ih3 and pki_ih3 go in host order because the
 * hardware swaps the instruction fetch - that is what ESR in R_IN_CONTROL turns on - while rptr and
 * irh are written byte-swapped, exactly as the vendor's own NIC path does, with the comment that it
 * saves the far side a swap.
 */
static void
octep_dp_build_instr_pk(struct octep_softc *sc, void *ring, uint32_t slot, bus_addr_t dptr,
    uint32_t datalen, uint32_t pkind)
{
	char *e = (char *)ring + ((size_t)slot * OCTEP_DP_INSTR_SIZE);
	uint64_t ih3, pki_ih3, irh;

	bzero(e, OCTEP_DP_INSTR_SIZE);

	ih3 = OCTEP_IH3(datalen + OCTEP_INSTR_FSZ, pkind, OCTEP_INSTR_FSZ);
	pki_ih3 = OCTEP_PKI_IH3(OCTEP_ORDERED_TAG, 1, OCTEP_INSTR_SL, OCTEP_INSTR_PM, 1);
	irh = OCTEP_IRH(OCTEP_IRH_CKSUM_OFF, 0, sc->dp_dport, OCTEP_OCT_NW_PKT_OP);

	*(uint64_t *)(e + OCTEP_INSTR_DPTR) = (uint64_t)dptr;
	*(uint64_t *)(e + OCTEP_INSTR_IH3) = ih3;
	*(uint64_t *)(e + OCTEP_INSTR_PKI_IH3) = pki_ih3;
	*(uint64_t *)(e + OCTEP_INSTR_RPTR) = bswap64(0);
	*(uint64_t *)(e + OCTEP_INSTR_IRH) = bswap64(irh);
}

/* The PF's own pkind, which every ring but the inspector's posts with. */
static void
octep_dp_build_instr_in(struct octep_softc *sc, void *ring, uint32_t slot, bus_addr_t dptr,
    uint32_t datalen)
{

	octep_dp_build_instr_pk(sc, ring, slot, dptr, datalen, sc->dp_pkind);
}

/* Ring 0's, which the instruments and the control messages post on. */
static void
octep_dp_build_instr(struct octep_softc *sc, uint32_t slot, bus_addr_t dptr, uint32_t datalen)
{

	octep_dp_build_instr_in(sc, sc->dp_iq.vaddr, slot, dptr, datalen);
}

/*
 * Bring transmit ring q->ring up, input side only - its output side is a receive sibling and is
 * left alone. The same registers and the same order as ring 0's in octep_dp_start: everything
 * cleared with the enable off, the policy bits, BADDR only while idle, the doorbell drained, then
 * the enable. Called from octep_dp_start with sc->mtx held, as ring 0's set-up is.
 */
static int
octep_dp_txq_up(struct octep_softc *sc, struct octep_dp_txq *q)
{
	uint32_t r = q->ring;
	uint64_t v;
	int err, i;

	err = octep_dma_alloc(sc, &q->iq, (bus_size_t)OCTEP_DP_IQ_DESCS * OCTEP_DP_INSTR_SIZE,
	    PAGE_SIZE, "dp txq iq");
	if (err == 0)
		err = octep_dma_alloc(sc, &q->bufs,
		    (bus_size_t)OCTEP_DP_IQ_DESCS * OCTEP_DP_TXBUF_STRIDE, OCTEP_DP_BUF_ALIGN,
		    "dp txq buffers");
	if (err != 0)
		return (err);

	octep_dp_ring_wr(sc, r, OCTEP_SDP_R_IN_ENABLE, 0);
	octep_dp_ring_wr(sc, r, OCTEP_SDP_R_IN_INSTR_BADDR, 0);
	octep_dp_ring_wr(sc, r, OCTEP_SDP_R_IN_INSTR_RSIZE, 0);
	octep_dp_ring_wr(sc, r, OCTEP_SDP_R_IN_INSTR_DBELL, 0xffffffffULL);
	octep_dp_ring_wr(sc, r, OCTEP_SDP_R_IN_CNTS, 0);
	octep_dp_ring_wr(sc, r, OCTEP_SDP_R_IN_INT_LEVELS, 0);
	octep_dp_ring_wr(sc, r, OCTEP_SDP_R_IN_PKT_CNT, 0);
	octep_dp_ring_wr(sc, r, OCTEP_SDP_R_IN_BYTE_CNT, 0);
	v = octep_dp_ring_rd(sc, r, OCTEP_SDP_R_IN_CONTROL);
	v |= OCTEP_R_IN_CTL_RDSIZE | OCTEP_R_IN_CTL_IS_64B | OCTEP_R_IN_CTL_ESR;
	octep_dp_ring_wr(sc, r, OCTEP_SDP_R_IN_CONTROL, v);

	for (i = 0; i < OCTEP_DP_IDLE_TRIES; i++) {
		if ((octep_dp_ring_rd(sc, r, OCTEP_SDP_R_IN_CONTROL) & OCTEP_R_IN_CTL_IDLE) != 0)
			break;
		DELAY(10);
	}
	if (i == OCTEP_DP_IDLE_TRIES) {
		device_printf(sc->dev, "dp: transmit ring %u never went idle\n", r);
		return (EBUSY);
	}
	octep_dp_ring_wr(sc, r, OCTEP_SDP_R_IN_INSTR_BADDR, q->iq.paddr);
	octep_dp_ring_wr(sc, r, OCTEP_SDP_R_IN_INSTR_RSIZE, OCTEP_DP_IQ_DESCS);
	octep_dp_ring_wr(sc, r, OCTEP_SDP_R_IN_INT_LEVELS, 0xffffffffULL);
	octep_dp_ring_wr(sc, r, OCTEP_SDP_R_IN_INSTR_DBELL, 0xffffffffULL);
	for (i = 0; i < OCTEP_DP_IDLE_TRIES; i++) {
		if ((octep_dp_ring_rd(sc, r, OCTEP_SDP_R_IN_INSTR_DBELL) & 0xffffffffULL) == 0)
			break;
		DELAY(10);
	}
	octep_dp_ring_wr(sc, r, OCTEP_SDP_R_IN_ENABLE,
	    octep_dp_ring_rd(sc, r, OCTEP_SDP_R_IN_ENABLE) | 1ULL);
	q->prod = 0;
	q->up = 1;
	return (0);
}

/* And down: the enable off, the ring cleared, the memory given back. */
static void
octep_dp_txq_down(struct octep_softc *sc, struct octep_dp_txq *q)
{

	if (q->up) {
		octep_dp_ring_wr(sc, q->ring, OCTEP_SDP_R_IN_ENABLE, 0);
		octep_dp_ring_wr(sc, q->ring, OCTEP_SDP_R_IN_INSTR_BADDR, 0);
		octep_dp_ring_wr(sc, q->ring, OCTEP_SDP_R_IN_INSTR_RSIZE, 0);
		q->up = 0;
	}
	octep_dma_free(&q->bufs);
	octep_dma_free(&q->iq);
}

/*
 * Build and post one CONTROL message - an instruction carrying OCT_NW_CMD_OP and a single 64-bit
 * command word, rather than a packet. See octep.h for where the format comes from and for the
 * counter that says the vendor sends 748 of these where this driver has sent none.
 */
static int
octep_dp_post_cmd(struct octep_softc *sc)
{
	char *e;
	uint64_t word, ih3, irh;
	uint32_t fsz;

	mtx_lock(&sc->mtx);
	if (sc->dp_up == 0) {
		mtx_unlock(&sc->mtx);
		return (ENXIO);
	}

	fsz = (sc->dp_cmd_fsz == OCTEP_CMD_FSZ_VENDOR) ?
	    OCTEP_CMD_FSZ_VENDOR : OCTEP_CMD_FSZ_LIKE_DATA;

	word = OCTEP_OCTNET_CMD(sc->dp_cmd, sc->dp_cmd_more, sc->dp_cmd_p1,
	    sc->dp_cmd_p2, sc->dp_cmd_p3);

	/* The command word is the whole payload, in host order - the vendor does not swap it. */
	memset(sc->dp_txbuf.vaddr, 0, 8);
	*(uint64_t *)sc->dp_txbuf.vaddr = word;
	bus_dmamap_sync(sc->dp_txbuf.tag, sc->dp_txbuf.map, BUS_DMASYNC_PREWRITE);

	e = (char *)sc->dp_iq.vaddr + ((size_t)sc->dp_iq_prod * OCTEP_DP_INSTR_SIZE);
	bzero(e, OCTEP_DP_INSTR_SIZE);

	ih3 = OCTEP_IH3(8 + fsz, sc->dp_pkind, fsz);
	irh = OCTEP_IRH(0, 0, 0, OCTEP_OCT_NW_CMD_OP);

	*(uint64_t *)(e + OCTEP_INSTR_DPTR) = (uint64_t)sc->dp_txbuf.paddr;
	*(uint64_t *)(e + OCTEP_INSTR_IH3) = ih3;
	if (fsz == OCTEP_CMD_FSZ_LIKE_DATA)
		*(uint64_t *)(e + OCTEP_INSTR_PKI_IH3) =
		    OCTEP_PKI_IH3(OCTEP_ORDERED_TAG, 1, OCTEP_INSTR_SL, OCTEP_INSTR_PM, 1);
	*(uint64_t *)(e + OCTEP_INSTR_RPTR) = bswap64(0);
	*(uint64_t *)(e + OCTEP_INSTR_IRH) = bswap64(irh);

	bus_dmamap_sync(sc->dp_iq.tag, sc->dp_iq.map, BUS_DMASYNC_PREWRITE);

	sc->dp_iq_prod = (sc->dp_iq_prod + 1) % OCTEP_DP_IQ_DESCS;
	sc->dp_tx_posted++;
	octep_dp_wr(sc, OCTEP_SDP_R_IN_INSTR_DBELL, 1);
	mtx_unlock(&sc->mtx);

	device_printf(sc->dev, "dp: posted control word 0x%016jx  cmd %u p1 %u p2 %u fsz %u\n",
	    (uintmax_t)word, sc->dp_cmd, sc->dp_cmd_p1, sc->dp_cmd_p2, fsz);
	return (0);
}

/*
 * Post a well-formed CONTROL MESSAGE: the 66-byte private header this driver already writes, with
 * the port tag forced to 254 because that is the tag the fast path reads as control, then the
 * four-byte message header and its entries. See octep.h for where each field comes from.
 */
static int
octep_dp_post_cmsg(struct octep_softc *sc)
{
	uint8_t *d;
	uint32_t i, n, body;

	mtx_lock(&sc->mtx);
	if (sc->dp_up == 0) {
		mtx_unlock(&sc->mtx);
		return (ENXIO);
	}

	n = sc->dp_cmsg_count;
	if (n > OCTEP_CMSG_MAX_ENTRIES)
		n = OCTEP_CMSG_MAX_ENTRIES;

	d = (uint8_t *)sc->dp_txbuf.vaddr;
	memset(d, 0, OCTEP_TOTAL_TAG_LEN + OCTEP_CMSG_ETH_HLEN + 8 +
	    OCTEP_CMSG_MAX_ENTRIES * 4);

	/* the private header: the control tag, then the metadata with its type byte */
	d[0] = (uint8_t)(OCTEP_CMSG_PORT_TAG >> 8);
	d[1] = (uint8_t)(OCTEP_CMSG_PORT_TAG & 0xff);
	d[OCTEP_PPORT_HLEN] = OCTEP_META_VENDOR_BYTE0;

	d += OCTEP_TOTAL_TAG_LEN;

	/*
	 * A control message is an Ethernet frame. The destination is the same address a data frame
	 * is pointed at, because nothing in the fast path's control branch looks at it - what it
	 * looks at is the EtherType, which is the whole point of these fourteen bytes.
	 */
	if (sc->dp_dst_mac[0] == 0 && sc->dp_dst_mac[1] == 0 && sc->dp_dst_mac[2] == 0 &&
	    sc->dp_dst_mac[3] == 0 && sc->dp_dst_mac[4] == 0 && sc->dp_dst_mac[5] == 0)
		memset(d, 0xff, 6);
	else
		memcpy(d, sc->dp_dst_mac, 6);
	d[6] = 0x02;				/* source: locally administered */
	d[11] = 0x01;
	d[12] = (uint8_t)(OCTEP_CMSG_ETHERTYPE >> 8);
	d[13] = (uint8_t)(OCTEP_CMSG_ETHERTYPE & 0xff);

	d += OCTEP_CMSG_ETH_HLEN;

	/* +0 and +1 stay zero */
	d[2] = (uint8_t)sc->dp_cmsg_type;
	d[3] = OCTEP_CMSG_VERSION;
	*(uint32_t *)(d + 4) = n;
	for (i = 0; i < n; i++) {
		*(uint16_t *)(d + 8 + i * 4) = (uint16_t)sc->dp_cmsg_port;
		*(uint16_t *)(d + 10 + i * 4) = (uint16_t)sc->dp_cmsg_value;
	}

	/*
	 * The body is a FIXED size, and that is not an accident. usfp_cmsg_alloc is called with
	 * len 0x44 whatever the count is, and it puts len + 4, so the vendor always sends 72
	 * bytes and only `count` says how many of the sixteen entry slots mean anything. A
	 * short message was tried first and the fast path did not count it at all - not even as
	 * a dropped control message - so the length is part of what makes it recognisable.
	 */
	body = OCTEP_CMSG_ETH_HLEN + 8 + OCTEP_CMSG_MAX_ENTRIES * 4;
	bus_dmamap_sync(sc->dp_txbuf.tag, sc->dp_txbuf.map, BUS_DMASYNC_PREWRITE);

	octep_dp_build_instr(sc, sc->dp_iq_prod, sc->dp_txbuf.paddr,
	    body + OCTEP_TOTAL_TAG_LEN);
	bus_dmamap_sync(sc->dp_iq.tag, sc->dp_iq.map, BUS_DMASYNC_PREWRITE);

	sc->dp_iq_prod = (sc->dp_iq_prod + 1) % OCTEP_DP_IQ_DESCS;
	sc->dp_tx_posted++;
	octep_dp_wr(sc, OCTEP_SDP_R_IN_INSTR_DBELL, 1);
	mtx_unlock(&sc->mtx);

	device_printf(sc->dev, "dp: posted a control message, type %u version %u, %u entr%s, "
	    "%u body bytes on tag %u\n", sc->dp_cmsg_type, OCTEP_CMSG_VERSION, n,
	    (n == 1) ? "y" : "ies", body, OCTEP_CMSG_PORT_TAG);
	return (0);
}

/*
 * THE HEARTBEAT WITH THE FAST PATH (discovery 11 of 2026-10-22, docs/findings/discoveries-2026-10-22.md).
 *
 * The vendor's host sends the fast path CMSG_TYPE_WATCHDOG_FROM_HOST every five seconds with a
 * sequence number, and the fast path answers with CMSG_TYPE_WATCHDOG_TO_HOST carrying it back (its
 * cmsg.c, watchdog_process); with no answer for 60 seconds it warns, and at 120 it takes the fast
 * path as dead and panics the box. Measured here before anything was built on it: the second XGS
 * 3300's fast path answered a type 8 posted through dp.cmsg_post within two seconds, its sequence
 * carried back in the four bytes after the header.
 *
 * So the host asks every heartbeat.period_ms and knows how long ago the fast path last answered:
 * late after heartbeat.late_ms without an answer, taken as stopped after heartbeat.dead_ms, each said
 * once in the system log, and the first answer after either said too. This one does nothing to the
 * box at the end of it - it says so - so it says so sooner than the vendor's sixty and hundred and
 * twenty seconds. A heartbeat is sent on ring 0 under sc->mtx, from that slot's own buffer, as a
 * frame of the interfaces is (octep_dp_if_transmit): a beat never waits on the fast path.
 *
 * It begins only once the fast path runs - the RPC answered (rpc_ready) and an interface made over
 * it (dp_nif) - and never from the moment ring 0 is up. Measured on the second XGS 3300 the first
 * time: beats went out from ring 0's first second, twenty seconds before the coprocessor's
 * handshake, and on that boot the fast path sent the host nothing - no answer to any of 135 beats
 * nor to a message posted by hand twelve minutes in, no control message of any type - and the far
 * end of the lab could not be reached through it. The next boot, with this gate: every beat
 * answered and the far end reached. The vendor's host waits five minutes before its first beat.
 * The times are sbinuptime's, not the tick's, so an answer within a tick is still measured.
 */
static const char *const octep_hb_state_name[] = { "off", "waiting", "alive", "late", "dead" };
#define	OCTEP_HB_PERIOD_MIN_MS	100

static int
octep_hb_send(struct octep_softc *sc)
{
	uint8_t *d, *e;
	uint32_t slot;

	mtx_assert(&sc->mtx, MA_OWNED);
	if ((octep_dp_rd(sc, OCTEP_SDP_R_IN_INSTR_DBELL) & 0xffffffffULL) >= OCTEP_DP_IQ_DESCS - 1)
		return (ENOBUFS);
	slot = sc->dp_iq_prod;
	d = (uint8_t *)sc->dp_txbufs.vaddr + (size_t)slot * OCTEP_DP_TXBUF_STRIDE;
	bzero(d, OCTEP_TOTAL_TAG_LEN + OCTEP_MIN_FRAME);
	/* the control tag, and the metadata's md_valid as octep_dp_post_cmsg writes it */
	d[0] = (uint8_t)(OCTEP_CMSG_PORT_TAG >> 8);
	d[1] = (uint8_t)(OCTEP_CMSG_PORT_TAG & 0xff);
	d[OCTEP_PPORT_HLEN] = OCTEP_META_VENDOR_BYTE0;
	/* an Ethernet frame whose EtherType is what the fast path's control branch reads */
	e = d + OCTEP_TOTAL_TAG_LEN;
	memset(e, 0xff, ETHER_ADDR_LEN);
	e[6] = 0x02;
	e[11] = 0x01;
	be16enc(e + 12, OCTEP_CMSG_ETHERTYPE);
	e[ETHER_HDR_LEN + 2] = OCTEP_CMSG_TYPE_HB_FROM_HOST;
	e[ETHER_HDR_LEN + 3] = OCTEP_CMSG_VERSION;
	le32enc(e + ETHER_HDR_LEN + 4, sc->dp_hb_seq);
	bus_dmamap_sync(sc->dp_txbufs.tag, sc->dp_txbufs.map, BUS_DMASYNC_PREWRITE);
	octep_dp_build_instr(sc, slot, sc->dp_txbufs.paddr + (bus_addr_t)slot * OCTEP_DP_TXBUF_STRIDE,
	    OCTEP_TOTAL_TAG_LEN + OCTEP_MIN_FRAME);
	bus_dmamap_sync(sc->dp_iq.tag, sc->dp_iq.map, BUS_DMASYNC_PREWRITE);
	sc->dp_iq_prod = (sc->dp_iq_prod + 1) % OCTEP_DP_IQ_DESCS;
	sc->dp_tx_posted++;
	octep_dp_wr(sc, OCTEP_SDP_R_IN_INSTR_DBELL, 1);
	return (0);
}

/* How long the fast path has gone without answering, and what that makes it. Under sc->mtx. */
static void
octep_hb_judge(struct octep_softc *sc, sbintime_t now)
{
	uint64_t ms;

	mtx_assert(&sc->mtx, MA_OWNED);
	ms = (uint64_t)sbttoms(now - (sc->dp_hb_answer_at != 0 ? sc->dp_hb_answer_at :
	    sc->dp_hb_since));
	if (ms >= sc->dp_hb_dead_ms) {
		if (sc->dp_hb_state != OCTEP_HB_DEAD) {
			sc->dp_hb_state = OCTEP_HB_DEAD;
			sc->dp_hb_deaths++;
			device_printf(sc->dev, "heartbeat: the fast path has not answered for %ju s - "
			    "taken as stopped\n", (uintmax_t)(ms / 1000));
		}
	} else if (ms >= sc->dp_hb_late_ms && (sc->dp_hb_state == OCTEP_HB_ALIVE ||
	    sc->dp_hb_state == OCTEP_HB_WAITING)) {
		sc->dp_hb_state = OCTEP_HB_LATE;
		sc->dp_hb_lates++;
		device_printf(sc->dev, "heartbeat: no answer from the fast path for %ju s\n",
		    (uintmax_t)(ms / 1000));
	}
}

/* The fast path's answer, from octep_dp_cmsg_rx. Under sc->mtx. */
static void
octep_hb_answer(struct octep_softc *sc, uint32_t seq)
{
	sbintime_t now;
	uint64_t us = 0, ms;
	int was;

	mtx_assert(&sc->mtx, MA_OWNED);
	was = sc->dp_hb_state;
	if (was == OCTEP_HB_OFF)
		return;
	now = sbinuptime();
	if (seq == sc->dp_hb_seq && sc->dp_hb_sent_at != 0) {
		us = (uint64_t)sbttous(now - sc->dp_hb_sent_at);
		sc->dp_hb_rtt_us = us;
		if (us > sc->dp_hb_rtt_max_us)
			sc->dp_hb_rtt_max_us = us;
		sc->dp_hb_answered++;
	} else
		sc->dp_hb_unmatched++;
	if (was == OCTEP_HB_LATE || was == OCTEP_HB_DEAD) {
		ms = (uint64_t)sbttoms(now - (sc->dp_hb_answer_at != 0 ? sc->dp_hb_answer_at :
		    sc->dp_hb_since));
		if (was == OCTEP_HB_DEAD)
			sc->dp_hb_revivals++;
		device_printf(sc->dev, "heartbeat: the fast path answers again, after %ju s without "
		    "an answer\n", (uintmax_t)(ms / 1000));
	} else if (was == OCTEP_HB_WAITING)
		device_printf(sc->dev, "heartbeat: the fast path answers, in %ju us\n", (uintmax_t)us);
	sc->dp_hb_answer_at = now;
	sc->dp_hb_state = OCTEP_HB_ALIVE;
}

/* Every heartbeat.period_ms while the ring is up: judge the silence so far, then ask again. */
static void
octep_hb_tick(void *arg)
{
	struct octep_softc *sc = arg;
	sbintime_t now;
	uint32_t ms;

	mtx_assert(&sc->mtx, MA_OWNED);
	if (sc->dp_hb_running == 0)
		return;
	now = sbinuptime();
	/* only once the fast path runs: its RPC answered and an interface made over it */
	if (sc->dp_up == 0 || sc->dp_hb_on == 0 || sc->rpc_ready == 0 || sc->dp_nif == 0)
		sc->dp_hb_state = OCTEP_HB_OFF;
	else {
		if (sc->dp_hb_state == OCTEP_HB_OFF) {
			sc->dp_hb_state = OCTEP_HB_WAITING;
			sc->dp_hb_since = now;
			sc->dp_hb_answer_at = 0;
		}
		octep_hb_judge(sc, now);
		sc->dp_hb_seq++;
		if (octep_hb_send(sc) == 0) {
			sc->dp_hb_sent++;
			sc->dp_hb_sent_at = now;
		} else
			sc->dp_hb_unsent++;
	}
	ms = sc->dp_hb_period_ms < OCTEP_HB_PERIOD_MIN_MS ? OCTEP_HB_PERIOD_MIN_MS :
	    sc->dp_hb_period_ms;
	callout_reset(&sc->dp_hb_co, imax(1, (int)((uint64_t)ms * hz / 1000)), octep_hb_tick, sc);
}

static int
octep_sysctl_hb_state(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	char buf[16];
	int st;

	mtx_lock(&sc->mtx);
	st = sc->dp_hb_state;
	mtx_unlock(&sc->mtx);
	strlcpy(buf, octep_hb_state_name[st >= 0 && st <= OCTEP_HB_DEAD ? st : 0], sizeof(buf));
	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}

static int
octep_sysctl_hb_since(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	uint64_t ms = 0;

	mtx_lock(&sc->mtx);
	if (sc->dp_hb_state != OCTEP_HB_OFF)
		ms = (uint64_t)sbttoms(sbinuptime() - (sc->dp_hb_answer_at != 0 ?
		    sc->dp_hb_answer_at : sc->dp_hb_since));
	mtx_unlock(&sc->mtx);
	return (sysctl_handle_64(oidp, &ms, 0, req));
}

static void
octep_hb_add_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
    struct sysctl_oid_list *top)
{
	struct sysctl_oid *node;
	struct sysctl_oid_list *c;

	sc->dp_hb_on = 1;
	sc->dp_hb_period_ms = 5000;
	sc->dp_hb_late_ms = 15000;
	sc->dp_hb_dead_ms = 30000;
	node = SYSCTL_ADD_NODE(ctx, top, OID_AUTO, "heartbeat", CTLFLAG_RD, NULL,
	    "the heartbeat with the fast path: asked every period_ms, answered with its sequence");
	if (node == NULL)
		return;
	c = SYSCTL_CHILDREN(node);
	SYSCTL_ADD_INT(ctx, c, OID_AUTO, "on", CTLFLAG_RW, &sc->dp_hb_on, 0,
	    "1 (the default) asks the fast path every period_ms; 0 stops asking");
	SYSCTL_ADD_UINT(ctx, c, OID_AUTO, "period_ms", CTLFLAG_RW, &sc->dp_hb_period_ms, 0,
	    "how often the fast path is asked, in milliseconds; 5000, the vendor's period");
	SYSCTL_ADD_UINT(ctx, c, OID_AUTO, "late_ms", CTLFLAG_RW, &sc->dp_hb_late_ms, 0,
	    "no answer for this long is late, and said once in the system log");
	SYSCTL_ADD_UINT(ctx, c, OID_AUTO, "dead_ms", CTLFLAG_RW, &sc->dp_hb_dead_ms, 0,
	    "no answer for this long and the fast path is taken as stopped, said once");
	SYSCTL_ADD_PROC(ctx, c, OID_AUTO, "state", CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    sc, 0, octep_sysctl_hb_state, "A",
	    "off, waiting (no answer yet), alive, late or dead");
	SYSCTL_ADD_PROC(ctx, c, OID_AUTO, "since_answer_ms", CTLTYPE_U64 | CTLFLAG_RD |
	    CTLFLAG_MPSAFE, sc, 0, octep_sysctl_hb_since, "QU",
	    "milliseconds since the fast path last answered, or since the heartbeat began");
	SYSCTL_ADD_UINT(ctx, c, OID_AUTO, "seq", CTLFLAG_RD, &sc->dp_hb_seq, 0,
	    "the last sequence sent");
	SYSCTL_ADD_U64(ctx, c, OID_AUTO, "sent", CTLFLAG_RD, &sc->dp_hb_sent, 0, "beats sent");
	SYSCTL_ADD_U64(ctx, c, OID_AUTO, "answered", CTLFLAG_RD, &sc->dp_hb_answered, 0,
	    "answers carrying the sequence last sent");
	SYSCTL_ADD_U64(ctx, c, OID_AUTO, "unmatched", CTLFLAG_RD, &sc->dp_hb_unmatched, 0,
	    "answers carrying another sequence: one that came too late, or not this host's");
	SYSCTL_ADD_U64(ctx, c, OID_AUTO, "unsent", CTLFLAG_RD, &sc->dp_hb_unsent, 0,
	    "beats not sent because ring 0 was full");
	SYSCTL_ADD_U64(ctx, c, OID_AUTO, "lates", CTLFLAG_RD, &sc->dp_hb_lates, 0,
	    "times the fast path went late_ms without answering");
	SYSCTL_ADD_U64(ctx, c, OID_AUTO, "deaths", CTLFLAG_RD, &sc->dp_hb_deaths, 0,
	    "times it went dead_ms without answering");
	SYSCTL_ADD_U64(ctx, c, OID_AUTO, "revivals", CTLFLAG_RD, &sc->dp_hb_revivals, 0,
	    "times it answered again after being taken as stopped");
	SYSCTL_ADD_U64(ctx, c, OID_AUTO, "rtt_us", CTLFLAG_RD, &sc->dp_hb_rtt_us, 0,
	    "the last answer's time from sent to answered, in microseconds");
	SYSCTL_ADD_U64(ctx, c, OID_AUTO, "rtt_max_us", CTLFLAG_RD, &sc->dp_hb_rtt_max_us, 0,
	    "the longest of those since the module was loaded");
}

static int
octep_sysctl_dp_cmsg_post(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, v = 0;

	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL || v == 0)
		return (error);
	return (octep_dp_post_cmsg(sc));
}

/*
 * One direction of a count as the coprocessor last sent it: what moved since the message before.
 *
 * The coprocessor sends running totals, not what moved since its last message, in a width of its
 * own: 35 bits of bytes and 29 of packets. So the difference from the last message is taken in
 * those widths - a total that wrapped is still the right difference. raw is what the last message
 * said, and starts at zero when this driver attaches, as the vendor's host keeps it.
 *
 * A total is the TABLE SLOT's, not the connection's: the coprocessor does not start it again when
 * a slot is given to a new connection. Until 2026-10-19 this took a new connection's first message
 * as all new, and a reused slot brought the whole history of the connections before it - measured:
 * a fetch of a few megabytes through IPsec shown with 47,013,607 packets and 1,792,591,546 bytes, 38
 * bytes a packet, the bytes having wrapped and the packets not. So raw is kept for the slot, across
 * the connections that use it, and moves with every message that names the slot, whoever it is
 * for; only what moved is given to the connection - the same as the vendor's host, whose
 * conn_entry_data keeps the last totals per slot for the life of the module (stat_diff and
 * update_fp_stats in its sp2fp_conn.c).
 */
static int
octep_hw_diff(uint32_t raw[2], uint32_t lo, uint32_t olp, uint64_t *pkts, uint64_t *bytes)
{
	uint64_t b0, b1;
	uint32_t pk;

	b1 = ((uint64_t)(olp & ((1u << OCTEP_HW_PKT_SHIFT) - 1)) << 32) | lo;
	b0 = ((uint64_t)(raw[1] & ((1u << OCTEP_HW_PKT_SHIFT) - 1)) << 32) | raw[0];
	b1 = (b1 - b0) & ((1ull << OCTEP_HW_BYTE_BITS) - 1);
	pk = ((olp >> OCTEP_HW_PKT_SHIFT) - (raw[1] >> OCTEP_HW_PKT_SHIFT)) & OCTEP_HW_PKT_MASK;
	raw[0] = lo;
	raw[1] = olp;
	*bytes = b1;
	*pkts = pk;
	return (pk != 0 || b1 != 0);
}

/*
 * The coprocessor's count of one port (type 0, LIF_STATS), for each LIF it forwarded on - a port's
 * own LIF is its index shifted left by twelve, VLAN 0, and a VLAN subinterface's the same with its
 * VLAN (octep_dp_vlan_lifs).
 * The entry is a connection's without the rev, and it is NOT a running total: measured on the
 * second XGS 3300 (docs/measurements/hw-counters-2026-10-19.md), the messages come about once a
 * second while the port forwards anything in hardware, each with what moved since the one before -
 * 23 of them, none missed, summed to 6,616,749 packets and 10,016,682,019 bytes arriving on F1 for a
 * stream the VM behind F2 received as 6,616,635 frames and 10,016,481,150 bytes. The first direction
 * is what arrived on the port, the second what left it; bytes are the Ethernet frame without its
 * FCS, as an interface counts them. And only what the coprocessor forwarded itself: the same stream
 * through the host brought no message at all, while the host counted 2,216,890 packets in. So these
 * are exactly what the interface's own counters miss, and dp.lif_apply adds them there. A VLAN's
 * counts go to its port, which carried its frames, and to the VLAN interface, as the host counts
 * a tagged frame it forwards on both.
 */
static void
octep_dp_lif_count(struct octep_softc *sc, uint32_t lif, const uint8_t *p)
{
	struct octep_dp_if *dif = NULL;
	struct epoch_tracker et;
	struct ifnet *vifp;
	uint64_t pk[2], by[2];
	uint32_t i, olp, vlan = lif & 0xfff;
	int d, in;

	mtx_assert(&sc->mtx, MA_OWNED);
	for (i = 0; vlan != 0 && (int)i < sc->dp_vlan_lif_n; i++)
		if (sc->dp_vlan_lif[i].vlan == vlan &&
		    sc->dp_if[sc->dp_vlan_lif[i].dif].lif_iface == (lif >> 12))
			break;
	if (vlan == 0 || (int)i < sc->dp_vlan_lif_n)
		for (i = 0; i < OCTEP_DP_IF_MAX; i++)
			if (sc->dp_if[i].ifp != NULL && sc->dp_if[i].lif_iface == (lif >> 12)) {
				dif = &sc->dp_if[i];
				break;
			}
	if (dif == NULL) {
		sc->dp_lif_unknown++;
		return;
	}
	for (d = 0; d < 2; d++) {
		olp = le32dec(p + 8 * d + 4);
		pk[d] = olp >> OCTEP_HW_PKT_SHIFT;
		by[d] = ((uint64_t)(olp & ((1u << OCTEP_HW_PKT_SHIFT) - 1)) << 32) | le32dec(p + 8 * d);
		dif->lif_pkts[d] += pk[d];
		dif->lif_bytes[d] += by[d];
	}
	dif->lif_seen = 1;
	if (vlan != 0)
		sc->dp_lif_vlan++;
	if (sc->dp_lif_apply != 1)
		return;
	for (d = 0; d < 2; d++) {
		in = (d == 0);
		if_inc_counter(dif->ifp, in ? IFCOUNTER_IPACKETS : IFCOUNTER_OPACKETS, pk[d]);
		if_inc_counter(dif->ifp, in ? IFCOUNTER_IBYTES : IFCOUNTER_OBYTES, by[d]);
	}
	if (vlan != 0) {
		NET_EPOCH_ENTER(et);
		vifp = VLAN_DEVAT((struct ifnet *)dif->ifp, (uint16_t)vlan);
		for (d = 0; vifp != NULL && d < 2; d++) {
			in = (d == 0);
			if_inc_counter((if_t)vifp, in ? IFCOUNTER_IPACKETS : IFCOUNTER_OPACKETS, pk[d]);
			if_inc_counter((if_t)vifp, in ? IFCOUNTER_IBYTES : IFCOUNTER_OBYTES, by[d]);
		}
		NET_EPOCH_EXIT(et);
	}
	sc->dp_lif_applied++;
}

/*
 * The first entry of a live user in a table sorted by user (dp.live_users), NULL if it is not
 * there: the entry that carries the user's counts.
 */
static struct octep_live_user *
octep_lu_find(struct octep_live_user *l, int n, uint16_t luid)
{
	int lo = 0, hi = n, mid;

	while (lo < hi) {
		mid = (lo + hi) / 2;
		if (l[mid].luid < luid)
			lo = mid + 1;
		else
			hi = mid;
	}
	return (lo < n && l[lo].luid == luid ? &l[lo] : NULL);
}

static int
octep_lu_cmp(const void *a, const void *b)
{
	const struct octep_live_user *x = a, *y = b;

	return ((int)x->luid - (int)y->luid);
}

/* What moved for a connection with a live user, given to its user: index 0 from the client. */
static void
octep_lu_count(struct octep_softc *sc, const struct octep_conn *c, const uint64_t pk[2],
    const uint64_t by[2])
{
	struct octep_live_user *u;
	int d;

	mtx_assert(&sc->mtx, MA_OWNED);
	if ((u = octep_lu_find(sc->dp_lu, sc->dp_nlu, c->luid)) == NULL)
		return;
	for (d = 0; d < 2; d++) {
		u->pkts[d ^ c->lu_side] += pk[d];
		u->bytes[d ^ c->lu_side] += by[d];
	}
	if (pk[0] != 0 || pk[1] != 0)
		u->moved = time_second;
}

/*
 * A copy of a frame the fast path forwarded itself (type 6), which it sends while fw_cfg's
 * FP_PKT_DUMP bit is set - so a connection in hardware can still be seen by a capture on its
 * ports. The vendor's host hands each copy to the packet capture of the port it names and to
 * nothing else (usfp_packet_dump in its cmsg.c), and so does this: to that interface's BPF, only
 * while something listens there, with BPF's own sense of direction - an arriving frame has the
 * port as its receiving interface, a leaving one has none, as a frame this host sent would. The
 * copy never enters the stack, so a firewall rule never sees a frame twice.
 *
 * Measured before it was written, 2026-10-25 on the second XGS 3300 (SFOS 22's software), the bit
 * set by hand for five seconds of a 50 Mbit/s stream: 7,640 type-6 messages, the last one
 * direction 1, tag 3 - the port the stream left by - and the routed frame behind it, its MACs
 * rewritten and its TTL one lower.
 */
static void
octep_dp_mirror(struct octep_softc *sc, const uint8_t *p, const uint8_t *end)
{
	struct epoch_tracker et;
	struct octep_dp_if *dif;
	struct mbuf *m;
	uint16_t tag;
	uint8_t dir;

	if (end - p < OCTEP_CMSG_MRR_HLEN + ETHER_HDR_LEN || p[4] > OCTEP_CMSG_MRR_OUT) {
		sc->dp_mirror_bad++;
		return;
	}
	dir = p[4];
	tag = le16dec(p + 6);
	sc->dp_mirror[dir]++;
	dif = octep_dp_if_by_tag(sc, tag);
	if (dif == NULL || dif->ifp == NULL) {
		sc->dp_mirror_noport++;
		return;
	}
	if (!bpf_peers_present_if(dif->ifp)) {
		sc->dp_mirror_unheard++;
		return;
	}
	m = m_devget(__DECONST(char *, p + OCTEP_CMSG_MRR_HLEN),
	    (int)(end - p - OCTEP_CMSG_MRR_HLEN), 0, dif->ifp, NULL);
	if (m == NULL) {
		sc->dp_mirror_nomem++;
		return;
	}
	if (dir == OCTEP_CMSG_MRR_OUT)
		m->m_pkthdr.rcvif = NULL;
	NET_EPOCH_ENTER(et);
	bpf_mtap_if(dif->ifp, m);
	NET_EPOCH_EXIT(et);
	m_freem(m);
	sc->dp_mirror_tapped++;
}

/*
 * A control message from the coprocessor, read where it arrives: on tag 254, with no interface of
 * its own. Each type is counted; the connection statistics (type 1) are applied to this table.
 *
 * The coprocessor counts every connection it forwards - the packets and bytes this host never sees
 * once a connection is in hardware - and sends what moved, unasked, about once a second: an entry
 * for each connection that carried anything. Until 2026-10-19 they were dropped here unread.
 * The layout is the vendor's (cmsg_types.h, struct cmsg_stats_update) and was measured before it
 * was trusted: a control frame captured on the second XGS 3300 during one stream in hardware
 * (dp.rx_untagged_frame, rx_untag_want 254) held type 1, version 1, one entry in one burst for
 * index 2 - the table's first - and two captures two seconds apart grew by 1,045,982,624 bytes and
 * 697,744 packets in the opener's direction: 1,499 bytes a packet, 4.2 Gbit/s, on a stream of
 * full-sized frames. An entry whose rev is not the entry's own is for a connection that has gone:
 * its slot's totals still move (octep_hw_diff), and it is counted and given to nobody. The
 * interface counts (type 0) are read the same way, burst by burst, into octep_dp_lif_count; every
 * type's last message is kept for dp.cmsg_last.
 */
static void
octep_dp_cmsg_rx(struct octep_softc *sc, const uint8_t *f, uint32_t flen)
{
	const uint8_t *p, *end;
	struct octep_conn *c;
	uint64_t pk[2], by[2];
	uint32_t nb, i, k, hdr, idx, cnt, ne, seen;
	uint16_t rev;
	uint8_t type;
	int d, moved;

	if (flen < ETHER_HDR_LEN + 8 || be16dec(f + 12) != OCTEP_CMSG_ETHERTYPE)
		return;
	p = f + ETHER_HDR_LEN;
	end = f + flen;
	type = p[2];
	/* a copy for a capture: given to it before the lock, which BPF has no need of */
	if (type == OCTEP_CMSG_TYPE_MIRROR)
		octep_dp_mirror(sc, p, end);
	mtx_lock(&sc->mtx);
	if (type < OCTEP_CMSG_TYPES) {
		sc->dp_cmsg_rx[type]++;
		sc->dp_cmsg_last_len[type] = (uint16_t)(flen < OCTEP_CMSG_KEEP ? flen : OCTEP_CMSG_KEEP);
		memcpy(sc->dp_cmsg_last[type], f, sc->dp_cmsg_last_len[type]);
	}
	/* the fast path's answer to a heartbeat: the sequence it was asked with, after the header */
	if (type == OCTEP_CMSG_TYPE_HB_TO_HOST && flen >= ETHER_HDR_LEN + 8)
		octep_hb_answer(sc, le32dec(p + 4));
	if ((type != OCTEP_CMSG_TYPE_CONN_STATS && type != OCTEP_CMSG_TYPE_LIF_STATS) ||
	    p[3] != OCTEP_CMSG_VERSION) {
		mtx_unlock(&sc->mtx);
		return;
	}
	ne = le16dec(p + 4);
	nb = p[6];
	p += 8;
	seen = 0;
	for (i = 0; i < nb; i++) {
		if (end - p < 4)
			goto short_msg;
		hdr = le32dec(p);
		p += 4;
		idx = hdr & 0x03ffffffu;
		cnt = (hdr >> 26) + 1;
		if (type == OCTEP_CMSG_TYPE_LIF_STATS) {
			if ((size_t)(end - p) < (size_t)cnt * OCTEP_LIF_ENTRY_LEN)
				goto short_msg;
			for (k = 0; k < cnt; k++, p += OCTEP_LIF_ENTRY_LEN) {
				seen++;
				octep_dp_lif_count(sc, idx + k, p);
			}
			continue;
		}
		if ((size_t)(end - p) < (size_t)cnt * OCTEP_HW_ENTRY_LEN)
			goto short_msg;
		for (k = 0; k < cnt; k++, p += OCTEP_HW_ENTRY_LEN) {
			seen++;
			rev = le16dec(p + 16);
			if (idx + k >= sc->dp_conn_cap) {
				sc->dp_hw_stale++;
				continue;
			}
			/* the slot's totals move whoever they are for: see octep_hw_diff */
			c = &sc->dp_conn[idx + k];
			moved = 0;
			for (d = 0; d < 2; d++)
				moved |= octep_hw_diff(c->hw_raw[d], le32dec(p + 8 * d),
				    le32dec(p + 8 * d + 4), &pk[d], &by[d]);
			if (!c->used || c->conn_rev != rev) {
				sc->dp_hw_stale++;
				continue;
			}
			for (d = 0; d < 2; d++) {
				c->hw_pkts[d] += pk[d];
				c->hw_bytes[d] += by[d];
			}
			if (c->ipsec && moved)
				octep_ipsec_flow_count(sc, c, pk, by);
			c->hw_seen = 1;
			if (moved)
				c->hw_moved = time_uptime;
			if (c->luid != 0 && moved)
				octep_lu_count(sc, c, pk, by);
			sc->dp_hw_applied++;
		}
	}
	if (seen != ne)
		sc->dp_hw_bad++;
	mtx_unlock(&sc->mtx);
	return;
short_msg:
	sc->dp_hw_bad++;
	mtx_unlock(&sc->mtx);
}

static int
octep_sysctl_dp_cmsg_rx(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	uint64_t n[OCTEP_CMSG_TYPES], ap, st, bad;
	struct sbuf *sb;
	int error, t;

	mtx_lock(&sc->mtx);
	memcpy(n, sc->dp_cmsg_rx, sizeof(n));
	ap = sc->dp_hw_applied;
	st = sc->dp_hw_stale;
	bad = sc->dp_hw_bad;
	mtx_unlock(&sc->mtx);

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);
	sbuf_cat(sb, "by type:");
	for (t = 0; t < OCTEP_CMSG_TYPES; t++)
		if (n[t] != 0)
			sbuf_printf(sb, " %d: %ju", t, (uintmax_t)n[t]);
	sbuf_printf(sb, "\nconnection counts: %ju applied, %ju for a connection gone, %ju messages "
	    "not read to their end\n", (uintmax_t)ap, (uintmax_t)st, (uintmax_t)bad);
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/* The fast path's copies for a capture (type 6), as octep_dp_mirror counted them. */
static int
octep_sysctl_dp_mirror(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct sbuf *sb;
	int error;

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);
	sbuf_printf(sb, "copies %ju arriving, %ju leaving: %ju given to a capture, %ju with nothing "
	    "listening, %ju for a tag no interface carries, %ju too short, %ju without an mbuf\n",
	    (uintmax_t)sc->dp_mirror[OCTEP_CMSG_MRR_IN], (uintmax_t)sc->dp_mirror[OCTEP_CMSG_MRR_OUT],
	    (uintmax_t)sc->dp_mirror_tapped, (uintmax_t)sc->dp_mirror_unheard,
	    (uintmax_t)sc->dp_mirror_noport, (uintmax_t)sc->dp_mirror_bad,
	    (uintmax_t)sc->dp_mirror_nomem);
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/* Each type's last message, after its Ethernet header, as hex: how a layout is read before use. */
static int
octep_sysctl_dp_cmsg_last(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	uint8_t *f;
	uint16_t n[OCTEP_CMSG_TYPES];
	struct sbuf *sb;
	int error, t, i;

	f = malloc(sizeof(sc->dp_cmsg_last), M_TEMP, M_WAITOK);
	mtx_lock(&sc->mtx);
	memcpy(f, sc->dp_cmsg_last, sizeof(sc->dp_cmsg_last));
	memcpy(n, sc->dp_cmsg_last_len, sizeof(n));
	mtx_unlock(&sc->mtx);

	sb = sbuf_new_for_sysctl(NULL, NULL, 2048, req);
	if (sb == NULL) {
		free(f, M_TEMP);
		return (ENOMEM);
	}
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);
	for (t = 0; t < OCTEP_CMSG_TYPES; t++) {
		if (n[t] <= ETHER_HDR_LEN)
			continue;
		sbuf_printf(sb, "type %d, %u bytes after the Ethernet header:\n", t,
		    n[t] - ETHER_HDR_LEN);
		for (i = ETHER_HDR_LEN; i < n[t]; i++)
			sbuf_printf(sb, "%02x%s", f[t * OCTEP_CMSG_KEEP + i],
			    ((i - ETHER_HDR_LEN) % 16) == 15 ? "\n" : " ");
		sbuf_cat(sb, "\n");
	}
	free(f, M_TEMP);
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/* The coprocessor's count of each port (type 0), as octep_dp_lif_count keeps it. */
static int
octep_sysctl_dp_lif_counted(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct sbuf *sb;
	char name[IFNAMSIZ];
	uint64_t pk[2], by[2];
	uint32_t i;
	int error, seen;

	sb = sbuf_new_for_sysctl(NULL, NULL, 1024, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);
	for (i = 0; i < OCTEP_DP_IF_MAX; i++) {
		mtx_lock(&sc->mtx);
		seen = sc->dp_if[i].ifp != NULL && sc->dp_if[i].lif_seen;
		if (seen) {
			strlcpy(name, if_name(sc->dp_if[i].ifp), sizeof(name));
			memcpy(pk, sc->dp_if[i].lif_pkts, sizeof(pk));
			memcpy(by, sc->dp_if[i].lif_bytes, sizeof(by));
		}
		mtx_unlock(&sc->mtx);
		if (seen)
			sbuf_printf(sb, "%s  in %ju packets %ju bytes  out %ju packets %ju bytes\n",
			    name, (uintmax_t)pk[0], (uintmax_t)by[0], (uintmax_t)pk[1], (uintmax_t)by[1]);
	}
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/*
 * dp.module_media: what the module in each cage is, for the medium the port reports.
 *
 * Written by the plugin (scripts/xgs/transceivers.py) after it reads the modules on the
 * coprocessor, one cage a write: "<interface> <at 1G> <at 10G>", each an Ethernet media subtype
 * as net/if_media.h numbers them (19 10Gbase-SR, 26 10Gbase-T, 11 1000baseSX), 0 for nothing
 * read. "<interface> 0 0" forgets a module taken out. Reading lists the cages and what each was
 * told.
 */
static int
octep_sysctl_dp_module_media(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct octep_dp_if *dif;
	struct sbuf *sb;
	char in[64], name[IFNAMSIZ], *p, *end;
	u_long v[2];
	uint32_t i;
	int error, k;

	if (req->newptr == NULL) {
		sb = sbuf_new_for_sysctl(NULL, NULL, 512, req);
		if (sb == NULL)
			return (ENOMEM);
		sbuf_clear_flags(sb, SBUF_INCLUDENUL);
		for (i = 0; i < OCTEP_DP_IF_MAX; i++) {
			mtx_lock(&sc->mtx);
			dif = &sc->dp_if[i];
			k = dif->ifp != NULL && dif->cage;
			if (k) {
				strlcpy(name, if_name(dif->ifp), sizeof(name));
				v[0] = dif->mod_media[0];
				v[1] = dif->mod_media[1];
			}
			mtx_unlock(&sc->mtx);
			if (k)
				sbuf_printf(sb, "%s %lu %lu\n", name, v[0], v[1]);
		}
		error = sbuf_finish(sb);
		sbuf_delete(sb);
		return (error);
	}

	memset(in, 0, sizeof(in));
	error = sysctl_handle_string(oidp, in, sizeof(in), req);
	if (error != 0)
		return (error);
	for (p = in; *p == ' '; p++)
		;
	for (k = 0; *p != '\0' && *p != ' ' && k < IFNAMSIZ - 1; k++)
		name[k] = *p++;
	name[k] = '\0';
	if (k == 0 || *p != ' ')
		return (EINVAL);
	for (k = 0; k < 2; k++) {
		v[k] = strtoul(p, &end, 10);
		/* the subtype field with its extension bits, IFM_ETHER_SUBTYPE's whole range */
		if (end == p || v[k] > 0x1ff)
			return (EINVAL);
		p = end;
	}
	while (*p == ' ' || *p == '\n')
		p++;
	if (*p != '\0')
		return (EINVAL);

	error = ENOENT;
	mtx_lock(&sc->mtx);
	for (i = 0; i < OCTEP_DP_IF_MAX; i++) {
		dif = &sc->dp_if[i];
		if (dif->ifp == NULL || !dif->cage || strcmp(if_name(dif->ifp), name) != 0)
			continue;
		dif->mod_media[0] = (u_int)v[0];
		dif->mod_media[1] = (u_int)v[1];
		error = 0;
		break;
	}
	mtx_unlock(&sc->mtx);
	return (error);
}

static int
octep_sysctl_dp_cmd_post(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, v = 0;

	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL || v == 0)
		return (error);
	return (octep_dp_post_cmd(sc));
}

/*
 * Post one frame and ring the doorbell. This is a deliberate single-shot: it writes one instruction,
 * advances one slot and credits exactly one. Nothing here is a transmit path for a network stack - it
 * exists to find out whether the silicon fetches and acts on an instruction we built.
 */
static int
octep_dp_xmit_test(struct octep_softc *sc, uint32_t len)
{
	uint8_t *d;
	uint32_t i, iplen, udplen, sum;

	if (len < 42)			/* Ethernet + IPv4 + UDP headers */
		len = 42;
	if (len < OCTEP_MIN_FRAME)
		len = OCTEP_MIN_FRAME;
	if (len > PAGE_SIZE)
		return (EINVAL);

	mtx_lock(&sc->mtx);
	if (sc->dp_up == 0) {
		mtx_unlock(&sc->mtx);
		return (ENXIO);
	}

	/*
	 * A WELL-FORMED IPv4/UDP frame, and it has to be well formed: the coprocessor's fast path
	 * computes an L3/L4 checksum on the way out, and a frame it cannot parse takes it into
	 * `sso_event_tx_adapter_enqueue_noff_l3l4csum` and a SIGSEGV. The first version of this test
	 * sent a non-IP EtherType and killed the fast path, which is how that was learned.
	 *
	 * Inert by construction rather than by being malformed: broadcast at both layers, a locally
	 * administered source MAC, source address 0.0.0.0, and UDP port 9 - the discard service - so
	 * there is nothing to route, nothing to answer, and nothing that any host should act on.
	 */
	/*
	 * The private header first: a 2-byte port tag in network order, then 64 bytes of metadata
	 * left zero. The Ethernet frame starts after it, so everything below indexes from `d`.
	 */
	d = (uint8_t *)sc->dp_txbuf.vaddr;
	bzero(d, OCTEP_TOTAL_TAG_LEN + len);
	d[0] = (uint8_t)((sc->dp_port_tag >> 8) & 0xff);
	d[1] = (uint8_t)(sc->dp_port_tag & 0xff);
	/*
	 * What goes in the 64 metadata bytes. This decides whether the frame reaches a wire.
	 *
	 * The fast path parses the metadata before it forwards. In worker_ordered it steps over a
	 * 28-byte prefix and then reads one byte; if that byte is non-zero it takes the four bytes
	 * that follow as an egress security-association handle and routes the frame to encryption
	 * instead of to the wire. The counters name both outcomes: FPCNTR_FROM_KN_TO_WIRE against
	 * FPCNTR_FROM_KN_TO_IPSEC_ENCR.
	 *
	 * The walking pattern from 0xc0 makes that byte 0xdf, so every frame was classified for
	 * encryption, the encryption had no association to use, and it was counted at
	 * FPCNTR_TX_DROP. Measured on the appliance: 272 frames posted under the pattern gave
	 * FROM_KN_TO_IPSEC_ENCR 272 and TX_DROP 272, with TX_WIRE zero; 21 frames posted under the
	 * vendor form gave FROM_KN_TO_WIRE 21 and TX_WIRE 21, with TX_DROP unchanged.
	 *
	 * So the vendor form is the default: byte 0 set to 1, the other 63 zero. The other modes
	 * stay because they are the controls that established this.
	 */
	switch (sc->dp_meta_mode) {
	case OCTEP_META_MODE_SIGNATURE:
		memset(d + OCTEP_PPORT_HLEN, 0, OCTEP_CUSTOM_META_LEN);
		be64enc(d + OCTEP_PPORT_HLEN, OCTEP_META_SIGNATURE);
		break;
	case OCTEP_META_MODE_ZERO:
		memset(d + OCTEP_PPORT_HLEN, 0, OCTEP_CUSTOM_META_LEN);
		break;
	case OCTEP_META_MODE_VENDOR:
		/*
		 * meta[0] is a type byte and the vendor's hook always writes 1. dp.meta_b0 exists
		 * to ask what the other values mean, because the fast path counts control messages
		 * from the host separately - FPCNTR_FROM_KN_PROC_CMSG - and something in the frame
		 * has to say which it is. Zero here keeps the vendor's value.
		 */
		memset(d + OCTEP_PPORT_HLEN, 0, OCTEP_CUSTOM_META_LEN);
		d[OCTEP_PPORT_HLEN] = (sc->dp_meta_b0 != 0) ?
		    (uint8_t)sc->dp_meta_b0 : OCTEP_META_VENDOR_BYTE0;
		break;
	default:
		for (i = 0; i < OCTEP_CUSTOM_META_LEN; i++)
			d[OCTEP_PPORT_HLEN + i] = (uint8_t)(OCTEP_META_START + i);
		break;
	}
	d += OCTEP_TOTAL_TAG_LEN;

	/*
	 * The destination matters, and finding out cost a wrong conclusion once already.
	 *
	 * This frame was broadcast for a long time, which is convenient and is also a class the
	 * coprocessor's fast path counts separately - PD_DEBUG_CNT_NA_ETH_DST_BC, which stood at
	 * 10,548 in a capture from this board under the vendor's firmware. And the appliance's own
	 * inventory records a test that addressed frames to a fabricated address no port owned: the
	 * transmit counters moved, no receive counter did, and for a few minutes that looked like a
	 * datapath fault. It was the receiving MAC filtering silently.
	 *
	 * So dp.dst_mac exists, and it defaults to broadcast only because that is what every earlier
	 * measurement on this page used.
	 */
	if (sc->dp_dst_mac[0] == 0 && sc->dp_dst_mac[1] == 0 && sc->dp_dst_mac[2] == 0 &&
	    sc->dp_dst_mac[3] == 0 && sc->dp_dst_mac[4] == 0 && sc->dp_dst_mac[5] == 0)
		memset(d, 0xff, 6);
	else
		memcpy(d, sc->dp_dst_mac, 6);
	d[6] = 0x02;				/* source MAC: locally administered */
	d[11] = 0x01;
	d[12] = 0x08; d[13] = 0x00;		/* EtherType: IPv4 */

	iplen = len - 14;
	d[14] = 0x45;				/* IPv4, 20-byte header */
	d[15] = 0x00;				/* DSCP/ECN */
	d[16] = (uint8_t)(iplen >> 8);		/* total length */
	d[17] = (uint8_t)(iplen & 0xff);
	d[20] = 0x40;				/* flags: do not fragment */
	d[22] = 64;				/* TTL */
	d[23] = 17;				/* protocol: UDP */
	/* source 0.0.0.0, destination 255.255.255.255 */
	memset(&d[30], 0xff, 4);

	/* header checksum over the 20 bytes at offset 14, with the field itself zero */
	sum = 0;
	for (i = 0; i < 20; i += 2)
		sum += ((uint32_t)d[14 + i] << 8) | d[15 + i];
	while ((sum >> 16) != 0)
		sum = (sum & 0xffff) + (sum >> 16);
	sum = ~sum & 0xffff;
	d[24] = (uint8_t)(sum >> 8);
	d[25] = (uint8_t)(sum & 0xff);

	udplen = iplen - 20;
	/*
	 * THE SOURCE PORT MATTERS, and every frame this driver ever sent had the same one.
	 *
	 * `hash_sp_txq_get` in the coprocessor's fast path computes a CRC32C over the connection
	 * tuple - the addresses at +24 and +32 and the ports at +40 and +48 of the flow record -
	 * and returns `hash % n`, where n is the number of slow-path transmit queues. That count is
	 * not a constant: `usfp_startup_octtx.sh` passes it as `-t $num_sp_txqs`, and for assembly
	 * AMDA0202-0004, which is this board, the script sets it to 8 where its own default is 4.
	 *
	 * So a frame the fast path decides to give the host goes to one of EIGHT queues, chosen by
	 * hashing the tuple. Every test frame here has carried the same tuple - 0.0.0.0 to
	 * 255.255.255.255, port 9 to port 9 - so every one of them hashed to the same queue. If that
	 * queue is not the one ring this driver programs, none of them could ever arrive, and that
	 * is consistent with every measurement taken so far.
	 *
	 * dp.sport varies the source port so the tuple varies with it. Zero keeps the old fixed 9,
	 * which is what every earlier run used.
	 */
	if (sc->dp_sport != 0) {
		d[34] = (uint8_t)(sc->dp_sport >> 8);
		d[35] = (uint8_t)(sc->dp_sport & 0xff);
	} else {
		d[34] = 0x00; d[35] = 0x09;		/* source port 9, discard */
	}
	d[36] = 0x00; d[37] = 0x09;		/* destination port 9 */
	d[38] = (uint8_t)(udplen >> 8);		/* UDP length */
	d[39] = (uint8_t)(udplen & 0xff);
	/* UDP checksum left zero, which IPv4 permits and means "not computed" */

	for (i = 42; i < len; i++)
		d[i] = (uint8_t)i;

	bus_dmamap_sync(sc->dp_txbuf.tag, sc->dp_txbuf.map, BUS_DMASYNC_PREWRITE);

	octep_dp_build_instr(sc, sc->dp_iq_prod, sc->dp_txbuf.paddr, len + OCTEP_TOTAL_TAG_LEN);
	bus_dmamap_sync(sc->dp_iq.tag, sc->dp_iq.map, BUS_DMASYNC_PREWRITE);

	sc->dp_iq_prod = (sc->dp_iq_prod + 1) % OCTEP_DP_IQ_DESCS;
	sc->dp_tx_posted++;

	/* One instruction is now valid. */
	octep_dp_wr(sc, OCTEP_SDP_R_IN_INSTR_DBELL, 1);
	mtx_unlock(&sc->mtx);

	device_printf(sc->dev, "dp: posted a %u byte IPv4/UDP frame, pkind %u, fsz %u, "
	    "cksum offset %u\n", len, sc->dp_pkind, OCTEP_INSTR_FSZ, OCTEP_IRH_CKSUM_OFF);
	return (0);
}

/*
 * How many receive buffers a packet with this length word used, or 0 if the word is no length.
 *
 * The vendor's rule, from octeon_droq_get_bufcount as it is built for buffer-pointer mode: the
 * first buffer holds the length word, the response word and then what is left of it, and every
 * buffer after it is all packet. The length word counts the response word, so one buffer holds a
 * packet whose word is at most OCTEP_DP_BUF_SIZE - 8, and what does not fit is the word plus
 * eight, less a buffer. A word above OCTEP_DP_PKT_MAX, or one that would use the whole ring, is
 * not a length. Every walk of a ring's buffers asks this, so that all of them agree where a
 * packet ends: the pass, the scan for a gap, and the report.
 */
static __inline uint32_t
octep_dp_oq_span(uint64_t blen, uint32_t rsize)
{
	uint32_t span;

	if (blen <= OCTEP_DP_BUF_SIZE - 8)
		return (1);
	if (blen > OCTEP_DP_PKT_MAX)
		return (0);
	span = 1 + (uint32_t)howmany(blen + 8 - OCTEP_DP_BUF_SIZE, (uint64_t)OCTEP_DP_BUF_SIZE);
	return (span < rsize ? span : 0);
}

/*
 * Scan one output ring's buffers for anything the coprocessor has written. The length word at the
 * head of a buffer is the arrival flag - the coprocessor zeroes nothing, so a non-zero length there
 * means it filled that buffer - and it is big-endian.
 *
 * The decode is the layout of a frame that completed the whole loop and was then read byte for byte
 * out of host memory: 8 bytes of SDP info carrying the length, 8 bytes of 0x8003000000000000, the
 * 2-byte pport tag, 64 metadata bytes, and the Ethernet header at +82. See OCTEP_RX_PREFIX_LEN.
 */
static uint32_t
octep_dp_rx_scan(struct octep_softc *sc, struct sbuf *sb, struct octep_dma *bufs, uint32_t ring)
{
	const uint8_t *b, *e;
	uint64_t len;
	uint32_t i, k, meta, rd, rs, span, shown = 0, found = 0;

	bus_dmamap_sync(bufs->tag, bufs->map, BUS_DMASYNC_POSTREAD);

	/*
	 * From the ring's read index and round, not from buffer 0. The read index is always on
	 * the first buffer of a packet, and the buffers a long packet went on into carry no word
	 * of their own - their first eight bytes are payload - so a walk that starts on a packet
	 * and steps by what each length says never reads one of those as a packet. A walk from
	 * buffer 0 did: it printed a length, a tag and two addresses made of statistics entries.
	 */
	rs = sc->dp_oq_rsize;
	if (rs == 0 || rs > OCTEP_DP_OQ_DESCS)
		rs = OCTEP_DP_OQ_DESCS;
	rd = (ring <= OCTEP_DP_SIBLINGS_MAX) ? sc->dp_oq_rd[ring] % rs : 0;
	for (k = 0; k < rs; k++) {
		i = (rd + k) % rs;
		b = (const uint8_t *)bufs->vaddr + ((size_t)i * OCTEP_DP_BUF_STRIDE);
		len = be64toh(*(const uint64_t *)(b + OCTEP_RX_LEN_OFF));
		if (len == 0 || len == OCTEP_DP_BUF_POISON_WORD)
			continue;   /* untouched, or written as zero - neither is an arrival */
		span = octep_dp_oq_span(len, rs);
		if (span == 0)
			span = 1;
		/* Its later buffers are written too, and are stepped over. */
		found += span;
		k += span - 1;
		if (shown >= 4)
			continue;
		shown++;
		e = b + OCTEP_RX_PREFIX_LEN;
		meta = le32dec(b + OCTEP_RX_META_OFF);
		sbuf_printf(sb, "  ring %u buf %3u  len %ju  tag %ju  meta 0x%08x%s",
		    ring, i, (uintmax_t)len, (uintmax_t)be16dec(b + OCTEP_RX_TAG_OFF),
		    meta, meta == OCTEP_RX_META_SIG ? " - the vendor's" : "");
		if (span > 1)
			sbuf_printf(sb, "  %u buffers", span);
		sbuf_printf(sb, "\n    %02x:%02x:%02x:%02x:%02x:%02x <- "
		    "%02x:%02x:%02x:%02x:%02x:%02x  type %02x%02x\n",
		    e[0], e[1], e[2], e[3], e[4], e[5],
		    e[6], e[7], e[8], e[9], e[10], e[11], e[12], e[13]);
	}
	/*
	 * Which ring, and where its reader is. The total alone - 268, across every armed ring -
	 * read as traffic in flight; that it was one ring, and the same 268 a minute later, is
	 * what said a ring was behind.
	 */
	if (found != 0 && ring <= OCTEP_DP_SIBLINGS_MAX)
		sbuf_printf(sb, "  ring %u: %u written and not taken, read index %u\n", ring,
		    found, rd);
	return (found);
}

/*
 * Every armed ring, not only the datapath's own. A frame that completed the loop was found on a
 * sibling - ring 12 - while this reported "0 of 256 receive buffers have been written", because it
 * only ever looked at one ring. The fast path picks its host queue by hashing the frame, so which
 * ring any given frame lands on is not the host's to choose.
 */
static void
octep_dp_rx_report(struct octep_softc *sc, struct sbuf *sb)
{
	uint32_t i, found;

	found = octep_dp_rx_scan(sc, sb, &sc->dp_bufs, sc->dp_ring);

	for (i = 0; i < OCTEP_DP_SIBLINGS_MAX; i++) {
		struct octep_dp_oq *oq = &sc->dp_sib[i];

		if (oq->armed == 0)
			continue;
		found += octep_dp_rx_scan(sc, sb, &oq->bufs, oq->ring);
		sbuf_printf(sb,
		    "  sibling ring %u  OUT_CONTROL 0x%016jx  ENABLE %ju  DBELL %ju  CNTS %ju\n",
		    oq->ring,
		    (uintmax_t)octep_dp_ring_rd(sc, oq->ring, OCTEP_SDP_R_OUT_CONTROL),
		    (uintmax_t)octep_dp_ring_rd(sc, oq->ring, OCTEP_SDP_R_OUT_ENABLE),
		    (uintmax_t)octep_dp_ring_rd(sc, oq->ring, OCTEP_SDP_R_OUT_SLIST_DBELL),
		    (uintmax_t)octep_dp_ring_rd(sc, oq->ring, OCTEP_SDP_R_OUT_CNTS));
	}
	sc->dp_rx_seen = found;
	sbuf_printf(sb, "  %u receive buffers have been written, across every armed ring\n",
	    found);
}

/* ---------------------------------------------------------------- sysctls */

/*
 * Suspend and resume servicing by hand, which is how the quiesce is tested.
 *
 * The quiesce exists so that an interface can be detached without a servicer holding mbufs that
 * point at it. Reaching it the way the driver does - dp.stop, dp.if_del, a detach - takes the twelve
 * front ports with it on this appliance, one of which is the WAN, and the last time those ports went
 * away for a boot OPNsense dropped four interface assignments out of its configuration and they had
 * to come back from a backup. That is too much to spend on a test.
 *
 * So the mechanism is reachable on its own. Writing 1 runs exactly the quiesce the detach path runs:
 * servicers are turned away, the watchdog is drained, and the wait for anyone already inside returns
 * only when every ring's busy flag is clear. At that point - and this is the property that makes a
 * detach safe - nothing can be holding an ifnet. Writing 0 resumes.
 *
 * It is a debug knob and it will stop reception while it is set, like dp.stop and unlike dp.service.
 */
/*
 * The last received frame's whole prefix, as hex, with the fields this driver understands named.
 *
 * Read-only and without side effects: it prints a copy the receive path took. What it is for is the
 * bytes nothing here has ever read. Issue #185 stops on one question - the crypto path takes its SA
 * index from a flow, a flow can only be programmed by (mflow_id, mflow_rev_num), and nothing
 * published says where a host learns those. A usfp_mflow_ident is four bytes, the far side writes
 * 82 in front of every frame, and the vendor's own host has to get it from somewhere. So every
 * four-byte window is also printed the way that structure is laid out, because a reader comparing
 * two frames of different flows wants the candidates side by side rather than shifting hex by hand.
 */
static int
octep_sysctl_dp_rx_prefix(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	uint8_t p[OCTEP_RX_PREFIX_LEN];
	struct sbuf *sb;
	uint64_t seq;
	int error, i;

	sb = sbuf_new_for_sysctl(NULL, NULL, 1024, req);
	if (sb == NULL)
		return (ENOMEM);

	mtx_lock(&sc->mtx);
	memcpy(p, sc->dp_rx_prefix, sizeof(p));
	seq = sc->dp_rx_prefix_seq;
	mtx_unlock(&sc->mtx);

	if (seq == 0) {
		sbuf_cat(sb, "\nno frame has been received yet\n");
		goto out;
	}
	sbuf_printf(sb, "\nframe %ju, %u prefix bytes\n", (uintmax_t)seq, OCTEP_RX_PREFIX_LEN);
	for (i = 0; i < OCTEP_RX_PREFIX_LEN; i += 16) {
		int n = OCTEP_RX_PREFIX_LEN - i, j;

		if (n > 16)
			n = 16;
		sbuf_printf(sb, "  +%02x  ", i);
		for (j = 0; j < n; j++)
			sbuf_printf(sb, "%02x ", p[i + j]);
		sbuf_cat(sb, "\n");
	}
	sbuf_printf(sb, "  length  %ju\n", (uintmax_t)be64dec(p + OCTEP_RX_LEN_OFF));
	sbuf_printf(sb, "  tag     0x%04x\n", be16dec(p + OCTEP_RX_TAG_OFF));
	/*
	 * struct usfp_kn_md, field by field rather than as a scan.
	 *
	 * This used to print the word at OCTEP_RX_META_OFF as "the vendor's" signature and then
	 * every non-zero word in the prefix decoded as a flow identity, because the layout was not
	 * known and one of them might have been it. That found the right word and three wrong ones
	 * beside it - the shape of reading that produces a confident wrong answer. The structure is
	 * in the vendor's own header and the offset is confirmed by md_valid reading 1, which a
	 * layout guessed wrong would not produce.
	 */
	{
		uint32_t idtag = le32dec(p + OCTEP_RX_META_OFF);
		uint32_t w1 = le32dec(p + OCTEP_RX_MD_VALID_OFF);
		uint32_t flow = le32dec(p + OCTEP_RX_MD_FLOW_OFF);
		uint32_t sa = le32dec(p + OCTEP_RX_MD_SA_OFF);

		sbuf_cat(sb, "  usfp_kn_md:\n");
		sbuf_printf(sb, "    id_tag    0x%08x%s\n", idtag,
		    idtag == OCTEP_RX_META_SIG ? "  - as this board sets it" : "");
		sbuf_printf(sb, "    md_valid  %u    port %u\n",
		    (w1 >> 8) & 0xff, (w1 >> 16) & 0xffff);
		sbuf_printf(sb, "    flow      0x%08x  id %u  rev %u  valid %u\n",
		    flow, flow & 0x01ffffffu, (flow >> 25) & 0x3fu, (flow >> 31) & 1u);
		sbuf_printf(sb, "    sa_index  %u    sa_rev %u\n",
		    sa & 0xffff, (sa >> 16) & 0xffff);
	}
out:
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/*
 * What pf says about the last punted frame.
 *
 * Read-only, and the whole of the host half of a flow offload rests on it: if this prints a state
 * for a frame whose slot dp.rx_prefix has just printed, then everything needed to program that
 * flow is in the driver's hands at the moment the frame arrives, and nothing has to be hooked,
 * polled or notified.
 *
 * It prints which index order matched rather than claiming one. pf stores a key with pd->sidx and
 * pd->didx, which follow the direction the state was created in, so which of (src,dst) and
 * (dst,src) matches a frame off the wire is a property of the connection and not a constant. Both
 * are tried; the one that answered is named.
 */
/*
 * The head of the last received frame, as hex.
 *
 * Sixty-four bytes is an Ethernet header, an IPv4 header and a TCP header through the checksum -
 * which is the whole question when a forwarded frame is being examined, and deliberately not enough
 * to be a packet capture. It is taken before the tag lookup, so it shows frames this driver then
 * drops as untagged, which is exactly the case this exists for.
 */
static int
octep_sysctl_dp_rx_frame(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	uint8_t f[sizeof(sc->dp_rx_frame)];
	struct sbuf *sb;
	uint32_t n, i;
	int error;

	sb = sbuf_new_for_sysctl(NULL, NULL, 512, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);

	mtx_lock(&sc->mtx);
	memcpy(f, sc->dp_rx_frame, sizeof(f));
	n = sc->dp_rx_frame_len;
	mtx_unlock(&sc->mtx);

	if (n == 0) {
		sbuf_cat(sb, "no frame captured yet\n");
		goto out;
	}
	for (i = 0; i < n; i++)
		sbuf_printf(sb, "%02x%s", f[i], (i % 16) == 15 ? "\n" : " ");
	if ((n % 16) != 0)
		sbuf_cat(sb, "\n");
out:
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/*
 * The head of the last frame dropped for an unknown tag, which is how a forwarded frame is read.
 */
static int
octep_sysctl_dp_rx_untag_frame(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	uint8_t f[sizeof(sc->dp_rx_untag_frame)];
	struct sbuf *sb;
	uint32_t n, i;
	uint16_t tg;
	int error;

	sb = sbuf_new_for_sysctl(NULL, NULL, 512, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);

	mtx_lock(&sc->mtx);
	memcpy(f, sc->dp_rx_untag_frame, sizeof(f));
	n = sc->dp_rx_untag_len;
	tg = sc->dp_rx_untag_tag;
	mtx_unlock(&sc->mtx);

	if (n == 0) {
		sbuf_cat(sb, "no frame with an unknown tag has arrived\n");
		goto out;
	}
	sbuf_printf(sb, "tag 0x%04x\n", tg);
	for (i = 0; i < n; i++)
		sbuf_printf(sb, "%02x%s", f[i], (i % 16) == 15 ? "\n" : " ");
	if ((n % 16) != 0)
		sbuf_cat(sb, "\n");
out:
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/*
 * The head of the last packet that used more than one receive buffer, with what it was.
 *
 * From the start of its first buffer: the length word, the response word, the tag, the sixty-four
 * metadata bytes, and then whatever the packet is - for a control message its fourteen bytes of
 * Ethernet header and its own four-byte header, whose third byte is the type.
 */
/*
 * The stalls the watchdog counted, newest last: when (ticks ago), the ring, what the first pass
 * took, how long nobody had been inside, what the whole visit took.
 */
static int
octep_sysctl_dp_stall_log(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct octep_dp_stall st[OCTEP_DP_STALL_LOG];
	struct sbuf *sb;
	uint32_t i, n, pos;
	int error, now;

	sb = sbuf_new_for_sysctl(NULL, NULL, 1024, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);
	/* arg2 1 is the look's log, 0 the watchdog's stalls. */
	memcpy(st, arg2 != 0 ? sc->dp_look_log : sc->dp_stall_log, sizeof(st));
	pos = arg2 != 0 ? sc->dp_look_pos : sc->dp_stall_pos;
	now = ticks;
	n = pos < OCTEP_DP_STALL_LOG ? pos : OCTEP_DP_STALL_LOG;
	if (n == 0)
		sbuf_cat(sb, arg2 != 0 ? "the look has visited no ring\n" : "no stall has been counted\n");
	for (i = 0; i < n; i++) {
		const struct octep_dp_stall *s = &st[(pos - n + i) % OCTEP_DP_STALL_LOG];

		sbuf_printf(sb, "%u s ago  ring %u  first pass %u  idle %u ticks  visit %u  before it: int_status 0x%jx  cnts 0x%jx  levels 0x%jx  vector %u mask %u pending %u handler entries %ju\n",
		    (uint32_t)(now - s->at) / (uint32_t)hz, s->ring, s->first, s->idle, s->visit,
		    (uintmax_t)s->istat, (uintmax_t)s->cnts, (uintmax_t)s->levels,
		    s->vec_index, s->msix_mask, s->msix_pending, (uintmax_t)s->vec_count);
		sbuf_printf(sb, "    as the visit before left it, %u ticks earlier: int_status 0x%jx  cnts 0x%jx  "
		    "handler entries %ju\n", s->left_ago, (uintmax_t)s->left_istat, (uintmax_t)s->left_cnts,
		    (uintmax_t)s->left_vcount);
		sbuf_printf(sb, "    its MSI-X entry: address 0x%jx data 0x%x, %s",
		    (uintmax_t)s->msix_addr, s->msix_data,
		    s->msix_match == 1 ? "the kernel's" : s->msix_match == 0 ? "NOT the kernel's" : "not compared");
		if (s->wake != 0)
			sbuf_printf(sb, "; woken with %u: pending %u handler entries %ju",
			    s->wake, s->wake_pending, (uintmax_t)s->wake_vcount);
		sbuf_cat(sb, "\n");
	}
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

static int
octep_sysctl_dp_rx_span_frame(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	uint8_t f[sizeof(sc->dp_rx_span_frame)];
	struct sbuf *sb;
	uint32_t i, len, ring;
	uint16_t tg;
	int error;

	sb = sbuf_new_for_sysctl(NULL, NULL, 768, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);

	memcpy(f, sc->dp_rx_span_frame, sizeof(f));
	len = sc->dp_rx_span_len;
	ring = sc->dp_rx_span_ring;
	tg = sc->dp_rx_span_tag;

	if (len == 0) {
		sbuf_cat(sb, "no packet has used more than one buffer\n");
		goto out;
	}
	sbuf_printf(sb, "ring %u  tag 0x%04x  length word %u  %u buffers\n", ring, tg, len,
	    octep_dp_oq_span(len, OCTEP_DP_OQ_DESCS));
	for (i = 0; i < sizeof(f); i++)
		sbuf_printf(sb, "%02x%s", f[i], (i % 16) == 15 ? "\n" : " ");
out:
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

static int
octep_sysctl_dp_pf_state(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct octep_pf_tuple t;
	struct octep_pf_state st;
	struct sbuf *sb;
	uint64_t seq, pseq;
	uint32_t slot, slotrev;
	int error;

	sb = sbuf_new_for_sysctl(NULL, NULL, 512, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);

	if (!octep_pf_present()) {
		sbuf_cat(sb, "pf is not loaded, so there is nothing to ask. The driver carries no\n"
		    "dependency on it: the lookups are weak symbols and resolve to zero.\n");
		goto out;
	}

	mtx_lock(&sc->mtx);
	t = sc->dp_rx_tuple;
	seq = sc->dp_rx_tuple_seq;
	pseq = sc->dp_rx_prefix_seq;
	slot = sc->dp_rx_slot;
	slotrev = sc->dp_rx_slot_rev;
	mtx_unlock(&sc->mtx);

	if (seq == 0) {
		sbuf_cat(sb, "no punted frame has been parsed yet. Only IPv4 is read; a frame that is\n"
		    "not IPv4, or is shorter than a header, leaves this empty.\n");
		goto out;
	}
	if (seq != pseq) {
		sbuf_printf(sb, "the last frame parsed (%ju) is not the last frame received (%ju), so\n"
		    "its tuple does not belong with the slot dp.rx_prefix is showing.\n",
		    (uintmax_t)seq, (uintmax_t)pseq);
		goto out;
	}

	sbuf_printf(sb, "frame %ju  proto %u  %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u\n",
	    (uintmax_t)seq, t.proto,
	    t.sip & 0xff, (t.sip >> 8) & 0xff, (t.sip >> 16) & 0xff, (t.sip >> 24) & 0xff,
	    ntohs(t.sport),
	    t.dip & 0xff, (t.dip >> 8) & 0xff, (t.dip >> 16) & 0xff, (t.dip >> 24) & 0xff,
	    ntohs(t.dport));
	/*
	 * The slot on the same line as the tuple, from the same frame, because the two are only
	 * useful together and reading them from two sysctls is a race that produced a measurement
	 * full of noise before it was noticed.
	 */
	sbuf_printf(sb, "slot %u  rev %u\n", slot, slotrev);

	if (!octep_pf_state_read(&t, &st)) {
		sbuf_cat(sb, "pf has no state for it, in either index order.\n");
		goto out;
	}

	sbuf_printf(sb, "pf has a state, matched %s\n",
	    st.order == 0 ? "as read off the wire" :
	    st.order == 1 ? "with the addresses reversed" :
	    st.order == 2 ? "with the ports exchanged" :
	    "with the addresses reversed and the ports exchanged");
	sbuf_printf(sb, "  direction %s  timeout %u  flags 0x%04x\n",
	    st.direction == 1 ? "in" : "out", st.timeout, st.state_flags);
	sbuf_printf(sb, "  peer states  src %u  dst %u\n", st.src_state, st.dst_state);
	sbuf_printf(sb, "  interface %s\n", st.ifname);

	/*
	 * Both of pf's keys, and the translation if the two differ.
	 *
	 * Printed as the raw 32-bit values rather than as dotted quads, because that is the form
	 * rpc.conn_nat_src and its five neighbours want: the sysctls take network order, which is
	 * what both the frame and pf hold, so a number read here can be written there unchanged. A
	 * dotted quad would have to be converted by whoever read it, and converted the wrong way
	 * half the time.
	 */
	sbuf_printf(sb, "  wire   0x%08x:%u  0x%08x:%u\n",
	    st.wire_addr[0], ntohs(st.wire_port[0]), st.wire_addr[1], ntohs(st.wire_port[1]));
	sbuf_printf(sb, "  stack  0x%08x:%u  0x%08x:%u\n",
	    st.stack_addr[0], ntohs(st.stack_port[0]), st.stack_addr[1], ntohs(st.stack_port[1]));
	if (!st.nat_valid) {
		sbuf_cat(sb, "  not translated\n");
		goto out;
	}

	/*
	 * The NAT block as the connection sees it, which is the form rpc.conn_* takes. Printed as
	 * the raw network-order words so a number read here goes into the sysctl unchanged - and
	 * printed at all because this derivation is the part that was wrong for four days.
	 */
	sbuf_printf(sb, "  translated:%s%s; this frame is the %s direction\n",
	    st.nat_snat ? " do_snat (the opener is rewritten)" : "",
	    st.nat_dnat ? " do_dnat (the responder is rewritten)" : "",
	    st.original ? "original" : "reply");
	sbuf_printf(sb, "    conn_orig_src 0x%08x  conn_orig_sport %u\n",
	    st.orig_src, ntohs(st.orig_sport));
	sbuf_printf(sb, "    conn_orig_dst 0x%08x  conn_orig_dport %u\n",
	    st.orig_dst, ntohs(st.orig_dport));
	sbuf_printf(sb, "    conn_nat_src  0x%08x  conn_nat_sport  %u\n",
	    st.nat_src, ntohs(st.nat_sport));
	sbuf_printf(sb, "    conn_nat_dst  0x%08x  conn_nat_dport  %u\n",
	    st.nat_dst, ntohs(st.nat_dport));
out:
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/*
 * The connection table: find a connection by either of its tuples, allocate an index, give it back.
 *
 * Index 0 is never handed out. The hand-programmed experiments that found all of this used index 1
 * and the bring-up uses 0 for its own next hop, so starting at 2 keeps a connection this driver
 * made automatically from colliding with either - and a collision there would look exactly like
 * the fast path misbehaving, which is a week nobody needs twice.
 *
 * Called with the softc lock held.
 */
/* Which chain a tuple hangs in: the top dp_conn_hbits bits of the mix dp_conn_hot takes sixteen of. */
static __inline uint32_t
octep_conn_bucket(const struct octep_softc *sc, const struct octep_pf_tuple *t)
{

	return (octep_conn_hash32(t) >> (32 - sc->dp_conn_hbits));
}

static struct octep_conn *
octep_conn_find(struct octep_softc *sc, const struct octep_pf_tuple *t, int *dir)
{
	struct octep_conn *c;
	uint32_t e;
	int d;

	/*
	 * One chain, the one this tuple hashes to: every tuple in it is a live connection's, because
	 * a connection is hung in when it is made and taken down before it is freed.
	 */
	for (e = sc->dp_conn_head[octep_conn_bucket(sc, t)]; e != 0; e = c->hnext[d]) {
		c = &sc->dp_conn[(e - 1) >> 1];
		d = (int)((e - 1) & 1);
		if (octep_tuple_eq(&c->tuple[d], t)) {
			if (dir != NULL)
				*dir = d;
			return (c);
		}
	}
	return (NULL);
}

/*
 * Hang a connection's two tuples in their chains, and take them down. The reply's goes in first
 * so the opener's sits nearer the head: a connection whose two tuples are the same is found as
 * its opener, as the search of the whole array found it.
 */
static void
octep_conn_link(struct octep_softc *sc, struct octep_conn *c)
{
	uint32_t h;
	int d;

	for (d = 1; d >= 0; d--) {
		h = octep_conn_bucket(sc, &c->tuple[d]);
		c->hnext[d] = sc->dp_conn_head[h];
		sc->dp_conn_head[h] = ((c->idx << 1) | (uint32_t)d) + 1;
	}
}

static void
octep_conn_unlink(struct octep_softc *sc, struct octep_conn *c)
{
	uint32_t *p, e, me;
	int d;

	for (d = 0; d < 2; d++) {
		me = ((c->idx << 1) | (uint32_t)d) + 1;
		for (p = &sc->dp_conn_head[octep_conn_bucket(sc, &c->tuple[d])]; (e = *p) != 0;
		    p = &sc->dp_conn[(e - 1) >> 1].hnext[(e - 1) & 1]) {
			if (e == me) {
				*p = c->hnext[d];
				break;
			}
		}
		c->hnext[d] = 0;
	}
}

/* Every index from 2 up as not in use, the lowest on top. */
static void
octep_conn_free_fill(struct octep_softc *sc)
{
	uint32_t i;

	sc->dp_conn_nfree = 0;
	for (i = sc->dp_conn_cap; i > 2; i--)
		sc->dp_conn_free[sc->dp_conn_nfree++] = i - 1;
}

/*
 * The two ingress tuples of a connection, from pf's oriented state.
 *
 * pf names the opener and the responder, each as the host sees it and as the wire sees it. A frame
 * from the opener arrives carrying the opener's own address towards the responder's wire address;
 * a frame from the responder arrives carrying the responder's own address towards the opener's
 * wire address. That is what the fast path's two microflows will be keyed on, because frames are
 * punted before translation - and it is derived from pf's naming, not read from any vendor line,
 * so octep_dp_flow_make checks it against the frame in hand and counts a disagreement.
 */
static void
octep_conn_tuples(const struct octep_pf_state *st, const struct octep_pf_tuple *like,
    struct octep_pf_tuple tup[2])
{

	tup[0] = *like;
	tup[0].sip = st->orig_src;
	tup[0].sport = st->orig_sport;
	tup[0].dip = st->nat_dst;
	tup[0].dport = st->nat_dport;
	tup[1] = *like;
	tup[1].sip = st->orig_dst;
	tup[1].sport = st->orig_dport;
	tup[1].dip = st->nat_src;
	tup[1].dport = st->nat_sport;
	/*
	 * IPv6, which is never translated here: each direction's addresses are the two ends in full,
	 * and the 32-bit fields above already hold their folds.
	 */
	if (st->af == AF_INET6) {
		memcpy(tup[0].sip6, st->orig_src6, 16);
		memcpy(tup[0].dip6, st->orig_dst6, 16);
		memcpy(tup[1].sip6, st->orig_dst6, 16);
		memcpy(tup[1].dip6, st->orig_src6, 16);
	}
	/*
	 * An echo has no ports and pf gives it two - the message's id and its type - and a frame
	 * is read with the id where a source port would be whichever way it travels: see
	 * octep_dp_rx_tuple. A port belongs to its address and changes ends with it; the id does
	 * not. So the reply's two words are the translated id and the type as they stand, where
	 * the lines above exchange them. Exchanged, every ping read from its reply was counted
	 * a tuple mismatch, and one read from its request looked for a reply that no frame is
	 * stored as.
	 */
	if (like->proto == IPPROTO_ICMP) {
		tup[1].sport = st->nat_sport;
		tup[1].dport = st->orig_dport;
	}
}

/*
 * A connection entry's TCP block from pf's numbers (issue #679): each direction's td_end is its
 * peer's seqlo, td_maxend its seqhi, td_maxwin its max_win scaled, and td_maxack the other peer's
 * seqlo - the highest acknowledgement it can have sent. The window scales are pf's, and pf applies
 * them only when both ends offered the option (pf_tcp_track_full), so both or neither. The check
 * goes on, with both liberal bits set: see OCTEP_CONN_OFF_TCP for why. The retransmission and FIN
 * fields start at zero, as the vendor's host starts them.
 */
#define	OCTEP_PF_WSCALE_MASK	0x0f	/* netpfil/pf/pf.h's PF_WSCALE_MASK; octep_pf.c owns pfvar.h */
static void
octep_tcp_scales(const struct octep_pf_state *st, uint8_t *s)
{

	if (st->wscale[0] != 0 && st->wscale[1] != 0) {
		s[0] = st->wscale[0] & OCTEP_PF_WSCALE_MASK;
		s[1] = st->wscale[1] & OCTEP_PF_WSCALE_MASK;
	} else
		s[0] = s[1] = 0;
}

static void
octep_tcp_from_pf(uint8_t *b, const struct octep_pf_state *st)
{
	uint8_t s[2];
	int k;

	memset(b, 0, OCTEP_CONN_TCP_LEN);
	octep_tcp_scales(st, s);
	for (k = 0; k < 2; k++) {
		le32enc(b + OCTEP_TCP_SEEN(k), st->seqlo[k]);
		le32enc(b + OCTEP_TCP_SEEN(k) + 4, st->seqhi[k]);
		le32enc(b + OCTEP_TCP_SEEN(k) + 8, (uint32_t)st->max_win[k] << s[k]);
		le32enc(b + OCTEP_TCP_SEEN(k) + 12, st->seqlo[k ^ 1]);
	}
	le32enc(b + OCTEP_TCP_OFF_FLAGS, OCTEP_TCP_SEQ_CHK_ON | OCTEP_TCP_LIBERAL_ORIG |
	    OCTEP_TCP_LIBERAL_REPLY | (s[1] << OCTEP_TCP_SCALE_REPLY_SHIFT) |
	    (s[0] << OCTEP_TCP_SCALE_ORIG_SHIFT));
}

/*
 * A connection entry's TCP block from the latest segment the host saw in each direction, s[0] the
 * opener's (issue #679): for a connection whose pf numbers have stopped - it was in hardware
 * before, pf is sloppy for it and tracks nothing - when it is made again, or revived after the
 * fast path gave it back, with dp.tcp_revive 2: an instrument, not the default, for the false
 * drops measured below and in octep_conn_probe. Those segments are what crossed the host last, so the window they make is
 * the one the connection is at now: td_end a direction's sequence plus its payload, td_maxend the
 * other direction's acknowledgement plus its window, td_maxwin its own window, td_maxack its own
 * acknowledgement - Linux conntrack's numbers after one segment each way, which is what the fast
 * path's check is. The window scales are the ones pf recorded at the handshake. Both segments must
 * acknowledge and neither may be a SYN; otherwise nothing is made and 0 returned.
 *
 * And each window gets room on top: twice itself or OCTEP_TCP_SEG_ROOM, at most
 * OCTEP_TCP_SEG_ROOM_MAX - the shape of the room pf is given back at a take-back
 * (octep_pf_handback), with a larger floor. The host goes on forwarding for a moment after the
 * segments are read: the frames already in its receive rings - a ring of 4,096 full frames is six
 * megabytes - and acknowledgements that open the sender's window further, so the first segments the
 * fast path sees can be megabytes past the ones read. The check's bounds that use these numbers
 * (conn_tcp_seq_ok in the vendor's conn_common.c) are a sender's td_maxend for its sequence and the
 * other end's td_end plus or minus the sender's td_maxwin for its acknowledgement; and once one
 * segment each way has passed them, nothing outside them passes again. Measured on the XGS 3300,
 * four streams down at 9.7 to 9.9 Gbit/s, ordinary segments the check dropped in a run of 20
 * seconds: 233 with no room (135 windows made); 5, 310 and 43 with a floor of 4 MiB (168, 136 and
 * 129); 0, 19 and 233 with 4 MiB and fresh segments only (40, 99 and 22); none in five runs with
 * 16 MiB (43 to 116 windows each). The room leaves a sender's sequence bounded where the injected
 * ones were: a window and 16 MiB past the acknowledgement, against 64 MiB.
 *
 * A window made this way and not from the fast path's own numbers, because those stop where the
 * fast path let the connection go, and the host forwarded it after that: measured - see
 * octep_conn_probe - a revival from them dropped ordinary segments.
 */
#define	OCTEP_TH_FIN	0x01
#define	OCTEP_TH_SYN	0x02
#define	OCTEP_TH_ACK	0x10
#define	OCTEP_TCP_SEG_ROOM	(16u << 20)
#define	OCTEP_TCP_SEG_ROOM_MAX	(1u << 28)
static int
octep_tcp_from_segs(uint8_t *b, const uint8_t *scale, const struct octep_tcp_seg *s)
{
	uint32_t w[2], room[2];
	int k;

	for (k = 0; k < 2; k++)
		if (!s[k].valid || (s[k].flags & OCTEP_TH_ACK) == 0 ||
		    (s[k].flags & OCTEP_TH_SYN) != 0)
			return (0);
	memset(b, 0, OCTEP_CONN_TCP_LEN);
	for (k = 0; k < 2; k++) {
		w[k] = (uint32_t)s[k].win << scale[k];
		if (w[k] == 0)
			w[k] = 1;
		room[k] = w[k] < OCTEP_TCP_SEG_ROOM_MAX / 2 ? 2 * w[k] : OCTEP_TCP_SEG_ROOM_MAX;
		if (room[k] < OCTEP_TCP_SEG_ROOM)
			room[k] = OCTEP_TCP_SEG_ROOM;
	}
	for (k = 0; k < 2; k++) {
		le32enc(b + OCTEP_TCP_SEEN(k), s[k].seq + s[k].len +
		    ((s[k].flags & OCTEP_TH_FIN) != 0 ? 1 : 0));
		le32enc(b + OCTEP_TCP_SEEN(k) + 4, s[k ^ 1].ack + w[k ^ 1] + room[k ^ 1]);
		le32enc(b + OCTEP_TCP_SEEN(k) + 8, w[k] + room[k]);
		le32enc(b + OCTEP_TCP_SEEN(k) + 12, s[k].ack);
	}
	le32enc(b + OCTEP_TCP_OFF_FLAGS, OCTEP_TCP_SEQ_CHK_ON | OCTEP_TCP_LIBERAL_ORIG |
	    OCTEP_TCP_LIBERAL_REPLY | ((uint32_t)scale[1] << OCTEP_TCP_SCALE_REPLY_SHIFT) |
	    ((uint32_t)scale[0] << OCTEP_TCP_SCALE_ORIG_SHIFT));
	return (1);
}

/*
 * The block a revival sends back with dp.tcp_revive 1, an instrument (see octep_conn_probe for why
 * it is not the default): the window the fast path answered CONN_RECLAIM_FP with a moment
 * ago, in place of the one the connection was made with, its scales and flags kept, the check on,
 * both liberal bits set again and the retransmission count at zero. The count is what gave the
 * connection back - sent back at its limit, the next identical segment would give it back again
 * at once - and the liberal bits cover the frames the host forwarded since.
 *
 * The reclaim and not LO_CONN_READ, measured: on the XGS 3300 a table read of a connection in the
 * middle of a 9 Gbit/s stream showed the numbers it was made with, both liberal bits still set,
 * the same twice three seconds apart - and the reclaim of the same kind of connection answered with
 * numbers a gigabyte on. The table is not where the fast path keeps a connection's window while it
 * forwards it; the reclaim gives the window back. A connection more than the liberal span past the
 * numbers it is sent with would have every segment refused, so a revival from the table could kill
 * the connection it revives.
 */
static void
octep_tcp_rearm(uint8_t *b, const uint8_t *seq)
{

	memcpy(b, seq, OCTEP_TCP_SEQ_LEN);
	b[OCTEP_TCP_OFF_RETRANS] = 0;
	le32enc(b + OCTEP_TCP_OFF_FLAGS, le32dec(b + OCTEP_TCP_OFF_FLAGS) | OCTEP_TCP_SEQ_CHK_ON |
	    OCTEP_TCP_LIBERAL_ORIG | OCTEP_TCP_LIBERAL_REPLY);
}

/*
 * The live user (dp.live_users) either end of this tuple has, 0 for none, and in *side whether the
 * client is the end that opened it (0) or the one it was opened to (1). Softc lock held.
 */
static uint16_t
octep_lu_of(const struct octep_softc *sc, const struct octep_pf_tuple *t, uint8_t *side)
{
	const struct octep_live_user *u;
	int i;

	for (i = 0; i < sc->dp_nlu; i++) {
		u = &sc->dp_lu[i];
		if (u->kind == 4 && t->af == AF_INET &&
		    (ntohl(t->sip) == u->a4 || ntohl(t->dip) == u->a4)) {
			*side = ntohl(t->sip) == u->a4 ? 0 : 1;
			return (u->luid);
		}
		if (u->kind == 6 && t->af == AF_INET6 &&
		    (memcmp(t->sip6, u->a6, sizeof(u->a6)) == 0 ||
		    memcmp(t->dip6, u->a6, sizeof(u->a6)) == 0)) {
			*side = memcmp(t->sip6, u->a6, sizeof(u->a6)) == 0 ? 0 : 1;
			return (u->luid);
		}
	}
	return (0);
}

/* Is this user listed now? A connection made with one that is not is not revived. Lock held. */
static int
octep_lu_listed(struct octep_softc *sc, uint16_t luid)
{

	return (octep_lu_find(sc->dp_lu, sc->dp_nlu, luid) != NULL);
}

static struct octep_conn *
octep_conn_alloc(struct octep_softc *sc, const struct octep_pf_state *st,
    const struct octep_pf_tuple tup[2], int d, uint32_t slot, uint32_t rev, int in_dif,
    uint16_t in_tag, const uint8_t *tcp, int tcp_able)
{
	struct octep_conn *c;
	uint32_t i;

	mtx_assert(&sc->mtx, MA_OWNED);
	while (sc->dp_conn_nfree > 0) {
		i = sc->dp_conn_free[--sc->dp_conn_nfree];
		/* Above a bound the board has lowered: never handed out, so dropped from the stack. */
		if (i < 2 || i >= sc->dp_conn_max)
			continue;
		c = &sc->dp_conn[i];
		if (c->used)
			continue;
		/*
		 * The revision outlives the entry: every allocation of this index carries the next
		 * one, so a command meant for the connection that used to be here is refused by
		 * the far side rather than applied to the one that is.
		 */
		c->conn_rev = (uint16_t)(c->conn_rev + 1);
		if (c->conn_rev == 0)
			c->conn_rev = 1;
		c->used = 1;
		c->idx = i;
		/* Its live user, if either end is a logged-in client (dp.live_users). */
		c->lu_side = 0;
		c->luid = octep_lu_of(sc, &tup[0], &c->lu_side);
		if (c->luid != 0)
			sc->dp_lu_made++;
		/* hw_raw is the slot's, and stays: see octep_hw_diff */
		memset(c->hw_pkts, 0, sizeof(c->hw_pkts));
		memset(c->hw_bytes, 0, sizeof(c->hw_bytes));
		memset(c->hw_pf_pkts, 0, sizeof(c->hw_pf_pkts));
		memset(c->hw_pf_bytes, 0, sizeof(c->hw_pf_bytes));
		c->hw_moved = 0;
		c->hw_seen = 0;
		c->probe_dir = (uint8_t)d;
		c->pf_dir = st->lookup_dir;
		c->ipsec = 0;
		c->enc_dir = 0;
		c->bridged = 0;
		c->doomed = 0;
		c->dropped = 0;
		c->ips = 0;
		c->ips_req = 0;
		c->ips_bytes = 0;
		c->tuple[0] = tup[0];
		c->tuple[1] = tup[1];
		c->nat.snat = st->nat_snat;
		c->nat.dnat = st->nat_dnat;
		c->nat.orig_src = st->orig_src;
		c->nat.orig_dst = st->orig_dst;
		c->nat.nat_src = st->nat_src;
		c->nat.nat_dst = st->nat_dst;
		c->nat.orig_sport = st->orig_sport;
		c->nat.orig_dport = st->orig_dport;
		c->nat.nat_sport = st->nat_sport;
		c->nat.nat_dport = st->nat_dport;
		c->tcp_on = (tcp != NULL);
		c->tcp_able = (uint8_t)(tcp != NULL || tcp_able);
		c->tcp_wait = 0;
		if (tcp != NULL)
			memcpy(c->tcp, tcp, OCTEP_CONN_TCP_LEN);
		else
			memset(c->tcp, 0, OCTEP_CONN_TCP_LEN);
		octep_tcp_scales(st, c->tcp_scale);
		memset(c->mf, 0, sizeof(c->mf));
		c->mf[0].in_dif = -1;
		c->mf[1].in_dif = -1;
		c->mf[d].slot = slot;
		c->mf[d].rev = rev;
		c->mf[d].in_dif = in_dif;
		c->mf[d].in_tag = in_tag;
		c->closing = 0;
		/*
		 * Not asked about for one gap. The frames that were already on their way to the
		 * host when the connection was made arrive under the identity it has just
		 * programmed, and they are not a hand-back.
		 */
		c->probe_tick = ticks;
		c->rv_tick = ticks - hz;
		c->rv_wait = 0;
		c->rv_first = ticks;
		c->rv_streak = 0;
		c->ka_time = time_uptime;
		sc->dp_conn_hot[octep_conn_hot_slot(&c->tuple[0])]++;
		sc->dp_conn_hot[octep_conn_hot_slot(&c->tuple[1])]++;
		octep_conn_link(sc, c);
		c->live_pos = sc->dp_conn_nlive;
		sc->dp_conn_live[sc->dp_conn_nlive++] = i;
		sc->dp_conn_used++;
		return (c);
	}
	sc->dp_auto_full++;
	return (NULL);
}

static __inline int
octep_tuple_same(const struct octep_pf_tuple *a, const struct octep_pf_tuple *b)
{

	return (octep_tuple_eq(a, b));
}

CTASSERT(OCTEP_FLOW_CAND_SLOTS % 64 == 0);	/* dp_fast_make_held is one bit a slot, in words */

/* Whether a candidate slot is the poll's until it has been round, and making it so. */
static __inline int
octep_cand_held(const struct octep_softc *sc, uint32_t s)
{

	return ((sc->dp_fast_make_held[s >> 6] >> (s & 63)) & 1);
}

static __inline void
octep_cand_hold(struct octep_softc *sc, uint32_t s)
{

	atomic_set_64(&sc->dp_fast_make_held[s >> 6], 1ull << (s & 63));
}
/* The softc's comment on the table says what a slot weighs. */
/* A whole number of cache lines: six since the tuple carries IPv6 addresses (2026-10-18). */
CTASSERT(sizeof(struct octep_flow_cand_slot) % CACHE_LINE_SIZE == 0);

/*
 * The candidate table's slot for a tuple: one hash, used by the writer that fills a slot and by
 * the reader that asks whether a tuple has been seen, so the two can never disagree.
 */
static uint32_t
octep_flow_cand_slot(const struct octep_pf_tuple *t)
{
	uint32_t h;

	h = t->sip ^ ((t->dip << 13) | (t->dip >> 19));
	h ^= ((uint32_t)t->sport << 16) | (uint32_t)t->dport;
	h ^= (uint32_t)t->proto;
	h *= 0x9e3779b1u;
	/*
	 * The top bits of the product, which every bit below them has reached. This read
	 * `(h >> 16) % 64` until it was measured: bits 16 to 21, into which a
	 * multiply carries nothing from above. The source port's second byte on the wire - the one
	 * that changes from one connection to the next - sits in bits 24 to 31 and never reached
	 * the index, so every connection from one host to one server in a run of 256 ports shared
	 * one slot for its opening direction. Four downloads at once were made one a second, each
	 * poll finding one of the four in the slot they shared.
	 */
	return ((uint32_t)(((uint64_t)h * OCTEP_FLOW_CAND_SLOTS) >> 32));
}

/*
 * A slot's busy word: tried, never waited for, and held inside a critical section by every holder
 * - the receive path's interrupt threads and its watchdog's callout, the drain, the reader of the
 * other direction, dp.cand_table. A holder preempted inside the word would keep every ring from
 * writing any tuple whose hash chooses the slot until it ran again; what is done inside is a few
 * loads and stores. Nothing sleeps, and no mutex is taken or let go, while the word is held.
 */
static __inline int
octep_flow_cand_word_try(struct octep_flow_cand_slot *cs)
{

	critical_enter();
	if (atomic_cmpset_acq_32(&cs->busy, 0, 1) != 0)
		return (1);
	critical_exit();
	return (0);
}

static __inline void
octep_flow_cand_word_drop(struct octep_flow_cand_slot *cs)
{

	atomic_store_rel_32(&cs->busy, 0);
	critical_exit();
}

/*
 * What the candidate table knows about a tuple: the microflow identity its last punted frame
 * carried, and the front port it arrived on. Returns 1 when a place of the tuple's slot holds
 * this tuple with an identity - written, when the caller gives an age, no longer ago than that.
 *
 * This is how the second direction of a connection is known at the moment the first is programmed:
 * every punted frame leaves a candidate behind, and a busy connection has had frames punted both
 * ways within the last second. Reading does not consume the candidate - seen and stamp are left
 * alone - so the drain still sees it, finds the connection already has that direction, and counts
 * it known.
 *
 * The same one-word lock the writers take, tried a few times and never waited for: the writers
 * hold it for a handful of stores. A word that stays taken is a miss, and a miss is not free. The
 * caller that is making a connection refuses it for want of its other direction, and when that
 * caller is a run between polls the slot of the direction in hand - its four tuples - is then
 * left to the poll, up to a second. It was tried once. The tuple is compared, not trusted to its
 * slot: a slot is a hash position, and other tuples sit in it.
 *
 * The age, in seconds and 0 for none, is dp.cand_age where a connection is being made and nothing
 * where a direction is being attached again, which compares the identity with the one it has. A
 * place keeps its tuple until the slot needs the room - on a quiet table a long time - and the
 * identity in it is the fast path's to expire.
 *
 * Never called from the receive path.
 */
static int
octep_flow_cand_ident(struct octep_softc *sc, const struct octep_pf_tuple *t, u_int age,
    uint32_t *slot, uint32_t *rev, int *in_dif, uint16_t *in_tag, uint16_t *sa, uint16_t *sa_rev,
    uint8_t *closing, uint8_t *mac, uint16_t *vlan)
{
	struct octep_flow_cand_slot *cs;
	struct octep_flow_cand *e;
	int i, w, hit = 0;

	cs = &sc->dp_cand[octep_flow_cand_slot(t)];
	for (i = 1; !octep_flow_cand_word_try(cs); i++) {
		if (i >= OCTEP_CAND_TRIES)
			return (0);
		cpu_spinwait();
	}
	for (w = 0; w < OCTEP_FLOW_CAND_WAYS; w++) {
		e = &cs->way[w];
		if (e->stamp == 0 || e->slot == 0 || !octep_tuple_same(&e->tuple, t))
			continue;
		if (age != 0 && (u_int)ticks - (u_int)e->when > age * (u_int)hz) {
			sc->dp_cand_aged++;
			break;
		}
		*slot = e->slot;
		*rev = e->rev;
		*in_dif = e->in_dif;
		*in_tag = e->in_tag;
		*sa = e->sa;
		*sa_rev = e->sa_rev;
		/* A FIN or RST of that tuple the reader has not taken yet: it is ending. */
		if (closing != NULL)
			*closing = e->closing;
		if (mac != NULL)
			memcpy(mac, e->mac, sizeof(e->mac));
		if (vlan != NULL)
			*vlan = e->vlan;
		hit = 1;
		break;
	}
	octep_flow_cand_word_drop(cs);
	return (hit);
}

/*
 * The latest TCP segment the host saw of a tuple, when its candidate holds one written in the last
 * OCTEP_TCP_SEG_AGE and, with since_on, after the tick since (issue #679): what a connection's
 * window is made from when pf's numbers for it have stopped. Returns OCTEP_SEG_HIT with it in *seg,
 * or why there is none; *age is how many ticks ago the tuple's latest frame was written, when there
 * is one.
 *
 * Fresh, and for a revival newer than the revival before it: a connection given back a few times a
 * second has, in the direction that has not crossed the host yet this time, a latest segment from
 * the give-back before - hundreds of megabytes behind at these rates. Measured: a revival found the
 * opener's latest segment 263 ms old. That alone did not end the false drops; the room did (see
 * octep_tcp_from_segs).
 */
#define	OCTEP_TCP_SEG_AGE	(hz / 20 > 0 ? hz / 20 : 1)
#define	OCTEP_TCP_SEG_WAIT	(hz / 5)	/* how long a revival waits for them: octep_conn_probe */
#define	OCTEP_SEG_HIT		0
#define	OCTEP_SEG_BUSY		1
#define	OCTEP_SEG_NONE		2
#define	OCTEP_SEG_NOTCP		3
#define	OCTEP_SEG_OLD		4
static const char *const octep_seg_why[] = {
	"", "its candidate was being written", "no frame of it reached the host lately",
	"its latest frame was no whole TCP segment",
	"its latest segment is older than 50 ms or than the last revival"
};
static int
octep_flow_cand_seg(struct octep_softc *sc, const struct octep_pf_tuple *t, struct octep_tcp_seg *seg,
    u_int *age, int since, int since_on)
{
	struct octep_flow_cand_slot *cs;
	struct octep_flow_cand *e;
	int i, w, r = OCTEP_SEG_NONE;

	*age = 0;
	cs = &sc->dp_cand[octep_flow_cand_slot(t)];
	for (i = 1; !octep_flow_cand_word_try(cs); i++) {
		if (i >= OCTEP_CAND_TRIES)
			return (OCTEP_SEG_BUSY);
		cpu_spinwait();
	}
	for (w = 0; w < OCTEP_FLOW_CAND_WAYS; w++) {
		e = &cs->way[w];
		if (e->stamp == 0 || !octep_tuple_same(&e->tuple, t))
			continue;
		*age = (u_int)ticks - (u_int)e->when;
		if (!e->seg.valid)
			r = OCTEP_SEG_NOTCP;
		else if (*age > (u_int)OCTEP_TCP_SEG_AGE ||
		    (since_on && (int)((u_int)e->when - (u_int)since) <= 0))
			r = OCTEP_SEG_OLD;
		else {
			*seg = e->seg;
			r = OCTEP_SEG_HIT;
		}
		break;
	}
	octep_flow_cand_word_drop(cs);
	return (r);
}

/*
 * A connection's TCP block from both directions' latest segments, tup[0] the opener's: 1 when it
 * was made, and 0 with why not in why - written to dp.tcp_segs_last by the caller. since and
 * since_on as octep_flow_cand_seg takes them.
 */
static int
octep_tcp_segs(struct octep_softc *sc, const struct octep_pf_tuple *tup, const uint8_t *scale,
    uint8_t *b, char *why, size_t len, int since, int since_on)
{
	struct octep_tcp_seg sg[2];
	u_int age;
	int k, r;

	for (k = 0; k < 2; k++) {
		r = octep_flow_cand_seg(sc, &tup[k], &sg[k], &age, since, since_on);
		if (r != OCTEP_SEG_HIT) {
			snprintf(why, len, "the %s's direction: %s", k == 0 ? "opener" : "responder",
			    octep_seg_why[r]);
			if (r == OCTEP_SEG_OLD || r == OCTEP_SEG_NOTCP)
				snprintf(why + strlen(why), len - strlen(why), " (%u ms ago)",
				    age * 1000 / hz);
			return (0);
		}
	}
	if (!octep_tcp_from_segs(b, scale, sg)) {
		snprintf(why, len, "a latest segment acknowledges nothing, or is a SYN");
		return (0);
	}
	return (1);
}

/*
 * Offer a punted frame as a candidate for acceleration.
 *
 * Called from the receive path, which holds no softc lock and may be running on several rings at
 * once - see struct octep_flow_cand for why each slot has a trylock rather than a seqlock, and why a
 * writer that cannot take it simply gives up.
 *
 * Cheap on purpose: a hash, a trylock, a look at the slot's few places and a struct copy. No lookup
 * against the flow table happens here; the drain does that, where it costs the poll rather than the
 * datapath.
 *
 * Two callers. A frame that arrived as it is comes through octep_flow_cand_push below, which finds
 * the port from the frame's tag. A frame the coprocessor decrypted comes from octep_ipsec_rx, after
 * it has been terminated, with the INNER packet's tuple, the port the ESP arrived on, and the
 * association its metadata named - which is the only evidence there will ever be that a frame of
 * that tuple on that port came out of a tunnel.
 */
void
octep_flow_cand_put(struct octep_softc *sc, const struct octep_pf_tuple *t, uint32_t slot,
    uint32_t rev, int in_dif, uint16_t tag, uint16_t sa, uint16_t sa_rev, uint8_t closing,
    const struct octep_tcp_seg *seg, const uint8_t *mac, uint16_t vlan)
{
	struct octep_flow_cand_slot *cs;
	struct octep_flow_cand *e, *o;
	uint32_t h, run, need;
	int fresh, same, ask, w;

	/*
	 * Indexed by the tuple, which is what makes this deduplicate for nothing: every frame of one
	 * connection lands on one slot, so a download occupies a single entry however many frames it
	 * sends, and distinct connections spread out.
	 *
	 * The mix is a multiply and a shift of the top bits rather than a fold and a modulo. Two
	 * reasons, both from review. `sip ^ dip` cancels everything a household LAN's addresses have
	 * in common and `h ^= h >> 16` then carries only bits 16..21 into a `% 64`, so the host octet
	 * and both ports' low bytes never reach the index at all - whole classes of connection would
	 * share a handful of slots. And a symmetric fold gives a connection's two directions the same
	 * index, so every connection would spend two of its slot's places; rotating one address
	 * breaks that.
	 */
	/*
	 * A slot has a few places and one word. A frame is written to the place that holds its
	 * tuple. When none does it takes a place nothing has ever been written to, and when there
	 * is none of those either, the place written longest ago: to the tick, and of places
	 * written in the same tick the first - whatever the reader has or has not taken from it.
	 * So a tuple that keeps coming makes room only when every place was written in the same
	 * tick as its own, and while no more than four tuples are busy in the slot the two
	 * directions of a connection whose tuples share it stand in it side by side.
	 *
	 * All of it under the word, so that two rings deciding at once cannot put one tuple in two
	 * places.
	 */
	h = octep_flow_cand_slot(t);
	cs = &sc->dp_cand[h];

	if (!octep_flow_cand_word_try(cs)) {
		sc->dp_cand_clash++;
		return;
	}
	e = o = NULL;
	for (w = 0; w < OCTEP_FLOW_CAND_WAYS; w++) {
		if (cs->way[w].stamp == 0) {
			if (o == NULL || o->stamp != 0)
				o = &cs->way[w];
		} else if (octep_tuple_same(&cs->way[w].tuple, t)) {
			e = &cs->way[w];
			break;
		} else if (o == NULL || (o->stamp != 0 &&
		    (int)((u_int)cs->way[w].when - (u_int)o->when) < 0))
			o = &cs->way[w];
	}
	same = (e != NULL);
	if (!same)
		e = o;

	/*
	 * An unread candidate about to be overwritten. By the next frame of its own tuple that
	 * loses nothing; by another tuple's it is a collision that costs something - more busy
	 * tuples in the slot than it has places, the one written longest ago making room before
	 * the poll got to it. A candidate the poll has read makes room uncounted, though the
	 * reader of the other direction would still have used it. dp_cand_clash sees neither,
	 * because those writers never meet on the word.
	 */
	fresh = (e->stamp == e->seen);
	if (!fresh) {
		sc->dp_cand_lost++;
		if (!same)
			sc->dp_cand_evicted++;
	}

	/*
	 * FIN or RST is remembered until the reader takes the candidate, as long as the frames
	 * that overwrite it are the same connection's: the frame that ends a connection is followed
	 * at once by acknowledgements that would otherwise hide it.
	 */
	if (!fresh && same)
		closing |= e->closing;
	/*
	 * The tuple's own count of frames in a row in its place, and at each power of two of it
	 * from the threshold up, a request that it be made a connection now and not at the next
	 * poll. Doubling is the whole of the memory of what was refused: a flow that cannot be made
	 * is asked about at its 16th frame, its 32nd, its 64th - a dozen askings in a hundred
	 * thousand frames. Tuples that take turns at one place - there are more of them busy in
	 * the slot than it has places - never count far, and those are left to the poll, as every
	 * connection was before. Not for a frame that ends a connection, and only for the two
	 * protocols a run is allowed to make.
	 *
	 * Doubling forgets the moment another tuple takes the place, and a reader showed what that
	 * allows: two tuples taking turns in one, each asked about at every sixteenth frame.
	 * So a slot whose attempt between polls was refused is held until the poll has been round,
	 * and nothing is asked for once this second's makes are spent.
	 */
	run = same ? e->run + (e->run != UINT32_MAX) : 1;
	ask = 0;
	if (!same)
		e->due = 0;
	need = 1u << (sc->dp_fast_make_log2 < 16 ? sc->dp_fast_make_log2 : 16);
	if (sc->dp_fast_make != 0 && closing == 0 && run >= need && (run & (run - 1)) == 0 &&
	    (t->proto == IPPROTO_TCP || t->proto == IPPROTO_UDP) &&
	    sc->dp_fast_make_left != 0 && !octep_cand_held(sc, h)) {
		e->due = 1;
		ask = 1;
	}
	e->run = run;
	e->tuple = *t;
	e->slot = slot;
	e->rev = rev;
	e->in_dif = in_dif;
	e->in_tag = tag;
	e->sa = sa;
	e->sa_rev = sa_rev;
	e->closing = closing;
	if (mac != NULL)
		memcpy(e->mac, mac, sizeof(e->mac));
	else
		memset(e->mac, 0, sizeof(e->mac));
	e->vlan = vlan;
	if (seg != NULL)
		e->seg = *seg;
	else
		e->seg.valid = 0;
	e->when = ticks;
	/* Never zero again once written: zero is how an empty place is known. */
	if (++e->stamp == 0) {
		e->stamp = 1;
		e->seen = 0;
	}
	sc->dp_cand_pushed++;

	octep_flow_cand_word_drop(cs);

	/*
	 * And if the table holds a connection with this tuple, this frame is news that cannot wait
	 * for the poll: a connection in hardware does not send the host its frames. Either the fast
	 * path has given the connection back, and every frame of it is crossing the host until it
	 * is revived; or its microflow has gone and the frames come under a new identity until the
	 * direction is attached again. A second of that, each time, was most of a fast download.
	 *
	 * dp_conn_hot is read with nothing held - see the softc. One kick at a time: while one is
	 * queued or waiting out its hold-off, this is a load and a compare per punted frame. Any
	 * frame of a held connection kicks, not only the first since the last run - a run that
	 * decided to leave a connection alone for now must be asked again while its frames keep
	 * coming - and it is the task, not this test, that keeps runs a hold-off apart. The task
	 * queue is the driver's own: the system's shared one is where the link poll sleeps on the
	 * management channel.
	 */
	if (sc->dp_fast_kick == 0 && sc->dp_fast != 0 && sc->dp_auto != 0 &&
	    (ask || sc->dp_conn_hot[octep_conn_hot_slot(t)] != 0) && sc->dp_fast_tq != NULL &&
	    atomic_cmpset_int(&sc->dp_fast_kick, 0, 1)) {
		sc->dp_fast_kicks++;
		taskqueue_enqueue(sc->dp_fast_tq, &sc->dp_fast_task);
	}
}

/*
 * Does this IPv4 packet carry a TCP FIN or RST? For the candidate a punted frame makes: a
 * connection that hands one of those back is ending, and the fast path's own entry does not say so
 * when its FIN tracking is off - it gives the connection back for FIN, SYN or RST and for its
 * retransmission rule alike, and only the frame tells them apart for certain.
 *
 * Not SYN, though the fast path gives back for that too. The frames that OPEN a connection are
 * handed up before it is in hardware and leave a candidate behind, and the first version, which
 * counted SYN, marked every other connection as ending by its own opening - measured: a download
 * given back in its fortieth second and taken out instead of revived. A SYN on a connection that
 * is already in hardware does not need the mark: the entry's counter is then not at its limit, and
 * the connection goes out for that.
 */
uint8_t
octep_dp_tcp_closing(const uint8_t *ip, uint32_t len)
{
	uint32_t ihl;

	if (len < 20 || (ip[0] >> 4) != 4 || ip[9] != IPPROTO_TCP)
		return (0);
	ihl = (uint32_t)(ip[0] & 0x0f) * 4;
	if (ihl < 20 || len < ihl + 14)
		return (0);
	return ((ip[ihl + 13] & 0x05) != 0);	/* FIN 0x01, RST 0x04 */
}

/*
 * A TCP segment's numbers, from an IPv4 or IPv6 packet as the candidate's frame carries it: the
 * sequence, the acknowledgement, the payload's length, the window as written and the flags. For
 * the window of a connection whose pf numbers have stopped (issue #679): made from its two
 * directions' latest segments, which are what the host saw last. An IPv4 fragment, an IPv6 header
 * with an extension before TCP, or a header shorter than it says, is no segment.
 */
void
octep_dp_tcp_seg(const uint8_t *ip, uint32_t len, struct octep_tcp_seg *s)
{
	uint32_t ihl, thl, tot;

	s->valid = 0;
	if (len < 40)
		return;
	if ((ip[0] >> 4) == 4) {
		ihl = (uint32_t)(ip[0] & 0x0f) * 4;
		tot = be16dec(ip + 2);
		if (ip[9] != IPPROTO_TCP || ihl < 20 || (be16dec(ip + 6) & 0x3fff) != 0)
			return;
	} else if ((ip[0] >> 4) == 6) {
		ihl = 40;
		tot = 40 + be16dec(ip + 4);
		if (ip[6] != IPPROTO_TCP)
			return;
	} else
		return;
	if (len < ihl + 20)
		return;
	thl = (uint32_t)(ip[ihl + 12] >> 4) * 4;
	if (thl < 20 || tot < ihl + thl)
		return;
	s->seq = be32dec(ip + ihl + 4);
	s->ack = be32dec(ip + ihl + 8);
	s->flags = ip[ihl + 13];
	s->win = be16dec(ip + ihl + 14);
	s->len = tot - ihl - thl;
	s->valid = 1;
}

static void
octep_flow_cand_push(struct octep_softc *sc, const struct octep_pf_tuple *t, uint32_t slot,
    uint32_t rev, uint16_t tag, uint8_t closing, const struct octep_tcp_seg *seg,
    const uint8_t *mac, uint16_t vlan)
{
	struct octep_dp_if *dif;

	/* Outside the candidate's lock: a walk of up to twelve interfaces is the whole cost. */
	dif = octep_dp_if_by_tag(sc, tag);
	octep_flow_cand_put(sc, t, slot, rev, (dif != NULL) ? (int)(dif - sc->dp_if) : -1, tag,
	    0, 0, closing, seg, mac, vlan);
}

/*
 * The tuple of an IPv4 packet, as pf keys it - what octep_dp_rx_tuple does for a frame off the
 * wire, for a caller that holds a bare packet: octep_ipsec_rx, with the inner packet of a frame the
 * coprocessor decrypted. The reasons for every line are in octep_dp_rx_tuple and are not repeated;
 * the two must agree or a tunnelled connection's two directions would never pair.
 *
 * A fragment is refused here. Its first piece has ports and the rest do not, and a candidate is a
 * statement about a connection.
 */
int
octep_dp_tuple_from_ip(const uint8_t *ip, uint32_t len, struct octep_pf_tuple *t)
{
	uint32_t ihl;

	if (len < 20 || (ip[0] >> 4) != 4)
		return (0);
	ihl = (uint32_t)(ip[0] & 0x0f) * 4;
	if (ihl < 20 || (ip[6] & 0x3f) != 0 || ip[7] != 0)
		return (0);
	bzero(t, sizeof(*t));
	t->af = AF_INET;
	t->proto = ip[9];
	memcpy(&t->sip, ip + 12, 4);
	memcpy(&t->dip, ip + 16, 4);
	if ((t->proto == IPPROTO_TCP || t->proto == IPPROTO_UDP) && len >= ihl + 4) {
		memcpy(&t->sport, ip + ihl, 2);
		memcpy(&t->dport, ip + ihl + 2, 2);
	}
	if (t->proto == IPPROTO_ICMP && len >= ihl + 6 && (ip[ihl] == 0 || ip[ihl] == 8)) {
		memcpy(&t->sport, ip + ihl + 4, 2);
		t->dport = htons(8);	/* ICMP_ECHO, as pf's virtual_type */
	}
	return (1);
}

/*
 * Forget every connection and every next hop, because the far side has just been told to discard
 * every flow.
 *
 * A revision bump invalidates the coprocessor's whole microflow table in one command, and this
 * table is only this driver's record of what it programmed there. Leaving the record behind would
 * make dp.flows count connections that no longer forward, fill the table with entries the sweep
 * has to walk and expire, and eventually refuse a real connection for want of room in a table that
 * is actually empty. The connection and next-hop entries themselves stay on the far side; each
 * keeps its revision here, so the next use of an index carries a higher one and overwrites them.
 *
 * Nothing is sent. The microflows are already gone on the far side, which is the whole point of
 * the command that got us here.
 *
 * The candidate table is emptied with them. A candidate is the identity of a microflow, and one
 * written before the bump names a microflow of before it: handed to a make as the other direction
 * of a connection it would be programmed into nothing. A slot whose word stays taken keeps its
 * places, as every slot kept its one before this was done at all, and is counted.
 *
 * Called with the softc lock held, by the revision bump and by nothing else.
 */
void
octep_dp_flows_forget(struct octep_softc *sc)
{
	struct octep_flow_cand_slot *cs;
	int got, i, s;

	mtx_assert(&sc->mtx, MA_OWNED);

	for (i = 0; i < (int)sc->dp_conn_cap; i++) {
		if (!sc->dp_conn[i].used)
			continue;
		sc->dp_conn[i].used = 0;
		sc->dp_flow_forgot++;
	}
	sc->dp_conn_used = 0;
	memset(sc->dp_conn_hot, 0, sizeof(sc->dp_conn_hot));
	memset(sc->dp_conn_head, 0, ((size_t)1 << sc->dp_conn_hbits) * sizeof(*sc->dp_conn_head));
	sc->dp_conn_nlive = 0;
	octep_conn_free_fill(sc);
	for (s = 0; s < OCTEP_FLOW_CAND_SLOTS; s++) {
		cs = &sc->dp_cand[s];
		for (got = 0, i = 0; i < OCTEP_CAND_TRIES && !got; i++) {
			got = octep_flow_cand_word_try(cs);
			if (!got)
				cpu_spinwait();
		}
		if (!got) {
			sc->dp_cand_kept++;
			continue;
		}
		memset(cs->way, 0, sizeof(cs->way));
		octep_flow_cand_word_drop(cs);
	}
	for (i = 0; i < OCTEP_NHOP_MAX; i++) {
		sc->dp_nhop[i].used = 0;
		sc->dp_nhop[i].refcnt = 0;
	}
	sc->dp_nhop_used = 0;
	sc->dp_nhop_hi = 2;
}

static void
octep_conn_free(struct octep_softc *sc, struct octep_conn *c)
{
	uint32_t h, last;
	int d;

	mtx_assert(&sc->mtx, MA_OWNED);
	for (d = 0; d < 2; d++) {
		h = octep_conn_hot_slot(&c->tuple[d]);
		if (sc->dp_conn_hot[h] != 0)
			sc->dp_conn_hot[h]--;
	}
	for (d = 0; d < 2; d++) {
		if (c->mf[d].nhop != 0)
			octep_nhop_put(sc, c->mf[d].nhop);
		c->mf[d].nhop = 0;
		c->mf[d].state = OCTEP_MF_NONE;
	}
	octep_conn_unlink(sc, c);
	/* Out of the live list: the last one takes its place. */
	if (c->used && sc->dp_conn_nlive > 0 && c->live_pos < sc->dp_conn_nlive &&
	    sc->dp_conn_live[c->live_pos] == c->idx) {
		last = sc->dp_conn_live[--sc->dp_conn_nlive];
		sc->dp_conn_live[c->live_pos] = last;
		sc->dp_conn[last].live_pos = c->live_pos;
	}
	c->used = 0;
	if (sc->dp_conn_nfree < sc->dp_conn_cap)
		sc->dp_conn_free[sc->dp_conn_nfree++] = c->idx;
	if (sc->dp_conn_used > 0)
		sc->dp_conn_used--;
	/*
	 * And its two candidate slots are the poll's until the poll has been round. A connection
	 * that is taken out while its frames keep coming would otherwise be asked for again
	 * sixteen frames later - made, given back and taken out, at the pace of the runs and not
	 * of the poll, for as long as whatever takes it out goes on.
	 */
	octep_cand_hold(sc, octep_flow_cand_slot(&c->tuple[0]));
	octep_cand_hold(sc, octep_flow_cand_slot(&c->tuple[1]));
}

/*
 * Take a connection out of the fast path: each programmed direction back to INACTIVE, then the
 * connection reclaimed, then the host's record freed.
 *
 * INACTIVE rather than deleted, because the microflow entry belongs to the fast path, which made
 * it and will reuse it; what the host owns is whether it is used. The reclaim is what the vendor's
 * host does when a connection ends: the far side marks the entry RECLAIMED and answers with its
 * final counters, and refuses if the revision has moved on - so a reused index can never reclaim
 * its successor. A reclaim the far side refuses is counted and not fatal: the entry is gone from
 * this table either way, and the next allocation of the index carries the next revision.
 *
 * And the answer carries the connection's window as the fast path last held it, which pf takes for
 * a connection whose window was checked in hardware (issue #679): queued here, given to pf by
 * octep_dp_handback_run once the lock is dropped. reclaim is OCTEP_TAKEOUT_GIVEN for a connection
 * the far side had already given back: its frames have been reaching the host since, past the
 * numbers the reclaim answers with, and its pf states stay sloppy. So does a connection that was
 * closing, and one whose entry, read just before the reclaim, the fast path had given back without
 * this host knowing yet. dp.tcp_last says which, for the last one. OCTEP_TAKEOUT_GONE is a
 * connection whose pf states have gone: there is nobody to give the window to, and it is reclaimed
 * as it always was, one command.
 *
 * Returns ETIMEDOUT when the far side stopped answering, so a sweep pass can stop rather than hold
 * the softc lock for one timeout per entry. Called with the softc lock held.
 */
#define	OCTEP_TAKEOUT_GIVEN	2
#define	OCTEP_TAKEOUT_GONE	3
static int
octep_conn_takeout(struct octep_softc *sc, struct octep_conn *c, int reclaim)
{
	struct octep_handback *hb;
	const char *why;
	uint8_t seq[OCTEP_TCP_SEQ_LEN];
	uint32_t f, cst, crev;
	int d, err, want;

	mtx_assert(&sc->mtx, MA_OWNED);
	for (d = 0; d < 2; d++) {
		if (c->mf[d].state != OCTEP_MF_PROGRAMMED)
			continue;
		err = octep_rpc_mflow_set(sc, c, d, OCTEP_MFLOW_STATE_INACTIVE);
		if (err == ETIMEDOUT) {
			c->doomed = 1;	/* decided and not done: the audit finishes it */
			return (err);
		}
		c->mf[d].state = OCTEP_MF_NONE;
	}
	if (reclaim) {
		/*
		 * Was the fast path still forwarding the connection until its microflows went
		 * INACTIVE just now? Asked rather than inferred, for a checked one: a connection the
		 * fast path gave back since the last look - its retransmission rule, a FIN, a RST - has
		 * had frames reach the host past the numbers the reclaim will answer with.
		 */
		why = NULL;
		want = (c->tcp_on && sc->dp_tcp_window != 0 && reclaim != OCTEP_TAKEOUT_GONE);
		if (reclaim == OCTEP_TAKEOUT_GONE)
			;	/* pf has let it go: nothing to give, nothing to count */
		else if (c->tcp_on && !want)
			why = "dp.tcp_window is 0";
		else if (want && reclaim == OCTEP_TAKEOUT_GIVEN)
			why = "the fast path had given it back already";
		else if (want && c->closing)
			why = "a FIN or RST of it had reached the host";
		else if (want) {
			err = octep_rpc_conn_read(sc, c->idx, &cst, &crev, NULL);
			if (err == ETIMEDOUT) {
				c->doomed = 1;
				return (err);
			}
			if (err != 0 || cst != OCTEP_CONN_VALID || crev != c->conn_rev)
				why = "the fast path had given it back already";
		}
		err = octep_rpc_conn_reclaim(sc, c, (want && why == NULL) ? seq : NULL);
		if (err == ETIMEDOUT) {
			c->doomed = 1;
			return (err);
		}
		if (err == 0 || err == EMSGSIZE)
			sc->dp_flow_reclaimed++;
		else
			sc->dp_reclaim_refused++;	/* the far side had already left RECLAIM_PENDING, or holds another rev */
		if (c->tcp_on && reclaim != OCTEP_TAKEOUT_GONE) {
			if (why == NULL && err != 0)
				why = "the reclaim was not answered with its numbers";
			if (why == NULL && OCTEP_TCP_FIN_STATE(seq[OCTEP_TCP_OFF_FIN]) != 0)
				why = "the fast path had seen it closing";
			if (why == NULL && sc->dp_hb_n >= OCTEP_HB_MAX) {
				why = "the queue to pf was full";
				sc->dp_tcp_lost++;
			} else if (why != NULL)
				sc->dp_tcp_kept++;
			if (why == NULL) {
				hb = &sc->dp_hb[sc->dp_hb_n++];
				hb->idx = c->idx;
				hb->tuple[0] = c->tuple[0];
				hb->tuple[1] = c->tuple[1];
				memcpy(hb->seq, seq, OCTEP_TCP_SEQ_LEN);
				f = le32dec(c->tcp + OCTEP_TCP_OFF_FLAGS);
				hb->scale[0] = (uint8_t)(f >> OCTEP_TCP_SCALE_ORIG_SHIFT);
				hb->scale[1] = (uint8_t)(f >> OCTEP_TCP_SCALE_REPLY_SHIFT);
			} else
				snprintf(sc->dp_tcp_last, sizeof(sc->dp_tcp_last),
				    "connection %u: left sloppy in pf - %s\n", c->idx, why);
		}
	}
	octep_conn_free(sc, c);
	return (0);
}

/*
 * Give pf the windows of the connections just taken back: the queue octep_conn_takeout fills,
 * emptied one entry at a time with the lock dropped around each, because pf's states are never
 * touched with the softc lock held - the transmit path takes it, and pf may hold a state's lock
 * while it sends. Called right after a lock that took connections out is dropped, so the frames
 * that reach pf while it is still sloppy are as few as can be; every caller of a takeout calls it,
 * and the sweep calls it each second for anything left. A state that does not take the numbers
 * stays sloppy and is counted with the kept ones.
 */
void
octep_dp_handback_run(struct octep_softc *sc)
{
	struct octep_handback one;
	int n;

	mtx_assert(&sc->mtx, MA_NOTOWNED);
	if (sc->dp_hb_n == 0)	/* unlocked: one missed now is found by the next call */
		return;
	for (;;) {
		mtx_lock(&sc->mtx);
		if (sc->dp_hb_n == 0) {
			mtx_unlock(&sc->mtx);
			return;
		}
		one = sc->dp_hb[--sc->dp_hb_n];
		mtx_unlock(&sc->mtx);
		/*
		 * pf's tables are per vnet, and some callers - the sysctls, the IPsec offload task -
		 * run with none set; this driver's interfaces are in vnet0 (octep_dp_poll).
		 */
		CURVNET_SET_QUIET(vnet0);
		n = octep_pf_handback(one.tuple, one.seq, one.scale);
		CURVNET_RESTORE();
		if (n > 0)
			sc->dp_tcp_handback++;
		else
			sc->dp_tcp_kept++;
		mtx_lock(&sc->mtx);
		snprintf(sc->dp_tcp_last, sizeof(sc->dp_tcp_last), n > 0 ?
		    "connection %u: handed back, %d pf state(s) took the fast path's numbers\n" :
		    "connection %u: no pf state took the fast path's numbers - gone, or the answer "
		    "did not look like a window\n", one.idx, n);
		mtx_unlock(&sc->mtx);
	}
}

/*
 * An association is going: every connection that names it either moves to its successor or leaves
 * the fast path. Called by octep_ipsec_sa_deinstall BEFORE the association is deleted, with no lock
 * held, from the kernel's IPsec offload task.
 *
 * The two directions need this for different reasons, and the second is the one that matters.
 *
 * The direction that leaves encrypted names the association in its microflow. With the
 * association gone the far side would hand every frame back to the host, which is correct and
 * slow; moved to the successor a rekey installed - same tunnel, same direction, newer - the
 * connection never notices. That is one MFLOW_PROGRAM with the same identity and the new handle
 * and revision, the re-stage the first hardware trials of 2026-10-07 did by hand.
 *
 * The direction that arrives decrypted names nothing. Its microflow is a plain one, keyed on the
 * port, the link addresses and the inner tuple, and the far side does not ask whether a frame it
 * matches was decrypted: read in the fast path's own code, and the reason ipsec.flows has a 2 that
 * has to be chosen. While an inbound association of the tunnel exists, that is the exposure the
 * operator accepted. Once none does, the microflow would go on forwarding frames of that tuple
 * that arrive IN THE CLEAR, for as long as the connection lived. So with no successor the
 * connection is taken out, here, before the association is; with one, only the record moves.
 *
 * One entry at a time under the lock, as the sweep does: each is up to three commands that can
 * each wait two seconds with the transmit path's lock held. A far side that has stopped answering
 * ends the walk, and the entries it did not reach are found a second later, and every second
 * after, by octep_dp_flows_ipsec_audit - which asks of each tunnelled connection only whether the
 * associations it names still exist, and so needs nobody to have remembered what was left undone.
 */
void
octep_dp_flows_sa_gone(struct octep_softc *sc, uint32_t handle, uint32_t rev, uint32_t repl,
    uint32_t repl_rev)
{
	struct octep_conn *c;
	uint16_t osa, orev;
	int i, d, out;

	if (handle == 0)
		return;
	for (int k = (int)sc->dp_conn_nlive - 1; k >= 0; k--) {
		i = (int)sc->dp_conn_live[k];
		mtx_lock(&sc->mtx);
		c = &sc->dp_conn[i];
		if (!c->used || !c->ipsec) {
			mtx_unlock(&sc->mtx);
			continue;
		}
		/*
		 * By handle AND revision: a handle is an index plus one, indices are reused, and a
		 * connection a cut-short walk left behind must not be taken for the next
		 * association's. And the successor is looked at again for every entry, under this
		 * lock: it was chosen before the walk began, and an interface going away removes
		 * associations from another thread.
		 */
		out = 0;
		for (d = 0; d < 2 && !out; d++) {
			if (c->mf[d].sa == handle && c->mf[d].sa_rev == (uint16_t)rev) {
				if (repl == 0 || !octep_ipsec_handle_live(sc, repl, repl_rev, 0, 1)) {
					out = 1;
					break;
				}
				/*
				 * The record names the successor only once the far side has been
				 * told: a command that fails leaves the record naming the dying
				 * association, which is the one thing the audit can see is wrong.
				 */
				osa = c->mf[d].sa;
				orev = c->mf[d].sa_rev;
				c->mf[d].sa = (uint16_t)repl;
				c->mf[d].sa_rev = (uint16_t)repl_rev;
				if (c->mf[d].state == OCTEP_MF_PROGRAMMED &&
				    octep_rpc_mflow_set(sc, c, d, OCTEP_MFLOW_STATE_ACTIVE) != 0) {
					c->mf[d].sa = osa;
					c->mf[d].sa_rev = orev;
					out = 1;
				} else {
					sc->ipsec_flow_repoint++;
					octep_ipsec_sa_flow_attached(sc, repl);
				}
			} else if (c->mf[d].dsa == handle && c->mf[d].dsa_rev == (uint16_t)rev) {
				if (repl == 0 || !octep_ipsec_handle_live(sc, repl, repl_rev, 1, 1)) {
					out = 1;
					break;
				}
				c->mf[d].dsa = (uint16_t)repl;
				c->mf[d].dsa_rev = (uint16_t)repl_rev;
				sc->ipsec_flow_repoint++;
				octep_ipsec_sa_flow_attached(sc, repl);
			}
		}
		if (out) {
			sc->ipsec_flow_gone++;
			if (octep_conn_takeout(sc, c, 1) == ETIMEDOUT) {
				mtx_unlock(&sc->mtx);
				octep_dp_handback_run(sc);
				device_printf(sc->dev, "ipsec: the far side stopped answering while "
				    "connections of association handle %u were being taken out\n",
				    handle);
				return;
			}
		}
		mtx_unlock(&sc->mtx);
		octep_dp_handback_run(sc);
	}
}

/*
 * ipsec.flows was lowered: take out what the new value no longer allows. To 0, every tunnelled
 * connection; to 1, every one whose decrypted direction is in hardware - whole, because a
 * connection with one direction taken out is one the fast path gives back within a few frames,
 * and the next plaintext frame makes it again in the shape the new value allows.
 */
void
octep_dp_flows_ipsec_out(struct octep_softc *sc, uint32_t mode)
{
	struct octep_conn *c;
	int i;

	for (int k = (int)sc->dp_conn_nlive - 1; k >= 0; k--) {
		i = (int)sc->dp_conn_live[k];
		mtx_lock(&sc->mtx);
		c = &sc->dp_conn[i];
		if (c->used && c->ipsec && (mode == 0 ||
		    c->mf[1 - c->enc_dir].state == OCTEP_MF_PROGRAMMED)) {
			sc->ipsec_flow_gone++;
			if (octep_conn_takeout(sc, c, 1) == ETIMEDOUT) {
				mtx_unlock(&sc->mtx);
				octep_dp_handback_run(sc);
				return;
			}
		}
		mtx_unlock(&sc->mtx);
		octep_dp_handback_run(sc);
	}
}

/* How many connections name this association, in either way. Takes the lock itself. */
uint32_t
octep_dp_flows_on_sa(struct octep_softc *sc, uint32_t handle, uint32_t rev)
{
	const struct octep_conn *c;
	uint32_t n = 0;
	int i, d;

	if (handle == 0)
		return (0);
	mtx_lock(&sc->mtx);
	for (int k = (int)sc->dp_conn_nlive - 1; k >= 0; k--) {
		i = (int)sc->dp_conn_live[k];
		c = &sc->dp_conn[i];
		if (!c->used || !c->ipsec)
			continue;
		for (d = 0; d < 2; d++)
			if ((c->mf[d].sa == handle && c->mf[d].sa_rev == (uint16_t)rev) ||
			    (c->mf[d].dsa == handle && c->mf[d].dsa_rev == (uint16_t)rev)) {
				n++;
				break;
			}
	}
	mtx_unlock(&sc->mtx);
	return (n);
}

/*
 * Once a second: is every tunnelled connection in the table still one that should be there?
 *
 * Three things can leave one behind, and each of them is somebody having been interrupted: the
 * walk that takes connections out with their association stops when the far side does not answer;
 * the walk that honours a lowered ipsec.flows stops the same way; and a connection can be written
 * in the instant between either walk passing its place in the table and the reason for the walk
 * taking effect. None of them leaves a note. So nothing here reads one. A tunnelled connection is
 * right when ipsec.flows still allows what it has in hardware, when the association its encrypting
 * direction names still exists with that revision, and - if its decrypted direction is in
 * hardware - when the association that direction was learned through still does. That last test
 * is the one the decrypted direction's microflow cannot make for itself. Anything else is taken
 * out, and the next frame makes it again if it should exist.
 *
 * No command unless something is wrong. An association being removed still exists until its
 * removal returns, so a connection the removal is about to move to a successor is not taken out
 * from under it.
 *
 * And one kind of connection that need not be tunnelled: an entry marked doomed. A command for it
 * was not answered, so nobody knows whether the far side has it, and it goes as soon as the far
 * side answers - see struct octep_conn.
 */
void
octep_dp_flows_ipsec_audit(struct octep_softc *sc)
{
	struct octep_conn *c;
	int i, e, bad;

	for (int k = (int)sc->dp_conn_nlive - 1; k >= 0; k--) {
		i = (int)sc->dp_conn_live[k];
		mtx_lock(&sc->mtx);
		c = &sc->dp_conn[i];
		if (!c->used || (!c->ipsec && !c->doomed)) {
			mtx_unlock(&sc->mtx);
			continue;
		}
		e = c->enc_dir;
		bad = c->doomed != 0;
		if (!bad)
			bad = (sc->ipsec_flows == 0) ||
			    !octep_ipsec_handle_live(sc, c->mf[e].sa, c->mf[e].sa_rev, 0, 0);
		if (!bad && c->mf[1 - e].state == OCTEP_MF_PROGRAMMED)
			bad = (sc->ipsec_flows < 2) || !octep_ipsec_handle_live(sc,
			    c->mf[1 - e].dsa, c->mf[1 - e].dsa_rev, 1, 0);
		if (bad) {
			sc->ipsec_flow_audit++;
			if (octep_conn_takeout(sc, c, 1) == ETIMEDOUT) {
				mtx_unlock(&sc->mtx);
				octep_dp_handback_run(sc);
				return;		/* and again next second */
			}
		}
		mtx_unlock(&sc->mtx);
		octep_dp_handback_run(sc);
	}
}

/*
 * A tunnelled connection with only its encrypting direction in hardware, given back by the fast
 * path: leave it with the host for a minute.
 *
 * The fast path gives a TCP connection back when it sees ten packets in a row, in one direction,
 * with the same acknowledgement number, end and window - its rule for a retransmission storm.
 * With both directions in hardware the other direction's packets come between and reset the
 * count. With one, they never do: on a download the direction in hardware is the acknowledgements,
 * and ten duplicate acknowledgements are what one lost segment produces. Measured with
 * ipsec.flows 1, four streams down for twelve seconds, twice: TCP_MAX_RETRANS +3 and +4, 66,201
 * and 98,787 frames handed back as CONN_RECLAIM_PENDING, the connections made again every few
 * seconds - commands spent for a direction that carries acknowledgements, on a path where the
 * bulk crosses the host either way. An upload is the other case: its data direction is the one in
 * hardware and every packet has a new end, so the rule has nothing to count.
 *
 * So the give-back is taken as the fast path's own verdict on that connection. The close of a
 * connection is a give-back too and is entered like one; that costs a place in the table for a
 * minute and nothing else. Called with sc->mtx held.
 */
static void
octep_ipsec_backoff_put(struct octep_softc *sc, const struct octep_pf_tuple *t)
{
	struct octep_ipsec_backoff *b, *o;
	int i;

	mtx_assert(&sc->mtx, MA_OWNED);
	/*
	 * The entry this connection already has; else one whose minute is over; else the one
	 * nearest the end of its minute. A connection given back again must not use the table up,
	 * and one whose minute is running must not lose its place to a connection that has closed.
	 */
	b = NULL;
	for (i = 0; i < OCTEP_IPSEC_BACKOFF_MAX; i++) {
		o = &sc->ipsec_backoff[i];
		if (octep_tuple_eq(&o->t, t)) {
			b = o;
			break;
		}
		if (b == NULL || o->until < b->until)
			b = o;
	}
	b->t = *t;
	b->until = time_uptime + OCTEP_IPSEC_BACKOFF_SECS;
}

static int
octep_ipsec_backoff_hit(struct octep_softc *sc, const struct octep_pf_tuple *t)
{
	const struct octep_ipsec_backoff *b;
	int i, hit = 0;

	mtx_lock(&sc->mtx);
	for (i = 0; i < OCTEP_IPSEC_BACKOFF_MAX && !hit; i++) {
		b = &sc->ipsec_backoff[i];
		hit = (b->until > time_uptime && octep_tuple_eq(&b->t, t));
	}
	mtx_unlock(&sc->mtx);
	return (hit);
}

/*
 * THE STREAK'S WAIT (issue 544, dp.revive_hold). With a thousand connections filling the 10G link
 * the fast path gives each one back about fourteen times in thirty seconds - its rule of ten
 * identical packets in a row, which runs of duplicate acknowledgements meet - and each is revived
 * at once. The XGS 136 since #532 leaves a connection given back soon after it was made with the
 * host for a wait, a second doubling to a minute. This is the same rule for a revived one: revived
 * OCTEP_REVIVE_STREAK times in as many seconds and given back again, it is taken out and both its
 * tuples wait here; the maker does not make it again until the wait is over. A wait that follows
 * the last one's end within OCTEP_HOLD_CALM_SECS is twice as long. Off by default: whether the
 * host carrying those connections a while costs less than reviving them is what it measures.
 * Called with sc->mtx held.
 */
static void
octep_hold_put(struct octep_softc *sc, const struct octep_pf_tuple *t)
{
	struct octep_flow_hold *h = NULL, *o, *spare = NULL, *oldest = NULL;
	u_int base;
	int i, wait;

	mtx_assert(&sc->mtx, MA_OWNED);
	if (sc->dp_hold == NULL)
		return;
	/*
	 * Its place among the OCTEP_HOLD_WAYS its hash chooses: the one that holds it; else one free
	 * or long calm; else the one whose wait ended first. A constant cost, as the maker asks for
	 * every candidate.
	 */
	base = octep_conn_hash32(t) & (OCTEP_HOLD_MAX - 1) & ~(OCTEP_HOLD_WAYS - 1);
	for (i = 0; i < OCTEP_HOLD_WAYS; i++) {
		o = &sc->dp_hold[base + i];
		if (o->until != 0 && octep_tuple_eq(&o->t, t)) {
			h = o;
			break;
		}
		if (o->until == 0 || (ticks - o->until) >= OCTEP_HOLD_CALM_SECS * hz) {
			if (spare == NULL)
				spare = o;
		} else if (oldest == NULL || (o->until - oldest->until) < 0)
			oldest = o;
	}
	if (h != NULL && (ticks - h->until) < OCTEP_HOLD_CALM_SECS * hz)
		wait = imin(h->wait * 2, OCTEP_HOLD_WAIT_MAX_SECS * hz);
	else
		wait = hz;
	if (h == NULL)
		h = spare != NULL ? spare : oldest;
	h->t = *t;
	h->wait = wait;
	h->until = ticks + wait;
	if (h->until == 0)
		h->until = 1;
}

/* Is this tuple's wait still running? Takes sc->mtx. */
static int
octep_hold_hit(struct octep_softc *sc, const struct octep_pf_tuple *t)
{
	const struct octep_flow_hold *h;
	u_int base;
	int i, hit = 0;

	if (sc->dp_hold == NULL)
		return (0);
	base = octep_conn_hash32(t) & (OCTEP_HOLD_MAX - 1) & ~(OCTEP_HOLD_WAYS - 1);
	mtx_lock(&sc->mtx);
	for (i = 0; i < OCTEP_HOLD_WAYS && !hit; i++) {
		h = &sc->dp_hold[base + i];
		hit = (h->until != 0 && (h->until - ticks) > 0 && octep_tuple_eq(&h->t, t));
	}
	mtx_unlock(&sc->mtx);
	return (hit);
}

/*
 * A programmed direction keeps being punted: ask the far side what state the connection is in.
 *
 * The fast path hands frames of an accelerated connection to the host for reasons the host cannot
 * see from here, and one of them - CONN_RECLAIM_PENDING - means it has stopped using the entry and
 * is waiting for the host to reclaim it. Nothing this driver sent ever did, so an entry that
 * reached that state stayed there, punting every frame, until the sweep found pf's state gone.
 * Read back: still VALID with our revision, a few punts are normal and the count starts again;
 * anything else, the connection is taken out, reclaimed if it is pending, and the next candidate
 * re-creates it with the next revision if pf still has the state.
 *
 * Returns 0 when the connection stands, EJUSTRETURN when it was taken out and its entry is free,
 * EBUSY when it was given back and its revival waits for a segment of each direction (see below),
 * and ETIMEDOUT when the far side did not answer - the read, or one of the commands that take the
 * connection out, and in that case the entry is still in the table, marked for the audit. The
 * caller must not go on to another command after ETIMEDOUT: each waits two seconds with this
 * lock held.
 *
 * REVIVED IN PLACE, when it can be. The fast path's handler for FLOW_CREATE_FP copies the request's
 * connection entry over the table's without looking at what is there - read in the vendor's source
 * and in the device's module - so the same command this driver creates a connection with, sent
 * again for the same index and the SAME revision with no microflow in it, puts a RECLAIM_PENDING
 * entry back to VALID, and the next frame is forwarded: the fast path reads the state from the
 * table on every frame and its microflows name the connection by index and revision. One command
 * instead of five, and nothing to wait for. It is done only for a connection given back by the
 * retransmission rule - the entry's own counter says so, and no frame of the connection has been
 * seen carrying FIN or RST - with both of its directions in hardware. A connection that is
 * ending, or half of one, goes out as before.
 *
 * What reviving does not do is save the microflows. From the first frame that found the connection
 * given back, each of them is on a terminal timer of about five seconds that no command lengthens;
 * they go, the frames arrive under new identities, and the attach path programs those against the
 * same connection. That is the same road an idle connection takes back, and the receive path's
 * kick is what makes both reactions prompt.
 *
 * And a connection in a long burst of duplicate acknowledgements is given back again a dozen
 * packets after every revival - which is the fast path saying the host should see this. So each
 * revival buys a wait before the connection is looked at again, doubling while the give-backs keep
 * coming and starting over after a second without one.
 *
 * Called with the softc lock held.
 */
static int
octep_conn_probe(struct octep_softc *sc, struct octep_conn *c)
{
	struct octep_pf_tuple held[2];
	uint8_t seq[OCTEP_TCP_SEQ_LEN];
	uint32_t state = 0, rev = 0, retrans = 0;
	int err, rerr, hold = 0;

	mtx_assert(&sc->mtx, MA_OWNED);
	err = octep_rpc_conn_read(sc, c->idx, &state, &rev, &retrans);
	if (err == ETIMEDOUT) {
		sc->dp_probe_timeout++;
		return (ETIMEDOUT);
	}
	if (err == 0 && state == OCTEP_CONN_VALID && rev == c->conn_rev) {
		sc->dp_probe_valid++;
		return (0);
	}
	/* A connection whose live user is no longer listed is not revived: its client has gone. */
	if (err == 0 && state == OCTEP_CONN_RECLAIM_PENDING && rev == c->conn_rev &&
	    sc->dp_revive != 0 && retrans >= OCTEP_CONN_MAX_RETRANS && !c->closing &&
	    (c->luid == 0 || octep_lu_listed(sc, c->luid)) &&
	    !c->doomed && c->mf[0].state == OCTEP_MF_PROGRAMMED &&
	    c->mf[1].state == OCTEP_MF_PROGRAMMED &&
	    !(hold = (sc->dp_revive_hold != 0 && sc->dp_hold != NULL &&
	    (u_int)(ticks - c->rv_first) < (u_int)(OCTEP_REVIVE_STREAK * hz) &&
	    c->rv_streak >= OCTEP_REVIVE_STREAK))) {
		/*
		 * A connection whose window was checked goes back unchecked (dp.tcp_revive 0, the
		 * default), and its take-back leaves pf sloppy, as every revival did before issue
		 * #679. The copy FLOW_CREATE_FP makes is the state the fast path works from -
		 * measured: a revival with the check off let segments 64 MiB outside the window
		 * through - so the revival decides whether the check goes on, and no window it can
		 * send has been measured free of false drops yet. Two instruments:
		 *
		 * dp.tcp_revive 2 sends a window made from the latest segment the host saw in each
		 * direction (octep_tcp_segs), waiting up to OCTEP_TCP_SEG_WAIT for them and answering
		 * EBUSY meanwhile - the revival comes at the first frame the fast path gave back, and
		 * the other direction may not have crossed the host yet - and when there are none it
		 * goes back unchecked for this revival only (tcp_able). Measured on the XGS 3300, four
		 * streams down at 9.6 to 9.9 Gbit/s: segments injected 64 MiB ahead dropped after a
		 * revival, and ordinary segments dropped by the check in some runs, from none to 510
		 * in 20 seconds (octep_tcp_from_segs has the runs). And some revivals found the
		 * opener's direction silent for a quarter of a second while the connection was given
		 * back: its acknowledgements were not reaching the host.
		 *
		 * dp.tcp_revive 1 reclaims the connection first for the window the fast path held for
		 * it and sends that back (octep_tcp_rearm), FLOW_CREATE_FP putting a RECLAIMED entry
		 * back to VALID as it does a RECLAIM_PENDING one. Measured, four streams down with 140
		 * to 165 revivals a run: none, none, 14 and 151 ordinary segments dropped by the check
		 * in four runs, and none in any run with 0. Most likely the frames the host forwarded
		 * are not in those numbers, and the first segments after a revival are a loss
		 * recovery's, old enough to fit the old window, so they end the liberal bits at once.
		 */
		if (c->tcp_able && sc->dp_tcp_revive == 2) {
			char why[96];
			int waited;

			waited = c->tcp_wait != 0 ? (int)((u_int)ticks - (u_int)c->tcp_wait) : 0;
			if (octep_tcp_segs(sc, c->tuple, c->tcp_scale, c->tcp, why, sizeof(why),
			    c->rv_tick, 1)) {
				c->tcp_on = 1;
				sc->dp_tcp_from_segs++;
				snprintf(sc->dp_tcp_segs_last, sizeof(sc->dp_tcp_segs_last),
				    "connection %u revived with a window from the host's latest segments, "
				    "after %d ms", c->idx, waited * 1000 / hz);
			} else if (waited < OCTEP_TCP_SEG_WAIT) {
				if (c->tcp_wait == 0) {
					c->tcp_wait = ticks != 0 ? ticks : 1;
					sc->dp_tcp_seg_waits++;
				}
				return (EBUSY);
			} else {
				if (c->tcp_on)
					sc->dp_tcp_revived++;
				c->tcp_on = 0;
				memset(c->tcp, 0, OCTEP_CONN_TCP_LEN);
				snprintf(sc->dp_tcp_segs_last, sizeof(sc->dp_tcp_segs_last),
				    "connection %u revived unchecked after %d ms - %s", c->idx,
				    waited * 1000 / hz, why);
			}
			c->tcp_wait = 0;
		} else if (c->tcp_on && sc->dp_tcp_revive == 1) {
			rerr = octep_rpc_conn_reclaim(sc, c, seq);
			if (rerr == ETIMEDOUT)
				return (ETIMEDOUT);
			if (rerr == 0)
				octep_tcp_rearm(c->tcp, seq);
			else {
				c->tcp_on = 0;
				memset(c->tcp, 0, OCTEP_CONN_TCP_LEN);
			}
		} else if (c->tcp_on) {
			c->tcp_on = 0;
			memset(c->tcp, 0, OCTEP_CONN_TCP_LEN);
			sc->dp_tcp_revived++;
		}
		rerr = octep_rpc_flow_create(sc, c, 0);
		if (rerr == 0)
			octep_ips_mark(sc, c);
		if (rerr == ETIMEDOUT)
			return (ETIMEDOUT);	/* still in the table, as it was; asked again later */
		if (rerr == 0) {
			c->rv_wait = ((u_int)(ticks - c->rv_tick) < (u_int)hz) ?
			    imin(imax(c->rv_wait * 2, OCTEP_REVIVE_WAIT_MIN),
			    OCTEP_REVIVE_WAIT_MAX) : OCTEP_REVIVE_WAIT_MIN;
			c->rv_tick = ticks;
			if ((u_int)(ticks - c->rv_first) >= (u_int)(OCTEP_REVIVE_STREAK * hz)) {
				c->rv_first = ticks;
				c->rv_streak = 0;
			}
			c->rv_streak++;
			sc->dp_flow_revived++;
			return (0);
		}
		sc->dp_revive_refused++;
	}
	/*
	 * Every other answer takes the connection out, and until #287 three of the four did it
	 * without a trace: only RECLAIM_PENDING had a counter. An hour of load made 1,226
	 * connections and could account for 887 of their endings; the rest went through here.
	 */
	if (err != 0)
		sc->dp_probe_read_err++;
	else if (state == OCTEP_CONN_RECLAIM_PENDING)
		sc->dp_flow_pending++;
	else if (state == OCTEP_CONN_VALID)
		sc->dp_probe_rev_mismatch++;
	else
		sc->dp_probe_state_other++;
	if (err == 0 && state == OCTEP_CONN_RECLAIM_PENDING && c->closing)
		sc->dp_flow_closing++;
	if (ppsratecheck(&sc->dp_probe_last, &sc->dp_probe_curpps, 1))
		device_printf(sc->dev, "dp: connection %u rev %u taken out by the probe: read %d, "
		    "far side state %u rev %u, %u identical in a row%s\n", c->idx, c->conn_rev, err,
		    state, rev, retrans, c->closing ? ", FIN or RST seen" : "");
	if (err == 0 && state == OCTEP_CONN_RECLAIM_PENDING && c->ipsec != 0 &&
	    c->mf[1 - c->enc_dir].state != OCTEP_MF_PROGRAMMED)
		octep_ipsec_backoff_put(sc, &c->tuple[c->enc_dir]);
	if (hold) {
		held[0] = c->tuple[0];
		held[1] = c->tuple[1];
	}
	if (octep_conn_takeout(sc, c, err == 0 && state == OCTEP_CONN_RECLAIM_PENDING ?
	    OCTEP_TAKEOUT_GIVEN : 0) == ETIMEDOUT)
		return (ETIMEDOUT);
	if (hold) {
		/* taken out at the end of its streak: it waits with the host before it is made again */
		octep_hold_put(sc, &held[0]);
		octep_hold_put(sc, &held[1]);
		sc->dp_revive_held++;
	}
	return (EJUSTRETURN);
}

/*
 * What the owner keeps with the host: struct octep_flow_held and struct octep_flow_excl, in octep.h.
 */

/* Is this IPv6 address, network order, in the network an exclusion names? */
static int
octep_flow_v6_in(const uint32_t *a, const struct octep_flow_excl *x)
{
	uint32_t m;
	int w, bits;

	for (w = 0, bits = x->plen; w < 4 && bits > 0; w++, bits -= 32) {
		m = bits >= 32 ? 0xffffffffu : htonl(0xffffffffu << (32 - bits));
		if ((a[w] & m) != (x->a6[w] & m))
			return (0);
	}
	return (1);
}

/*
 * Does one of the owner's exclusions name this direction of a connection: either of its addresses
 * in a listed network, or either of its ports a listed one? Called with the softc lock held.
 */
static int
octep_flow_excl_names(const struct octep_softc *sc, const struct octep_pf_tuple *t)
{
	const struct octep_flow_excl *x;
	uint32_t m;
	int i;

	for (i = 0; i < sc->dp_nexcl; i++) {
		x = &sc->dp_excl[i];
		switch (x->kind) {
		case 'p':
			if (ntohs(t->sport) == x->port || ntohs(t->dport) == x->port)
				return (1);
			break;
		case 4:
			if (t->af != AF_INET)
				break;
			m = x->plen == 0 ? 0 : 0xffffffffu << (32 - x->plen);
			if ((ntohl(t->sip) & m) == (x->a4 & m) || (ntohl(t->dip) & m) == (x->a4 & m))
				return (1);
			break;
		case 6:
			if (t->af == AF_INET6 && (octep_flow_v6_in(t->sip6, x) || octep_flow_v6_in(t->dip6, x)))
				return (1);
			break;
		}
	}
	return (0);
}

/* Is this direction one of a connection handed back from the Flows page? Softc lock held. */
static int
octep_flow_held_names(const struct octep_softc *sc, const struct octep_pf_tuple *t)
{
	int i;

	for (i = 0; i < OCTEP_FLOW_HELD_MAX; i++)
		if (sc->dp_held[i].used && (octep_tuple_eq(&sc->dp_held[i].tuple[0], t) ||
		    octep_tuple_eq(&sc->dp_held[i].tuple[1], t)))
			return (1);
	return (0);
}

/*
 * The question octep_dp_flow_make asks, before it reserves anything: does the owner keep this
 * connection with the host? n tuples, one (the frame's own, before pf is asked) or two (both
 * directions, once pf has said what they are - the reply direction of a translated connection
 * carries the outside addresses, so an excluded inside host is found on the original one).
 */
static int
octep_flow_kept(struct octep_softc *sc, const struct octep_pf_tuple *tup, int n)
{
	int i, kept = 0;

	mtx_lock(&sc->mtx);
	for (i = 0; i < n && !kept; i++)
		kept = octep_flow_held_names(sc, &tup[i]) || octep_flow_excl_names(sc, &tup[i]);
	mtx_unlock(&sc->mtx);
	return (kept);
}

/* Keep a connection's two directions with the host until pf lets it go. Softc lock held. */
static void
octep_flow_hold(struct octep_softc *sc, const struct octep_conn *c)
{
	struct octep_flow_held *h;
	int i;

	mtx_assert(&sc->mtx, MA_OWNED);
	for (i = 0; i < OCTEP_FLOW_HELD_MAX && sc->dp_held[i].used; i++)
		;
	if (i == OCTEP_FLOW_HELD_MAX)
		i = (int)(sc->dp_held_next++ % OCTEP_FLOW_HELD_MAX);	/* all in use: the oldest place */
	h = &sc->dp_held[i];
	h->used = 1;
	h->pf_dir = c->pf_dir;
	h->probe_dir = c->probe_dir;
	h->tuple[0] = c->tuple[0];
	h->tuple[1] = c->tuple[1];
}

/*
 * The canonical list scripts/xgs/flow_exclude.php writes: tokens separated by spaces or commas,
 * each "a4:" and eight hex digits of an IPv4 network, host order, then "/" and its prefix length;
 * "a6:" and thirty-two hex digits of an IPv6 network, then "/" and its length; or "p:" and a port.
 * The owner's own list - addresses as they are written - is validated and turned into this in
 * userland, so the kernel parses nothing but digits. Empty is no exclusion.
 */
static int
octep_flow_excl_parse(const char *in, struct octep_flow_excl *x, int *np)
{
	const char *p = in;
	char word[9], *end;
	u_long v;
	int n = 0, w;

	for (;;) {
		while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n')
			p++;
		if (*p == '\0')
			break;
		if (n == OCTEP_FLOW_EXCL_MAX)
			return (E2BIG);
		memset(&x[n], 0, sizeof(x[n]));
		if (strncmp(p, "p:", 2) == 0) {
			v = strtoul(p + 2, &end, 10);
			if (end == p + 2 || v == 0 || v > 65535)
				return (EINVAL);
			x[n].kind = 'p';
			x[n].port = (uint16_t)v;
			p = end;
		} else if (strncmp(p, "a4:", 3) == 0) {
			if (strnlen(p + 3, 9) < 9 || p[11] != '/')
				return (EINVAL);
			memcpy(word, p + 3, 8);
			word[8] = '\0';
			v = strtoul(word, &end, 16);
			if (end != word + 8)
				return (EINVAL);
			x[n].a4 = (uint32_t)v;
			p += 12;
			v = strtoul(p, &end, 10);
			if (end == p || v > 32)
				return (EINVAL);
			x[n].kind = 4;
			x[n].plen = (uint8_t)v;
			p = end;
		} else if (strncmp(p, "a6:", 3) == 0) {
			p += 3;
			if (strnlen(p, 33) < 33 || p[32] != '/')
				return (EINVAL);
			for (w = 0; w < 4; w++, p += 8) {
				memcpy(word, p, 8);
				word[8] = '\0';
				v = strtoul(word, &end, 16);
				if (end != word + 8)
					return (EINVAL);
				x[n].a6[w] = htonl((uint32_t)v);
			}
			p++;
			v = strtoul(p, &end, 10);
			if (end == p || v > 128)
				return (EINVAL);
			x[n].kind = 6;
			x[n].plen = (uint8_t)v;
			p = end;
		} else
			return (EINVAL);
		if (*p != '\0' && *p != ' ' && *p != ',' && *p != '\t' && *p != '\n')
			return (EINVAL);
		n++;
	}
	*np = n;
	return (0);
}

/*
 * dp.flow_exclude: the list above. A list that does not parse is refused whole, and the one in
 * force stays. One that does is in force at once, and what is in hardware now that it names goes
 * back to the host in the same write - one entry at a time under the lock, as the sweep does it.
 */
static int
octep_sysctl_dp_flow_exclude(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct octep_flow_excl *x;
	struct octep_conn *c;
	char *in;
	int error, n, k, i;

	if (req->newptr == NULL)
		return (sysctl_handle_string(oidp, sc->dp_excl_text, sizeof(sc->dp_excl_text), req));
	in = malloc(OCTEP_FLOW_EXCL_TEXT, M_TEMP, M_WAITOK | M_ZERO);
	x = malloc(sizeof(*x) * OCTEP_FLOW_EXCL_MAX, M_TEMP, M_WAITOK | M_ZERO);
	n = 0;
	error = sysctl_handle_string(oidp, in, OCTEP_FLOW_EXCL_TEXT, req);
	if (error == 0)
		error = octep_flow_excl_parse(in, x, &n);
	if (error == 0) {
		mtx_lock(&sc->mtx);
		memcpy(sc->dp_excl, x, sizeof(*x) * n);
		sc->dp_nexcl = n;
		strlcpy(sc->dp_excl_text, in, sizeof(sc->dp_excl_text));
		for (k = (int)sc->dp_conn_nlive - 1; k >= 0; k--) {
			if ((uint32_t)k >= sc->dp_conn_nlive)
				continue;
			i = (int)sc->dp_conn_live[k];
			c = &sc->dp_conn[i];
			if (!c->used || c->doomed || (!octep_flow_excl_names(sc, &c->tuple[0]) &&
			    !octep_flow_excl_names(sc, &c->tuple[1])))
				continue;
			if (octep_conn_takeout(sc, c, 1) == ETIMEDOUT)
				break;		/* the audit finishes the doomed one; the rest go by their punts */
			sc->dp_flow_excluded++;
		}
		mtx_unlock(&sc->mtx);
		octep_dp_handback_run(sc);
	}
	free(x, M_TEMP);
	free(in, M_TEMP);
	return (error);
}

/*
 * The canonical table scripts/xgs/cp_users.py writes for dp.live_users: tokens separated by spaces,
 * each "a4:" and the eight hex digits of an IPv4 address, host order, or "a6:" and the thirty-two of
 * an IPv6 one, then "=" and the live user, 1 to 65535. The addresses are the Captive Portal's own,
 * validated in userland; the kernel parses hex and decimal only. Empty is no live user.
 */
static int
octep_lu_parse(const char *in, struct octep_live_user *u, int *np)
{
	const char *p = in;
	char word[9], *end;
	u_long v;
	int n = 0, w;

	for (;;) {
		while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n')
			p++;
		if (*p == '\0')
			break;
		if (n == OCTEP_LIVE_USERS_MAX)
			return (E2BIG);
		memset(&u[n], 0, sizeof(u[n]));
		if (strncmp(p, "a4:", 3) == 0) {
			if (strnlen(p + 3, 9) < 9 || p[11] != '=')
				return (EINVAL);
			memcpy(word, p + 3, 8);
			word[8] = '\0';
			v = strtoul(word, &end, 16);
			if (end != word + 8)
				return (EINVAL);
			u[n].kind = 4;
			u[n].a4 = (uint32_t)v;
			p += 12;
		} else if (strncmp(p, "a6:", 3) == 0) {
			p += 3;
			if (strnlen(p, 33) < 33 || p[32] != '=')
				return (EINVAL);
			for (w = 0; w < 4; w++, p += 8) {
				memcpy(word, p, 8);
				word[8] = '\0';
				v = strtoul(word, &end, 16);
				if (end != word + 8)
					return (EINVAL);
				u[n].a6[w] = htonl((uint32_t)v);
			}
			u[n].kind = 6;
			p++;
		} else
			return (EINVAL);
		v = strtoul(p, &end, 10);
		if (end == p || v == 0 || v >= OCTEP_LUID_MAX)
			return (EINVAL);
		u[n].luid = (uint16_t)v;
		p = end;
		if (*p != '\0' && *p != ' ' && *p != ',' && *p != '\t' && *p != '\n')
			return (EINVAL);
		n++;
	}
	*np = n;
	return (0);
}

/* Is this entry - the same address with the same user - in that list? */
static int
octep_lu_in(const struct octep_live_user *e, const struct octep_live_user *l, int n)
{
	int i;

	for (i = 0; i < n; i++)
		if (l[i].luid == e->luid && l[i].kind == e->kind && (e->kind == 4 ?
		    l[i].a4 == e->a4 : memcmp(l[i].a6, e->a6, sizeof(e->a6)) == 0))
			return (1);
	return (0);
}

/*
 * dp.live_users: the table above, written whole each time. One that does not parse is refused whole,
 * and the one in force stays. Each user that went is deleted from the fast path's table - which gives
 * back every connection made with it - and those connections are taken out here too, so nothing
 * revives them; each user that came is added, and a connection already in hardware with an end at
 * its address and no user yet is given it in place - the Captive Portal's login and this table are a
 * poll apart. Written again unchanged, it does nothing. Reading shows the table and the counts.
 */
static int
octep_sysctl_dp_live_users(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct octep_live_user *nu, *old, *u;
	struct octep_conn *c;
	struct sbuf *sb;
	uint64_t cnt[6];
	char *in;
	int error, n, nold, i, k, ci;
	uint16_t lu;
	uint8_t side;

	nu = malloc(sizeof(*nu) * OCTEP_LIVE_USERS_MAX, M_TEMP, M_WAITOK | M_ZERO);
	old = malloc(sizeof(*old) * OCTEP_LIVE_USERS_MAX, M_TEMP, M_WAITOK | M_ZERO);
	if (req->newptr == NULL) {
		/* copied under the lock, printed after it: the sysctl's own copy-out may sleep */
		mtx_lock(&sc->mtx);
		n = sc->dp_nlu;
		memcpy(nu, sc->dp_lu, sizeof(*nu) * n);
		cnt[0] = sc->dp_lu_added;
		cnt[1] = sc->dp_lu_deleted;
		cnt[2] = sc->dp_lu_made;
		cnt[3] = sc->dp_lu_set;
		cnt[4] = sc->dp_lu_out;
		cnt[5] = sc->dp_lu_failed;
		mtx_unlock(&sc->mtx);
		sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
		sbuf_printf(sb, "%d listed | users added %ju, deleted %ju | connections made with one %ju, "
		    "given one in place %ju, taken out when theirs went %ju | commands not answered 0 %ju\n",
		    n, (uintmax_t)cnt[0], (uintmax_t)cnt[1], (uintmax_t)cnt[2], (uintmax_t)cnt[3],
		    (uintmax_t)cnt[4], (uintmax_t)cnt[5]);
		for (i = 0; i < n; i++) {
			if (nu[i].kind == 4)
				sbuf_printf(sb, "a4:%08x=%u", nu[i].a4, nu[i].luid);
			else
				sbuf_printf(sb, "a6:%08x%08x%08x%08x=%u", ntohl(nu[i].a6[0]),
				    ntohl(nu[i].a6[1]), ntohl(nu[i].a6[2]), ntohl(nu[i].a6[3]), nu[i].luid);
			/* the user's counts, on its first entry: packets and bytes from it, to it */
			if (i == 0 || nu[i - 1].luid != nu[i].luid)
				sbuf_printf(sb, " in %ju %ju out %ju %ju at %jd", (uintmax_t)nu[i].pkts[0],
				    (uintmax_t)nu[i].bytes[0], (uintmax_t)nu[i].pkts[1],
				    (uintmax_t)nu[i].bytes[1], (intmax_t)nu[i].moved);
			sbuf_printf(sb, "\n");
		}
		error = sbuf_finish(sb);
		sbuf_delete(sb);
		free(old, M_TEMP);
		free(nu, M_TEMP);
		return (error);
	}
	in = malloc(OCTEP_LIVE_USERS_TEXT, M_TEMP, M_WAITOK | M_ZERO);
	n = 0;
	error = sysctl_handle_string(oidp, in, OCTEP_LIVE_USERS_TEXT, req);
	if (error == 0)
		error = octep_lu_parse(in, nu, &n);
	if (error == 0) {
		qsort(nu, n, sizeof(*nu), octep_lu_cmp);
		mtx_lock(&sc->mtx);
		nold = sc->dp_nlu;
		memcpy(old, sc->dp_lu, sizeof(*old) * nold);
		/* a user that stays keeps what was counted for it */
		for (i = 0; i < n; i++) {
			if (i > 0 && nu[i - 1].luid == nu[i].luid)
				continue;
			if ((u = octep_lu_find(old, nold, nu[i].luid)) == NULL)
				continue;
			memcpy(nu[i].pkts, u->pkts, sizeof(u->pkts));
			memcpy(nu[i].bytes, u->bytes, sizeof(u->bytes));
			nu[i].moved = u->moved;
		}
		memcpy(sc->dp_lu, nu, sizeof(*nu) * n);
		sc->dp_nlu = n;
		/*
		 * The users that went: deleted, and their connections out - once for each user, and only
		 * when none of its addresses stays (a session keeps its user while it has one; both tables
		 * are sorted by user).
		 */
		for (i = 0; i < nold; i++) {
			lu = old[i].luid;
			if ((i > 0 && old[i - 1].luid == lu) || octep_lu_find(nu, n, lu) != NULL)
				continue;
			if (octep_rpc_live_uid(sc, 0, lu) == 0 && sc->rpc_last_rc == 0)
				sc->dp_lu_deleted++;
			else
				sc->dp_lu_failed++;
			for (k = (int)sc->dp_conn_nlive - 1; k >= 0; k--) {
				if ((uint32_t)k >= sc->dp_conn_nlive)
					continue;
				ci = (int)sc->dp_conn_live[k];
				c = &sc->dp_conn[ci];
				if (!c->used || c->doomed || c->luid != lu)
					continue;
				if (octep_conn_takeout(sc, c, 1) == ETIMEDOUT)
					break;		/* the audit finishes the doomed one */
				sc->dp_lu_out++;
			}
		}
		/*
		 * The addresses that came: a user new to the table added once, and the connections in
		 * hardware with an end at the address given their user.
		 */
		for (i = 0; i < n; i++) {
			if (octep_lu_in(&nu[i], old, nold))
				continue;
			lu = nu[i].luid;
			if ((i == 0 || nu[i - 1].luid != lu) && octep_lu_find(old, nold, lu) == NULL) {
				if (octep_rpc_live_uid(sc, 1, lu) == 0 && sc->rpc_last_rc == 0)
					sc->dp_lu_added++;
				else
					sc->dp_lu_failed++;
			}
			for (k = 0; k < (int)sc->dp_conn_nlive; k++) {
				c = &sc->dp_conn[sc->dp_conn_live[k]];
				if (!c->used || c->doomed || c->luid != 0 ||
				    octep_lu_of(sc, &c->tuple[0], &side) != lu)
					continue;
				/*
				 * Its counts so far are not the user's: it may be older than the session
				 * (one opened toward the address, which the portal's logout does not kill).
				 */
				if (octep_rpc_conn_luid(sc, c, lu) == 0 && sc->rpc_last_rc == 0) {
					c->luid = lu;
					c->lu_side = side;
					sc->dp_lu_set++;
				} else
					sc->dp_lu_failed++;
			}
		}
		mtx_unlock(&sc->mtx);
		octep_dp_handback_run(sc);
	}
	free(in, M_TEMP);
	free(old, M_TEMP);
	free(nu, M_TEMP);
	return (error);
}

/*
 * dp.flow_release: write a connection's index - dp.flow_table's first column, the Flows page's Slot -
 * to hand that connection back to the host, and keep it there until pf lets its state go. Reading
 * shows what the last write did.
 */
static int
octep_sysctl_dp_flow_release(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	char in[16], *end;
	u_long idx;
	int error, err;

	if (req->newptr == NULL)
		return (sysctl_handle_string(oidp, sc->dp_release_result, sizeof(sc->dp_release_result),
		    req));
	in[0] = '\0';
	error = sysctl_handle_string(oidp, in, sizeof(in), req);
	if (error != 0)
		return (error);
	idx = strtoul(in, &end, 10);
	mtx_lock(&sc->mtx);
	if (end == in || sc->dp_conn == NULL || idx < 2 || idx >= sc->dp_conn_cap ||
	    !sc->dp_conn[idx].used || sc->dp_conn[idx].doomed) {
		snprintf(sc->dp_release_result, sizeof(sc->dp_release_result),
		    "no connection %s in hardware\n", in);
		mtx_unlock(&sc->mtx);
		return (0);
	}
	octep_flow_hold(sc, &sc->dp_conn[idx]);
	err = octep_conn_takeout(sc, &sc->dp_conn[idx], 1);
	if (err == 0)
		sc->dp_flow_released++;
	snprintf(sc->dp_release_result, sizeof(sc->dp_release_result), err == 0 ?
	    "connection %lu handed back to the host, and kept there until pf lets it go\n" :
	    "connection %lu: the coprocessor did not answer (%d); the audit finishes it, and it is "
	    "kept with the host\n", idx, err);
	mtx_unlock(&sc->mtx);
	octep_dp_handback_run(sc);
	return (0);
}

/*
 * dp.flow_verdict: write "<index> <verdict>" to change a connection's verdict in place on the
 * coprocessor - 1 DROP, 2 CUT_THRU (discovery 3). scripts/xgs/hwdrop.py writes DROP for every
 * connection a block list or a TLS fingerprint cuts, before their pf states are killed; it acts
 * only on a connection made with the microflow action IPS and the fast path's IPS switch on, which
 * is what XGS: Settings "Cut in hardware" makes (dp.mflow_action, offload.sh). pf's state is left
 * alone here, and a dropped connection is kept after pf lets it go while its frames keep coming
 * (octep_flow_sweep). 0, SEND_TO_IPS, is refused: there is no inspector behind it. Reading shows
 * what the last write did.
 */
static int
octep_sysctl_dp_flow_verdict(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	char in[24], *end, *end2;
	u_long idx, v;
	int error, err;

	if (req->newptr == NULL)
		return (sysctl_handle_string(oidp, sc->dp_verdict_result, sizeof(sc->dp_verdict_result),
		    req));
	in[0] = '\0';
	error = sysctl_handle_string(oidp, in, sizeof(in), req);
	if (error != 0)
		return (error);
	idx = strtoul(in, &end, 10);
	v = strtoul(end, &end2, 10);
	mtx_lock(&sc->mtx);
	if (end == in || end2 == end || (v != OCTEP_CONN_VERDICT_SEND_TO_IPS &&
	    v != OCTEP_CONN_VERDICT_DROP && v != OCTEP_CONN_VERDICT_CUT_THRU)) {
		snprintf(sc->dp_verdict_result, sizeof(sc->dp_verdict_result),
		    "write \"<index> <verdict>\": 0 inspects, 1 drops, 2 forwards\n");
		mtx_unlock(&sc->mtx);
		return (0);
	}
	if (sc->dp_conn == NULL || idx < 2 || idx >= sc->dp_conn_cap ||
	    !sc->dp_conn[idx].used || sc->dp_conn[idx].doomed) {
		snprintf(sc->dp_verdict_result, sizeof(sc->dp_verdict_result),
		    "no connection %lu in hardware\n", idx);
		mtx_unlock(&sc->mtx);
		return (0);
	}
	err = octep_rpc_conn_verdict(sc, &sc->dp_conn[idx], (uint32_t)v);
	if (err == 0) {
		sc->dp_conn[idx].dropped = (v == OCTEP_CONN_VERDICT_DROP);
		sc->dp_conn[idx].drop_alive = time_uptime;
		if (v == OCTEP_CONN_VERDICT_DROP)
			sc->dp_flow_dropped++;
	}
	snprintf(sc->dp_verdict_result, sizeof(sc->dp_verdict_result), err == 0 ?
	    "connection %lu: verdict %lu (%s) on the coprocessor\n" :
	    "connection %lu: verdict %lu (%s) refused, rc %u (%d)\n", idx, v,
	    v == OCTEP_CONN_VERDICT_DROP ? "drop" : v == OCTEP_CONN_VERDICT_SEND_TO_IPS ?
	    "to the inspector" : "forward", sc->rpc_last_rc, err);
	mtx_unlock(&sc->mtx);
	return (0);
}

/* dp.live_uid: write "add <user>" or "del <user>" (discovery 5); reading shows the last result. */
static int
octep_sysctl_dp_live_uid(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	char in[24], *end;
	u_long uid;
	int add, error, err;

	if (req->newptr == NULL)
		return (sysctl_handle_string(oidp, sc->dp_luid_result, sizeof(sc->dp_luid_result), req));
	in[0] = '\0';
	error = sysctl_handle_string(oidp, in, sizeof(in), req);
	if (error != 0)
		return (error);
	add = strncmp(in, "add ", 4) == 0;
	uid = strtoul(in + 4, &end, 10);
	mtx_lock(&sc->mtx);
	if ((!add && strncmp(in, "del ", 4) != 0) || end == in + 4 || uid >= OCTEP_LUID_MAX) {
		snprintf(sc->dp_luid_result, sizeof(sc->dp_luid_result),
		    "write \"add <user>\" or \"del <user>\", the user below %d\n", OCTEP_LUID_MAX);
		mtx_unlock(&sc->mtx);
		return (0);
	}
	err = octep_rpc_live_uid(sc, add, (uint32_t)uid);
	snprintf(sc->dp_luid_result, sizeof(sc->dp_luid_result), "live user %lu %s, rc %u (%d)\n", uid,
	    add ? "added" : "deleted", sc->rpc_last_rc, err);
	mtx_unlock(&sc->mtx);
	return (0);
}

/*
 * dp.conn_luid: write "<index> <user>" to set a connection's live user in place (discovery 5); the
 * entry is read back after, to show what the fast path holds.
 */
static int
octep_sysctl_dp_conn_luid(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	char in[24], *end, *end2;
	u_long idx, uid;
	uint32_t flags, held;
	u_int rc;
	int error, err;

	if (req->newptr == NULL)
		return (sysctl_handle_string(oidp, sc->dp_luid_result, sizeof(sc->dp_luid_result), req));
	in[0] = '\0';
	error = sysctl_handle_string(oidp, in, sizeof(in), req);
	if (error != 0)
		return (error);
	idx = strtoul(in, &end, 10);
	uid = strtoul(end, &end2, 10);
	mtx_lock(&sc->mtx);
	if (end == in || end2 == end || uid >= OCTEP_LUID_MAX) {
		snprintf(sc->dp_luid_result, sizeof(sc->dp_luid_result),
		    "write \"<index> <user>\", the user below %d\n", OCTEP_LUID_MAX);
		mtx_unlock(&sc->mtx);
		return (0);
	}
	if (sc->dp_conn == NULL || idx < 2 || idx >= sc->dp_conn_cap ||
	    !sc->dp_conn[idx].used || sc->dp_conn[idx].doomed) {
		snprintf(sc->dp_luid_result, sizeof(sc->dp_luid_result),
		    "no connection %lu in hardware\n", idx);
		mtx_unlock(&sc->mtx);
		return (0);
	}
	err = octep_rpc_conn_luid(sc, &sc->dp_conn[idx], (uint32_t)uid);
	rc = sc->rpc_last_rc;
	if (octep_rpc_conn_peek(sc, (uint32_t)idx, &flags, &held) != 0)
		flags = held = 0xffffffffu;
	snprintf(sc->dp_luid_result, sizeof(sc->dp_luid_result), "connection %lu: live user %lu, rc %u "
	    "(%d); the entry now: live user %u, flags 0x%08x (verdict %u, state %u)\n", idx, uid, rc, err,
	    held, flags, (flags >> 21) & 3, flags >> 30);
	mtx_unlock(&sc->mtx);
	return (0);
}

/*
 * dp.conn_qos: write "<index> <priority 0|1> [<valid 0|1> [<meter> [<directions 1|2|3>]]]" to set a
 * connection's QoS words in place (discovery 6) - the original direction (1), the reply (2) or both
 * (3, the default); valid defaults to 1 and the meter to 0. The entry is read back after, to show
 * what the fast path holds. A connection made again, or revived, is made with both words 0.
 */
static int
octep_sysctl_dp_conn_qos(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	char in[48], *p, *end;
	u_long v[5] = { 0, 0, 1, 0, 3 };
	uint32_t flags, held, word, qo, qr;
	u_int rc;
	int error, err, n;

	if (req->newptr == NULL)
		return (sysctl_handle_string(oidp, sc->dp_luid_result, sizeof(sc->dp_luid_result), req));
	in[0] = '\0';
	error = sysctl_handle_string(oidp, in, sizeof(in), req);
	if (error != 0)
		return (error);
	for (n = 0, p = in; n < 5; n++, p = end) {
		while (*p == ' ')
			p++;
		if (*p == '\0')
			break;
		v[n] = strtoul(p, &end, 10);
		if (end == p)
			break;
	}
	mtx_lock(&sc->mtx);
	if (n < 2 || v[1] > 1 || v[2] > 1 || v[3] > OCTEP_QOS_METER_MAX || v[4] < 1 || v[4] > 3) {
		snprintf(sc->dp_luid_result, sizeof(sc->dp_luid_result),
		    "write \"<index> <priority 0|1> [<valid 0|1> [<meter> [<directions 1|2|3>]]]\"\n");
		mtx_unlock(&sc->mtx);
		return (0);
	}
	if (sc->dp_conn == NULL || v[0] < 2 || v[0] >= sc->dp_conn_cap ||
	    !sc->dp_conn[v[0]].used || sc->dp_conn[v[0]].doomed) {
		snprintf(sc->dp_luid_result, sizeof(sc->dp_luid_result),
		    "no connection %lu in hardware\n", v[0]);
		mtx_unlock(&sc->mtx);
		return (0);
	}
	word = (v[1] ? OCTEP_QOS_PRI : 0) | (v[2] ? OCTEP_QOS_VALID : 0) |
	    ((uint32_t)v[3] << OCTEP_QOS_METER_SHIFT);
	err = octep_rpc_conn_qos(sc, &sc->dp_conn[v[0]],
	    ((v[4] & 1) ? OCTEP_CONN_MODIFY_QOS_ORIG : 0) | ((v[4] & 2) ? OCTEP_CONN_MODIFY_QOS_REPLY : 0),
	    word, word);
	rc = sc->rpc_last_rc;
	qo = qr = 0xffffffffu;
	if (octep_rpc_conn_peek(sc, (uint32_t)v[0], &flags, &held) == 0 && sc->rpc_last_len >= 28) {
		qo = le32dec(sc->rpc_last_reply + 20);
		qr = le32dec(sc->rpc_last_reply + 24);
	}
	snprintf(sc->dp_luid_result, sizeof(sc->dp_luid_result), "connection %lu: QoS word 0x%08x "
	    "to direction%s %lu, rc %u (%d); the entry now: original 0x%08x, reply 0x%08x\n", v[0], word,
	    v[4] == 3 ? "s" : "", v[4], rc, err, qo, qr);
	mtx_unlock(&sc->mtx);
	return (0);
}

/* dp.luid_table: the users 0 to 1023 whose bit is set in the coprocessor's table, read back. */
static int
octep_sysctl_dp_luid_table(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	uint8_t bits[128];
	struct sbuf *sb;
	int n, i, set = 0, error;

	/* read under the lock, written out after it: draining to the reader may sleep */
	mtx_lock(&sc->mtx);
	n = octep_rpc_luid_read(sc, bits, sizeof(bits));
	i = (int)sc->rpc_last_rc;
	mtx_unlock(&sc->mtx);
	sb = sbuf_new_for_sysctl(NULL, NULL, 512, req);
	if (n < 0) {
		sbuf_printf(sb, "read failed: %d, rc %d\n", -n, i);
	} else {
		sbuf_printf(sb, "%d bytes; live users 0 to 1023:", n);
		for (i = 0; i < n * 8 && i < 1024; i++) {
			if (bits[i / 8] & (1u << (i % 8))) {
				sbuf_printf(sb, " %d", i);
				set++;
			}
		}
		sbuf_printf(sb, "%s\n", set ? "" : " none");
	}
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/*
 * A held connection is let go once pf has let its state go: a new connection on the same five
 * tuple is the firewall's to offload again. Asked from the sweep, as the table's own entries are.
 */
static void
octep_flow_held_sweep(struct octep_softc *sc)
{
	struct octep_pf_tuple t;
	int i, pf_dir;

	for (i = 0; i < OCTEP_FLOW_HELD_MAX; i++) {
		mtx_lock(&sc->mtx);
		if (!sc->dp_held[i].used) {
			mtx_unlock(&sc->mtx);
			continue;
		}
		t = sc->dp_held[i].tuple[sc->dp_held[i].probe_dir];
		pf_dir = sc->dp_held[i].pf_dir;
		mtx_unlock(&sc->mtx);
		if (octep_pf_state_exists(&t, pf_dir, NULL))
			continue;
		mtx_lock(&sc->mtx);
		if (sc->dp_held[i].used &&
		    octep_tuple_eq(&sc->dp_held[i].tuple[sc->dp_held[i].probe_dir], &t))
			sc->dp_held[i].used = 0;
		mtx_unlock(&sc->mtx);
	}
}

/*
 * Walk the table and take out every connection whose state pf no longer has.
 *
 * This is the half that makes the other half safe to leave running. A microflow outlives nothing by
 * itself: its own timeout is sixty seconds, which is a long time for a connection the firewall has
 * finished with, and a reused five-tuple inside that window would be forwarded on the strength of a
 * connection that is gone.
 *
 * pf is asked the way the connection was found: by the tuple that found it and in the list that
 * held it. pf_find_state_all_exists is the cheap lookup - it holds no lock on return - which is why
 * it was worth telling apart from the one that does.
 *
 * Called from the link poll, which may sleep and is already periodic. Not from the receive path.
 */
static void
octep_flow_sweep(struct octep_softc *sc)
{
	struct octep_pf_tuple t, tup[2];
	struct octep_ipsec_flow fi;
	uint32_t spgen;
	uint16_t conn_rev;
	uint8_t cipsec, cenc, dropped;
	time_t drop_alive;
	int i, gone, pf_dir, reval, wrong, r, chk;

	if (!octep_pf_present())
		return;

	octep_flow_held_sweep(sc);

	/*
	 * And the second question, asked only when the kernel's policy database has changed since
	 * the last pass: does the policy still say about this connection what it said when the
	 * connection was made? A connection is asked about when it is made and never again by
	 * itself, because a connection in hardware sends the host nothing to ask about - so a
	 * tunnel that comes up over a plain connection would leave it forwarding in the clear, and
	 * a tunnel taken down would leave its connections forwarding on whatever the far side
	 * still held. One pass after each change, a handful of lookups per connection and no
	 * command unless the answer has changed.
	 *
	 * What must still hold: a plain connection is covered by no policy; a tunnelled one is
	 * still a tunnel of the shape this driver carries, encrypting in the same direction, and
	 * ipsec.flows still allows it - and in the same tunnel: the association its encrypting
	 * direction names must hang from the head the policy names now. Which association of that
	 * head it names is not asked here, nor whether the kernel's present choice is on the
	 * coprocessor yet - a rekey changes both for a moment, and the association's own removal
	 * and the audit are what act on those.
	 */
	spgen = octep_ipsec_spgen();
	reval = (spgen != sc->ipsec_spgen_seen);

	for (int k = (int)sc->dp_conn_nlive - 1; k >= 0; k--) {
		i = (int)sc->dp_conn_live[k];
		mtx_lock(&sc->mtx);
		if (!sc->dp_conn[i].used) {
			mtx_unlock(&sc->mtx);
			continue;
		}
		t = sc->dp_conn[i].tuple[sc->dp_conn[i].probe_dir];
		tup[0] = sc->dp_conn[i].tuple[0];
		tup[1] = sc->dp_conn[i].tuple[1];
		cipsec = sc->dp_conn[i].ipsec;
		cenc = sc->dp_conn[i].enc_dir;
		pf_dir = sc->dp_conn[i].pf_dir;
		conn_rev = sc->dp_conn[i].conn_rev;
		dropped = sc->dp_conn[i].dropped;
		drop_alive = sc->dp_conn[i].drop_alive;
		mtx_unlock(&sc->mtx);

		gone = !octep_pf_state_exists(&t, pf_dir, NULL);
		/*
		 * A connection dropped by its verdict outlives its pf state while its frames keep coming:
		 * they die on the coprocessor, and taken out they would reach this host for pf to drop.
		 */
		if (gone && dropped && (int)(time_uptime - drop_alive) < OCTEP_DROP_HOLD) {
			sc->dp_drop_held++;
			continue;
		}
		wrong = 0;
		chk = 0;
		if (!gone && reval) {
			r = octep_ipsec_flow_resolve(sc, tup, &fi, 1, sc->ipsec_flows != 0);
			wrong = !((r == 0 && cipsec == 0) || (cipsec != 0 &&
			    sc->ipsec_flows != 0 && (r == 1 || r == -2) && fi.enc_dir == cenc));
			/*
			 * And a tunnelled connection must still be in the SAME tunnel: a narrower
			 * policy added for another peer, or another child between the same two
			 * gateways, leaves both the old tunnel and the connection standing, and the
			 * microflow would go on encrypting toward a peer the policy no longer
			 * names. Asked under the lock, of the association the microflow names.
			 */
			chk = (!wrong && cipsec != 0);
		}
		if (!gone && !wrong && !chk)
			continue;

		mtx_lock(&sc->mtx);
		if (sc->dp_conn[i].used && sc->dp_conn[i].conn_rev == conn_rev) {
			if (!gone && !wrong && octep_ipsec_flow_same_tunnel(sc, &fi,
			    sc->dp_conn[i].mf[cenc].sa)) {
				mtx_unlock(&sc->mtx);
				continue;
			}
			if (octep_conn_takeout(sc, &sc->dp_conn[i],
			    gone ? OCTEP_TAKEOUT_GONE : 1) == ETIMEDOUT) {
				mtx_unlock(&sc->mtx);
				octep_dp_handback_run(sc);
				return;		/* and the generation is not marked seen */
			}
			if (gone)
				sc->dp_auto_gone++;
			else
				sc->ipsec_flow_reval++;
		}
		mtx_unlock(&sc->mtx);
		octep_dp_handback_run(sc);
	}
	sc->ipsec_spgen_seen = spgen;
}

/*
 * Accelerate the connection a punted frame belongs to: everything that was proven separately, in
 * one call.
 *
 * The parts have all been measured on their own and every one of them cost something to find:
 *
 *   the slot      from the punted frame's own metadata, captured with its tuple in one read
 *                 because reading them from two sysctls is a race under load
 *   the verdict   from pf, reached by walking the linker's files because a weak symbol without a
 *                 dependency can only ever be zero
 *   the NAT       from pf's two keys, in the connection's orientation and not the frame's, taken
 *                 from the state that carries the translation and not the first one found
 *   the next hop  from the host's routing table and its ARP, because our own table would be wrong
 *                 in the way that is hardest to notice
 *   the state     2, MF_ACTIVE, which is the field that decides whether any of it is used
 *
 * And now the shape the far side expects: ONE connection with TWO microflows. The frame in hand
 * gives one direction. The other is looked up in the candidate table, where every punted frame of
 * the last second left its identity and port; if it is there, both go out in one FLOW_CREATE_FP.
 * If not, one goes out, and the other direction's first punted frame attaches to the same
 * connection by MFLOW_PROGRAM - which is also how a direction the fast path has re-created, with a
 * new revision, is re-attached. A connection offloaded in one direction only is handed back by the
 * fast path within a few frames, measured, so the second half is not optional; it is just allowed
 * to arrive late.
 *
 * The order matters: the next hops must exist before a microflow points at them, and the
 * connection before the microflow names it, so the requests go out in that order and the first
 * failure stops the rest. The far side answers each with an rc, which is checked - a connection
 * the far side refused is freed here rather than left in the table pretending to forward.
 */
/*
 * Whether a MAC is one of this host's own: an interface's address - a bridge's among them - or a
 * CARP or VRRP group's (00:00:5e:00:01 and :02), which the host answers for as well. A frame
 * addressed to one of them is for the host, which routes it; octep_dp_frame_bridged's question.
 */
static int
octep_mac_cb(if_t ifp, void *arg)
{
	const uint8_t *lla;

	if (if_getaddrlen(ifp) != ETHER_ADDR_LEN || (lla = (const uint8_t *)if_getlladdr(ifp)) == NULL)
		return (0);
	return (memcmp(lla, arg, ETHER_ADDR_LEN) == 0);
}

static int
octep_mac_is_host(const uint8_t *m)
{
	struct epoch_tracker et;
	uint8_t mac[ETHER_ADDR_LEN];
	int hit;

	if (m[0] == 0x00 && m[1] == 0x00 && m[2] == 0x5e && m[3] == 0x00 &&
	    (m[4] == 0x01 || m[4] == 0x02))
		return (1);
	memcpy(mac, m, sizeof(mac));
	CURVNET_SET_QUIET(vnet0);
	NET_EPOCH_ENTER(et);
	hit = if_foreach(octep_mac_cb, mac);
	NET_EPOCH_EXIT(et);
	CURVNET_RESTORE();
	return (hit != 0);
}

/*
 * A frame this appliance BRIDGES rather than routes (discovery 9, issue 723): it came in on a
 * member of a bridge and is addressed to another machine, not to the host. 1 for such a frame; -1
 * for one on a member addressed to a group - a broadcast or a multicast, which the bridge sends to
 * every port and no one microflow can carry; 0 for every other frame, which the host routes. mac is
 * the frame's two MACs as the candidate keeps them, or NULL - or all zero - when it has none.
 */
static int
octep_dp_frame_bridged(struct octep_softc *sc, int in_dif, const uint8_t *mac)
{
	int i, any = 0;

	if (mac == NULL || in_dif < 0 || in_dif >= OCTEP_DP_IF_MAX || sc->dp_if[in_dif].ifp == NULL ||
	    ((struct ifnet *)sc->dp_if[in_dif].ifp)->if_bridge == NULL)
		return (0);
	/* A frame that came out of a tunnel has no MACs in its candidate: all zero, never bridged. */
	for (i = 0; i < 12; i++)
		any |= mac[i];
	if (any == 0)
		return (0);
	if ((mac[0] & 1) != 0)
		return (-1);
	return (!octep_mac_is_host(mac));
}

/*
 * Whether two front ports are members of one bridge. A port's if_bridge is not its bridge: it is the
 * port's own entry in its bridge, if_bridge.c's struct bridge_iflist, one for each member - so two
 * members of one bridge never compare equal by it, and every bridged connection was refused as one
 * whose two directions were on two bridges (measured 2026-10-24). The bridge is that entry's bif_sc.
 * The struct is if_bridge.c's own and in no header, so its head is restated here and checked at every
 * use: its bif_ifp must be the port itself, or the layout is not the one this was written for and the
 * answer is no - the connection then stays with the host's bridge, which is never wrong. Read in the
 * net epoch, which is what if_bridge.c frees a member's entry after.
 */
struct octep_bridge_member {	/* the head of if_bridge.c's struct bridge_iflist */
	void		*bif_next[2];	/* CK_LIST_ENTRY */
	struct ifnet	*bif_ifp;	/* the member */
	void		*bif_sc;	/* its bridge */
};

static void *
octep_port_bridge(struct ifnet *ifp)
{
	const struct octep_bridge_member *b = ifp->if_bridge;

	return ((b != NULL && b->bif_ifp == ifp) ? b->bif_sc : NULL);
}

static int
octep_ports_one_bridge(struct octep_softc *sc, int a, int b)
{
	struct epoch_tracker et;
	void *x, *y;

	if (a < 0 || b < 0 || a >= OCTEP_DP_IF_MAX || b >= OCTEP_DP_IF_MAX ||
	    sc->dp_if[a].ifp == NULL || sc->dp_if[b].ifp == NULL)
		return (0);
	NET_EPOCH_ENTER(et);
	x = octep_port_bridge((struct ifnet *)sc->dp_if[a].ifp);
	y = octep_port_bridge((struct ifnet *)sc->dp_if[b].ifp);
	NET_EPOCH_EXIT(et);
	return (x != NULL && x == y);
}

/*
 * A bridged connection's next hop: out of the port the other direction's frames come in on, which is
 * where the bridge has learned the far end is, and named with the frame's own two MACs - which the
 * microflow's bridge control then leaves as they are, as it leaves the TTL (OCTEP_BRCTL_BRIDGED). The
 * route plays no part. For a bridge with no address it is the default route, and a bridged
 * connection made from it was sent to the WAN's gateway (issue 723).
 */
static int
octep_nhop_bridged(struct octep_softc *sc, int out_dif, const uint8_t *mac, struct octep_nhop *out)
{
	struct octep_dp_if *dif;

	bzero(out, sizeof(*out));
	if (mac == NULL || out_dif < 0 || out_dif >= OCTEP_DP_IF_MAX)
		return (ENETUNREACH);
	dif = &sc->dp_if[out_dif];
	if (dif->ifp == NULL || dif->lif_iface == OCTEP_DP_IF_PORT_AUTO || dif->link != 1)
		return (ENETUNREACH);
	memcpy(out->dmac, mac, ETHER_ADDR_LEN);
	memcpy(out->smac, mac + ETHER_ADDR_LEN, ETHER_ADDR_LEN);
	out->iface = dif->lif_iface;
	out->mtu = (uint16_t)if_getmtu(dif->ifp);
	out->ifname_unit = out_dif;
	out->from_flow = 1;
	return (0);
}

static int
octep_dp_flow_make(struct octep_softc *sc, const struct octep_pf_tuple *tin, uint32_t slot,
    uint32_t rev, int in_dif, uint16_t tag, uint16_t fsa, uint16_t fsa_rev, const uint8_t *mac,
    uint16_t vlan, struct sbuf *sb, int *stop)
{
	struct octep_pf_tuple t = *tin, tup[2];
	struct octep_pf_state st;
	struct octep_pf_tcp pt;
	struct octep_ipsec_flow fi;
	struct octep_nhop nh[2];
	struct octep_conn *c;
	uint32_t rslot, rrev, oslot, orev, mask, idx, nha[2];
	uint16_t rtag, otag, rsa, rsa_rev, osa, osa_rev;
	uint8_t rclosing, tcpblk[OCTEP_CONN_TCP_LEN];
	int err, d, fd, rd, rin_dif, oin_dif, have_rev, attach, hint, marked;
	int ipsec, ipsec_half, dec, tunmiss, kept, perr, tcp_on, tcp_able, tos_marked;
	int bridged, fb;
	uint16_t dscp[2], rvlan = 0;
	uint8_t rmac[12];

	if (!octep_pf_present()) {
		sbuf_cat(sb, "pf is not loaded, so there is no verdict to act on\n");
		return (ENXIO);
	}
	if (slot == 0) {
		sbuf_cat(sb, "the frame named no flow: the offload gate is off, and with it off the "
		    "metadata carries flow id 0\n");
		return (ENOENT);
	}
	if (sc->rpc_plat_num_mflows != 0 && slot >= sc->rpc_plat_num_mflows) {
		sbuf_printf(sb, "the frame names microflow %u and the board has %u\n", slot,
		    sc->rpc_plat_num_mflows);
		return (ERANGE);
	}
	/* The owner's own choice first, and cheaply: the frame's tuple, before pf is asked anything. */
	if (octep_flow_kept(sc, &t, 1)) {
		sc->dp_flow_excluded++;
		sbuf_cat(sb, "not accelerated: the owner keeps this connection with the host\n");
		return (EPERM);
	}
	if (!octep_pf_state_read(&t, &st)) {
		sc->dp_flow_nostate++;
		sbuf_cat(sb, "pf has no state for that frame's tuple\n");
		return (ENOENT);
	}
	if ((sc->dp_accel_dir == 1 && !st.original) || (sc->dp_accel_dir == 2 && st.original)) {
		sbuf_printf(sb, "not accelerated: dp.accel_dir admits only the %s direction and this "
		    "frame is the %s\n", sc->dp_accel_dir == 1 ? "original" : "reply",
		    st.original ? "original" : "reply");
		return (EPERM);
	}

	/*
	 * Which direction this frame is, and what both directions' frames look like as they
	 * arrive. The derivation is checked against the frame itself: a disagreement is counted,
	 * and the frame's own tuple is what the table keeps for its direction.
	 */
	d = st.original ? OCTEP_CONN_DIR_ORIGINAL : OCTEP_CONN_DIR_REPLY;
	octep_conn_tuples(&st, &t, tup);
	if (!octep_tuple_eq(&tup[d], &t)) {
		/*
		 * Said once per second at most, with both tuples, because the counter alone names
		 * nothing: the first soak showed one of these in two hours and nothing to read.
		 */
		if (ppsratecheck(&sc->dp_mismatch_last, &sc->dp_mismatch_curpps, 1))
			device_printf(sc->dev, "dp: tuple mismatch on the %s direction: frame "
			    "0x%08x:%u -> 0x%08x:%u, pf implied 0x%08x:%u -> 0x%08x:%u (%s%s%s, pf "
			    "list %d, order %d)\n", d == 0 ? "original" : "reply",
			    t.sip, ntohs(t.sport), t.dip, ntohs(t.dport),
			    tup[d].sip, ntohs(tup[d].sport), tup[d].dip, ntohs(tup[d].dport),
			    st.nat_snat ? "snat" : "", st.nat_snat && st.nat_dnat ? "+" : "",
			    st.nat_dnat ? "dnat" : "", st.lookup_dir, st.order);
		sc->dp_flow_tuple_mismatch++;
		tup[d] = t;
	}
	/* And both directions, now that pf has said what they are. */
	if (octep_flow_kept(sc, tup, 2)) {
		sc->dp_flow_excluded++;
		sbuf_cat(sb, "not accelerated: the owner keeps this connection with the host\n");
		return (EPERM);
	}

	/*
	 * A connection the kernel's IPsec policy covers. The fast path forwards what it is given,
	 * and the policy is applied in ip_forward - after the point a punted frame is taken from -
	 * so such a connection programmed as a plain one leaves in the clear, in hardware (measured
	 * on the lab tunnel, issue 290). It is either programmed with its association or left to
	 * the host.
	 *
	 * Programmed, it is one connection with two microflows like any other, and the two are not
	 * alike - see struct octep_conn_mf. Which direction is which comes from the policy, never
	 * from who opened the connection. ipsec.flows says how much of it the operator has asked
	 * for: nothing, the direction that leaves encrypted, or both - and the last is a separate
	 * choice because of what the far side does not check, which that sysctl's own description
	 * says in full.
	 *
	 * No translation inside a tunnel in this version. The connection block would carry it, and
	 * what the far side does with a translated packet it is about to encrypt, or has just
	 * decrypted, has not been measured.
	 */
	tunmiss = 0;
	/*
	 * The association is only looked for when the operator has asked for tunnel connections in
	 * hardware: finding it is the kernel's own call, which asks the key daemon for an
	 * association when there is none, and a host that is leaving tunnels alone should not be
	 * the one to ask. ipsec.flows is read here without the lock and again, under it, at every
	 * place a connection is written - a value lowered in between is honoured there.
	 */
	ipsec = octep_ipsec_flow_resolve(sc, tup, &fi, 0, sc->ipsec_flows != 0);
	if (ipsec > 0 && (st.nat_snat || st.nat_dnat)) {
		sc->ipsec_flow_shape++;
		ipsec = -1;
	}
	/*
	 * IPv6 goes into hardware plain and untranslated, or not at all: the fast path's NAT block
	 * carries IPv4 addresses only, and an IPv6 connection a policy covers is never made plain -
	 * octep_ipsec_flow_resolve answers -1 for one, so it stays with the host and its policy.
	 */
	if (st.af == AF_INET6 && (st.nat_valid || ipsec != 0)) {
		sc->dp_flow_v6_refused++;
		sbuf_cat(sb, st.nat_valid ? "not accelerated: an IPv6 connection with a translation\n" :
		    "not accelerated: an IPv6 connection an IPsec policy covers stays with the host\n");
		return (EACCES);
	}
	/*
	 * pf's DSCP mark (discovery 8): the value a set-tos rule - OPNsense's normalization "DSCP" -
	 * has pf write into every frame of the connection. The fast path writes it in pf's place when
	 * the microflow carries it (octep_pf_tos_scan says which value each direction takes); before
	 * this, an offloaded connection's frames left as they came. Measured for a plain IPv4
	 * connection. An IPv6 one with a mark stays with the host: the fast path leaves an IPv6
	 * frame's traffic class as it came whatever the override says - measured 2026-10-24 with
	 * dp.mflow_dscp, IPv4 written and IPv6 not on one path and one build
	 * (docs/measurements/flow-dscp6-2026-10-24.md). A tunnelled one stays too, untried, and so
	 * does one whose mark sets the ECN bits as well, which the fast path's six bits cannot carry.
	 */
	dscp[0] = dscp[1] = 0;
	tos_marked = sc->dp_dscp_pf != 0 ? octep_pf_tos_scan(tup, dscp) : 0;
	if (tos_marked < 0 || (tos_marked > 0 && (st.af == AF_INET6 || ipsec > 0))) {
		sc->dp_flow_dscp_kept++;
		sbuf_cat(sb, tos_marked < 0 ? "not accelerated: pf's set-tos sets the ECN bits as "
		    "well, and the fast path writes the DSCP alone\n" : st.af == AF_INET6 ?
		    "not accelerated: pf marks this connection's DSCP, and the fast path does not write "
		    "an IPv6 traffic class\n" : "not accelerated: pf marks this connection's DSCP, and "
		    "the fast path's writing of it is untried for a tunnelled connection\n");
		return (EACCES);
	}
	if (ipsec == 0 && fsa != 0) {
		/* Decrypted on the way in, and no policy of this host asks for that: never a flow. */
		sc->ipsec_flow_shape++;
		sbuf_cat(sb, "not accelerated: decrypted on the way in, and no policy covers it\n");
		return (EACCES);
	}

	/*
	 * Is this connection already in the table? Before anything is said about the frame, the
	 * entry itself is held to what the policy says NOW: a connection in hardware that the
	 * policy has since covered, or uncovered, or that ipsec.flows no longer allows, is wrong
	 * whatever this frame is, so it goes, and the next frame makes it again the way things
	 * stand. The sweep does the same for every connection when the policy's generation moves;
	 * this is the same test for the one connection a frame has just named, and it comes before
	 * the "already programmed" answer below, which would otherwise hide it.
	 */
	mtx_lock(&sc->mtx);
	c = octep_conn_find(sc, &t, &fd);
	if (c != NULL && c->ipsec != 0 && ipsec == -2 && sc->ipsec_flows != 0 &&
	    fi.enc_dir == c->enc_dir &&
	    octep_ipsec_flow_same_tunnel(sc, &fi, c->mf[c->enc_dir].sa)) {
		/*
		 * The same tunnel, and the kernel's choice of association in it is one this
		 * driver has not finished installing: a rekey, for the moment between the kernel
		 * preferring the new association and the coprocessor having it. Not a change of
		 * policy. The connection stays as it is, on the association it names, and that
		 * association's removal moves it; this frame attaches nothing.
		 */
		mtx_unlock(&sc->mtx);
		sbuf_cat(sb, "waiting: the kernel's new association for this tunnel is not on the "
		    "coprocessor yet\n");
		return (EAGAIN);
	}
	if (c != NULL && (ipsec < 0 || (c->ipsec != 0) != (ipsec > 0) ||
	    (ipsec > 0 && (sc->ipsec_flows == 0 || fi.enc_dir != c->enc_dir ||
	    !octep_ipsec_flow_same_tunnel(sc, &fi, c->mf[c->enc_dir].sa))))) {
		idx = c->idx;
		err = octep_conn_takeout(sc, c, 1);
		mtx_unlock(&sc->mtx);
		octep_dp_handback_run(sc);
		sc->ipsec_flow_reval++;
		if (err == ETIMEDOUT && stop != NULL)
			*stop = 1;
		sbuf_printf(sb, "connection %u %s: the IPsec policy over it is not what it was "
		    "made under\n", idx, err == ETIMEDOUT ? "could not be taken out, the far side "
		    "is not answering" : "taken out");
		return (err == ETIMEDOUT ? err : EAGAIN);
	}
	if (ipsec < 0 || (ipsec > 0 && sc->ipsec_flows == 0)) {
		mtx_unlock(&sc->mtx);
		sc->ipsec_flow_policy++;
		sbuf_cat(sb, "not accelerated: the kernel's IPsec policy covers this connection, and "
		    "it stays with the host\n");
		return (EACCES);
	}
	/*
	 * If this very identity is programmed, the frame is one the fast path handed back and the
	 * drain counts those; otherwise the direction is attached - or re-attached under a new
	 * identity - to the connection that exists.
	 */
	if (c != NULL && c->mf[fd].state == OCTEP_MF_PROGRAMMED && c->mf[fd].slot == slot &&
	    c->mf[fd].rev == rev) {
		mtx_unlock(&sc->mtx);
		sbuf_printf(sb, "connection %u already forwards this direction at slot %u\n",
		    c->idx, slot);
		return (EEXIST);
	}
	attach = (c != NULL);
	bridged = 0;
	if (attach) {
		d = fd;
		hint = c->mf[1 - d].in_dif;
		bridged = c->bridged;
	} else
		hint = -1;
	mtx_unlock(&sc->mtx);

	/*
	 * A NEW TCP connection is not taken from pf before pf has seen its handshake through. Once
	 * it is in hardware pf sees nothing more of it, and a state left at SYN and SYN-ACK is on a
	 * thirty-second timer: it runs out, the sweep takes the connection out as if it had ended,
	 * and the next segment meets rules that pass a TCP packet with no state only if it is a
	 * SYN. Both directions have been punted the moment the SYN-ACK arrives, which is one packet
	 * too early; whether the poll fell in that gap used to be luck. Not a refusal - the next
	 * frame of the connection is asked again - and not for the hand instrument, which passes no
	 * stop and is given what it asks for.
	 *
	 * UDP waits the same way for both of its peers to read MULTIPLE - one more datagram from
	 * the opener - so that its states are on the sixty-second timer and not the thirty-second
	 * one when pf stops seeing them; which also keeps a two-datagram exchange out of the table
	 * altogether. It does not wait for ever: a flow whose opener has said nothing more by the
	 * time its state is OCTEP_PF_UDP_ONEWAY_MS old is a stream in one direction, and is made
	 * on the timer pf itself keeps such a flow on. Except a tunnelled connection of which only
	 * the encrypting direction is going to hardware: that has no reply to settle on at all,
	 * and is made as before.
	 */
	if (!attach && stop != NULL &&
	    !(t.proto == IPPROTO_UDP && ipsec > 0 && sc->ipsec_flows == 1 && d == fi.enc_dir) &&
	    !(octep_pf_settled(&tup[0]) && octep_pf_settled(&tup[1]))) {
		sc->dp_flow_unsettled++;
		sbuf_cat(sb, "not yet: pf's states for this connection are not all on their long "
		    "timer - the handshake is not through, or it is closing, or one side of a UDP "
		    "flow has been seen only once and the state is still young\n");
		return (EAGAIN);
	}

	/*
	 * A TCP connection's window, which the fast path checks from here on in pf's place (issue
	 * #679). Before this, a connection went to the fast path with its TCP block at zero and the
	 * check off, while pf was told to stop checking it: the window was checked by nobody.
	 *
	 * pf's numbers are good for it when pf has been following the connection: none of its states
	 * sloppy. A state a rule made sloppy keeps the check off in hardware too - that is the
	 * owner's choice for the connection. One this driver marked sloppy when the connection was in
	 * hardware before, and that came back without the fast path's numbers - left behind by a
	 * revision bump, or given back by the fast path rather than taken - has numbers that stopped
	 * there, and is made as before, unchecked, and counted; with dp.tcp_revive 2, the instrument
	 * octep_conn_probe describes, its window is made instead from the latest segment the host saw
	 * in each direction (octep_tcp_from_segs). And a connection pf rewrites in every packet is not
	 * made at all.
	 *
	 * A tunnel's connection is sent unchecked, as the XGS 136's is: with ipsec.flows 1 only its
	 * encrypting direction is in hardware, so the fast path never sees the other direction's
	 * segments, and a take-back would give pf that direction's numbers as they were when the
	 * connection was made.
	 */
	tcp_on = tcp_able = 0;
	if (!attach && t.proto == IPPROTO_TCP) {
		uint8_t scale[2];

		(void)octep_pf_tcp_scan(tup, &pt);
		if (pt.rewrites || st.rewrites) {
			sc->dp_tcp_rewrites++;
			sbuf_cat(sb, "not accelerated: pf modulates this connection's sequence numbers "
			    "or timestamps (modulate state, synproxy state or scrub reassemble tcp), "
			    "and the fast path would forward them unchanged\n");
			return (EACCES);
		}
		if (sc->dp_tcp_window != 0 && pt.states != 0 && !pt.rule_sloppy && ipsec <= 0) {
			tcp_able = 1;
			if (!pt.sloppy && st.seqlo[0] != 0 && st.seqlo[1] != 0) {
				octep_tcp_from_pf(tcpblk, &st);
				tcp_on = 1;
			} else if (sc->dp_tcp_revive == 2) {
				char why[96];

				octep_tcp_scales(&st, scale);
				if (octep_tcp_segs(sc, tup, scale, tcpblk, why, sizeof(why), 0,
				    0)) {
					tcp_on = 1;
					sc->dp_tcp_from_segs++;
					snprintf(sc->dp_tcp_segs_last, sizeof(sc->dp_tcp_segs_last),
					    "made with a window from the host's latest segments");
				} else
					snprintf(sc->dp_tcp_segs_last, sizeof(sc->dp_tcp_segs_last),
					    "made unchecked, pf's numbers having stopped - %s", why);
			}
		}
	}

	/*
	 * And the frame in hand must be what its direction's frames are: in the clear where the
	 * policy encrypts on the way out; decrypted, by an association of the same tunnel, where it
	 * decrypts on the way in. A frame in the clear in the decrypted direction is exactly what
	 * the kernel's inbound policy check refuses, and it must never make or join a flow.
	 */
	if (ipsec > 0) {
		if (d == fi.enc_dir) {
			if (fsa != 0) {
				sc->ipsec_flow_shape++;
				sc->ipsec_flow_policy++;
				sbuf_cat(sb, "not accelerated: decrypted on the way in and encrypted "
				    "on the way out - tunnel to tunnel stays with the host\n");
				return (EACCES);
			}
			/*
			 * And it did not arrive on the tunnel's own port. A frame the policy wants
			 * encrypted comes from the protected side; one that carries those addresses
			 * and arrives where the ESP does is somebody on the outside saying they are
			 * inside - and the port a direction's frames arrive on is where the OTHER
			 * direction's frames are sent when the route names a bridge. Accepted, it
			 * would point the decrypted traffic back out of the tunnel's port.
			 */
			if (in_dif == fi.out_dif) {
				sc->ipsec_flow_clear++;
				sc->ipsec_flow_policy++;
				sbuf_cat(sb, "not accelerated: a frame of the direction the policy "
				    "encrypts arrived on the tunnel's own port\n");
				return (EACCES);
			}
		} else if (fsa == 0) {
			sc->ipsec_flow_clear++;
			sc->ipsec_flow_policy++;
			sbuf_cat(sb, "not accelerated: this frame arrived in the clear in the "
			    "direction the policy wants decrypted\n");
			return (EACCES);
		} else if (sc->ipsec_flows < 2) {
			sbuf_cat(sb, "not accelerated: ipsec.flows leaves the decrypted direction "
			    "with the host\n");
			return (EACCES);
		} else if (!octep_ipsec_flow_dec_ok(sc, &fi, fsa, fsa_rev)) {
			sc->ipsec_flow_shape++;
			sc->ipsec_flow_policy++;
			sbuf_cat(sb, "not accelerated: the association that decrypted this frame is "
			    "not this tunnel's\n");
			return (EACCES);
		}
	}

	/*
	 * The other direction, from the candidate table, when this is a new connection. Its port is
	 * also the best hint for where this direction's frames should leave when the route names
	 * the bridge: that port is where the other end's frames come in.
	 *
	 * For a tunnelled connection the other direction's candidate is held to the same rule as
	 * the frame in hand: in the clear where it should be, decrypted by this tunnel where it
	 * should be. A candidate that fails it is no candidate.
	 */
	have_rev = 0;
	rd = 1 - d;
	rsa = rsa_rev = 0;
	if (!attach) {
		rclosing = 0;
		have_rev = octep_flow_cand_ident(sc, &tup[rd], sc->dp_cand_age, &rslot, &rrev,
		    &rin_dif, &rtag, &rsa, &rsa_rev, &rclosing, rmac, &rvlan);
		/*
		 * The other direction has carried a FIN or RST that the reader has not taken yet.
		 * pf may not have seen that frame through - a decrypted one is a queue away - so
		 * its states can still read established, and a connection made now is given back
		 * at the next frame and taken out a moment later: seven commands for nothing. Not
		 * for the hand instrument.
		 */
		if (have_rev && rclosing != 0 && stop != NULL) {
			sc->dp_flow_other_closing++;
			sbuf_cat(sb, "not made: the other direction has carried a FIN or RST that has "
			    "not been read yet - the connection is ending\n");
			return (EAGAIN);
		}
		if (have_rev && sc->rpc_plat_num_mflows != 0 && rslot >= sc->rpc_plat_num_mflows)
			have_rev = 0;
		if (have_rev && ipsec > 0) {
			if (rd == fi.enc_dir)
				have_rev = (rsa == 0 && rin_dif != fi.out_dif);
			else
				have_rev = (sc->ipsec_flows >= 2 && rsa != 0 &&
				    octep_ipsec_flow_dec_ok(sc, &fi, rsa, rsa_rev));
		} else if (have_rev && rsa != 0)
			have_rev = 0;
		if (have_rev)
			hint = rin_dif;
	}

	/*
	 * A connection this appliance BRIDGES (discovery 9, issue 723): its frames are addressed to the
	 * other machine and not to the host, and both directions arrive on members of one bridge. The
	 * bridge forwards it, not the routing table. Until this it was made as a routed connection, its
	 * next hop from the route - for a bridge with no address the default route, so its frames went
	 * to the WAN's gateway; for one with an address the right port, but every frame with the
	 * bridge's MAC and its TTL lowered. Made bridged, each direction leaves by the port the other
	 * direction comes in on, with nothing rewritten. A bridged connection that cannot be made so
	 * stays with the host, whose bridge carries it: one whose other direction has not been seen -
	 * nothing else says which port the far end is on - one to a group address, one pf translates or
	 * an IPsec policy covers, one whose two directions are not on two ports of one bridge, and every
	 * one while dp.bridge_flows is 0.
	 */
	if (!attach && (fb = octep_dp_frame_bridged(sc, in_dif, mac)) != 0) {
		const char *why = NULL;

		if (sc->dp_bridged == 0)
			why = "dp.bridge_flows is 0";
		else if (fb < 0)
			why = "it is addressed to a group, which the bridge sends to every port";
		else if (st.nat_valid || ipsec != 0)
			why = "pf translates it or an IPsec policy covers it";
		else if (have_rev && rin_dif == in_dif)
			why = "both its directions arrive on one port";
		else if (have_rev && octep_dp_frame_bridged(sc, rin_dif, rmac) != 1)
			why = "its other direction is addressed to the host or to a group, or arrives "
			    "on a port in no bridge";
		else if (have_rev && !octep_ports_one_bridge(sc, in_dif, rin_dif))
			why = "its two directions arrive on ports of two bridges";
		if (why != NULL) {
			sc->dp_flow_bridged_kept++;
			sbuf_printf(sb, "not accelerated: a bridged connection, and %s - the bridge "
			    "keeps it\n", why);
			return (EACCES);
		}
		if (!have_rev) {
			sbuf_cat(sb, "not yet: a bridged connection whose other direction has not been "
			    "punted - nothing else says which port the far end is on\n");
			return (EAGAIN);
		}
		bridged = 1;
	}

	/*
	 * A tagged frame (discovery 10). One addressed to this host is a VLAN subinterface's, routed,
	 * and is made below like any other, its next hops naming the VLANs they leave by. One that is
	 * not, and that no bridge of a port carries, is one a bridge over a VLAN subinterface carries -
	 * not made here - and every one while dp.vlan_flows is 0: both stay with the host.
	 */
	if (!attach && !bridged && vlan != 0 && (sc->dp_vlan_flows == 0 ||
	    (mac != NULL && (mac[0] & 1) == 0 && !octep_mac_is_host(mac)))) {
		sbuf_cat(sb, sc->dp_vlan_flows == 0 ? "not accelerated: a VLAN's connection, and "
		    "dp.vlan_flows is 0\n" : "not accelerated: a tagged frame not addressed to this "
		    "host - a bridge over a VLAN carries it, and the host's bridge keeps it\n");
		return (EACCES);
	}

	/*
	 * Where each direction's frames go after translation: the other end of the connection, as
	 * that end's own side of the firewall knows it. Resolved outside the lock, from the host's
	 * own route and ARP.
	 *
	 * Except the direction that leaves encrypted, whose frames go to the TUNNEL's far end: the
	 * coprocessor builds the outer header from the association and the link header from the
	 * microflow's next hop, so that next hop is the way to the association's outer destination,
	 * by the interface the association is on, with that interface's own MTU - the far side
	 * takes the tunnel's overhead off it by itself.
	 */
	nha[0] = st.orig_dst;
	nha[1] = st.orig_src;
	if (ipsec > 0)
		nha[fi.enc_dir] = fi.out_dst;
	/* An IPv6 connection's next hops: the other end in full, by the host's IPv6 route and ND. */
	if (bridged)
		err = octep_nhop_bridged(sc, hint, mac, &nh[d]);
	else if (st.af == AF_INET6)
		err = octep_nhop_resolve6(sc, (const uint8_t *)(d == 0 ? st.orig_dst6 : st.orig_src6),
		    hint, &nh[d]);
	else
		err = octep_nhop_resolve(sc, nha[d], (ipsec > 0 && d == fi.enc_dir) ? -1 : hint,
		    &nh[d]);
	/*
	 * And each must leave by the right side of the tunnel: the encrypted direction by the
	 * interface its association is on, the decrypted one by any interface but that.
	 */
	if (err == 0 && ipsec > 0 &&
	    (d == fi.enc_dir) != (nh[d].ifname_unit == fi.out_dif)) {
		tunmiss = 1;
		err = EHOSTUNREACH;
	}
	if (err != 0) {
		sc->dp_flow_nonhop++;
		sbuf_printf(sb, "no next hop for 0x%08x: %s\n",
		    nha[d],
		    bridged ? "a bridged connection, and the port the other direction arrives on "
		    "has no link, or the frame carried no MACs" :
		    err == EWOULDBLOCK ? "the neighbour is not resolved yet, and asking for it has "
		    "just been done - try again in a moment" :
		    err == ENETUNREACH ? (hint < 0 ? "the route leaves by an interface this driver "
		    "does not own, and the other direction of this connection has not been punted "
		    "from a front port, so there is nothing to say which port the destination is on" :
		    "the route leaves by an interface this driver does not own, and the port the "
		    "other direction arrives on has no link") : tunmiss ? "a tunnelled connection's "
		    "encrypted direction must leave by the interface its association is on, and its "
		    "decrypted direction by another" : "no route");
		return (err);
	}
	if (have_rev) {
		if (bridged)
			err = octep_nhop_bridged(sc, in_dif, rmac, &nh[rd]);
		else if (st.af == AF_INET6)
			err = octep_nhop_resolve6(sc,
			    (const uint8_t *)(rd == 0 ? st.orig_dst6 : st.orig_src6), in_dif, &nh[rd]);
		else
			err = octep_nhop_resolve(sc, nha[rd],
			    (ipsec > 0 && rd == fi.enc_dir) ? -1 : in_dif, &nh[rd]);
		if (err == 0 && ipsec > 0 &&
		    (rd == fi.enc_dir) != (nh[rd].ifname_unit == fi.out_dif))
			err = EHOSTUNREACH;
		if (err != 0) {
			sbuf_printf(sb, "  the other direction's next hop did not resolve (%d)\n", err);
			have_rev = 0;
		}
	}
	/*
	 * A tunnelled connection that arrives tagged or leaves by a VLAN stays with the host: a VLAN on
	 * either side of a tunnel is untried in the fast path (discovery 10).
	 */
	if (ipsec != 0 && (vlan != 0 || rvlan != 0 || nh[d].vlan != 0 ||
	    (have_rev && nh[rd].vlan != 0))) {
		sbuf_cat(sb, "not accelerated: a tunnelled connection by a VLAN - untried in the fast "
		    "path, and the host keeps it\n");
		return (EACCES);
	}
	/*
	 * BOTH DIRECTIONS OR NEITHER. A connection offloaded in one direction is handed back by the
	 * fast path within a few frames - milliseconds on a busy connection - which is long before
	 * the next poll could attach the other direction, and an entry the fast path has put in
	 * RECLAIM_PENDING is not revived by attaching to it. Measured with the automatic trigger
	 * programming direction by direction: 33 connections, 250 frames forwarded, 367,000 handed
	 * back. Programmed together, the same download ran entirely in hardware with none handed
	 * back. So a new connection waits until both of its directions have been punted; on a
	 * busy connection that is the next poll. dp.accel_dir and dp.accel_half are the instruments
	 * that ask for one direction on purpose, and they get it.
	 */
	/*
	 * A tunnelled connection is held to it absolutely - the two instruments do not apply - with
	 * one exception that is the operator's and not an instrument's: ipsec.flows 1 asks for the
	 * encrypting direction alone, and the frame in hand is that direction.
	 */
	ipsec_half = (ipsec > 0 && sc->ipsec_flows == 1 && d == fi.enc_dir);
	if (ipsec_half && !attach && octep_ipsec_backoff_hit(sc, &tup[fi.enc_dir])) {
		sc->ipsec_flow_backoff++;
		sbuf_cat(sb, "not accelerated: the fast path gave this connection back with only "
		    "its encrypting direction in hardware, and it is left with the host for a "
		    "minute\n");
		return (EAGAIN);
	}
	if (!attach && !have_rev && !ipsec_half &&
	    (ipsec > 0 || (sc->dp_accel_dir == 0 && sc->dp_accel_half == 0))) {
		if (ipsec > 0)
			sc->ipsec_flow_wait++;
		else
			sc->dp_flow_wait_other++;
		sbuf_printf(sb, "waiting: the %s direction has not been punted yet, and a connection "
		    "offloaded in one direction is handed back within a few frames\n",
		    rd == 0 ? "original" : "reply");
		return (EAGAIN);
	}

	mtx_lock(&sc->mtx);
	if (attach) {
		/*
		 * The entry may have gone, or this direction may have been attached meanwhile by
		 * the drain. Look again under the lock rather than trusting the earlier look.
		 */
		c = octep_conn_find(sc, &t, &fd);
		if (c == NULL || fd != d) {
			mtx_unlock(&sc->mtx);
			sbuf_cat(sb, "the table changed under this frame - try again\n");
			return (EAGAIN);
		}
		if (c->mf[d].state == OCTEP_MF_PROGRAMMED && c->mf[d].slot == slot &&
		    c->mf[d].rev == rev) {
			mtx_unlock(&sc->mtx);
			return (EEXIST);
		}
		if ((c->ipsec != 0) != (ipsec > 0)) {
			/* Not the connection the first look found: another took its place. */
			mtx_unlock(&sc->mtx);
			sbuf_cat(sb, "the table changed under this frame - try again\n");
			return (EAGAIN);
		}
		if (c->doomed) {
			/* Waiting for the audit to take it out: nothing is added to it. */
			mtx_unlock(&sc->mtx);
			sbuf_cat(sb, "this connection is being taken out - try again\n");
			return (EAGAIN);
		}
		if (c->ipsec != 0 && c->mf[d].state == OCTEP_MF_PROGRAMMED) {
			/*
			 * A direction that is programmed, arriving under another identity: the fast
			 * path dropped the microflow this table names and made a new one. That is
			 * what an idle connection looks like after the board's timeout - and what a
			 * connection the fast path has given back looks like after five seconds of
			 * it, and attaching to THAT revives nothing. So the far side is asked first.
			 * Given back, the connection is reclaimed here, and a later candidate - the
			 * other direction's, in this same poll if it is still to come - makes it
			 * again whole, unless it is one being left with the host. Not answered,
			 * nothing more is asked in this poll.
			 */
			perr = octep_conn_probe(sc, c);
			if (perr != 0) {
				mtx_unlock(&sc->mtx);
				if (perr == ETIMEDOUT) {
					if (stop != NULL)
						*stop = 1;
					sbuf_cat(sb, "the far side is not answering\n");
					return (ETIMEDOUT);
				}
				if (perr == EBUSY) {
					sbuf_cat(sb, "the fast path has given this connection back, and it "
					    "is revived once the host has seen a segment each way\n");
					return (EAGAIN);
				}
				sbuf_cat(sb, "the fast path had given this connection back; it has "
				    "been reclaimed, and a later frame makes it again\n");
				return (EAGAIN);
			}
		}
		if (ipsec > 0) {
			/*
			 * The associations and ipsec.flows were read with nothing locked. Under the
			 * lock that an association's removal and a change of ipsec.flows both take,
			 * they are looked at once more, and what the microflow will name is written
			 * from that look.
			 */
			if (sc->ipsec_flows == 0 || (d != fi.enc_dir && sc->ipsec_flows < 2) ||
			    !octep_ipsec_flow_live(sc, &fi, d == fi.enc_dir ? 0 : fsa, fsa_rev)) {
				mtx_unlock(&sc->mtx);
				sbuf_cat(sb, "the association or ipsec.flows changed under this "
				    "frame - try again\n");
				return (EAGAIN);
			}
			c->enc_dir = (uint8_t)fi.enc_dir;
			if (d == fi.enc_dir) {
				c->mf[d].sa = fi.out_sa;
				c->mf[d].sa_rev = fi.out_rev;
				c->mf[d].dsa = c->mf[d].dsa_rev = 0;
				octep_ipsec_sa_flow_attached(sc, fi.out_sa);
			} else {
				c->mf[d].sa = c->mf[d].sa_rev = 0;
				c->mf[d].dsa = fsa;
				c->mf[d].dsa_rev = fsa_rev;
				octep_ipsec_sa_flow_attached(sc, fsa);
			}
		}
		if (c->mf[d].nhop != 0) {
			octep_nhop_put(sc, c->mf[d].nhop);
			c->mf[d].nhop = 0;
		}
		c->mf[d].slot = slot;
		c->mf[d].rev = rev;
		c->mf[d].in_dif = in_dif;
		c->mf[d].in_tag = tag;
		c->mf[d].punts = 0;
		c->mf[d].ka_seen = 0;	/* another entry: its stamp is not this one's */
		c->mf[d].vlan_rw = (uint8_t)(vlan != 0 || nh[d].vlan != 0);
		err = octep_nhop_get(sc, &nh[d], &c->mf[d].nhop);
		if (err == 0)
			err = octep_rpc_mflow_set(sc, c, d, OCTEP_MFLOW_STATE_ACTIVE);
		if (err != 0) {
			if (err == ETIMEDOUT && c->mf[d].nhop != 0) {
				/* Not answered is not refused: see `doomed` in struct octep_conn. */
				c->mf[d].state = OCTEP_MF_PROGRAMMED;
				c->doomed = 1;
			} else {
				if (c->mf[d].nhop != 0) {
					octep_nhop_put(sc, c->mf[d].nhop);
					c->mf[d].nhop = 0;
				}
				c->mf[d].state = OCTEP_MF_NONE;
			}
			idx = c->idx;
			mtx_unlock(&sc->mtx);
			if (err == EIO)
				sc->dp_flow_rc_refused++;
			sbuf_printf(sb, "attaching the %s direction to connection %u refused: %d\n",
			    d == 0 ? "original" : "reply", idx, err);
			if (stop != NULL)
				*stop = 1;
			return (err);
		}
		c->mf[d].state = OCTEP_MF_PROGRAMMED;
		sc->dp_flow_attached++;
		idx = c->idx;
		mask = 1u << d;
		/*
		 * A direction re-attached under a new identity is usually a connection that was idle
		 * past the board's ten-second timeout, and the far side expired BOTH its microflows at
		 * once. Re-attaching one leaves the connection half-offloaded until the other's frame
		 * comes round a poll later - the window the fast path hands a connection back in. So
		 * the other direction is looked up in the candidate table now, and if its frame has
		 * already been punted under a new identity it is re-attached in the same poll, to the
		 * next hop it already has.
		 */
		if (c->mf[rd].nhop != 0 &&
		    octep_flow_cand_ident(sc, &c->tuple[rd], 0, &oslot, &orev, &oin_dif, &otag,
		    &osa, &osa_rev, NULL, NULL, NULL) &&
		    (c->mf[rd].state != OCTEP_MF_PROGRAMMED || c->mf[rd].slot != oslot ||
		    c->mf[rd].rev != orev) &&
		    (sc->rpc_plat_num_mflows == 0 || oslot < sc->rpc_plat_num_mflows) &&
		    (ipsec > 0 ? (rd == fi.enc_dir ? (osa == 0 && oin_dif != fi.out_dif) :
		    (sc->ipsec_flows >= 2 && osa != 0 &&
		    octep_ipsec_flow_live(sc, &fi, osa, osa_rev))) : osa == 0)) {
			c->mf[rd].slot = oslot;
			c->mf[rd].rev = orev;
			c->mf[rd].in_dif = oin_dif;
			c->mf[rd].in_tag = otag;
			c->mf[rd].punts = 0;
			c->mf[rd].ka_seen = 0;
			if (ipsec > 0 && rd == fi.enc_dir) {
				c->mf[rd].sa = fi.out_sa;
				c->mf[rd].sa_rev = fi.out_rev;
				octep_ipsec_sa_flow_attached(sc, fi.out_sa);
			} else if (ipsec > 0) {
				c->mf[rd].dsa = osa;
				c->mf[rd].dsa_rev = osa_rev;
				octep_ipsec_sa_flow_attached(sc, osa);
			}
			{
				int rerr = octep_rpc_mflow_set(sc, c, rd, OCTEP_MFLOW_STATE_ACTIVE);

				if (rerr == 0) {
					c->mf[rd].state = OCTEP_MF_PROGRAMMED;
					sc->dp_flow_attached++;
					mask |= 1u << rd;
				} else {
					if (rerr == ETIMEDOUT) {
						c->mf[rd].state = OCTEP_MF_PROGRAMMED;
						c->doomed = 1;
					} else
						c->mf[rd].state = OCTEP_MF_NONE;
					/*
					 * A refusal is counted inside the post. A timeout is the far side not
					 * answering, and the drain's bound is one two-second wait per poll - so
					 * say so, or the next candidate pays the same two seconds.
					 */
					if (rerr == ETIMEDOUT && stop != NULL)
						*stop = 1;
				}
			}
		}
	} else {
		if (octep_conn_find(sc, &t, NULL) != NULL) {
			mtx_unlock(&sc->mtx);
			sbuf_cat(sb, "the table changed under this frame - try again\n");
			return (EAGAIN);
		}
		c = octep_conn_alloc(sc, &st, tup, d, slot, rev, in_dif, tag,
		    tcp_on ? tcpblk : NULL, tcp_able);
		if (c == NULL) {
			mtx_unlock(&sc->mtx);
			sbuf_printf(sb, "no room: all %u connection entries are in use\n",
			    sc->dp_conn_max - 2);
			if (stop != NULL)
				*stop = 1;	/* the next candidate has nowhere to go either */
			return (ENOSPC);
		}
		if (have_rev) {
			c->mf[rd].slot = rslot;
			c->mf[rd].rev = rrev;
			c->mf[rd].in_dif = rin_dif;
			c->mf[rd].in_tag = rtag;
		}
		c->mf[OCTEP_CONN_DIR_ORIGINAL].dscp = dscp[OCTEP_CONN_DIR_ORIGINAL];
		c->mf[OCTEP_CONN_DIR_REPLY].dscp = dscp[OCTEP_CONN_DIR_REPLY];
		c->bridged = (uint8_t)bridged;
		/* the VLAN written where a direction arrives tagged or leaves by a VLAN (discovery 10) */
		c->mf[d].vlan_rw = (uint8_t)(vlan != 0 || nh[d].vlan != 0);
		if (have_rev)
			c->mf[rd].vlan_rw = (uint8_t)(rvlan != 0 || nh[rd].vlan != 0);
		if (ipsec > 0) {
			/* Looked at once more under the lock their removal takes: see the attach. */
			dec = 1 - fi.enc_dir;
			if (sc->ipsec_flows == 0 ||
			    (sc->ipsec_flows < 2 && (d == dec || have_rev)) ||
			    !octep_ipsec_flow_live(sc, &fi, d == dec ? fsa : (have_rev ? rsa : 0),
			    d == dec ? fsa_rev : rsa_rev)) {
				octep_conn_free(sc, c);
				mtx_unlock(&sc->mtx);
				sbuf_cat(sb, "the association or ipsec.flows changed under this "
				    "frame - try again\n");
				return (EAGAIN);
			}
			c->ipsec = 1;
			c->enc_dir = (uint8_t)fi.enc_dir;
			c->mf[fi.enc_dir].sa = fi.out_sa;
			c->mf[fi.enc_dir].sa_rev = fi.out_rev;
			octep_ipsec_sa_flow_attached(sc, fi.out_sa);
			if (d == dec) {
				c->mf[dec].dsa = fsa;
				c->mf[dec].dsa_rev = fsa_rev;
				octep_ipsec_sa_flow_attached(sc, fsa);
			} else if (have_rev) {
				c->mf[dec].dsa = rsa;
				c->mf[dec].dsa_rev = rsa_rev;
				octep_ipsec_sa_flow_attached(sc, rsa);
			}
		}
		err = octep_nhop_get(sc, &nh[d], &c->mf[d].nhop);
		if (err == 0 && have_rev) {
			if (octep_nhop_get(sc, &nh[rd], &c->mf[rd].nhop) != 0)
				have_rev = 0;
		}
		mask = (1u << d) | (have_rev ? (1u << rd) : 0);
		if (err == 0)
			err = octep_rpc_flow_create(sc, c, mask);
		if (err == 0)
			octep_ips_mark(sc, c);
		if (err != 0) {
			idx = c->idx;
			kept = 0;
			if (err == ETIMEDOUT && c->mf[d].nhop != 0) {
				/*
				 * Not answered is not refused. FLOW_CREATE was posted - the next hop
				 * is in hand, so it was reached - and the entry stays, with every
				 * direction the command named counted as programmed, so that the
				 * audit sends the commands that take it out as soon as the far side
				 * answers. A timeout before that, on the next hop itself, posted
				 * nothing about this connection and leaves nothing to stand for.
				 */
				c->mf[d].state = OCTEP_MF_PROGRAMMED;
				if (have_rev && c->mf[rd].nhop != 0)
					c->mf[rd].state = OCTEP_MF_PROGRAMMED;
				c->doomed = 1;
				kept = 1;
			} else
				octep_conn_free(sc, c);
			mtx_unlock(&sc->mtx);
			/*
			 * And if the far side does carry it out, pf must not judge the frames that
			 * come back when the audit takes it out again: the marking every connection
			 * that may be in hardware gets, further down.
			 */
			if (kept && sc->dp_pf_sloppy != 0) {
				(void)octep_pf_mark_sloppy(&tup[0]);
				(void)octep_pf_mark_sloppy(&tup[1]);
			}
			if (err == EIO)
				sc->dp_flow_rc_refused++;
			sbuf_printf(sb, "programming connection %u refused: %d\n", idx, err);
			/*
			 * Worth giving up the whole drain for. A refused post is almost always the
			 * far side not answering, and each one costs up to OCTEP_RPC_CMD_WAIT_MS with
			 * the softc lock held - so pressing on would hold that lock against the
			 * transmit path for as long as there are candidates.
			 */
			if (stop != NULL)
				*stop = 1;
			return (err);
		}
		c->mf[d].state = OCTEP_MF_PROGRAMMED;
		if (have_rev)
			c->mf[rd].state = OCTEP_MF_PROGRAMMED;
		idx = c->idx;
		if (tos_marked > 0)
			sc->dp_flow_dscp++;
		if (bridged)
			sc->dp_flow_bridged++;
		if (c->mf[0].vlan_rw || c->mf[1].vlan_rw)
			sc->dp_flow_vlan++;
		if (t.proto == IPPROTO_TCP) {
			if (tcp_on)
				sc->dp_tcp_checked++;
			else
				sc->dp_tcp_unchecked++;
		}
	}
	mtx_unlock(&sc->mtx);
	if (ipsec > 0 && !attach)
		sc->ipsec_flow_made++;

	/*
	 * From here on pf will not see this connection's frames, so it must stop judging their
	 * sequence numbers: the first frame to come back to it - at the discard, or handed back by
	 * the fast path - would otherwise be measured against a window pf never saw advance, and
	 * dropped as a bad state. Measured: 786 such drops and a dead connection without this.
	 *
	 * From BOTH tuples, not from the frame's. A tuple reaches only the pf states that share a
	 * key arrangement with it, and the reply tuple of a translated connection - server to the
	 * WAN address - reaches the WAN-side state alone; the LAN-side state's key is the opener's
	 * address, which that tuple never carries. So every connection the drain met reply-first,
	 * about half of them, had one state sloppy and one strict, and the strict one dropped every
	 * frame the fast path handed back. Measured 2026-10-07 (#287): `pfctl -vss` on a connection
	 * learned from its reply showed the WAN state sloppy and the LAN state not; pf counted
	 * 16,104 state-mismatch drops in an hour of load, and one download in three was reset.
	 */
	marked = sc->dp_pf_sloppy != 0 ?
	    octep_pf_mark_sloppy(&tup[0]) + octep_pf_mark_sloppy(&tup[1]) : 0;

	sbuf_printf(sb, "connection %u%s: %s%s%s programmed, %d pf state(s) marked sloppy%s\n", idx,
	    attach ? " (attached)" : "",
	    (mask & 1u) ? "original" : "", mask == 3u ? " and " : "",
	    (mask & 2u) ? "reply" : "", marked,
	    attach || t.proto != IPPROTO_TCP ? "" : tcp_on ? ", the window checked in hardware" :
	    ", the window not checked (pf's numbers had stopped, or a rule asked for sloppy)");
	if (ipsec > 0)
		sbuf_printf(sb, "  tunnelled: the %s direction leaves encrypted by association handle "
		    "%u rev %u toward 0x%08x; the other arrives decrypted and %s\n",
		    fi.enc_dir == 0 ? "original" : "reply", fi.out_sa, fi.out_rev, fi.out_dst,
		    (mask & (1u << (1 - fi.enc_dir))) != 0 ? "is forwarded here too" :
		    "stays with the host");
	sbuf_printf(sb, "  %s direction to %02x:%02x:%02x:%02x:%02x:%02x on interface %u, mtu %u%s\n",
	    d == 0 ? "original" : "reply",
	    nh[d].dmac[0], nh[d].dmac[1], nh[d].dmac[2], nh[d].dmac[3], nh[d].dmac[4],
	    nh[d].dmac[5], nh[d].iface, nh[d].mtu, nh[d].from_flow ?
	    "  (port from the other direction of this connection, not from the route)" : "");
	if (!attach && (mask & (1u << rd)) != 0)
		sbuf_printf(sb, "  %s direction to %02x:%02x:%02x:%02x:%02x:%02x on interface %u, "
		    "mtu %u, identity from the candidate table%s\n",
		    rd == 0 ? "original" : "reply",
		    nh[rd].dmac[0], nh[rd].dmac[1], nh[rd].dmac[2], nh[rd].dmac[3], nh[rd].dmac[4],
		    nh[rd].dmac[5], nh[rd].iface, nh[rd].mtu, nh[rd].from_flow ?
		    "  (port from this frame)" : "");
	else if (!attach)
		sbuf_printf(sb, "  the %s direction has not been punted yet; programmed half on "
		    "request, and it attaches when it is\n", rd == 0 ? "original" : "reply");
	if (st.nat_snat)
		sbuf_printf(sb, "  do_snat: the opener 0x%08x:%u is 0x%08x:%u on the wire\n",
		    st.orig_src, ntohs(st.orig_sport), st.nat_src, ntohs(st.nat_sport));
	if (st.nat_dnat)
		sbuf_printf(sb, "  do_dnat: the responder 0x%08x:%u is 0x%08x:%u on the wire\n",
		    st.orig_dst, ntohs(st.orig_dport), st.nat_dst, ntohs(st.nat_dport));
	if (!st.nat_valid)
		sbuf_cat(sb, "  not translated\n");
	return (0);
}

/*
 * Accelerate the one frame the receive path kept for an operator to look at.
 *
 * This is dp.accelerate, and it stays exactly as it was: one frame, the most recent, with the
 * sequence check that makes sure the slot and the tuple came from the same one. It is the hand
 * instrument, and the automatic path no longer goes through it - see octep_dp_flow_drain.
 */
static int
octep_dp_accelerate(struct octep_softc *sc, struct sbuf *sb)
{
	struct octep_pf_tuple t;
	struct octep_dp_if *dif;
	uint32_t slot, rev;
	uint64_t seq, pseq;
	uint16_t tag;
	uint8_t mac[12];
	uint16_t vlan = 0;
	int in_dif, havemac;

	/*
	 * Asked before the capture is read, so the first failure reported is the first one that is
	 * true. Factoring the worker out moved this below the read, and it then said "no punted
	 * frame to act on" on a box where the real answer was that pf is not loaded.
	 */
	if (!octep_pf_present()) {
		sbuf_cat(sb, "pf is not loaded, so there is no verdict to act on\n");
		return (ENXIO);
	}

	mtx_lock(&sc->mtx);
	t = sc->dp_rx_tuple;
	seq = sc->dp_rx_tuple_seq;
	pseq = sc->dp_rx_prefix_seq;
	slot = sc->dp_rx_slot;
	rev = sc->dp_rx_slot_rev;
	tag = sc->dp_rx_tag;
	dif = octep_dp_if_by_tag(sc, tag);
	in_dif = (dif != NULL) ? (int)(dif - sc->dp_if) : -1;
	havemac = (sc->dp_rx_frame_len >= sizeof(mac));
	if (havemac)
		memcpy(mac, sc->dp_rx_frame, sizeof(mac));
	if (sc->dp_rx_frame_len >= ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN &&
	    be16dec(sc->dp_rx_frame + 12) == ETHERTYPE_VLAN)
		vlan = be16dec(sc->dp_rx_frame + 14) & 0x0fff;
	mtx_unlock(&sc->mtx);

	if (seq == 0 || seq != pseq) {
		sbuf_cat(sb, "no punted frame to act on\n");
		return (ENOENT);
	}
	return (octep_dp_flow_make(sc, &t, slot, rev, in_dif, tag, 0, 0, havemac ? mac : NULL, vlan,
	    sb, NULL));
}

/*
 * Turn candidates into flows: up to OCTEP_FLOW_PER_POLL attempts that cost a command.
 *
 * This is what the automatic trigger runs instead of acting on the single capture slot. The
 * measurement that made it necessary: on a 186 Mbit/s download the old path accelerated three flows
 * in eight seconds while 57,212 frames punted in five, because it could only ever learn one flow per
 * poll and the slot it learned from was overwritten eleven thousand times a second.
 *
 * Called from the link poll with the vnet set and nothing locked.
 *
 * THE DRAIN STOPS AT THE FIRST COMMAND THAT FAILS, deliberately - not at a refusal that posts
 * none, which in the poll is not even one of the eight. Each flow is three posted commands and each
 * can wait OCTEP_RPC_CMD_WAIT_MS for a reply while holding the softc lock, so a poll that pressed on
 * through eight timeouts would hold that lock for most of a minute. One failure is almost always the
 * far side being unreachable, in which case the next seven would fail the same way; stopping bounds
 * a poll's worst case to one timeout, which is what it was when it programmed one flow.
 *
 * A candidate whose flow the table already has is dropped without a command, which is what makes the
 * repeated frames of one connection free rather than merely deduplicated.
 *
 * KICKED is the run the receive path asked for, between polls. It deals with the connections the
 * table holds - a frame of one was punted - and, with dp.fast_make, it makes a connection for a
 * candidate the receive path has marked due: one whose place has taken a run of frames from its
 * tuple. Any other candidate for a connection that does not exist yet is left exactly as it was,
 * unread, for the poll. What bounds the rate at which connections are made is no longer the
 * poll's eight attempts a second but a budget of its own - OCTEP_FAST_MAKE_PER_RUN a run,
 * OCTEP_FAST_MAKE_PER_SEC a second - and those attempts are counted apart from the run's eight,
 * so that a burst of new connections cannot keep a given-back one waiting for its revival.
 * Returns 1 when the far side did not answer, so that the caller can stay away from it for a
 * while.
 */
static int
octep_dp_flow_drain(struct octep_softc *sc, int kicked)
{
	struct octep_pf_tuple t;
	struct octep_flow_cand_slot *cs;
	struct octep_flow_cand *e;
	struct octep_conn *c;
	struct sbuf *sb;
	uint32_t slot, rev, stamp;
	uint16_t tag, sa, sa_rev;
	uint8_t closing, asked, mac[12];
	uint16_t vlan;
	uint64_t cmds;
	uint32_t hb;
	u_int left;
	int i, cd, in_dif, tried, stop, again, perr, due, silent;
	int newmake, newtries, nomore, merr;

	tried = 0;
	silent = 0;
	newtries = 0;
	nomore = 0;
	for (i = 0; i < OCTEP_FLOW_CAND_MAX && tried < OCTEP_FLOW_PER_POLL; i++) {
		cs = &sc->dp_cand[i / OCTEP_FLOW_CAND_WAYS];
		e = &cs->way[i % OCTEP_FLOW_CAND_WAYS];
		hb = i / OCTEP_FLOW_CAND_WAYS;

		/*
		 * The same trylock the writers take, on the slot's one word. The reader skipping a
		 * busy slot costs one pass's attention to one candidate, and the alternative -
		 * waiting - is the receive path waiting on the poll.
		 */
		if (!octep_flow_cand_word_try(cs))
			continue;
		stamp = e->stamp;
		if (stamp == e->seen) {
			octep_flow_cand_word_drop(cs);
			continue;
		}
		t = e->tuple;
		slot = e->slot;
		rev = e->rev;
		in_dif = e->in_dif;
		tag = e->in_tag;
		sa = e->sa;
		sa_rev = e->sa_rev;
		closing = e->closing;
		memcpy(mac, e->mac, sizeof(mac));
		vlan = e->vlan;
		asked = e->due;
		if (!kicked) {
			e->seen = stamp;
			e->closing = 0;
			e->due = 0;
		}
		octep_flow_cand_word_drop(cs);
		newmake = 0;

		/*
		 * A kicked run is here for the connections the table holds and for the candidates
		 * the receive path asked about. One that is neither is passed over before the lock:
		 * the hot table, which the receive path already reads with nothing held, says no
		 * connection has this tuple. Without this every run took the transmit path's lock
		 * and searched the whole table once for each unread place.
		 */
		if (kicked && sc->dp_conn_hot[octep_conn_hot_slot(&t)] == 0) {
			if (!asked || closing || nomore || sc->dp_fast_make == 0 ||
			    sc->dp_accel_dir != 0 || sc->dp_accel_half != 0 ||
			    octep_cand_held(sc, hb))
				continue;
			/* Asked for, and nothing left to make it with: counted once a run. */
			if (newtries >= sc->dp_fast_make_per_run || sc->dp_fast_make_left == 0) {
				sc->dp_fast_make_spent++;
				continue;
			}
		}

		/*
		 * A candidate for an identity this table already has programmed is a frame the fast
		 * path handed back from an active microflow. A few are normal; a run of them is the
		 * only host-visible sign that the far side has stopped using the connection, so after
		 * OCTEP_FLOW_PUNT_PROBE of them the entry is read back. Any other candidate - a new
		 * connection, the other direction of a known one, or a known direction under a new
		 * identity - goes to octep_dp_flow_make, which creates or attaches.
		 */
		mtx_lock(&sc->mtx);
		c = octep_conn_find(sc, &t, &cd);
		if (kicked) {
			if (c == NULL) {
				/*
				 * A tuple the table does not hold. Made here only when the receive
				 * path asked for it; otherwise left unread, for the poll, as every
				 * such candidate was before. Not one that is ending, not while an
				 * instrument has both-or-neither switched off, and not once this run
				 * has met a full table.
				 */
				if (!asked || closing || nomore || sc->dp_fast_make == 0 ||
				    sc->dp_accel_dir != 0 || sc->dp_accel_half != 0 ||
				    octep_cand_held(sc, hb)) {
					/* Not asked for: the hot table's perhaps was a no. */
					if (!asked)
						sc->dp_fast_scans++;
					mtx_unlock(&sc->mtx);
					continue;
				}
				/* The budget is spent: it stays due, and the poll takes it anyway. */
				if (newtries >= sc->dp_fast_make_per_run || sc->dp_fast_make_left == 0) {
					mtx_unlock(&sc->mtx);
					sc->dp_fast_make_spent++;
					continue;
				}
				/*
				 * Taken: the request is withdrawn before it is acted on, so that
				 * one asking is one attempt however many runs go by. With a writer
				 * inside the slot that cannot be done, and the attempt waits.
				 */
				if (!octep_flow_cand_word_try(cs)) {
					mtx_unlock(&sc->mtx);
					continue;
				}
				if (octep_tuple_same(&e->tuple, &t)) {
					/*
					 * A FIN or RST of this tuple since it was read, while
					 * this run waited for the lock: it is ending, and the poll
					 * will say so.
					 */
					if (e->closing != 0) {
						octep_flow_cand_word_drop(cs);
						mtx_unlock(&sc->mtx);
						continue;
					}
					e->due = 0;
					if (e->stamp == stamp)
						e->seen = stamp;
				}
				octep_flow_cand_word_drop(cs);
				newmake = 1;
			}
			/*
			 * Ours: taken now. The writers hold this bit for a few stores and never
			 * with sc->mtx, so trying it here cannot wait or deadlock; a writer in the
			 * way leaves the candidate to be read again, which costs a lookup.
			 */
			if (!newmake && octep_flow_cand_word_try(cs)) {
				if (e->stamp == stamp) {
					e->seen = stamp;
					e->closing = 0;
				}
				octep_flow_cand_word_drop(cs);
			}
		}
		/*
		 * The table holds this tuple: whatever was asked for it is answered, and the count
		 * of its frames starts again - so that if the connection is taken out, the frames
		 * that follow ask for it at the sixteenth and not at the next power of two above
		 * everything it ever handed back.
		 */
		if (c != NULL && octep_flow_cand_word_try(cs)) {
			if (octep_tuple_same(&e->tuple, &t)) {
				e->due = 0;
				e->run = 0;
			}
			octep_flow_cand_word_drop(cs);
		}
		if (c != NULL && closing)
			c->closing = 1;
		if (c == NULL && closing) {
			/*
			 * The frame that ends a connection does not make one. Without this the
			 * poll built the connection again from its FIN, for the fast path to give
			 * back at the next frame and the probe to take out a second later.
			 */
			mtx_unlock(&sc->mtx);
			continue;
		}
		if (c != NULL && c->mf[cd].state == OCTEP_MF_PROGRAMMED &&
		    c->mf[cd].slot == slot && c->mf[cd].rev == rev) {
			sc->dp_cand_known++;
			/*
			 * A tunnelled connection is asked about at the FIRST such poll, not the
			 * third. Three is a tolerance for the odd frame a healthy connection hands
			 * back, and it costs three seconds; a download through a tunnel at the
			 * LAN port's capacity is given back by the fast path every few seconds -
			 * its rule for retransmissions fires on the duplicate acknowledgements one
			 * lost segment produces - and three seconds each time left a twelfth of it
			 * in hardware, measured. One read of the connection entry per punted poll
			 * is what asking costs.
			 *
			 * And when the answer is that it had been given back, the entry has just
			 * been reclaimed and freed - so this frame is not a hand-back any more, it
			 * is the first candidate of the connection's successor, and it goes on to
			 * octep_dp_flow_make below in the same poll instead of waiting for the next.
			 */
			/* Not for one that is ending: taken out, it is not to be made again. */
			again = (c->ipsec != 0 && !c->closing);
			perr = 0;
			/*
			 * A kicked run asks at the first frame, whatever the connection: the three
			 * polls were three seconds, and here they would be three runs a few
			 * milliseconds apart, which tolerates nothing. What bounds the asking
			 * instead is time - not the same connection twice inside the probe gap,
			 * and not one that has just been revived until its wait is over.
			 */
			if (kicked)
				/*
				 * Unsigned: the stamps are never in the future, and a signed
				 * difference turns negative for one more than 2^31 ticks old -
				 * which a connection that lasts a month would be.
				 */
				due = ((u_int)(ticks - c->probe_tick) >=
				    (u_int)OCTEP_FAST_PROBE_GAP &&
				    (u_int)(ticks - c->rv_tick) >= (u_int)c->rv_wait);
			else
				due = (++c->mf[cd].punts >= (again ? 1u : OCTEP_FLOW_PUNT_PROBE));
			if (due) {
				c->mf[cd].punts = 0;
				c->probe_tick = ticks;
				perr = octep_conn_probe(sc, c);
			}
			mtx_unlock(&sc->mtx);
			/*
			 * Not answered: that was two seconds with the transmit path's lock held,
			 * and the next candidate would wait its own two. The poll ends here.
			 */
			if (perr == ETIMEDOUT) {
				silent = 1;
				break;
			}
			if (!again || perr != EJUSTRETURN || closing)
				continue;
		} else
			mtx_unlock(&sc->mtx);

		/*
		 * A connection being left with the host after a give-back costs nothing here: not
		 * a lookup, and not one of this poll's attempts - eight of them at the front of the
		 * ring would otherwise keep every other candidate waiting out their minute.
		 */
		if (sc->dp_revive_hold != 0 && octep_hold_hit(sc, &t)) {
			sc->dp_hold_skipped++;
			if (newmake)
				octep_cand_hold(sc, hb);
			continue;
		}
		if (sc->ipsec_flows == 1 && octep_ipsec_backoff_hit(sc, &t)) {
			sc->ipsec_flow_backoff++;
			/* An asking that ends here was withdrawn and is not to be made again. */
			if (newmake)
				octep_cand_hold(sc, hb);
			continue;
		}

		sb = sbuf_new_auto();
		if (sb == NULL)
			return (silent);
		stop = 0;
		/*
		 * Counted as an attempt when it cost a command, whether or not it worked: the
		 * budget bounds the commands a pass sends with the transmit path's lock held.
		 *
		 * It used to count every attempt, and that was a fault with a measurement to its
		 * name. A refusal that sends nothing - pf has no state, the route has no front
		 * port, the states are not settled - costs a few lookups, and eight of them at the
		 * front of the ring were the whole of a poll: forty-eight slow flows that can never
		 * be accelerated ran beside eight downloads, the refusals read exactly eight a
		 * second, and four of the downloads were never made a connection at all. The walk
		 * is the table's 256 places at most - sixty-four slots of four - which is the
		 * bound on what is not counted. It was sixty-four.
		 *
		 * The poll's attempts, that is. A kicked run counts every one, as it always did:
		 * it comes two hundred times a second, and eight attempts of any kind is its bound.
		 */
		if (newmake) {
			newtries++;
			/*
			 * Spent with a compare and swap. The poll refills the word from another
			 * thread, and a plain decrement that straddled the refill wrote what was
			 * left of the old second over the new one.
			 */
			left = sc->dp_fast_make_left;
			while (left != 0 && atomic_fcmpset_int(&sc->dp_fast_make_left, &left,
			    left - 1) == 0)
				;
			sc->dp_fast_make_tries++;
		}
		cmds = sc->rpc_wait_n + sc->rpc_timeouts;
		merr = octep_dp_flow_make(sc, &t, slot, rev, in_dif, tag, sa, sa_rev, mac, vlan, sb,
		    &stop);
		if (!newmake && (kicked || merr == 0 || sc->rpc_wait_n + sc->rpc_timeouts != cmds))
			tried++;
		if (merr == 0) {
			sc->dp_auto_made++;
			sc->dp_cand_taken++;
			if (newmake)
				sc->dp_fast_made++;
		} else if (newmake) {
			/* Refused: this slot is the poll's until the poll has been round. */
			sc->dp_fast_make_refused++;
			octep_cand_hold(sc, hb);
		}
		sbuf_delete(sb);

		/*
		 * Only a posted command failing, or the table being full, is worth giving up the whole
		 * drain for. The ordinary refusals - pf has no state for this tuple yet, the route has
		 * no front port, the neighbour is not resolved - are frequent, cost no command, and say
		 * nothing about the next candidate. Treating them as fatal was the first version of this
		 * loop and it would have stopped on the first DNS query that had already closed.
		 */
		if (stop != 0) {
			/*
			 * A full table met between polls is not the far side being silent, and must
			 * not cost the connections the table does hold their next second of runs:
			 * making stops until the poll has been round, and nothing else does.
			 */
			if (kicked && merr == ENOSPC) {
				nomore = 1;
				sc->dp_fast_make_left = 0;
				continue;
			}
			/*
			 * And a create the far side answered with a refusal is an answer: the
			 * identity taken from the other direction's candidate was stale, most
			 * likely. Only a far side that did not answer is one to stay away from -
			 * which an attempt that succeeded can also have met, on its second command.
			 */
			if (newmake && merr != 0 && merr != ETIMEDOUT)
				continue;
			silent = 1;
			break;
		}
	}
	return (silent);
}

/*
 * Keep pf's states alive for the connections the coprocessor is still forwarding.
 *
 * pf sees none of an accelerated connection's packets. Its state for the connection is therefore
 * never restamped and runs out on the timer of the last packet pf did see - sixty seconds for UDP,
 * a day for an established TCP connection - and the sweep, finding no state, takes the connection
 * out exactly as it does one that has ended. Measured before this existed: a steady UDP flow
 * through NAT was taken out every sixty-five seconds and came back with a different source port,
 * because the state pf made for its next datagram was a new translation; and a TCP download
 * stopped dead when its states were removed, because pf's rules pass a TCP packet without a state
 * only if it is a SYN.
 *
 * So the far side is asked. Every OCTEP_KA_PERIOD seconds, for each connection, each programmed
 * direction's microflow is read back. It is still the one this table programmed - valid, the same
 * revision - and its own timestamp has moved since the last reading: frames have gone through it
 * that the host did not see, and pf's states for the connection are restamped as those frames
 * would have restamped them. Otherwise nothing is done, and that is the other half of the design:
 * a connection that has gone quiet is not kept, and ends on pf's timer as it always did; one whose
 * microflows the fast path has removed is not kept either, and its next frame, which comes to the
 * host, restamps pf by itself.
 *
 * Called from the link poll with the vnet set and nothing locked. One command per programmed
 * direction, the softc lock held for each; a far side that does not answer ends the pass, is left
 * alone for a period, and is told about in the log once and not once a period.
 */
static void
octep_flow_keepalive(struct octep_softc *sc)
{
	struct octep_pf_tuple tup[2];
	struct octep_conn *c;
	uint64_t gp[2], gb[2];
	uint32_t i, k, valid, rev, stamp;
	uint16_t crev;
	int d, reads, alive, err, silent, give, ns;

	if (sc->dp_keepalive == 0 || !octep_pf_present())
		return;
	/*
	 * A far side that did not answer is left alone for a period. Without this each poll would
	 * come to the next connection that is due and wait its own two seconds with the transmit
	 * path's lock held - on a table of a few connections, two seconds in every three.
	 */
	if ((int)(sc->dp_ka_hold - time_uptime) > 0)
		return;
	reads = 0;
	silent = 0;
	/* dp_ka_next is a place in the live list, so a pass costs what the table holds. */
	for (k = 0; k < sc->dp_conn_nlive && reads < OCTEP_KA_READS_PER_POLL; k++) {
		i = sc->dp_ka_next;
		if (i >= sc->dp_conn_nlive)
			i = 0;
		sc->dp_ka_next = i + 1;
		i = sc->dp_conn_live[i];

		mtx_lock(&sc->mtx);
		c = &sc->dp_conn[i];
		if (!c->used || c->doomed ||
		    (int)(time_uptime - c->ka_time) < OCTEP_KA_PERIOD) {
			mtx_unlock(&sc->mtx);
			continue;
		}
		c->ka_time = time_uptime;
		alive = 0;
		for (d = 0; d < 2; d++) {
			if (c->mf[d].state != OCTEP_MF_PROGRAMMED)
				continue;
			sc->rpc_quiet = sc->dp_ka_silent;
			err = octep_rpc_mflow_peek(sc, c->mf[d].slot, &valid, &rev, &stamp);
			sc->rpc_quiet = 0;
			reads++;
			sc->dp_ka_reads++;
			if (err == ETIMEDOUT) {
				silent = 1;	/* and what the other direction said still counts */
				break;
			}
			sc->dp_ka_silent = 0;
			if (err != 0 || !valid || (rev & 0x3f) != (c->mf[d].rev & 0x3f)) {
				c->mf[d].ka_seen = 0;
				continue;
			}
			if (!c->mf[d].ka_seen || stamp != c->mf[d].ka_stamp)
				alive = 1;
			c->mf[d].ka_seen = 1;
			c->mf[d].ka_stamp = stamp;
		}
		if (alive && c->dropped)
			c->drop_alive = time_uptime;	/* frames still coming: kept by the sweep */
		tup[0] = c->tuple[0];
		tup[1] = c->tuple[1];
		/*
		 * And what the coprocessor counted for it since the last visit goes to pf's states, when
		 * dp.hw_to_pf asks: pf sees none of these packets, so its state - and the States page,
		 * and the rule's counters - would otherwise stop at the moment the flow was made.
		 */
		crev = c->conn_rev;
		give = 0;
		for (d = 0; d < 2; d++) {
			gp[d] = c->hw_pkts[d] - c->hw_pf_pkts[d];
			gb[d] = c->hw_bytes[d] - c->hw_pf_bytes[d];
			if (sc->dp_hw_to_pf == 1 && (gp[d] != 0 || gb[d] != 0))
				give = 1;
		}
		mtx_unlock(&sc->mtx);

		if (alive && octep_pf_touch(&tup[0]) + octep_pf_touch(&tup[1]) != 0)
			sc->dp_ka_touched++;
		else if (!silent)
			sc->dp_ka_idle++;
		if (give && (ns = octep_pf_count(tup, gp, gb)) > 0) {
			mtx_lock(&sc->mtx);
			if (c->used && c->conn_rev == crev) {
				for (d = 0; d < 2; d++) {
					c->hw_pf_pkts[d] += gp[d];
					c->hw_pf_bytes[d] += gb[d];
				}
			}
			sc->dp_hw_pf_conns++;
			sc->dp_hw_pf_states += (uint64_t)ns;
			mtx_unlock(&sc->mtx);
		}
		if (silent) {
			sc->dp_ka_hold = time_uptime + OCTEP_KA_PERIOD;
			sc->dp_ka_silent = 1;
			return;
		}
	}
}

/*
 * The run the receive path kicks: see octep_flow_cand_put and octep_dp_flow_drain. On the driver's
 * own task queue, a kernel thread that may sleep and has no vnet until it is given one - the same
 * vnet0, for the same reason, as the link poll below.
 *
 * Runs are kept a hold-off apart HERE, by waiting out what is left of it before starting: the
 * receive path kicks whenever a held connection's frame is punted and no kick is outstanding, and
 * the kick stays outstanding through the wait. It is let go before the run, so a frame punted
 * while the run is under way asks for the next one. When the far side has stopped answering the
 * hold-off is a second, and the poll - which runs regardless - is what finds out that it is back.
 */
static void
octep_dp_fast_task(void *arg, int pending __unused)
{
	struct octep_softc *sc = arg;
	int left, silent;

	left = sc->dp_fast_hold - ticks;
	if (left > 0 && left <= hz)
		pause("octepf", left);
	atomic_store_rel_int(&sc->dp_fast_kick, 0);
	if (sc->dp_link_running == 0 || sc->dp_auto == 0 || sc->dp_fast == 0)
		return;
	sc->dp_fast_runs++;
	CURVNET_SET(vnet0);
	silent = octep_dp_flow_drain(sc, 1);
	CURVNET_RESTORE();
	sc->dp_fast_hold = ticks + (silent ? hz : OCTEP_FAST_HOLD);
}

static int
octep_sysctl_dp_accelerate(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct sbuf *sb;
	int error;

	/*
	 * sysctl(8) reads a string in two calls: the first with no buffer, to learn the length,
	 * and the second to fetch it. A read with side effects that acts on both programs two
	 * flows per command - the frame in hand, then whichever frame is being punted a moment
	 * later, which on a busy connection is its other direction. Measured: one `sysctl -n`
	 * showed two table entries, and its printed line reported the second call's view. So the
	 * first call only promises a length, and the flow is made on the call that can say so.
	 */
	if (req->oldptr == NULL) {
		req->oldidx = 512;
		return (0);
	}
	sb = sbuf_new_for_sysctl(NULL, NULL, 512, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);
	(void)octep_dp_accelerate(sc, sb);
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/*
 * dp.cand_table: the candidate table as it stands. An instrument: a slot's places are copied out
 * under its word and printed after it is let go, and nothing in the table is changed. A read of a
 * string runs this twice and each run takes all sixty-four words, so reading it while
 * dp.cand_clash is being measured adds to what is measured.
 */
static int
octep_sysctl_dp_cand_table(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct octep_flow_cand way[OCTEP_FLOW_CAND_WAYS];
	struct octep_flow_cand_slot *cs;
	struct sbuf *sb;
	int error, got, i, s, w, now;

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);
	for (s = 0; s < OCTEP_FLOW_CAND_SLOTS; s++) {
		cs = &sc->dp_cand[s];
		for (got = 0, i = 0; i < OCTEP_CAND_TRIES && !got; i++) {
			got = octep_flow_cand_word_try(cs);
			if (!got)
				cpu_spinwait();
		}
		if (!got) {
			sbuf_printf(sb, "%2d    busy\n", s);
			continue;
		}
		memcpy(way, cs->way, sizeof(way));
		now = ticks;
		octep_flow_cand_word_drop(cs);
		for (w = 0; w < OCTEP_FLOW_CAND_WAYS; w++) {
			if (way[w].stamp == 0)
				continue;
			sbuf_printf(sb, "%2d.%d  proto %u  0x%08x:%u -> 0x%08x:%u  %ju ms ago  %s  "
			    "run %u%s%s  microflow %u rev %u\n", s, w, way[w].tuple.proto,
			    way[w].tuple.sip, ntohs(way[w].tuple.sport),
			    way[w].tuple.dip, ntohs(way[w].tuple.dport),
			    (uintmax_t)((u_int)now - (u_int)way[w].when) * 1000 / (u_int)hz,
			    way[w].stamp == way[w].seen ? "read  " : "unread",
			    way[w].run, way[w].due ? "  asked" : "",
			    way[w].closing ? "  closing" : "", way[w].slot, way[w].rev);
		}
	}
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/*
 * The flow table, entry by entry.
 *
 * dp.flows is a count, which answers how many and nothing else. Which five-tuple is at which index,
 * and which front port each one arrives on, is what a wrong forward has to be read out of - and the
 * ingress port is now load-bearing, because it is the egress port the other direction uses when the
 * route names a bridge.
 */
#define	OCTEP_FLOW_TABLE_PRINT	4096
#define	OCTEP_ADDR_TEXT		(INET6_ADDRSTRLEN + 2)

/*
 * One address of a tuple as the table prints it: an IPv4 address as the word the driver holds,
 * which flows.py reads back, and an IPv6 address in full and in brackets - its word is only the
 * four folded together.
 */
static const char *
octep_tuple_addr(char buf[2][OCTEP_ADDR_TEXT], const struct octep_pf_tuple *t, int dst)
{
	if (t->af == AF_INET6)
		snprintf(buf[0], OCTEP_ADDR_TEXT, "[%s]",
		    ip6_sprintf(buf[1], (const struct in6_addr *)(dst ? t->dip6 : t->sip6)));
	else
		snprintf(buf[0], OCTEP_ADDR_TEXT, "0x%08x", dst ? t->dip : t->sip);
	return (buf[0]);
}

static int
octep_sysctl_dp_flow_table(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct octep_conn c;
	struct sbuf *sb;
	char name[2][IFNAMSIZ];
	char a[2][2][OCTEP_ADDR_TEXT];
	int error, i, d, n;

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);

	/*
	 * One entry at a time under the lock, as the sweep does, and the printing outside it: an
	 * sbuf backed by a sysctl drains to userspace and may sleep, and the whole table on the
	 * stack is a hundred kilobytes of it for no reason.
	 */
	n = 0;
	for (int k = 0; k < (int)sc->dp_conn_nlive; k++) {
		i = (int)sc->dp_conn_live[k];
		mtx_lock(&sc->mtx);
		c = sc->dp_conn[i];
		for (d = 0; d < 2; d++) {
			name[d][0] = '\0';
			if (c.used && c.mf[d].in_dif >= 0 && c.mf[d].in_dif < OCTEP_DP_IF_MAX &&
			    sc->dp_if[c.mf[d].in_dif].ifp != NULL)
				strlcpy(name[d], if_name(sc->dp_if[c.mf[d].in_dif].ifp),
				    sizeof(name[d]));
		}
		mtx_unlock(&sc->mtx);

		if (!c.used)
			continue;
		n++;
		/* The first ones are enough to read; a full table would be twenty megabytes of text. */
		if (n > OCTEP_FLOW_TABLE_PRINT)
			continue;
		sbuf_printf(sb, "%4d rev %u  proto %u%s%s%s  sweep asks pf with the %s tuple, list %u\n",
		    i, c.conn_rev, c.tuple[0].proto,
		    c.nat.snat ? "  do_snat" : "", c.nat.dnat ? "  do_dnat" : "",
		    c.bridged ? "  bridged" : "",
		    c.probe_dir == 0 ? "orig" : "reply", c.pf_dir);
		/* what the coprocessor counted for it: see octep_dp_cmsg_rx */
		if (!c.hw_seen)
			sbuf_cat(sb, "      counted  nothing yet\n");
		else
			sbuf_printf(sb, "      counted  orig %ju packets %ju bytes  reply %ju packets %ju bytes"
			    "  moved %jd s ago\n",
			    (uintmax_t)c.hw_pkts[0], (uintmax_t)c.hw_bytes[0],
			    (uintmax_t)c.hw_pkts[1], (uintmax_t)c.hw_bytes[1],
			    c.hw_moved != 0 ? (intmax_t)(time_uptime - c.hw_moved) : (intmax_t)-1);
		for (d = 0; d < 2; d++) {
			if (c.mf[d].state == OCTEP_MF_NONE && c.mf[d].slot == 0) {
				sbuf_printf(sb, "      %s  %s:%u -> %s:%u  %s\n",
				    d == 0 ? "orig " : "reply",
				    octep_tuple_addr(a[0], &c.tuple[d], 0), ntohs(c.tuple[d].sport),
				    octep_tuple_addr(a[1], &c.tuple[d], 1), ntohs(c.tuple[d].dport),
				    c.ipsec && d != c.enc_dir ? "arrives decrypted; with the host" :
				    "not punted yet");
				continue;
			}
			sbuf_printf(sb, "      %s  %s:%u -> %s:%u  slot %u rev %u  %s  "
			    "nhop %u  in %s (tag 0x%04x)  punts %u%s\n",
			    d == 0 ? "orig " : "reply",
			    octep_tuple_addr(a[0], &c.tuple[d], 0), ntohs(c.tuple[d].sport),
			    octep_tuple_addr(a[1], &c.tuple[d], 1), ntohs(c.tuple[d].dport),
			    c.mf[d].slot, c.mf[d].rev,
			    c.mf[d].state == OCTEP_MF_PROGRAMMED ? "active  " : "inactive",
			    c.mf[d].nhop, name[d][0] != '\0' ? name[d] : "no front port",
			    c.mf[d].in_tag, c.mf[d].punts, c.mf[d].vlan_rw ? "  vlan written" : "");
			if (c.ipsec && d == c.enc_dir)
				sbuf_printf(sb, "             leaves encrypted by association handle "
				    "%u rev %u\n", c.mf[d].sa, c.mf[d].sa_rev);
			else if (c.ipsec)
				sbuf_printf(sb, "             arrives decrypted; by association handle "
				    "%u rev %u when it was learned\n", c.mf[d].dsa, c.mf[d].dsa_rev);
		}
	}
	if (n > OCTEP_FLOW_TABLE_PRINT)
		sbuf_printf(sb, "and %d more connections, not printed\n", n - OCTEP_FLOW_TABLE_PRINT);
	if (n == 0)
		sbuf_cat(sb, "the table is empty\n");

	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

static int
octep_sysctl_dp_nhop_table(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct octep_nhop_ent e;
	struct sbuf *sb;
	int error, i, n;

	sb = sbuf_new_for_sysctl(NULL, NULL, 1024, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);

	n = 0;
	for (i = 0; i < (int)sc->dp_nhop_hi && i < OCTEP_NHOP_MAX; i++) {
		mtx_lock(&sc->mtx);
		e = sc->dp_nhop[i];
		mtx_unlock(&sc->mtx);
		if (!e.used)
			continue;
		n++;
		sbuf_printf(sb, "%3d rev %u  refs %u  iface %u  to %02x:%02x:%02x:%02x:%02x:%02x  "
		    "from %02x:%02x:%02x:%02x:%02x:%02x  mtu %u", i, e.rev, e.refcnt, e.iface,
		    e.dmac[0], e.dmac[1], e.dmac[2], e.dmac[3], e.dmac[4], e.dmac[5],
		    e.smac[0], e.smac[1], e.smac[2], e.smac[3], e.smac[4], e.smac[5], e.mtu);
		if (e.vlan != 0)
			sbuf_printf(sb, "  vlan %u", e.vlan);
		sbuf_cat(sb, "\n");
	}
	if (n == 0)
		sbuf_cat(sb, "no next hops are programmed\n");

	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

static int
octep_sysctl_dp_rx_quiesce(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, v;

	v = atomic_load_acq_int(&sc->dp_rx_quiesce);
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (v != 0)
		octep_dp_rx_quiesce(sc);
	else
		octep_dp_rx_resume(sc);
	return (0);
}

/*
 * Which rings have a servicer inside them, as a line of flags.
 *
 * Read on its own it is a sample and means little, because a servicer comes and goes in microseconds.
 * Read straight after a quiesce it is the whole point: every flag clear is the guarantee the detach
 * path depends on, and the only way to see that the wait did its job.
 */
static int
octep_sysctl_dp_oq_busy(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct sbuf *sb;
	uint32_t i;
	int error, busy = 0;

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);
	/*
	 * Only the rings this driver serves. dp_oq_busy is indexed by ring number and sized for the
	 * largest one, so walking the whole array prints sixty-four flags of which eight mean
	 * anything - which is how it read the first time and is not a line anybody can check.
	 */
	sbuf_printf(sb, "quiesce %d  r%u=%d",
	    atomic_load_acq_int(&sc->dp_rx_quiesce), sc->dp_ring,
	    atomic_load_acq_int(&sc->dp_oq_busy[sc->dp_ring]));
	busy = atomic_load_acq_int(&sc->dp_oq_busy[sc->dp_ring]);
	for (i = 0; i < OCTEP_DP_SIBLINGS_MAX; i++) {
		struct octep_dp_oq *oq = &sc->dp_sib[i];
		int b;

		if (oq->armed == 0)
			continue;
		b = atomic_load_acq_int(&sc->dp_oq_busy[oq->ring]);
		busy += b;
		sbuf_printf(sb, " r%u=%d", oq->ring, b);
	}
	sbuf_printf(sb, "  (%d in a ring)  waits %ju  longest %u us", busy,
	    (uintmax_t)sc->dp_quiesce_waits, sc->dp_quiesce_max_us);
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/*
 * The published ring size: only while the datapath is down, and only what the buffers cover.
 * octep_dp_start refuses the rest; this refuses it at the moment it is asked for.
 */
static int
octep_sysctl_dp_oq_rsize(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	u_int v;
	int error;

	v = sc->dp_oq_rsize;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	mtx_lock(&sc->mtx);
	if (sc->dp_up != 0)
		error = EBUSY;
	else if (v < OCTEP_DP_OQ_FETCH || v > OCTEP_DP_OQ_DESCS || v % OCTEP_DP_OQ_FETCH != 0)
		error = EINVAL;
	else
		sc->dp_oq_rsize = v;
	mtx_unlock(&sc->mtx);
	return (error);
}

/*
 * What the coprocessor's output watermark holds: only while the datapath is down, and in whole
 * fetches. See OCTEP_DP_OQ_WMARK.
 */
static int
octep_sysctl_dp_oq_wmark(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	u_int v;
	int error;

	v = sc->dp_oq_wmark;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	mtx_lock(&sc->mtx);
	if (sc->dp_up != 0)
		error = EBUSY;
	else if (v < OCTEP_DP_OQ_FETCH || v > 65536 || v % OCTEP_DP_OQ_FETCH != 0)
		error = EINVAL;
	else
		sc->dp_oq_wmark = v;
	mtx_unlock(&sc->mtx);
	return (error);
}

/*
 * Which ring is the datapath's: only while it is down, and only one this driver keeps state for.
 * It was a bare integer that said "only while down". Written while the rings were up it had the
 * service read one ring's registers against another's buffers; written above the arrays the ring's
 * number indexes it had every servicer reading and writing past them.
 */
static int
octep_sysctl_dp_ring(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	u_int v;
	int error;

	v = sc->dp_ring;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	mtx_lock(&sc->mtx);
	if (sc->dp_up != 0)
		error = EBUSY;
	else if (v > OCTEP_DP_SIBLINGS_MAX)
		error = EINVAL;
	else
		sc->dp_ring = v;
	mtx_unlock(&sc->mtx);
	return (error);
}

static int
octep_sysctl_dp_service(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, v = 0;

	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (v == 0)
		return (0);
	(void)octep_dp_service(sc);
	return (0);
}


static int
octep_sysctl_dp_time_threshold(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	unsigned int val;
	int error;

	val = sc->dp_time_threshold;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	sc->dp_time_threshold = val;
	sc->dp_time_threshold_set = 1;
	return (0);
}

static int
octep_sysctl_dp_refresh_levels(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, val = 0;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (sc->dp_up == 0)
		return (ENXIO);
	octep_dp_refresh_int_levels(sc);
	return (0);
}

static int
octep_sysctl_dp_if_add(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	unsigned int val = 0;
	int error;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (val > 0xffff)
		return (EINVAL);
	error = octep_dp_if_attach(sc, (uint16_t)val);
	/* The name given in dp.if_label was for this port only, whichever way the attach went. */
	sc->dp_if_label[0] = '\0';
	return (error);
}

static int
octep_sysctl_dp_if_del(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, val = 0;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	octep_dp_if_detach_all(sc);
	return (0);
}

static int
octep_sysctl_dp_msix(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	unsigned int val;
	int error;

	val = sc->dp_msix_on;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (val == 0) {
		octep_dp_msix_teardown(sc);
		return (0);
	}
	return (octep_dp_msix_setup(sc));
}

static int
octep_sysctl_dp_start(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, val = 0;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	return (octep_dp_start(sc));
}

static int
octep_sysctl_dp_stop(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, val = 0;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	octep_dp_stop(sc);
	return (0);
}

static int
octep_sysctl_dp_dst_mac(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	char buf[18];
	unsigned int m[6];
	int error, i;

	mtx_lock(&sc->mtx);
	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
	    sc->dp_dst_mac[0], sc->dp_dst_mac[1], sc->dp_dst_mac[2],
	    sc->dp_dst_mac[3], sc->dp_dst_mac[4], sc->dp_dst_mac[5]);
	mtx_unlock(&sc->mtx);

	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (sscanf(buf, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6)
		return (EINVAL);
	mtx_lock(&sc->mtx);
	for (i = 0; i < 6; i++)
		sc->dp_dst_mac[i] = (uint8_t)m[i];
	mtx_unlock(&sc->mtx);
	return (0);
}

static int
octep_sysctl_dp_xmit(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	unsigned int len = 0;
	int error;

	error = sysctl_handle_int(oidp, &len, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	return (octep_dp_xmit_test(sc, len));
}

/*
 * Read one register out of this ring's own block and print it decoded three ways, because a 64-bit
 * SDP register is almost never one number: it is a counter in the low bits and a pile of mode and
 * status flags above it, and reading it as a decimal integer is how a mode bit gets missed.
 */
static int
octep_sysctl_dp_peek(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct sbuf *sb;
	uint64_t v;
	uint32_t off;
	int error, i;

	off = sc->dp_peek_off;
	sb = sbuf_new_for_sysctl(NULL, NULL, 512, req);
	if (sb == NULL)
		return (ENOMEM);

	if (off < OCTEP_PEEK_FIRST || off > OCTEP_PEEK_LAST || (off & 7) != 0) {
		sbuf_printf(sb, "\nrefused: 0x%x is not an eight-byte-aligned offset inside "
		    "0x%x..0x%x\n", off, OCTEP_PEEK_FIRST, OCTEP_PEEK_LAST);
		error = sbuf_finish(sb);
		sbuf_delete(sb);
		return (error);
	}

	mtx_lock(&sc->mtx);
	v = octep_dp_rd(sc, off);
	mtx_unlock(&sc->mtx);

	sbuf_printf(sb, "\nring %u  offset 0x%05x  =  0x%016jx\n", sc->dp_ring, off,
	    (uintmax_t)v);
	sbuf_printf(sb, "  low 32   %ju\n", (uintmax_t)(v & 0xffffffffULL));
	sbuf_printf(sb, "  bits set ");
	for (i = 63; i >= 0; i--)
		if (v & (1ULL << i))
			sbuf_printf(sb, "%d ", i);
	sbuf_printf(sb, "\n");
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

static int
octep_sysctl_dp_state(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct sbuf *sb;
	uint64_t inctl, inen, inbaddr, inrsize, indbell, incnts;
	uint64_t outctl, outen, outbaddr, outrsize, outdbell, outcnts;
	uint64_t inpkts, inbytes, outpkts, outbytes;
	int error;

	sb = sbuf_new_for_sysctl(NULL, NULL, 1024, req);
	if (sb == NULL)
		return (ENOMEM);

	mtx_lock(&sc->mtx);
	inctl    = octep_dp_rd(sc, OCTEP_SDP_R_IN_CONTROL);
	inen     = octep_dp_rd(sc, OCTEP_SDP_R_IN_ENABLE);
	inbaddr  = octep_dp_rd(sc, OCTEP_SDP_R_IN_INSTR_BADDR);
	inrsize  = octep_dp_rd(sc, OCTEP_SDP_R_IN_INSTR_RSIZE);
	/*
	 * Two fields, and the high one is a position rather than a count. Its low half is what
	 * the block still owes - zero whenever it has caught up - and its high half, based at
	 * bit 32, is the fetch pointer's byte offset into the instruction ring:
	 * (instructions consumed mod RSIZE) * 64, sixty-four being the instruction size this
	 * ring is configured for. One post moves it by 64 << 32, which is the 1 << 38 that made
	 * it look like a field based at bit 38. Same shape as R_OUT_SLIST_DBELL, whose unit is
	 * sixteen because a scatter-list entry is sixteen bytes.
	 */
	indbell  = octep_dp_rd(sc, OCTEP_SDP_R_IN_INSTR_DBELL);
	incnts   = octep_dp_rd(sc, OCTEP_SDP_R_IN_CNTS);
	outctl   = octep_dp_rd(sc, OCTEP_SDP_R_OUT_CONTROL);
	outen    = octep_dp_rd(sc, OCTEP_SDP_R_OUT_ENABLE);
	outbaddr = octep_dp_rd(sc, OCTEP_SDP_R_OUT_SLIST_BADDR);
	outrsize = octep_dp_rd(sc, OCTEP_SDP_R_OUT_SLIST_RSIZE);
	outdbell = octep_dp_rd(sc, OCTEP_SDP_R_OUT_SLIST_DBELL);
	outcnts  = octep_dp_rd(sc, OCTEP_SDP_R_OUT_CNTS);
	inpkts   = octep_dp_rd(sc, OCTEP_SDP_R_IN_PKT_CNT);
	inbytes  = octep_dp_rd(sc, OCTEP_SDP_R_IN_BYTE_CNT);
	outpkts  = octep_dp_rd(sc, OCTEP_SDP_R_OUT_PKT_CNT);
	outbytes = octep_dp_rd(sc, OCTEP_SDP_R_OUT_BYTE_CNT);
	mtx_unlock(&sc->mtx);

	sbuf_printf(sb, "\nring %u, %s\n\n", sc->dp_ring,
	    sc->dp_up ? "up" : "not started by us");
	sbuf_printf(sb, "  IN_CONTROL   0x%016jx  %s%s%s\n", (uintmax_t)inctl,
	    (inctl & OCTEP_R_IN_CTL_IDLE) ? "idle " : "BUSY ",
	    (inctl & OCTEP_R_IN_CTL_IS_64B) ? "64B " : "32B ",
	    (inctl & OCTEP_R_IN_CTL_ESR) ? "ESR" : "");
	sbuf_printf(sb, "  IN_ENABLE    %ju\n", (uintmax_t)(inen & 1));
	sbuf_printf(sb, "  IN_BADDR     0x%016jx  RSIZE %ju  DBELL %ju  CNTS %ju\n",
	    (uintmax_t)inbaddr, (uintmax_t)inrsize, (uintmax_t)indbell, (uintmax_t)incnts);
	sbuf_printf(sb, "  OUT_CONTROL  0x%016jx  %s%sBSIZE %ju\n", (uintmax_t)outctl,
	    (outctl & OCTEP_R_OUT_CTL_IDLE) ? "idle " : "BUSY ",
	    (outctl & OCTEP_R_OUT_CTL_IMODE) ? "IMODE " : "",
	    (uintmax_t)(outctl & 0xffff));
	sbuf_printf(sb, "  OUT_ENABLE   %ju\n", (uintmax_t)(outen & 1));
	sbuf_printf(sb, "  OUT_BADDR    0x%016jx  RSIZE %ju  DBELL %ju  CNTS %ju\n",
	    (uintmax_t)outbaddr, (uintmax_t)outrsize, (uintmax_t)outdbell, (uintmax_t)outcnts);
	sbuf_printf(sb, "  IN_PKT_CNT   %ju   IN_BYTE_CNT %ju\n",
	    (uintmax_t)inpkts, (uintmax_t)inbytes);
	sbuf_printf(sb, "  OUT_PKT_CNT  %ju   OUT_BYTE_CNT %ju\n",
	    (uintmax_t)outpkts, (uintmax_t)outbytes);
	sbuf_printf(sb, "\n  posted by us %ju\n\n", (uintmax_t)sc->dp_tx_posted);

	if (sc->dp_up != 0) {
		mtx_lock(&sc->mtx);
		octep_dp_rx_report(sc, sb);
		mtx_unlock(&sc->mtx);
	}

	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

static int octep_sysctl_dp_meta_tpl(SYSCTL_HANDLER_ARGS);

static int
octep_sysctl_dp_txq(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct sbuf *sb;
	uint32_t i;
	int error;

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_printf(sb, "ring %u: posted %ju, full %ju (under sc->mtx)\n", sc->dp_ring,
	    (uintmax_t)sc->dp_tx_posted, (uintmax_t)sc->dp_tx_iq_full);
	for (i = 0; i < sc->dp_ntxq; i++)
		sbuf_printf(sb, "ring %u: posted %ju, full %ju\n", sc->dp_txq[i].ring,
		    (uintmax_t)sc->dp_txq[i].posted, (uintmax_t)sc->dp_txq[i].full);
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/* At detach, after octep_dp_stop, and on a failed attach: nothing reads the table any more. */
void
octep_dp_table_free(struct octep_softc *sc)
{

	free(sc->dp_conn, M_DEVBUF);
	free(sc->dp_conn_head, M_DEVBUF);
	free(sc->dp_conn_free, M_DEVBUF);
	free(sc->dp_conn_live, M_DEVBUF);
	free(sc->dp_hold, M_DEVBUF);
	sc->dp_hold = NULL;
	sc->dp_conn = NULL;
	sc->dp_conn_head = NULL;
	sc->dp_conn_free = NULL;
	sc->dp_conn_live = NULL;
	sc->dp_conn_cap = 0;
	sc->dp_conn_nfree = 0;
	sc->dp_conn_nlive = 0;
}

void
octep_dp_add_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
    struct sysctl_oid_list *top)
{
	struct sysctl_oid *node;
	uintmax_t ram_cap;
	size_t per;
	int i, cap, set;

	/*
	 * The connection table, its chains, its stack of free indexes and its live list, sized once
	 * here: the family's 2,000,000, or what a 16th of this host's memory holds if that is fewer,
	 * unless the loader's hw.octep.flow_max says otherwise. The chains are at least as many as the
	 * entries, so one is a tuple or two long. Attach may sleep, so M_WAITOK cannot fail.
	 */
	per = sizeof(*sc->dp_conn) + 3 * sizeof(uint32_t);
	ram_cap = ptoa((uintmax_t)physmem) / OCTEP_FLOW_RAM_SHARE / per;
	cap = OCTEP_FLOW_DEFAULT;
	if (ram_cap < (uintmax_t)cap)
		cap = (int)ram_cap;
	set = TUNABLE_INT_FETCH("hw.octep.flow_max", &cap);
	if (cap < OCTEP_FLOW_MIN)
		cap = OCTEP_FLOW_MIN;
	if (cap > OCTEP_FLOW_CEIL)
		cap = OCTEP_FLOW_CEIL;
	sc->dp_conn_cap = (uint32_t)cap;
	for (sc->dp_conn_hbits = 16; ((uint32_t)1 << sc->dp_conn_hbits) < sc->dp_conn_cap;
	    sc->dp_conn_hbits++)
		;
	sc->dp_conn = malloc((size_t)cap * sizeof(*sc->dp_conn), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->dp_conn_head = malloc(((size_t)1 << sc->dp_conn_hbits) * sizeof(*sc->dp_conn_head),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	sc->dp_conn_free = malloc((size_t)cap * sizeof(*sc->dp_conn_free), M_DEVBUF, M_WAITOK);
	sc->dp_conn_live = malloc((size_t)cap * sizeof(*sc->dp_conn_live), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	sc->dp_conn_nlive = 0;
	sc->dp_nhop_hi = 2;
	octep_conn_free_fill(sc);
	device_printf(sc->dev, "flow table: %u connections, %u chains, %ju bytes - %s "
	    "(the family's %u; a 16th of %ju MB of memory holds %ju)\n",
	    sc->dp_conn_cap, 1u << sc->dp_conn_hbits,
	    (uintmax_t)((size_t)cap * (sizeof(*sc->dp_conn) + 2 * sizeof(uint32_t)) +
	    ((size_t)1 << sc->dp_conn_hbits) * sizeof(uint32_t)),
	    set ? "hw.octep.flow_max" : "this host's own size", OCTEP_FLOW_DEFAULT,
	    ptoa((uintmax_t)physmem) >> 20, ram_cap);

	/*
	 * -1 and not 0, because 0 is a front port. Nothing reads in_dif of an entry that is not in
	 * use, but an index that means "none" has to be a value no index can be.
	 */
	for (i = 0; i < cap; i++) {
		sc->dp_conn[i].mf[0].in_dif = -1;
		sc->dp_conn[i].mf[1].in_dif = -1;
	}
	/* The host's bounds; the board's are read when the facility is up and may lower them. */
	sc->dp_conn_max = sc->dp_conn_cap;
	sc->dp_nhop_max = OCTEP_NHOP_MAX;

	octep_ipsec_add_sysctls(sc, ctx, top);
	octep_ipsec_attach(sc);
	octep_hb_add_sysctls(sc, ctx, top);

	node = SYSCTL_ADD_NODE(ctx, top, OID_AUTO, "dp", CTLFLAG_RD, NULL,
	    "one SDP datapath ring pair - allocated only when asked");
	if (node == NULL)
		return;

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "up",
	    CTLFLAG_RD, &sc->dp_up, 0, "1 when this driver has programmed the ring");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ring",
	    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_ring, "IU",
	    "which SDP ring to use; only while down, and refused otherwise");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sib_base",
	    CTLFLAG_RW, &sc->dp_sib_base, 0,
	    "the first output sibling ring; 0 means follow dp.ring, which is the default. Set it to "
	    "arm the output rings somewhere other than next to the input ring - the appliance's own "
	    "firmware runs its live rings at 8-15. Only while down");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "siblings",
	    CTLFLAG_RW, &sc->dp_siblings, 0,
	    "receive-only rings to arm after this one, 0 to 63; only while down. Eight in total is "
	    "what the published handshake gives the PF and what every normal run uses; the rest of "
	    "the range exists to arm every ring BAR0 holds at once, which is a diagnostic and costs "
	    "about 27 MB");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_time_threshold",
	    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_time_threshold, "IU",
	    "output interrupt time threshold, in 1024-clock ticks. Derived from the tick rate "
	    "unless it is set here, and the derived value on this board is 1 where the vendor's "
	    "own ring carries 0x56. Setting it takes effect on the next refresh_levels");
	sc->dp_intr_pkt = OCTEP_DP_OQ_INTR_PKT;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_intr_pkt",
	    CTLFLAG_RW, &sc->dp_intr_pkt, 0,
	    "output interrupt packet threshold, the low half of R_OUT_INT_LEVELS. Takes effect on "
	    "the next refresh_levels");
	sc->dp_credit_unit = OCTEP_DP_CREDIT_UNIT;
	sc->dp_oq_rsize = OCTEP_DP_OQ_DESCS;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_grant",
	    CTLFLAG_RW, &sc->dp_oq_grant, 0,
	    "the first credit written to R_OUT_SLIST_DBELL when a ring is armed, and the ceiling "
	    "its credit is held to afterwards. 0 derives it: oq_rsize and oq_wmark less sixteen, "
	    "which is credit for the buffers the host has taken and no more. The ceiling is read "
	    "at every pass: lowered on a live ring it holds credit back, which is kept and given "
	    "when it is raised again; raised, it does nothing until a ring is next armed. A first "
	    "grant above the derived one has the block write round a full ring over frames "
	    "nobody has read, and one under oq_wmark is a ring that delivers nothing");
	sc->dp_oq_wmark = OCTEP_DP_OQ_WMARK;
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_wmark",
	    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_oq_wmark, "IU",
	    "what the coprocessor's SDP_OUT_WMARK holds: the block stops sending to a ring whose "
	    "doorbell reads under it. The host cannot read it. 1,024 is what this appliance's "
	    "firmware writes, read on the coprocessor's console; the stock source writes 256. "
	    "Only while down");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_rsize",
	    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_oq_rsize, "IU",
	    "how many scatter-list entries to publish in R_OUT_SLIST_RSIZE, in sixteens. The "
	    "buffers behind them are always allocated in full, so a smaller value simply hides the "
	    "rest from the block - which is how a ring small enough to force a wrap gets tested. "
	    "Never larger than they are: that is refused here and again when the ring is started. "
	    "Only while down");
	/*
	 * The LIF this port's interface belongs to, staged before dp.if_add exactly as if_port is.
	 *
	 * It cannot be derived from the tag. The ten behind the switch follow
	 * 0x8000 | ((iface + 1) << 8), but bringup.sh gives the two cages tags 1 and 2 and
	 * interfaces 10 and 11, which that rule does not produce - so the mapping is told rather
	 * than computed, and there is one place it is written down.
	 */
	sc->dp_if_iface = OCTEP_DP_IF_PORT_AUTO;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "if_iface",
	    CTLFLAG_RW, &sc->dp_if_iface, 0,
	    "the logical interface index the next dp.if_add belongs to. Leave it at 0xffffffff "
	    "and the interface follows no LIF, so its forwarding mode is never set");
	sc->dp_if_port = OCTEP_DP_IF_PORT_AUTO;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "if_port",
	    CTLFLAG_RW, &sc->dp_if_port, 0,
	    "which NetAgent port the next if_add belongs to, for reading its MAC. Left at "
	    "0xffffffff it uses the tag, which is right only where the two coincide - they do "
	    "for the direct SFP+ cages and they do not for the switch uplink, which is port 0");
	SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "if_label",
	    CTLFLAG_RW, sc->dp_if_label, sizeof(sc->dp_if_label),
	    "the name the next dp.if_add gives its interface: the label on the metal, from the "
	    "model's table or, for an expansion module's port, the coprocessor's board file. Left "
	    "empty, a port is named from its tag as on the XGS 3300's panel; cleared by each if_add");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "if_add",
	    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_if_add, "IU",
	    "write a pport tag to present that front port to the stack as an interface. The tag is "
	    "the one its LIF was installed against, and an arriving frame carries it, so a frame "
	    "names its own interface. Only while up");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "if_del",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_if_del, "I", "write 1 to take every front-port interface away");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_untagged",
	    CTLFLAG_RD, &sc->dp_rx_untagged, 0,
	    "frames that arrived carrying a port tag no interface here claims");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_untagged_other",
	    CTLFLAG_RD, &sc->dp_rx_untagged_other, 0,
	    "of those, the ones not on the control channel's tag 254 - which the coprocessor sends "
	    "every second - so a frame from anything else on it shows here");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmsg_rx",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_cmsg_rx, "A",
	    "the coprocessor's control messages read on arrival, by type, and of its connection "
	    "counts how many were applied to a connection here, how many named one gone, and how "
	    "many messages could not be read to their end");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mirror",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_mirror, "A",
	    "the fast path's copies of what it forwarded (type 6, sent while fw_cfg has FP_PKT_DUMP): "
	    "how many arrived for each direction, and what became of them - given to the packet "
	    "capture of the port they name, or not");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hw_counted",
	    CTLFLAG_RD, &sc->dp_hw_applied, 0,
	    "connection counts from the coprocessor applied to this table since attach");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmsg_last",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_cmsg_last, "A",
	    "each type's last control message from the coprocessor, as hex after its Ethernet header");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "module_media",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_module_media, "A",
	    "what the module in each cage is: '<interface> <subtype at 1G> <subtype at 10G>', 0 unread");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_counted",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_lif_counted, "A",
	    "what the coprocessor forwarded itself on each port (type 0), in and out, since attach - "
	    "the traffic the interface's own counters do not see");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_apply",
	    CTLFLAG_RW, &sc->dp_lif_apply, 0,
	    "1: add the coprocessor's port counts - what it forwarded itself, which the interfaces' "
	    "own counters never see - to those counters; 0 no");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_applied",
	    CTLFLAG_RD, &sc->dp_lif_applied, 0, "port counts added to an interface");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_vlan",
	    CTLFLAG_RD, &sc->dp_lif_vlan, 0, "port counts for a VLAN subinterface's LIF");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_unknown",
	    CTLFLAG_RD, &sc->dp_lif_unknown, 0, "port counts for a LIF no interface here has");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hw_to_pf",
	    CTLFLAG_RW, &sc->dp_hw_to_pf, 0,
	    "1: every few seconds, add what the coprocessor counted for each connection to its pf "
	    "states and the rule that passed it, as its packets would have; 0 off (until measured)");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hw_pf_conns",
	    CTLFLAG_RD, &sc->dp_hw_pf_conns, 0, "connections whose counts were added to pf");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hw_pf_states",
	    CTLFLAG_RD, &sc->dp_hw_pf_states, 0, "pf states those counts were added to");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_prefix",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_rx_prefix, "A",
	    "the last received frame's whole prefix as hex, with every non-zero four-byte window "
	    "also read as a usfp_mflow_ident - which is what programming a flow needs and nothing "
	    "published says where a host gets");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_frame",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_rx_frame, "A",
	    "the first 64 bytes of the last received frame as hex, taken before the tag lookup - so "
	    "it shows a frame this driver went on to drop as untagged, which is how a frame the fast "
	    "path forwarded to the host's own port can be read at all");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_untag_want",
	    CTLFLAG_RW, &sc->dp_rx_untag_want, 0,
	    "which tag rx_untagged_frame should keep. The control channel is tag 254 and takes the "
	    "same path, and the coprocessor sends a statistics message on it every second while "
	    "it has anything to count, so a buffer that keeps the last untagged frame keeps one "
	    "of those. Zero keeps any; 4294967295 keeps any but 254");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_untagged_frame",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_rx_untag_frame, "A",
	    "the head of the last frame dropped for a tag no interface owns, with that tag. This is "
	    "how a frame the fast path forwarded is read: point a next hop at the host's own DPDK "
	    "port and the frame arrives here, tagged for nothing, and is kept");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_span_nobuf",
	    CTLFLAG_RD, &sc->dp_rx_span_nobuf, 0,
	    "packets of more than one buffer dropped for want of a jumbo cluster");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_spans",
	    CTLFLAG_RD, &sc->dp_rx_spans, 0,
	    "packets that used more than one receive buffer: longer than a buffer holds, written "
	    "on into the next and counted once. Taken whole - the read index moves by what the "
	    "packet used - and read by nothing yet. What sends them is the coprocessor: its "
	    "statistics messages on the control tag, which grow with the connections busy at once");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_span_bufs",
	    CTLFLAG_RD, &sc->dp_rx_span_bufs, 0,
	    "the buffers those packets used after their first. Each was a buffer a ring fell "
	    "behind the block by, for good, while a packet was taken for one buffer");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_span_max",
	    CTLFLAG_RD, &sc->dp_rx_span_max, 0,
	    "the longest length word any of them carried, in bytes after the word itself");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_long",
	    CTLFLAG_RD, &sc->dp_rx_long, 0,
	    "length words that no packet could have - above 65,535, or enough to span the ring. "
	    "One buffer is consumed and nothing is read from it. It should not move: a ring whose "
	    "read index has come to rest inside a packet reads payload where a length belongs");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_span_frame",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_rx_span_frame, "A",
	    "the first 128 bytes of the last packet that used more than one buffer, from the "
	    "start of its first buffer, with its ring, its tag and its length word");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_behind",
	    CTLFLAG_RD, &sc->dp_oq_behind, 0,
	    "packets taken from a ring whose count no longer held them: its next buffer had been "
	    "written, and its count had read zero, for two ticks with nothing taken in between. "
	    "As many are taken as were there when that wait began, in order, and handed up late, "
	    "not lost; nothing is acknowledged for them. It should not move - a ring gets there "
	    "only when a count and its buffers disagree");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_behind_runs",
	    CTLFLAG_RD, &sc->dp_oq_behind_runs, 0,
	    "the passes that took them");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_lose",
	    CTLFLAG_RW, &sc->dp_oq_lose, 0,
	    "an instrument, and it breaks a ring on purpose: acknowledge this many packets on "
	    "ring oq_lose_ring without taking them, one for each pass that finds a count. Each "
	    "leaves that ring one buffer behind the block, which is the fault oq_behind exists to "
	    "undo and the only way left to make it. Counts down to zero as it is spent");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_lose_ring",
	    CTLFLAG_RW, &sc->dp_oq_lose_ring, 0,
	    "the ring oq_lose is spent on");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_lose_burst",
	    CTLFLAG_RW, &sc->dp_oq_lose_burst, 0,
	    "how many of oq_lose one pass may spend, at most what it counted; 0 is one. One a pass "
	    "loses no faster than the way back recovers, so a ring is put far behind only with a burst");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "auto",
	    CTLFLAG_RW, &sc->dp_auto, 0,
	    "make a flow every second without being asked, from whatever the last punted frame was. "
	    "Off, and it should stay off until accelerate has been run by hand for a while: every "
	    "piece of it is tested and the combination is not, and a combination that is wrong is "
	    "wrong on every connection at once");
	sc->dp_flow_timeout = OCTEP_FLOW_AUTO_TIMEOUT;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_timeout",
	    CTLFLAG_RW, &sc->dp_flow_timeout, 0,
	    "seconds a microflow this driver makes may stay idle before the fast path expires it "
	    "(the mflow_timeout word of every FLOW_CREATE_FP and MFLOW_PROGRAM from here on; flows "
	    "already made keep theirs). Measured at 60: both microflows of a data-carrying connection "
	    "went fw_valid 0 between 53 and 63 s of silence. 0 means the default, 60");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "accel_dir",
	    CTLFLAG_RW, &sc->dp_accel_dir, 0,
	    "which direction of a connection may be accelerated: 0 either, 1 only the original "
	    "(opener to responder), 2 only the reply. An instrument for telling the two halves' "
	    "faults apart, not a setting to leave on");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "accel_half",
	    CTLFLAG_RW, &sc->dp_accel_half, 0,
	    "allow a connection to be programmed with only the direction in hand when the other "
	    "has not been punted yet. Off: it waits for both, because a half-offloaded connection "
	    "is handed back by the fast path within a few frames. An instrument, not a setting");
	sc->dp_fast = 1;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fast",
	    CTLFLAG_RW, &sc->dp_fast, 0,
	    "a punted frame that belongs to a connection in the flow table is acted on at once, "
	    "from the receive path, instead of at the next one-second poll: the connection the "
	    "fast path gave back, or the direction whose microflow it expired. 1 by default; 0 "
	    "leaves it all to the poll, which is how the difference is measured");
	sc->dp_revive = 1;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "revive",
	    CTLFLAG_RW, &sc->dp_revive, 0,
	    "a connection the fast path gave back for its retransmission rule is put back in "
	    "service by rewriting its entry - one command - instead of being taken out and made "
	    "again. 1 by default; 0 takes it out as before");
	sc->dp_revive_hold = 0;
	if (sc->dp_hold == NULL)
		sc->dp_hold = malloc(sizeof(*sc->dp_hold) * OCTEP_HOLD_MAX, M_DEVBUF, M_NOWAIT | M_ZERO);
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "revive_hold",
	    CTLFLAG_RW, &sc->dp_revive_hold, 0,
	    "1: a connection revived four times in four seconds and given back again is taken out "
	    "and left with the host for a wait - a second, doubling while it keeps coming back, to "
	    "64 s - as the XGS 136 does (issue 544). 0 by default: revived every time");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "revive_held",
	    CTLFLAG_RD, &sc->dp_revive_held, 0, "connections taken out at the end of a revival streak");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hold_skipped",
	    CTLFLAG_RD, &sc->dp_hold_skipped, 0, "makes not tried while a connection's wait ran");
	sc->dp_fast_make = 1;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fast_make",
	    CTLFLAG_RW, &sc->dp_fast_make, 0,
	    "a connection is made between polls, when one of its directions has had a run of "
	    "frames punted, instead of at the next one-second poll: TCP and UDP, at most "
	    "four a run and sixty-four a second. 1 by default; 0 leaves the making to the poll, "
	    "which is how the difference is measured. Needs dp.fast");
	sc->dp_fast_make_log2 = OCTEP_FAST_MAKE_LOG2;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fast_make_log2",
	    CTLFLAG_RW, &sc->dp_fast_make_log2, 0,
	    "how many frames one tuple must have had punted since it took its place in the "
	    "candidate table before that is asked for, "
	    "as a power of two: 4 is sixteen. It is asked again each time the count doubles. An "
	    "instrument");
	{
		int want = OCTEP_DP_TXQ_MAX;

		TUNABLE_INT_FETCH("hw.octep.txq", &want);
		sc->dp_txq_want = want < 0 ? 0 : (uint32_t)want;
	}
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ntxq",
	    CTLFLAG_RD, &sc->dp_ntxq, 0,
	    "transmit rings in use besides ring 0, each with its own lock; hw.octep.txq at the "
	    "loader asks for fewer, 0 for ring 0 alone");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "txq",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, octep_sysctl_dp_txq, "A",
	    "each transmit ring: its number, frames posted, frames refused for a full ring");
	sc->dp_fast_make_per_sec = OCTEP_FAST_MAKE_PER_SEC;
	sc->dp_fast_make_per_run = OCTEP_FAST_MAKE_PER_RUN;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fast_make_per_sec",
	    CTLFLAG_RW, &sc->dp_fast_make_per_sec, 0,
	    "connections the runs between two polls may make: each holds the transmit path's lock "
	    "for three commands, 62 us measured, so 8000 is half a second of it");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fast_make_per_run",
	    CTLFLAG_RW, &sc->dp_fast_make_per_run, 0,
	    "connections one run may try to make: 16 is about a millisecond of the lock in one go");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fast_made",
	    CTLFLAG_RD, &sc->dp_fast_made, 0, "connections made by a run, between polls");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fast_make_tries",
	    CTLFLAG_RD, &sc->dp_fast_make_tries, 0,
	    "attempts runs have spent on a connection the table did not hold");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fast_make_refused",
	    CTLFLAG_RD, &sc->dp_fast_make_refused, 0,
	    "attempts by a run that made nothing; the candidate's slot - every tuple in it - is "
	    "then left to the poll until the poll has been round");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fast_make_spent",
	    CTLFLAG_RD, &sc->dp_fast_make_spent, 0,
	    "times a run passed over a candidate that had been asked for because the run's or "
	    "the second's budget was spent. Once a run, so one asking can be counted many times");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_nostate",
	    CTLFLAG_RD, &sc->dp_flow_nostate, 0,
	    "attempts to make a connection that found no pf state for the frame's tuple");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_wait_other",
	    CTLFLAG_RD, &sc->dp_flow_wait_other, 0,
	    "attempts put off for want of the other direction: no place of its slot held a "
	    "candidate for it that could be used - never punted, made room for another tuple, "
	    "older than dp.cand_age allows, unusable as it stood, or the slot's word stayed "
	    "taken. A connection that never leaves this has its other direction arriving where "
	    "this driver does not see it, or shares its slot with more busy tuples than the "
	    "slot has places: dp.cand_table shows the slot");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_nonhop",
	    CTLFLAG_RD, &sc->dp_flow_nonhop, 0,
	    "attempts that found no next hop for the frame's direction");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_v6_refused",
	    CTLFLAG_RD, &sc->dp_flow_v6_refused, 0,
	    "IPv6 connections left to the host: translated, or covered by an IPsec policy");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_other_closing",
	    CTLFLAG_RD, &sc->dp_flow_other_closing, 0,
	    "attempts put off because the other direction had carried a FIN or RST not yet read");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fast_kicks",
	    CTLFLAG_RD, &sc->dp_fast_kicks, 0, "times the receive path asked for a run");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fast_runs",
	    CTLFLAG_RD, &sc->dp_fast_runs, 0, "runs it got");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_revived",
	    CTLFLAG_RD, &sc->dp_flow_revived, 0,
	    "connections found given back by the retransmission rule and rewritten in place");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "revive_refused",
	    CTLFLAG_RD, &sc->dp_revive_refused, 0,
	    "rewrites the far side answered with a refusal; the connection was then taken out");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_closing",
	    CTLFLAG_RD, &sc->dp_flow_closing, 0,
	    "connections found given back after a frame of theirs with FIN or RST had been "
	    "handed up: ending, and taken out");
	sc->dp_pf_sloppy = 1;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "pf_sloppy",
	    CTLFLAG_RW, &sc->dp_pf_sloppy, 0,
	    "mark pf's states for an accelerated connection sloppy, so pf stops judging TCP "
	    "sequence numbers it can no longer see advance. 1 by default; 0 leaves the states as "
	    "they are, which is how the flag's own effect is measured");
	sc->dp_tcp_window = 1;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tcp_window",
	    CTLFLAG_RW, &sc->dp_tcp_window, 0,
	    "1 (the default): the fast path checks an offloaded TCP connection's window, filled from "
	    "pf's numbers when it is made, and pf takes the fast path's numbers back when the "
	    "connection is taken back, its sloppy mark cleared. 0: sent with the check off and left "
	    "sloppy in pf, as before (issue #679)");
	sc->dp_tcp_revive = 0;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tcp_revive",
	    CTLFLAG_RW, &sc->dp_tcp_revive, 0,
	    "how a revival treats a connection whose window is checked: 0 (the default) sends it "
	    "back unchecked, as every revival did before; 2 sends it back with a window made from "
	    "the latest segment the host saw each way, and makes a connection whose pf numbers have "
	    "stopped with one too; 1 reclaims it for the fast path's window and sends that back. 1 "
	    "and 2 are instruments: both measured to drop ordinary segments (issue #679)");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tcp_from_segs",
	    CTLFLAG_RD, &sc->dp_tcp_from_segs, 0,
	    "connections made or revived with a window made from the latest segment the host saw "
	    "in each direction, pf's numbers having stopped");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tcp_seg_waits",
	    CTLFLAG_RD, &sc->dp_tcp_seg_waits, 0,
	    "revivals of a checked connection that waited for a segment of each direction first");
	SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tcp_segs_last",
	    CTLFLAG_RD, sc->dp_tcp_segs_last, sizeof(sc->dp_tcp_segs_last),
	    "the last window made from the host's latest segments, or why one could not be");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tcp_revived",
	    CTLFLAG_RD, &sc->dp_tcp_revived, 0,
	    "connections made with the check and revived without it: dp.tcp_revive 0, or with 2 "
	    "no segment of one of its directions to make the window from");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tcp_checked",
	    CTLFLAG_RD, &sc->dp_tcp_checked, 0,
	    "TCP connections made with their window checked in hardware");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tcp_unchecked",
	    CTLFLAG_RD, &sc->dp_tcp_unchecked, 0,
	    "TCP connections made without the check: a rule asked for keep state (sloppy), pf's "
	    "numbers had stopped when the connection was in hardware before (with tcp_revive 2: and "
	    "the host had no segment of each direction to make the window from), a tunnel's "
	    "connection, or tcp_window is 0");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tcp_rewrites",
	    CTLFLAG_RD, &sc->dp_tcp_rewrites, 0,
	    "TCP connections not made because pf modulates their sequence numbers or timestamps");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tcp_handback",
	    CTLFLAG_RD, &sc->dp_tcp_handback, 0,
	    "take-backs whose window numbers pf took, its sloppy mark cleared");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tcp_kept",
	    CTLFLAG_RD, &sc->dp_tcp_kept, 0,
	    "take-backs of a checked connection left sloppy in pf: given back by the fast path "
	    "already, closing, or its numbers did not look like a window");
	SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tcp_last",
	    CTLFLAG_RD, sc->dp_tcp_last, sizeof(sc->dp_tcp_last),
	    "what the last take-back of a checked connection did with its window, and why");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tcp_lost",
	    CTLFLAG_RD, &sc->dp_tcp_lost, 0,
	    "take-backs left sloppy because the queue to pf was full");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "auto_made",
	    CTLFLAG_RD, &sc->dp_auto_made, 0, "flows programmed without being asked");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "auto_gone",
	    CTLFLAG_RD, &sc->dp_auto_gone, 0,
	    "flows taken out of MF_ACTIVE because pf no longer had their state");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "auto_full",
	    CTLFLAG_RD, &sc->dp_auto_full, 0, "times the flow table had no room");
	sc->dp_keepalive = 1;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "keepalive",
	    CTLFLAG_RW, &sc->dp_keepalive, 0,
	    "keep pf's states alive for connections the coprocessor is still forwarding: every five "
	    "seconds each connection's microflows are read back, and when they have been used the "
	    "states are restamped as the packets pf did not see would have restamped them. 1 by "
	    "default. With 0 a state runs out under a live connection - a UDP flow is taken out "
	    "about once a minute and comes back with another translated port, a TCP connection "
	    "that stays busy for a day stops - which is how the difference is measured");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ka_reads",
	    CTLFLAG_RD, &sc->dp_ka_reads, 0, "microflows read back by the keep-alive");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ka_touched",
	    CTLFLAG_RD, &sc->dp_ka_touched, 0,
	    "times a connection was found in use by the coprocessor and pf's states for it were "
	    "restamped");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ka_idle",
	    CTLFLAG_RD, &sc->dp_ka_idle, 0,
	    "times a connection was found quiet, or its microflows gone: left to pf's own timer");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_unsettled",
	    CTLFLAG_RD, &sc->dp_flow_unsettled, 0,
	    "attempts to make a connection that were put off because pf's states for it were not "
	    "on their long timer: a TCP handshake not through or a connection closing, a UDP flow "
	    "of which one side has been seen only once. One per attempt, not per connection");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_pushed",
	    CTLFLAG_RD, &sc->dp_cand_pushed, 0,
	    "punted frames written to the candidate table: every punted IPv4 frame that carries a "
	    "flow identity, less the clashes");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_taken",
	    CTLFLAG_RD, &sc->dp_cand_taken, 0, "candidates the poll turned into flows");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_known",
	    CTLFLAG_RD, &sc->dp_cand_known, 0,
	    "candidates whose flow the table already had, dropped without a command");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_clash",
	    CTLFLAG_RD, &sc->dp_cand_clash, 0,
	    "writers that found a slot's word taken and gave up: another ring, the drain, the "
	    "reader of the other direction or dp.cand_table was inside. It costs that frame's "
	    "candidate, and its kick or its FIN mark if it had one");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_lost",
	    CTLFLAG_RD, &sc->dp_cand_lost, 0,
	    "candidates overwritten before the poll had read them, by a frame of their own "
	    "tuple or of another. Those by their own tuple - every frame after the first since "
	    "the candidate was last read - are this less cand_evicted");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_evicted",
	    CTLFLAG_RD, &sc->dp_cand_evicted, 0,
	    "candidates overwritten before the poll had read them by a frame of ANOTHER tuple: "
	    "every place of the slot was taken and this one was written longest ago, to the "
	    "tick - more busy tuples in a slot than it has places, inside one poll. It does not "
	    "see a candidate the poll had already read making room, which can still cost a "
	    "connection its other direction; the sign of that is flow_wait_other climbing while "
	    "this stands still");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_kept",
	    CTLFLAG_RD, &sc->dp_cand_kept, 0,
	    "slots of the candidate table that were not emptied when the flows were discarded, "
	    "because their word stayed taken");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_age",
	    CTLFLAG_RW, &sc->dp_cand_age, 0,
	    "seconds a candidate may be old and still stand for the other direction of a "
	    "connection that is being made; 0 for no limit. The identity in it is the fast "
	    "path's to expire, and a place keeps its tuple until the slot needs the room");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_aged",
	    CTLFLAG_RD, &sc->dp_cand_aged, 0,
	    "times a connection was not made because the other direction's candidate was there "
	    "and older than dp.cand_age allows");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fast_scans",
	    CTLFLAG_RD, &sc->dp_fast_scans, 0,
	    "times a run between polls searched the table for a candidate nobody had asked "
	    "about and found no connection: the hot table's bucket was another connection's");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_table",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_cand_table, "A",
	    "the candidate table: every place that has been written to - its slot and place, "
	    "the tuple, how long ago its last frame came, whether it has been taken by the poll "
	    "or by a run, its count of frames, and the microflow identity the frame carried. A "
	    "slot whose word stayed taken prints as busy");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_forgot",
	    CTLFLAG_RD, &sc->dp_flow_forgot, 0,
	    "entries dropped from this table because a ruleset reload discarded the whole of the "
	    "far side's - see rpc.fw_rev_bump");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flows",
	    CTLFLAG_RD, &sc->dp_conn_used, 0,
	    "connections currently accelerated, each with one or two directions in hardware");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_max",
	    CTLFLAG_RD, &sc->dp_conn_max, 0,
	    "how many connections this table can hold: the host's bound (hw.octep.flow_max, else 2000000 "
	    "or what a 16th of the memory holds), capped by the board's");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhops",
	    CTLFLAG_RD, &sc->dp_nhop_used, 0, "next hops currently programmed");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_max",
	    CTLFLAG_RD, &sc->dp_nhop_max, 0,
	    "how many next hops this table can hold: the host's bound, capped by the board's");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_attached",
	    CTLFLAG_RD, &sc->dp_flow_attached, 0,
	    "second directions attached to a connection already in hardware");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_reclaimed",
	    CTLFLAG_RD, &sc->dp_flow_reclaimed, 0,
	    "connections the far side agreed to reclaim when they were taken out");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_pending",
	    CTLFLAG_RD, &sc->dp_flow_pending, 0,
	    "connections found RECLAIM_PENDING on the far side and taken out for it");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "probe_valid",
	    CTLFLAG_RD, &sc->dp_probe_valid, 0,
	    "probes that found the connection VALID with this host's revision - punts that meant nothing");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "probe_timeout",
	    CTLFLAG_RD, &sc->dp_probe_timeout, 0, "probes the far side did not answer; nothing done");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "probe_read_err",
	    CTLFLAG_RD, &sc->dp_probe_read_err, 0,
	    "connections taken out because the read came back unusable (short, or another index)");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "probe_rev_mismatch",
	    CTLFLAG_RD, &sc->dp_probe_rev_mismatch, 0,
	    "connections taken out because the far side holds another revision for the index");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "probe_state_other",
	    CTLFLAG_RD, &sc->dp_probe_state_other, 0,
	    "connections taken out because the far side reads them INVALID or RECLAIMED");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "reclaim_refused",
	    CTLFLAG_RD, &sc->dp_reclaim_refused, 0,
	    "CONN_RECLAIM_FP the far side answered with an error: it had already left RECLAIM_PENDING, "
	    "or holds another revision");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_tuple_mismatch",
	    CTLFLAG_RD, &sc->dp_flow_tuple_mismatch, 0,
	    "frames whose tuple was not what pf's state implied for their direction. Zero on a "
	    "plain connection; anything else falsifies the derivation and names the connection to "
	    "read");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_rc_refused",
	    CTLFLAG_RD, &sc->dp_flow_rc_refused, 0,
	    "programming requests the far side answered with a non-zero rc; the entry was freed");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_shared",
	    CTLFLAG_RD, &sc->dp_nhop_shared, 0,
	    "next hops a connection found already programmed and shared");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_full",
	    CTLFLAG_RD, &sc->dp_nhop_full, 0, "times the next-hop table had no room");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_exclude",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_flow_exclude, "A",
	    "addresses, networks and ports never offloaded, as scripts/xgs/flow_exclude.php writes the "
	    "owner's list: a4:<8 hex digits>/<length>, a6:<32 hex digits>/<length> and p:<port>, "
	    "separated by spaces. A list that does not parse is refused whole; one that does takes out "
	    "at once what it names. Empty is none");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_release",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_flow_release, "A",
	    "write a connection's index (dp.flow_table's first column) to hand it back to the host and "
	    "keep it there until pf lets its state go; reading shows what the last write did");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_verdict",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_flow_verdict, "A",
	    "write \"<index> <verdict>\" to change a connection's verdict on the coprocessor: 0 hands "
	    "its frames to the inspector's virtual function, 1 drops them there, 2 forwards them "
	    "again - only for a connection made with mflow_action 2 and the fast path's IPS switch "
	    "on. Reading shows what the last write did");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "live_uid",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, octep_sysctl_dp_live_uid, "A",
	    "an instrument (discovery 5): write \"add <user>\" or \"del <user>\" to set or clear a "
	    "user's bit in the coprocessor's table of users logged in; reading shows the last result");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_luid",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, octep_sysctl_dp_conn_luid, "A",
	    "an instrument (discovery 5): write \"<index> <user>\" to set a connection's live user in "
	    "place, the entry read back after; reading shows the last result");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_qos",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, octep_sysctl_dp_conn_qos, "A",
	    "an instrument (discovery 6): write \"<index> <priority 0|1> [<valid 0|1> [<meter> "
	    "[<directions 1|2|3>]]]\" to set a connection's QoS words in place, read back after");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "luid_table",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, octep_sysctl_dp_luid_table, "A",
	    "the users 0 to 1023 whose bit is set in the coprocessor's table, read back");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "live_users",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, octep_sysctl_dp_live_users, "A",
	    "the Captive Portal's clients as live users, a4:<hex>=<user> ... (scripts/xgs/cp_users.py)");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_action",
	    CTLFLAG_RW, &sc->dp_mflow_action, 0,
	    "the action plain connections are made with from now on: 2 IPS, which has the fast path "
	    "ask each connection's verdict for every frame, so a cut can drop it there; anything else "
	    "FWD, which never asks. XGS: Settings \"Cut in hardware\" sets it, at a save and at boot");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_dscp_on",
	    CTLFLAG_RW, &sc->dp_mflow_dscp_on, 0,
	    "an instrument (discovery 8): 1 makes connections from now on with dp.mflow_dscp in both "
	    "microflows' DSCP override, which the fast path writes into every frame, in place of "
	    "pf's own mark; 0, the default, writes pf's mark where a set-tos rule made one");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_dscp",
	    CTLFLAG_RW, &sc->dp_mflow_dscp, 0,
	    "the microflows' dscp_override_val, 8 bits, written while dp.mflow_dscp_on is 1");
	sc->dp_dscp_pf = 1;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "dscp_pf",
	    CTLFLAG_RW, &sc->dp_dscp_pf, 0,
	    "1 (the default): connections are made with pf's DSCP mark - a set-tos rule, OPNsense's "
	    "normalization \"DSCP\" - for the fast path to write into every frame as pf would. 0: "
	    "made without it, their frames leaving as they came, as before (discovery 8) - which is "
	    "how the mark's effect is measured");
	TASK_INIT(&sc->dp_closing_task, 0, octep_dp_closing_task, sc);
	mbufq_init(&sc->dp_closing_q, OCTEP_CLOSING_QLEN);
	sc->dp_closing_check = 1;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "closing_check",
	    CTLFLAG_RW, &sc->dp_closing_check, 0,
	    "1 (the default): a FIN or a RST of a connection in hardware whose window is checked is "
	    "held until pf has the fast path's numbers, and pf judges it - a blind reset is refused "
	    "as pf refuses it for a connection it follows (issue #679). 0: handed up at once, to a "
	    "sloppy state that takes it at its word");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "closing_checked",
	    CTLFLAG_RD, &sc->dp_closing_checked, 0,
	    "FINs and RSTs pf judged with the fast path's numbers");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "closing_passed",
	    CTLFLAG_RD, &sc->dp_closing_passed, 0,
	    "FINs and RSTs of such a connection handed up without them - the queue full, or the "
	    "numbers not had");
	sc->dp_vlan_flows = 1;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "vlan_flows",
	    CTLFLAG_RW, &sc->dp_vlan_flows, 0,
	    "1 (the default): each VLAN subinterface of a front port is given a logical interface on "
	    "the fast path, and a connection routed by one is made in hardware, its frames tagged and "
	    "untagged by the fast path from its next hops (discovery 10). 0: every VLAN's logical "
	    "interface switched off and every such connection left to the host, as before");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_vlan",
	    CTLFLAG_RD, &sc->dp_flow_vlan, 0,
	    "connections made with a VLAN in a direction - arriving tagged, or leaving by a VLAN");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "vlan_lif_table",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, octep_sysctl_dp_vlan_lif_table, "A",
	    "the VLAN subinterfaces of the front ports and their logical interfaces on the fast path");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "vlan_lif_added",
	    CTLFLAG_RD, &sc->dp_vlan_lif_added, 0, "logical interfaces made for a VLAN subinterface");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "vlan_lif_repaired",
	    CTLFLAG_RD, &sc->dp_vlan_lif_repaired, 0,
	    "... made again: the periodic full add found the fast path had lost one");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "vlan_lif_disabled",
	    CTLFLAG_RD, &sc->dp_vlan_lif_disabled, 0, "... switched off: their VLAN interface went");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "vlan_lif_failed",
	    CTLFLAG_RD, &sc->dp_vlan_lif_failed, 0, "adds and updates the far side refused");
	sc->dp_bridged = 1;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "bridge_flows",
	    CTLFLAG_RW, &sc->dp_bridged, 0,
	    "1 (the default): a connection a bridge carries - addressed to another machine, its two "
	    "directions on two ports of one bridge - is made a bridged flow: each direction out of the "
	    "port the other comes in on, its frames' MACs and TTL left as they are (discovery 9, issue "
	    "723). 0: every such connection stays with the host, whose bridge carries it");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_bridged",
	    CTLFLAG_RD, &sc->dp_flow_bridged, 0,
	    "connections made as bridged flows");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_bridged_kept",
	    CTLFLAG_RD, &sc->dp_flow_bridged_kept, 0,
	    "times a bridged connection was left to the host's bridge - dp.bridge_flows 0, a group "
	    "address, a translation or a tunnel, or its two directions not on two ports of one bridge "
	    "- an attempt each, so one connection's frames count more than once");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_dscp",
	    CTLFLAG_RD, &sc->dp_flow_dscp, 0,
	    "connections made with pf's DSCP mark in their microflows, for the fast path to write "
	    "into every frame as pf would");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_dscp_kept",
	    CTLFLAG_RD, &sc->dp_flow_dscp_kept, 0,
	    "times a connection was not made because of pf's DSCP mark - an IPv6 or tunnelled one, or "
	    "a mark that sets the ECN bits as well - an attempt each, so one connection's punted "
	    "frames count more than once");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_dropped",
	    CTLFLAG_RD, &sc->dp_flow_dropped, 0,
	    "connections dropped on the coprocessor by their verdict (flow_verdict 1)");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "drop_held",
	    CTLFLAG_RD, &sc->dp_drop_held, 0,
	    "sweep passes that kept a dropped connection pf no longer holds, its frames still coming");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_excluded",
	    CTLFLAG_RD, &sc->dp_flow_excluded, 0,
	    "times a connection was not made, or was taken out, because the owner keeps it with the "
	    "host - an attempt each, so one connection's punted frames count more than once");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_released",
	    CTLFLAG_RD, &sc->dp_flow_released, 0,
	    "connections handed back to the host one at a time, from the Flows page");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_table",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_flow_table, "A",
	    "every accelerated connection: its index and revision, and per direction its ingress "
	    "tuple, the microflow slot and revision the fast path gave it, whether it is active, its "
	    "next hop, the front port its frames arrive on and how many were handed back");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_table",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_nhop_table, "A",
	    "every next hop programmed: index, revision, how many directions name it, the egress "
	    "interface, the neighbour's address, ours, and the MTU");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "accelerate",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_accelerate, "A",
	    "read this to accelerate the flow of the last punted frame: the slot from its metadata, "
	    "the verdict and the translation from pf, the next hop from the host's own route and "
	    "ARP. Reports what it did or why it could not. A read with side effects, deliberately, "
	    "and one read programs ONE direction: a connection offloaded in one direction only is "
	    "reclaimed by the fast path within a few frames, so read it twice back to back - once "
	    "the first direction is in hardware, the frame being punted is the other one");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "pf_state",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_pf_state, "A",
	    "what pf says about the last punted frame's five-tuple. Read-only, and the host half of "
	    "a flow offload rests on it: a state printed here for the slot dp.rx_prefix is showing "
	    "means everything needed to program that flow is in hand when the frame arrives. Says "
	    "which index order matched rather than assuming one");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_resync",
	    CTLFLAG_RD, &sc->dp_rx_resync, 0,
	    "times a ring's read index was moved past buffers the block will never fill");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_skipped",
	    CTLFLAG_RD, &sc->dp_rx_skipped, 0,
	    "empty buffers stepped over by those moves");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "credit_capped",
	    CTLFLAG_RD, &sc->dp_credit_capped, 0,
	    "service passes whose credit was cut to keep a ring at or under its grant");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "msix",
	    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_msix, "IU",
	    "write 1 to allocate MSI-X and hook one vector per armed ring, 0 to release them. Ring "
	    "n is table entry 16 + n, which is the vendor's own arithmetic for this device id. The "
	    "interrupt levels have to be reachable for anything to fire - see refresh_levels");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "intr_taken",
	    CTLFLAG_RD, &sc->dp_intr_taken, 0,
	    "how many times a ring's MSI-X handler has run, across every hooked ring");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "intr_drained",
	    CTLFLAG_RD, &sc->dp_intr_drained, 0,
	    "passes beyond the first inside one hold of a ring that still took packets, by the "
	    "handler or by any other servicer - the "
	    "empty pass that ends the loop is not one of them, so four productive passes add "
	    "three. It climbs under load, because the block's write is a DMA and frames land "
	    "while the handler is running: a second pass that finds three more frames is an "
	    "ordinary one, not a burst the first could not drain. The stall this loop exists "
	    "for showed itself in R_OUT_CNTS left above the interrupt level, not here");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_declined",
	    CTLFLAG_RD, &sc->dp_oq_declined, 0,
	    "servicers that found a ring held by another, left it a note and went away");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_handed",
	    CTLFLAG_RD, &sc->dp_oq_handed, 0,
	    "times a servicer leaving a ring found such a note, left since its last pass began, "
	    "and went round again. An upper bound on the rings that would otherwise have waited "
	    "for the watchdog: the watchdog leaves one too, finding a handler at work on a ring "
	    "with nothing waiting");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_unseen",
	    CTLFLAG_RD, &sc->dp_oq_unseen, 0,
	    "passes that read a count above zero and found no buffer to take: a packet counted "
	    "before its buffer could be read, or a gap - see rx_resync. The watchdog is asked "
	    "for the next tick. Not counted with ack_cnts 0, when the register is never brought "
	    "back to zero and the last pass of every visit reads this way");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_bound",
	    CTLFLAG_RD, &sc->dp_oq_bound, 0,
	    "visits that took as many packets as one visit may - sixteen rings' worth for the "
	    "handler, one for the watchdog, the sysctl and each sweep of a resume - and stopped "
	    "there; the watchdog is asked for the next tick");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rxwd_rescues",
	    CTLFLAG_RD, &sc->dp_rxwd_rescues, 0,
	    "rings the watchdog found packets in on a visit that ring had not asked for. On a "
	    "loaded ring it finds some with nothing wrong - it can arrive between an interrupt "
	    "and its handler - so it is rxwd_stalls that says a ring was left");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rxwd_stalls",
	    CTLFLAG_RD, &sc->dp_rxwd_stalls, 0,
	    "... and whose first pass took thirty-two packets or more, or twice oq_intr_pkt if "
	    "that is more, with nobody inside the ring for the two ticks before: a ring that had "
	    "been left with packets in it. The number that should not move. A ring slower than "
	    "640 packets a second cannot reach it and shows only in rxwd_rescues; and a tick "
	    "that lands between a burst arriving on an idle ring and its handler still counts "
	    "one - one in 18 million frames, when it was measured");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rxwd_stall_log",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_stall_log, "A",
	    "the last eight stalls rxwd_stalls counted: how long ago, which ring, what the first "
	    "pass took, how many ticks nobody had been inside, what the visit took");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rxpeek_log",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 1,
	    octep_sysctl_dp_stall_log, "A",
	    "the last eight rings the look visited, as rxwd_stall_log prints a stall (#361)");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rxwd_runs",
	    CTLFLAG_RD, &sc->dp_rxwd_runs, 0,
	    "watchdog runs that took a packet from any ring, asked for or not. It was read as "
	    "interrupts missed; moving on every period under one stream, it was the watchdog "
	    "taking a ring from its handler. rxwd_rescues and rxwd_stalls say which");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rxwd_ticks",
	    CTLFLAG_RW, &sc->dp_rxwd_ticks, 0,
	    "the watchdog's period in ticks. A write takes effect when it next re-arms; 0 is "
	    "replaced by the default only when the ring comes up. A servicer that leaves work "
	    "behind asks for the next tick whatever this says");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "msix_count",
	    CTLFLAG_RD, &sc->dp_msix_count, 0, "MSI-X messages allocated");
	sc->dp_msix_spread = 1;
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "msix_spread",
	    CTLFLAG_RW, &sc->dp_msix_spread, 0,
	    "1: each ring's vector is bound to a CPU of its own when the vectors are hooked, as the "
	    "vendor's host does; 0 leaves them where the kernel puts them, which was cpu0 for all of "
	    "them, and a ring's handler waited behind the others' (#361). Read at dp.msix");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "msix_bound",
	    CTLFLAG_RD, &sc->dp_msix_bound, 0, "ring vectors bound to a CPU of their own");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "refresh_levels",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_refresh_levels, "I",
	    "write 1 to write R_OUT_INT_LEVELS on every armed ring from oq_time_threshold and "
	    "oq_intr_pkt. Nothing else about the rings is touched, so it is safe on a live one");
	sc->dp_ack_cnts = 1;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ack_cnts",
	    CTLFLAG_RW, &sc->dp_ack_cnts, 0,
	    "write the packet count back to R_OUT_CNTS when a ring is serviced. The vendor's host "
	    "driver keeps a shadow and subtracts, and writes the register perhaps never; the "
	    "register is free-running and its top bits are flags, so a bare count writes zeros "
	    "over them. 1 is what this driver has always done");
	/*
	 * Off by default. Measured 2026-10-13 (#361): an hour with it on had 5 stalls against 3
	 * without, so it is an instrument to switch on for a run, not a repair.
	 */
	sc->dp_resend = 0;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "resend",
	    CTLFLAG_RW, &sc->dp_resend, 0,
	    "write RESEND (R_OUT_CNTS bit 59) when a visit leaves a ring, so the block raises the "
	    "interrupt again if the count is at the level - the vendor host's end-of-poll write. "
	    "0 is what this driver did until #361: a burst landing on an idle ring between the "
	    "last read and the release waited for the watchdog");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "resend_writes",
	    CTLFLAG_RD, &sc->dp_resend_writes, 0, "how many times RESEND was written");
	/* Instruments for a run (#361, 2026-10-25); what they are set to by default is what was. */
	sc->dp_istat_clear = 1;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "istat_clear",
	    CTLFLAG_RW, &sc->dp_istat_clear, 0,
	    "1: each pass clears its ring's bit of R_OUT_INT_STATUS before it reads the count, which "
	    "this driver has always done. 0: the bit is left alone, as the vendor's host leaves it");
	sc->dp_look_wake = 0;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "look_wake",
	    CTLFLAG_RW, &sc->dp_look_wake, 0,
	    "what the look tries on a ring holding packets past the level with the count's interrupt "
	    "bit set, before it visits: 1 RESEND, 2 the ring's status bit cleared, 3 its MSI-X entry "
	    "masked and put back; then whether the handler ran is kept with the visit in "
	    "rxpeek_log. 0: nothing");
	sc->dp_look_wake_us = 50;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "look_wake_us",
	    CTLFLAG_RW, &sc->dp_look_wake_us, 0,
	    "how long, in microseconds, the look waits for the handler after its try (1 to 1000)");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "look_wakes",
	    CTLFLAG_RD, &sc->dp_look_wakes, 0, "tries the look made at a deaf ring");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "look_woke",
	    CTLFLAG_RD, &sc->dp_look_woke, 0, "... after which the ring's handler ran");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "msix_moved",
	    CTLFLAG_RD, &sc->dp_msix_moved, 0,
	    "watchdog and look visits that found a ring's MSI-X entry holding an address or data "
	    "other than the kernel's");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "istat_reset",
	    CTLFLAG_RD, &sc->dp_istat_reset, 0,
	    "passes that found the ring's bit of R_OUT_INT_STATUS set again on the read right after "
	    "clearing it: a packet landing as the clear went out (#361)");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "istat_others",
	    CTLFLAG_RD, &sc->dp_istat_others, 0,
	    "passes that found other rings' bits in R_OUT_INT_STATUS, which holds a bit for every "
	    "ring, and left them: every pass used to clear them all (#361)");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_left_up",
	    CTLFLAG_RD, &sc->dp_oq_left_up, 0,
	    "visits that left a ring with nothing in it and its R_OUT_INT_STATUS still set right after "
	    "the last pass's clear (#361); common, and not by itself a ring left");
	sc->dp_rxpeek_ticks = 1;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rxpeek_ticks",
	    CTLFLAG_RW, &sc->dp_rxpeek_ticks, 0,
	    "ticks between the watchdog's looks at each ring's head buffer, between its sweeps: a ring "
	    "whose head has held the same packet for a look with nobody inside is visited - an "
	    "interrupt the block raised and the host never had, or a lone packet waiting for the time "
	    "threshold (#361). 0: no look, a sweep each period");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rxpeek_found",
	    CTLFLAG_RD, &sc->dp_rxpeek_found, 0,
	    "rings the look found holding a packet a whole look long with nobody inside, and visited");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rxpeek_taken",
	    CTLFLAG_RD, &sc->dp_rxpeek_taken, 0, "what those visits took");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rxwd_gap_max",
	    CTLFLAG_RW, &sc->dp_rxwd_gap_max, 0,
	    "the longest time between the starts of two runs of the receive watchdog, in ticks: its "
	    "period, plus how long the earlier run took - a sweep that takes a burst up the stack is "
	    "not short - or how late the callout came; write 0 to start again");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "credit_unit",
	    CTLFLAG_RD, &sc->dp_credit_unit, 0,
	    "doorbell units returned for each buffer taken: 1, which is what the block spends on "
	    "one. It was a setting, and 16, with the credit cut back to a ceiling of sixteen "
	    "rings - credit for the block to write round a full ring. It only reports now");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_lapped",
	    CTLFLAG_RD, &sc->dp_oq_lapped, 0,
	    "passes that read a count larger than the ring: the block had written over buffers "
	    "the host had not taken. With the grant derived and oq_wmark what the coprocessor "
	    "holds it should not move; see rx_resync for what follows when it does. Not counted "
	    "with ack_cnts 0, when the count only ever grows");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_short",
	    CTLFLAG_RD, &sc->dp_rx_short, 0,
	    "buffers the read index was stepped over that held something - a length too short "
	    "to be a frame. Each is re-poisoned and its credit returned");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmsg_type",
	    CTLFLAG_RW, &sc->dp_cmsg_type, 0,
	    "the control message type; 7 is the port speed notification, the one the host sends");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmsg_count",
	    CTLFLAG_RW, &sc->dp_cmsg_count, 0, "how many entries, at most 16");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmsg_port",
	    CTLFLAG_RW, &sc->dp_cmsg_port, 0, "the port tag in every entry");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmsg_value",
	    CTLFLAG_RW, &sc->dp_cmsg_value, 0, "the value in every entry; a speed, for type 7");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmsg_post",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_cmsg_post, "I", "write 1 to post the control message");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sport",
	    CTLFLAG_RW, &sc->dp_sport, 0,
	    "the UDP source port, which is part of the tuple the far side hashes to pick one of "
	    "its eight slow-path queues. 0 keeps the fixed 9 every earlier run used - see octep.h");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "meta_b0",
	    CTLFLAG_RW, &sc->dp_meta_b0, 0,
	    "in meta mode 3, the value of the metadata type byte; 0 means use the vendor's 1");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_if",
	    CTLFLAG_RW, &sc->dp_sa_if, 0,
	    "which interface index asks for encryption, or -1 for none. Default -1");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_idx",
	    CTLFLAG_RW, &sc->dp_sa_idx, 0,
	    "the association handle that interface names, or 0 for none. It must be an index rpc.sa_idx has already installed");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "meta_tpl",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_meta_tpl, "A",
	    "the 64 metadata bytes as hex, empty for the default. Written over the cleared block before the association handle");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmd",
	    CTLFLAG_RW, &sc->dp_cmd, 0,
	    "the control message to send: 4 is RX_CTL, which is what the vendor sends from its "
	    "interface open and stop handlers");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmd_p1",
	    CTLFLAG_RW, &sc->dp_cmd_p1, 0, "param1; for RX_CTL the interface index");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmd_p2",
	    CTLFLAG_RW, &sc->dp_cmd_p2, 0, "param2; for RX_CTL 1 starts and 0 stops");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmd_p3",
	    CTLFLAG_RW, &sc->dp_cmd_p3, 0, "param3, unused by RX_CTL");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmd_more",
	    CTLFLAG_RW, &sc->dp_cmd_more, 0,
	    "how many extra eight-byte words follow the command word; zero for RX_CTL");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmd_fsz",
	    CTLFLAG_RW, &sc->dp_cmd_fsz, 0,
	    "16 builds the instruction the way the vendor builds a control packet, with no PKI "
	    "header; anything else uses 28, the same front size a data packet uses here");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmd_post",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_cmd_post, "I", "write 1 to post the control message");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "peek_off",
	    CTLFLAG_RW, &sc->dp_peek_off, 0,
	    "the register offset dp.peek reads, inside this ring's own block only - see octep.h "
	    "for why it is bounded");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "peek",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_peek, "A",
	    "read peek_off and print it as hex, as its low 32 bits, and as a list of set bits");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "state",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_state, "A", "the ring's registers, read fresh");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "start",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_start, "I", "allocate the rings and program them");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "dst_mac",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_dst_mac, "A",
	    "the test frame's destination. All zeros means broadcast, which is what every earlier "
	    "measurement used and which the fast path counts as a class of its own");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "meta",
	    CTLFLAG_RW, &sc->dp_meta_mode, 0,
	    "what goes in the 64 metadata bytes: 0 the walking pattern from 0xc0, which the GPL pport "
	    "driver writes only into the bytes the vendor's hook did not claim; 1 the sample "
	    "application's signature 0xa0a1a2a3a4a5a6a7 with the rest zero; 2 all zeros; 3 what the "
	    "hook itself writes, which is byte 0 set to 1 and the rest zero. 3 is the default and is "
	    "the only one measured to reach a wire; the others route the frame to encryption and it "
	    "is dropped - see octep.h");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "port_tag",
	    CTLFLAG_RW, &sc->dp_port_tag, 0,
	    "the 2-byte port tag prepended to every frame: 0x0001 and 0x0002 are the two 10G MACs");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "dport",
	    CTLFLAG_RW, &sc->dp_dport, 0,
	    "irh.dport, the egress port the far side should use: 0 the switch uplink, "
	    "1 and 2 the two coprocessor MACs. Zero is not neutral - see octep.h");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "pkind",
	    CTLFLAG_RW, &sc->dp_pkind, 0,
	    "the PKIND the coprocessor assigned; 40 + num_vfs");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ips_loop",
	    CTLFLAG_RW, &sc->dp_ips_loop, 0,
	    "1 hands every frame the fast path sends the inspector straight back, as an inspector "
	    "that passes everything; 0 drops them (discovery 4, with the inspector's function)");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ips_pkind",
	    CTLFLAG_RW, &sc->dp_ips_pkind, 0,
	    "the PKIND a frame handed back from the inspector is posted with");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ips_rx",
	    CTLFLAG_RD, &sc->dp_ips_rx, 0, "frames the fast path sent the inspector");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ips_back",
	    CTLFLAG_RD, &sc->dp_ips_back, 0, "of those, handed back to the fast path");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ips_dropped",
	    CTLFLAG_RD, &sc->dp_ips_dropped, 0, "of those, dropped here");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ips_up",
	    CTLFLAG_RD, &sc->dp_ips_up, 0, "of those, given to oxips0 for an inspector reading it");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ips_passed",
	    CTLFLAG_RD, &sc->dp_ips_passed, 0, "frames the inspector let through oxips0, handed back");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ips_nomd",
	    CTLFLAG_RD, &sc->dp_ips_nomd, 0,
	    "frames let through with no metadata kept for their connection, dropped");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ips_unkeyed",
	    CTLFLAG_RD, &sc->dp_ips_unkeyed, 0, "frames that were not IP, so had no key, dropped");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ips_new",
	    CTLFLAG_RW, &sc->dp_ips_new, 0,
	    "1 makes new connections inspected - every frame to whatever reads oxips0 until it has "
	    "let ips_depth bytes through - while something reads it; 0 makes them forward");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ips_depth",
	    CTLFLAG_RW, &sc->dp_ips_depth, 0,
	    "bytes of a connection the inspector lets through before it is set to forward; "
	    "0 inspects it for good");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ips_made",
	    CTLFLAG_RD, &sc->dp_ips_made, 0, "connections made inspected");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ips_cut",
	    CTLFLAG_RD, &sc->dp_ips_cut, 0, "of those, set to forward once inspected");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_posted",
	    CTLFLAG_RD, &sc->dp_tx_posted, 0, "instructions this driver has posted");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_iq_full",
	    CTLFLAG_RD, &sc->dp_tx_iq_full, 0,
	    "frames refused because the input ring had no free slot. A reading above zero means "
	    "the coprocessor is consuming instructions more slowly than this host posts them, "
	    "which before the check was a silent overwrite of descriptors still in flight");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_quiesce",
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_rx_quiesce, "I",
	    "write 1 to stop every servicer and wait until none is inside a ring, 0 to resume. "
	    "This is the quiesce the detach path runs before it frees an interface, reachable on "
	    "its own because reaching it that way costs the twelve front ports. It stops "
	    "reception while it is set");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_busy",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_oq_busy, "A",
	    "the quiesce flag and one busy flag per ring. A sample on its own; read after "
	    "rx_quiesce=1 it is what shows the wait finished");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "service",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_service, "I",
	    "write 1 to take what has arrived on every armed ring: re-poison the buffers, hand "
	    "them back as credit, and acknowledge the packet count. Arming a ring is not serving "
	    "it, and without this exactly one packet ever fits");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_done",
	    CTLFLAG_RD, &sc->dp_rx_done, 0,
	    "packets acknowledged and whose buffers were returned, since the ring came up");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_seen",
	    CTLFLAG_RD, &sc->dp_rx_seen, 0,
	    "receive buffers the coprocessor had written, as of the last state read");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "xmit",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_xmit, "IU",
	    "write a frame length to post one test frame and ring the doorbell");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "stop",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_stop, "I", "disable the ring and release the memory");
}

/*
 * Step a ring's read index over a gap that will never be filled.
 *
 * The service walks a ring from its own read index and stops at the first empty buffer, because the
 * block writes in order and everything after an empty one is empty too. That holds only while the
 * read index and the block agree on where the next packet goes. When they stop agreeing - the block
 * wrote over buffers the host had not taken, see octep_dp_oq_first_grant - the buffer at the read index
 * stays empty for good while the block fills the ones after it, and that ring delivers nothing ever
 * again: measured as a download that stopped dead, R_OUT_CNTS at 1785 on a 256-entry ring, while
 * every other ring carried on.
 *
 * So when a pass finds nothing but the count says there is something, look further. If a later
 * buffer holds a packet and the gap in front of it has lasted OCTEP_DP_RESYNC_TICKS - long past any
 * DMA still in flight - move the read index to it. And take out of R_OUT_CNTS the packets that will
 * never be found, so the count is again what the ring holds and its interrupt level means something.
 * The frames already lost stay lost; what this prevents is a ring that never recovers.
 */
static void
octep_dp_oq_resync(struct octep_softc *sc, struct octep_dma *bufs, uint32_t ring, uint32_t n)
{
	uint32_t d, first, held, idx, span;
	uint64_t blen;
	uint8_t *b;

	if (n == 0) {
		sc->dp_oq_gap[ring] = 0;
		return;
	}
	first = 0;
	held = 0;
	for (d = 1; d < sc->dp_oq_rsize; d++) {
		idx = (sc->dp_oq_rd[ring] + d) % sc->dp_oq_rsize;
		b = (uint8_t *)bufs->vaddr + ((size_t)idx * OCTEP_DP_BUF_STRIDE);
		blen = be64toh(*(uint64_t *)(b + OCTEP_RX_LEN_OFF));
		if (blen == 0 || blen == OCTEP_DP_BUF_POISON_WORD || blen <= OCTEP_RX_PREFIX_LEN - 8)
			continue;
		if (first == 0)
			first = d;
		held++;
		/*
		 * Held is compared with a count of packets, so the buffers a long packet went
		 * on into are stepped over and not counted: the first written buffer after an
		 * empty one is the first of its packet, and from there each length says where
		 * the next begins.
		 */
		span = octep_dp_oq_span(blen, sc->dp_oq_rsize);
		if (span > 1)
			d += span - 1;
	}
	if (sc->dp_oq_gap[ring] == 0) {
		sc->dp_oq_gap[ring] = ticks;
		return;
	}
	if (ticks - sc->dp_oq_gap[ring] < OCTEP_DP_RESYNC_TICKS)
		return;
	sc->dp_oq_gap[ring] = 0;

	/*
	 * A buffer that is about to be stepped over and is neither untouched nor poison was
	 * written by the block: a length too short to be one of its frames, which the walk above
	 * and the pass both read as nothing here yet. The block spent a unit on it. With credit
	 * topped up at every pass that did not matter; with one unit for one buffer it is a buffer
	 * the block never gets back, and a thousand of them is a ring under its watermark. So it is
	 * re-poisoned and its unit is owed like any other. If it is the buffer at the read index
	 * and nothing is full after it, it is stepped over all the same.
	 */
	b = (uint8_t *)bufs->vaddr + ((size_t)(sc->dp_oq_rd[ring] % sc->dp_oq_rsize) *
	    OCTEP_DP_BUF_STRIDE);
	blen = be64toh(*(uint64_t *)(b + OCTEP_RX_LEN_OFF));
	/*
	 * And if what the read index is on has become a packet since the pass looked - one landed
	 * while the ring was being walked above - there is no gap any more, and nothing here may
	 * touch it: not step over it, not poison it, not take it out of the count. The next pass
	 * takes it whole. Stepping over it used to cost that one frame; for a packet of several
	 * buffers it would leave the read index on the second of them, where the first eight bytes
	 * are payload and would be believed as a length.
	 */
	if (blen != 0 && blen != OCTEP_DP_BUF_POISON_WORD && blen > OCTEP_RX_PREFIX_LEN - 8)
		return;
	if (first == 0 && blen != 0 && blen != OCTEP_DP_BUF_POISON_WORD)
		first = 1;
	if (first != 0) {
		for (d = 0; d < first; d++) {
			idx = (sc->dp_oq_rd[ring] + d) % sc->dp_oq_rsize;
			b = (uint8_t *)bufs->vaddr + ((size_t)idx * OCTEP_DP_BUF_STRIDE);
			blen = be64toh(*(uint64_t *)(b + OCTEP_RX_LEN_OFF));
			if (blen == 0 || blen == OCTEP_DP_BUF_POISON_WORD)
				continue;
			memset(b, OCTEP_DP_BUF_POISON, OCTEP_DP_BUF_SIZE);
			sc->dp_oq_carry[ring]++;
			sc->dp_rx_short++;
		}
		bus_dmamap_sync(bufs->tag, bufs->map, BUS_DMASYNC_PREREAD);
		sc->dp_oq_rd[ring] = (sc->dp_oq_rd[ring] + first) % sc->dp_oq_rsize;
		sc->dp_rx_resync++;
		sc->dp_rx_skipped += first;
	}
	if (n > held && sc->dp_ack_cnts != 0)
		octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_CNTS, n - held);
}

/*
 * Has this ring's next buffer held a packet, with a count of nothing, for longer than a count
 * takes to arrive?
 *
 * The other way for a ring's read index and the block to stop agreeing, and the mirror of the one
 * above. There the count says a packet and the buffer is empty. Here the buffer holds a packet and
 * the count says none - and a pass that trusts the count leaves it there. The next packet to
 * arrive is counted, the pass takes one buffer, and the one it takes is the old one: from then on
 * every frame on that ring is handed up only when as many more have arrived behind it as the ring
 * is behind. Nothing stops, nothing is lost and no counter moves. Measured, on a ring left 268
 * buffers behind: a connection's opening frame delivered 268 arrivals after it was written, which
 * on a ring with a frame or two a second is minutes, and one new connection in eight - the share
 * of tuples the fast path hashes to one ring of eight - timing out for as long as the appliance
 * stayed up.
 *
 * What put that ring behind is closed where it happened: a packet that used more than one buffer
 * and was taken as one, see octep_dp_oq_pass. This is for whatever does it next, because a ring in
 * that state has no way back that depends on a count.
 *
 * A written buffer and a count of zero is also what an ordinary arrival looks like for the moment
 * between the block's write and its count. So it has to last: the same buffer, still there and
 * still uncounted OCTEP_DP_RESYNC_TICKS after it was first seen, with no pass having taken
 * anything in between - every pass that takes a packet, and every one that finds the buffer
 * empty, starts the wait again. On a ring carrying a packet every millisecond that never comes
 * true, and there a ring that is behind costs a millisecond for each buffer it is behind by; on a
 * quiet one the watchdog's visits are what find it, two of them, a period apart.
 *
 * AND WHAT IS TAKEN IS WHAT WAITED. When the buffer is first seen the packets that are there are
 * counted - from the read index, each by what its length says it used, to the first empty buffer
 * - and that number is kept beside the time. Nothing is taken before the wait is over, so they
 * are the same packets when it is, every one of them written at least that long ago and none of
 * them counted: those are taken, and no more. A packet that lands afterwards has a count coming
 * and is left for it, and one the block is still writing is not touched at all - walking on to
 * the first empty buffer would have taken both.
 *
 * Called with the ring held, by a pass that read a count of zero. Returns how many packets are
 * to be taken without a count, or 0.
 */
static uint32_t
octep_dp_oq_ahead(struct octep_softc *sc, struct octep_dma *bufs, uint32_t ring)
{
	uint64_t blen;
	uint32_t d, npkt, span;
	uint8_t *b;

	bus_dmamap_sync(bufs->tag, bufs->map, BUS_DMASYNC_POSTREAD);
	b = (uint8_t *)bufs->vaddr + ((size_t)(sc->dp_oq_rd[ring] % sc->dp_oq_rsize) *
	    OCTEP_DP_BUF_STRIDE);
	blen = be64toh(*(uint64_t *)(b + OCTEP_RX_LEN_OFF));
	if (blen == 0 || blen == OCTEP_DP_BUF_POISON_WORD || blen <= OCTEP_RX_PREFIX_LEN - 8) {
		sc->dp_oq_ahead[ring] = 0;
		return (0);
	}
	if (sc->dp_oq_ahead[ring] == 0) {
		npkt = 0;
		for (d = 0; d < sc->dp_oq_rsize; d += span) {
			b = (uint8_t *)bufs->vaddr +
			    ((size_t)((sc->dp_oq_rd[ring] + d) % sc->dp_oq_rsize) *
			    OCTEP_DP_BUF_STRIDE);
			blen = be64toh(*(uint64_t *)(b + OCTEP_RX_LEN_OFF));
			if (blen == 0 || blen == OCTEP_DP_BUF_POISON_WORD ||
			    blen <= OCTEP_RX_PREFIX_LEN - 8)
				break;
			span = octep_dp_oq_span(blen, sc->dp_oq_rsize);
			if (span == 0)
				span = 1;
			npkt++;
		}
		sc->dp_oq_ahead_n[ring] = npkt;
		sc->dp_oq_ahead[ring] = ticks;
		return (0);
	}
	if (ticks - sc->dp_oq_ahead[ring] < OCTEP_DP_RESYNC_TICKS)
		return (0);
	sc->dp_oq_ahead[ring] = 0;
	return (sc->dp_oq_ahead_n[ring]);
}

/*
 * Take the five-tuple out of a punted frame, beside its prefix.
 *
 * Not for the datapath's sake - nothing here changes what happens to the frame. It is the other
 * half of the one question worth asking about a punted frame: the prefix says which flow the
 * coprocessor thinks it is, and the tuple is what pf can be asked about. Programming the slot from
 * the prefix while the tuple came from a later frame would program the wrong flow, so both are
 * stamped with the same sequence number and the reader checks they agree.
 *
 * Cheap on purpose: a bounds check and four loads, no checksum, no options walked. An IPv4 header
 * with options is read for its addresses and its protocol and its ports are left at zero, which is
 * honest - the ports are at a different offset and a flow programmed from the wrong offset is
 * worse than a flow not programmed. IPv6 is not parsed at all yet and says so.
 */
static void
octep_dp_rx_capture(struct octep_softc *sc, uint16_t tag, const uint8_t *f, uint32_t flen)
{
	/*
	 * The capture half of octep_dp_rx_tuple, for a frame the coprocessor decrypted on the way in:
	 * the head of the frame and its port are kept for dp.rx_frame, and nothing is offered as a
	 * candidate. Parsed, such a frame reads as ESP between the two tunnel ends with the INNER
	 * flow's slot behind it, and a candidate built from that pairs one flow's identity with
	 * another's addresses. The decrypted direction's candidate is offered by octep_ipsec_rx
	 * instead, from the inner packet, once the frame has been terminated and filtered.
	 */
	sc->dp_rx_frame_len = flen < sizeof(sc->dp_rx_frame) ?
	    flen : (uint32_t)sizeof(sc->dp_rx_frame);
	memcpy(sc->dp_rx_frame, f, sc->dp_rx_frame_len);
	sc->dp_rx_tag = tag;
	sc->dp_rx_tuple_seq = 0;
}

static int
octep_dp_rx_tuple(struct octep_softc *sc, uint16_t tag, uint32_t slot, uint32_t rev,
    const uint8_t *f, uint32_t flen, struct octep_pf_tuple *out)
{
	/*
	 * Parsed into a local and published afterwards, and the slot and the revision are arguments
	 * the caller decoded from its own buffer.
	 *
	 * Nothing that reaches a candidate may be read back out of the softc. The capture fields
	 * below are one set shared by every ring, and up to eight rings run this concurrently from
	 * their own MSI-X handlers - so a candidate assembled from them can hold one connection's
	 * addresses with another's flow slot, and a microflow programmed from that pair forwards one
	 * connection's packets with another's translation, in hardware, past pf. The slot's lock
	 * on the candidate table cannot help: it guards where the candidate is written, not where it
	 * is read from. Review caught this before it ran.
	 */
	struct octep_pf_tuple t;
	const uint8_t *ip;
	uint32_t ihl, l2 = ETHER_HDR_LEN;
	uint16_t etype, vid = 0;
	int closing = 0;

	/*
	 * The head of the frame, kept before anything can reject it.
	 *
	 * This runs before the tag lookup, which is the point: a frame the fast path forwarded to
	 * the host's own port carries a tag no interface here owns and is dropped as untagged a few
	 * lines later. Its bytes are the only direct evidence of what the fast path builds.
	 */
	sc->dp_rx_frame_len = flen < sizeof(sc->dp_rx_frame) ?
	    flen : (uint32_t)sizeof(sc->dp_rx_frame);
	memcpy(sc->dp_rx_frame, f, sc->dp_rx_frame_len);

	/*
	 * And the port it came in on, kept for the same frame and for the same reason as the rest.
	 * It is the other direction's egress port when the route cannot name one - see struct
	 * octep_flow.
	 */
	sc->dp_rx_tag = tag;

	sc->dp_rx_tuple_seq = 0;
	if (flen < ETHER_HDR_LEN + 20)
		return (0);

	etype = be16dec(f + 12);

	/*
	 * An 802.1Q tag (discovery 10): a VLAN subinterface's frames, which the fast path looks up as
	 * flows once the VLAN has a logical interface of its own (octep_dp_vlan_lifs). The IP header is
	 * four bytes on, and the VLAN goes with the candidate.
	 */
	if (etype == ETHERTYPE_VLAN) {
		if (flen < ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN + 20)
			return (0);
		vid = be16dec(f + 14) & 0x0fff;
		etype = be16dec(f + 16);
		l2 = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
	}

	/*
	 * IPv6, since 2026-10-18: TCP and UDP whose header follows the fixed forty bytes directly.
	 * An extension header in between puts the ports somewhere else, and the frame is published
	 * for the instruments but offered as nothing - as an IPv4 header with options is. The
	 * addresses go in whole into sip6 and dip6, and their folds into sip and dip.
	 */
	if (etype == ETHERTYPE_IPV6) {
		ip = f + l2;
		if (flen < l2 + 40 || (ip[0] >> 4) != 6)
			return (0);
		bzero(&t, sizeof(t));
		t.af = AF_INET6;
		t.proto = ip[6];
		memcpy(t.sip6, ip + 8, 16);
		memcpy(t.dip6, ip + 24, 16);
		t.sip = octep_fold6(t.sip6);
		t.dip = octep_fold6(t.dip6);
		if ((t.proto == IPPROTO_TCP || t.proto == IPPROTO_UDP) &&
		    flen >= l2 + 40 + 4) {
			memcpy(&t.sport, ip + 40, 2);
			memcpy(&t.dport, ip + 42, 2);
			if (t.proto == IPPROTO_TCP && flen >= l2 + 40 + 14)
				closing = (ip[40 + 13] & 0x05) != 0;	/* FIN or RST */
			if (slot != 0) {
				struct octep_tcp_seg seg;

				octep_dp_tcp_seg(ip, flen - l2, &seg);
				octep_flow_cand_push(sc, &t, slot, rev, tag, closing, &seg, f, vid);
			}
		}
		sc->dp_rx_tuple = t;
		sc->dp_rx_slot = slot;
		sc->dp_rx_slot_rev = rev;
		sc->dp_rx_tuple_seq = sc->dp_rx_prefix_seq;
		*out = t;
		return (closing);
	}
	if (etype != ETHERTYPE_IP)
		return (0);

	ip = f + l2;
	if ((ip[0] >> 4) != 4)
		return (0);
	ihl = (uint32_t)(ip[0] & 0x0f) * 4;
	if (ihl < 20)
		return (0);

	/*
	 * memcpy, not be16dec or le32dec, and that is the whole point of these four lines.
	 *
	 * pf keeps both the address and the port in NETWORK order - pf_state_key_setup is handed
	 * pd->nsport straight out of the header and stores it unconverted, and struct in_addr holds
	 * s_addr the same way. A be32dec here would byte-swap on this host and match nothing, and an
	 * le32dec would happen to be right on a little-endian machine for the wrong reason. Copying
	 * the bytes verbatim is right on both, and says so.
	 */
	bzero(&t, sizeof(t));
	t.af = AF_INET;
	t.proto = ip[9];
	memcpy(&t.sip, ip + 12, 4);
	memcpy(&t.dip, ip + 16, 4);

	if ((t.proto == IPPROTO_TCP || t.proto == IPPROTO_UDP) &&
	    flen >= l2 + ihl + 4) {
		memcpy(&t.sport, ip + ihl, 2);
		memcpy(&t.dport, ip + ihl + 2, 2);
	}

	/*
	 * ICMP has no ports and pf gives it two anyway.
	 *
	 * pf_icmp_mapping reduces an echo request and an echo reply to the same virtual_type,
	 * htons(ICMP_ECHO), and takes virtual_id from the message's own id field - then
	 * pf.c:5945 puts one in nsport and the other in ndport, which of them in which depending
	 * on the direction it decided. So an ICMP state's key carries (id, ECHO) or (ECHO, id),
	 * and a key built with two zeros matches nothing: measured, a live ping reported
	 * a tuple with both ports zero and `pf has no state for it` while pfctl was showing the
	 * state.
	 *
	 * Only echo is given ports here. The other ICMP types carry an embedded packet rather than
	 * an id and pf keys them off that; they are left at zero, which is wrong in a way that
	 * reports itself rather than wrong in a way that matches the next connection along.
	 */
	if (t.proto == IPPROTO_ICMP &&
	    flen >= l2 + ihl + 6 &&
	    (ip[ihl] == 0 || ip[ihl] == 8)) {
		memcpy(&t.sport, ip + ihl + 4, 2);
		t.dport = htons(8);	/* ICMP_ECHO, as pf's virtual_type */
	}

	/*
	 * Offer it for acceleration FIRST, from the locals, before any of this touches the shared
	 * capture. A frame whose metadata names no flow - which is every frame while the offload gate
	 * is shut - is not a candidate, because the slot is what a flow is programmed by.
	 */
	closing = t.proto == IPPROTO_TCP ? octep_dp_tcp_closing(ip, flen - l2) : 0;
	if (slot != 0) {
		struct octep_tcp_seg seg;

		octep_dp_tcp_seg(ip, flen - l2, &seg);
		octep_flow_cand_push(sc, &t, slot, rev, tag, closing, &seg, f, vid);
	}

	/*
	 * And then publish, for the instruments. dp.pf_state and dp.accelerate read these, and their
	 * sequence pairing is what tells a reader the slot and the tuple came from one frame; it is
	 * still the right guard for a hand instrument acting on the most recent frame, and it is no
	 * longer the only thing standing between two rings and a mixed candidate.
	 */
	sc->dp_rx_tuple = t;
	sc->dp_rx_slot = slot;
	sc->dp_rx_slot_rev = rev;
	sc->dp_rx_tuple_seq = sc->dp_rx_prefix_seq;
	*out = t;
	return (closing);
}

/*
 * The inspector's side (discovery 4). The fast path hands a connection whose verdict is "send to
 * inspection" to the inspector's function, frame by frame: the frame as it arrived, behind a two-byte
 * tag and sixty-four bytes of usfp_dp_md - the IPS block and the forwarding block, which the fast path
 * applies when the frame comes back (proc_from_ips, FROM_IPS_TO_WIRE). Handing one back is posting the
 * same sixty-six bytes and the frame, as the inspector let it through, on the function's input ring.
 *
 * The inspector is whatever reads oxips0. Suricata in IPS mode does, through netmap, which replaces the
 * interface's input function with its own while it runs and calls ours (octep_ips_input) with each
 * frame it lets through - netmap's way of passing a frame on to the host's stack. So a frame goes to
 * oxips0 as input when something has taken that function over, and straight back when nothing has
 * and dp.ips_loop says to pass it; otherwise it is dropped and counted.
 */

/*
 * The frame's key: its address family, protocol, both addresses and, where there are some, both
 * ports - which a frame of the other direction has the other way round. Returns its length, or 0 for a
 * frame that is not IP.
 */
static uint32_t
octep_ips_key(const uint8_t *f, uint32_t len, uint8_t *key)
{
	const uint8_t *ip;
	uint32_t ihl, k;
	uint8_t proto;

	if (len < ETHER_HDR_LEN + 20)
		return (0);
	ip = f + ETHER_HDR_LEN;
	k = 0;
	switch (be16dec(f + 12)) {
	case ETHERTYPE_IP:
		ihl = (uint32_t)(ip[0] & 0x0f) * 4;
		if (ihl < 20 || len < ETHER_HDR_LEN + ihl)
			return (0);
		proto = ip[9];
		key[k++] = 4;
		key[k++] = proto;
		memcpy(key + k, ip + 12, 8);
		k += 8;
		/* Ports from the first piece only: a later fragment has none, and is keyed without. */
		if ((proto == IPPROTO_TCP || proto == IPPROTO_UDP || proto == IPPROTO_SCTP) &&
		    (be16dec(ip + 6) & 0x1fff) == 0 && len >= ETHER_HDR_LEN + ihl + 4) {
			memcpy(key + k, ip + ihl, 4);
			k += 4;
		}
		return (k);
	case ETHERTYPE_IPV6:
		if (len < ETHER_HDR_LEN + 40)
			return (0);
		proto = ip[6];
		key[k++] = 6;
		key[k++] = proto;
		memcpy(key + k, ip + 8, 32);
		k += 32;
		if ((proto == IPPROTO_TCP || proto == IPPROTO_UDP || proto == IPPROTO_SCTP) &&
		    len >= ETHER_HDR_LEN + 44) {
			memcpy(key + k, ip + 40, 4);
			k += 4;
		}
		return (k);
	default:
		return (0);
	}
}

static void
octep_ips_remember(struct octep_softc *sc, const uint8_t *key, uint32_t klen, const uint8_t *hdr)
{
	struct octep_ips_ent *e;

	e = &sc->dp_ips_tab[hash32_buf(key, klen, HASHINIT) & (OCTEP_IPS_TAB - 1)];
	mtx_lock(&sc->dp_ips_tab_mtx);
	e->klen = (uint8_t)klen;
	memcpy(e->key, key, klen);
	memcpy(e->hdr, hdr, OCTEP_TOTAL_TAG_LEN);
	mtx_unlock(&sc->dp_ips_tab_mtx);
}

static int
octep_ips_recall(struct octep_softc *sc, const uint8_t *key, uint32_t klen, uint8_t *hdr)
{
	struct octep_ips_ent *e;
	int found;

	e = &sc->dp_ips_tab[hash32_buf(key, klen, HASHINIT) & (OCTEP_IPS_TAB - 1)];
	mtx_lock(&sc->dp_ips_tab_mtx);
	found = (e->klen == klen && memcmp(e->key, key, klen) == 0);
	if (found)
		memcpy(hdr, e->hdr, OCTEP_TOTAL_TAG_LEN);
	mtx_unlock(&sc->dp_ips_tab_mtx);
	return (found);
}

/*
 * Hand one frame back to the fast path on the inspector's function's input ring: the sixty-six bytes
 * `hdr`, then the frame - out of the mbuf `m` when there is one, out of `f` when not. Returns 0, or
 * ENOBUFS when the ring is down or full.
 */
static int
octep_ips_post(struct octep_softc *sc, const uint8_t *hdr, struct mbuf *m, const uint8_t *f,
    uint32_t flen)
{
	struct octep_dp_txq *q = &sc->dp_ips_txq;
	uint32_t slot;
	uint8_t *d;

	if (OCTEP_TOTAL_TAG_LEN + flen > OCTEP_DP_TXBUF_STRIDE)
		return (ENOBUFS);
	mtx_lock(&q->mtx);
	if (!q->up || (octep_dp_ring_rd(sc, q->ring, OCTEP_SDP_R_IN_INSTR_DBELL) & 0xffffffffULL) >=
	    OCTEP_DP_IQ_DESCS - 1) {
		q->full++;
		mtx_unlock(&q->mtx);
		return (ENOBUFS);
	}
	slot = q->prod;
	d = (uint8_t *)q->bufs.vaddr + (size_t)slot * OCTEP_DP_TXBUF_STRIDE;
	memcpy(d, hdr, OCTEP_TOTAL_TAG_LEN);
	if (m != NULL)
		m_copydata(m, 0, flen, (caddr_t)(d + OCTEP_TOTAL_TAG_LEN));
	else
		memcpy(d + OCTEP_TOTAL_TAG_LEN, f, flen);
	bus_dmamap_sync(q->bufs.tag, q->bufs.map, BUS_DMASYNC_PREWRITE);
	octep_dp_build_instr_pk(sc, q->iq.vaddr, slot,
	    q->bufs.paddr + (bus_addr_t)slot * OCTEP_DP_TXBUF_STRIDE, OCTEP_TOTAL_TAG_LEN + flen,
	    sc->dp_ips_pkind);
	bus_dmamap_sync(q->iq.tag, q->iq.map, BUS_DMASYNC_PREWRITE);
	q->prod = (q->prod + 1) % OCTEP_DP_IQ_DESCS;
	q->posted++;
	octep_dp_ring_wr(sc, q->ring, OCTEP_SDP_R_IN_INSTR_DBELL, 1);
	mtx_unlock(&q->mtx);
	return (0);
}

/*
 * Ask for connection `idx` to be set to forward: once, by the task below, which takes the lock the
 * command needs. A full list leaves the request unmade, to be made by a later frame.
 */
static void
octep_ips_done_req(struct octep_softc *sc, uint32_t idx)
{
	struct octep_conn *c;
	int queued = 0;

	if (sc->dp_conn == NULL || idx < 2 || idx >= sc->dp_conn_cap)
		return;
	c = &sc->dp_conn[idx];
	if (!c->ips || !atomic_cmpset_32(&c->ips_req, 0, 1))
		return;
	mtx_lock(&sc->dp_ips_tab_mtx);
	if (sc->dp_ips_npend < OCTEP_IPS_PEND) {
		sc->dp_ips_pend[sc->dp_ips_npend++] = idx;
		queued = 1;
	}
	mtx_unlock(&sc->dp_ips_tab_mtx);
	if (queued)
		taskqueue_enqueue(taskqueue_thread, &sc->dp_ips_done_task);
	else
		atomic_store_rel_32(&c->ips_req, 0);
}

/*
 * What the inspector let through of connection `idx` - the session in the frame's own metadata - and,
 * once that reaches dp.ips_depth, the request to set it to forward. A depth of 0 inspects for good.
 */
static void
octep_ips_count(struct octep_softc *sc, uint32_t idx, uint32_t len)
{
	struct octep_conn *c;

	if (sc->dp_ips_depth == 0 || sc->dp_conn == NULL || idx < 2 || idx >= sc->dp_conn_cap)
		return;
	c = &sc->dp_conn[idx];
	if (!c->ips)
		return;
	if (atomic_fetchadd_64(&c->ips_bytes, len) + len >= sc->dp_ips_depth)
		octep_ips_done_req(sc, idx);
}

/*
 * oxips0's input function: a frame the inspector let through, or one nothing inspected. Handed back
 * with the metadata its connection's frames arrived with in this direction, or dropped and counted
 * when none is kept.
 */
static void
octep_ips_input(if_t ifp, struct mbuf *m)
{
	struct octep_softc *sc = if_getsoftc(ifp);
	uint8_t head[ETHER_HDR_LEN + 60 + 4], key[OCTEP_IPS_KEY_MAX], hdr[OCTEP_TOTAL_TAG_LEN];
	uint32_t flen, hlen, klen;

	while (m != NULL) {
		struct mbuf *n = m->m_nextpkt;

		m->m_nextpkt = NULL;
		flen = (uint32_t)m->m_pkthdr.len;
		hlen = flen < sizeof(head) ? flen : (uint32_t)sizeof(head);
		m_copydata(m, 0, hlen, (caddr_t)head);
		klen = octep_ips_key(head, hlen, key);
		if (klen == 0) {
			atomic_add_64(&sc->dp_ips_unkeyed, 1);
			atomic_add_64(&sc->dp_ips_dropped, 1);
		} else if (!octep_ips_recall(sc, key, klen, hdr)) {
			atomic_add_64(&sc->dp_ips_nomd, 1);
			atomic_add_64(&sc->dp_ips_dropped, 1);
		} else if (octep_ips_post(sc, hdr, m, NULL, flen) != 0) {
			atomic_add_64(&sc->dp_ips_dropped, 1);
		} else {
			atomic_add_64(&sc->dp_ips_passed, 1);
			atomic_add_64(&sc->dp_ips_back, 1);
			octep_ips_count(sc, le32dec(hdr + OCTEP_PPORT_HLEN + 8), flen);
		}
		m_freem(m);
		m = n;
	}
}

/*
 * Whether a connection about to be made should be made inspected: new connections are to be
 * (dp.ips_new), something reads oxips0, the connection is not a tunnel's and its microflows ask the
 * verdict at all, and - for one revived after a give-back - it has not been let through to the depth
 * already. octep_rpc_flow_create asks, with sc->mtx held.
 */
int
octep_ips_wanted(struct octep_softc *sc, const struct octep_conn *c)
{
	if_t ifp = sc->dp_ips_ifp;

	return (sc->dp_ips_new != 0 && sc->dp_ips_on && ifp != NULL &&
	    (if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0 &&
	    if_getinputfn(ifp) != octep_ips_input && !c->ipsec &&
	    sc->dp_mflow_action == OCTEP_MFLOW_ACTION_IPS &&
	    (sc->dp_ips_depth == 0 || c->ips_bytes < sc->dp_ips_depth));
}

/* After a connection is made: inspected, if that is the verdict it was made with. */
static void
octep_ips_mark(struct octep_softc *sc, struct octep_conn *c)
{

	mtx_assert(&sc->mtx, MA_OWNED);
	if (sc->rpc_conn_verdict == OCTEP_CONN_VERDICT_SEND_TO_IPS) {
		if (!c->ips)
			sc->dp_ips_made++;
		c->ips = 1;
		c->ips_req = 0;
	}
}

/*
 * The requests, carried out: each connection still inspected, not dropped since, and either let
 * through to the depth or with nothing reading oxips0 any more, is set to forward. A connection whose
 * slot was reused since the request is new and has let nothing through yet, so it is left alone.
 */
static void
octep_ips_done_task(void *arg, int pending)
{
	struct octep_softc *sc = arg;
	uint32_t idx[OCTEP_IPS_PEND], n, i;
	struct octep_conn *c;
	if_t ifp;
	int listening;

	(void)pending;
	mtx_lock(&sc->dp_ips_tab_mtx);
	n = sc->dp_ips_npend;
	memcpy(idx, sc->dp_ips_pend, n * sizeof(idx[0]));
	sc->dp_ips_npend = 0;
	mtx_unlock(&sc->dp_ips_tab_mtx);

	mtx_lock(&sc->mtx);
	ifp = sc->dp_ips_ifp;
	listening = ifp != NULL && if_getinputfn(ifp) != octep_ips_input;
	for (i = 0; i < n; i++) {
		if (sc->dp_conn == NULL || idx[i] >= sc->dp_conn_cap)
			continue;
		c = &sc->dp_conn[idx[i]];
		if (!c->used || c->doomed || c->dropped || !c->ips ||
		    (listening && sc->dp_ips_depth != 0 && c->ips_bytes < sc->dp_ips_depth)) {
			c->ips_req = 0;
			continue;
		}
		if (octep_rpc_conn_verdict(sc, c, OCTEP_CONN_VERDICT_CUT_THRU) == 0) {
			c->ips = 0;
			sc->dp_ips_cut++;
		}
		c->ips_req = 0;
	}
	mtx_unlock(&sc->mtx);
}

/*
 * A frame the fast path handed to the inspector, `b` its receive buffer. Returns an mbuf for the
 * caller to give oxips0 with the rest of the pass, or NULL when the frame was handed straight back or
 * dropped here.
 */
static struct mbuf *
octep_dp_ips_rx(struct octep_softc *sc, const uint8_t *b, uint32_t flen, uint32_t span)
{
	if_t ifp = sc->dp_ips_ifp;
	const uint8_t *f = b + OCTEP_RX_PREFIX_LEN;
	uint8_t key[OCTEP_IPS_KEY_MAX];
	struct mbuf *m;
	uint32_t klen;

	atomic_add_64(&sc->dp_ips_rx, 1);
	if (span > 1 || flen < ETHER_HDR_LEN ||
	    OCTEP_TOTAL_TAG_LEN + flen > OCTEP_DP_TXBUF_STRIDE) {
		atomic_add_64(&sc->dp_ips_dropped, 1);
		return (NULL);
	}

	/* Something reads oxips0: its input function is no longer this driver's. */
	if (ifp != NULL && (if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0 &&
	    if_getinputfn(ifp) != octep_ips_input) {
		klen = octep_ips_key(f, flen, key);
		if (klen == 0) {
			atomic_add_64(&sc->dp_ips_unkeyed, 1);
			atomic_add_64(&sc->dp_ips_dropped, 1);
			return (NULL);
		}
		m = m_get2((int)flen, M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m == NULL) {
			atomic_add_64(&sc->dp_ips_dropped, 1);
			return (NULL);
		}
		octep_ips_remember(sc, key, klen, b + OCTEP_RX_TAG_OFF);
		memcpy(mtod(m, uint8_t *), f, flen);
		m->m_len = m->m_pkthdr.len = (int)flen;
		m->m_pkthdr.rcvif = ifp;
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		if_inc_counter(ifp, IFCOUNTER_IBYTES, flen);
		atomic_add_64(&sc->dp_ips_up, 1);
		return (m);
	}

	/*
	 * Nothing reads oxips0. With dp.ips_loop, the default, the frame passes as if inspected and its
	 * connection is set to forward - an inspector that stops costs its connections nothing - and
	 * without it the frame is dropped.
	 */
	if (sc->dp_ips_loop == 0 || octep_ips_post(sc, b + OCTEP_RX_TAG_OFF, NULL, f, flen) != 0) {
		atomic_add_64(&sc->dp_ips_dropped, 1);
		return (NULL);
	}
	atomic_add_64(&sc->dp_ips_back, 1);
	octep_ips_done_req(sc, le32dec(b + OCTEP_RX_META_OFF + 8));
	return (NULL);
}

/*
 * oxips0 itself: an Ethernet interface with no address of its own, up as soon as it exists. What the
 * stack would send through it is dropped - nothing on this host has anything to say to the inspector's
 * function but the frames handed back above.
 */
static void
octep_ips_if_init(void *arg)
{
	if_t ifp = arg;

	if_setdrvflagbits(ifp, IFF_DRV_RUNNING, IFF_DRV_OACTIVE);
}

static int
octep_ips_if_ioctl(if_t ifp, u_long cmd, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq *)data;

	switch (cmd) {
	case SIOCSIFFLAGS:
		if ((if_getflags(ifp) & IFF_UP) != 0)
			if_setdrvflagbits(ifp, IFF_DRV_RUNNING, 0);
		else
			if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
		return (0);
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > OCTEP_DP_IF_MTU_MAX)
			return (EINVAL);
		if_setmtu(ifp, ifr->ifr_mtu);
		return (0);
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		return (0);
	default:
		return (ether_ioctl(ifp, cmd, data));
	}
}

static int
octep_ips_if_transmit(if_t ifp, struct mbuf *m)
{

	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	m_freem(m);
	return (0);
}

static void
octep_ips_if_qflush(if_t ifp)
{

	(void)ifp;
}

static void
octep_ips_if_create(struct octep_softc *sc)
{
	uint8_t mac[ETHER_ADDR_LEN] = { 0x02, 0x0c, 0xe0, 0x83, 0x1f, 0x00 };
	if_t ifp;

	if (sc->dp_ips_ifp != NULL)
		return;
	if (sc->dp_ips_tab == NULL) {
		sc->dp_ips_tab = malloc(sizeof(*sc->dp_ips_tab) * OCTEP_IPS_TAB, M_DEVBUF,
		    M_WAITOK | M_ZERO);
		mtx_init(&sc->dp_ips_tab_mtx, "octep ips tab", NULL, MTX_DEF);
	}
	TASK_INIT(&sc->dp_ips_done_task, 0, octep_ips_done_task, sc);
	sc->dp_ips_loop = 1;
	if (sc->dp_ips_depth == 0)
		sc->dp_ips_depth = OCTEP_IPS_DEPTH;
	mac[5] = (uint8_t)device_get_unit(sc->dev);
	ifp = if_alloc(IFT_ETHER);
	if_initname(ifp, "oxips", device_get_unit(sc->dev));
	if_setsoftc(ifp, sc);
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
	if_setinitfn(ifp, octep_ips_if_init);
	if_setioctlfn(ifp, octep_ips_if_ioctl);
	if_settransmitfn(ifp, octep_ips_if_transmit);
	if_setqflushfn(ifp, octep_ips_if_qflush);
	if_setmtu(ifp, ETHERMTU);
	ether_ifattach(ifp, mac);
	if_setbaudrate(ifp, 0);
	/* After ether_ifattach, which sets ether_input: netmap keeps whatever is here when it attaches. */
	if_setinputfn(ifp, octep_ips_input);
	sc->dp_ips_ifp = ifp;
	device_printf(sc->dev, "dp: %s, the inspector's interface\n", if_name(ifp));
}

static void
octep_ips_if_destroy(struct octep_softc *sc)
{

	if (sc->dp_ips_ifp != NULL) {
		ether_ifdetach(sc->dp_ips_ifp);
		if_free(sc->dp_ips_ifp);
		sc->dp_ips_ifp = NULL;
		taskqueue_drain(taskqueue_thread, &sc->dp_ips_done_task);
	}
	if (sc->dp_ips_tab != NULL) {
		free(sc->dp_ips_tab, M_DEVBUF);
		sc->dp_ips_tab = NULL;
		mtx_destroy(&sc->dp_ips_tab_mtx);
	}
}

/*
 * Service one output ring, which is the half this driver never had.
 *
 * Arming a ring is not the same as serving it. The far side writes a packet, and then waits for the
 * host to say it has taken it: the packet count in R_OUT_CNTS has to be acknowledged, and the
 * buffer has to be handed back as a fresh credit through R_OUT_SLIST_DBELL. This driver did
 * neither, so exactly one packet ever fitted - measured twice, on ring 12 with the vendor's VF
 * topology and on ring 4 without it, each time with OUT_CNTS left reading 1 and nothing following.
 *
 * The order is the vendor's: take what arrived, put the buffers back, then acknowledge. Doing it
 * the other way round hands the far side a credit for a buffer the host has not re-poisoned.
 *
 * This is one pass, and the caller holds the ring: octep_dp_oq_service, which is the only caller.
 * It takes at most `most` packets, which is what is left of the caller's bound. Returns how many
 * packets it took - acknowledged, or taken without a count, see octep_dp_oq_ahead - and sets
 * *unseen when the register counted packets and the buffers held none: a pass that took nothing
 * from a ring that is not empty. A packet is as many buffers as its length says, and it is
 * buffers that are re-poisoned and owed credit, and packets that are counted and acknowledged.
 */
/*
 * A FIN OR A RST OF A CONNECTION IN HARDWARE (issue #679), the XGS 136 driver's npuflow_closing_hold.
 * The fast path hands every one to the host (TO_KN_TCP_FIN_SYN_RST), and pf, sloppy for a connection
 * it no longer sees, took it at its word: a reset with its sequence number 64 MiB outside the window
 * turned the connection's pf states to TIME_WAIT, and an idle connection ended 90 seconds later -
 * measured on the XGS 136, 2026-10-24. So the receive pass holds such a frame of a connection whose
 * window the fast path checks; octep_dp_closing_task reads the connection's numbers from the fast path
 * (CONN_GET_FP_TCP_STATE, which leaves it where it is), gives them to pf and clears the sloppy mark
 * (octep_pf_handback, the take-back's own), hands the frame up for pf to judge, and puts the mark
 * back. Once for each connection in what the task finds waiting: they come in bursts, resets
 * answering data still on its way. One that cannot wait - the queue full, the numbers not had - goes
 * up as before, counted. Called from the receive pass, which takes the lock for the coprocessor's
 * messages too.
 */
#define	OCTEP_CLOSING_CACHE	8

static int
octep_dp_closing_hold(struct octep_softc *sc, struct mbuf *m, const struct octep_pf_tuple *t)
{
	struct octep_conn *c;
	int held = 0;

	if (sc->dp_closing_check == 0 || sc->dp_conn == NULL)
		return (0);
	mtx_lock(&sc->mtx);
	c = sc->dp_closing_off ? NULL : octep_conn_find(sc, t, NULL);
	if (c != NULL && c->tcp_on) {
		if (mbufq_full(&sc->dp_closing_q))
			sc->dp_closing_passed++;
		else {
			m->m_pkthdr.PH_loc.thirtytwo[0] = c->idx;
			m->m_pkthdr.PH_loc.thirtytwo[1] = c->conn_rev;
			(void)mbufq_enqueue(&sc->dp_closing_q, m);
			held = 1;
		}
	}
	mtx_unlock(&sc->mtx);
	if (held)
		taskqueue_enqueue(taskqueue_thread, &sc->dp_closing_task);
	return (held);
}

struct octep_closing_nums {
	uint32_t		idx, rev;
	int			ok;		/* the numbers were had */
	struct octep_pf_tuple	tup[2];
	uint8_t			scale[2];
	uint8_t			seq[OCTEP_TCP_SEQ_LEN];
};

static void
octep_dp_closing_task(void *arg, int pending __unused)
{
	struct octep_softc *sc = arg;
	struct octep_closing_nums nums[OCTEP_CLOSING_CACHE], *k;
	struct epoch_tracker et;
	struct octep_conn *c;
	struct mbufq batch;
	struct mbuf *m;
	uint32_t idx, rev;
	int i, nn, w;

	for (;;) {
		mbufq_init(&batch, INT_MAX);
		mtx_lock(&sc->mtx);
		mbufq_concat(&batch, &sc->dp_closing_q);
		mtx_unlock(&sc->mtx);
		if (mbufq_len(&batch) == 0)
			break;
		nn = 0;
		while ((m = mbufq_dequeue(&batch)) != NULL) {
			idx = m->m_pkthdr.PH_loc.thirtytwo[0];
			rev = m->m_pkthdr.PH_loc.thirtytwo[1];
			m->m_pkthdr.PH_loc.thirtytwo[0] = 0;
			m->m_pkthdr.PH_loc.thirtytwo[1] = 0;
			for (i = 0; i < nn && (nums[i].idx != idx || nums[i].rev != rev); i++)
				;
			if (i < nn)
				k = &nums[i];
			else {
				k = &nums[nn < OCTEP_CLOSING_CACHE ? nn++ : OCTEP_CLOSING_CACHE - 1];
				k->idx = idx;
				k->rev = rev;
				k->ok = 0;
				mtx_lock(&sc->mtx);
				c = (sc->dp_conn != NULL && idx < sc->dp_conn_cap) ? &sc->dp_conn[idx] : NULL;
				if (c != NULL && c->used && c->conn_rev == rev && c->tcp_on) {
					k->tup[0] = c->tuple[0];
					k->tup[1] = c->tuple[1];
					k->scale[0] = c->tcp_scale[0];
					k->scale[1] = c->tcp_scale[1];
					k->ok = octep_rpc_conn_tcp_state(sc, idx, (uint16_t)rev,
					    k->seq) == 0;
				}
				mtx_unlock(&sc->mtx);
			}
			w = 0;
			if (k->ok) {
				CURVNET_SET_QUIET(vnet0);
				w = octep_pf_handback(k->tup, k->seq, k->scale);
				CURVNET_RESTORE();
			}
			if (w > 0)
				sc->dp_closing_checked++;
			else
				sc->dp_closing_passed++;
			/* up, as the receive pass hands a frame: pf judges it, by the numbers when w > 0 */
			NET_EPOCH_ENTER(et);
			if_input(m->m_pkthdr.rcvif, m);
			NET_EPOCH_EXIT(et);
			if (w > 0) {
				CURVNET_SET_QUIET(vnet0);
				(void)octep_pf_mark_sloppy(&k->tup[0]);
				(void)octep_pf_mark_sloppy(&k->tup[1]);
				CURVNET_RESTORE();
			}
		}
	}
}

static uint32_t
octep_dp_oq_pass(struct octep_softc *sc, struct octep_dma *bufs, uint32_t ring, uint32_t most,
    int *unseen)
{
	struct mbuf *mh = NULL, *mt = NULL, *m;
	struct epoch_tracker et;
	uint64_t cnts, istat, up;
	uint32_t n, i, taken, bufs_taken, lose, burst;
	int behind, rc;

	rc = 0;
	behind = 0;
	up = 0;

	/*
	 * Clear the latched output status first, and do it whether or not anything arrived.
	 *
	 * R_OUT_INT_STATUS is write-one-to-clear and the vendor's driver never touches it - because
	 * the vendor runs with interrupts, and servicing the interrupt is what clears it there. This
	 * driver has no interrupt consumer, so the bit set by the first packet stayed set, and
	 * nothing arrived after it. Measured: four frames sent one at a time, OUT_PKT_CNT 1,
	 * ISTAT 0x010 latched, and frames two, three and four never written.
	 *
	 * THIS RING'S BIT, AND NO OTHER (#361). The register is not the ring's own: read through any
	 * ring's window it holds a bit for every ring, bit n for ring n - ring 6 read 0x41, ring 3
	 * 0xa, ring 1 0x22 (the second XGS 3300, 2026-10-21). Writing back what was read cleared
	 * every other ring's bit as well, from whichever servicer was in whichever ring. Each pass
	 * now clears its own. It was not what left rings with a burst - with this alone the soak
	 * counted a stall in its first minutes; that was every ring's vector on cpu0, see
	 * octep_dp_msix_setup - but another ring's status is not this pass's to clear.
	 *
	 * AND PERHAPS NOT ITS OWN (#361, 2026-10-25). The four frames above were measured before this
	 * driver had an interrupt; it has one now, and the vendor's driver, which has always had one,
	 * never writes this register at all - it acknowledges the count and writes RESEND, and that
	 * is all (octeon_enable_irq). A clear made while the block is setting the bit again may be
	 * what leaves a ring deaf, so dp.istat_clear 0 leaves the bit alone, as the vendor does, for a
	 * run against 1, which is what this driver has done.
	 */
	istat = octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_INT_STATUS);
	if ((istat & ~(1ULL << ring)) != 0)
		sc->dp_istat_others++;
	istat &= 1ULL << ring;
	if (istat != 0 && sc->dp_istat_clear == 0)
		up = istat;
	else if (istat != 0) {
		octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_INT_STATUS, istat);
		/*
		 * And look again at once (#361): a status that is set again on the read after
		 * the clear is one the block set in the same window, a packet landing as the
		 * clear went out. Counted, so a stall can be read against how often that is.
		 *
		 * And kept, with the count read below, as the state this pass leaves the ring in,
		 * for a stall to print - octep_dp_oq_service.
		 */
		up = octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_INT_STATUS) & (1ULL << ring);
		if (up != 0)
			sc->dp_istat_reset++;
	}

	cnts = octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_CNTS);
	sc->dp_oq_left_istat[ring] = up;
	sc->dp_oq_left_cnts[ring] = cnts;
	n = (uint32_t)(cnts & 0xffffffffULL);
	/*
	 * The instrument for octep_dp_oq_ahead: acknowledge one packet and do not take it, which
	 * is the fault itself - a count that no longer holds a packet the ring does - made on
	 * purpose, on one ring, as many times as dp.oq_lose says. Nothing else can show that the
	 * way back works, because what used to put a ring behind no longer does.
	 */
	lose = sc->dp_oq_lose;
	burst = sc->dp_oq_lose_burst != 0 ? sc->dp_oq_lose_burst : 1;
	if (burst > n)
		burst = n;
	if (burst > lose)
		burst = lose;
	if (n != 0 && lose != 0 && ring == sc->dp_oq_lose_ring && sc->dp_ack_cnts != 0 &&
	    atomic_cmpset_32(&sc->dp_oq_lose, lose, lose - burst) != 0) {
		/*
		 * Spent by compare and set: the sysctl can write this word while a pass is here,
		 * and a plain decrement of a word just written to zero would arm it four thousand
		 * million times. A write that gets in between wins, and this use is not made.
		 */
		/*
		 * One a pass loses no faster than the way back recovers - measured, 1,463 lost and
		 * 1,463 brought back under a stream of 1,400 a second, the ring never more than a
		 * few behind. A burst is how a ring is put a whole grant behind.
		 */
		octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_CNTS, burst);
		n -= burst;
	}
	if (n == 0) {
		/* No count, so no gap either: octep_dp_oq_resync is only ever called with one. */
		sc->dp_oq_gap[ring] = 0;
		if (sc->dp_ack_cnts != 0)
			n = octep_dp_oq_ahead(sc, bufs, ring);
		if (n == 0) {
			/* Nothing to take; but what is owed is still owed - octep_dp_oq_credit. */
			if (sc->dp_oq_carry[ring] != 0)
				octep_dp_oq_credit(sc, ring, 0);
			goto out;
		}
		/*
		 * Packets the count will never hold: as many as were there when the wait began,
		 * and no more - see octep_dp_oq_ahead. They are taken as any are, in order, and
		 * nothing is acknowledged for them, because the register has nothing of theirs to
		 * give back.
		 */
		behind = 1;
	}
	if (n > sc->dp_oq_rsize) {
		/*
		 * More counted than the ring holds: the block has been round it. The frames it
		 * wrote over are gone, the buffers no longer hold frames in the order the walk
		 * below reads them, and the count will be left too high by what was lost. With the
		 * grant derived this does not happen - see octep_dp_oq_first_grant - and it is
		 * counted so that it is known if it does.
		 */
		if (sc->dp_ack_cnts != 0)
			sc->dp_oq_lapped++;
		n = sc->dp_oq_rsize;
	}
	if (n > most)
		n = most;

	/*
	 * Take the packets, in the order the block wrote them.
	 *
	 * The block walks the scatter list; so does this, from a read index of its own. Reading
	 * buffers 0..n-1 instead happened to work while only one packet per ring ever arrived, and
	 * would have read the same buffer twice the moment more than one did.
	 *
	 * Each frame is copied into an mbuf and chained locally rather than handed up here, because
	 * the ring is held exclusive for the length of this function and the stack can take a frame
	 * a long way. Then the buffer is re-poisoned: the length word at its head is the arrival
	 * flag, and a stale one would be counted twice.
	 */
	/*
	 * R_OUT_CNTS IS NOT A COUNT OF BUFFERS THIS PASS CAN TAKE, and reading it as one is how a
	 * pass came to report work it had not done.
	 *
	 * It says how many packets the block has written and the host has not acknowledged. The
	 * two differ for an ordinary reason: the block's write is a DMA, so a packet can be
	 * counted in that register before its buffer is visible here, and a burst larger than the
	 * ring leaves a reading this pass cannot satisfy however many buffers it walks.
	 *
	 * So the register bounds the walk and the buffers decide it. A slot whose length word is
	 * still poison holds nothing yet; the block fills the scatter list in order, so every slot
	 * after it is empty too and the pass is finished. It must not advance the read index past
	 * that slot, must not re-poison it, must not credit it and must not count it.
	 *
	 * Counting it was a real defect and not a cosmetic one. The drain loop - it was in
	 * octep_dp_intr then and is in octep_dp_oq_service now -
	 * goes round until a pass returns zero, and this function used to return the register's
	 * reading whatever it found - so after a first round took RSIZE packets and re-poisoned
	 * their buffers, the next round walked those same buffers, found its own poison in every
	 * one, delivered nothing, and still reported RSIZE. That inflated dp_rx_done and
	 * dp_intr_drained, and worse, it rang R_OUT_SLIST_DBELL a second time for buffers already
	 * credited: the over-credit this file describes above as a doorbell gone past the grant it
	 * started from, which is what lets the block write where the host has not refilled.
	 */
	bus_dmamap_sync(bufs->tag, bufs->map, BUS_DMASYNC_POSTREAD);
	taken = 0;
	bufs_taken = 0;
	for (i = 0; i < n; i++) {
		struct octep_dp_if *dif;
		struct mbuf *m;
		uint8_t *b;
		uint64_t blen;
		uint32_t idx, flen, ident;
		struct octep_pf_tuple ct;	/* a FIN's or a RST's, for octep_dp_closing_hold */
		int closing = 0;
		uint16_t tag;
		uint32_t saw, span, j;
		int nolen;

		idx = sc->dp_oq_rd[ring] % sc->dp_oq_rsize;
		b = (uint8_t *)bufs->vaddr + ((size_t)idx * OCTEP_DP_BUF_STRIDE);

		/* Nothing here yet, so nothing after it either - see above. */
		blen = be64toh(*(uint64_t *)(b + OCTEP_RX_LEN_OFF));
		if (blen == 0 || blen == OCTEP_DP_BUF_POISON_WORD ||
		    blen <= OCTEP_RX_PREFIX_LEN - 8)
			break;

		/*
		 * From here the buffer is consumed whatever becomes of the frame in it. A frame this
		 * driver will not deliver - an unknown tag, a length that cannot be right, no mbuf to
		 * put it in - is still a packet the block wrote and counted, so its buffer is
		 * re-poisoned, credited and acknowledged like any other. Only the frame is dropped,
		 * and the counter for that drop is kept where the reason is known.
		 */
		/*
		 * ONE PACKET IS NOT ONE BUFFER, and taking it for one is how a ring came to be
		 * hundreds of buffers behind the block with every counter here at rest.
		 *
		 * A packet longer than a buffer is written on into the next buffer and the one
		 * after, and counted once. The vendor's host reads how many from the length word:
		 * octeon_droq_get_bufcount, built BUFPTR_ONLY_MODE - the first buffer holds the
		 * length and the response word and then OCTEP_DP_BUF_SIZE less those sixteen
		 * bytes of packet, and every later buffer is OCTEP_DP_BUF_SIZE of packet with
		 * nothing in front. The length word counts the response word, so what does not
		 * fit in the first buffer is the length plus eight, less a buffer.
		 *
		 * This walk took one buffer for each packet counted. For a packet of two buffers
		 * it took the first, dropped it for a length that could not be right, acknowledged
		 * one and returned one unit of credit; the second buffer stayed where it was. The
		 * next packet to be counted paid for it - the walk took the second buffer, whose
		 * first eight bytes are payload and no length, and dropped that too - and from
		 * then on the ring was one buffer behind for good: each count took the buffer of
		 * the packet before. See octep_dp_oq_ahead for what that looks like from outside.
		 *
		 * What sends them is the coprocessor itself. Its statistics messages - control
		 * frames, tag 254, to this ring among the eight - carry an entry for every
		 * connection that moved since the last one, eighteen bytes each, so their
		 * length follows how many connections are busy at once and no MTU: eighty or
		 * so fit a buffer and more do not. A frame from a wire never does this; the
		 * fast path refuses one longer than the port's MTU before it gets here.
		 *
		 * So the read index moves by what the packet used, each of those buffers is
		 * re-poisoned and owed its unit, and the count is acknowledged by one. A length
		 * word above OCTEP_DP_PKT_MAX, or one that would span the ring, is no length:
		 * one buffer, as before, and counted.
		 */
		span = octep_dp_oq_span(blen, sc->dp_oq_rsize);
		nolen = (span == 0);
		if (nolen != 0) {
			sc->dp_rx_long++;
			span = 1;
		}
		sc->dp_oq_rd[ring] = (idx + span) % sc->dp_oq_rsize;
		taken++;
		bufs_taken += span;
		/* Nothing in it is where a length would put it, so nothing in it is read. */
		if (nolen != 0)
			goto repoison;

		/*
		 * Keep the whole prefix of the most recent frame, for dp.rx_prefix to print.
		 *
		 * One 82-byte copy on a path that is already copying the frame itself, and it is the
		 * only way to look at the bytes the far side sends in front of every frame that this
		 * driver has never read. Issue #185 turns on them: the crypto path takes its SA index
		 * from a flow, a flow can only be programmed by (mflow_id, mflow_rev_num), and nothing
		 * published says where a host learns those - but a four-byte usfp_mflow_ident would
		 * fit here, and the vendor's own host has to get it from somewhere.
		 */
		memcpy(sc->dp_rx_prefix, b, OCTEP_RX_PREFIX_LEN);
		sc->dp_rx_prefix_seq++;
		/*
		 * Read here rather than below, where the interface lookup needs it, so the tuple
		 * capture gets the tag of its own frame. The offset is inside the prefix that was
		 * just copied, which the length check above has already established is there.
		 */
		tag = be16dec(b + OCTEP_RX_TAG_OFF);
		/*
		 * A packet of more than one buffer is gathered whole below. Its head is kept so
		 * that what arrived can be read (dp.rx_span_frame). The long ones that are not an
		 * interface's frame are the coprocessor's statistics messages - each connection's
		 * packets and bytes, counted where this host cannot see them - and since
		 * 2026-10-19 they are read: octep_dp_cmsg_rx.
		 */
		m = NULL;
		flen = (uint32_t)blen - (OCTEP_RX_PREFIX_LEN - 8);
		/*
		 * A frame the fast path handed to the inspector (discovery 4): on the inspector's
		 * function's ring, headed by usfp_dp_md rather than usfp_kn_md. It is no front port's
		 * frame, whatever its tag - the tag says where it goes once inspected - so it never
		 * reaches the stack as one, nor the flow maker.
		 */
		if (sc->dp_ips_on && ring == sc->dp_ips_ring &&
		    le32dec(b + OCTEP_RX_META_OFF) == OCTEP_RX_IPS_SIG) {
			m = octep_dp_ips_rx(sc, b, flen, span);
			if (m != NULL) {
				if (mt == NULL)
					mh = mt = m;
				else {
					mt->m_nextpkt = m;
					mt = m;
				}
			}
			goto repoison;
		}
		if (span > 1) {
			/*
			 * Since 2026-10-12 such a packet is gathered rather than counted: a frame
			 * longer than a buffer, written by the coprocessor over `span` of them in a row
			 * by the vendor's rule octep_dp_oq_span encodes - the first buffer holds the
			 * length word, the prefix and what is left of it, every later one is all
			 * frame (measured: a 9,014-byte frame from the peer's jumbo bridge arrives in
			 * six once the port's LIF allows it). It goes into one jumbo cluster, so that
			 * everything downstream - the termination of a decrypted frame, the flow
			 * candidate, the stack - sees the one contiguous copy it has always seen. The
			 * extra buffers are poisoned and credited at repoison, as before.
			 */
			const uint8_t *bj;
			uint8_t *pkt;
			uint32_t off, chunk;

			sc->dp_rx_spans++;
			sc->dp_rx_span_bufs += span - 1;
			sc->dp_rx_span_len = (uint32_t)blen;
			if ((uint32_t)blen > sc->dp_rx_span_max)
				sc->dp_rx_span_max = (uint32_t)blen;
			sc->dp_rx_span_ring = ring;
			sc->dp_rx_span_tag = tag;
			memcpy(sc->dp_rx_span_frame, b, sizeof(sc->dp_rx_span_frame));
			if (flen < ETHER_HDR_LEN || flen > OCTEP_DP_IF_MTU_MAX + ETHER_HDR_LEN + 4)
				goto repoison;
			m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR,
			    flen <= MJUM9BYTES ? MJUM9BYTES : MJUM16BYTES);
			if (m == NULL) {
				sc->dp_rx_span_nobuf++;
				goto repoison;
			}
			pkt = mtod(m, uint8_t *);
			chunk = OCTEP_DP_BUF_SIZE - OCTEP_RX_PREFIX_LEN;
			if (chunk > flen)
				chunk = flen;
			memcpy(pkt, b + OCTEP_RX_PREFIX_LEN, chunk);
			off = chunk;
			for (j = 1; j < span && off < flen; j++) {
				bj = (const uint8_t *)bufs->vaddr +
				    ((size_t)((idx + j) % sc->dp_oq_rsize) * OCTEP_DP_BUF_STRIDE);
				chunk = flen - off < OCTEP_DP_BUF_SIZE ? flen - off : OCTEP_DP_BUF_SIZE;
				memcpy(pkt + off, bj, chunk);
				off += chunk;
			}
			m->m_len = m->m_pkthdr.len = flen;
		}
		/*
		 * And the microflow this frame names, decoded here out of THIS ring's own buffer
		 * rather than out of the shared capture copy. The ident is id:25, rev:6, valid:1;
		 * the valid bit is not kept, because the capture's sequence number already says
		 * whether anything was captured at all.
		 *
		 * It is read here and not in octep_dp_rx_tuple because `b` is this servicer's and
		 * sc->dp_rx_prefix is every servicer's. Decoding it from the shared copy is how a
		 * candidate came to be able to carry one frame's slot with another frame's
		 * addresses.
		 */
		ident = le32dec(b + OCTEP_RX_MD_FLOW_OFF);
		/*
		 * And the association, from the same block: kn_md.sa_index is the handle of the
		 * association that decrypted this frame on the way in, or 0. Such a frame is still
		 * ESP on the outside and is terminated by octep_ipsec_rx below, so it is captured
		 * here and not parsed as a candidate.
		 */
		saw = le32dec(b + OCTEP_RX_MD_SA_OFF);
		if ((saw & 0xffffu) != 0)
			octep_dp_rx_capture(sc, tag, m != NULL ? mtod(m, const uint8_t *) :
			    b + OCTEP_RX_PREFIX_LEN, flen);
		else
			closing = octep_dp_rx_tuple(sc, tag, ident & 0x01ffffffu, (ident >> 25) & 0x3fu,
			    m != NULL ? mtod(m, const uint8_t *) : b + OCTEP_RX_PREFIX_LEN, flen, &ct);

		/*
		 * The length counts everything after the first qword, and the Ethernet header
		 * starts at OCTEP_RX_PREFIX_LEN - so the frame is the length less the rest of the
		 * prefix. Measured on the frame that first completed the loop: length 134, prefix
		 * 82, and a 60-byte frame behind it.
		 */
		/*
		 * Bound it by what is left AFTER the prefix, not by the buffer (a gathered frame
		 * was bounded above, by the largest MTU a port may have).
		 *
		 * The copy below starts at b + OCTEP_RX_PREFIX_LEN, so a frame may be at most
		 * OCTEP_DP_BUF_SIZE - OCTEP_RX_PREFIX_LEN bytes - 1520 here, not 1602. Bounding it
		 * by the whole buffer let a long length read up to 82 bytes past the end of the DMA
		 * buffer and into whatever the next one holds, which is the far side's length field
		 * deciding how far this host reads. OCTEP_DP_IF_MTU_MAX is the same arithmetic and
		 * was already right; this site was not.
		 */
		if (m == NULL && (flen < ETHER_HDR_LEN ||
		    flen > OCTEP_DP_BUF_SIZE - OCTEP_RX_PREFIX_LEN))
			goto repoison;

		dif = octep_dp_if_by_tag(sc, tag);
		if (dif == NULL || dif->ifp == NULL) {
			/* the coprocessor's own messages: read before the copy goes */
			if (tag == OCTEP_CMSG_PORT_TAG)
				octep_dp_cmsg_rx(sc, m != NULL ? mtod(m, const uint8_t *) :
				    b + OCTEP_RX_PREFIX_LEN, flen);
			if (m != NULL) {
				m_freem(m);
				m = NULL;
			}
			sc->dp_rx_untagged++;
			if (tag != OCTEP_CMSG_PORT_TAG)
				sc->dp_rx_untagged_other++;
			/*
			 * Keep this one. A frame with a tag no interface owns is, on this appliance,
			 * either a stray or a frame the fast path was asked to forward to the host's
			 * own DPDK port - and the second is the only way to read what the fast path
			 * actually builds, because a frame forwarded to a front port never comes here.
			 */
			if (sc->dp_rx_untag_want == OCTEP_DP_UNTAG_WANT_OTHER ?
			    tag == OCTEP_CMSG_PORT_TAG :
			    (sc->dp_rx_untag_want != 0 && sc->dp_rx_untag_want != (uint32_t)tag))
				goto repoison;
			sc->dp_rx_untag_tag = tag;
			sc->dp_rx_untag_len = flen < sizeof(sc->dp_rx_untag_frame) ?
			    flen : (uint32_t)sizeof(sc->dp_rx_untag_frame);
			/*
			 * From the START of the buffer, not past the prefix.
			 *
			 * A frame the fast path forwards to the host does NOT carry the 82-byte punt
			 * prefix: prep_mbuf_for_port prepends a cvmcs_resp_hdr_t instead, which is
			 * shorter, so everything this driver reads at a fixed offset lands in the
			 * wrong place. It showed as a tag of 0xfc10 - which is not a tag at all but
			 * bytes 2 and 3 of this appliance's own MAC, read out of an Ethernet header
			 * that was not where the offset said. Capturing from the buffer start shows
			 * the header and the frame behind it, and lets the layout be read rather than
			 * assumed.
			 */
			sc->dp_rx_untag_len = blen < sizeof(sc->dp_rx_untag_frame) ?
			    (uint32_t)blen : (uint32_t)sizeof(sc->dp_rx_untag_frame);
			memcpy(sc->dp_rx_untag_frame, b, sc->dp_rx_untag_len);
			goto repoison;
		}
		if (m == NULL) {
			m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
			if (m == NULL) {
				dif->rx_nobuf++;
				if_inc_counter(dif->ifp, IFCOUNTER_IQDROPS, 1);
				goto repoison;
			}
			memcpy(mtod(m, void *), b + OCTEP_RX_PREFIX_LEN, flen);
			m->m_len = m->m_pkthdr.len = flen;
		}
		m->m_pkthdr.rcvif = dif->ifp;
		m->m_nextpkt = NULL;
		if ((saw & 0xffffu) != 0) {
			/*
			 * Decrypted in place by the coprocessor: terminated and delivered by
			 * octep_ipsec_rx, which consumes the mbuf, so it never joins the chain the
			 * stack's input sees - that chain would hand an ESP frame with a plaintext
			 * payload to esp_input, which drops it as a bad authentication.
			 */
			dif->rx_packets++;
			dif->rx_bytes += flen;
			if_inc_counter(dif->ifp, IFCOUNTER_IPACKETS, 1);
			octep_ipsec_rx(sc, dif, m, saw, ident);
			goto repoison;
		}
		if (closing && octep_dp_closing_hold(sc, m, &ct)) {
			/*
			 * A FIN or a RST of a connection in hardware whose window the fast path checks:
			 * held, and handed up by octep_dp_closing_task once pf has the fast path's numbers
			 * to judge it by (issue #679).
			 */
		} else if (mt == NULL)
			mh = mt = m;
		else {
			mt->m_nextpkt = m;
			mt = m;
		}
		dif->rx_packets++;
		dif->rx_bytes += flen;
		/*
		 * IPACKETS here, IBYTES nowhere.
		 *
		 * ether_input_internal() adds the received byte count itself, guarded on
		 * IFCAP_HWSTATS, which this driver does not claim - so counting it here as well
		 * doubled it. A ping flood of 30,000 full-size frames charged this interface
		 * 3,026 bytes for each 1,514-byte frame on the wire, which is how it was found,
		 * and a firewall whose byte counters read double is worse than one with none.
		 *
		 * The stack counts the mbuf's own length, which is the frame as the stack sees
		 * it. dif->rx_bytes above is this driver's separate figure and stays, because
		 * dp.stats is about the ring rather than about the interface.
		 *
		 * The packet count is ours to keep: that file increments no IPACKETS.
		 */
		if_inc_counter(dif->ifp, IFCOUNTER_IPACKETS, 1);
repoison:
		memset(b, OCTEP_DP_BUF_POISON, OCTEP_DP_BUF_SIZE);
		if (span > 1) {
			/*
			 * The buffers the packet went on into. The vendor's host reads a register
			 * before it copies out of them, and its comment gives the reason: a read
			 * of a register lets the writes still on their way land first. Nothing is
			 * copied here, but a write that landed after the poison would leave a
			 * buffer that looks written to everything that scans the ring. So the
			 * same read is made first.
			 */
			(void)octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_CNTS);
			for (j = 1; j < span; j++) {
				b = (uint8_t *)bufs->vaddr +
				    ((size_t)((idx + j) % sc->dp_oq_rsize) * OCTEP_DP_BUF_STRIDE);
				memset(b, OCTEP_DP_BUF_POISON, OCTEP_DP_BUF_SIZE);
			}
		}
	}
	bus_dmamap_sync(bufs->tag, bufs->map, BUS_DMASYNC_PREREAD);

	/*
	 * Hand the buffers back: one unit for each taken, which is what the block spent - and never
	 * past the grant the ring was armed with.
	 *
	 * The ceiling is older than the unit being right. Sixteen were returned for every packet,
	 * which raised a ring's credit by fifteen for every packet it carried: a ring that had
	 * carried a day's downloads held 1,026,112, and with that much the block wrote over buffers
	 * the host had not taken. So the register is read and the credit cut to what brings it back
	 * to the grant - which with one unit to a buffer it should never need, and what it does cut
	 * is carried: octep_dp_oq_credit.
	 */
	if (taken == 0) {
		/*
		 * A count and no buffer. With dp.ack_cnts 0 that is every last pass of every visit -
		 * the register is never brought back to zero - and says nothing about the ring.
		 */
		if (behind == 0) {
			if (sc->dp_ack_cnts != 0) {
				sc->dp_oq_unseen++;
				*unseen = 1;
			}
			octep_dp_oq_resync(sc, bufs, ring, n);
		}
		if (sc->dp_oq_carry[ring] != 0)
			octep_dp_oq_credit(sc, ring, 0);
		goto out;
	}
	sc->dp_oq_gap[ring] = 0;
	sc->dp_oq_ahead[ring] = 0;
	/* One unit for each BUFFER, which a packet may have used several of. */
	octep_dp_oq_credit(sc, ring, bufs_taken);

	/*
	 * Acknowledging the count is this driver's invention, not the vendor's.
	 *
	 * The vendor's octeon_droq_check_hw_for_pkts reads R_OUT_CNTS, subtracts a shadow it keeps
	 * beside the ring, and writes the register back only when the reading passes 0xf0000000 -
	 * so on a working host it is written perhaps never. The register is free-running, and its
	 * top bits are not count at all: a freshly reset ring reads 0x2000000000000000 with the low
	 * 32 bits clear. Writing a bare packet count therefore also writes zeros over those bits.
	 *
	 * Whether that matters is the next thing to measure, so it is a switch rather than an
	 * opinion.
	 */
	if (behind != 0) {
		/* Taken without a count: there is nothing of theirs to acknowledge. */
		sc->dp_oq_behind += taken;
		sc->dp_oq_behind_runs++;
	} else if (sc->dp_ack_cnts != 0)
		octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_CNTS, taken);
	rc = (int)taken;
out:
	/*
	 * Hand the frames up while the ring is still held; the caller releases it when its visit
	 * ends.
	 *
	 * These mbufs carry pointers to ifnets in their rcvif, so for as long as this loop is running
	 * those interfaces must not be freed under it. The busy flag is what octep_dp_rx_quiesce()
	 * waits on before an interface is detached, so this loop has to be inside it or that wait is
	 * watching the wrong thing - which it was.
	 *
	 * It used to be the other way round, with the flag released first so the ring was free while
	 * the stack worked. That costs nothing to give up: the buffers were re-poisoned and the
	 * doorbell credited above, so the block has its credits back before this line either way, and
	 * the only thing held out is another servicer of this same ring.
	 */
	/*
	 * And hand them up inside the network epoch, because if_input requires it.
	 *
	 * This is not a formality. if.c asserts NET_EPOCH_ASSERT() in the paths a frame reaches from
	 * here, so on a kernel built with INVARIANTS this panics, and on one without it the stack
	 * walks interface and address lists that are only safe to read while the epoch is held - a
	 * use-after-free that waits for an interface to be reconfigured under live traffic rather
	 * than failing when it is written.
	 *
	 * It has to be here rather than further out because three paths reach it and none is
	 * in the epoch already: the receive watchdog's callout, the MSI-X handler, and a sysctl.
	 * The delivery point is the thing that has the requirement, so it is the thing that meets it.
	 *
	 * No lock is held across this - the ring is guarded by its own dp_oq_busy flag and not by
	 * sc->mtx - so entering the epoch here adds no new ordering.
	 */
	NET_EPOCH_ENTER(et);
	while (mh != NULL) {
		m = mh;
		mh = m->m_nextpkt;
		m->m_nextpkt = NULL;
		if_input(m->m_pkthdr.rcvif, m);
	}
	NET_EPOCH_EXIT(et);
	return ((uint32_t)rc);
}

/*
 * How many times a ring's MSI-X handler has run, or 0 if the ring has no vector.
 */
static uint64_t
octep_dp_ring_vcount(struct octep_softc *sc, uint32_t ring)
{
	uint32_t i;

	for (i = 0; i <= OCTEP_DP_SIBLINGS_MAX; i++)
		if (sc->dp_vec[i].res != NULL && sc->dp_vec[i].ring == ring)
			return (sc->dp_vec[i].count);
	return (0);
}

/*
 * Visit one output ring: hold it, take everything in it, and leave it empty or spoken for.
 *
 * The block sends a ring's MSI-X message when R_OUT_CNTS crosses the level, not while it sits
 * above it. So a ring left with packets in it, and nobody inside, is a ring nothing will look at
 * until the watchdog does. This file took that for something only the handler could do, by
 * stopping after one pass, and it was the other servicers that did it: see octep_dp_rxwd. Three
 * rules close it, and each of them is here because of a way a ring was left.
 *
 * ONE SERVICER PER RING AT A TIME, FOR ITS WHOLE VISIT. Until there were interrupts there was only
 * ever one path in here, so this did not matter. Now a ring's MSI-X handler, the watchdog and a
 * sysctl-driven sweep can all arrive, and two of them inside one ring both read R_OUT_CNTS and
 * both return credits for what they read - which double-counts the packets and over-credits the
 * block. Measured before the flag: 523 packets reported for 400 frames, and a doorbell that had
 * gone above the grant it started from. The flag used to be taken and given back around each
 * pass, and the gap between two passes of the handler is where the watchdog came in.
 *
 * A VISIT GOES ROUND UNTIL A PASS TAKES NOTHING, whoever is making it. A pass reads R_OUT_CNTS,
 * takes at most RSIZE packets and acknowledges those; frames land while it hands its own up the
 * stack, and they do not cross the level if the count never fell below it. A servicer that reads
 * a count of zero leaves the count below the level, and from there the block's own rule brings a
 * servicer back: the count crossing the level, or the block's timer running out on a packet that
 * waits below it. One that made a single pass has left it wherever the pass did.
 *
 * A SERVICER THAT IS TURNED AWAY SAYS SO, AND THE HOLDER LOOKS. Between a holder's last reading of
 * the count and its giving the flag back a packet can arrive, raise the interrupt, and have the
 * handler find the ring held: the interrupt is spent and the packet is in a ring nobody is
 * inside. So the one turned away leaves a note and looks at the flag once more - if the holder has
 * gone in between, the note is its own to honour - and the holder reads the note after it has let
 * go, and goes in again if there is one. Both writes are locked operations and not plain stores:
 * each side writes and then reads what the other writes, and with plain stores both can read the
 * old value. The note is taken down before each pass and not once a visit, because a pass that
 * begins after a note was left has read the count after it too, and is the look the note asked
 * for.
 *
 * And a visit that ends with work it knows about - it reached its bound, or the register counted
 * a packet whose buffer could not be read yet - asks the watchdog for the next tick, because
 * nothing else is coming for that ring.
 *
 * A VISIT THAT LEAVES THE STATUS UP IS COUNTED, AND THAT IS ALL (#361). The reading was that a
 * status a visit left up gives the next packet no edge to raise a message on. Measured against it,
 * 2026-10-21 on the first XGS 3300: under bursts of 64 datagrams on eight streams, half of all
 * visits - 29,718 in a minute, against 58,003 interrupts - ended with nothing to take and the status
 * still set right after the last pass's clear, and the interrupts went on coming and no ring was
 * left. A ring is left when the block raises its interrupt and the host never runs the handler -
 * see octep_dp_rxpeek_ring.
 *
 * The bound is in packets and it is the caller's: the handler is the servicer a busy ring should
 * have and is given sixteen rings' worth, and the watchdog and the sysctl, which run where other
 * things are waiting their turn, one ring's worth - what their single pass could always take. A
 * pass is told what is left of it and takes no more.
 *
 * A ring index is bounded by the sibling array, so this needs no lock of its own. Returns how
 * many packets the visit took, and in *first, when it is asked for, how many the first pass it
 * made did - none, if the ring was empty or held: that is what was waiting when the servicer
 * arrived.
 */
static uint32_t
octep_dp_oq_service(struct octep_softc *sc, struct octep_dma *bufs, uint32_t ring,
    uint32_t budget, uint32_t *first, int stamp)
{
	uint32_t n, rounds, total;
	int bound, empty, looked, unseen, up;

	if (first != NULL)
		*first = 0;
	if (ring > OCTEP_DP_SIBLINGS_MAX)
		return (0);
	if (budget == 0)
		budget = 1;
	total = 0;
	looked = 0;
	for (;;) {
		if (atomic_cmpset_int(&sc->dp_oq_busy[ring], 0, 1) == 0) {
			atomic_set_int(&sc->dp_oq_owed[ring], 1);
			if (atomic_load_acq_int(&sc->dp_oq_busy[ring]) != 0) {
				sc->dp_oq_declined++;
				return (total);
			}
			continue;
		}

		/*
		 * AFTER the flag is taken, not before. Before it, a servicer could pass this test and
		 * acquire the flag after octep_dp_rx_quiesce() had finished waiting on it - which is
		 * exactly the servicer the quiesce exists to exclude.
		 *
		 * And it is given back the way a visit gives it back, and the quiesce looked at again.
		 * A resume can lift the quiesce between that test and this line, come to this ring,
		 * find it held by a servicer that is about to leave without having looked, and go: so
		 * the one leaving looks. If the quiesce is still there, resume's visit has yet to
		 * come and finds the ring free.
		 */
		if (atomic_load_acq_int(&sc->dp_rx_quiesce) != 0) {
			(void)atomic_swap_int(&sc->dp_oq_busy[ring], 0);
			if (atomic_load_acq_int(&sc->dp_rx_quiesce) != 0)
				return (total);
			continue;
		}

		unseen = 0;
		bound = 0;
		empty = 0;
		for (rounds = 0;; rounds++) {
			(void)atomic_readandclear_int(&sc->dp_oq_owed[ring]);
			n = octep_dp_oq_pass(sc, bufs, ring, budget - total, &unseen);
			if (first != NULL && looked == 0)
				*first = n;
			looked = 1;
			if (n == 0) {
				empty = 1;
				break;
			}
			total += n;
			if (rounds != 0)
				sc->dp_intr_drained++;
			if (total >= budget) {
				bound = 1;
				break;
			}
			/* A quiesce is waiting on this flag; do not make it wait for the bound. */
			if (atomic_load_acq_int(&sc->dp_rx_quiesce) != 0)
				break;
		}
		/*
		 * When somebody other than the watchdog was last inside: the watchdog's reading of
		 * a stall asks. Its own visits are not stamped, or a ring it alone was carrying,
		 * tick after tick, would never be one that nobody had been inside.
		 */
		if (stamp != 0)
			sc->dp_oq_seen[ring] = ticks;
		/*
		 * The visit found the ring empty, or has to leave it. A packet that lands between the
		 * last pass's read of the count and this line raises nothing by itself when the
		 * count it lands on is not below the level - the block raises on the level being
		 * crossed - and such a ring waited for the watchdog: three times in the hour of
		 * 2026-10-12 with the trigger on, on rings that had been idle, 32 to 92 packets each
		 * (#361). The vendor's host asks the block to look again at the end of every poll,
		 * and so does this: RESEND alone, no count bits, after the acknowledgements above.
		 * dp.resend 2 writes it to the ring's R_IN_CNTS as well, which the vendor's host does
		 * too, and this driver otherwise never: the ring's one vector serves both directions.
		 */
		if (sc->dp_resend != 0) {
			octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_CNTS, OCTEP_SDP_R_OUT_CNTS_RESEND);
			if (sc->dp_resend >= 2)
				octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_IN_CNTS,
				    OCTEP_SDP_R_OUT_CNTS_RESEND);
			sc->dp_resend_writes++;
		}
		/* Read while the ring is still held: the next visit writes these. */
		up = (empty != 0 && unseen == 0 && sc->dp_oq_left_istat[ring] != 0);
		sc->dp_oq_left_at[ring] = ticks;
		sc->dp_oq_left_vcount[ring] = octep_dp_ring_vcount(sc, ring);
		(void)atomic_swap_int(&sc->dp_oq_busy[ring], 0);

		/* Resume services every ring itself, and arms the watchdog. */
		if (atomic_load_acq_int(&sc->dp_rx_quiesce) != 0)
			return (total);
		if (up != 0)
			sc->dp_oq_left_up++;
		if (bound != 0 || unseen != 0) {
			if (bound != 0)
				sc->dp_oq_bound++;
			octep_dp_rxwd_kick(sc, ring);
			return (total);
		}
		if (atomic_load_acq_int(&sc->dp_oq_owed[ring]) == 0)
			return (total);
		sc->dp_oq_handed++;
	}
}

/*
 * The ring's MSI-X vector as the host sees it (#361): the table entry's mask bit, the PBA's
 * pending bit and how many times its handler has run. Read before a watchdog visit and kept with a
 * stall: a status the block has set, with the pending bit up and the mask down and no handler
 * entry, is a message the host never took; the pending bit down with the status set is a message
 * the block never sent. The table and PBA are the kernel's mapping of them (pci_alloc_msix made
 * it), reached as pci_mask_msix and pci_pending_msix reach them.
 */
static void
octep_dp_vec_state(struct octep_softc *sc, uint32_t ring, uint32_t *index, uint32_t *mask,
    uint32_t *pending, uint64_t *count)
{
	struct pci_devinfo *dinfo;
	struct pcicfg_msix *msix;
	struct octep_dp_vec *vec;
	uint32_t i;

	*index = 0xffffffff;
	*mask = *pending = 0;
	*count = 0;
	for (i = 0; i <= OCTEP_DP_SIBLINGS_MAX; i++) {
		vec = &sc->dp_vec[i];
		if (vec->res == NULL || vec->ring != ring)
			continue;
		*index = (uint32_t)vec->rid - 1;
		*count = vec->count;
		break;
	}
	if (*index == 0xffffffff)
		return;
	dinfo = device_get_ivars(sc->dev);
	msix = &dinfo->cfg.msix;
	if (msix->msix_table_res != NULL && *index < msix->msix_table_len)
		*mask = (bus_read_4(msix->msix_table_res,
		    msix->msix_table_offset + *index * 16 + 12) & PCIM_MSIX_VCTRL_MASK) != 0;
	if (msix->msix_pba_res != NULL && *index < msix->msix_table_len)
		*pending = pci_pending_msix(sc->dev, *index) != 0;
}

/*
 * The rest of the ring's MSI-X table entry (#361): the address and data the block writes its
 * message with, as the table holds them, against what the kernel put there (pci_enable_msix, and
 * again when bus_bind_intr moved the vector). A ring that stays deaf with its vector unmasked and
 * nothing pending is either a block that sends nothing or a block that sends somewhere else; the
 * table is the block's own, in its BAR, and so something other than the host could have written it.
 */
static void
octep_dp_vec_entry(struct octep_softc *sc, uint32_t index, uint64_t *addr, uint32_t *data,
    uint32_t *match)
{
	struct pci_devinfo *dinfo;
	struct pcicfg_msix *msix;
	struct msix_vector *mv;
	u_int v;

	*addr = 0;
	*data = 0;
	*match = 2;
	dinfo = device_get_ivars(sc->dev);
	msix = &dinfo->cfg.msix;
	if (msix->msix_table_res == NULL || index >= msix->msix_table_len)
		return;
	*addr = bus_read_4(msix->msix_table_res, msix->msix_table_offset + index * 16) |
	    ((uint64_t)bus_read_4(msix->msix_table_res, msix->msix_table_offset + index * 16 + 4) << 32);
	*data = bus_read_4(msix->msix_table_res, msix->msix_table_offset + index * 16 + 8);
	if (msix->msix_table == NULL || msix->msix_vectors == NULL)
		return;
	v = msix->msix_table[index].mte_vector;
	if (v == 0 || v > msix->msix_alloc)
		return;
	mv = &msix->msix_vectors[v - 1];
	*match = (mv->mv_address == *addr && mv->mv_data == *data);
}

/*
 * The look's try at a deaf ring (#361), made before its visit, on a ring that holds packets past
 * the level with the count's interrupt bit set - a message the block has raised and the host has
 * not had. Each mode is one thing the vendor's host or the PCI spec says should bring it:
 *
 *   1  RESEND alone on R_OUT_CNTS. Measured before only on rings the visit had just emptied
 *      (dp.resend), where by its definition it raises nothing; never on a ring that holds packets.
 *   2  the ring's own bit of R_OUT_INT_STATUS cleared, the packets left where they are.
 *   3  the entry masked and unmasked: a message held while masked is sent on the unmask.
 *
 * Then dp.look_wake_us for the handler, and what the vector says after. A handler entry in that
 * time is a block that could still send and had missed an edge; none, with the entry the kernel's,
 * is a block that is not sending to this vector at all. The handler, if it runs, takes the ring,
 * and the visit after finds less: that is the answer, not a fault.
 */
static void
octep_dp_look_wake(struct octep_softc *sc, uint32_t ring, uint32_t vindex, uint32_t *pending,
    uint64_t *vcount)
{
	struct pci_devinfo *dinfo;
	struct pcicfg_msix *msix;
	uint32_t i, m, off;
	uint64_t before;

	octep_dp_vec_state(sc, ring, &i, &m, pending, &before);
	*vcount = before;
	switch (sc->dp_look_wake) {
	case 1:
		octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_CNTS, OCTEP_SDP_R_OUT_CNTS_RESEND);
		break;
	case 2:
		octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_INT_STATUS, 1ULL << ring);
		break;
	case 3:
		dinfo = device_get_ivars(sc->dev);
		msix = &dinfo->cfg.msix;
		if (msix->msix_table_res == NULL || vindex >= msix->msix_table_len)
			return;
		off = msix->msix_table_offset + vindex * 16 + 12;
		/* Put back as it was: an entry the kernel had masked stays masked. */
		m = bus_read_4(msix->msix_table_res, off);
		bus_write_4(msix->msix_table_res, off, m | PCIM_MSIX_VCTRL_MASK);
		bus_write_4(msix->msix_table_res, off, m);
		break;
	default:
		return;
	}
	sc->dp_look_wakes++;
	DELAY(sc->dp_look_wake_us != 0 && sc->dp_look_wake_us <= 1000 ? sc->dp_look_wake_us : 50);
	octep_dp_vec_state(sc, ring, &i, &m, pending, vcount);
	if (*vcount != before)
		sc->dp_look_woke++;
}

/*
 * One ring's turn in a sweep: a visit of one ring's worth, and for the watchdog the reading of it.
 *
 * What the watchdog finds in a ring is sorted by whether that ring had asked for the visit. Work
 * in a ring that asked is the arrangement working. Work in a ring that had not is a ring that had
 * packets and no servicer inside at that moment, which on a loaded ring happens with nothing
 * wrong; and enough of it in the first pass, in a ring nobody had been inside for two ticks - see
 * OCTEP_DP_RXWD_STALL_PKTS - is a ring that had been left. The request and the time of the last
 * visit are read before this one, so a request made during it stands for the next.
 */
static uint32_t
octep_dp_oq_sweep(struct octep_softc *sc, struct octep_dma *bufs, uint32_t ring, int watchdog)
{
	uint64_t cnts, istat, levels, vcount, listat, lcnts, lvcount, maddr, wvcount;
	uint32_t first, n, stall, vindex, vmask, vpending, mdata, mmatch, wake, wpending;
	u_int asked;
	int idle, lat;

	if (ring > OCTEP_DP_SIBLINGS_MAX)
		return (0);
	asked = 1;
	if (watchdog == 1)
		asked = atomic_readandclear_int(&sc->dp_oq_asked[ring]);
	idle = ticks - sc->dp_oq_seen[ring];
	/*
	 * What the block says about the ring before the visit clears any of it: the latched
	 * interrupt status, the count and the levels. With them a stall says whether the block
	 * never raised the interrupt (status clear, count above the level) or raised one that
	 * did not arrive (status set) - the question #361 turns on. Three reads a visit, on the
	 * watchdog's visits only.
	 */
	istat = cnts = levels = vcount = maddr = wvcount = 0;
	vindex = vmask = vpending = mdata = wake = wpending = 0;
	mmatch = 2;
	/* And how the visit before left it, which this one is about to overwrite. */
	listat = sc->dp_oq_left_istat[ring];
	lcnts = sc->dp_oq_left_cnts[ring];
	lat = sc->dp_oq_left_at[ring];
	lvcount = sc->dp_oq_left_vcount[ring];
	if (watchdog != 0) {
		istat = octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_INT_STATUS);
		cnts = octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_CNTS);
		levels = octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_INT_LEVELS);
		octep_dp_vec_state(sc, ring, &vindex, &vmask, &vpending, &vcount);
		if (vindex != 0xffffffff) {
			octep_dp_vec_entry(sc, vindex, &maddr, &mdata, &mmatch);
			if (mmatch == 0)
				sc->dp_msix_moved++;
		}
		/* A ring the block has raised and the host has not had: try to wake it first. */
		if (watchdog == 2 && sc->dp_look_wake != 0 && vindex != 0xffffffff &&
		    (uint32_t)cnts > sc->dp_intr_pkt && (cnts & OCTEP_SDP_R_OUT_CNTS_OUT_INT) != 0) {
			wake = sc->dp_look_wake;
			octep_dp_look_wake(sc, ring, vindex, &wpending, &wvcount);
		}
	}
	n = octep_dp_oq_service(sc, bufs, ring, sc->dp_oq_rsize, &first, watchdog == 0);
	if (watchdog == 2) {
		/* The look's visit: every one is kept, with the registers as it found them. */
		struct octep_dp_stall *st;

		sc->dp_rxpeek_taken += n;
		st = &sc->dp_look_log[sc->dp_look_pos % OCTEP_DP_STALL_LOG];
		st->at = ticks;
		st->ring = ring;
		st->first = first;
		st->idle = (uint32_t)idle;
		st->visit = n;
		st->istat = istat;
		st->cnts = cnts;
		st->levels = levels;
		st->vec_index = vindex;
		st->msix_mask = vmask;
		st->msix_pending = vpending;
		st->vec_count = vcount;
		st->left_istat = listat;
		st->left_cnts = lcnts;
		st->left_ago = (uint32_t)(ticks - lat);
		st->left_vcount = lvcount;
		st->msix_addr = maddr;
		st->msix_data = mdata;
		st->msix_match = mmatch;
		st->wake = wake;
		st->wake_pending = wpending;
		st->wake_vcount = wvcount;
		sc->dp_look_pos++;
		return (n);
	}
	if (n != 0 && asked == 0) {
		sc->dp_rxwd_rescues++;
		stall = 2 * sc->dp_intr_pkt;
		if (stall < OCTEP_DP_RXWD_STALL_PKTS)
			stall = OCTEP_DP_RXWD_STALL_PKTS;
		if (first >= stall && idle >= OCTEP_DP_RXWD_STALL_TICKS) {
			struct octep_dp_stall *st;

			/*
			 * Kept with the count. Three builds counted 0, 2 and 2 of these over twenty million
			 * frames of the same suites, and the count alone could not say which ring, how much
			 * was waiting or how long nobody had been inside - which is what tells a tick that
			 * landed on a burst from a ring that was left. dp.rxwd_stall_log prints them.
			 */
			st = &sc->dp_stall_log[sc->dp_stall_pos % OCTEP_DP_STALL_LOG];
			st->at = ticks;
			st->ring = ring;
			st->first = first;
			st->idle = (uint32_t)idle;
			st->visit = n;
			st->istat = istat;
			st->cnts = cnts;
			st->levels = levels;
			st->vec_index = vindex;
			st->msix_mask = vmask;
			st->msix_pending = vpending;
			st->vec_count = vcount;
			st->left_istat = listat;
			st->left_cnts = lcnts;
			st->left_ago = (uint32_t)(ticks - lat);
			st->left_vcount = lvcount;
			st->msix_addr = maddr;
			st->msix_data = mdata;
			st->msix_match = mmatch;
			st->wake = 0;
			st->wake_pending = 0;
			st->wake_vcount = 0;
			sc->dp_stall_pos++;
			sc->dp_rxwd_stalls++;
		}
	}
	return (n);
}

/*
 * Service every armed ring. Which ring a frame lands on is the fast path's choice, not the host's,
 * so all of them have to be served and not only the one transmit uses.
 *
 * One ring's worth from each. The watchdog says it is the watchdog, and has what it finds counted.
 */
static uint32_t
octep_dp_service_all(struct octep_softc *sc, int watchdog)
{
	uint32_t i, done;

	if (sc->dp_up == 0)
		return (0);

	done = octep_dp_oq_sweep(sc, &sc->dp_bufs, sc->dp_ring, watchdog);
	for (i = 0; i < OCTEP_DP_SIBLINGS_MAX; i++) {
		struct octep_dp_oq *oq = &sc->dp_sib[i];

		if (oq->armed != 0)
			done += octep_dp_oq_sweep(sc, &oq->bufs, oq->ring, watchdog);
	}
	sc->dp_rx_done += done;
	return (done);
}

uint32_t
octep_dp_service(struct octep_softc *sc)
{

	return (octep_dp_service_all(sc, 0));
}

/*
 * The look at one ring (#361): its head buffer, in host memory, and a visit if the same packet has
 * been at the head since the last look and nobody is inside.
 *
 * WHY. The block raises a ring's interrupt and the host does not always get it. Measured on the
 * second XGS 3300, 2026-10-21, with every visit noting how it left the ring: ring 7 left with its
 * status clear and a count of nothing, and 34 ticks later 44 packets in it, R_OUT_INT_STATUS set,
 * the count's interrupt bit (62) set, the vector unmasked, nothing pending in the PBA - and the
 * handler's entry count where it had been when the ring was left. The block had raised it; no
 * message reached the host, and none was going to. And again with each ring's own bit cleared and
 * each ring's vector on a CPU of its own - ring 5 on cpu5, left clean by the sweep 50 ticks before
 * and holding 81 packets - so it is not the other rings' clears and not one CPU's queue. The host
 * does not write to a ring it has left, so what it can do is not wait: the watchdog's sweep found
 * such a ring after up to a period, 50 ms, and this finds it after one or two ticks.
 *
 * It finds a second thing as well: a lone packet. The count interrupt is raised when the count goes
 * past the level, so with a level of one a single packet raises nothing and waits for the time
 * threshold - read at a look's visit as a count of 1, the count's interrupt bit clear and the ring's
 * status bit clear (cnts 0x800000001). A tick is less than that wait.
 *
 * It reads no register, which is what lets it run every tick: the length word at the head of the
 * ring is in host memory, written by the block's DMA, poison until a packet lands. A packet that
 * landed a moment ago has its interrupt on the way, and the handler takes it within microseconds,
 * so the look asks for the same head twice - one look apart - before it goes in. The read index and
 * the length word are read without the ring: what they say is a hint, and the visit is made the
 * way every visit is, holding it.
 */
static uint32_t
octep_dp_rxpeek_ring(struct octep_softc *sc, struct octep_dma *bufs, uint32_t ring)
{
	uint64_t blen;
	uint32_t idx;
	uint8_t *b;

	if (ring > OCTEP_DP_SIBLINGS_MAX || bufs->vaddr == NULL)
		return (0);
	idx = *(volatile uint32_t *)&sc->dp_oq_rd[ring];
	if (idx >= sc->dp_oq_rsize)
		return (0);
	b = (uint8_t *)bufs->vaddr + ((size_t)idx * OCTEP_DP_BUF_STRIDE);
	blen = be64toh(*(volatile uint64_t *)(b + OCTEP_RX_LEN_OFF));
	if (blen == 0 || blen == OCTEP_DP_BUF_POISON_WORD || blen <= OCTEP_RX_PREFIX_LEN - 8) {
		sc->dp_rxpeek_head[ring] = 0;
		return (0);
	}
	if (sc->dp_rxpeek_head[ring] != idx + 1 ||
	    atomic_load_acq_int(&sc->dp_oq_busy[ring]) != 0) {
		sc->dp_rxpeek_head[ring] = idx + 1;
		return (0);
	}
	sc->dp_rxpeek_head[ring] = 0;
	sc->dp_rxpeek_found++;
	return (octep_dp_oq_sweep(sc, bufs, ring, 2));
}

static void
octep_dp_rxpeek(struct octep_softc *sc)
{
	uint32_t i, done;

	done = octep_dp_rxpeek_ring(sc, &sc->dp_bufs, sc->dp_ring);
	for (i = 0; i < OCTEP_DP_SIBLINGS_MAX; i++) {
		struct octep_dp_oq *oq = &sc->dp_sib[i];

		if (oq->armed != 0)
			done += octep_dp_rxpeek_ring(sc, &oq->bufs, oq->ring);
	}
	sc->dp_rx_done += done;
}

/* ---------------------------------------------------------------- the front-port interfaces */

/*
 * Which interface carries this tag, or NULL.
 *
 * An arriving frame names its own port: the return prefix carries the pport tag the fast path
 * matched, so nothing here has to infer it from the ring. A frame whose tag no interface claims is
 * counted and dropped rather than guessed at.
 */
static struct octep_dp_if *
octep_dp_if_by_tag(struct octep_softc *sc, uint16_t tag)
{
	uint32_t i;

	for (i = 0; i < sc->dp_nif; i++)
		if (sc->dp_if[i].tag == tag)
			return (&sc->dp_if[i]);
	return (NULL);
}

/*
 * Post one mbuf out of a front port.
 *
 * The same instruction a test frame uses, with the interface's own tag in the private header and
 * the stack's bytes in place of the generated ones. The metadata mode is whatever dp.meta says,
 * which is the vendor's form by default - see octep_dp_xmit_test for why that byte decides whether
 * a frame reaches a wire at all.
 *
 * A frame the kernel's IPsec offload tagged for an association - PACKET_TAG_IPSEC_ACCEL_OUT - is a
 * plaintext packet it expects this interface to encrypt. octep_ipsec_tx_prepare turns the tag into
 * what the envelope needs, and a tag naming an association this driver does not hold is a frame that
 * must not leave as it is.
 */
static int
octep_dp_if_transmit(if_t ifp, struct mbuf *m)
{
	struct octep_dp_if *dif = if_getsoftc(ifp);
	struct octep_esp_tx esp;
	int r;

	if (m == NULL)
		return (0);
	if (dif == NULL || dif->sc == NULL) {
		m_freem(m);
		return (ENETDOWN);
	}
	r = octep_ipsec_tx_prepare(dif->sc, dif, m, &esp);
	if (r < 0) {
		dif->tx_drops++;
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		m_freem(m);
		return (EACCES);
	}
	if (r > 0) {
		/*
		 * The link header the stack put on it is for wherever the inner destination routes;
		 * the ESP frame gets the tunnel's next hop instead, so it goes. From here the packet
		 * takes the same road as one taken at the cipher: sized, answered or fragmented if it
		 * is too big - the kernel measured it without the outer header - and handed over.
		 */
		m_adj(m, ETHER_HDR_LEN);
		return (octep_ipsec_send_inner(dif->sc, dif, m, &esp));
	}
	return (octep_dp_tx(dif, m, NULL));
}

/*
 * The transmit itself: a frame from the stack as it is (esp NULL), or an inner packet the
 * coprocessor is to encrypt (esp set: the mbuf is the inner packet alone), whose ESP frame is built
 * in the slot's own buffer by octep_ipsec_envelope. Consumes the mbuf either way. The esp caller is
 * octep_ipsec_send_inner, which has sized the packet first.
 */
int
octep_dp_tx(struct octep_dp_if *dif, struct mbuf *m, const struct octep_esp_tx *esp)
{
	struct octep_softc *sc = dif->sc;
	if_t ifp = dif->ifp;
	struct octep_dp_txq *q;
	struct octep_dma *bufs;
	struct mtx *lock;
	uint8_t *d;
	uint32_t len, wire, slot, inner, padlen, nq;

	if (sc == NULL || ifp == NULL) {
		m_freem(m);
		return (ENETDOWN);
	}
	len = m->m_pkthdr.len;
	inner = padlen = 0;
	if (esp != NULL) {
		/*
		 * The envelope has to fit the port's MTU and this slot's buffer. The caller sized the
		 * packet and answered or fragmented one that did not fit, so this is the backstop,
		 * counted, and it should never count.
		 */
		if (len < 20)
			wire = 0;
		else {
			inner = len;
			wire = octep_ipsec_wire_len(inner, esp, &padlen);
		}
		if (wire == 0 || wire + OCTEP_TOTAL_TAG_LEN > OCTEP_DP_TXBUF_SIZE ||
		    wire - ETHER_HDR_LEN > (uint32_t)if_getmtu(ifp)) {
			sc->ipsec_tx_toobig++;
			dif->tx_drops++;
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			m_freem(m);
			return (EMSGSIZE);
		}
	} else {
		if (len == 0 || len + OCTEP_TOTAL_TAG_LEN > OCTEP_DP_TXBUF_SIZE) {
			dif->tx_drops++;
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			m_freem(m);
			return (EMSGSIZE);
		}
		wire = len < OCTEP_MIN_FRAME ? OCTEP_MIN_FRAME : len;
	}

	/*
	 * Which ring: the one this frame's flow hashes to, so a connection's frames leave in order,
	 * or the CPU's when the stack gave no hash. Ring 0 under sc->mtx when there are no others.
	 */
	q = NULL;
	lock = &sc->mtx;
	nq = sc->dp_ntxq;
	if (nq != 0) {
		q = &sc->dp_txq[(M_HASHTYPE_GET(m) != M_HASHTYPE_NONE ?
		    m->m_pkthdr.flowid : (uint32_t)curcpu) % nq];
		lock = &q->mtx;
	}
	mtx_lock(lock);
	if (sc->dp_up == 0 || (q != NULL && q->up == 0)) {
		mtx_unlock(lock);
		dif->tx_drops++;
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		m_freem(m);
		return (ENETDOWN);
	}

	/*
	 * Tap before the copy, so a capture on this interface sees what left it. Without this
	 * tcpdump shows only the receive direction, which is exactly the half that is easy to see
	 * by other means.
	 *
	 * Not for a packet on its way to be encrypted: what leaves the port is ESP the host never
	 * sees, and showing the plaintext here would read as a packet that left in the clear.
	 */
	if (esp == NULL)
		ETHER_BPF_MTAP(ifp, m);

	/*
	 * This slot's own buffer. The coprocessor reads the frame out of memory after the doorbell,
	 * on its own schedule, so the buffer an instruction points at must not be reused until that
	 * instruction has been consumed - see the allocation for what sharing one buffer cost.
	 */
	/*
	 * Refuse the frame if the ring has no room, rather than wrapping over an instruction the
	 * coprocessor has not read yet.
	 *
	 * The low 32 bits of the input doorbell are what has been posted and not yet consumed; the
	 * high bits are not a count, and reading the whole register as one is the mistake that made
	 * the drain wait above useless. With 256 slots and no check at all, a burst the far side is
	 * slower than walks the producer right round and overwrites descriptors that are still in
	 * flight - and the buffer each one points at with them, which is why the buffers are
	 * per-slot in the first place.
	 *
	 * One slot is left unused so a full ring is distinguishable from an empty one.
	 */
	if (((q != NULL ? octep_dp_ring_rd(sc, q->ring, OCTEP_SDP_R_IN_INSTR_DBELL) :
	    octep_dp_rd(sc, OCTEP_SDP_R_IN_INSTR_DBELL)) & 0xffffffffULL) >= OCTEP_DP_IQ_DESCS - 1) {
		/*
		 * Unlock and free before returning. The first version of this check did neither,
		 * which left sc->mtx held for good the first time the ring filled - every path in
		 * the driver takes it - and leaked the mbuf with it. It never fired on this
		 * appliance, so nothing showed; a guard whose error path is wrong is worse than no
		 * guard, because it only runs when something is already going badly.
		 */
		if (q != NULL)
			q->full++;
		else
			sc->dp_tx_iq_full++;
		mtx_unlock(lock);
		m_freem(m);
		return (ENOBUFS);
	}
	slot = q != NULL ? q->prod : sc->dp_iq_prod;
	bufs = q != NULL ? &q->bufs : &sc->dp_txbufs;
	d = (uint8_t *)bufs->vaddr + (size_t)slot * OCTEP_DP_TXBUF_STRIDE;

	/*
	 * Zero the header and the pad, and NOT the frame.
	 *
	 * This used to zero the whole buffer - header, frame and pad - and then copy the frame
	 * straight over the part it had just cleared. Every full-size packet was therefore written
	 * twice, 1,514 bytes of it for nothing, in the one place the transmit path cannot afford
	 * waste: under the softc mutex, which every front port shares.
	 *
	 * What actually has to be zero is the 66-byte private header, because the metadata decides
	 * whether the fast path sends the frame to a wire or to encryption and a stale byte there
	 * is not a performance problem but a correctness one; and the pad between the frame and
	 * the minimum wire length, which goes out as part of the frame.
	 */
	bzero(d, OCTEP_TOTAL_TAG_LEN);
	d[0] = (uint8_t)((dif->tag >> 8) & 0xff);
	d[1] = (uint8_t)(dif->tag & 0xff);
	/*
	 * metadata[0] is md_valid, and the vendor's own hook always writes 1 - usfp_sp_md in the
	 * vendor's metadata.h names the field. This path used to write dp.meta_b0 straight, which
	 * is zero unless somebody set it, so every frame the interface sent carried md_valid 0
	 * while dp.xmit carried 1. Fall back to the vendor's value exactly as octep_dp_xmit does.
	 */
	if (sc->dp_meta_mode == OCTEP_META_MODE_VENDOR)
		d[OCTEP_PPORT_HLEN] = (sc->dp_meta_b0 != 0) ?
		    (uint8_t)sc->dp_meta_b0 : OCTEP_META_VENDOR_BYTE0;
	if (esp == NULL) {
		if (wire > len)
			bzero(d + OCTEP_TOTAL_TAG_LEN + len, wire - len);
		if (sc->dp_meta_tpl_len != 0)
			memcpy(d + OCTEP_PPORT_HLEN, sc->dp_meta_tpl,
			    sc->dp_meta_tpl_len > OCTEP_CUSTOM_META_LEN ? OCTEP_CUSTOM_META_LEN :
			    sc->dp_meta_tpl_len);
		/*
		 * Ask the coprocessor to encrypt this frame, when this interface is the one named:
		 * the hand instrument, dp.sa_idx and dp.sa_if.
		 *
		 * The two bytes were found by bisection on the appliance - a walking pattern over the
		 * 64, with each offset removed in turn - and the vendor's own header then named them.
		 * From metadata.h, struct usfp_sp_md:
		 *
		 *     +0   uint8_t  md_valid
		 *     +1   uint8_t  sa_is_out        IPsec offload direction is output (encrypt)
		 *     +8   usfp_mflow_ident flow
		 *     +12  uint32_t sa_index         IPsec offload SA index, 0 means no offload
		 *
		 * So bisection had found sa_is_out and sa_index, and the index is a 32-bit field
		 * rather than the single byte written here before - which did not matter while the
		 * only index ever tried was 1, and would have quietly truncated any other.
		 *
		 * Default off, and one interface at a time.
		 */
		if (sc->dp_sa_idx != 0 && sc->dp_sa_if == (int)(dif - sc->dp_if)) {
			d[OCTEP_PPORT_HLEN + 1] = 1;
			le32enc(d + OCTEP_PPORT_HLEN + 12, sc->dp_sa_idx);
		}
		m_copydata(m, 0, len, (caddr_t)(d + OCTEP_TOTAL_TAG_LEN));
	} else {
		/*
		 * The kernel's own request, by the same two fields: sa_is_out, and the handle, which
		 * is the association's index plus one. The frame behind them is the envelope.
		 */
		d[OCTEP_PPORT_HLEN + 1] = 1;
		le32enc(d + OCTEP_PPORT_HLEN + 12, esp->handle);
		octep_ipsec_envelope(d + OCTEP_TOTAL_TAG_LEN, esp, m, inner, padlen);
		atomic_add_64(&sc->ipsec_tx_encrypt, 1);
		/*
		 * Each frame handed over takes one sequence number on the coprocessor. The count is
		 * what puts the kernel's own counter past the coprocessor's when the association is
		 * taken out - octep_ipsec_sa_deinstall.
		 */
		if (esp->handle >= 2 && esp->handle <= OCTEP_SA_MAX)
			atomic_add_64(&sc->ipsec_sa[esp->handle - 1].handed, 1);
	}

	bus_dmamap_sync(bufs->tag, bufs->map, BUS_DMASYNC_PREWRITE);
	if (q != NULL) {
		octep_dp_build_instr_in(sc, q->iq.vaddr, slot,
		    bufs->paddr + (bus_addr_t)slot * OCTEP_DP_TXBUF_STRIDE,
		    OCTEP_TOTAL_TAG_LEN + wire);
		bus_dmamap_sync(q->iq.tag, q->iq.map, BUS_DMASYNC_PREWRITE);
		q->prod = (q->prod + 1) % OCTEP_DP_IQ_DESCS;
		q->posted++;
		octep_dp_ring_wr(sc, q->ring, OCTEP_SDP_R_IN_INSTR_DBELL, 1);
	} else {
		octep_dp_build_instr(sc, slot,
		    bufs->paddr + (bus_addr_t)slot * OCTEP_DP_TXBUF_STRIDE,
		    OCTEP_TOTAL_TAG_LEN + wire);
		bus_dmamap_sync(sc->dp_iq.tag, sc->dp_iq.map, BUS_DMASYNC_PREWRITE);
		sc->dp_iq_prod = (sc->dp_iq_prod + 1) % OCTEP_DP_IQ_DESCS;
		sc->dp_tx_posted++;
		octep_dp_wr(sc, OCTEP_SDP_R_IN_INSTR_DBELL, 1);
	}
	mtx_unlock(lock);

	dif->tx_packets++;
	dif->tx_bytes += (esp != NULL) ? wire : len;
	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_OBYTES, (esp != NULL) ? wire : len);
	m_freem(m);
	return (0);
}

static void
octep_dp_if_qflush(if_t ifp)
{

	(void)ifp;		/* nothing is queued inside the driver */
}

static void
octep_dp_if_init(void *arg)
{
	struct octep_dp_if *dif = arg;

	if (dif == NULL || dif->ifp == NULL)
		return;
	if_setdrvflagbits(dif->ifp, IFF_DRV_RUNNING, IFF_DRV_OACTIVE);
}

/*
 * Media, which exists so that something can report a link at all.
 *
 * There is nothing to choose: the speed is settled on the far side, by the switch for a panel port
 * and by the cage for a direct one, and this driver has no way to ask for a different one. So the
 * only medium offered is auto, a change request is accepted and ignored, and the whole point of
 * the pair is the status callback - without it ifconfig prints no status line, and a firewall that
 * reads the status line to decide whether to run a DHCP client on an interface will never run one.
 */
static int
octep_dp_media_change(if_t ifp __unused)
{

	return (0);
}

static void
octep_dp_media_status(if_t ifp, struct ifmediareq *ifmr)
{
	struct octep_dp_if *dif = if_getsoftc(ifp);

	/*
	 * Report the speed the port actually negotiated, because something reads it.
	 *
	 * This used to answer IFM_AUTO with no subtype, and a management interface with no speed to
	 * show picks the lowest Ethernet rate there is: every front port on this appliance was
	 * displayed as 10 Mbit/s while carrying a gigabit. Nothing was throttled by it - no queue or
	 * pipe was configured against that number - but a dashboard that says 10 Mbit about a port
	 * doing 1,000 is a defect whether or not anything acts on it.
	 *
	 * And the medium a cage reports is the module's, not the cage's. Every 10G port said
	 * 10Gbase-SR and every 1G one 1000baseT, so the copper 10GBASE-T modules in F1 and F2 were
	 * fibre and the 1000BASE-SX one in F3 was copper (#533). Now a cage says what the plugin read
	 * from its module (dp.module_media), and "unknown" until then.
	 */
	ifmr->ifm_active = IFM_ETHER;
	ifmr->ifm_status = IFM_AVALID;
	if (dif != NULL && dif->link == 1) {
		ifmr->ifm_status |= IFM_ACTIVE;
		ifmr->ifm_active |= IFM_FDX;
		switch (dif->speed) {
		case 10:
			ifmr->ifm_active |= IFM_10_T;
			break;
		case 100:
			ifmr->ifm_active |= IFM_100_TX;
			break;
		case 1000:
			if (!dif->cage)
				ifmr->ifm_active |= IFM_1000_T;
			else if (dif->mod_media[0] != 0)
				ifmr->ifm_active |= IFM_ETHER_SUBTYPE(dif->mod_media[0]);
			else
				ifmr->ifm_active |= IFM_UNKNOWN;
			break;
		case 10000:
			if (dif->mod_media[1] != 0)
				ifmr->ifm_active |= IFM_ETHER_SUBTYPE(dif->mod_media[1]);
			else
				ifmr->ifm_active |= IFM_UNKNOWN;
			break;
		default:
			ifmr->ifm_active |= IFM_AUTO;
			break;
		}
	} else {
		ifmr->ifm_active |= IFM_NONE;
	}
}

/*
 * What to believe after asking a front port to change a receive filter.
 *
 * Both filters have the same three outcomes and the same trap, so they share this rather than
 * carrying two copies of a rule that has to stay identical.
 *
 * Two pieces of state per attribute and no more: what the stack wants, and what the far side was
 * last SUCCESSFULLY told. A request that fails records nothing, so the two still disagree and the
 * next sweep of the ports asks again. Nothing here can latch a port out of being asked, which is
 * the property that matters and the one an earlier version of this got wrong twice.
 */
static void
octep_dp_filter_one(struct octep_dp_if *dif, int err, int want, int *have, const char *what)
{

	if (err == 0) {
		*have = want;
		if_printf(dif->ifp, "%s %s\n", what, want ? "on" : "off");
		return;
	}

	/*
	 * A FAILURE IS SILENT HERE, and that is copied rather than chosen. npuep prints from the
	 * ioctl path, which runs once per change, and says nothing from its poll, which runs
	 * forever - because a line printed from a retry is printed for as long as the retry lasts.
	 *
	 * This path has no ioctl to print from, so the trade is real: a firmware that refuses the
	 * attribute outright leaves no line at all, and the only sign is the absence of the one
	 * above. The alternative was worse. Printing here puts a line in the log once per sweep of
	 * the ports, forever, for a condition that will never change - and suppressing THAT needs a
	 * field to remember it by, which is the state this commit exists to delete. The first
	 * version of this mechanism acquired five fields per attribute exactly that way, one
	 * reasonable addition at a time.
	 *
	 * Both requests are accepted on this board, measured, so on this hardware the line above
	 * is the one that appears. If a future board refuses one, the way to find out is
	 * `nwa.last` after an `nwa.request` by hand, which is what that sysctl is for.
	 */
}

/*
 * Reconcile both receive filters for one front port. Called from the link poll, for the port that
 * tick is already on, and only when that port's link read came back - so at most one extra mailbox
 * request per tick, and none at all once the far side agrees.
 *
 * The caller must not hold sc->mtx: both requests sleep.
 */
static void
octep_dp_if_filters(struct octep_softc *sc, struct octep_dp_if *dif)
{
	int want;

	/*
	 * Multicast. Without this IPv6 cannot work at all: neighbour discovery is carried on the
	 * solicited-node group, and a front port whose switch entry names only its own unicast
	 * address never sees it.
	 *
	 * The want is taken ONCE, into a local. The ioctl thread writes dif->filt_want without a
	 * lock and the request sleeps, so re-reading the field afterwards could record a value as
	 * sent that never was.
	 */
	want = dif->filt_want;
	if (want != dif->filt_have)
		octep_dp_filter_one(dif, octep_nwa_port_filter(sc, dif->nwaport, want), want,
		    &dif->filt_have, "all-multicast");

	/*
	 * And promiscuous, for a sharper reason.
	 *
	 * A front port's filter drops incoming unicast addressed to anything but the address that
	 * port owns. Put it in a bridge and every reply to every machine behind it carries the
	 * BRIDGE's address, so not one of them is let in - while broadcast still arrives, so ARP
	 * and DHCP work and the port looks perfectly alive. It cost a day on this appliance before
	 * the cause was named, because the symptom points at everything except the filter.
	 *
	 * The flag is read from the interface rather than recorded from SIOCSIFFLAGS. The interface
	 * holds it already, so a copy in the softc could only disagree - and a want read from the
	 * interface cannot be missed, however the flag came to be set: by if_bridge adding a
	 * member, by an operator, or by a member added while this driver was not yet listening.
	 */
	want = (if_getflags(dif->ifp) & (IFF_PROMISC | IFF_PPROMISC)) != 0;
	dif->prom_want = want;
	if (want != dif->prom_have)
		octep_dp_filter_one(dif, octep_nwa_port_promisc(sc, dif->nwaport, want), want,
		    &dif->prom_have, "promiscuous");

	/*
	 * And the logical interface's forwarding mode, which is the same rule one layer up and
	 * matters more.
	 *
	 * A LIF holds one address and the fast path drops any frame whose destination is not it.
	 * The promiscuous bit above gets the frame past the PORT; this gets it past the LIF.
	 * Without it, a bridge member in L3 discards every frame addressed to the bridge - which
	 * is all of them - and does so before the flow table is consulted. Measured with the
	 * offload gate open: twenty pings to a host behind a bridged front port, 0 returned and
	 * 35 on FROM_WIRE_DROP_LIF_NOT_MY_MAC; with L2, 20 of 20 at 0.66 ms.
	 *
	 * Sophos's own driver chooses it from the device's flags - L3, BOTH for a bridge, L2 for a
	 * bridge port - so this reads the same thing FreeBSD keeps it in. ifp->if_bridge is how the
	 * kernel itself asks the question, at `if (ifp->if_bridge != NULL ...)` in
	 * sys/net/if_ethersubr.c, and there is no accessor for it in the if_t KPI. Reaching past
	 * that KPI is a real cost and it is bounded here: this module is built on the appliance
	 * against the running kernel's own sources and refuses to load against any other, so the
	 * struct layout it compiles against is the one it runs on.
	 *
	 * A port that was never given a LIF index is left alone rather than guessed at.
	 */
	if (dif->lif_iface != OCTEP_DP_IF_PORT_AUTO) {
		want = ((struct ifnet *)dif->ifp)->if_bridge != NULL ?
		    OCTEP_LIF_FWD_MODE_L2 : OCTEP_LIF_FWD_MODE_L3;
		dif->fwd_want = want;
		if (want != dif->fwd_have) {
			if (octep_rpc_lif_fwd(sc, dif->lif_iface, 0, (uint32_t)want) == 0) {
				dif->fwd_have = want;
				if_printf(dif->ifp, "forwarding mode %s\n",
				    want == OCTEP_LIF_FWD_MODE_L2 ? "L2, as a bridge member" :
				    "L3, routed");
			}
			/*
			 * A failure records nothing, so the comparison still disagrees and the
			 * next sweep asks again - the same rule as the two filters above, and the
			 * same reason: the rotation is the retry bound.
			 */
		}
	}
}

/*
 * Ask one port whether it has a link, and tell the stack when the answer changes.
 *
 * ONE PORT PER TICK. The alternative is twelve NetAgent round trips a second on a control channel
 * this project has already watched go down under a single unlucky request, and the thing being
 * measured moves at the speed of somebody plugging in a cable.
 *
 * It runs on the thread taskqueue rather than a callout because the request sleeps.
 */
/*
 * The VLAN subinterfaces of the front ports, and their logical interfaces on the fast path
 * (discovery 10). A tagged frame is looked up as a flow only when its port's VLAN has a logical
 * interface of its own, (iface << 12) | vlan; without one the fast path hands it to the host
 * unlooked-up (FROM_WIRE_TO_KN_LIF_UNKNOWN_VLAN_L3_MODE), which is what happened to every VLAN's
 * frames before this. Measured: with VLAN 50's added by hand, its frames were looked up -
 * FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE in place of that counter - and still reached the host tagged.
 *
 * Each pass collects every vlan(4) interface whose parent is a front port, with what its logical
 * interface is made of: the VLAN interface's MAC and MTU, and L3 - a bridge member's VLAN is left
 * without one. A new one is added with the full mask; one that changed is updated. One whose
 * interface went, or joined a bridge, is switched off, not removed - LIF_DELETE (4) removes an entry
 * (measured on the XGS 136, docs/measurements/lif-delete-2026-10-24.md), but what a removed entry
 * does with its frames is not measured -
 * by offload_disabled, which hands its frames to the host as before it was made. Not by
 * admin_disabled: that drops them, measured - with dp.vlan_flows 0 switched off that way, a TCP
 * connection across VLAN 50 moved nothing, where offload_disabled carried it through the host at
 * 997 Mbit/s. And every
 * OCTEP_VLAN_LIF_CHECK seconds each is added again in full: a full add of an entry that is still
 * there is refused with rc 1, so it costs one command and finds an entry the fast path has lost - at
 * a restart of its own - and makes it again. With dp.vlan_flows 0 every one is switched off.
 */
struct octep_vlan_seen {
	struct octep_softc	*sc;
	int			 n;
	struct octep_vlan_lif	 v[OCTEP_VLAN_LIF_MAX];
};

static int
octep_vlan_seen_cb(if_t ifp, void *arg)
{
	struct octep_vlan_seen *seen = arg;
	struct octep_softc *sc = seen->sc;
	struct octep_vlan_lif *w;
	struct ifnet *parent;
	uint16_t vtag;
	int i;

	if (if_gettype(ifp) != IFT_L2VLAN || seen->n >= OCTEP_VLAN_LIF_MAX)
		return (0);
	parent = VLAN_TRUNKDEV((struct ifnet *)ifp);
	if (parent == NULL || VLAN_TAG((struct ifnet *)ifp, &vtag) != 0 || vtag == 0)
		return (0);
	for (i = 0; i < OCTEP_DP_IF_MAX; i++)
		if (sc->dp_if[i].ifp != NULL && (struct ifnet *)sc->dp_if[i].ifp == parent &&
		    sc->dp_if[i].lif_iface != OCTEP_DP_IF_PORT_AUTO)
			break;
	/*
	 * Not a bridge member's: what the fast path does with an L2 entry of a VLAN is untried, and
	 * without one the VLAN's frames reach the host's bridge as they always have.
	 */
	if (i == OCTEP_DP_IF_MAX || if_getaddrlen(ifp) != ETHER_ADDR_LEN ||
	    ((struct ifnet *)ifp)->if_bridge != NULL)
		return (0);
	w = &seen->v[seen->n++];
	w->dif = i;
	w->vlan = vtag;
	w->mtu = (uint16_t)if_getmtu(ifp);
	memcpy(w->mac, if_getlladdr(ifp), ETHER_ADDR_LEN);
	w->fwd = OCTEP_LIF_FWD_MODE_L3;
	return (0);
}

static void
octep_dp_vlan_lifs(struct octep_softc *sc)
{
	struct octep_vlan_seen *seen;
	struct epoch_tracker et;
	struct octep_vlan_lif *o, *w;
	uint16_t rc;
	int i, j, full, err;

	if (sc->dp_nif == 0 || vlan_trunkdev_p == NULL || vlan_tag_p == NULL)
		return;
	seen = malloc(sizeof(*seen), M_TEMP, M_NOWAIT | M_ZERO);
	if (seen == NULL)
		return;
	seen->sc = sc;
	if (sc->dp_vlan_flows != 0) {
		NET_EPOCH_ENTER(et);
		(void)if_foreach(octep_vlan_seen_cb, seen);
		NET_EPOCH_EXIT(et);
	}
	full = (time_uptime - sc->dp_vlan_lif_checked) >= OCTEP_VLAN_LIF_CHECK;
	if (full)
		sc->dp_vlan_lif_checked = time_uptime;

	/* the ones whose interface went: switched off, and forgotten */
	for (i = 0; i < sc->dp_vlan_lif_n; i++) {
		o = &sc->dp_vlan_lif[i];
		for (j = 0; j < seen->n; j++)
			if (seen->v[j].dif == o->dif && seen->v[j].vlan == o->vlan)
				break;
		if (j < seen->n)
			continue;
		err = octep_rpc_lif_set(sc, sc->dp_if[o->dif].lif_iface, o->vlan, o->mac, o->mtu,
		    o->fwd, 1, OCTEP_LIF_M_OFFLOAD_DISABLED, &rc);
		if (err == 0 && rc == 0) {
			sc->dp_vlan_lif_disabled++;
			if_printf(sc->dp_if[o->dif].ifp, "VLAN %u: %s, its logical interface is switched "
			    "off - its frames go to the host\n", o->vlan, sc->dp_vlan_flows == 0 ?
			    "dp.vlan_flows is 0" : "its interface went");
		} else
			sc->dp_vlan_lif_failed++;
		mtx_lock(&sc->mtx);
		sc->dp_vlan_lif[i] = sc->dp_vlan_lif[--sc->dp_vlan_lif_n];
		mtx_unlock(&sc->mtx);
		i--;
	}

	/* the new, the changed, and every OCTEP_VLAN_LIF_CHECK seconds each again */
	for (j = 0; j < seen->n; j++) {
		w = &seen->v[j];
		for (i = 0; i < sc->dp_vlan_lif_n; i++)
			if (sc->dp_vlan_lif[i].dif == w->dif && sc->dp_vlan_lif[i].vlan == w->vlan)
				break;
		o = i < sc->dp_vlan_lif_n ? &sc->dp_vlan_lif[i] : NULL;
		if (o != NULL && !full && o->mtu == w->mtu && o->fwd == w->fwd &&
		    memcmp(o->mac, w->mac, ETHER_ADDR_LEN) == 0)
			continue;
		err = octep_rpc_lif_set(sc, sc->dp_if[w->dif].lif_iface, w->vlan, w->mac, w->mtu,
		    w->fwd, 0, OCTEP_LIF_M_ALL, &rc);
		if (err == 0 && rc == 0) {
			if (o == NULL)
				sc->dp_vlan_lif_added++;
			else
				sc->dp_vlan_lif_repaired++;
			if_printf(sc->dp_if[w->dif].ifp, "VLAN %u: logical interface %s, mtu %u, %s\n",
			    w->vlan, o == NULL ? "made" : "made again - the fast path had lost it",
			    w->mtu, w->fwd == OCTEP_LIF_FWD_MODE_L2 ? "L2" : "L3");
		} else if (err == 0 && rc == 1) {
			/*
			 * It is there. New to this table, or changed: set it as it should be, switched
			 * on - an entry switched off when its VLAN went comes back this way.
			 */
			if (o == NULL || o->mtu != w->mtu || o->fwd != w->fwd ||
			    memcmp(o->mac, w->mac, ETHER_ADDR_LEN) != 0) {
				err = octep_rpc_lif_set(sc, sc->dp_if[w->dif].lif_iface, w->vlan,
				    w->mac, w->mtu, w->fwd, 0, OCTEP_LIF_M_MAC | OCTEP_LIF_M_MTU |
				    OCTEP_LIF_M_FWD | OCTEP_LIF_M_ADMIN_DISABLED | OCTEP_LIF_M_OFFLOAD_DISABLED,
				    &rc);
				if (err != 0 || rc != 0) {
					sc->dp_vlan_lif_failed++;
					continue;
				}
				if (o == NULL)
					sc->dp_vlan_lif_added++;
				if_printf(sc->dp_if[w->dif].ifp, "VLAN %u: logical interface %s, mtu %u\n",
				    w->vlan, o == NULL ? "there already, switched on" : "changed", w->mtu);
			}
		} else {
			sc->dp_vlan_lif_failed++;
			continue;
		}
		mtx_lock(&sc->mtx);
		if (o != NULL)
			*o = *w;
		else if (sc->dp_vlan_lif_n < OCTEP_VLAN_LIF_MAX)
			sc->dp_vlan_lif[sc->dp_vlan_lif_n++] = *w;
		mtx_unlock(&sc->mtx);
	}
	free(seen, M_TEMP);
}

static int
octep_sysctl_dp_vlan_lif_table(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct octep_vlan_lif *v;
	struct sbuf *sb;
	int error, i, n;

	/* a copy taken under the lock the table is changed under; the printing may sleep */
	v = malloc(sizeof(*v) * OCTEP_VLAN_LIF_MAX, M_TEMP, M_WAITOK);
	mtx_lock(&sc->mtx);
	n = sc->dp_vlan_lif_n;
	memcpy(v, sc->dp_vlan_lif, sizeof(*v) * n);
	mtx_unlock(&sc->mtx);
	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL) {
		free(v, M_TEMP);
		return (ENOMEM);
	}
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);
	for (i = 0; i < n; i++)
		sbuf_printf(sb, "%s vlan %u  logical interface 0x%05x  mtu %u  %s\n",
		    sc->dp_if[v[i].dif].ifp != NULL ? if_name(sc->dp_if[v[i].dif].ifp) : "?",
		    v[i].vlan, (sc->dp_if[v[i].dif].lif_iface << 12) | v[i].vlan, v[i].mtu,
		    v[i].fwd == OCTEP_LIF_FWD_MODE_L2 ? "L2" : "L3");
	if (n == 0)
		sbuf_cat(sb, "no VLAN subinterface on a front port\n");
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	free(v, M_TEMP);
	return (error);
}

static void
octep_dp_link_poll(void *arg, int pending __unused)
{
	struct octep_softc *sc = arg;
	struct octep_dp_if *dif;
	uint32_t i;
	int up, err;

	if (sc->dp_link_running == 0)
		return;

	if (sc->dp_nif != 0) {
		i = sc->dp_link_next % sc->dp_nif;
		sc->dp_link_next = i + 1;
		dif = &sc->dp_if[i];
		if (dif->ifp != NULL) {
			err = octep_nwa_port_link(sc, dif->nwaport, &up);
			/*
			 * An error is not a link-down. NetAgent can be busy, and reporting a
			 * carrier loss because one request did not come back would take a
			 * firewall's interface out from under it for no reason.
			 *
			 * It is, however, a fact about the mailbox worth keeping for a moment:
			 * the filter work at the end of this block is skipped unless this read
			 * came back, because it would otherwise spend another four seconds
			 * learning the same thing. Nothing is lost - that work is driven by
			 * state rather than by events, so the next sweep asks again.
			 */
			if (err == 0 && up != dif->link) {
				dif->link = up;
				if_link_state_change(dif->ifp,
				    up ? LINK_STATE_UP : LINK_STATE_DOWN);
			}
			/*
			 * And the speed, which only means anything while the link is up:
			 * attribute 0x04 is nominal and a dark port answers 1000 exactly as
			 * a cabled one does. Measured on this appliance, which is why it is
			 * read here and thrown away below.
			 */
			if (dif->link == 1 && dif->speed == 0) {
				uint32_t mbit = 0;

				if (octep_nwa_port_speed(sc, dif->nwaport, &mbit) == 0 &&
				    mbit != 0 && mbit <= 100000) {
					dif->speed = mbit;
					if_setbaudrate(dif->ifp, (uint64_t)mbit * 1000000);
					if_printf(dif->ifp, "%u Mbit/s\n", mbit);
				}
			} else if (dif->link != 1 && dif->speed != 0) {
				dif->speed = 0;
				if_setbaudrate(dif->ifp, 0);
			}

			/*
			 * And, while this tick is on this port, the two receive filters.
			 *
			 * ONLY WHEN THE LINK READ CAME BACK. That read went through the same
			 * mailbox, so a failure says the window is busy or wedged, and asking
			 * again now would cost another four seconds to learn the same thing.
			 *
			 * The wanted state is read fresh each time - from the ifnet for
			 * promiscuous, from the group count for multicast - and compared with
			 * what the far side was last SUCCESSFULLY told. A request that fails
			 * records nothing, so the comparison still disagrees and this port is
			 * asked again when the rotation next reaches it, one sweep of the ports
			 * later. That is the whole of the retry: no counter, no budget, and no
			 * state that can latch a port out of being asked.
			 *
			 * It is the shape npuep has used since it first carried a bridge, and
			 * this driver had a state machine of its own instead - five fields per
			 * attribute, a retry budget and a refusal latch, which took three rounds
			 * of review to get right and was wrong in a new way after each of the
			 * first two. The record of what that cost is in the git history; what is
			 * left here is the version that was already working elsewhere.
			 */
			if (err == 0)
				octep_dp_if_filters(sc, dif);
		}
	}

	/*
	 * The two halves of keeping a flow table honest, on the task that already runs every second
	 * and may sleep - which the receive path may not, and a route lookup needs.
	 *
	 * The keep-alive comes before the sweep, so that a state it restamps is not one the sweep
	 * finds gone in the same pass. The sweep runs whether or not anything is being made
	 * automatically, because a flow made by hand needs taking out just as much as one made
	 * here. The making
	 * is one flow per pass on purpose: this is new, it is off by default, and a rate worth
	 * tuning is a rate worth measuring first.
	 */
	/*
	 * CURVNET_SET, and this is what the first version of it died for.
	 *
	 * pf's lookups and the routing table are reached through VNET variables, which resolve
	 * against curvnet - and a taskqueue thread has no vnet set, so every one of them dereferences
	 * a null base. It read correctly from the sysctl path for days, because a sysctl runs in a
	 * process context that already has one, and then faulted the moment the same call was made
	 * from here: "page fault while in kernel mode, octep_pf_state_exists, octep_dp_link_poll",
	 * out of the appliance's own textdump.
	 *
	 * vnet0 is the right one: this driver's interfaces are in the default vnet and nothing moves
	 * them. A jail with its own vnet would need the flow table per vnet, which is a different
	 * piece of work and not one to pretend at here.
	 */
	CURVNET_SET(vnet0);
	octep_rpc_platform_learn(sc);
	octep_dp_vlan_lifs(sc);
	octep_flow_keepalive(sc);
	octep_flow_sweep(sc);
	octep_dp_flows_ipsec_audit(sc);
	octep_dp_handback_run(sc);	/* anything a take-back left that its caller did not run */
	/* What the runs between now and the next poll may make: see OCTEP_FAST_MAKE_PER_SEC. */
	sc->dp_fast_make_left = sc->dp_fast_make_per_sec;
	for (int w = 0; w < OCTEP_FLOW_CAND_SLOTS / 64; w++)
		atomic_store_64(&sc->dp_fast_make_held[w], 0);
	if (sc->dp_auto != 0)
		(void)octep_dp_flow_drain(sc, 0);
	octep_ipsec_stats_poll(sc);
	CURVNET_RESTORE();

	if (sc->dp_link_running != 0)
		taskqueue_enqueue_timeout(taskqueue_thread, &sc->dp_link_task, hz);
}

/* One per link-layer multicast address, so if_foreach_llmaddr() returns the count. */
static int
octep_dp_hexval(char c)
{

	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	return (-1);
}

/* The metadata template as hex, so an offset can be swept without a rebuild. */
static int
octep_sysctl_dp_meta_tpl(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	char buf[2 * 64 + 1];
	int error, i, hi, lo;

	for (i = 0; i < (int)sc->dp_meta_tpl_len; i++)
		snprintf(buf + i * 2, 3, "%02x", sc->dp_meta_tpl[i]);
	buf[sc->dp_meta_tpl_len * 2] = 0;
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	memset(sc->dp_meta_tpl, 0, sizeof(sc->dp_meta_tpl));
	for (i = 0; i < 64; i++) {
		hi = octep_dp_hexval(buf[i * 2]);
		lo = octep_dp_hexval(buf[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			break;
		sc->dp_meta_tpl[i] = (uint8_t)((hi << 4) | lo);
	}
	sc->dp_meta_tpl_len = i;
	return (0);
}

static u_int
octep_dp_maddr_one(void *arg __unused, struct sockaddr_dl *sdl __unused, u_int cnt __unused)
{

	return (1);
}

static int
octep_dp_if_ioctl(if_t ifp, u_long cmd, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq *)data;
	int err = 0;

	switch (cmd) {
	case SIOCSIFFLAGS:
		/*
		 * IFF_PROMISC is deliberately not read here. The link poll reads it from the
		 * interface itself, so there is no transition this has to catch and no copy of
		 * the flag to go stale - see octep_dp_link_poll().
		 */
		if ((if_getflags(ifp) & IFF_UP) != 0)
			if_setdrvflagbits(ifp, IFF_DRV_RUNNING, 0);
		else
			if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
		break;
	case SIOCSIFMTU: {
		struct octep_dp_if *dif = if_getsoftc(ifp);

		if (ifr->ifr_mtu < 72 || ifr->ifr_mtu > OCTEP_DP_IF_MTU_MAX) {
			err = EINVAL;
			break;
		}
		/*
		 * The port's LIF first: the fast path refuses a frame longer than the LIF's MTU
		 * before this host sees it, so the two must agree, and a LIF that cannot be told
		 * (no RPC yet, or a port without one) keeps the MTU where it was.
		 */
		if (dif != NULL && dif->sc != NULL && dif->lif_iface != OCTEP_DP_IF_PORT_AUTO &&
		    dif->sc->rpc_ready != 0) {
			err = octep_rpc_lif_mtu(dif->sc, dif->lif_iface, 0, (uint32_t)ifr->ifr_mtu);
			if (err != 0)
				break;
		}
		if_setmtu(ifp, ifr->ifr_mtu);
		break;
	}
	case SIOCADDMULTI:
	case SIOCDELMULTI: {
		struct octep_dp_if *dif = if_getsoftc(ifp);

		/*
		 * Record what the stack wants and let the link poll tell the far side, because
		 * saying so means a NetAgent request and a NetAgent request sleeps. A count is
		 * enough: this driver can ask a port to pass multicast or not to, and there is
		 * no per-address filter it can program.
		 *
		 * Without this IPv6 cannot work at all. Neighbour discovery is carried on the
		 * solicited-node group, and a front port whose switch entry names only its own
		 * unicast address never sees it.
		 */
		if (dif != NULL)
			dif->filt_want =
			    if_foreach_llmaddr(ifp, octep_dp_maddr_one, NULL) != 0;
		break;
	}
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA: {
		struct octep_dp_if *dif = if_getsoftc(ifp);

		if (dif == NULL)
			return (ENXIO);
		err = ifmedia_ioctl(ifp, ifr, &dif->media, cmd);
		break;
	}
	default:
		err = ether_ioctl(ifp, cmd, data);
		break;
	}
	return (err);
}

/*
 * Present one front port. The tag is the pport tag the LIF was installed against.
 */
static int
octep_dp_if_attach(struct octep_softc *sc, uint16_t tag)
{
	struct octep_dp_if *dif;
	uint32_t nwaport;
	int linkup;
	if_t ifp;

	if (sc->dp_up == 0)
		return (ENXIO);
	if (tag == 0)
		return (EINVAL);
	if (octep_dp_if_by_tag(sc, tag) != NULL)
		return (EEXIST);
	if (sc->dp_nif >= OCTEP_DP_IF_MAX)
		return (ENOSPC);

	dif = &sc->dp_if[sc->dp_nif];
	memset(dif, 0, sizeof(*dif));
	dif->sc = sc;
	dif->tag = tag;
	/*
	 * `filt_have` and `prom_have` are left at the zero the memset gave them, which is correct
	 * rather than convenient: a host reboot restarts the coprocessor, so it comes up with both
	 * filters closed, and nothing else creates an interface here. The one case where that is a
	 * lie is dp.if_del followed by dp.if_add on a running machine, and octep_dp_if_detach_all()
	 * closes the filters on the way out so that it is not.
	 */

	/*
	 * Ask the port for its own address before inventing one.
	 *
	 * The port has a real MAC and it is the appliance's: the same base as the management NIC,
	 * with the interface id in the last byte - see OCTEP_NWA_SUB_MAC. Using it is not tidiness.
	 * The port's hardware filter passes unicast only to the address the port owns, so an
	 * invented address means nothing addressed to this interface ever arrives unless the port
	 * is also put in promiscuous mode.
	 *
	 * The fallback is locally administered, with the tag in the last byte so two ports never
	 * collide and a capture still says which port a frame came from.
	 */
	nwaport = sc->dp_if_port == OCTEP_DP_IF_PORT_AUTO ? (uint32_t)tag : sc->dp_if_port;
	dif->lif_iface = sc->dp_if_iface;
	dif->nwaport = nwaport;
	dif->link = -1;			/* not down: nothing has asked yet */
	if (octep_nwa_port_mac(sc, nwaport, dif->mac) != 0) {
		dif->mac[0] = 0x02;
		dif->mac[1] = 0x0c;
		dif->mac[2] = 0xe0;
		dif->mac[3] = 0x83;
		dif->mac[4] = (uint8_t)device_get_unit(sc->dev);
		dif->mac[5] = (uint8_t)tag;
		device_printf(sc->dev, "dp: NetAgent port %u would not give its MAC; using a "
		    "locally administered address for tag %u, and unicast will need promiscuous "
		    "mode\n", nwaport, tag);
	}

	ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL)
		return (ENOMEM);
	if_initname(ifp, "oxp", sc->dp_nif);
	if_setsoftc(ifp, dif);
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
	if_setinitfn(ifp, octep_dp_if_init);
	if_setioctlfn(ifp, octep_dp_if_ioctl);
	if_settransmitfn(ifp, octep_dp_if_transmit);
	if_setqflushfn(ifp, octep_dp_if_qflush);
	if_setmtu(ifp, ETHERMTU);
	if_setcapabilities(ifp, 0);
	if_setcapenable(ifp, 0);
	octep_ipsec_if_attach(sc, ifp);
	dif->ifp = ifp;
	sc->dp_nif++;

	ifmedia_init(&dif->media, 0, octep_dp_media_change, octep_dp_media_status);
	ifmedia_add(&dif->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&dif->media, IFM_ETHER | IFM_AUTO);

	ether_ifattach(ifp, dif->mac);

	/*
	 * No line rate until the port has one, which means saying nothing rather than saying ten.
	 *
	 * ether_ifattach() ends with `if (ifp->if_baudrate == 0) ifp->if_baudrate = IF_Mbps(10)`,
	 * commented in if_ethersubr.c as "just a default" - the lowest rate Ethernet ever ran at,
	 * chosen because the field has no "unknown". The link poll fills in the real figure once the
	 * port has a link, and attribute 0x04 answers 1000 for a dark port, so there is nothing
	 * truthful to put here before then. Left at the default, every unplugged front port reported
	 * 10 Mbit/s on the dashboard and he asked about it twice.
	 *
	 * Zero is what the rest of the system already reads as "not known": ifinfo prints no line-rate
	 * line for it, and OPNsense's own formatter returns an empty string rather than a number.
	 */
	if_setbaudrate(ifp, 0);

	/*
	 * And say which socket on the front of the machine this is.
	 *
	 * An unassigned front port showed as nothing but "oxp5", and which piece of metal that is was
	 * in no file the operating system reads. The label is derived from the PORT TAG and not from
	 * the unit number, deliberately: the units are handed out in the order bringup.sh calls
	 * dp.if_add, so a table keyed on them mislabels silently the day that order changes, while the
	 * tag belongs to the port.
	 *
	 * Tags 1 and 2 are the two SFP+ cages wired straight to the coprocessor; 0x8000 | p << 8 for
	 * p of 1..8 are the eight RJ45 sockets behind the switch, and p of 9 and 10 its two SFP cages.
	 * Only F1 and Port2 have been confirmed against a cable - the rest is the vendor's numbering,
	 * which is also what the printing on the metal follows.
	 *
	 * OPNsense overwrites this with its own name once the port is assigned, which is what should
	 * happen: this is for the ports it has no name for yet.
	 */
	{
		const char *label = NULL;
		char buf[16];

		if (dif->tag == 1)
			label = "F1";
		else if (dif->tag == 2)
			label = "F2";
		else if ((dif->tag & 0x80ffU) == 0x8000U) {
			uint32_t p = (dif->tag >> 8) & 0x7f;

			if (p >= 1 && p <= 8) {
				snprintf(buf, sizeof(buf), "Port%u", p);
				label = buf;
			} else if (p == 9) {
				label = "F3";
			} else if (p == 10) {
				label = "F4";
			}
		}
		/*
		 * A name given in dp.if_label wins over the tag's. bringup.sh gives every panel port
		 * the label its model has for it (scripts/xgs/models.py) and an expansion module's
		 * port the one read from the coprocessor's board file; the tag's name above is the XGS
		 * 3300's panel and is right on that panel only - on an XGS 2100 the switch's two cages,
		 * tagged as the 3300's F3 and F4, are F1 and F2 on the metal. Consumed: the next if_add
		 * starts without one.
		 */
		if (sc->dp_if_label[0] != '\0')
			label = sc->dp_if_label;
		if (label != NULL) {
			char *d = if_allocdescr(OCTEP_DP_DESCR_LEN, M_NOWAIT);

			if (d != NULL) {
				snprintf(d, OCTEP_DP_DESCR_LEN, "XGS front port %s", label);
				if_setdescr(ifp, d);
			}
		}
		/* Only the switch's sockets are RJ45; a cage, or a port with no name, is not known. */
		dif->cage = (label == NULL || strncmp(label, "Port", 4) != 0);
	}

	/*
	 * Ask once now rather than waiting a whole round of the poll, because the first thing
	 * that reads this interface is the operating system deciding whether to configure it -
	 * and on this appliance one of these ports is the WAN.
	 */
	if (octep_nwa_port_link(sc, nwaport, &linkup) == 0) {
		dif->link = linkup;
		if_link_state_change(ifp, linkup ? LINK_STATE_UP : LINK_STATE_DOWN);
	}

	if (sc->dp_link_running == 0) {
		sc->dp_link_running = 1;
		TIMEOUT_TASK_INIT(taskqueue_thread, &sc->dp_link_task, 0,
		    octep_dp_link_poll, sc);
		taskqueue_enqueue_timeout(taskqueue_thread, &sc->dp_link_task, hz);
		/*
		 * And the queue the receive path kicks. One thread of the driver's own, because
		 * the shared queue above is where the poll sleeps for the management channel - up
		 * to seconds - and a kick is worth what it is worth in milliseconds. Without it
		 * the poll does everything, as it always did.
		 */
		TASK_INIT(&sc->dp_fast_task, 0, octep_dp_fast_task, sc);
		sc->dp_fast_hold = ticks;
		if (sc->dp_fast_tq == NULL) {
			sc->dp_fast_tq = taskqueue_create("octep_fast", M_NOWAIT,
			    taskqueue_thread_enqueue, &sc->dp_fast_tq);
			if (sc->dp_fast_tq != NULL && taskqueue_start_threads(&sc->dp_fast_tq, 1,
			    PI_NET, "%s fast", device_get_nameunit(sc->dev)) != 0) {
				taskqueue_free(sc->dp_fast_tq);
				sc->dp_fast_tq = NULL;
			}
		}
	}
	device_printf(sc->dev, "dp: %s carries port tag %u\n", if_name(ifp), tag);
	return (0);
}

static void
octep_dp_if_detach_all(struct octep_softc *sc)
{
	uint32_t i;

	/*
	 * Stop the receive path before any ifnet goes, and this is the only place that has to
	 * remember to - both callers come through here, including the dp.if_del sysctl, which leaves
	 * the datapath up afterwards and so needs the resume at the end.
	 */
	octep_dp_rx_quiesce(sc);

	/*
	 * Stop the poll before the interfaces go, and drain it: it sleeps inside a NetAgent
	 * request, so it can be part-way through one that names an ifnet this loop is about
	 * to free.
	 */
	if (sc->dp_link_running != 0) {
		sc->dp_link_running = 0;
		taskqueue_cancel_timeout(taskqueue_thread, &sc->dp_link_task, NULL);
		taskqueue_drain_timeout(taskqueue_thread, &sc->dp_link_task);
		/*
		 * And the kicked task, which finds dp_link_running clear and does nothing. Its
		 * queue stays: this function is also dp.if_del on a running machine, after which
		 * the receive path goes on and may kick again. octep_dp_stop frees it.
		 */
		if (sc->dp_fast_tq != NULL)
			taskqueue_drain(sc->dp_fast_tq, &sc->dp_fast_task);
	}

	/*
	 * Give back the receive filters this driver opened, while there is still an ifnet to name
	 * in a message and the poll is stopped so nothing is asking at the same time.
	 *
	 * The coprocessor keeps its own filter state and the driver's belief does not survive this
	 * function, so a port left permissive here is permissive until something else happens to
	 * it. That matters for dp.if_del followed by dp.if_add on a running machine: the new
	 * interface starts with prom_have at zero, reads IFF_PROMISC as clear, and therefore never
	 * sends the OFF that would tidy up - the port stays open and nothing in the system says so.
	 * A host reboot restarts the coprocessor and would have cleared it anyway, which is why
	 * this was easy to miss.
	 *
	 * It stops at the first request that does not reach the port. On a mailbox that has stopped
	 * answering every one of these costs four seconds and achieves nothing, and tidying up is a
	 * courtesy - a host reboot restarts the coprocessor and clears all of it anyway - so it is
	 * not worth waiting on.
	 */
	for (i = 0; i < sc->dp_nif; i++) {
		struct octep_dp_if *dif = &sc->dp_if[i];

		if (dif->ifp == NULL)
			continue;
		if (dif->prom_have != 0) {
			if (octep_nwa_port_promisc(sc, dif->nwaport, 0) != 0)
				break;
			dif->prom_have = 0;
		}
		if (dif->filt_have != 0) {
			if (octep_nwa_port_filter(sc, dif->nwaport, 0) != 0)
				break;
			dif->filt_have = 0;
		}
	}

	for (i = 0; i < sc->dp_nif; i++) {
		if (sc->dp_if[i].ifp == NULL)
			continue;
		ether_ifdetach(sc->dp_if[i].ifp);
		ifmedia_removeall(&sc->dp_if[i].media);
		if_free(sc->dp_if[i].ifp);
		sc->dp_if[i].ifp = NULL;
	}
	sc->dp_nif = 0;
	octep_ips_if_destroy(sc);
	sc->dp_link_next = 0;

	/*
	 * The interfaces are gone, so there is nothing left to free under a servicer and the rings can
	 * be served again. dp.if_del needs this; the teardown path does not care, because dp_up goes to
	 * zero immediately after and a pass over a stopped datapath does nothing.
	 */
	octep_dp_rx_resume(sc);
}

/*
 * The buffers behind one ring, for a handler that has only the ring number.
 */
static struct octep_dma *
octep_dp_ring_bufs(struct octep_softc *sc, uint32_t ring)
{
	uint32_t i;

	if (ring == sc->dp_ring)
		return (&sc->dp_bufs);
	for (i = 0; i < OCTEP_DP_SIBLINGS_MAX; i++)
		if (sc->dp_sib[i].armed != 0 && sc->dp_sib[i].ring == ring)
			return (&sc->dp_sib[i].bufs);
	return (NULL);
}

/*
 * One ring's MSI-X interrupt.
 *
 * The vendor hooks one of these per ring with a per-ring context, and this is the same shape. It
 * visits that ring, as dp.service visits every ring, and does nothing else, because the question
 * it exists to answer is whether the block needs its interrupt taken at all: every register the
 * host can write by hand has been written by hand, and the ring still stops after one packet.
 */
static void
octep_dp_intr(void *arg)
{
	struct octep_dp_vec *vec = arg;
	struct octep_softc *sc = vec->sc;
	struct octep_dma *bufs;

	vec->count++;
	sc->dp_intr_taken++;
	if (sc->dp_up == 0)
		return;
	bufs = octep_dp_ring_bufs(sc, vec->ring);
	if (bufs == NULL)
		return;

	/*
	 * Go round until the ring is empty - which is what a visit is, for every servicer and not
	 * only this one.
	 *
	 * One pass was the defect. A pass reads R_OUT_CNTS, takes at most RSIZE packets and
	 * acknowledges those, so a burst bigger than the ring leaves the count above the interrupt
	 * level - and the block raises the interrupt on that level being crossed, not on its being
	 * exceeded. Nothing is left to raise it again and the ring is dead until something else
	 * touches it. This is what made a download arrive as nothing at all while a ping, which
	 * never fills a ring, came back in 0.6 ms.
	 *
	 * The loop lived here, around a flag taken for each pass, and the watchdog made one pass of
	 * its own: the second defect, with the same shape as the first. Both are in
	 * octep_dp_oq_service now, and so is the request for the next tick when a visit is left
	 * with work still queued.
	 */
	sc->dp_rx_done += octep_dp_oq_service(sc, bufs, vec->ring,
	    OCTEP_DP_OQ_DRAIN_ROUNDS * sc->dp_oq_rsize, NULL, 1);
}

/*
 * Release every vector, and the allocation behind them.
 */
static void
octep_dp_msix_teardown(struct octep_softc *sc)
{
	uint32_t i;

	for (i = 0; i <= OCTEP_DP_SIBLINGS_MAX; i++) {
		struct octep_dp_vec *vec = &sc->dp_vec[i];

		if (vec->cookie != NULL) {
			bus_teardown_intr(sc->dev, vec->res, vec->cookie);
			vec->cookie = NULL;
		}
		if (vec->res != NULL) {
			bus_release_resource(sc->dev, SYS_RES_IRQ, vec->rid, vec->res);
			vec->res = NULL;
		}
		vec->rid = 0;
	}
	if (sc->dp_msix_on != 0) {
		pci_release_msi(sc->dev);
		sc->dp_msix_on = 0;
		sc->dp_msix_count = 0;
	}
}

/*
 * Allocate MSI-X and hook one vector per armed ring.
 *
 * Ring n is table entry OCTEP_DP_MSIX_RING_BASE + n; see that constant for where the number comes
 * from. FreeBSD hands out one message per resource id starting at 1, and it allocates a contiguous
 * block from entry 0, so reaching the ring vectors means asking for the sixteen named ones as well
 * even though nothing here hooks them.
 */
static int
octep_dp_msix_setup(struct octep_softc *sc)
{
	uint32_t rings[OCTEP_DP_SIBLINGS_MAX + 1];
	uint32_t i, n, hooked, want, highest;
	int msgs, count, err;

	if (sc->dp_msix_on != 0)
		return (EALREADY);
	if (sc->dp_up == 0)
		return (ENXIO);

	n = 0;
	rings[n++] = sc->dp_ring;
	for (i = 0; i < OCTEP_DP_SIBLINGS_MAX; i++)
		if (sc->dp_sib[i].armed != 0)
			rings[n++] = sc->dp_sib[i].ring;

	highest = 0;
	for (i = 0; i < n; i++)
		if (rings[i] > highest)
			highest = rings[i];
	want = OCTEP_DP_MSIX_RING_BASE + highest + 1;

	msgs = pci_msix_count(sc->dev);
	if (msgs <= 0) {
		device_printf(sc->dev, "dp: the endpoint advertises no MSI-X messages\n");
		return (ENXIO);
	}
	if ((uint32_t)msgs < want) {
		device_printf(sc->dev, "dp: %d MSI-X messages, and ring %u needs %u\n",
		    msgs, highest, want);
		return (ENOSPC);
	}

	count = (int)want;
	err = pci_alloc_msix(sc->dev, &count);
	if (err != 0) {
		device_printf(sc->dev, "dp: pci_alloc_msix failed: %d\n", err);
		return (err);
	}
	if ((uint32_t)count < want) {
		device_printf(sc->dev, "dp: asked for %u MSI-X messages and got %d\n", want, count);
		pci_release_msi(sc->dev);
		return (ENOSPC);
	}
	sc->dp_msix_on = 1;
	sc->dp_msix_count = count;

	hooked = 0;
	sc->dp_msix_bound = 0;
	for (i = 0; i < n; i++) {
		struct octep_dp_vec *vec = &sc->dp_vec[i];

		vec->sc = sc;
		vec->ring = rings[i];
		vec->count = 0;
		vec->rid = OCTEP_DP_MSIX_RID(rings[i]);
		vec->res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ, &vec->rid, RF_ACTIVE);
		if (vec->res == NULL) {
			device_printf(sc->dev, "dp: no interrupt resource for ring %u, rid %d\n",
			    rings[i], vec->rid);
			continue;
		}
		err = bus_setup_intr(sc->dev, vec->res, INTR_TYPE_NET | INTR_MPSAFE, NULL,
		    octep_dp_intr, vec, &vec->cookie);
		if (err != 0) {
			device_printf(sc->dev, "dp: cannot hook ring %u: %d\n", rings[i], err);
			bus_release_resource(sc->dev, SYS_RES_IRQ, vec->rid, vec->res);
			vec->res = NULL;
			continue;
		}
		hooked++;
		(void)bus_describe_intr(sc->dev, vec->res, vec->cookie, "ring%u", rings[i]);
		/*
		 * EACH RING ON A CPU OF ITS OWN (#361). Left to the kernel, every ring's vector was
		 * delivered to cpu0 - hw.intrs read "@cpu0" for all eight on the second XGS 3300,
		 * 2026-10-21 - and an interrupt thread is run where its interrupt arrives. So the
		 * rings' handlers queued behind one another on one CPU, and so did the receive
		 * watchdog's callout: a ring that a long visit of another ring held up waited with its
		 * interrupt delivered and its handler not yet run, until the watchdog came - which
		 * itself came up to 177 ticks late against a period of 50. The vendor's host gives each
		 * ring's vector a CPU of its own; this does the same, ring by ring in the order they
		 * are hooked.
		 */
		if (sc->dp_msix_spread != 0 && mp_ncpus > 1) {
			int cpu = (int)(i % (uint32_t)mp_ncpus);

			err = bus_bind_intr(sc->dev, vec->res, cpu);
			if (err == 0)
				sc->dp_msix_bound++;
			else
				device_printf(sc->dev, "dp: ring %u's vector not bound to cpu%d: %d\n",
				    rings[i], cpu, err);
		}
	}

	device_printf(sc->dev, "dp: %d MSI-X messages allocated, %u of %u rings hooked, ring n "
	    "on table entry %u + n, %u bound a CPU each\n", count, hooked, n, OCTEP_DP_MSIX_RING_BASE,
	    sc->dp_msix_bound);
	if (hooked == 0) {
		octep_dp_msix_teardown(sc);
		return (ENXIO);
	}
	return (0);
}

/*
 * Re-write the output interrupt levels on every armed ring, once the coprocessor's tick rate is
 * known.
 *
 * These two things are in conflict and the conflict is real: the rings have to be programmed
 * BEFORE the handshake, because the target latches their addresses when its port opens and never
 * looks again - but the tick rate needed for the time threshold arrives WITH the handshake. So at
 * dp.start the time field is necessarily 0, and the vendor's live ring carries 0x56 there.
 *
 * Re-writing a threshold is not re-allocating a ring: the base addresses and the enables are left
 * exactly as the target latched them.
 */
void
octep_dp_refresh_int_levels(struct octep_softc *sc)
{
	uint64_t lev;
	uint32_t i;

	if (sc->dp_up == 0)
		return;
	/*
	 * Only derive the threshold from the tick rate when nobody has set one by hand. The derived
	 * value on this board is 1, where the vendor's own ring carries 0x56, and the difference has
	 * never been tried - so both are reachable and the choice is recorded rather than assumed.
	 */
	if (sc->dp_time_threshold_set == 0) {
		sc->dp_time_threshold = octep_dp_oq_ticks(sc, OCTEP_DP_OQ_INTR_TIME);
		if (sc->dp_time_threshold == 0)
			return;
	}
	if (sc->dp_intr_pkt == 0)
		sc->dp_intr_pkt = OCTEP_DP_OQ_INTR_PKT;

	lev = ((uint64_t)sc->dp_time_threshold << 32) | sc->dp_intr_pkt;
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_INT_LEVELS, lev);
	for (i = 0; i < OCTEP_DP_SIBLINGS_MAX; i++) {
		if (sc->dp_sib[i].armed != 0)
			octep_dp_ring_wr(sc, sc->dp_sib[i].ring,
			    OCTEP_SDP_R_OUT_INT_LEVELS, lev);
	}
	device_printf(sc->dev, "dp: output interrupt levels refreshed to 0x%016jx now the tick "
	    "rate is known\n", (uintmax_t)lev);
}
