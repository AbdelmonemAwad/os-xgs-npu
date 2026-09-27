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

/* Fill the scatter list: one buffer pointer per entry, and the info word left alone. */
static void
octep_dp_fill_slist(struct octep_softc *sc)
{
	uint64_t *e = (uint64_t *)sc->dp_slist.vaddr;
	uint32_t i;

	for (i = 0; i < OCTEP_DP_OQ_DESCS; i++) {
		e[i * 2] = (uint64_t)sc->dp_bufs.paddr +
		    ((uint64_t)i * OCTEP_DP_BUF_SIZE);
		e[i * 2 + 1] = 0;	/* info_ptr - never read in this mode */
	}
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
	 * The coprocessor has to know a host exists before its side of SDP means anything, and that
	 * is what the EP-mode handshake did. Refuse rather than program rings nothing is listening
	 * to - the register still carries the target's started-port bitmap, so this is checkable.
	 */
	v = bus_read_8(sc->bar0, OCTEP_SLI_EPF_SCRATCH);
	if (v == 0) {
		device_printf(sc->dev, "dp: the target reports no started port "
		    "(SLI_EPF_SCRATCH is zero) - run the handshake and the coprocessor's fast "
		    "path first\n");
		mtx_unlock(&sc->mtx);
		return (ENXIO);
	}

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

	octep_dma_free(&sc->dp_bufs);
	octep_dma_free(&sc->dp_slist);
	octep_dma_free(&sc->dp_iq);
	device_printf(sc->dev, "dp: ring %u down\n", sc->dp_ring);
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
octep_sysctl_dp_state(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct sbuf *sb;
	uint64_t inctl, inen, inbaddr, inrsize, indbell, incnts;
	uint64_t outctl, outen, outbaddr, outrsize, outdbell, outcnts;
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
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "stop",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dp_stop, "I", "disable the ring and release the memory");
}
