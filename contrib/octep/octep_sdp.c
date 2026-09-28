/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * octep_sdp - what SDP, the PCIe datapath, says about itself, and the one handshake that makes the
 * coprocessor notice us.
 *
 * WHY THIS FILE EXISTS. The management facility in octep_mgmt.c carries one interface, octep0, and
 * that is all it was ever meant to carry. The appliance's twelve front ports are behind a different
 * mechanism: the coprocessor's BGX MACs, reached from the host across SDP rings. The vendor's own
 * fast path will not even start until the host has brought SDP up - its launcher reads
 * /sys/module/slipf/parameters/pci_port, counts the PF interfaces the host published, and sleeps
 * until that count is non-zero. With octep's management link fully up it still reads "0 0 0 0 0",
 * which is the measurement that says mgmt_net is not what SDP counts.
 *
 * So the front ports need a host-side SDP datapath, and this file is its first step: find out what
 * the hardware is offering before writing a single register. Everything here is a read of a BAR0
 * CSR that the endpoint publishes about itself, in the same spirit as the barmap parse in octep.c -
 * the management link began the same way.
 *
 * TWO BOUNDS, BOTH DELIBERATE. A ring is only touched if the hardware advertised it in RINFO, and
 * only if its whole register block lies inside BAR0. Neither bound is theoretical: BAR0 is 8 MB and
 * the ring stride is 128 KiB, so ring 63's last register ends at 0x7f0198 and ring 64 would start
 * past the end of the BAR. The two agree, which is a useful check on the decode.
 *
 * THE RING SURVEY WRITES NOTHING. Not the enables, not the base addresses, not the doorbells.
 * Bringing a ring up is a later and much larger change, and the same rule as the management link
 * will apply to it: never announce readiness before the rings are complete.
 *
 * WHAT DOES WRITE, AND ONLY WHEN ASKED, is the SDP/EP-mode handshake at the bottom of this file. It
 * turned out that the coprocessor does not watch the ring registers to decide whether a host-facing
 * port exists - it polls one scratch register and creates the port when a four-step exchange
 * completes. So the gate on the front ports is a handshake, not the ring ladder, and it is far
 * smaller than the ladder would have been. The protocol, the constants, the fact that both of the
 * target's waits are busy loops with no timeout, and what the published source says about its polling
 * window versus what the appliance actually does, are all set out above struct octep_softc in
 * octep.h. Read that before arming this.
 *
 * Sources for the offsets and the field positions, and what was taken from each, are in
 * docs/octeontx/provenance.md. The register names follow
 * host/drivers/legacy/modules/driver/src/common/cn83xx_pf_regs.h.
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

/*
 * How many rings can be reached at all, given the BAR we were given. Ring n occupies
 * n * OCTEP_SDP_RING_STRIDE and its last register ends at OCTEP_SDP_RING_SPAN past that, so the
 * answer is however many of those fit. This is a property of the mapping, not of the hardware's
 * ring budget; the two are compared below and are expected to agree.
 */
static uint32_t
octep_sdp_mappable(struct octep_softc *sc)
{
	uint64_t size = (uint64_t)rman_get_size(sc->bar0);
	uint32_t n = 0;

	while ((uint64_t)n * OCTEP_SDP_RING_STRIDE + OCTEP_SDP_RING_SPAN <= size)
		n++;
	return (n);
}

static uint64_t
octep_sdp_ring_read(struct octep_softc *sc, uint32_t ring, bus_size_t reg)
{

	return (bus_read_8(sc->bar0,
	    reg + (bus_size_t)ring * OCTEP_SDP_RING_STRIDE));
}

/*
 * RINFO is one 64-bit read at a fixed BAR0 offset, sixteen bytes past the readiness register that
 * attach already reads. It says which rings belong to this physical function and how many, and
 * whether any have been carved off for virtual functions.
 */
void
octep_sdp_read_rinfo(struct octep_softc *sc, int verbose)
{
	uint64_t v;

	v = bus_read_8(sc->bar0, OCTEP_SDP_EPF_RINFO);

	sc->sdp_rinfo = v;
	sc->sdp_srn  = (uint32_t)OCTEP_RINFO_SRN(v);
	sc->sdp_trs  = (uint32_t)OCTEP_RINFO_TRS(v);
	sc->sdp_rpvf = (uint32_t)OCTEP_RINFO_RPVF(v);
	sc->sdp_nvfs = (uint32_t)OCTEP_RINFO_NVFS(v);
	sc->sdp_rings_mappable = octep_sdp_mappable(sc);

	if (verbose)
		device_printf(sc->dev,
		    "SDP RINFO 0x%016jx: rings %u..%u (%u), %u VF x %u rings; "
		    "%u rings fit in BAR0\n",
		    (uintmax_t)v, sc->sdp_srn,
		    sc->sdp_trs ? sc->sdp_srn + sc->sdp_trs - 1 : sc->sdp_srn,
		    sc->sdp_trs, sc->sdp_nvfs, sc->sdp_rpvf,
		    sc->sdp_rings_mappable);
}

/*
 * A ring at rest reports IDLE in both control words and zero everywhere else. A ring the host has
 * configured carries a base address and a size. Printing both halves side by side is the whole
 * point: it answers, in one look, whether anything has ever brought SDP up on this board.
 */
static int
octep_sysctl_sdp_rings(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct sbuf *sb;
	uint32_t ring, last, live;
	uint64_t inctl, inen, inbaddr, inrsize;
	uint64_t outctl, outen, outbaddr, outrsize;
	int error;

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	mtx_lock(&sc->mtx);
	octep_sdp_read_rinfo(sc, 0);

	sbuf_printf(sb, "\nRINFO 0x%016jx  srn %u  trs %u  rpvf %u  nvfs %u"
	    "   (BAR0 holds %u rings)\n",
	    (uintmax_t)sc->sdp_rinfo, sc->sdp_srn, sc->sdp_trs, sc->sdp_rpvf,
	    sc->sdp_nvfs, sc->sdp_rings_mappable);

	last = sc->sdp_srn + sc->sdp_trs;		/* one past the last */
	if (last > sc->sdp_rings_mappable) {
		sbuf_printf(sb, "clamped to %u: the rest would leave BAR0\n",
		    sc->sdp_rings_mappable);
		last = sc->sdp_rings_mappable;
	}

	sbuf_cat(sb, "\nring  IN_CONTROL          en    baddr  rsize"
	    "   OUT_CONTROL         en    baddr  rsize  state\n");

	live = 0;
	for (ring = sc->sdp_srn; ring < last; ring++) {
		inctl    = octep_sdp_ring_read(sc, ring, OCTEP_SDP_R_IN_CONTROL);
		inen     = octep_sdp_ring_read(sc, ring, OCTEP_SDP_R_IN_ENABLE);
		inbaddr  = octep_sdp_ring_read(sc, ring, OCTEP_SDP_R_IN_INSTR_BADDR);
		inrsize  = octep_sdp_ring_read(sc, ring, OCTEP_SDP_R_IN_INSTR_RSIZE);
		outctl   = octep_sdp_ring_read(sc, ring, OCTEP_SDP_R_OUT_CONTROL);
		outen    = octep_sdp_ring_read(sc, ring, OCTEP_SDP_R_OUT_ENABLE);
		outbaddr = octep_sdp_ring_read(sc, ring, OCTEP_SDP_R_OUT_SLIST_BADDR);
		outrsize = octep_sdp_ring_read(sc, ring, OCTEP_SDP_R_OUT_SLIST_RSIZE);

		if ((inen & 1) != 0 || (outen & 1) != 0 || inbaddr != 0 ||
		    outbaddr != 0)
			live++;

		sbuf_printf(sb,
		    "%4u  0x%016jx  %2ju  %7s  %5ju"
		    "   0x%016jx  %2ju  %7s  %5ju  %s%s%s\n",
		    ring,
		    (uintmax_t)inctl, (uintmax_t)(inen & 1),
		    inbaddr ? "set" : "-", (uintmax_t)inrsize,
		    (uintmax_t)outctl, (uintmax_t)(outen & 1),
		    outbaddr ? "set" : "-", (uintmax_t)outrsize,
		    (inctl & OCTEP_R_IN_CTL_IDLE) ? "in-idle " : "in-BUSY ",
		    (outctl & OCTEP_R_OUT_CTL_IDLE) ? "out-idle" : "out-BUSY",
		    (inctl & OCTEP_R_IN_CTL_IS_64B) ? " 64B" : "");
	}
	mtx_unlock(&sc->mtx);

	sbuf_printf(sb, "\n%u of %u rings carry any host configuration\n",
	    live, last > sc->sdp_srn ? last - sc->sdp_srn : 0);

	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}


/* ---------------------------------------------------------------- the handshake */

static const char *
octep_hs_name(int st)
{

	switch (st) {
	case OCTEP_HS_IDLE:	return ("idle");
	case OCTEP_HS_LOADED:	return ("host-loaded, waiting to be noticed");
	case OCTEP_HS_INFO:	return ("info published, waiting for the reply");
	case OCTEP_HS_DONE:	return ("completed");
	case OCTEP_HS_TIMEOUT:	return ("timed out - nobody was listening");
	default:		return ("?");
	}
}

/*
 * The word the target reads straight back into its own fields. Everything in it is either measured
 * from RINFO or fixed by what we are claiming to be.
 *
 * rppf is the vendor's own default and the target uses it as the port's channel count; app_mode is
 * CVM_DRV_NIC_APP, the only mode the target's CN83XX path accepts. Announcing NIC mode is a claim
 * this driver cannot yet honour - there is no datapath behind it - so this is a probe, and is
 * written down as one. A coprocessor reboot undoes it.
 */
/*
 * Write the word the vendor writes once its queues are up, having saved what was there. See the
 * note in octep.h for the whole sequence and for what this word displaces.
 */
static int
octep_sysctl_sdp_ioq_announce(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, v = 0;

	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL || v == 0)
		return (error);

	sc->scratch_saved = bus_read_8(sc->bar0, OCTEP_SDP_SCRATCH);
	bus_write_8(sc->bar0, OCTEP_SDP_SCRATCH, OCTEP_SDP_IOQ_DONE);
	device_printf(sc->dev, "sdp: saved 0x%016jx and announced IOQ creation complete "
	    "(0x%016jx at 0x%x) - sdp.ioq_restore puts the old word back\n",
	    (uintmax_t)sc->scratch_saved, (uintmax_t)OCTEP_SDP_IOQ_DONE, OCTEP_SDP_SCRATCH);
	return (0);
}

static int
octep_sysctl_sdp_ioq_restore(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, v = 0;

	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL || v == 0)
		return (error);
	if (sc->scratch_saved == 0) {
		device_printf(sc->dev, "sdp: nothing saved to restore\n");
		return (ENXIO);
	}
	bus_write_8(sc->bar0, OCTEP_SDP_SCRATCH, sc->scratch_saved);
	device_printf(sc->dev, "sdp: restored 0x%016jx at 0x%x\n",
	    (uintmax_t)sc->scratch_saved, OCTEP_SDP_SCRATCH);
	return (0);
}

static uint64_t
octep_sdp_info_word(struct octep_softc *sc)
{
	uint32_t pf_srn, rppf, nvfs, vf_srn, rpvf;

	/*
	 * The default is what this driver has always published: no VFs, so every ring is the PF's
	 * and the VF starting ring is just the base.
	 *
	 * The overrides exist because this word decides more than it looks like it does. The fast
	 * path on the far side does not choose how many host ports it has - it reads them out of a
	 * module parameter that THIS handshake fills, and it will not start until we have filled
	 * it:
	 *
	 *	num_pfs=${pci_info[0]}
	 *	num_vfs=${pci_info[2]}
	 *	num_hostports=$[num_pfs + num_vfs]
	 *
	 * which is usfp_startup_octtx.sh, the script dp_startup.conf names for this platform.
	 * Publishing no VFs therefore gives the far side one host port where the vendor's host
	 * gives it nine, and a ring the vendor would call a VF's is a ring the target believes
	 * belongs to nobody - which is what happened when ring 8 was tried and its frames came out
	 * classified as arriving from the wire.
	 *
	 * Setting these is a claim about a topology this driver does not implement, so they default
	 * to off and have to be set deliberately. A coprocessor reboot undoes what is published.
	 */
	pf_srn = sc->hs_pf_srn != 0 ? sc->hs_pf_srn : sc->sdp_srn;
	rppf   = sc->hs_rppf   != 0 ? sc->hs_rppf   : OCTEP_SDP_RINGS_PER_PF;
	nvfs   = sc->hs_nvfs;
	vf_srn = sc->hs_nvfs != 0 ? sc->hs_vf_srn : (sc->sdp_srn & 0x3f);
	rpvf   = sc->hs_nvfs != 0 ? (sc->hs_rpvf != 0 ? sc->hs_rpvf : 1) : 0;

	return (OCTEP_SDP_INFO(OCTEP_SDP_APP_MODE_NIC, pf_srn, rppf, nvfs, vf_srn, rpvf));
}

static void
octep_sdp_hs_write(struct octep_softc *sc, uint64_t v)
{

	bus_write_8(sc->bar0, OCTEP_SLI_EPF_SCRATCH, v);
}

static uint64_t
octep_sdp_hs_read(struct octep_softc *sc)
{

	sc->sdp_hs_seen = bus_read_8(sc->bar0, OCTEP_SLI_EPF_SCRATCH);
	return (sc->sdp_hs_seen);
}

static void octep_sdp_poll(void *arg);

static void
octep_sdp_hs_settle(struct octep_softc *sc, int state)
{

	sc->sdp_hs_state = state;
	sc->sdp_hs_ticks = 0;
	callout_stop(&sc->sdp_poll);
}

/*
 * One step per tick, 50 Hz. Every state the target can be spinning in is left by a write, and every
 * state this side can wait in has a deadline - see the protocol comment in octep.h for why that
 * matters more here than it usually would.
 */
static void
octep_sdp_poll(void *arg)
{
	struct octep_softc *sc = arg;
	uint64_t v;

	mtx_lock(&sc->mtx);
	v = octep_sdp_hs_read(sc);
	sc->sdp_hs_ticks++;

	switch (sc->sdp_hs_state) {
	case OCTEP_HS_LOADED:
		if (v == OCTEP_SDP_GET_HOST_INFO) {
			sc->sdp_hs_info = octep_sdp_info_word(sc);
			octep_sdp_hs_write(sc, sc->sdp_hs_info);
			device_printf(sc->dev, "sdp: target asked; published "
			    "0x%016jx (app %ju, pf_srn %ju, rppf %ju, %ju VFs, vf_srn %ju, "
			    "rpvf %ju)\n", (uintmax_t)sc->sdp_hs_info,
			    (uintmax_t)((sc->sdp_hs_info >> 40) & 0xff),
			    (uintmax_t)((sc->sdp_hs_info >> 32) & 0xff),
			    (uintmax_t)((sc->sdp_hs_info >> 24) & 0xff),
			    (uintmax_t)((sc->sdp_hs_info >> 16) & 0xff),
			    (uintmax_t)((sc->sdp_hs_info >> 8) & 0xff),
			    (uintmax_t)(sc->sdp_hs_info & 0xff));
			sc->sdp_hs_state = OCTEP_HS_INFO;
			sc->sdp_hs_ticks = 0;
			break;
		}
		if (sc->sdp_hs_ticks >= OCTEP_HS_LOADED_TICKS) {
			/*
			 * Nothing read it, so nothing can be spinning: put the register back to
			 * zero and say plainly what that means.
			 */
			octep_sdp_hs_write(sc, 0);
			octep_sdp_hs_settle(sc, OCTEP_HS_TIMEOUT);
			device_printf(sc->dev, "sdp: no answer in %d s - the target is not "
			    "polling this register. Its published source gives up eleven seconds "
			    "after its own boot, and slipf is built into its kernel, so a "
			    "coprocessor reboot is the only way to reopen that. Register set "
			    "back to zero.\n",
			    OCTEP_HS_LOADED_TICKS / OCTEP_SDP_POLL_HZ);
			mtx_unlock(&sc->mtx);
			return;
		}
		break;

	case OCTEP_HS_INFO:
		if ((v >> 16) == OCTEP_SDP_HOST_INFO_RECEIVED) {
			sc->sdp_coproc_ticks_per_us = (uint32_t)(v & 0xffff);
			octep_sdp_hs_write(sc, OCTEP_SDP_HANDSHAKE_COMPLETED);
			octep_sdp_hs_settle(sc, OCTEP_HS_DONE);
			device_printf(sc->dev, "sdp: target took the info and reports "
			    "%u ticks/us; announced HANDSHAKE_COMPLETED\n",
			    sc->sdp_coproc_ticks_per_us);
			mtx_unlock(&sc->mtx);
			return;
		}
		if (sc->sdp_hs_ticks >= OCTEP_HS_INFO_TICKS) {
			/*
			 * The target's reply is immediate once it has our word, so this should not
			 * happen - but it read GET_HOST_INFO to get here, which means it may be in
			 * one of its two untimed spins. COMPLETED is the value that releases both,
			 * so that is what goes out rather than a zero.
			 */
			octep_sdp_hs_write(sc, OCTEP_SDP_HANDSHAKE_COMPLETED);
			octep_sdp_hs_settle(sc, OCTEP_HS_DONE);
			device_printf(sc->dev, "sdp: no reply to the info word in %d s; wrote "
			    "HANDSHAKE_COMPLETED anyway, because it releases either spin the "
			    "target could be in\n",
			    OCTEP_HS_INFO_TICKS / OCTEP_SDP_POLL_HZ);
			mtx_unlock(&sc->mtx);
			return;
		}
		break;

	default:
		callout_stop(&sc->sdp_poll);
		mtx_unlock(&sc->mtx);
		return;
	}

	callout_reset(&sc->sdp_poll, hz / OCTEP_SDP_POLL_HZ, octep_sdp_poll, sc);
	mtx_unlock(&sc->mtx);
}

/*
 * Arm it. This is the first thing in this driver that changes the coprocessor's state rather than
 * reading it, so it happens on an explicit sysctl and never as a side effect of anything else.
 */
static int
octep_sdp_handshake_start(struct octep_softc *sc)
{
	uint64_t v;

	mtx_lock(&sc->mtx);
	if (sc->sdp_hs_state == OCTEP_HS_LOADED ||
	    sc->sdp_hs_state == OCTEP_HS_INFO) {
		mtx_unlock(&sc->mtx);
		return (EALREADY);
	}
	if (sc->sdp_hs_state == OCTEP_HS_DONE) {
		/*
		 * Once is all it takes, and a second time would do harm: the target marks its own
		 * side done and will not poll again, and it has repurposed this register as a
		 * bitmap of started ports. Writing HOST_LOADED over that would throw away live
		 * state belonging to the target.
		 */
		device_printf(sc->dev, "sdp: the handshake is already done - the target owns "
		    "this register now and uses it to report started ports; refusing\n");
		mtx_unlock(&sc->mtx);
		return (EALREADY);
	}

	octep_sdp_read_rinfo(sc, 0);
	if (sc->sdp_trs == 0) {
		device_printf(sc->dev, "sdp: RINFO advertises no rings; refusing\n");
		mtx_unlock(&sc->mtx);
		return (ENXIO);
	}

	v = octep_sdp_hs_read(sc);

	/*
	 * The state machine above only remembers within one module lifetime, and this register does not
	 * reset when the driver is reloaded. So the value in it has to be believed rather than the
	 * state: anything other than zero, or our own HOST_LOADED from a previous arm, means someone
	 * else is using it - either an exchange is in flight, or, far more likely on a running system,
	 * the target has finished the handshake and is using it as its started-port bitmap. Writing
	 * HOST_LOADED over that would destroy live state, and it would do so silently.
	 */
	if (v != 0 && v != OCTEP_SDP_HOST_LOADED) {
		device_printf(sc->dev, "sdp: SLI_EPF_SCRATCH already holds 0x%016jx, which is not "
		    "ours - the target has the register%s. Refusing; a coprocessor reboot is what "
		    "clears it.\n", (uintmax_t)v,
		    (v & ~1ULL) == 0 ? " and is reporting a started port" : "");
		octep_sdp_hs_settle(sc, OCTEP_HS_IDLE);
		mtx_unlock(&sc->mtx);
		return (EBUSY);
	}

	device_printf(sc->dev, "sdp: SLI_EPF_SCRATCH was 0x%016jx; writing HOST_LOADED\n",
	    (uintmax_t)v);

	octep_sdp_hs_write(sc, OCTEP_SDP_HOST_LOADED);
	if (octep_sdp_hs_read(sc) != OCTEP_SDP_HOST_LOADED) {
		device_printf(sc->dev, "sdp: HOST_LOADED did not read back (0x%016jx) - "
		    "not arming\n", (uintmax_t)sc->sdp_hs_seen);
		octep_sdp_hs_settle(sc, OCTEP_HS_IDLE);
		mtx_unlock(&sc->mtx);
		return (EIO);
	}

	sc->sdp_hs_state = OCTEP_HS_LOADED;
	sc->sdp_hs_ticks = 0;
	sc->sdp_hs_cleared = 0;
	sc->sdp_coproc_ticks_per_us = 0;
	callout_reset(&sc->sdp_poll, hz / OCTEP_SDP_POLL_HZ, octep_sdp_poll, sc);
	mtx_unlock(&sc->mtx);
	return (0);
}

/*
 * Stand down. Only safe from a state where the target cannot be spinning, which is why it refuses
 * from OCTEP_HS_INFO: by then the target has our word and is between two untimed loops, and the
 * poller will finish or time out within two seconds on its own.
 */
void
octep_sdp_handshake_stop(struct octep_softc *sc)
{

	mtx_lock(&sc->mtx);
	if (sc->sdp_hs_state == OCTEP_HS_LOADED) {
		octep_sdp_hs_write(sc, 0);
		device_printf(sc->dev, "sdp: stood down; register back to zero\n");
	}
	if (sc->sdp_hs_state != OCTEP_HS_INFO)
		octep_sdp_hs_settle(sc, OCTEP_HS_IDLE);
	mtx_unlock(&sc->mtx);
}

static int
octep_sysctl_sdp_handshake(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, val = 0;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (val == 0) {
		octep_sdp_handshake_stop(sc);
		return (0);
	}
	return (octep_sdp_handshake_start(sc));
}

static int
octep_sysctl_sdp_hs_state(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	char buf[96];
	uint64_t v;

	mtx_lock(&sc->mtx);
	v = octep_sdp_hs_read(sc);
	if (sc->sdp_hs_state == OCTEP_HS_DONE && v == 0)
		sc->sdp_hs_cleared = 1;
	if (sc->sdp_hs_state == OCTEP_HS_DONE && sc->sdp_hs_cleared != 0 && v != 0) {
		/* The target is using it as a started-port bitmap: bit 0 is the PF. */
		snprintf(buf, sizeof(buf), "%s; target reports ports started: 0x%016jx%s",
		    octep_hs_name(sc->sdp_hs_state), (uintmax_t)v,
		    (v & 1) != 0 ? " (PF up)" : "");
	} else if (sc->sdp_hs_state == OCTEP_HS_IDLE && v != 0) {
		/*
		 * Not ours, and this driver did not write it - almost always a handshake completed
		 * before the module was last reloaded, with the target now reporting started ports.
		 */
		snprintf(buf, sizeof(buf), "not handshaken by this instance; register holds "
		    "0x%016jx%s", (uintmax_t)v, (v & 1) != 0 ? " (a port is started)" : "");
	} else {
		snprintf(buf, sizeof(buf), "%s (scratch 0x%016jx)",
		    octep_hs_name(sc->sdp_hs_state), (uintmax_t)v);
	}
	mtx_unlock(&sc->mtx);

	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}

void
octep_sdp_add_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
    struct sysctl_oid_list *top)
{
	struct sysctl_oid *node;

	node = SYSCTL_ADD_NODE(ctx, top, OID_AUTO, "sdp", CTLFLAG_RD, NULL,
	    "the PCIe datapath - what the hardware offers, nothing configured");
	if (node == NULL)
		return;

	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rinfo",
	    CTLFLAG_RD, &sc->sdp_rinfo, 0, "SDP_EPF_RINFO as read at attach");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "srn",
	    CTLFLAG_RD, &sc->sdp_srn, 0, "first ring this function owns");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "trs",
	    CTLFLAG_RD, &sc->sdp_trs, 0, "how many rings it owns");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rpvf",
	    CTLFLAG_RD, &sc->sdp_rpvf, 0, "rings carved off per virtual function");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ioq_announce",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_sdp_ioq_announce, "I",
	    "write 1 to tell the target its queues are up, the way the vendor does. It saves the "
	    "word already there, which is the barmap pointer - see octep.h");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ioq_restore",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_sdp_ioq_restore, "I", "write 1 to put the saved word back");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hs_pf_srn",
	    CTLFLAG_RW, &sc->hs_pf_srn, 0,
	    "what the handshake publishes as the PF's starting ring; 0 uses RINFO's. Set it before "
	    "sdp.handshake, which can only be done once per coprocessor boot");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hs_rppf",
	    CTLFLAG_RW, &sc->hs_rppf, 0, "rings per PF to publish; 0 uses the vendor default of 8");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hs_nvfs",
	    CTLFLAG_RW, &sc->hs_nvfs, 0,
	    "how many VFs to publish. The far side adds this to the PF count to decide how many "
	    "host ports it has, so it is not cosmetic - see octep_sdp_info_word");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hs_vf_srn",
	    CTLFLAG_RW, &sc->hs_vf_srn, 0, "the VF starting ring, used only when hs_nvfs is set");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hs_rpvf",
	    CTLFLAG_RW, &sc->hs_rpvf, 0,
	    "rings per VF, only when hs_nvfs is set; 0 becomes 1, which is what this board has");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nvfs",
	    CTLFLAG_RD, &sc->sdp_nvfs, 0, "how many virtual functions");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rings_mappable",
	    CTLFLAG_RD, &sc->sdp_rings_mappable, 0,
	    "how many rings fit inside the BAR we were given");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rings",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_sdp_rings, "A",
	    "per-ring control and base registers, read fresh on each read");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "handshake",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_sdp_handshake, "I",
	    "write 1 to announce HOST_LOADED and drive the EP-mode handshake, 0 to stand down");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hs_state",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_sdp_hs_state, "A",
	    "where the handshake stands; once done, the ports the target reports started");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hs_info",
	    CTLFLAG_RD, &sc->sdp_hs_info, 0, "the info word published to the target");
	/*
	 * Writable, and it has to be: the target publishes this rate once, during the handshake, and
	 * the handshake runs once per coprocessor boot. A module reload therefore loses it with no way
	 * to read it back - the register it arrived in has since been zeroed and repurposed as the
	 * started-port bitmap. Measured 800 on this board.
	 */
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "coproc_ticks_per_us",
	    CTLFLAG_RW, &sc->sdp_coproc_ticks_per_us, 0,
	    "target timer rate from the handshake; set it by hand after a reload");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hs_cleared",
	    CTLFLAG_RD, &sc->sdp_hs_cleared, 0,
	    "1 once the target has zeroed the register, which it does only after finishing");
}
