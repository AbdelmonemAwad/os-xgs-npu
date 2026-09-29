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
 * THE POST-IOQ ANNOUNCE, and the five steps it is the last of.
 *
 * `oct_init_base_module` in the vendor's host driver finishes bringing the queues up like this, and
 * the order is the interesting part:
 *
 *	cn83xx_enable_pf_interrupt(pci_dev, 0xff)   then printk "Interrupts set up completed"
 *	cn83xx_enable_io_queues(oct)                writes 0xffffffff to the ring's 0x10040 and
 *	                                            polls it back to zero; printk "IQ/OQs Enable completed"
 *	for each output queue: *pkts_credit_reg = droq->max_count      a THIRTY-TWO bit store
 *	write 0x11223344 to BAR0 + 0x20180
 *	oct->status = 9, schedule_timeout, octeon_send_short_command(0x1004, 2)
 *
 * This driver already does the equivalent of `cn83xx_enable_output_queue` - 0xffffffff to the
 * credit register then bit 0 into the enable - and then credits the ring. It does not enable PF
 * interrupts, it does not do the 0x10040 write-and-poll, and it has never written the scratch word.
 *
 * Two things about that last call, both read out of the binary rather than assumed. The vendor
 * comments it "send an indication to f/w saying ioq creation is completed". And the short command
 * that follows it is DEAD: `octeon_send_short_command` tests `oct->status == 9` - which was set
 * three instructions earlier - and jumps into a stub whose whole body is a printk saying the path
 * is deprecated, followed by `ud2`. So on this firmware generation the bring-up is register writes
 * plus this scratch word, and there is no missing opcode to find.
 *
 * WHAT THIS WRITE COSTS. 0x20180 currently holds the barmap word - the facility table's offset in
 * the high half and the ready magic in the low half - which is how the facilities were found. So
 * `sdp.ioq_announce` saves it first and `sdp.ioq_restore` puts it back, and neither is done
 * automatically.
 */
#define	OCTEP_SDP_IOQ_DONE	0x11223344ULL

/*
 * THE INTERRUPT ENABLES, which are the step of the vendor's five this driver was missing.
 *
 * `cn83xx_enable_pf_interrupt` builds a mask of the rings this function owns - one bit per ring from
 * `srn` for `trs` rings, both read out of RINFO - and writes it to four enable registers, plus all
 * ones to a fifth:
 *
 *	SDP_EPF_IRERR_RINT_ENA_W1S   0x200b0   input-ring errors,  the ring mask
 *	SDP_EPF_ORERR_RINT_ENA_W1S   0x20130   output-ring errors, the ring mask
 *	SDP_EPF_OEI_RINT_ENA_W1S     0x20170   -1ULL, every bit
 *	SLI_EPF_MISC_RINT_ENA_W1S    0x28270   the ring mask
 *	SLI_EPF_PP_VF_RINT_ENA_W1S   0x282f0   the ring mask
 *
 * `_W1S` means write-one-to-set, so these are ORs and writing a mask cannot clear anything. The
 * `_RINT` registers themselves stay untouched: writing one of THOSE is how the vendor clears a
 * latched status, and this driver reads them as evidence.
 *
 * The one that is interesting is OEI. `SDP_EPF_OEI_RINT` at 0x20140 is the register the TARGET sets
 * to signal the host, and on this board it already reads 0x2 - so the target has signalled and
 * nothing on this side ever enabled the delivery of that signal. Whether the target waits on the
 * enable is not documented anywhere local; it is a measurement.
 *
 * CN83XX_EPF_OFFSET is 0 on this part, so there is no per-function stride and these are absolute
 * BAR0 offsets.
 */
#define	OCTEP_SDP_EPF_IRERR_RINT_ENA_W1S	0x200b0
#define	OCTEP_SDP_EPF_ORERR_RINT_ENA_W1S	0x20130
#define	OCTEP_SDP_EPF_OEI_RINT_ENA_W1S	0x20170
#define	OCTEP_SLI_EPF_MISC_RINT_ENA_W1S	0x28270
#define	OCTEP_SLI_EPF_PP_VF_RINT_ENA_W1S	0x282f0

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

/*
 * The set the vendor's cn83xx_pf_setup_global_oq_reg clears, and the one bit it sets. Disassembled
 * from octeon_drv.ko off a running SFOS 22.0.2 appliance: it clears IMODE and all nine ordering and
 * snoop bits, then ORs ES_P, and writes the result back. The live register on that appliance read
 * 0x1004000642 - bit 36 IDLE from the hardware, bit 26 ES_P from this function, and 1602 of BSIZE -
 * with every other bit clear, which is the whole word accounted for.
 */
#define	OCTEP_R_OUT_CTL_ATTR_MASK					\
	(OCTEP_R_OUT_CTL_IMODE | OCTEP_R_OUT_CTL_ES_I |			\
	 OCTEP_R_OUT_CTL_NSR_I | OCTEP_R_OUT_CTL_ROR_I |		\
	 OCTEP_R_OUT_CTL_ES_D | OCTEP_R_OUT_CTL_NSR_D |			\
	 OCTEP_R_OUT_CTL_ROR_D | OCTEP_R_OUT_CTL_ES_P |			\
	 OCTEP_R_OUT_CTL_NSR_P | OCTEP_R_OUT_CTL_ROR_P)

/* ISIZE (22:16) and BSIZE (15:0) share the low 23 bits and are cleared together before BSIZE. */
#define	OCTEP_R_OUT_CTL_SIZE_MASK	0x7fffffULL

/*
 * The bound on `dp.peek`, and the reason there is one.
 *
 * Reading a register is cheap and reading the WRONG one on this board is not. A sweep of BAR1's
 * per-megabyte windows wedged the appliance hard enough to need the power, and entry 15 of that
 * table is the coprocessor's interrupt controller - a window nothing on the host has any business
 * in. So `dp.peek` is deliberately not a general peek: it takes an offset inside one SDP ring's own
 * register block, adds the ring base the same way every other access here does, and refuses
 * anything else. BAR1 is not reachable through it at all.
 *
 * The block runs from R_IN_INSTR_BADDR at 0x10000 to R_OUT_BYTE_CNT at 0x10190 in the vendor's own
 * map, so the window below covers it with room for a register the map does not name yet.
 */
#define	OCTEP_PEEK_FIRST	0x10000
#define	OCTEP_PEEK_LAST		0x28fff

/*
 * The window was 0x10000..0x101f8 at first, which is one ring's datapath block. It reaches further
 * now because the interesting registers on a silent output queue are the LATCHED ERROR ones, and
 * they live above it:
 *
 *	0x10170  R_OUT_INT_STATUS   per ring
 *	0x10400  R_ERR_TYPE         per ring, and the GPL drop carries no bit names for it
 *	0x20080  EPF_IRERR_RINT     one bit per input ring
 *	0x20100  EPF_ORERR_RINT     one bit per OUTPUT ring - the register that says whether the
 *	                            block ever attempted a host write and failed
 *	0x20140  EPF_OEI_RINT       whether the target ever rang its own doorbell
 *	0x20180  SDP_SCRATCH(0)
 *	0x28240  SLI_EPF_MISC_RINT
 *	0x28500  SLI_EPF_DMA_RINT
 *
 * The vendor's own PF interrupt handler reads all of these as ordinary registers, including a sweep
 * of R_ERR_TYPE across all 64 rings, so reading them is what the chip expects. WRITING one is how
 * the vendor CLEARS it, so this stays a read-only sysctl: a write here would destroy the evidence
 * it exists to collect.
 *
 * Still nowhere near BAR1. See the note above on why that matters.
 */

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
/*
 * CN83XX_OQ_BUF_SIZE, and the pport overhead is part of it. The vendor builds with CONFIG_PPORT,
 * which sets MV_PPORT_OVERHEAD to 64 + 2 and makes the buffer 1536 + 66. This was 1536 on the
 * assumption that the overhead did not apply, which is the same assumption that made
 * TOTAL_TAG_LEN zero.
 *
 * BSIZE in OUT_CONTROL is what the far side believes each of these buffers holds. Too small and it
 * must split a frame or refuse it, and there is no register that says which.
 */
#define	OCTEP_DP_BUF_SIZE	(1536 + OCTEP_TOTAL_TAG_LEN)
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

/*
 * Each output-queue descriptor is TWO 64-bit words, not one: a buffer pointer and an info pointer.
 * The vendor's own header says so plainly - "the descriptor ring is made of descriptors which have 2
 * 64-bit values: physical address of the data buffer, physical address of an octeon_droq_info_t" -
 * and the device DMAs the packet to the first and its information to the second.
 *
 * octeon_droq_info_t is a response header and a length, so 16 bytes, and it must be real memory.
 * Leaving the info pointer at zero asks the far side to write a packet's header to physical address
 * zero.
 */
#define	OCTEP_DP_OQ_INFO_SIZE	16

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

#define	OCTEP_IRH(ckoff, dport, param, opcode)				\
	((((uint64_t)(ckoff) & 0x3fff) << 20) |				\
	 (((uint64_t)(dport) & 0x3f) << 34) |				\
	 (((uint64_t)(param) & 0xff) << 40) |				\
	 (((uint64_t)(opcode) & 0xffff) << 48))

/*
 * `irh.dport` selects which port the far side sends the frame out of, and this driver left it zero
 * for as long as the datapath had nothing plugged into it. Zero is not a neutral value: it is the
 * fast path's port 0, which on this board is the uplink to the 88E6193X - a switch that nothing
 * configures under a non-vendor operating system. So every frame was being addressed to the one
 * egress that could not deliver it.
 *
 * The fast path numbers its ports the way `usfp_table_print.sh worker_port_cnt` does: 0 is the
 * switch uplink, 1 is the first coprocessor MAC and 2 the second, which on the XGS 3300 are the two
 * SFP+ cages. That mapping was measured under the vendor firmware by sending a known number of
 * frames and watching exactly one counter move.
 */

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
/*
 * The host prepends a 66-byte private header to every frame: a 2-byte port tag in network order,
 * then 64 bytes of metadata. Both ends name the same split - PPORT_HLEN 2 and CUSTOM_META_TAG_LEN
 * 64 on the host side, PORT_TAG_SIZE 2 and METADATA_SIZE 64 on the coprocessor - and the fast path
 * counts what arrives without it in FPCNTR_FROM_KN_DROP_NO_METADATA.
 *
 * This driver had TOTAL_TAG_LEN as zero, which made the two derived constants below wrong by 66.
 */
#define	OCTEP_PPORT_HLEN	2
#define	OCTEP_CUSTOM_META_LEN	64
#define	OCTEP_META_START		0xc0		/* the 64 bytes run 0xc0..0xff */
/*
 * The vendor's own target application says the head of that 64-byte block is a signature,
 * not a pattern: apps_rxtx.h writes rte_cpu_to_be_64(METADATA_SIGNATURE) at PORT_TAG_SIZE
 * and reads it back the same way, and the same header defines
 *
 *	PORT_TAG_SIZE  2      METADATA_SIZE  64      PRIV_TAG_SIZE  66
 *
 * which is this driver's 66-byte header under the vendor's own names. The walking pattern
 * from 0xc0 came from reading the shipped binary rather than from that source, and it is what
 * this driver sends today - with frames that do reach a front port. Both cannot be the
 * requirement, so dp.meta selects which is sent and the answer is a measurement.
 */
#define	OCTEP_META_SIGNATURE	0xa0a1a2a3a4a5a6a7ULL
#define	OCTEP_META_MODE_PATTERN	0
#define	OCTEP_META_MODE_SIGNATURE	1
#define	OCTEP_META_MODE_ZERO	2
/*
 * Mode 3 is the only one of the four that came from the code that actually writes this block.
 *
 * `mrvl_cst_set_tx_meta` in the host-side `usfp_firewall.ko` is the `cst_set_tx_meta` callback the
 * GPL `pport` driver calls before it prepends anything: `pport_dev_hard_start_xmit` asks the hook
 * how many of the 64 bytes it has already written, fills only what is left with the walking pattern
 * - under a comment that says "FIXME: for debug" - and pushes the port tag on top. So the pattern
 * this driver has been sending is the filler for the bytes the vendor's hook did not claim, and
 * when the hook is loaded it claims all 64 of them.
 *
 * What the hook writes, in order, is a 64-byte push followed by:
 *
 *	meta[0]      = 1            a constant, the first store after the push
 *	meta[1]      = one bit      from the pport netdev's private area
 *	meta[2]      = one bit      from a per-entry word, only on one path
 *	meta[6..7]   = u16          zero, or a queue index the real device supplies
 *	meta[8..10]  = 25 bits      from an skb field
 *	meta[11]     = bits 1-7     from the high byte of that same field
 *	meta[12..13] = u16          from the netdev's private area, with 14..15 zeroed
 *
 * and it leaves bytes 16 to 63 as whatever was in the skb's headroom. So the whole contract is in
 * the first sixteen bytes, and only one byte of it is a constant this driver can know without a
 * netdev: `meta[0] = 1`. Mode 3 writes that and zeros the rest.
 *
 * VERSION, stated because it matters: that module is the v21 XGS 136 host copy, the only one held
 * locally in a readable form. The appliance's coprocessor runs v22. The metadata is a host-to-fast
 * path contract, and the fast path binary is the same product on both families, so the layout is
 * expected to carry - but it is a lead to measure, not a v22 fact.
 */
#define	OCTEP_META_MODE_VENDOR	3
#define	OCTEP_META_VENDOR_BYTE0	1
#define	OCTEP_TOTAL_TAG_LEN	(OCTEP_PPORT_HLEN + OCTEP_CUSTOM_META_LEN)

#define	OCTEP_IRH_CKSUM_OFF	(OCTEP_TOTAL_TAG_LEN + 14 + 1)	/* TOTAL_TAG_LEN + ethhdr + 1 */
#define	OCTEP_INSTR_SL		(OCTEP_INSTR_FSZ + OCTEP_TOTAL_TAG_LEN)

#define	OCTEP_OCT_NW_PKT_OP	0x1220		/* OCT_NW_PKT_OP */
/*
 * THE CONTROL MESSAGE, and why it is the thing that was missing.
 *
 * The fast path's own counter list has a pair for control traffic arriving from the host:
 * FPCNTR_FROM_KN_PROC_CMSG at index 94 and FPCNTR_FROM_KN_DROP_CMSG at 93. A capture taken off this
 * board while the vendor's firmware was driving it reads
 *
 *	FPCNTR_FROM_KN_PROC_CMSG : 748
 *
 * against 49,966 data frames. So the host sends control messages in-band, on the same ring as data,
 * and the fast path counts them separately. This driver has sent exactly none of them.
 *
 * A control message is an instruction with opcode OCT_NW_CMD_OP whose data is one 64-bit word:
 *
 *	param3:8 (0-7)   param2:16 (8-23)   param1:32 (24-55)   more:3 (56-58)   cmd:5 (59-63)
 *
 * and the one that matters here is RX_CTL. The vendor sends it from the interface's open and stop
 * handlers, with param1 the interface index and param2 the start/stop flag:
 *
 *	nctrl.ncmd.s.cmd    = OCTNET_CMD_RX_CTL;
 *	nctrl.ncmd.s.param1 = priv->linfo.ifidx;
 *	nctrl.ncmd.s.param2 = start_stop;
 *	nparams.resp_order  = OCTEON_RESP_NORESPONSE;
 *
 * No response is asked for, so rptr and rlenssz stay zero. The word is NOT byte-swapped: the
 * vendor's swap of it is commented out in its own source.
 *
 * The version caveat that applies everywhere else applies here: this is the GPL drop, SDK10.22.03,
 * and the appliance runs v22.0.2. The counter that motivates it, though, was read off this board.
 */
#define	OCTEP_OCT_NW_CMD_OP	0x1221		/* OCT_NW_CMD_OP */
#define	OCTEP_OCTNET_CMD_RX_CTL	0x4

#define	OCTEP_OCTNET_CMD(cmd, more, p1, p2, p3)				\
	((((uint64_t)(cmd) & 0x1f) << 59) |				\
	 (((uint64_t)(more) & 0x7) << 56) |				\
	 (((uint64_t)(p1) & 0xffffffffULL) << 24) |			\
	 (((uint64_t)(p2) & 0xffff) << 8) |				\
	 ((uint64_t)(p3) & 0xff))

/*
 * The vendor builds a control instruction with fsz 16 rather than the 28 a data packet uses, and
 * with no PKI header. Both forms are offered here because which one this target accepts is a
 * measurement, not a deduction - the legacy path that would have settled it from source is marked
 * deprecated and guarded by BUG_ON for this chip.
 */
#define	OCTEP_CMD_FSZ_VENDOR	16
#define	OCTEP_CMD_FSZ_LIKE_DATA	28

/*
 * THE CONTROL MESSAGE BODY, extracted from the host module that builds it.
 *
 * `usfp_cmsg_alloc(type, len, gfp)` in usfp_firewall.ko allocates len + 0x46 bytes, steps over
 * 0x42 - the 66-byte private header this driver already writes - and lays down four bytes:
 *
 *	movb $0x1,0x45(%rax)      version = 1
 *	movb <type>,0x44(%rax)    type
 *	movw $0x0,0x42(%rax)      two zero bytes
 *
 * then puts len + 4. The receive side agrees field for field:
 * `usfp_firewall_cmsg_process_one_rx` reads byte 3 and refuses anything but 1, reads byte 2 as the
 * type, refuses a value above 6, and jumps through a seven-entry table.
 *
 *	+0  u16 rsvd      always written zero
 *	+2  u8  type      0..6 are target-to-host; 7 is the one the host sends
 *	+3  u8  version   1, and the receiver checks it
 *	+4  u32 count     how many entries follow
 *	+8  entries
 *
 * The only host-to-target message in that module is the port speed notification, type 7, built by
 * `usfp_pport_monitor_speed_work` with len 0x44. Its entries are four bytes each - a port tag and a
 * value - and the filler refuses to write a seventeenth, so 4 + 4 + 16 * 4 = 72, which is exactly
 * what the allocator puts. Type 5's length check on the receive side has the same shape,
 * `>= 8 + 72 * count` with the count at +4, so the count-at-+4 layout is not particular to type 7.
 *
 * VERSION: usfp_firewall.ko here is the v21 XGS 136 host copy, the only one held readable. The
 * appliance runs v22. Treat the layout as a lead that is then measured, which is what the counters
 * FPCNTR_FROM_KN_PROC_CMSG and FPCNTR_FROM_KN_DROP_CMSG make possible.
 */
#define	OCTEP_CMSG_VERSION	1
#define	OCTEP_CMSG_TYPE_PORT_SPEED	7
#define	OCTEP_CMSG_MAX_ENTRIES	16
#define	OCTEP_CMSG_PORT_TAG	254		/* measured: this tag, and only this tag, is control */

/*
 * AND THE FOURTEEN BYTES THAT WERE MISSING.
 *
 * The four-byte header above is right and it is not the whole frame. The coprocessor's own v22 fast
 * path says what a control message has to look like, and it says it in five instructions:
 *
 *	426c28  cmp   w0, #0xfe          the port tag
 *	426c2c  b.eq  429030             and only then, the control branch
 *	429034  mov   w3, #0xefef
 *	429040  ldrh  w1, [x2, #12]      a u16 at offset 12 of what follows the private header
 *	429044  cmp   w1, w3
 *	429048  b.eq  42a430             a control message, or nothing
 *	42a430  ldrb  w4, [x2, #17]      and the version at offset 17
 *	42a434  cmp   w4, #1
 *
 * Offset 12 of a frame is an EtherType, and the same routine proves it two instructions later: for
 * every frame that is NOT on tag 254 it reads `[x1, #12]` and compares against `#0x8` and `#0x81`,
 * which are 0x0800 and 0x8100 read as little-endian u16s off a big-endian wire. It also keeps
 * `mov w27, #0xe` - fourteen, the Ethernet header length - for that path.
 *
 * So a control message is **an ordinary Ethernet frame with EtherType 0xEFEF**, and the four-byte
 * header is its first four payload bytes: offset 12 is the EtherType, 16 is `type` and 17 is
 * `version`. The driver had been writing the four bytes straight after the metadata, so the fast
 * path read its EtherType out of what was actually the count field, never saw 0xEFEF, and took
 * FPCNTR_FROM_KN_DROP_CMSG - exactly the measured behaviour, message after message, whatever type
 * was in it.
 *
 * 0xEFEF is byte-symmetric, so wire order does not arise for this one value.
 *
 * One more constraint from the same routine: at 426bd8 it strips a further 66 bytes only if what
 * remains after the 28-byte instruction header exceeds 0x41. A control message shorter than about
 * 94 bytes would skip that strip and have its EtherType read from inside the metadata instead.
 */
#define	OCTEP_CMSG_ETHERTYPE	0xefef
#define	OCTEP_CMSG_ETH_HLEN	14
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

/*
 * What an untouched receive buffer holds. The poison has to be something no plausible length could
 * be, so that "nothing arrived" is a reading rather than an absence of one.
 */
#define	OCTEP_DP_BUF_POISON	0xa5
#define	OCTEP_DP_BUF_POISON_WORD	0xa5a5a5a5a5a5a5a5ULL

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
/*
 * READING A FACILITY WINDOW, and the bound on it.
 *
 * The management facility delivers frames into host memory and the SDP datapath does not, and the
 * difference is not the ring: it is that the management path PUBLISHES host physical addresses into
 * the coprocessor's window, in descriptors the target reads, and the datapath publishes its buffers
 * only through SDP registers. So the question is whether the datapath has a structure of that shape
 * anywhere in a window, and the only way to answer it is to look.
 *
 * `fclt.peek` reads sixty-four bytes at an offset inside the four published windows and nowhere
 * else. The windows are a megabyte each, from 0x02000000, and this driver has only ever used the
 * first few dozen bytes of two of them. The bound matters for the same reason dp.peek's does: BAR1
 * is not reachable through this, and neither is anything outside the four windows the target itself
 * announced.
 */
#define	OCTEP_FCLT_PEEK_FIRST	0x02000000
#define	OCTEP_FCLT_PEEK_LAST	0x023fffc0

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

/*
 * The operations, from docs/netagent.md where each was confirmed on ARMADA. SET is defined here so it
 * can be REFUSED by name rather than accidentally issued: it changes a port's administrative state,
 * MTU or address, and nothing in this driver has any business doing that yet.
 *
 * The port field carries a TAG, not an ordinal - on ARMADA the front ports are 0x8100, 0x8200 and so
 * on. OCTEON's tags are not known, which is why the request below takes the value from a sysctl
 * instead of an index: finding out what the far side accepts is the point.
 */
#define	OCTEP_NWA_OP_DISCOVER	0x01
#define	OCTEP_NWA_OP_SET	0x03		/* two subs only - see octep_nwa_do_request() */
#define	OCTEP_NWA_OP_GET	0x04
#define	OCTEP_NWA_OP_STATUS	0x45

/*
 * SET sub-codes. These are enum nwa_msg_port_attr in Marvell's own mv_nwa_host.h, which the
 * v21 and v22 GPL drops define identically. Only STATE is reachable from this driver; every
 * other attribute is refused by name in octep_nwa_do_request().
 *
 * The target reads the state payload as a boolean - npu_port_state_set() passes !!state - so
 * any non-zero value means up. This driver still accepts only 0 and 1, because a field whose
 * meaning is 'anything non-zero' is a field worth narrowing before it reaches hardware.
 */
/*
 * FEC. Never send this, in either direction.
 *
 * A sweep of the attribute space on this appliance reached 0x0b and the target's NetAgent
 * handler stopped answering - not for that request, but for every request after it, including
 * ones that had just worked. Nothing short of a coprocessor reboot brought it back, and `usfp`
 * had not crashed, so it is the handler going quiet rather than the process dying.
 *
 * It is not an obscure code. It is what Marvell's own host stack sends for `ethtool --show-fec`
 * on a front port, four calls deep and all of it source, and it applies to every port whose
 * switch-init record carries the MNG flag - which here is every panel port. So this driver
 * refuses it by number rather than leaving it to a reader's care. See issue #78.
 */
#define	  OCTEP_NWA_SUB_FEC	0x0b

#define	OCTEP_NWA_SUB_STATE	0x00
#define	OCTEP_NWA_STATE_DOWN	0
#define	OCTEP_NWA_STATE_UP	1

#define	  OCTEP_NWA_SUB_LINK	0x04		/* with OP_GET: query link; the answer has the speed */
/*
 * Promiscuous, and it is not a convenience. Marvell's own host module reaches it through
 * pport's ndo_set_rx_mode, and the vendor's bring-up document says in as many words that a
 * host port has to be in this mode for the coprocessor to forward frames to the host at all:
 *
 *	# ifconfig pport_l0 up
 *	# ifconfig pport_l0 promisc
 *
 * The number is enumerated, not guessed: nwa_msg_port_attr starts at 0 and jumps to 64 at
 * SUPP_LINK_MODES, which puts PROMISC at 69. Every other value this driver had already
 * measured falls where that enum says it should - 0x0d DUPLEX, 0x0e STATS, 0x50 PHY_ID,
 * 0x55 KSETTINGS - which is the check that the enum is the right one.
 */
#define	  OCTEP_NWA_SUB_PROMISC	0x45
#define	  OCTEP_NWA_PROMISC_OFF	0
#define	  OCTEP_NWA_PROMISC_ON	1

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

/*
 * The RPC facility.
 *
 * Every number here is from usfp_rh.ko as shipped on this appliance - its .debug_macro for the
 * constants, its .debug_info for the layouts, and a disassembly for the ordering. The module is
 * release v22.0.Maint.060.Bali, which is the board's own build. See
 * docs/families/octeon-tx-rpc.md, which is that reading written out in full.
 */
#define	OCTEP_RPC_STATE_CFG_MAGIC	0xD7D3AB00U
#define	OCTEP_RPC_STATE_SIZE		232
#define	OCTEP_RPC_STATE_CFG		0
#define	OCTEP_RPC_STATE_RING_LO		72
#define	OCTEP_RPC_STATE_RINGS		104
#define	OCTEP_RPC_RING_SIZE		32
#define	OCTEP_RPC_HI_RINGS_MAX		4

/* struct rpc_ring */
#define	OCTEP_RPC_RING_POSTED		0
#define	OCTEP_RPC_RING_DONE		8
#define	OCTEP_RPC_RING_OFFSET		16
#define	OCTEP_RPC_RING_DESC_OFF		20
#define	OCTEP_RPC_RING_DESC_CNT		24
#define	OCTEP_RPC_RING_CFG		28
/* union ring_hw_cfg: ring_num, facility index, doorbell, shared */
#define	OCTEP_RPC_RCFG(num, fidx, dbell, shared)				\
	(((uint32_t)(num) & 0xff) | (((uint32_t)(fidx) & 0xff) << 8) |	\
	 (((uint32_t)(dbell) & 0xff) << 16) | (((uint32_t)(shared) & 0xff) << 24))

/* struct rpc_cmd_bar_desc, in the window */
#define	OCTEP_RPC_BAR_DESC_SIZE		16
#define	OCTEP_RPC_DESC_POST_FLAG	1
#define	OCTEP_RPC_DESC_NO_AGG_DMA	2

/* struct rpc_cmd_buf_desc and struct rpc_resp_buf_desc, in host memory */
#define	OCTEP_RPC_BUF_DESC_SIZE		8
#define	OCTEP_RPC_DATA_MAX_SIZE		4096
#define	OCTEP_RPC_RC_ERRNO_BIT		(1 << 15)

/* struct usfp_fpop_req_table_read: s_index, num_entries, flags, e_index */
#define	OCTEP_RPC_REQ_LEN		12

/*
 * enum rpc_cmd_type. Only the commands that read are named, because only those are issued.
 * The writing half of that enumeration - LIF_ADD_UPDATE at 3, the flow and connection commands,
 * the QoS and DoS and IPsec ones - is in the document and deliberately not here.
 */
#define	OCTEP_RPC_CMD_LIF_ADD_UPDATE		3
#define	OCTEP_RPC_CMD_PPORT_UPDATE		5
#define	OCTEP_RPC_CMD_PLATFORM_READ		36
#define	OCTEP_RPC_CMD_LO_LIF_READ		37
#define	OCTEP_RPC_CMD_LO_CONN_READ		38
#define	OCTEP_RPC_CMD_LO_NHOP_READ		39
#define	OCTEP_RPC_CMD_LO_MFLOW_READ		40
#define	OCTEP_RPC_CMD_LO_LUID_READ		41
#define	OCTEP_RPC_CMD_LO_SA_READ		42
#define	OCTEP_RPC_CMD_LO_WORKER_DBG_CNT_READ	43
#define	OCTEP_RPC_CMD_LO_WORKER_SYS_CNT_READ	44
#define	OCTEP_RPC_CMD_LO_WORKER_PORT_CNT_READ	45
#define	OCTEP_RPC_CMD_LO_WORKER_DF_CNT_READ	46
#define	OCTEP_RPC_CMD_MAX			51

#define	OCTEP_RPC_CFG_WAIT_MS		3000
#define	OCTEP_RPC_CMD_WAIT_MS		2000
#define	OCTEP_RPC_MAX_REPLY_WORDS	256
#define	OCTEP_RPC_BUF_POISON		0x5a

/* struct usfp_lif_entry's flags word, and the mask an add-or-update carries */
#define	OCTEP_LIF_FWD_MODE_INVALID	0x0
#define	OCTEP_LIF_FWD_MODE_L2		0x1
#define	OCTEP_LIF_FWD_MODE_L3		0x2
#define	OCTEP_LIF_FWD_MODE_BOTH		0x3
#define	OCTEP_LIF_M_MAC			0x0001
#define	OCTEP_LIF_M_MTU			0x0002
#define	OCTEP_LIF_M_FWD			0x0004
#define	OCTEP_LIF_M_ADMIN_DISABLED	0x0008
#define	OCTEP_LIF_M_OFFLOAD_DISABLED	0x0010
#define	OCTEP_LIF_M_REPPID		0x0020
#define	OCTEP_LIF_M_ALL			0x003f

static __inline int
octep_rpc_cmd_is_read(uint32_t cmd)
{

	return (cmd == OCTEP_RPC_CMD_PLATFORM_READ ||
	    (cmd >= OCTEP_RPC_CMD_LO_LIF_READ &&
	     cmd <= OCTEP_RPC_CMD_LO_WORKER_DF_CNT_READ));
}

/*
 * The only two writes this driver will issue, and they are a pair: a port mapping is what
 * makes an ingress tag resolve to an interface, and a logical interface is what the wire-to-host
 * gate then finds. Everything else in the enumeration stays refused by number.
 */
static __inline int
octep_rpc_cmd_is_allowed_write(uint32_t cmd)
{

	return (cmd == OCTEP_RPC_CMD_LIF_ADD_UPDATE ||
	    cmd == OCTEP_RPC_CMD_PPORT_UPDATE);
}

static __inline const char *
octep_rpc_cmd_name(uint32_t cmd)
{

	switch (cmd) {
	case OCTEP_RPC_CMD_LIF_ADD_UPDATE:		return ("LIF_ADD_UPDATE");
	case OCTEP_RPC_CMD_PPORT_UPDATE:		return ("PPORT_UPDATE");
	case OCTEP_RPC_CMD_PLATFORM_READ:		return ("PLATFORM_READ");
	case OCTEP_RPC_CMD_LO_LIF_READ:		return ("LO_LIF_READ");
	case OCTEP_RPC_CMD_LO_CONN_READ:		return ("LO_CONN_READ");
	case OCTEP_RPC_CMD_LO_NHOP_READ:		return ("LO_NHOP_READ");
	case OCTEP_RPC_CMD_LO_MFLOW_READ:		return ("LO_MFLOW_READ");
	case OCTEP_RPC_CMD_LO_LUID_READ:		return ("LO_LUID_READ");
	case OCTEP_RPC_CMD_LO_SA_READ:		return ("LO_SA_READ");
	case OCTEP_RPC_CMD_LO_WORKER_DBG_CNT_READ:	return ("LO_WORKER_DBG_CNT_READ");
	case OCTEP_RPC_CMD_LO_WORKER_SYS_CNT_READ:	return ("LO_WORKER_SYS_CNT_READ");
	case OCTEP_RPC_CMD_LO_WORKER_PORT_CNT_READ:	return ("LO_WORKER_PORT_CNT_READ");
	case OCTEP_RPC_CMD_LO_WORKER_DF_CNT_READ:	return ("LO_WORKER_DF_CNT_READ");
	default:					return ("");
	}
}

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
	struct octep_dma	 dp_info;	/* descs * OCTEP_DP_OQ_INFO_SIZE */
	uint32_t		 dp_time_threshold;
	uint32_t		 dp_pkind;
	uint32_t		 dp_dport;
	uint32_t		 dp_port_tag;
	uint32_t		 dp_iq_prod;		/* next instruction slot */
	struct octep_dma	 dp_txbuf;

	/* the RPC facility: one command buffer, and what the last command returned */
	struct octep_dma		 rpc_cmd;
	uint32_t		 rpc_ready;
	uint32_t		 rpc_revision;
	uint32_t		 rpc_dbell;
	uint32_t		 rpc_shared;
	uint32_t		 rpc_desc_off;
	uint32_t		 rpc_desc_count;
	uint32_t		 rpc_cmd_num;
	uint32_t		 rpc_s_index;
	uint32_t		 rpc_e_index;
	uint32_t		 rpc_num_entries;
	uint32_t		 rpc_req_flags;
	uint32_t		 rpc_resp_sz;
	uint32_t		 rpc_desc_flags;
	uint32_t		 rpc_allow_write;
	uint32_t		 rpc_lif_iface;
	uint32_t		 rpc_lif_vlan;
	uint32_t		 rpc_lif_tag;
	uint32_t		 rpc_lif_mtu;
	uint32_t		 rpc_lif_fwd;
	uint32_t		 rpc_lif_admin_dis;
	uint32_t		 rpc_lif_offload_dis;
	uint32_t		 rpc_lif_reppid;
	uint32_t		 rpc_lif_df;
	uint32_t		 rpc_lif_mask;
	uint8_t			 rpc_lif_mac[6];
	uint32_t		 rpc_commands;
	uint32_t		 rpc_timeouts;
	uint32_t		 rpc_last_cmd;
	uint16_t		 rpc_last_rc;
	uint16_t		 rpc_last_seed;
	uint16_t		 rpc_last_len;
	uint8_t			 rpc_last_done;
	int			 rpc_last_error;
	uint8_t			 rpc_last_reply[OCTEP_RPC_DATA_MAX_SIZE];		/* one frame, for the test transmit */
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
	uint32_t		 nwa_last_sub;
	uint32_t		 nwa_last_port;
	uint32_t		 nwa_last_param;
	int			 nwa_last_error;
	int			 nwa_last_words;
	uint32_t		 nwa_last_marker;
	uint32_t		 nwa_last_status;
	uint32_t		 nwa_last_len;
	uint32_t		 nwa_last_reply[OCTEP_NWA_MAX_WORDS];
	/* what the next request will carry; set by sysctl, because the tags are not known */
	uint32_t		 nwa_req_op;
	uint32_t		 nwa_req_sub;
	uint32_t		 nwa_req_port;
	uint32_t		 nwa_req_param;
	uint32_t		 dp_meta_mode;
	uint8_t			 dp_dst_mac[6];
	uint32_t		 dp_peek_off;
	uint32_t		 dp_cmd;
	uint32_t		 dp_cmd_p1;
	uint32_t		 dp_cmd_p2;
	uint32_t		 dp_cmd_p3;
	uint32_t		 dp_cmd_more;
	uint32_t		 dp_cmd_fsz;
	uint32_t		 dp_meta_b0;
	uint32_t		 dp_cmsg_type;
	uint32_t		 dp_cmsg_count;
	uint32_t		 dp_cmsg_port;
	uint32_t		 dp_cmsg_value;
	/* what the handshake publishes; zero means "use what RINFO and the defaults say" */
	uint32_t		 hs_pf_srn;
	uint32_t		 hs_rppf;
	uint32_t		 hs_nvfs;
	uint32_t		 hs_vf_srn;
	uint32_t		 hs_rpvf;
	uint64_t		 scratch_saved;
	uint32_t		 fclt_peek_off;
	uint32_t		 dp_sport;

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
void	octep_rpc_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
	    struct sysctl_oid *node);
int	octep_dma_alloc(struct octep_softc *sc, struct octep_dma *d, bus_size_t size,
	    bus_size_t align, const char *what);
void	octep_dma_free(struct octep_dma *d);
int	octep_mgmt_start(struct octep_softc *sc);
void	octep_mgmt_stop(struct octep_softc *sc);
void	octep_if_detach(struct octep_softc *sc);
void	octep_poll(void *arg);

/*
 * Names for what the transaction log prints. These come from Marvell's own two enumerations
 * rather than from the wire, and every value this driver had measured falls where they put it -
 * which is the check that they are the right enumerations. Only the values this driver can
 * actually produce are listed; a number with no name prints as a number.
 */
const char	*octep_nwa_op_name(uint32_t op);
const char	*octep_nwa_sub_name(uint32_t sub);

extern const char *const octep_facility_name[OCTEP_FACILITY_COUNT];

#endif /* _OCTEP_H_ */
