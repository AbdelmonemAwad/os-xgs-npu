/*-
 * SPDX-License-Identifier: BSD-2-Clause
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

#include <net/if.h>
#include <net/if_var.h>
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

#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "octep.h"

#define	OCTEP_DP_IDLE_TRIES	1000		/* x 10 us */

static int octep_dp_msix_setup(struct octep_softc *sc);
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
 * The first credit a ring is granted, and the ceiling its credit is held to afterwards.
 *
 * `entries * OCTEP_DP_CREDIT_UNIT`. Granting `entries` made a ring deliver exactly one packet for
 * weeks; granted sixteen times that, it delivered 37 in a single burst, with 511 interrupts where
 * there had been 8. Less than a batch of credit fetches a batch anyway and writes nothing: a ring
 * granted 2 read back 0xfffffff2, which is 2 - 16.
 *
 * The unit is NOT what one packet costs - see OCTEP_DP_CREDIT_UNIT - so this figure is also the most
 * credit a ring is ever allowed to hold. octep_dp_oq_service returns credit only up to it.
 */
static uint32_t
octep_dp_oq_first_grant(struct octep_softc *sc)
{

	if (sc->dp_oq_grant != 0)
		return (sc->dp_oq_grant);
	return (sc->dp_oq_rsize * sc->dp_credit_unit);
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
 * octep_dp_oq_service() is already safe against a handler running beside it - it takes the ring's
 * busy flag and leaves if another servicer holds it.
 */
static void
octep_dp_rxwd(void *arg)
{
	struct octep_softc *sc = arg;

	/*
	 * No re-arm while a quiesce is set, which is what makes callout_drain() in
	 * octep_dp_rx_quiesce() final instead of a race against this line.
	 */
	if (sc->dp_rxwd_on == 0 || atomic_load_acq_int(&sc->dp_rx_quiesce) != 0)
		return;
	if (sc->dp_up != 0 && octep_dp_service(sc) != 0)
		sc->dp_rxwd_runs++;
	callout_reset(&sc->dp_rxwd, sc->dp_rxwd_ticks, octep_dp_rxwd, sc);
}

/*
 * Ask for a pass on the next tick, because a handler has left work behind.
 */
static void
octep_dp_rxwd_kick(struct octep_softc *sc)
{
	if (sc->dp_rxwd_on != 0)
		callout_reset(&sc->dp_rxwd, 1, octep_dp_rxwd, sc);
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
 * set, so callout_drain() is final rather than racing a re-arm.
 */
static void
octep_dp_rx_quiesce(struct octep_softc *sc)
{
	uint32_t i;
	int spins;

	atomic_store_rel_int(&sc->dp_rx_quiesce, 1);
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
 * Bounded like the handler's own loop, because a quiesce taken under load leaves a backlog that one
 * pass cannot clear and no interrupt is coming to finish it.
 */
static void
octep_dp_rx_resume(struct octep_softc *sc)
{
	uint32_t rounds;

	atomic_store_rel_int(&sc->dp_rx_quiesce, 0);
	for (rounds = 0; rounds < OCTEP_DP_OQ_DRAIN_ROUNDS; rounds++) {
		if (sc->dp_up == 0 || octep_dp_service(sc) == 0)
			break;
	}
	if (sc->dp_rxwd_on != 0)
		callout_reset(&sc->dp_rxwd, sc->dp_rxwd_ticks, octep_dp_rxwd, sc);
}

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
	    (bus_size_t)OCTEP_DP_IQ_DESCS * OCTEP_DP_BUF_STRIDE, OCTEP_DP_BUF_ALIGN,
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

	/* Now grant the output ring the buffers it may write into: exactly the ring size, once. */
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_SLIST_DBELL, octep_dp_oq_first_grant(sc));

	sc->dp_sa_if = -1;
	sc->dp_iq_prod = 0;
	sc->dp_tx_posted = 0;
	sc->dp_rx_seen = 0;
	if (sc->dp_pkind == 0)
		sc->dp_pkind = OCTEP_DP_PKIND;
	if (!sc->dp_meta_mode_set) {
		sc->dp_meta_mode = OCTEP_META_MODE_VENDOR;
		sc->dp_meta_mode_set = 1;
	}
	sc->dp_up = 1;
	if (sc->dp_rxwd_on == 0) {
		if (sc->dp_rxwd_ticks == 0)
			sc->dp_rxwd_ticks = OCTEP_DP_RXWD_TICKS;
		callout_init(&sc->dp_rxwd, 1);
		sc->dp_rxwd_on = 1;
		callout_reset(&sc->dp_rxwd, sc->dp_rxwd_ticks, octep_dp_rxwd, sc);
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

		if (r >= sc->sdp_rings_mappable) {
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
	octep_dp_msix_teardown(sc);
	octep_dp_if_detach_all(sc);
	if (sc->dp_rxwd_on != 0) {
		sc->dp_rxwd_on = 0;
		callout_drain(&sc->dp_rxwd);
	}
	mtx_lock(&sc->mtx);
	if (sc->dp_up == 0) {
		mtx_unlock(&sc->mtx);
		return;
	}
	octep_dp_reset_ring(sc);
	sc->dp_up = 0;
	mtx_unlock(&sc->mtx);

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
octep_dp_build_instr(struct octep_softc *sc, uint32_t slot, bus_addr_t dptr, uint32_t datalen)
{
	char *e = (char *)sc->dp_iq.vaddr + ((size_t)slot * OCTEP_DP_INSTR_SIZE);
	uint64_t ih3, pki_ih3, irh;

	bzero(e, OCTEP_DP_INSTR_SIZE);

	ih3 = OCTEP_IH3(datalen + OCTEP_INSTR_FSZ, sc->dp_pkind, OCTEP_INSTR_FSZ);
	pki_ih3 = OCTEP_PKI_IH3(OCTEP_ORDERED_TAG, 1, OCTEP_INSTR_SL, OCTEP_INSTR_PM, 1);
	irh = OCTEP_IRH(OCTEP_IRH_CKSUM_OFF, 0, sc->dp_dport, OCTEP_OCT_NW_PKT_OP);

	*(uint64_t *)(e + OCTEP_INSTR_DPTR) = (uint64_t)dptr;
	*(uint64_t *)(e + OCTEP_INSTR_IH3) = ih3;
	*(uint64_t *)(e + OCTEP_INSTR_PKI_IH3) = pki_ih3;
	*(uint64_t *)(e + OCTEP_INSTR_RPTR) = bswap64(0);
	*(uint64_t *)(e + OCTEP_INSTR_IRH) = bswap64(irh);
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
	uint32_t i, meta, found = 0;

	(void)sc;
	bus_dmamap_sync(bufs->tag, bufs->map, BUS_DMASYNC_POSTREAD);

	for (i = 0; i < OCTEP_DP_OQ_DESCS; i++) {
		b = (const uint8_t *)bufs->vaddr + ((size_t)i * OCTEP_DP_BUF_STRIDE);
		len = be64toh(*(const uint64_t *)(b + OCTEP_RX_LEN_OFF));
		if (len == 0 || len == OCTEP_DP_BUF_POISON_WORD)
			continue;   /* untouched, or written as zero - neither is an arrival */
		found++;
		if (found > 4)
			continue;
		e = b + OCTEP_RX_PREFIX_LEN;
		meta = le32dec(b + OCTEP_RX_META_OFF);
		sbuf_printf(sb, "  ring %u buf %3u  len %ju  tag %ju  meta 0x%08x%s\n",
		    ring, i, (uintmax_t)len, (uintmax_t)be16dec(b + OCTEP_RX_TAG_OFF),
		    meta, meta == OCTEP_RX_META_SIG ? " - the vendor's" : "");
		sbuf_printf(sb, "    %02x:%02x:%02x:%02x:%02x:%02x <- "
		    "%02x:%02x:%02x:%02x:%02x:%02x  type %02x%02x\n",
		    e[0], e[1], e[2], e[3], e[4], e[5],
		    e[6], e[7], e[8], e[9], e[10], e[11], e[12], e[13]);
	}
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
	    st.direction == 0 ? "in" : "out", st.timeout, st.state_flags);
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
	sbuf_printf(sb, "  translated, and the connection is %s\n",
	    st.nat_snat ? "do_snat - its source is rewritten on the way out" :
	    "do_dnat - its destination is rewritten on the way in");
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
 * The flow table: allocate an index, find a flow again, and give it back.
 *
 * Index 0 is never handed out. The hand-programmed experiments that found all of this used index 1
 * and the bring-up uses 0 for its own next hop, so starting at 2 keeps a flow this driver made
 * automatically from colliding with either - and a collision there would look exactly like the fast
 * path misbehaving, which is a week nobody needs twice.
 *
 * Called with the softc lock held.
 */
static struct octep_flow *
octep_flow_find(struct octep_softc *sc, const struct octep_pf_tuple *t)
{
	int i;

	for (i = 0; i < OCTEP_FLOW_MAX; i++) {
		if (!sc->dp_flow[i].used)
			continue;
		if (sc->dp_flow[i].tuple.sip == t->sip &&
		    sc->dp_flow[i].tuple.dip == t->dip &&
		    sc->dp_flow[i].tuple.sport == t->sport &&
		    sc->dp_flow[i].tuple.dport == t->dport &&
		    sc->dp_flow[i].tuple.proto == t->proto)
			return (&sc->dp_flow[i]);
	}
	return (NULL);
}

static struct octep_flow *
octep_flow_alloc(struct octep_softc *sc, const struct octep_pf_tuple *t,
    uint32_t slot, uint32_t rev, int in_dif, uint16_t in_tag)
{
	int i;

	for (i = 2; i < OCTEP_FLOW_MAX; i++) {
		if (sc->dp_flow[i].used)
			continue;
		sc->dp_flow[i].used = 1;
		sc->dp_flow[i].slot = slot;
		sc->dp_flow[i].rev = rev;
		sc->dp_flow[i].idx = (uint32_t)i;
		sc->dp_flow[i].tuple = *t;
		sc->dp_flow[i].in_dif = in_dif;
		sc->dp_flow[i].in_tag = in_tag;
		sc->dp_flow_used++;
		return (&sc->dp_flow[i]);
	}
	sc->dp_auto_full++;
	return (NULL);
}

/*
 * Offer a punted frame as a candidate for acceleration.
 *
 * Called from the receive path, which holds no softc lock and may be running on several rings at
 * once - see struct octep_flow_cand for why each slot has a trylock rather than a seqlock, and why a
 * writer that cannot take it simply gives up.
 *
 * Cheap on purpose: a hash, a trylock and a struct copy. No lookup against the flow table happens
 * here; the drain does that, where it costs the poll rather than the datapath.
 */
static void
octep_flow_cand_push(struct octep_softc *sc, const struct octep_pf_tuple *t, uint32_t slot,
    uint32_t rev, uint16_t tag)
{
	struct octep_flow_cand *e;
	struct octep_dp_if *dif;
	uint32_t h;
	int in_dif;

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
	 * index, so each would evict the other forever; rotating one address breaks that.
	 */
	h = t->sip ^ ((t->dip << 13) | (t->dip >> 19));
	h ^= ((uint32_t)t->sport << 16) | (uint32_t)t->dport;
	h ^= (uint32_t)t->proto;
	h *= 0x9e3779b1u;
	e = &sc->dp_cand[(h >> 16) % OCTEP_FLOW_CAND_MAX];

	/* Outside the lock: a walk of up to twelve interfaces is the whole cost of this function. */
	dif = octep_dp_if_by_tag(sc, tag);
	in_dif = (dif != NULL) ? (int)(dif - sc->dp_if) : -1;

	if (atomic_cmpset_acq_32(&e->busy, 0, 1) == 0) {
		sc->dp_cand_clash++;
		return;
	}

	/*
	 * An unread candidate about to be overwritten, which is the collision that actually costs
	 * something - two connections sharing a slot, each evicting the other before the poll gets
	 * to either. dp_cand_clash cannot see that, because those writers never meet on an entry.
	 */
	if (e->stamp != e->seen)
		sc->dp_cand_lost++;

	e->tuple = *t;
	e->slot = slot;
	e->rev = rev;
	e->in_dif = in_dif;
	e->in_tag = tag;
	e->stamp++;
	sc->dp_cand_pushed++;

	atomic_store_rel_32(&e->busy, 0);
}

/*
 * Forget every flow, because the far side has just been told to discard every flow.
 *
 * A revision bump invalidates the coprocessor's whole table in one command, and this table is only
 * this driver's record of what it programmed there. Leaving the record behind would make dp.flows
 * count flows that no longer exist, fill the table with entries the sweep has to walk and expire,
 * and eventually refuse a real flow for want of room in a table that is actually empty.
 *
 * Nothing is sent. The entries are already gone on the far side, which is the whole point of the
 * command that got us here; sending an invalidate for each would be asking it to discard what it
 * has discarded, and from inside the lock that posted the discard.
 *
 * Called with the softc lock held, by the revision bump and by nothing else.
 */
void
octep_dp_flows_forget(struct octep_softc *sc)
{
	int i;

	mtx_assert(&sc->mtx, MA_OWNED);

	for (i = 0; i < OCTEP_FLOW_MAX; i++) {
		if (!sc->dp_flow[i].used)
			continue;
		sc->dp_flow[i].used = 0;
		sc->dp_flow[i].in_dif = -1;
		sc->dp_flow_forgot++;
	}
	sc->dp_flow_used = 0;
}

/*
 * The front port the other direction of this flow arrives on, or -1.
 *
 * The one thing a bridged destination needs. The route to a machine on the LAN names the bridge and
 * not the port; the opposite direction of this very connection was punted from that machine, and
 * the frame that was punted says which port it came in on.
 *
 * Which tuple that opposite direction has depends on whether the connection is translated, and both
 * cases say the same thing in different words:
 *
 *   translated     pf's two keys give the untranslated pair, oriented with the rewritten end as the
 *                  source - which is exactly how the other direction's frames arrive, because they
 *                  are punted before translation.
 *   not translated the reverse of this frame's own tuple, because nothing rewrites it either way.
 *
 * This is a lookup in the flow table and not a table of its own: it finds one entry, belonging to
 * one connection, and finds nothing once that connection's entry has been swept.
 */
static int
octep_flow_hint(struct octep_softc *sc, const struct octep_pf_tuple *t,
    const struct octep_pf_state *st)
{
	struct octep_pf_tuple r;
	struct octep_flow *f;
	int hint;

	r = *t;
	if (st->nat_valid) {
		r.sip = st->orig_src;
		r.sport = st->orig_sport;
		r.dip = st->orig_dst;
		r.dport = st->orig_dport;
	} else {
		r.sip = t->dip;
		r.sport = t->dport;
		r.dip = t->sip;
		r.dport = t->sport;
	}

	/*
	 * Unless what came out is this frame's own tuple, in which case there is no other direction
	 * to ask and the answer would be the port the frame arrived on.
	 *
	 * It happens for the outbound half of a translated connection: that half is punted before
	 * translation, so pf's untranslated pair is the tuple in hand. The hint goes unused there
	 * because the route names a port - but a hint that is a hairpin is not a hint, and the one
	 * topology where it would be used is the one where it would be wrong.
	 */
	if (r.sip == t->sip && r.sport == t->sport &&
	    r.dip == t->dip && r.dport == t->dport)
		return (-1);

	mtx_lock(&sc->mtx);
	f = octep_flow_find(sc, &r);
	hint = (f != NULL) ? f->in_dif : -1;
	mtx_unlock(&sc->mtx);
	return (hint);
}

static void
octep_flow_free(struct octep_softc *sc, struct octep_flow *f)
{

	f->used = 0;
	f->in_dif = -1;
	if (sc->dp_flow_used > 0)
		sc->dp_flow_used--;
}

/*
 * Walk the table and take out every flow whose state pf no longer has.
 *
 * This is the half that makes the other half safe to leave running. A microflow outlives nothing by
 * itself: its own timeout is sixty seconds, which is a long time for a connection the firewall has
 * finished with, and a reused five-tuple inside that window would be forwarded on the strength of a
 * connection that is gone.
 *
 * pf_find_state_all_exists is the cheap lookup - it holds no lock on return - which is why it was
 * worth telling apart from the one that does.
 *
 * Called from the link poll, which may sleep and is already periodic. Not from the receive path.
 */
static void
octep_flow_sweep(struct octep_softc *sc)
{
	struct octep_pf_tuple t;
	uint32_t slot, rev, idx, dir;
	int i, gone;

	if (!octep_pf_present())
		return;

	for (i = 2; i < OCTEP_FLOW_MAX; i++) {
		mtx_lock(&sc->mtx);
		if (!sc->dp_flow[i].used) {
			mtx_unlock(&sc->mtx);
			continue;
		}
		t = sc->dp_flow[i].tuple;
		slot = sc->dp_flow[i].slot;
		rev = sc->dp_flow[i].rev;
		idx = sc->dp_flow[i].idx;
		dir = sc->dp_flow[i].dir;
		mtx_unlock(&sc->mtx);

		gone = !octep_pf_state_exists(&t, NULL);
		if (!gone)
			continue;

		/*
		 * Take it out of MF_ACTIVE rather than deleting it. The entry belongs to the fast
		 * path, which made it and will reuse it; what the host owns is whether it is used,
		 * and setting the state back is exactly the inverse of what turned it on.
		 */
		(void)octep_rpc_flow_off(sc, slot, rev, idx, dir);

		mtx_lock(&sc->mtx);
		if (sc->dp_flow[i].used && sc->dp_flow[i].slot == slot) {
			octep_flow_free(sc, &sc->dp_flow[i]);
			sc->dp_auto_gone++;
		}
		mtx_unlock(&sc->mtx);
	}
}

/*
 * Accelerate the flow of the last punted frame: everything that was proven separately, in one call.
 *
 * The parts have all been measured on their own and every one of them cost something to find:
 *
 *   the slot      from the punted frame's own metadata, captured with its tuple in one read
 *                 because reading them from two sysctls is a race under load
 *   the verdict   from pf, reached by walking the linker's files because a weak symbol without a
 *                 dependency can only ever be zero
 *   the NAT       from pf's two keys, in the connection's orientation and not the frame's
 *   the next hop  from the host's routing table and its ARP, because our own table would be wrong
 *                 in the way that is hardest to notice
 *   the state     2, MF_ACTIVE, which is the field that decides whether any of it is used
 *
 * The order matters: the next hop must exist before a microflow points at it, and the connection
 * before the microflow names it, so the three requests go out in that order and the first failure
 * stops the rest.
 *
 * It is a sysctl and not an automatic action on purpose. Every piece is tested; the combination is
 * not, and a combination that programs a flow on every punted frame would, if it were wrong, be
 * wrong on every connection at once.
 */
static int
octep_dp_flow_make(struct octep_softc *sc, const struct octep_pf_tuple *tin, uint32_t slot,
    uint32_t rev, int in_dif, uint16_t tag, struct sbuf *sb, int *stop)
{
	struct octep_pf_tuple t = *tin;
	struct octep_pf_state st;
	struct octep_nhop nh;
	struct octep_flow *f;
	uint32_t dst, idx;
	int err, hint;

	if (!octep_pf_present()) {
		sbuf_cat(sb, "pf is not loaded, so there is no verdict to act on\n");
		return (ENXIO);
	}
	if (slot == 0) {
		sbuf_cat(sb, "the frame named no flow: the offload gate is off, and with it off the "
		    "metadata carries flow id 0\n");
		return (ENOENT);
	}
	if (!octep_pf_state_read(&t, &st)) {
		sbuf_cat(sb, "pf has no state for that frame's tuple\n");
		return (ENOENT);
	}

	/*
	 * Where the frame should go after translation. For a connection whose source is rewritten
	 * on the way out, a reply is heading for the machine behind the firewall - which is
	 * orig_src, the untranslated originator. Without translation it is simply the destination.
	 */
	if (st.nat_valid)
		dst = st.nat_snat ? st.orig_src : st.orig_dst;
	else
		dst = t.dip;

	/*
	 * The port to fall back on if the route names an interface this driver does not own, which
	 * on this appliance means the bridge its LAN ports are members of.
	 */
	hint = octep_flow_hint(sc, &t, &st);

	err = octep_nhop_resolve(sc, dst, hint, &nh);
	if (err != 0) {
		sbuf_printf(sb, "no next hop for 0x%08x: %s\n", dst,
		    err == EWOULDBLOCK ? "the neighbour is not resolved yet, and asking for it has "
		    "just been done - try again in a moment" :
		    err == ENETUNREACH ? (hint < 0 ? "the route leaves by an interface this driver "
		    "does not own, and the other direction of this flow has not been punted from a "
		    "front port, so there is nothing to say which port the destination is on" :
		    "the route leaves by an interface this driver does not own, and the port the "
		    "other direction arrives on has no link") : "no route");
		return (err);
	}

	/*
	 * An index of its own, and the entry recorded before the request goes out - so a sweep
	 * running between the two finds it and takes it back out, rather than leaving a flow active
	 * with nothing tracking it.
	 */
	mtx_lock(&sc->mtx);
	f = octep_flow_find(sc, &t);
	if (f == NULL)
		f = octep_flow_alloc(sc, &t, slot, rev, in_dif, tag);
	else if (in_dif >= 0) {
		/*
		 * A flow seen again: re-record where it arrives. The entry outlives any one frame
		 * and the port is the one thing in it that can change while it does.
		 *
		 * Only ever overwritten by an answer. A punted frame whose tag no interface owns -
		 * one the fast path sent to the host's own port - says nothing about where this
		 * flow's machine is, and taking it for an answer would erase the one there is.
		 */
		f->in_dif = in_dif;
		f->in_tag = tag;
	}
	/*
	 * And which direction of the connection this is, kept so the sweep can turn the flow off
	 * with the same value it was programmed with - the far side indexes per-direction state by
	 * it, so an invalidate carrying the other one addresses the wrong half.
	 */
	if (f != NULL)
		f->dir = (st.order & 1) ? OCTEP_CONN_DIR_REPLY : OCTEP_CONN_DIR_ORIGINAL;
	idx = (f != NULL) ? f->idx : 0;
	mtx_unlock(&sc->mtx);
	if (f == NULL) {
		sbuf_printf(sb, "no room: all %d flow table entries are in use\n",
		    OCTEP_FLOW_MAX - 2);
		if (stop != NULL)
			*stop = 1;		/* the next candidate has nowhere to go either */
		return (ENOSPC);
	}

	err = octep_rpc_flow(sc, slot, rev, idx, &st, &nh);
	if (err != 0) {
		mtx_lock(&sc->mtx);
		octep_flow_free(sc, f);
		mtx_unlock(&sc->mtx);
		sbuf_printf(sb, "programming refused: %d\n", err);
		/*
		 * Worth giving up the whole drain for. A refused post is almost always the far side
		 * not answering, and each one costs up to OCTEP_RPC_CMD_WAIT_MS with the softc lock
		 * held - so pressing on would hold that lock against the transmit path for as long
		 * as there are candidates. This assignment was missing from the first version and
		 * the bound the comment on the drain promised did not exist.
		 */
		if (stop != NULL)
			*stop = 1;
		return (err);
	}

	sbuf_printf(sb, "slot %u rev %u accelerated as entry %u\n", slot, rev, idx);
	sbuf_printf(sb, "  to %02x:%02x:%02x:%02x:%02x:%02x on interface %u, mtu %u%s\n",
	    nh.dmac[0], nh.dmac[1], nh.dmac[2], nh.dmac[3], nh.dmac[4], nh.dmac[5],
	    nh.iface, nh.mtu, nh.from_flow ?
	    "  (port from the other direction of this flow, not from the route)" : "");
	if (st.nat_valid)
		sbuf_printf(sb, "  %s, 0x%08x:%u becomes 0x%08x:%u\n",
		    st.nat_snat ? "do_snat" : "do_dnat",
		    st.nat_src, ntohs(st.nat_sport), st.orig_src, ntohs(st.orig_sport));
	else
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
	int in_dif;

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
	mtx_unlock(&sc->mtx);

	if (seq == 0 || seq != pseq) {
		sbuf_cat(sb, "no punted frame to act on\n");
		return (ENOENT);
	}
	return (octep_dp_flow_make(sc, &t, slot, rev, in_dif, tag, sb, NULL));
}

/*
 * Turn candidates into flows, up to OCTEP_FLOW_PER_POLL of them.
 *
 * This is what the automatic trigger runs instead of acting on the single capture slot. The
 * measurement that made it necessary: on a 186 Mbit/s download the old path accelerated three flows
 * in eight seconds while 57,212 frames punted in five, because it could only ever learn one flow per
 * poll and the slot it learned from was overwritten eleven thousand times a second.
 *
 * Called from the link poll with the vnet set and nothing locked.
 *
 * THE DRAIN STOPS AT THE FIRST FAILURE, deliberately. Each flow is three posted commands and each
 * can wait OCTEP_RPC_CMD_WAIT_MS for a reply while holding the softc lock, so a poll that pressed on
 * through eight timeouts would hold that lock for most of a minute. One failure is almost always the
 * far side being unreachable, in which case the next seven would fail the same way; stopping bounds
 * a poll's worst case to one timeout, which is what it was when it programmed one flow.
 *
 * A candidate whose flow the table already has is dropped without a command, which is what makes the
 * repeated frames of one connection free rather than merely deduplicated.
 */
static void
octep_dp_flow_drain(struct octep_softc *sc)
{
	struct octep_pf_tuple t;
	struct octep_flow_cand *e;
	struct sbuf *sb;
	uint32_t slot, rev, stamp;
	uint16_t tag;
	int i, in_dif, tried, stop;

	tried = 0;
	for (i = 0; i < OCTEP_FLOW_CAND_MAX && tried < OCTEP_FLOW_PER_POLL; i++) {
		e = &sc->dp_cand[i];

		/*
		 * The same trylock the writers take. The reader skipping a busy slot costs one poll's
		 * attention to one candidate, and the alternative - waiting - is the receive path
		 * waiting on the poll.
		 */
		if (atomic_cmpset_acq_32(&e->busy, 0, 1) == 0)
			continue;
		stamp = e->stamp;
		if (stamp == e->seen) {
			atomic_store_rel_32(&e->busy, 0);
			continue;
		}
		t = e->tuple;
		slot = e->slot;
		rev = e->rev;
		in_dif = e->in_dif;
		tag = e->in_tag;
		e->seen = stamp;
		atomic_store_rel_32(&e->busy, 0);

		mtx_lock(&sc->mtx);
		if (octep_flow_find(sc, &t) != NULL) {
			sc->dp_cand_known++;
			mtx_unlock(&sc->mtx);
			continue;
		}
		mtx_unlock(&sc->mtx);

		sb = sbuf_new_auto();
		if (sb == NULL)
			return;
		stop = 0;
		/*
		 * Counted as an attempt whether or not it worked, so the budget bounds the work this
		 * poll does rather than the flows it manages to create. Counting only successes
		 * would let a run of candidates that all fail walk the whole table.
		 */
		tried++;
		if (octep_dp_flow_make(sc, &t, slot, rev, in_dif, tag, sb, &stop) == 0) {
			sc->dp_auto_made++;
			sc->dp_cand_taken++;
		}
		sbuf_delete(sb);

		/*
		 * Only a posted command failing, or the table being full, is worth giving up the whole
		 * drain for. The ordinary refusals - pf has no state for this tuple yet, the route has
		 * no front port, the neighbour is not resolved - are frequent, cost no command, and say
		 * nothing about the next candidate. Treating them as fatal was the first version of this
		 * loop and it would have stopped on the first DNS query that had already closed.
		 */
		if (stop != 0)
			break;
	}
}

static int
octep_sysctl_dp_accelerate(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct sbuf *sb;
	int error;

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
 * The flow table, entry by entry.
 *
 * dp.flows is a count, which answers how many and nothing else. Which five-tuple is at which index,
 * and which front port each one arrives on, is what a wrong forward has to be read out of - and the
 * ingress port is now load-bearing, because it is the egress port the other direction uses when the
 * route names a bridge.
 */
static int
octep_sysctl_dp_flow_table(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct octep_flow f;
	struct sbuf *sb;
	char name[IFNAMSIZ];
	int error, i, n;

	sb = sbuf_new_for_sysctl(NULL, NULL, 1024, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);

	/*
	 * One entry at a time under the lock, as the sweep does, and the printing outside it: an
	 * sbuf backed by a sysctl drains to userspace and may sleep, and the whole table on the
	 * stack is three and a half kilobytes of it for no reason.
	 */
	n = 0;
	for (i = 0; i < OCTEP_FLOW_MAX; i++) {
		mtx_lock(&sc->mtx);
		f = sc->dp_flow[i];
		name[0] = '\0';
		if (f.used && f.in_dif >= 0 && f.in_dif < OCTEP_DP_IF_MAX &&
		    sc->dp_if[f.in_dif].ifp != NULL)
			strlcpy(name, if_name(sc->dp_if[f.in_dif].ifp), sizeof(name));
		mtx_unlock(&sc->mtx);

		if (!f.used)
			continue;
		n++;
		sbuf_printf(sb, "%2d  slot %u rev %u  proto %u  "
		    "0x%08x:%u -> 0x%08x:%u  in %s (tag 0x%04x)\n",
		    i, f.slot, f.rev, f.tuple.proto,
		    f.tuple.sip, ntohs(f.tuple.sport),
		    f.tuple.dip, ntohs(f.tuple.dport),
		    name[0] != '\0' ? name : "no front port", f.in_tag);
	}
	if (n == 0)
		sbuf_cat(sb, "the table is empty\n");

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
	return (octep_dp_if_attach(sc, (uint16_t)val));
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

void
octep_dp_add_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
    struct sysctl_oid_list *top)
{
	struct sysctl_oid *node;
	int i;

	/*
	 * -1 and not 0, because 0 is a front port. Nothing reads in_dif of an entry that is not in
	 * use, but an index that means "none" has to be a value no index can be.
	 */
	for (i = 0; i < OCTEP_FLOW_MAX; i++)
		sc->dp_flow[i].in_dif = -1;

	node = SYSCTL_ADD_NODE(ctx, top, OID_AUTO, "dp", CTLFLAG_RD, NULL,
	    "one SDP datapath ring pair - allocated only when asked");
	if (node == NULL)
		return;

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "up",
	    CTLFLAG_RD, &sc->dp_up, 0, "1 when this driver has programmed the ring");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ring",
	    CTLFLAG_RW, &sc->dp_ring, 0, "which SDP ring to use; only while down");
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
	    "the first credit written to R_OUT_SLIST_DBELL when a ring is armed. 0 derives it, "
	    "which is oq_rsize times credit_unit and is the only value that works: the register's "
	    "unit is not the entry, and a ring granted one unit per entry delivers a single packet "
	    "and then stops. Only while down");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_rsize",
	    CTLFLAG_RW, &sc->dp_oq_rsize, 0,
	    "how many scatter-list entries to publish in R_OUT_SLIST_RSIZE, and to grant. The "
	    "buffers behind them are always allocated in full, so a smaller value simply hides the "
	    "rest from the block - which is how a ring small enough to force a wrap gets tested. "
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
	    "same path, and the link poll sends one every second, so a buffer that keeps the last "
	    "untagged frame keeps a control message. Zero keeps any");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_untagged_frame",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_rx_untag_frame, "A",
	    "the head of the last frame dropped for a tag no interface owns, with that tag. This is "
	    "how a frame the fast path forwarded is read: point a next hop at the host's own DPDK "
	    "port and the frame arrives here, tagged for nothing, and is kept");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "auto",
	    CTLFLAG_RW, &sc->dp_auto, 0,
	    "make a flow every second without being asked, from whatever the last punted frame was. "
	    "Off, and it should stay off until accelerate has been run by hand for a while: every "
	    "piece of it is tested and the combination is not, and a combination that is wrong is "
	    "wrong on every connection at once");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "auto_made",
	    CTLFLAG_RD, &sc->dp_auto_made, 0, "flows programmed without being asked");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "auto_gone",
	    CTLFLAG_RD, &sc->dp_auto_gone, 0,
	    "flows taken out of MF_ACTIVE because pf no longer had their state");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "auto_full",
	    CTLFLAG_RD, &sc->dp_auto_full, 0, "times the flow table had no room");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_pushed",
	    CTLFLAG_RD, &sc->dp_cand_pushed, 0,
	    "punted frames offered as candidates for acceleration. Far fewer than the frames punted, "
	    "because every frame of one connection lands on one slot");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_taken",
	    CTLFLAG_RD, &sc->dp_cand_taken, 0, "candidates the poll turned into flows");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_known",
	    CTLFLAG_RD, &sc->dp_cand_known, 0,
	    "candidates whose flow the table already had, dropped without a command");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_clash",
	    CTLFLAG_RD, &sc->dp_cand_clash, 0,
	    "writers that found a candidate slot busy and gave up. Two rings writing at the same "
	    "instant, which costs one candidate and is harmless");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_lost",
	    CTLFLAG_RD, &sc->dp_cand_lost, 0,
	    "candidates overwritten before the poll had read them. This is the collision that costs "
	    "something - two connections sharing a slot, each evicting the other - and it is the one "
	    "to watch if acceleration covers less traffic than the table has room for");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_forgot",
	    CTLFLAG_RD, &sc->dp_flow_forgot, 0,
	    "entries dropped from this table because a ruleset reload discarded the whole of the "
	    "far side's - see rpc.fw_rev_bump");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flows",
	    CTLFLAG_RD, &sc->dp_flow_used, 0, "flows currently accelerated");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_table",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_flow_table, "A",
	    "every accelerated flow: its index, the microflow slot it was programmed at, its "
	    "five-tuple, and the front port its frames arrive on - which is the port the other "
	    "direction leaves by when the route names the bridge instead of a port");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "accelerate",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_dp_accelerate, "A",
	    "read this to accelerate the flow of the last punted frame: the slot from its metadata, "
	    "the verdict and the translation from pf, the next hop from the host's own route and "
	    "ARP. Reports what it did or why it could not. A read with side effects, deliberately: "
	    "it is one flow at a time and index 1 is reused, so a second read replaces the first");
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
	    "passes beyond the first inside one handler entry that still took packets - the "
	    "empty pass that ends the loop is not one of them, so four productive passes add "
	    "three. It climbs under load, because the block's write is a DMA and frames land "
	    "while the handler is running: a second pass that finds three more frames is an "
	    "ordinary one, not a burst the first could not drain. The stall this loop exists "
	    "for showed itself in R_OUT_CNTS left above the interrupt level, not here");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rxwd_runs",
	    CTLFLAG_RD, &sc->dp_rxwd_runs, 0,
	    "watchdog passes that found packets waiting. On a healthy ring this stays near "
	    "zero: it climbing means interrupts are being missed and the timer is carrying "
	    "the traffic");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rxwd_ticks",
	    CTLFLAG_RW, &sc->dp_rxwd_ticks, 0,
	    "the watchdog's period in ticks, taken when the ring comes up. Lower costs one "
	    "register read per armed ring per pass and bounds a lost interrupt more tightly");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "msix_count",
	    CTLFLAG_RD, &sc->dp_msix_count, 0, "MSI-X messages allocated");
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
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "credit_unit",
	    CTLFLAG_RW, &sc->dp_credit_unit, 0,
	    "doorbell units each received buffer is credited with. 16 is what keeps the block "
	    "writing; the block spends 1 per descriptor, so the credit is held at or under the "
	    "ring's grant rather than allowed to climb");

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
	    "the PKIND the coprocessor assigned; 40 + num_vfs, and num_vfs is 0 here");
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
 * wrote over buffers the host had not taken, see OCTEP_DP_CREDIT_UNIT - the buffer at the read index
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
	uint32_t d, first, held, idx;
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
	}
	if (sc->dp_oq_gap[ring] == 0) {
		sc->dp_oq_gap[ring] = ticks;
		return;
	}
	if (ticks - sc->dp_oq_gap[ring] < OCTEP_DP_RESYNC_TICKS)
		return;
	sc->dp_oq_gap[ring] = 0;
	if (first != 0) {
		sc->dp_oq_rd[ring] = (sc->dp_oq_rd[ring] + first) % sc->dp_oq_rsize;
		sc->dp_rx_resync++;
		sc->dp_rx_skipped += first;
	}
	if (n > held && sc->dp_ack_cnts != 0)
		octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_CNTS, n - held);
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
octep_dp_rx_tuple(struct octep_softc *sc, uint16_t tag, uint32_t slot, uint32_t rev,
    const uint8_t *f, uint32_t flen)
{
	/*
	 * Parsed into a local and published afterwards, and the slot and the revision are arguments
	 * the caller decoded from its own buffer.
	 *
	 * Nothing that reaches a candidate may be read back out of the softc. The capture fields
	 * below are one set shared by every ring, and up to eight rings run this concurrently from
	 * their own MSI-X handlers - so a candidate assembled from them can hold one connection's
	 * addresses with another's flow slot, and a microflow programmed from that pair forwards one
	 * connection's packets with another's translation, in hardware, past pf. The per-entry lock
	 * on the candidate table cannot help: it guards where the candidate is written, not where it
	 * is read from. Review caught this before it ran.
	 */
	struct octep_pf_tuple t;
	const uint8_t *ip;
	uint32_t ihl;
	uint16_t etype;

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
		return;

	etype = be16dec(f + 12);
	if (etype != ETHERTYPE_IP)
		return;

	ip = f + ETHER_HDR_LEN;
	if ((ip[0] >> 4) != 4)
		return;
	ihl = (uint32_t)(ip[0] & 0x0f) * 4;
	if (ihl < 20)
		return;

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
	    flen >= ETHER_HDR_LEN + ihl + 4) {
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
	    flen >= ETHER_HDR_LEN + ihl + 6 &&
	    (ip[ihl] == 0 || ip[ihl] == 8)) {
		memcpy(&t.sport, ip + ihl + 4, 2);
		t.dport = htons(8);	/* ICMP_ECHO, as pf's virtual_type */
	}

	/*
	 * Offer it for acceleration FIRST, from the locals, before any of this touches the shared
	 * capture. A frame whose metadata names no flow - which is every frame while the offload gate
	 * is shut - is not a candidate, because the slot is what a flow is programmed by.
	 */
	if (slot != 0)
		octep_flow_cand_push(sc, &t, slot, rev, tag);

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
 * Returns how many packets were acknowledged.
 */
static uint32_t
octep_dp_oq_service(struct octep_softc *sc, struct octep_dma *bufs, uint32_t ring)
{
	struct mbuf *mh = NULL, *mt = NULL, *m;
	struct epoch_tracker et;
	uint64_t cnts, istat;
	uint32_t n, i, taken, credit;
	int rc;

	/*
	 * One servicer per ring at a time.
	 *
	 * Until there were interrupts there was only ever one path in here, so this did not matter.
	 * Now a ring's MSI-X handler and a sysctl-driven sweep can both arrive, and both read
	 * R_OUT_CNTS and both return credits for what they read - which double-counts the packets
	 * and over-credits the block. Measured before this: 523 packets reported for 400 frames,
	 * and a doorbell that had gone above the grant it started from.
	 *
	 * A ring index is bounded by the sibling array, so this needs no lock of its own.
	 */
	if (ring > OCTEP_DP_SIBLINGS_MAX)
		return (0);
	if (atomic_cmpset_int(&sc->dp_oq_busy[ring], 0, 1) == 0)
		return (0);

	/*
	 * AFTER the flag is taken, not before. Before it, a servicer could pass this test and acquire
	 * the flag after octep_dp_rx_quiesce() had finished waiting on it - which is exactly the
	 * servicer the quiesce exists to exclude.
	 */
	if (atomic_load_acq_int(&sc->dp_rx_quiesce) != 0) {
		atomic_store_rel_int(&sc->dp_oq_busy[ring], 0);
		return (0);
	}
	rc = 0;

	/*
	 * Clear the latched output status first, and do it whether or not anything arrived.
	 *
	 * R_OUT_INT_STATUS is write-one-to-clear and the vendor's driver never touches it - because
	 * the vendor runs with interrupts, and servicing the interrupt is what clears it there. This
	 * driver has no interrupt consumer, so the bit set by the first packet stayed set, and
	 * nothing arrived after it. Measured: four frames sent one at a time, OUT_PKT_CNT 1,
	 * ISTAT 0x010 latched, and frames two, three and four never written.
	 */
	istat = octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_INT_STATUS);
	if (istat != 0)
		octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_INT_STATUS, istat);

	cnts = octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_CNTS);
	n = (uint32_t)(cnts & 0xffffffffULL);
	if (n == 0)
		goto out;
	if (n > sc->dp_oq_rsize)
		n = sc->dp_oq_rsize;

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
	 * Counting it was a real defect and not a cosmetic one. The drain loop in octep_dp_intr
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
	for (i = 0; i < n; i++) {
		struct octep_dp_if *dif;
		struct mbuf *m;
		uint8_t *b;
		uint64_t blen;
		uint32_t idx, flen, ident;
		uint16_t tag;

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
		sc->dp_oq_rd[ring] = (idx + 1) % sc->dp_oq_rsize;
		taken++;

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
		octep_dp_rx_tuple(sc, tag, ident & 0x01ffffffu, (ident >> 25) & 0x3fu,
		    b + OCTEP_RX_PREFIX_LEN,
		    (uint32_t)blen - (OCTEP_RX_PREFIX_LEN - 8));

		/*
		 * The length counts everything after the first qword, and the Ethernet header
		 * starts at OCTEP_RX_PREFIX_LEN - so the frame is the length less the rest of the
		 * prefix. Measured on the frame that first completed the loop: length 134, prefix
		 * 82, and a 60-byte frame behind it.
		 */
		flen = (uint32_t)blen - (OCTEP_RX_PREFIX_LEN - 8);
		/*
		 * Bound it by what is left AFTER the prefix, not by the buffer.
		 *
		 * The copy below starts at b + OCTEP_RX_PREFIX_LEN, so a frame may be at most
		 * OCTEP_DP_BUF_SIZE - OCTEP_RX_PREFIX_LEN bytes - 1520 here, not 1602. Bounding it
		 * by the whole buffer let a long length read up to 82 bytes past the end of the DMA
		 * buffer and into whatever the next one holds, which is the far side's length field
		 * deciding how far this host reads. OCTEP_DP_IF_MTU_MAX is the same arithmetic and
		 * was already right; this site was not.
		 */
		if (flen < ETHER_HDR_LEN ||
		    flen > OCTEP_DP_BUF_SIZE - OCTEP_RX_PREFIX_LEN)
			goto repoison;

		dif = octep_dp_if_by_tag(sc, tag);
		if (dif == NULL || dif->ifp == NULL) {
			sc->dp_rx_untagged++;
			/*
			 * Keep this one. A frame with a tag no interface owns is, on this appliance,
			 * either a stray or a frame the fast path was asked to forward to the host's
			 * own DPDK port - and the second is the only way to read what the fast path
			 * actually builds, because a frame forwarded to a front port never comes here.
			 */
			if (sc->dp_rx_untag_want != 0 &&
			    sc->dp_rx_untag_want != (uint32_t)tag)
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
		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m == NULL) {
			dif->rx_nobuf++;
			if_inc_counter(dif->ifp, IFCOUNTER_IQDROPS, 1);
			goto repoison;
		}
		memcpy(mtod(m, void *), b + OCTEP_RX_PREFIX_LEN, flen);
		m->m_len = m->m_pkthdr.len = flen;
		m->m_pkthdr.rcvif = dif->ifp;
		m->m_nextpkt = NULL;
		if (mt == NULL)
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
	}
	bus_dmamap_sync(bufs->tag, bufs->map, BUS_DMASYNC_PREREAD);

	/*
	 * Hand the buffers back - but never past the grant the ring was armed with.
	 *
	 * Sixteen units per buffer is what keeps the block writing, and the block spends one per
	 * descriptor, so returning sixteen for every packet raised a ring's credit by fifteen for every
	 * packet it carried, without limit - see OCTEP_DP_CREDIT_UNIT. A ring that had carried a day's
	 * downloads held 1,026,112, and with that much the block wrote over buffers the host had not
	 * taken. So the register is read and the credit cut to what brings it back to the grant. The
	 * low half is signed: a ring that fetched a batch it had no credit for reads below zero.
	 */
	if (taken == 0) {
		octep_dp_oq_resync(sc, bufs, ring, n);
		goto out;
	}
	sc->dp_oq_gap[ring] = 0;
	{
		int64_t have, ceiling;

		have = (int32_t)(octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_SLIST_DBELL) &
		    0xffffffffULL);
		if (have < 0)
			have = 0;
		ceiling = octep_dp_oq_first_grant(sc);
		credit = taken * sc->dp_credit_unit;
		if (have >= ceiling)
			credit = 0;
		else if ((int64_t)credit > ceiling - have)
			credit = (uint32_t)(ceiling - have);
		if (credit != taken * sc->dp_credit_unit)
			sc->dp_credit_capped++;
	}
	if (credit != 0)
		octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_SLIST_DBELL, credit);

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
	if (sc->dp_ack_cnts != 0)
		octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_CNTS, taken);
	rc = (int)taken;
out:
	/*
	 * Hand the frames up while the ring is still held, and release it after.
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
	 * It has to be here rather than in the callers because there are three of them and none is
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
	atomic_store_rel_int(&sc->dp_oq_busy[ring], 0);
	return ((uint32_t)rc);
}

/*
 * Service every armed ring. Which ring a frame lands on is the fast path's choice, not the host's,
 * so all of them have to be served and not only the one transmit uses.
 */
uint32_t
octep_dp_service(struct octep_softc *sc)
{
	uint32_t i, done;

	if (sc->dp_up == 0)
		return (0);

	done = octep_dp_oq_service(sc, &sc->dp_bufs, sc->dp_ring);
	for (i = 0; i < OCTEP_DP_SIBLINGS_MAX; i++) {
		struct octep_dp_oq *oq = &sc->dp_sib[i];

		if (oq->armed != 0)
			done += octep_dp_oq_service(sc, &oq->bufs, oq->ring);
	}
	sc->dp_rx_done += done;
	return (done);
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
 */
static int
octep_dp_if_transmit(if_t ifp, struct mbuf *m)
{
	struct octep_dp_if *dif = if_getsoftc(ifp);
	struct octep_softc *sc;
	uint8_t *d;
	uint32_t len, wire, slot;

	if (m == NULL)
		return (0);
	if (dif == NULL || (sc = dif->sc) == NULL) {
		m_freem(m);
		return (ENETDOWN);
	}
	len = m->m_pkthdr.len;
	if (len == 0 || len + OCTEP_TOTAL_TAG_LEN > OCTEP_DP_BUF_SIZE) {
		dif->tx_drops++;
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		m_freem(m);
		return (EMSGSIZE);
	}

	mtx_lock(&sc->mtx);
	if (sc->dp_up == 0) {
		mtx_unlock(&sc->mtx);
		dif->tx_drops++;
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		m_freem(m);
		return (ENETDOWN);
	}

	/*
	 * Tap before the copy, so a capture on this interface sees what left it. Without this
	 * tcpdump shows only the receive direction, which is exactly the half that is easy to see
	 * by other means.
	 */
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
	if ((octep_dp_rd(sc, OCTEP_SDP_R_IN_INSTR_DBELL) & 0xffffffffULL) >=
	    OCTEP_DP_IQ_DESCS - 1) {
		/*
		 * Unlock and free before returning. The first version of this check did neither,
		 * which left sc->mtx held for good the first time the ring filled - every path in
		 * the driver takes it - and leaked the mbuf with it. It never fired on this
		 * appliance, so nothing showed; a guard whose error path is wrong is worse than no
		 * guard, because it only runs when something is already going badly.
		 */
		sc->dp_tx_iq_full++;
		mtx_unlock(&sc->mtx);
		m_freem(m);
		return (ENOBUFS);
	}
	slot = sc->dp_iq_prod;
	d = (uint8_t *)sc->dp_txbufs.vaddr + (size_t)slot * OCTEP_DP_BUF_STRIDE;
	wire = len < OCTEP_MIN_FRAME ? OCTEP_MIN_FRAME : len;

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
	if (wire > len)
		bzero(d + OCTEP_TOTAL_TAG_LEN + len, wire - len);
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
	if (sc->dp_meta_tpl_len != 0)
		memcpy(d + OCTEP_PPORT_HLEN, sc->dp_meta_tpl,
		    sc->dp_meta_tpl_len > OCTEP_CUSTOM_META_LEN ? OCTEP_CUSTOM_META_LEN :
		    sc->dp_meta_tpl_len);
	/*
	 * Ask the coprocessor to encrypt this frame, when this interface is the one named.
	 *
	 * The two bytes were found by bisection on the appliance - a walking pattern over the 64,
	 * with each offset removed in turn - and the vendor's own header then named them. From
	 * metadata.h, struct usfp_sp_md:
	 *
	 *     +0   uint8_t  md_valid
	 *     +1   uint8_t  sa_is_out        IPsec offload direction is output (encrypt)
	 *     +8   usfp_mflow_ident flow
	 *     +12  uint32_t sa_index         IPsec offload SA index, 0 means no offload
	 *
	 * So bisection had found sa_is_out and sa_index, and the index is a 32-bit field rather
	 * than the single byte written here before - which did not matter while the only index
	 * ever tried was 1, and would have quietly truncated any other.
	 *
	 * Default off, and one interface at a time.
	 */
	if (sc->dp_sa_idx != 0 && sc->dp_sa_if == (int)(dif - sc->dp_if)) {
		d[OCTEP_PPORT_HLEN + 1] = 1;
		le32enc(d + OCTEP_PPORT_HLEN + 12, sc->dp_sa_idx);
	}
	m_copydata(m, 0, len, (caddr_t)(d + OCTEP_TOTAL_TAG_LEN));

	bus_dmamap_sync(sc->dp_txbufs.tag, sc->dp_txbufs.map, BUS_DMASYNC_PREWRITE);
	octep_dp_build_instr(sc, slot,
	    sc->dp_txbufs.paddr + (bus_addr_t)slot * OCTEP_DP_BUF_STRIDE,
	    OCTEP_TOTAL_TAG_LEN + wire);
	bus_dmamap_sync(sc->dp_iq.tag, sc->dp_iq.map, BUS_DMASYNC_PREWRITE);
	sc->dp_iq_prod = (sc->dp_iq_prod + 1) % OCTEP_DP_IQ_DESCS;
	sc->dp_tx_posted++;
	octep_dp_wr(sc, OCTEP_SDP_R_IN_INSTR_DBELL, 1);
	mtx_unlock(&sc->mtx);

	dif->tx_packets++;
	dif->tx_bytes += len;
	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_OBYTES, len);
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
			ifmr->ifm_active |= IFM_1000_T;
			break;
		case 10000:
			ifmr->ifm_active |= IFM_10G_SR;
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
	 * The sweep comes first and runs whether or not anything is being made automatically,
	 * because a flow made by hand needs taking out just as much as one made here. The making
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
	octep_flow_sweep(sc);
	if (sc->dp_auto != 0)
		octep_dp_flow_drain(sc);
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
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < 72 || ifr->ifr_mtu > OCTEP_DP_IF_MTU_MAX)
			err = EINVAL;
		else
			if_setmtu(ifp, ifr->ifr_mtu);
		break;
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
		if (label != NULL) {
			char *d = if_allocdescr(OCTEP_DP_DESCR_LEN, M_NOWAIT);

			if (d != NULL) {
				snprintf(d, OCTEP_DP_DESCR_LEN, "XGS front port %s", label);
				if_setdescr(ifp, d);
			}
		}
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
 * does what dp.service does for that ring and nothing else, because the question it exists to
 * answer is whether the block needs its interrupt taken at all: every register the host can write
 * by hand has been written by hand, and the ring still stops after one packet.
 */
static void
octep_dp_intr(void *arg)
{
	struct octep_dp_vec *vec = arg;
	struct octep_softc *sc = vec->sc;
	struct octep_dma *bufs;
	uint32_t n, rounds;

	vec->count++;
	sc->dp_intr_taken++;
	if (sc->dp_up == 0)
		return;
	bufs = octep_dp_ring_bufs(sc, vec->ring);
	if (bufs == NULL)
		return;

	/*
	 * Go round until the ring is empty.
	 *
	 * One pass was the defect. A pass reads R_OUT_CNTS, takes at most RSIZE packets and
	 * acknowledges those, so a burst bigger than the ring leaves the count above the interrupt
	 * level - and the block raises the interrupt on that level being crossed, not on its being
	 * exceeded. Nothing is left to raise it again and the ring is dead until something else
	 * touches it. This is what made a download arrive as nothing at all while a ping, which
	 * never fills a ring, came back in 0.6 ms.
	 *
	 * Leaving with work still queued is the one case that must not happen silently, so when the
	 * bound is reached the watchdog is asked for the next tick.
	 */
	for (rounds = 0; rounds < OCTEP_DP_OQ_DRAIN_ROUNDS; rounds++) {
		n = octep_dp_oq_service(sc, bufs, vec->ring);
		if (n == 0)
			break;
		sc->dp_rx_done += n;
		if (rounds != 0)
			sc->dp_intr_drained++;
	}
	if (rounds == OCTEP_DP_OQ_DRAIN_ROUNDS)
		octep_dp_rxwd_kick(sc);
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
	}

	device_printf(sc->dev, "dp: %d MSI-X messages allocated, %u of %u rings hooked, ring n "
	    "on table entry %u + n\n", count, hooked, n, OCTEP_DP_MSIX_RING_BASE);
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
