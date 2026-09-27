/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * octep_sdp - report what SDP, the PCIe datapath, says about itself. Reads only.
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
 * NOTHING HERE WRITES. Not the enables, not the base addresses, not the doorbells. Bringing a ring
 * up is a later and much larger change, and the same rule as the management link will apply to it:
 * never announce readiness before the rings are complete.
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
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nvfs",
	    CTLFLAG_RD, &sc->sdp_nvfs, 0, "how many virtual functions");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rings_mappable",
	    CTLFLAG_RD, &sc->sdp_rings_mappable, 0,
	    "how many rings fit inside the BAR we were given");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rings",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_sdp_rings, "A",
	    "per-ring control and base registers, read fresh on each read");
}
