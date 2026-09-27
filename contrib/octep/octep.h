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
 * See docs/families/octeon-tx.md for what each of them means and how it was checked, and
 * docs/octeontx/provenance.md for the licence of each source and what was taken from it.
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

/* ---------------------------------------------------------------- SDP, the datapath rings */

/*
 * SDP is the PCIe packet interface between host and coprocessor, and it is a different thing from
 * the management facility above. The coprocessor's own resource manager draws the distinction in
 * one place, `octeontx_main.c` filling a domain's config:
 *
 *	dcfg->net_port_count  = domain->bgx_count;	the twelve FRONT ports
 *	dcfg->virt_port_count = domain->lbk_count;	internal loopback
 *	dcfg->pci_port_count  = domain->sdp_count;	the HOST-facing ports
 *
 * The vendor's fast path blocks on `/sys/module/slipf/parameters/pci_port`, which is the SDP count,
 * so nothing reaches the front ports until the host brings SDP rings up. Everything named here is
 * read-only and is used only to report what the hardware already says about itself.
 *
 * All of it is BAR0. The vendor reaches these through octeon_read_csr64(), which is mmio[0] plus an
 * offset, and mmio[n] is PCI BAR n*2. Names follow cn83xx_pf_regs.h so the two can be read side by
 * side. CN83XX_EPF_OFFSET is 0 on this part, so the EPF index adds no stride and only the ring does.
 */

#define	OCTEP_SDP_EPF_RINFO	0x20190		/* CN83XX_SDP_EPF_RINFO_START */

#define	OCTEP_RINFO_SRN(v)	(((v) >> 0)  & 0x7fULL)		/* first ring this PF owns */
#define	OCTEP_RINFO_TRS(v)	(((v) >> 16) & 0xffULL)		/* how many it owns */
#define	OCTEP_RINFO_RPVF(v)	(((v) >> 32) & 0x1fULL)		/* rings carved off per VF */
#define	OCTEP_RINFO_NVFS(v)	(((v) >> 48) & 0x7fULL)		/* how many VFs */

/* CN83XX_RING_OFFSET. 128 KiB per ring, and 0x10000 + 64 * 0x20000 is exactly BAR0's 8 MB. */
#define	OCTEP_SDP_RING_STRIDE	0x20000

#define	OCTEP_SDP_R_IN_CONTROL		0x10000
#define	OCTEP_SDP_R_IN_ENABLE		0x10010
#define	OCTEP_SDP_R_IN_INSTR_BADDR	0x10020
#define	OCTEP_SDP_R_IN_INSTR_RSIZE	0x10030
#define	OCTEP_SDP_R_IN_INSTR_DBELL	0x10040
#define	OCTEP_SDP_R_IN_CNTS		0x10050
#define	OCTEP_SDP_R_OUT_CNTS		0x10100
#define	OCTEP_SDP_R_OUT_SLIST_BADDR	0x10120
#define	OCTEP_SDP_R_OUT_SLIST_RSIZE	0x10130
#define	OCTEP_SDP_R_OUT_SLIST_DBELL	0x10140
#define	OCTEP_SDP_R_OUT_CONTROL		0x10150
#define	OCTEP_SDP_R_OUT_ENABLE		0x10160

/* Past the last register of a ring - used to refuse any ring whose block leaves BAR0. */
#define	OCTEP_SDP_RING_SPAN		0x10198

/* The two control words, decoded only far enough to say what state a ring is in. */
#define	OCTEP_R_IN_CTL_IDLE	(1ULL << 28)
#define	OCTEP_R_IN_CTL_RDSIZE	(3ULL << 25)
#define	OCTEP_R_IN_CTL_IS_64B	(1ULL << 24)
#define	OCTEP_R_OUT_CTL_IDLE	(1ULL << 36)
#define	OCTEP_R_OUT_CTL_IMODE	(1ULL << 23)

/* ---------------------------------------------------------------- the SDP/EP-mode handshake */

/*
 * A SECOND scratch register, and a different one from OCTEP_SDP_SCRATCH above. That one carries the
 * barmap pointer and the readiness magic; this one, `CN83XX_SLI_EPF_SCRATCH_START`, carries a
 * four-step handshake whose only purpose is to tell the coprocessor how the host has divided the SDP
 * rings. It is the gate on the front ports, and finding it changed the shape of that work: the
 * coprocessor's `slipf` does not watch ring enables at all. It polls this register, and when the
 * handshake completes it creates an `octtx_sdp_port` and adds it to the list that
 * `sli_get_num_ports()` counts.
 *
 * The exchange, from `slipf_main.c:poll_for_ep_mode()` on the coprocessor and
 * `octeon_device.c:octeon_get_app_mode()` on the host:
 *
 *	host	writes HOST_LOADED			"I am here"
 *	target	writes GET_HOST_INFO, then spins	"tell me how you split the rings"
 *	host	writes the info word below
 *	target	reads it, writes (HOST_INFO_RECEIVED << 16) | ticks_per_us, then spins
 *	host	writes HANDSHAKE_COMPLETED
 *	target	sees it, marks the handshake done, writes 0, and creates the SDP port
 *
 * BOTH TARGET WAITS ARE BUSY LOOPS WITH NO TIMEOUT - literally `while (read == x) ;` inside a
 * workqueue. A host that starts this and then stops answering leaves a coprocessor core spinning
 * until it is rebooted. So the host side is a state machine on its own callout, it is armed only by
 * an explicit write, and every state it can wait in has a deadline with a defined way out.
 *
 * AND THE REGISTER HAS A SECOND LIFE, WHICH IS WHY THE HANDSHAKE RUNS EXACTLY ONCE. After the
 * exchange the target zeroes it, and from then on `sdp_port_start()` uses it as a bitmap of started
 * ports - bit 0 for the physical function, bit n for VF n. So once a port has started, this register
 * is carrying live state that belongs to the target, and writing HOST_LOADED over it would destroy
 * that. Arming refuses from OCTEP_HS_DONE for exactly that reason, and reading `hs_state` after the
 * fast path starts is how the host learns the port came up: the scratch goes from 0 to 1.
 *
 * THE TARGET'S POLLING WINDOW: THE SOURCE AND THE SILICON DISAGREE, AND THE SILICON WINS. In the
 * published `slipf_main.c`, `poll_for_ep_mode` requeues itself once a second only while a handshake
 * is outstanding and gives up for good after eleven misses - which would mean the window shuts about
 * eleven seconds after the coprocessor boots, and `slipf` is built into its kernel rather than a
 * module, so nothing short of a coprocessor reboot would reopen it. That is what the published
 * source says.
 *
 * It is not what the appliance does. The handshake was answered on the first attempt on a
 * coprocessor that had been up for more than five hours with no host ever having written this
 * register - its own log timestamps the exchange at 18336 seconds. The running kernel is
 * 4.14.207-10.22.03 against a published 4.14.76, and this is one of the places they differ. So the
 * window is not a constraint in practice; the deadlines below stay anyway, because they cost nothing
 * and the failure they guard against is a coprocessor core spinning until it is rebooted.
 */

#define	OCTEP_SLI_EPF_SCRATCH		0x28100

#define	OCTEP_SDP_HOST_LOADED		0xDEADBEEFULL
#define	OCTEP_SDP_GET_HOST_INFO		0xBEEFDEEDULL
#define	OCTEP_SDP_HOST_INFO_RECEIVED	0xDEADDEULL
#define	OCTEP_SDP_HANDSHAKE_COMPLETED	0xDEEDDEEDULL

/*
 * The info word. Field positions are the target's, which reads them straight back out:
 *
 *	rpvf 0:7   vf_srn 8:15   num_vfs 16:23   rppf 24:31   pf_srn 32:39   app_mode 40:47
 */
#define	OCTEP_SDP_INFO(app, pf_srn, rppf, nvfs, vf_srn, rpvf)			(((uint64_t)(app) << 40) | ((uint64_t)(pf_srn) << 32) |			 ((uint64_t)(rppf) << 24) | ((uint64_t)(nvfs) << 16) |			 ((uint64_t)(vf_srn) << 8) | (uint64_t)(rpvf))

/* CVM_DRV_NIC_APP. The only mode the target's CN83XX path accepts. */
#define	OCTEP_SDP_APP_MODE_NIC		2

/* The vendor's own `num_rings_per_pf` default. The target uses it as the port's channel count. */
#define	OCTEP_SDP_RINGS_PER_PF		8

enum octep_sdp_hs {
	OCTEP_HS_IDLE = 0,	/* nothing written; the register is as we found it */
	OCTEP_HS_LOADED,	/* HOST_LOADED published, waiting to be noticed */
	OCTEP_HS_INFO,		/* info word published, waiting for the target's reply */
	OCTEP_HS_DONE,		/* COMPLETED published; the register is the target's now */
	OCTEP_HS_TIMEOUT,	/* nobody was listening; the register was set back to zero */
};

#define	OCTEP_SDP_POLL_HZ	50
#define	OCTEP_HS_LOADED_TICKS	(5 * OCTEP_SDP_POLL_HZ)	/* target polls at 1 Hz */
#define	OCTEP_HS_INFO_TICKS	(2 * OCTEP_SDP_POLL_HZ)	/* its reply is immediate */

/* ---------------------------------------------------------------- the datapath rings */

/*
 * The remaining per-ring registers, and the policy bits the host has to set. Offsets follow
 * cn83xx_pf_regs.h; the values come from the SHIPPED v22 driver rather than from the source, because
 * the source wraps every one of them in #ifdef and a makefile is not proof of what was built. See
 * docs/octeontx/provenance.md for how each was settled.
 */
#define	OCTEP_SDP_R_IN_INT_LEVELS	0x10060
#define	OCTEP_SDP_R_IN_PKT_CNT		0x10080
#define	OCTEP_SDP_R_IN_BYTE_CNT		0x10090
#define	OCTEP_SDP_R_OUT_INT_LEVELS	0x10110
#define	OCTEP_SDP_R_OUT_PKT_CNT		0x10180
#define	OCTEP_SDP_R_OUT_BYTE_CNT	0x10190

/* R_IN_CONTROL policy: the host ORs these three in and touches nothing else. */
#define	OCTEP_R_IN_CTL_ESR		(1ULL << 1)

/*
 * R_OUT_CONTROL. The ordering and snoop attributes come in three sets - _P for the buffer/info pair
 * fetch, _I for info writes, _D for data writes - and the vendor clears all of them except ES_P.
 */
#define	OCTEP_R_OUT_CTL_ES_I		(1ULL << 34)
#define	OCTEP_R_OUT_CTL_NSR_I		(1ULL << 33)
#define	OCTEP_R_OUT_CTL_ROR_I		(1ULL << 32)
#define	OCTEP_R_OUT_CTL_ES_D		(1ULL << 30)
#define	OCTEP_R_OUT_CTL_NSR_D		(1ULL << 29)
#define	OCTEP_R_OUT_CTL_ROR_D		(1ULL << 28)
#define	OCTEP_R_OUT_CTL_ES_P		(1ULL << 26)
#define	OCTEP_R_OUT_CTL_NSR_P		(1ULL << 25)
#define	OCTEP_R_OUT_CTL_ROR_P		(1ULL << 24)

/* ISIZE (22:16) and BSIZE (15:0) share the low 23 bits and are cleared together before BSIZE. */
#define	OCTEP_R_OUT_CTL_SIZE_MASK	0x7fffffULL

/*
 * SETTLED FROM THE SHIPPED BINARY, not from the source.
 *
 * `default_cn83xx_pf_conf` is a 320-byte object in octeon_drv.ko's .data. Its instr_type field is
 * inside `#ifndef IOQ_PERF_MODE_O3`, and it reads 64 - so instructions are 64 bytes and the host must
 * SET IS_64B, which the hardware reads as clear. Its buf_size reads exactly 1536, and
 * CN83XX_OQ_BUF_SIZE is (1536 + MV_PPORT_OVERHEAD) where that overhead is 66 under CONFIG_PPORT - so
 * there is no port-extender header on this path, in either shipped variant.
 *
 * BUFPTR_ONLY_MODE could NOT be read off that object: its info_ptr field is the constant 1 in every
 * build. It was settled instead by a string that exists only in the other branch,
 * "OCTEON: Cannot allocate memory for info list.", which is absent from both shipped modules. So the
 * output ring carries BUFFER POINTERS ONLY - no info list, IMODE stays clear, and each filled buffer
 * begins with an 8-byte BIG-ENDIAN length followed by the target's 8-byte response header.
 */
#define	OCTEP_DP_INSTR_SIZE	64		/* OCTEON_64BYTE_INSTR */
#define	OCTEP_DP_SLIST_ENTRY	16		/* buffer_ptr, then an info_ptr we never write */
#define	OCTEP_DP_BUF_SIZE	1536		/* CN83XX_OQ_BUF_SIZE with no pport overhead */
#define	OCTEP_DP_OQ_INTR_PKT	8
#define	OCTEP_DP_OQ_INTR_TIME	2		/* microseconds */

/*
 * The vendor ships 2048 input and 4096 output descriptors. This driver uses 256 of each for a first
 * bring-up: it must be a power of two (the index arithmetic requires it, not the hardware), and 256
 * output descriptors at 1536 bytes is 384 KiB of coherent memory rather than 6 MiB. Raise it once
 * something has run.
 */
#define	OCTEP_DP_IQ_DESCS	256
#define	OCTEP_DP_OQ_DESCS	256

/* Which ring to bring up first. srn is 0 on this board and rings_per_pf was published as 8. */
#define	OCTEP_DP_RING		0

/*
 * THE 64-BYTE INSTRUCTION, as the vendor's own NIC path builds it for CN83XX. Word offsets:
 *
 *	dptr@0   ih3@8   pki_ih3@16   rptr@24   irh@32   exhdr[3]@40
 *
 * Three exhdr words, not four - that was corrected against the source rather than assumed. Only the
 * first five words carry anything here.
 *
 * ih3 is the `ihx` form on this chip, and its fields fill from the least significant bit on a
 * little-endian host:
 *
 *	tlen:16 (0-15)  rsvd:20 (16-35)  pkind:6 (36-41)  fsz:6 (42-47)  gsz:14 (48-61)
 *	gather:1 (62)   rsvd:1 (63)
 *
 * pki_ih3:
 *	tag:32 (0-31)   qpg:11 (32-42)  rsvd:2  tagtype:2 (45-46)  utt:1 (47)  sl:8 (48-55)
 *	pm:3 (56-58)    rsvd:1          uqpg:1 (60)  utag:1 (61)  raw:1 (62)  w:1 (63)
 *
 * irh:
 *	rid:16 (0-15)   pcie_port:3 (16-18)  scatter:1 (19)  rlenssz:14 (20-33)
 *	dport:6 (34-39) param:8 (40-47)      opcode:16 (48-63)
 *
 * TWO THINGS THAT ARE EASY TO GET WRONG, both taken from the vendor path rather than deduced.
 * `fsz` is 16 plus 4 for the PKI header plus 8 for the extra header = 28, and `sl` is the same 28 -
 * the skip length has to step over exactly the front data. And rptr and irh are written BYTE-SWAPPED
 * while dptr, ih3 and pki_ih3 are not: the vendor swaps those two in software "to avoid swapping on
 * the Octeon side", and the hardware's own swap on the instruction fetch is what R_IN_CTL_ESR turns
 * on.
 */
#define	OCTEP_INSTR_DPTR	0
#define	OCTEP_INSTR_IH3		8
#define	OCTEP_INSTR_PKI_IH3	16
#define	OCTEP_INSTR_RPTR	24
#define	OCTEP_INSTR_IRH		32

#define	OCTEP_IH3(tlen, pkind, fsz)					\
	(((uint64_t)(tlen) & 0xffff) |					\
	 (((uint64_t)(pkind) & 0x3f) << 36) |				\
	 (((uint64_t)(fsz) & 0x3f) << 42))

#define	OCTEP_PKI_IH3(tagtype, utt, sl, pm, w)				\
	((((uint64_t)(tagtype) & 0x3) << 45) |				\
	 (((uint64_t)(utt) & 1) << 47) |				\
	 (((uint64_t)(sl) & 0xff) << 48) |				\
	 (((uint64_t)(pm) & 0x7) << 56) |				\
	 (((uint64_t)(w) & 1) << 63))

#define	OCTEP_IRH(ckoff, param, opcode)					\
	((((uint64_t)(ckoff) & 0x3fff) << 20) |				\
	 (((uint64_t)(param) & 0xff) << 40) |				\
	 (((uint64_t)(opcode) & 0xffff) << 48))

/*
 * `irh.rlenssz` is a response length in the general case, but the vendor's NIC path overloads it as
 * the CHECKSUM OFFSET - it writes `TOTAL_TAG_LEN + sizeof(ethhdr) + 1`, which with no port-extender
 * tag is 0 + 14 + 1 = 15. That is the L3 header offset plus one, not the offset itself.
 *
 * LEAVING IT ZERO IS NOT SAFE, and this cost the coprocessor's fast path a crash. Frames posted with
 * rlenssz 0 and a non-IP EtherType took the far side into
 * `sso_event_tx_adapter_enqueue_noff_l3l4csum` and it died there with SIGSEGV: it computes an L3/L4
 * checksum on the way out and does not guard against being given nowhere to find the headers. So a
 * frame put on this ring is a well-formed IPv4 packet with this field set, or it is a fault on the
 * other side of the link.
 */
#define	OCTEP_IRH_CKSUM_OFF	15		/* sizeof(ethhdr) + 1 */

#define	OCTEP_OCT_NW_PKT_OP	0x1220		/* OCT_NW_PKT_OP */
#define	OCTEP_ORDERED_TAG	0		/* ORDERED_TAG */
#define	OCTEP_INSTR_FSZ		28		/* 16 + 4 (PKI_IH3) + 8 (extra header) */
#define	OCTEP_INSTR_PM		0		/* parse starting at L2 */

/*
 * The pkind the coprocessor assigned us. The vendor computes it as 40 + num_vfs on CN83XX, and we
 * published num_vfs = 0 in the EP-mode handshake, so it is 40. Settable, because it is the one value
 * here that depends on what the far side decided rather than on a register we can read.
 */
#define	OCTEP_DP_PKIND		40

/*
 * A received buffer, in buffer-pointer-only mode:
 *
 *	+0  u64 big-endian total length - the response header plus the payload, not itself.
 *	    It is also the arrival flag: zero means the coprocessor has not written yet.
 *	+8  u64 response header - rid:16, rsvd:2, csum_verified:2, dest_qport:22, src_port:6,
 *	    opcode:16
 *	+16 payload
 */
/*
 * ETH_ZLEN. The management target refuses a shorter frame WITHOUT consuming the descriptor, so one
 * undersized frame jams that ring permanently; there is no hardware here to pad for us. Kept beside
 * the other shared facts because both datapaths need it.
 */
#define	OCTEP_MIN_FRAME		60

#define	OCTEP_RX_LEN_OFF	0
#define	OCTEP_RX_RESP_OFF	8
#define	OCTEP_RX_DATA_OFF	16

/* ---------------------------------------------------------------- NetAgent */

/*
 * NetAgent is the control plane for the front ports - enumeration, link state, MTU, MAC, admin up
 * and down - and it is NOT the datapath. It is also Sophos's own and family-independent, which this
 * project asserted from where its source sits and has now measured on a second family: the header
 * this window publishes on OCTEON is identical, word for word, to the one docs/netagent.md records
 * for ARMADA.
 *
 * The protocol is described in docs/netagent.md and the offsets below carry the same names as
 * contrib/npuep/npunwa.h, so the two can be read side by side. Both sides POLL - a capture on the
 * management interface sees nothing at all, because none of this is packets.
 *
 * WHAT IS DIFFERENT HERE. The window is found through octep's barmap rather than at a fixed offset,
 * and the doorbell is a write of an SPI number to gicd_offset rather than an MSI-X vector. The
 * facility advertises exactly one host-to-target doorbell, 154 on this board.
 *
 * THE WINDOW BELONGS TO ANOTHER PROCESSOR. Never clear the target's status register to take a turn:
 * it may be mid-reply, and a host that resets the far side's register to get its own turn is how two
 * drivers end up writing one slot. Wait, or give up.
 */
#define	OCTEP_NWA_COOKIE	0x00
#define	  OCTEP_NWA_COOKIE_VALUE	0xCAFEBABEU
#define	OCTEP_NWA_BODY_OFF	0x04
#define	  OCTEP_NWA_BODY_EXPECTED	0x34
#define	OCTEP_NWA_MAX_REQ	0x08
#define	OCTEP_NWA_EVT_OFF	0x0c
#define	OCTEP_NWA_EVT_LEN	0x10

#define	OCTEP_NWA_TURN		0x18
#define	  OCTEP_NWA_TURN_REQUEST	1
/*
 * AND THE ACKNOWLEDGE, WHICH IS NOT OPTIONAL. The host writes 1 to send and 2 to acknowledge, and a
 * host that reads the reply and never acknowledges leaves the target holding the window: STATUS stays
 * at REPLY, and every later transaction times out waiting for idle. That is exactly what happened
 * here on the first attempt - a 2020-byte reply sat stranded in the window and nothing could be sent
 * again until it was released. Releasing it is a write of ACK to the host's own TURN field, never a
 * write to the target's STATUS.
 */
#define	  OCTEP_NWA_TURN_ACK		2
#define	OCTEP_NWA_REQ_LEN	0x1c
/* The target's own register, and the only thing worth polling. It never writes TURN. */
#define	OCTEP_NWA_STATUS	0x20
#define	  OCTEP_NWA_STATUS_IDLE		0
#define	  OCTEP_NWA_STATUS_REPLY	1
#define	OCTEP_NWA_REPLY_LEN	0x24

/* The request body, at OCTEP_NWA_BODY_EXPECTED. Get and set requests are 32 bytes. */
#define	OCTEP_NWA_REQ_SIZE	0x20
#define	OCTEP_NWA_RQ_OP		0x00
#define	OCTEP_NWA_RQ_SUB	0x04
#define	OCTEP_NWA_RQ_PORT	0x08
#define	OCTEP_NWA_RQ_PAYLOAD	0x10

/*
 * The reply follows the request at the UNROUNDED request length, so a 32-byte request is answered at
 * +0x54. Its length counts BYTES and INCLUDES the eight-byte header, so a one-byte answer is nine -
 * and the word count must ROUND UP. Truncating instead is what hid link state on ten ports for the
 * whole life of the ARMADA driver, because a nine-byte reply divided to zero words and every caller
 * read a definite "no". Then mask the tail, because the bytes past what the target wrote are whatever
 * the other processor left there.
 */
#define	OCTEP_NWA_RP_MARKER	0x00
#define	  OCTEP_NWA_RP_MARKER_VALUE	0x14
#define	OCTEP_NWA_RP_STATUS	0x04
#define	  OCTEP_NWA_RP_STATUS_OK		0
#define	OCTEP_NWA_RP_PAYLOAD	0x08

#define	OCTEP_NWA_OP_DISCOVER	0x01
#define	OCTEP_NWA_OP_GET	0x04

/*
 * The reply to a discover measured 2020 bytes here, so 64 words truncated it badly. 512 words is that
 * with room; it is not a protocol limit - the target advertises a maximum request near 32 KB.
 */
#define	OCTEP_NWA_MAX_WORDS	512
#define	OCTEP_NWA_IDLE_TRIES	100	/* x 10 ms, waiting for the previous transaction */
#define	OCTEP_NWA_REPLY_TRIES	300	/* x 10 ms, waiting for an answer */

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

	/* what SDP says about the datapath ring budget - read, never written */
	uint64_t		 sdp_rinfo;
	uint32_t		 sdp_srn;
	uint32_t		 sdp_trs;
	uint32_t		 sdp_rpvf;
	uint32_t		 sdp_nvfs;
	uint32_t		 sdp_rings_mappable;	/* how many fit inside BAR0 */

	/* the SDP/EP-mode handshake - the only thing in this driver that writes BAR0 */
	struct callout		 sdp_poll;
	int			 sdp_hs_state;
	int			 sdp_hs_ticks;
	uint64_t		 sdp_hs_info;		/* the word we published */
	uint64_t		 sdp_hs_seen;		/* the last value read back */
	uint32_t		 sdp_coproc_ticks_per_us;
	int			 sdp_hs_cleared;	/* the target zeroed it, i.e. it finished */

	/* the datapath - allocated and programmed only on an explicit request */
	int			 dp_up;
	uint32_t		 dp_ring;
	struct octep_dma	 dp_iq;		/* instruction ring, descs * 64 */
	struct octep_dma	 dp_slist;	/* scatter list, descs * 16 */
	struct octep_dma	 dp_bufs;	/* descs * OCTEP_DP_BUF_SIZE */
	uint32_t		 dp_time_threshold;
	uint32_t		 dp_pkind;
	uint32_t		 dp_iq_prod;		/* next instruction slot */
	struct octep_dma	 dp_txbuf;		/* one frame, for the test transmit */
	uint64_t		 dp_tx_posted;
	uint64_t		 dp_rx_seen;

	/* NetAgent - the control plane, reads only so far */
	int			 nwa_ready;
	uint32_t		 nwa_body;	/* request offset inside the window */
	uint32_t		 nwa_max_req;
	uint64_t		 nwa_commands;
	uint64_t		 nwa_timeouts;
	/* the last transaction's answer, so a read handler never has to issue one */
	uint32_t		 nwa_last_op;
	int			 nwa_last_error;
	int			 nwa_last_words;
	uint32_t		 nwa_last_marker;
	uint32_t		 nwa_last_status;
	uint32_t		 nwa_last_len;
	uint32_t		 nwa_last_reply[OCTEP_NWA_MAX_WORDS];

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

/* octep_sdp.c */
struct sysctl_ctx_list;		/* octep_mgmt.c has no need of <sys/sysctl.h> */
struct sysctl_oid_list;
void	octep_sdp_read_rinfo(struct octep_softc *sc, int verbose);
void	octep_sdp_add_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
	    struct sysctl_oid_list *top);
void	octep_sdp_handshake_stop(struct octep_softc *sc);

/* octep_dp.c */
int	octep_dp_start(struct octep_softc *sc);
void	octep_dp_stop(struct octep_softc *sc);
void	octep_dp_add_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
	    struct sysctl_oid_list *top);

/* octep_nwa.c */
int	octep_nwa_probe(struct octep_softc *sc, int verbose);
void	octep_nwa_add_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
	    struct sysctl_oid_list *top);

/* octep_mgmt.c */
int	octep_dma_alloc(struct octep_softc *sc, struct octep_dma *d, bus_size_t size,
	    bus_size_t align, const char *what);
void	octep_dma_free(struct octep_dma *d);
int	octep_mgmt_start(struct octep_softc *sc);
void	octep_mgmt_stop(struct octep_softc *sc);
void	octep_if_detach(struct octep_softc *sc);
void	octep_poll(void *arg);

extern const char *const octep_facility_name[OCTEP_FACILITY_COUNT];

#endif /* _OCTEP_H_ */
