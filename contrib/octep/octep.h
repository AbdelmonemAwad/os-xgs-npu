/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * octep - the OCTEON TX (Cavium CN83XX) PCIe endpoint on a Sophos XGS appliance.
 *
 * Constants here were read out of Marvell's own published source and then confirmed against the
 * hardware. Where a name appears in that source it is kept, so the two can be read side by side:
 *
 *	host/drivers/legacy/modules/driver/src/common/cn83xx_pf_regs.h	the CSR offsets
 *	target/drivers/pcie_ep/src/barmap.h				struct npu_bar_map
 *	host/drivers/mgmt_net/bar_space_mgmt_net.h			the mgmt register map
 *	host/drivers/mgmt_net/desc_queue.h				the descriptor rings
 *
 * See docs/families/octeon-tx.md for what each of them means and how it was checked.
 */

#ifndef _OCTEP_H_
#define _OCTEP_H_

/* ---------------------------------------------------------------- the endpoint */

#define	OCTEP_VENDOR_CAVIUM	0x177d
#define	OCTEP_DEV_CN83XX_PF	0xa300

/*
 * BAR0 holds the CSRs. CN83XX_SDP_SCRATCH_START; CN83XX_EPF_OFFSET is 0 on this part, so index 0
 * needs no stride. The vendor reads it with octeon_read_csr64(), which is mmio[0] + offset, and
 * mmio[n] is PCI BAR n*2 - hence BAR0.
 */
#define	OCTEP_SDP_SCRATCH	0x20180

/*
 * The low half of that register is the readiness magic and the high half is the barmap's offset
 * inside the 64 MB window. For CN83XX the vendor follows the pointer into mmio[1] = PCI BAR2.
 */
#define	OCTEP_READY_MAGIC	0xabcdabcdU

/* ---------------------------------------------------------------- struct npu_bar_map */

/*
 * Five facility entries. OCTEON fills four: GIU is ARMADA's own NIC and does not exist here, so its
 * entry reads back as zeroes.
 */
#define	OCTEP_FACILITY_COUNT	5
#define	OCTEP_FCLT_CONTROL	0
#define	OCTEP_FCLT_MGMT		1
#define	OCTEP_FCLT_NW_AGENT	2
#define	OCTEP_FCLT_RPC		3
#define	OCTEP_FCLT_GIU		4

#define	OCTEP_FCLT_ENT_SIZE	16
#define	OCTEP_BARMAP_VERSION_OFF	0
#define	OCTEP_BARMAP_FCLT_OFF		4
#define	OCTEP_BARMAP_GICD_OFF	\
	(OCTEP_BARMAP_FCLT_OFF + (OCTEP_FACILITY_COUNT * OCTEP_FCLT_ENT_SIZE))
#define	OCTEP_BARMAP_PEM_OFF	(OCTEP_BARMAP_GICD_OFF + 4)

/* (major << 16) | minor. The tree we read, and the hardware, both say 0.2. */
#define	OCTEP_BARMAP_VERSION	0x00000002U

/* ---------------------------------------------------------------- software state */

struct octep_facility {
	uint32_t	offset;
	uint32_t	size;
	uint32_t	dbell_start;
	uint32_t	dbell_count;
};

/* One contiguous coherent allocation, and the physical address the coprocessor needs. */
struct octep_dma {
	bus_dma_tag_t	 tag;
	bus_dmamap_t	 map;
	void		*vaddr;
	bus_addr_t	 paddr;
	bus_size_t	 size;
};

struct octep_softc {
	device_t		 dev;
	struct mtx		 mtx;
	struct callout		 poll;

	struct resource		*bar0;		/* CSRs */
	struct resource		*bar2;		/* the 64 MB facility window */
	int			 rid0;
	int			 rid2;

	/* what the endpoint told us about itself */
	uint64_t		 scratch;
	int			 ready;
	uint32_t		 barmap_off;
	uint32_t		 barmap_version;
	uint32_t		 gicd_offset;
	uint32_t		 pem_num;
	struct octep_facility	 fclt[OCTEP_FACILITY_COUNT];

	uint64_t		 dbells_rung;

	/* the management facility */
	int			 mgmt_up;
	if_t			 ifp;
	uint8_t			 mac[6];
	struct octep_dma	 shadow;	/* the two consumer-index words */
	struct octep_dma	 rxbuf;
	struct octep_dma	 txbuf;
	volatile uint32_t	*tx_cons_shadow;
	volatile uint32_t	*rx_cons_shadow;
	uint32_t		 rx_prod;	/* what we have told the target */
	uint32_t		 rx_cons_local;	/* what we have drained */
	uint32_t		 tx_prod;
	uint32_t		 rx_cons_seen;
	uint32_t		 tx_cons_seen;
	uint64_t		 host_status;
	uint64_t		 target_status;
	uint8_t			 mbox_id;
	int			 poll_ticks;

	uint64_t		 rx_packets, rx_bytes, rx_drops, rx_errors;
	uint64_t		 tx_packets, tx_bytes, tx_drops, tx_errors;
};

/* ---------------------------------------------------------------- across the two files */

/* octep.c */
int	octep_read_barmap(struct octep_softc *sc, int verbose);
int	octep_ring_dbell(struct octep_softc *sc, uint32_t spi);
int	octep_ring_dbell_locked(struct octep_softc *sc, uint32_t spi);

/* octep_mgmt.c */
int	octep_mgmt_start(struct octep_softc *sc);
void	octep_mgmt_stop(struct octep_softc *sc);
void	octep_if_detach(struct octep_softc *sc);
void	octep_poll(void *arg);

extern const char *const octep_facility_name[OCTEP_FACILITY_COUNT];

#endif /* _OCTEP_H_ */
