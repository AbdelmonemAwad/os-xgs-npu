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
 * with its buffers, programs the ring pair through the sequence below, enables it, and grants the
 * output ring its buffer credits. It does not transmit, and it does not yet read received packets
 * back out - the next step - so nothing here can put a malformed frame on a wire. What it proves is
 * narrower and worth proving on its own: that the host can hand this silicon a ring and have the
 * silicon accept it, which is visible as the ring's IDLE bit going away.
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
 *   1536-byte buffers      `buf_size` reads exactly 1536, and it would be 1602 under CONFIG_PPORT.
 *                          Neither shipped variant has the port-extender overhead.
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
#include <sys/mbuf.h>
#include <sys/endian.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "octep.h"

#define	OCTEP_DP_IDLE_TRIES	1000		/* x 10 us */

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
	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_SLIST_RSIZE, OCTEP_DP_OQ_DESCS);
	v = octep_dp_ring_rd(sc, ring, OCTEP_SDP_R_OUT_CONTROL);
	v &= ~(OCTEP_R_OUT_CTL_SIZE_MASK | OCTEP_R_OUT_CTL_ATTR_MASK);
	v |= OCTEP_R_OUT_CTL_ES_P;
	v |= (uint64_t)(OCTEP_DP_BUF_SIZE & 0xffff);
	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_CONTROL, v);
	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_INT_LEVELS,
	    ((uint64_t)sc->dp_time_threshold << 32) | OCTEP_DP_OQ_INTR_PKT);
	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_ENABLE, 1);
	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_SLIST_DBELL, 0xffffffffULL);
	octep_dp_ring_wr(sc, ring, OCTEP_SDP_R_OUT_SLIST_DBELL, OCTEP_DP_OQ_DESCS);
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
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_SLIST_RSIZE, OCTEP_DP_OQ_DESCS);

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
		for (i = 0; i < OCTEP_DP_IDLE_TRIES; i++) {
			if (octep_dp_rd(sc, OCTEP_SDP_R_IN_INSTR_DBELL) == 0)
				break;
			DELAY(10);
		}
	}
	octep_dp_wr(sc, OCTEP_SDP_R_IN_ENABLE,
	    octep_dp_rd(sc, OCTEP_SDP_R_IN_ENABLE) | 1ULL);

	octep_dp_wr(sc, OCTEP_SDP_R_OUT_SLIST_DBELL, 0xffffffffULL);
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_ENABLE,
	    octep_dp_rd(sc, OCTEP_SDP_R_OUT_ENABLE) | 1ULL);

	/* Now grant the output ring the buffers it may write into. */
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_SLIST_DBELL, OCTEP_DP_OQ_DESCS);

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

	mtx_lock(&sc->mtx);
	if (sc->dp_up == 0) {
		mtx_unlock(&sc->mtx);
		return;
	}
	octep_dp_reset_ring(sc);
	sc->dp_up = 0;
	mtx_unlock(&sc->mtx);

	octep_dp_free_siblings(sc);
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

void
octep_dp_add_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
    struct sysctl_oid_list *top)
{
	struct sysctl_oid *node;

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
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_time_threshold",
	    CTLFLAG_RD, &sc->dp_time_threshold, 0,
	    "output interrupt time threshold, in 1024-clock ticks");

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
