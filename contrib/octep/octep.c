/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * octep - bind the OCTEON TX (Cavium CN83XX) PCIe endpoint on a Sophos XGS appliance, read what it
 * publishes about itself, and bring its management facility up.
 *
 * WHY THIS IS A SEPARATE DRIVER FROM npuep. npuep is for ARMADA (Marvell CN913x, 0x11ab:0x7080),
 * and the two families are not the same protocol wearing different PCI ids. ARMADA keeps its barmap
 * at a FIXED offset in its window and guards it with a cookie; OCTEON publishes a POINTER to its
 * barmap in a CSR and guards it with a different magic. ARMADA's facility records are
 * {bar, type, offset, size}; OCTEON's are {offset, size, h2t_dbell_start, h2t_dbell_count}. ARMADA
 * raises target-to-host doorbells as MSI-X vectors; on OCTEON every facility reports it has none, so
 * that path does not exist there at all. And ARMADA has a fifth facility, GIU, which carries its
 * front ports and which OCTEON does not have. Threading a chip variable through npuep's five source
 * files to serve one board would be the larger change, not the smaller one.
 *
 * WHAT HAPPENS WHEN, AND WHAT NEVER HAPPENS BY ITSELF. Loading the driver binds the endpoint, maps
 * BAR0 and BAR2, reads the readiness register, follows the pointer and parses the barmap. That is
 * all reads, and it is all that attach does. Bringing the management facility up - which allocates
 * DMA memory and tells the coprocessor our rings are valid - happens only when asked:
 *
 *	kldload ./octep.ko
 *	sysctl dev.octep.0			# what the endpoint says about itself
 *	sysctl dev.octep.0.mgmt_start=1		# the handshake; octep0 appears
 *	sysctl dev.octep.0.mgmt_stop=1		# and back down
 *
 * That split is not caution for its own sake. The coprocessor's mgmt_net validates the rings the
 * moment the host announces HOST_READY, and on any fault it sets TARGET_FATAL and stays there until
 * its own module is reloaded. There is no second attempt, so the announcement is never made as a
 * side effect of loading a module.
 *
 * ONE HAZARD, WRITTEN DOWN BECAUSE IT COST A POWER CYCLE. Entry 15 of the BAR2 window maps the
 * coprocessor's GIC distributor. READING that window from the host stalls the host with no panic and
 * no console output. WRITING GICD_SETSPI_NSR in it is the designed doorbell and is safe. So this
 * driver never reads at or past gicd_offset.
 *
 * Provenance - which vendor sources were read, under which licence, and what was taken from
 * them - is docs/octeontx/provenance.md, kept apart from the ARMADA one on purpose.
 *
 * See docs/families/octeon-tx.md for the protocol and where each constant came from.
 *
 *	sh contrib/npuep/fetch-sources.sh
 *	make -C contrib/octep SYSDIR=/usr/src-26.7-<sha>/sys
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/ethernet.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "octep.h"

const char *const octep_facility_name[OCTEP_FACILITY_COUNT] = {
	"control", "mgmt_netdev", "nw_agent", "rpc", "giu"
};

static int
octep_probe(device_t dev)
{
	if (pci_get_vendor(dev) != OCTEP_VENDOR_CAVIUM ||
	    pci_get_device(dev) != OCTEP_DEV_CN83XX_PF)
		return (ENXIO);

	device_set_desc(dev, "Cavium OCTEON TX CN83XX PCIe endpoint (Sophos XGS NPU)");
	return (BUS_PROBE_DEFAULT);
}

/*
 * Read the readiness register and, if the coprocessor has published one, the barmap it points at.
 *
 * Returns EAGAIN rather than an error when the magic is absent: the coprocessor writes it late in its
 * own boot, so "not yet" is an ordinary state and not a fault. The caller decides whether to
 * complain.
 */
int
octep_read_barmap(struct octep_softc *sc, int verbose)
{
	device_t dev = sc->dev;
	uint32_t magic, ver;
	bus_size_t base;
	int i;

	sc->scratch = bus_read_8(sc->bar0, OCTEP_SDP_SCRATCH);
	magic = (uint32_t)(sc->scratch & 0xffffffffU);
	sc->barmap_off = (uint32_t)(sc->scratch >> 32);

	if (sc->scratch == ~(uint64_t)0) {
		device_printf(dev, "BAR0 reads all ones - the endpoint is not decoding\n");
		sc->ready = 0;
		return (ENXIO);
	}
	if (magic != OCTEP_READY_MAGIC) {
		/*
		 * Quiet by default. A coprocessor that was just reset sits here for about half a
		 * minute, and an ungated printf turns that into a screenful on a 115200 console.
		 */
		if (verbose)
			device_printf(dev,
			    "NPU not ready yet (scratch 0x%016jx, magic 0x%08x)\n",
			    (uintmax_t)sc->scratch, magic);
		sc->ready = 0;
		return (EAGAIN);
	}

	/*
	 * Refuse an offset that would put the table outside the window we mapped, before reading
	 * through it. The endpoint is telling us where to look and we are about to believe it.
	 */
	if (sc->barmap_off + OCTEP_BARMAP_PEM_OFF + 4 > rman_get_size(sc->bar2)) {
		device_printf(dev, "barmap offset 0x%08x is outside BAR2 (size 0x%jx)\n",
		    sc->barmap_off, (uintmax_t)rman_get_size(sc->bar2));
		sc->ready = 0;
		return (EINVAL);
	}

	base = sc->barmap_off;
	ver = bus_read_4(sc->bar2, base + OCTEP_BARMAP_VERSION_OFF);
	if (ver == 0xffffffffU) {
		/*
		 * The vendor's host driver treats this exact value as "the DMA that writes the table
		 * has not landed yet" and retries rather than failing. Do the same.
		 */
		if (verbose)
			device_printf(dev, "barmap not written yet (version all ones)\n");
		sc->ready = 0;
		return (EAGAIN);
	}
	sc->barmap_version = ver;
	if (ver != OCTEP_BARMAP_VERSION) {
		device_printf(dev, "barmap version 0x%08x, expected 0x%08x - the layout may have "
		    "changed; refusing to parse it\n", ver, OCTEP_BARMAP_VERSION);
		sc->ready = 0;
		return (EINVAL);
	}

	for (i = 0; i < OCTEP_FACILITY_COUNT; i++) {
		bus_size_t e = base + OCTEP_BARMAP_FCLT_OFF + (i * OCTEP_FCLT_ENT_SIZE);

		sc->fclt[i].offset      = bus_read_4(sc->bar2, e);
		sc->fclt[i].size        = bus_read_4(sc->bar2, e + 4);
		sc->fclt[i].dbell_start = bus_read_4(sc->bar2, e + 8);
		sc->fclt[i].dbell_count = bus_read_4(sc->bar2, e + 12);
	}
	sc->gicd_offset = bus_read_4(sc->bar2, base + OCTEP_BARMAP_GICD_OFF);
	sc->pem_num = bus_read_4(sc->bar2, base + OCTEP_BARMAP_PEM_OFF) & 0xff;
	sc->ready = 1;

	if (verbose) {
		device_printf(dev, "barmap at BAR2+0x%08x, version %u.%u, PEM %u, "
		    "GICD at BAR2+0x%08x\n", sc->barmap_off,
		    sc->barmap_version >> 16, sc->barmap_version & 0xffff,
		    sc->pem_num, sc->gicd_offset);
		for (i = 0; i < OCTEP_FACILITY_COUNT; i++) {
			if (sc->fclt[i].size == 0)
				continue;	/* not present on this family */
			device_printf(dev, "  %-11s BAR2+0x%08x  %u KiB  h2t doorbells %u..%u\n",
			    octep_facility_name[i], sc->fclt[i].offset,
			    sc->fclt[i].size / 1024, sc->fclt[i].dbell_start,
			    sc->fclt[i].dbell_start + sc->fclt[i].dbell_count - 1);
		}
	}
	return (0);
}

/*
 * Ring a host-to-target doorbell.
 *
 * From the vendor's facility.c: a 32-bit store of the SPI number to mmio[1] + gicd_offset, which is
 * GICD_SETSPI_NSR inside the GIC distributor window. The number is validated against the facilities'
 * own advertised ranges first, exactly as the vendor does, so a typo cannot turn into an arbitrary
 * interrupt - or an arbitrary store into GIC register space.
 *
 * The _locked form exists because the transmit path already holds the lock.
 */
int
octep_ring_dbell_locked(struct octep_softc *sc, uint32_t spi)
{
	int i;

	if (!sc->ready)
		return (ENXIO);

	for (i = 0; i < OCTEP_FACILITY_COUNT; i++) {
		if (sc->fclt[i].dbell_count == 0)
			continue;
		if (spi >= sc->fclt[i].dbell_start &&
		    spi < sc->fclt[i].dbell_start + sc->fclt[i].dbell_count) {
			bus_write_4(sc->bar2, sc->gicd_offset, spi);
			sc->dbells_rung++;
			return (0);
		}
	}
	return (EINVAL);
}

int
octep_ring_dbell(struct octep_softc *sc, uint32_t spi)
{
	int err;

	mtx_lock(&sc->mtx);
	err = octep_ring_dbell_locked(sc, spi);
	mtx_unlock(&sc->mtx);
	return (err);
}

/* ---------------------------------------------------------------- sysctls */

static int
octep_sysctl_rescan(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, val = 0;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	return (octep_read_barmap(sc, 1));
}

static int
octep_sysctl_dbell(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	unsigned int spi = 0;
	int error;

	error = sysctl_handle_int(oidp, &spi, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	return (octep_ring_dbell(sc, spi));
}

static int
octep_sysctl_mgmt_start(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, val = 0;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	return (octep_mgmt_start(sc));
}

static int
octep_sysctl_mgmt_stop(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, val = 0;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	octep_mgmt_stop(sc);
	return (0);
}

static void
octep_add_sysctls(struct octep_softc *sc)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->dev);
	struct sysctl_oid_list *top = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev));
	struct sysctl_oid *node;
	int i;

	SYSCTL_ADD_U64(ctx, top, OID_AUTO, "scratch", CTLFLAG_RD,
	    &sc->scratch, 0, "readiness register: (barmap offset << 32) | magic");
	SYSCTL_ADD_INT(ctx, top, OID_AUTO, "ready", CTLFLAG_RD,
	    &sc->ready, 0, "1 when the magic was seen and the barmap parsed");
	SYSCTL_ADD_UINT(ctx, top, OID_AUTO, "barmap_offset", CTLFLAG_RD,
	    &sc->barmap_off, 0, "offset of the barmap inside BAR2");
	SYSCTL_ADD_UINT(ctx, top, OID_AUTO, "barmap_version", CTLFLAG_RD,
	    &sc->barmap_version, 0, "(major << 16) | minor");
	SYSCTL_ADD_UINT(ctx, top, OID_AUTO, "gicd_offset", CTLFLAG_RD,
	    &sc->gicd_offset, 0, "GICD_SETSPI_NSR inside BAR2 - never read this");
	SYSCTL_ADD_UINT(ctx, top, OID_AUTO, "pem_num", CTLFLAG_RD,
	    &sc->pem_num, 0, "which PEM the endpoint is behind");
	SYSCTL_ADD_U64(ctx, top, OID_AUTO, "dbells_rung", CTLFLAG_RD,
	    &sc->dbells_rung, 0, "host-to-target doorbells this driver has rung");

	SYSCTL_ADD_INT(ctx, top, OID_AUTO, "mgmt_up", CTLFLAG_RD,
	    &sc->mgmt_up, 0, "1 when the management rings are published");
	SYSCTL_ADD_U64(ctx, top, OID_AUTO, "host_status", CTLFLAG_RD,
	    &sc->host_status, 0, "0 down 1 ready 2 running 3 going-down 4 fatal");
	SYSCTL_ADD_U64(ctx, top, OID_AUTO, "target_status", CTLFLAG_RD,
	    &sc->target_status, 0, "the same scale, as last polled from the coprocessor");
	SYSCTL_ADD_UINT(ctx, top, OID_AUTO, "rx_cons_shadow", CTLFLAG_RD,
	    &sc->rx_cons_seen, 0, "receive consumer index the target wrote to host memory");
	SYSCTL_ADD_UINT(ctx, top, OID_AUTO, "tx_cons_shadow", CTLFLAG_RD,
	    &sc->tx_cons_seen, 0, "transmit consumer index the target wrote to host memory");
	SYSCTL_ADD_U64(ctx, top, OID_AUTO, "rx_packets", CTLFLAG_RD,
	    &sc->rx_packets, 0, "frames handed to the network stack");
	SYSCTL_ADD_U64(ctx, top, OID_AUTO, "rx_bytes", CTLFLAG_RD,
	    &sc->rx_bytes, 0, "bytes received");
	SYSCTL_ADD_U64(ctx, top, OID_AUTO, "rx_drops", CTLFLAG_RD,
	    &sc->rx_drops, 0, "frames lost for want of an mbuf");
	SYSCTL_ADD_U64(ctx, top, OID_AUTO, "rx_errors", CTLFLAG_RD,
	    &sc->rx_errors, 0, "descriptors with an impossible length");
	SYSCTL_ADD_U64(ctx, top, OID_AUTO, "tx_packets", CTLFLAG_RD,
	    &sc->tx_packets, 0, "frames given to the coprocessor");
	SYSCTL_ADD_U64(ctx, top, OID_AUTO, "tx_bytes", CTLFLAG_RD,
	    &sc->tx_bytes, 0, "bytes transmitted");
	SYSCTL_ADD_U64(ctx, top, OID_AUTO, "tx_drops", CTLFLAG_RD,
	    &sc->tx_drops, 0, "frames dropped with the transmit ring full");
	SYSCTL_ADD_U64(ctx, top, OID_AUTO, "tx_errors", CTLFLAG_RD,
	    &sc->tx_errors, 0, "frames refused as too long");

	SYSCTL_ADD_PROC(ctx, top, OID_AUTO, "rescan",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_rescan, "I", "write anything to re-read the barmap");
	SYSCTL_ADD_PROC(ctx, top, OID_AUTO, "ring_dbell",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_dbell, "IU",
	    "write an SPI number in an advertised facility range to ring it");
	SYSCTL_ADD_PROC(ctx, top, OID_AUTO, "mgmt_start",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_mgmt_start, "I",
	    "publish the management rings and announce HOST_READY");
	SYSCTL_ADD_PROC(ctx, top, OID_AUTO, "mgmt_stop",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_mgmt_stop, "I", "announce GOING_DOWN and release the rings");

	octep_sdp_add_sysctls(sc, ctx, top);

	for (i = 0; i < OCTEP_FACILITY_COUNT; i++) {
		node = SYSCTL_ADD_NODE(ctx, top, OID_AUTO, octep_facility_name[i],
		    CTLFLAG_RD, NULL, "facility");
		if (node == NULL)
			continue;
		SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "offset",
		    CTLFLAG_RD, &sc->fclt[i].offset, 0, "offset inside BAR2");
		SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "size",
		    CTLFLAG_RD, &sc->fclt[i].size, 0, "bytes");
		SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "dbell_start",
		    CTLFLAG_RD, &sc->fclt[i].dbell_start, 0, "first h2t SPI");
		SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "dbell_count",
		    CTLFLAG_RD, &sc->fclt[i].dbell_count, 0, "how many h2t SPIs");
	}
}

/* ---------------------------------------------------------------- newbus */

static int
octep_attach(device_t dev)
{
	struct octep_softc *sc = device_get_softc(dev);
	int err;

	sc->dev = dev;
	mtx_init(&sc->mtx, device_get_nameunit(dev), NULL, MTX_DEF);
	callout_init(&sc->poll, 1);

	/*
	 * BAR0 is the CSR space and BAR2 is the 64 MB facility window. They are 64-bit BARs, so the
	 * resource ids are the low halves: PCIR_BAR(0) and PCIR_BAR(2). BAR4 exists and is not
	 * mapped, because nothing here needs it.
	 */
	sc->rid0 = PCIR_BAR(0);
	sc->bar0 = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->rid0, RF_ACTIVE);
	if (sc->bar0 == NULL) {
		device_printf(dev, "cannot map BAR0 (CSR space)\n");
		err = ENXIO;
		goto fail;
	}
	sc->rid2 = PCIR_BAR(2);
	sc->bar2 = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->rid2, RF_ACTIVE);
	if (sc->bar2 == NULL) {
		device_printf(dev, "cannot map BAR2 (facility window)\n");
		err = ENXIO;
		goto fail;
	}

	device_printf(dev, "BAR0 %ju bytes, BAR2 %ju bytes\n",
	    (uintmax_t)rman_get_size(sc->bar0), (uintmax_t)rman_get_size(sc->bar2));

	octep_add_sysctls(sc);

	/*
	 * Read once, loudly, and do not fail the attach if the coprocessor is not ready: it boots on
	 * its own schedule and may simply not have published yet.
	 */
	err = octep_read_barmap(sc, 1);
	if (err == EAGAIN)
		device_printf(dev, "attached; the NPU has not published its barmap yet - "
		    "re-read with: sysctl dev.%s.%d.rescan=1\n",
		    device_get_name(dev), device_get_unit(dev));

	/*
	 * The datapath ring budget, which is a BAR0 read like the one above and does not depend
	 * on the coprocessor having published anything. Reported, never acted on.
	 */
	octep_sdp_read_rinfo(sc, 1);

	return (0);

fail:
	if (sc->bar2 != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->rid2, sc->bar2);
	if (sc->bar0 != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->rid0, sc->bar0);
	mtx_destroy(&sc->mtx);
	return (err);
}

static int
octep_detach(device_t dev)
{
	struct octep_softc *sc = device_get_softc(dev);

	/*
	 * Tell the coprocessor before freeing anything it might be writing into. octep_mgmt_stop()
	 * is a no-op when the facility was never started.
	 */
	octep_mgmt_stop(sc);
	callout_drain(&sc->poll);
	octep_if_detach(sc);

	if (sc->bar2 != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->rid2, sc->bar2);
	if (sc->bar0 != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->rid0, sc->bar0);
	mtx_destroy(&sc->mtx);
	return (0);
}

static device_method_t octep_methods[] = {
	DEVMETHOD(device_probe,		octep_probe),
	DEVMETHOD(device_attach,	octep_attach),
	DEVMETHOD(device_detach,	octep_detach),
	DEVMETHOD_END
};

static driver_t octep_driver = {
	"octep",
	octep_methods,
	sizeof(struct octep_softc)
};

DRIVER_MODULE(octep, pci, octep_driver, NULL, NULL);
MODULE_VERSION(octep, 1);
