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
	    (size_t)OCTEP_DP_OQ_DESCS * OCTEP_DP_BUF_SIZE);
	bus_dmamap_sync(sc->dp_bufs.tag, sc->dp_bufs.map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bzero(sc->dp_info.vaddr, (size_t)OCTEP_DP_OQ_DESCS * OCTEP_DP_OQ_INFO_SIZE);
	for (i = 0; i < OCTEP_DP_OQ_DESCS; i++) {
		e[i * 2] = (uint64_t)sc->dp_bufs.paddr +
		    ((uint64_t)i * OCTEP_DP_BUF_SIZE);
		e[i * 2 + 1] = (uint64_t)sc->dp_info.paddr +
		    ((uint64_t)i * OCTEP_DP_OQ_INFO_SIZE);
	}
	bus_dmamap_sync(sc->dp_info.tag, sc->dp_info.map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->dp_slist.tag, sc->dp_slist.map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

int
octep_dp_start(struct octep_softc *sc)
{
	uint64_t v;
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
	    (bus_size_t)OCTEP_DP_OQ_DESCS * OCTEP_DP_BUF_SIZE, PAGE_SIZE, "dp buffers");
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

	/* BSIZE and ISIZE share the low 23 bits; ISIZE stays zero in buffer-pointer-only mode. */
	v = octep_dp_rd(sc, OCTEP_SDP_R_OUT_CONTROL);
	v &= ~OCTEP_R_OUT_CTL_SIZE_MASK;
	v |= (uint64_t)(OCTEP_DP_BUF_SIZE & 0xffff);
	octep_dp_wr(sc, OCTEP_SDP_R_OUT_CONTROL, v);

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
	sc->dp_up = 1;
	device_printf(sc->dev,
	    "dp: ring %u up - iq %u x %u B at 0x%jx, oq %u x %u B, slist at 0x%jx, "
	    "oq time threshold %u\n",
	    sc->dp_ring, OCTEP_DP_IQ_DESCS, OCTEP_DP_INSTR_SIZE,
	    (uintmax_t)sc->dp_iq.paddr, OCTEP_DP_OQ_DESCS, OCTEP_DP_BUF_SIZE,
	    (uintmax_t)sc->dp_slist.paddr, sc->dp_time_threshold);
	mtx_unlock(&sc->mtx);
	return (0);

fail:
	octep_dp_reset_ring(sc);
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
	 * What goes in the 64 metadata bytes, and why there is a choice.
	 *
	 * The first reading was that the vendor fills them with a walking pattern from 0xc0 and the
	 * fast path validates them, counting what fails in
	 * FPCNTR_FROM_KN_DROP_MISMATCH_METADATA_FIELDS - which is a real counter in the shipped
	 * binary. The vendor's own target application says something different: apps_rxtx.h writes
	 * one big-endian 64-bit signature, 0xa0a1a2a3a4a5a6a7, and leaves the other 56 bytes alone.
	 *
	 * All three were then sent down the same fibre, 25 frames each: the pattern, the signature,
	 * and zeros. Every one of the 75 was consumed, IN_BYTE_CNT matched each time, OUT_PKT_CNT
	 * stayed 0 and not one receive buffer was written. So the content does not decide anything
	 * on this path, and the claim that zeros were a mismatch is withdrawn. The choice stays
	 * because it is the control that established that, and 0 keeps what was sent before.
	 */
	switch (sc->dp_meta_mode) {
	case OCTEP_META_MODE_SIGNATURE:
		memset(d + OCTEP_PPORT_HLEN, 0, OCTEP_CUSTOM_META_LEN);
		be64enc(d + OCTEP_PPORT_HLEN, OCTEP_META_SIGNATURE);
		break;
	case OCTEP_META_MODE_ZERO:
		memset(d + OCTEP_PPORT_HLEN, 0, OCTEP_CUSTOM_META_LEN);
		break;
	default:
		for (i = 0; i < OCTEP_CUSTOM_META_LEN; i++)
			d[OCTEP_PPORT_HLEN + i] = (uint8_t)(OCTEP_META_START + i);
		break;
	}
	d += OCTEP_TOTAL_TAG_LEN;

	memset(d, 0xff, 6);			/* destination MAC: broadcast */
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
	d[34] = 0x00; d[35] = 0x09;		/* source port 9, discard */
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
 * Look for anything the coprocessor has written into the output ring. The length word at the head of
 * a buffer is the arrival flag - the coprocessor zeroes nothing, so a non-zero length there means it
 * filled that buffer - and it is big-endian.
 */
static void
octep_dp_rx_report(struct octep_softc *sc, struct sbuf *sb)
{
	const uint8_t *b;
	uint64_t len, resp;
	uint32_t i, found = 0;

	bus_dmamap_sync(sc->dp_bufs.tag, sc->dp_bufs.map, BUS_DMASYNC_POSTREAD);

	for (i = 0; i < OCTEP_DP_OQ_DESCS; i++) {
		b = (const uint8_t *)sc->dp_bufs.vaddr + ((size_t)i * OCTEP_DP_BUF_SIZE);
		len = be64toh(*(const uint64_t *)(b + OCTEP_RX_LEN_OFF));
		if (len == 0 || len == OCTEP_DP_BUF_POISON_WORD)
			continue;   /* untouched, or written as zero - neither is an arrival */
		found++;
		if (found > 4)
			continue;
		resp = *(const uint64_t *)(b + OCTEP_RX_RESP_OFF);
		sbuf_printf(sb, "  buf %3u  len %ju  resp 0x%016jx  "
		    "opcode 0x%04jx src_port %ju\n", i, (uintmax_t)len, (uintmax_t)resp,
		    (uintmax_t)((resp >> 48) & 0xffff), (uintmax_t)((resp >> 42) & 0x3f));
		sbuf_printf(sb, "           %02x %02x %02x %02x %02x %02x  <- %02x %02x %02x "
		    "%02x %02x %02x  type %02x%02x\n",
		    b[16], b[17], b[18], b[19], b[20], b[21],
		    b[22], b[23], b[24], b[25], b[26], b[27], b[28], b[29]);
	}
	sc->dp_rx_seen = found;
	sbuf_printf(sb, "  %u of %u receive buffers have been written\n",
	    found, OCTEP_DP_OQ_DESCS);
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
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "oq_time_threshold",
	    CTLFLAG_RD, &sc->dp_time_threshold, 0,
	    "output interrupt time threshold, in 1024-clock ticks");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "state",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_state, "A", "the ring's registers, read fresh");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "start",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_start, "I", "allocate the rings and program them");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "meta",
	    CTLFLAG_RW, &sc->dp_meta_mode, 0,
	    "what goes in the 64 metadata bytes: 0 the walking pattern from 0xc0, read out of the "
	    "shipped binary; 1 the vendor source's signature 0xa0a1a2a3a4a5a6a7 with the rest "
	    "zero; 2 all zeros");
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
