/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
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

/*
 * The softc below holds a timeout_task and each front port an ifmedia, both by value, so the
 * definitions have to be here rather than in whichever file happens to include this one.
 */
#include <sys/taskqueue.h>
#include <sys/mbuf.h>
#include <net/if_media.h>

#include "sfosnum.h"

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
 * This driver does the equivalent of `cn83xx_enable_output_queue`, and then credits the ring - with
 * the 0xffffffff drain in front of the base address rather than after it, which is the only place
 * it is a drain and not a grant of four billion buffers. All three of the things this paragraph once
 * listed as missing have since been done and measured: the PF interrupt enables, the 0x10040
 * write-and-poll and the scratch word are all reachable from sysctls, and none of them changed
 * anything - they are negatives 1, 2 and 9 in the list the family page keeps.
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
/*
 * CN83XX_SDP_EPF_R_VF_NUM, a ring register: which virtual function the ring belongs to, counted
 * from 1, 0 for the PF. The vendor's cn83xx_setup_global_mac_regs writes it for each of the first
 * nvfs * rpvf rings in the same breath as it publishes the split, and the PF's rings follow them.
 */
#define	OCTEP_SDP_EPF_R_VF_NUM	0x10500
#define	  OCTEP_R_VF_NUM_MASK	0x7fULL

/* CN83XX_RING_OFFSET. 128 KiB per ring, so 64 * 0x20000 is exactly BAR0's 8 MB, and ring 63's
 * block at 0x7e0000 + 0x10000 ends well inside it. */
#define	OCTEP_SDP_RING_STRIDE	0x20000

#define	OCTEP_SDP_R_IN_CONTROL		0x10000
#define	OCTEP_SDP_R_IN_ENABLE		0x10010
#define	OCTEP_SDP_R_IN_INSTR_BADDR	0x10020
#define	OCTEP_SDP_R_IN_INSTR_RSIZE	0x10030
#define	OCTEP_SDP_R_IN_INSTR_DBELL	0x10040
#define	OCTEP_SDP_R_IN_CNTS		0x10050
#define	OCTEP_SDP_R_OUT_CNTS		0x10100
/*
 * Bit 59 of R_OUT_CNTS, written alone: the block looks at the count against the interrupt level
 * again and raises the interrupt if it is there to be raised. The vendor's CN83XX host writes it at
 * the end of every poll, after the count, and on R_IN_CNTS as well (octeon_enable_irq,
 * pcie_ep_octeontx's octeon_device.c); octep_enable_ioq_irq and OCTEP_OQ_INTR_RESEND_BIT, named
 * here until 2026-10-25, are Linux's octeon_ep for the later chips, not the vendor's driver for
 * this one. The count bits must be zero in that write or they would be subtracted.
 */
#define	OCTEP_SDP_R_OUT_CNTS_RESEND	(1ULL << 59)
/* Bit 62 of R_OUT_CNTS, read: the count's interrupt is raised (cn83xx_pf_regs.h, OUT_INT). */
#define	OCTEP_SDP_R_OUT_CNTS_OUT_INT	(1ULL << 62)
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
#define	OCTEP_SDP_R_OUT_INT_STATUS	0x10170	/* latched; the vendor never writes it */
#define	OCTEP_SDP_R_VF_NUM		0x10500	/* which function owns this ring; 0 is the PF */
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
 * from octeon_drv.ko off a running vendor build 22.0.2 appliance: it clears IMODE and all nine ordering and
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
 * The block runs from R_IN_CONTROL at 0x10000 - R_IN_INSTR_BADDR is 0x10020, two registers into it -
 * to R_OUT_BYTE_CNT at 0x10190 in the vendor's own map, so the window below covers it with room for
 * a register the map does not name yet.
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
 * neither shipped host module was built with the port-extender header. The appliance itself is: its
 * own OUT_CONTROL reads 0x1004000642, whose low 23 bits are 1602, which is 1536 + 66. The modules
 * describe a build; the register describes this board.
 *
 * BUFPTR_ONLY_MODE could NOT be read off that object: its info_ptr field is the constant 1 in every
 * build. It was settled instead by a string that exists only in the other branch,
 * "OCTEON: Cannot allocate memory for info list.", which is absent from both shipped modules. So the
 * output ring carries BUFFER POINTERS ONLY - no info list, IMODE stays clear, and each filled buffer
 * begins with an 8-byte BIG-ENDIAN length followed by the target's 8-byte response header.
 */
#define	OCTEP_DP_INSTR_SIZE	64		/* OCTEON_64BYTE_INSTR */
/*
 * A scatter-list entry is 16 bytes: a buffer pointer, then an info pointer. The vendor's refill path
 * writes only the first eight bytes and leaves the second word at zero; this driver writes a real
 * info-block address there. The block demonstrably never uses it - the whole info region was read
 * out of /dev/mem after a delivery and is zero to the last byte - so the difference is cosmetic, and
 * it is recorded rather than removed because removing it would be an untested change.
 *
 * The entry size was long taken for the doorbell's unit as well. It is not: the block debits one per
 * descriptor, sixteen descriptors at a time. See OCTEP_DP_CREDIT_UNIT and OCTEP_DP_OQ_WMARK.
 */
#define	OCTEP_DP_SLIST_ENTRY	16
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

/*
 * The spacing between one receive buffer and the next, which is NOT the same as BSIZE.
 *
 * The vendor's octeon_init_droq allocates each buffer on its own, `buffer_size + 0x40`, and then
 * advances skb->data to the next 64-byte boundary before publishing the pointer - so every address
 * it hands the block is 64-byte aligned. This driver carves its buffers out of one block, and at a
 * stride of BSIZE - 1602, which is not a multiple of 64 - only buffer 0 landed on a boundary and the
 * other 255 did not.
 *
 * Rounding the stride up to 64 costs 62 bytes a buffer, 15.5 KB across the ring, and makes every
 * published address look like the ones the vendor publishes. BSIZE stays at OCTEP_DP_BUF_SIZE:
 * the far side is still told how much it may write, not how far apart they sit.
 */
#define	OCTEP_DP_BUF_ALIGN	64
#define	OCTEP_DP_BUF_STRIDE						\
	((OCTEP_DP_BUF_SIZE + OCTEP_DP_BUF_ALIGN - 1) & ~(OCTEP_DP_BUF_ALIGN - 1))
/*
 * Interrupt coalescing: how many packets, or how many microseconds, before the block raises.
 *
 * These were 8 and 2 for the bring-up, which is an interrupt per eight frames - fine for proving
 * that an interrupt arrives at all, and the wrong shape for a download. At the rate this appliance
 * actually sees, eight frames is tens of microseconds, so the host spends its time entering and
 * leaving the handler rather than draining the ring, and one visit by the handler can take up to
 * DRAIN_ROUNDS x RSIZE packets anyway - so a later interrupt costs nothing and a frequent one costs
 * a context switch.
 *
 * 32 and 50 are a first relaxation, not a tuned value. They are written here together because
 * raising only the packet count leaves the timer firing at the old rate on a quiet link, which is
 * exactly when the low number was wanted.
 */
#define	OCTEP_DP_OQ_INTR_PKT	32
#define	OCTEP_DP_OQ_INTR_TIME	50		/* microseconds */

/*
 * How many rings' worth of packets the handler may take in one visit to a ring before it gives up
 * and leaves the rest to the watchdog's next tick: 16,384 packets at the default RSIZE of 1,024.
 * octep_dp_rx_resume makes the same number of sweeps. See octep_dp_oq_service.
 *
 * It was a count of passes, written when a pass was taken to be a ring's worth. A pass takes what
 * is in the ring, which under a steady stream is a handful: with the flag held for a whole visit
 * and the bound still in passes, 71 to 211 visits a second ended on it under one TCP stream. So it
 * is the packets that are counted now.
 *
 * One pass is not enough, and that was the whole defect: a pass takes at most RSIZE packets, and
 * the block raises its interrupt when R_OUT_CNTS crosses the level from below rather than while
 * it sits above it. A download arriving faster than one pass can drain leaves the count above the
 * level with nothing left to re-cross it, and the receive path stops for good - measured on this
 * appliance with 1,532 packets sitting in host memory, eight MSI-X vectors frozen, and the first
 * hop 100% unreachable until the rings were drained by hand.
 *
 * Sixteen rings' worth was 4,096 packets when this was written and a ring was 256 entries, which
 * is more than a gigabit line delivers between two interrupts. It is a bound rather than a promise
 * at either size.
 */
#define	OCTEP_DP_OQ_DRAIN_ROUNDS	16

/*
 * The receive watchdog's period, in ticks.
 *
 * The drain loop above is the fix; this is the net under it. Nothing else in this driver ever
 * looks at an output ring - there was no periodic receive path at all - so a single lost edge took
 * the appliance's WAN away until the module was reloaded. Twenty times a second costs one register
 * read per armed ring and bounds that failure at 50 ms instead of forever.
 *
 * It is not only a net any more: a visit that has to leave a ring with work it knows about asks
 * this timer for its next tick, whatever the period is.
 */
#define	OCTEP_DP_RXWD_TICKS		(hz / 20)

/*
 * What the watchdog calls a stall: at least this many packets in its first pass over a ring that
 * had not asked for the visit - or twice the interrupt packet level if that is more, because a
 * ring whose interrupt is raised by the count has the level waiting by construction when the
 * watchdog arrives between the interrupt and its handler. A ring carrying fewer than 640 packets
 * a second does not hold thirty-two after a whole period, so a slow ring left on every tick shows
 * only among the rescues.
 *
 * And nobody inside the ring for OCTEP_DP_RXWD_STALL_TICKS before the watchdog came. The block
 * holds packets back when the ring is full and writes them in one burst when buffers are handed
 * back - hundreds inside a millisecond - and a tick that lands between such a burst and its
 * handler finds thirty-two in a ring whose servicer left it a few microseconds ago. That is not a
 * ring that was left. Two stalls were counted in rows where the ring was made to fill, on the
 * build before this was asked; that this is what they were is a reading of them.
 */
#define	OCTEP_DP_RXWD_STALL_PKTS	32
#define	OCTEP_DP_RXWD_STALL_TICKS	2
#define	OCTEP_DP_STALL_LOG		8	/* stalls kept with their ring and sizes */

/*
 * How long a quiesce waits for a servicer already inside the ring to come out, in ten-microsecond
 * steps - so this is one second.
 *
 * It is a bound rather than a deadline. A servicer holds its ring's busy flag across the frames it
 * is handing to the stack, and the stack can take a frame a long way, but it cannot take a second;
 * a reading that reaches this bound means something is wrong and the wait says so rather than
 * hanging a teardown forever.
 */
#define	OCTEP_DP_QUIESCE_SPINS	100000

/*
 * The vendor ships 2048 input and 4096 output descriptors. This driver used 256 of each for its
 * first bring-up - it must be a power of two, which the index arithmetic requires and the hardware
 * does not - and the comment here said to raise it once something had run.
 *
 * Something has run, and then it ran slowly. A download through this appliance tops out around
 * 84 Mbit/s with the coprocessor reporting TX_DROP_QUEUE_FULL: it had frames to hand over and the
 * host's ring had no room. 256 buffers is a ring that holds about a fifth of a millisecond of a
 * gigabit line, so a burst that arrives while the host is between service passes has nowhere to go.
 *
 * 1024 rather than the vendor's 4096, and that is deliberate. Each of the eight rings carves its
 * buffers out of ONE contiguous DMA allocation of DESCS x BUF_STRIDE, so 4096 asks for 6.4 MiB
 * contiguous per ring and 51 MiB in all, where 1024 asks for 1.6 MiB and 13 MiB. The appliance has
 * the memory - 16 GiB, 14.9 free, measured - but a four-fold step that can be measured is worth
 * more than a sixteen-fold one that cannot be attributed, and a contiguous allocation that fails
 * takes the interface with it.
 *
 * NOT a claimed fix. The bottleneck has not been located: credit_capped was examined first and
 * rules nothing out, because it also counts the ordinary steady state of a fully credited ring.
 * (It did while sixteen were returned for a buffer and cut back to the ceiling at every pass. With
 * one for one it counts a pass that could not return all it owed, which should not happen.)
 * This is the vendor's own number moved towards, in a direction the queue-full counter supports,
 * to be measured against the next download rather than asserted.
 */
#define	OCTEP_DP_IQ_DESCS	256
#define	OCTEP_DP_OQ_DESCS	1024

/*
 * How many doorbell units one receive buffer is credited with: one, because one is what the block
 * spends on it.
 *
 * It was sixteen, on this reasoning, which is kept because every sentence of it was measured and
 * the conclusion was still wrong:
 *
 *   Sixteen is what makes the block write at all. Granted the ring in units of one - 256 - it
 *   fetches one batch of sixteen descriptors, writes one packet and stops, which is the
 *   one-packet-per-ring fault this constant was introduced to cure; measured again on 2026-10-02
 *   with 400 frames offered and 8 delivered. Granted 256 x 16 it runs.
 *
 *   But the block does not spend sixteen per packet. It fetches the scatter list sixteen
 *   descriptors at a time and debits the doorbell by one per descriptor - measured under traffic
 *   from the register's two halves, the low half the credit and the high half the block's byte
 *   offset into the list, sixteen bytes a descriptor:
 *
 *	~235 packets on one ring, sixteen returned for each: offset +224 descriptors, credit +3536
 *	~234 packets on one ring, one returned for each:     offset +240 descriptors, credit -6
 *
 *   So returning sixteen per packet added fifteen credits for every packet a ring carried, without
 *   limit: the ring that carried a day's downloads held 1,026,112. With that much the block wrote
 *   over buffers the host had not taken; the frames in them were lost without a drop counter
 *   moving, and a download stopped dead while ping and SSH carried on. The credit is therefore
 *   never allowed above the grant the ring was armed with.
 *
 * What makes the block write is not sixteen to a buffer. It is a doorbell that reads at or above
 * the coprocessor's output watermark - see OCTEP_DP_OQ_WMARK - and 256 x 16 happened to. The
 * ceiling that reasoning put on the credit was sixteen rings' worth, so the block was never short
 * of credit for a buffer the host had not taken: it went on writing round a full ring, over
 * frames nobody had read. One unit for one buffer is the block's own arithmetic, and the vendor's.
 *
 * It was a tunable while it was a guess. It is a constant now and dp.credit_unit only reports it:
 * any unit above one hands the block, at every pass, credit for a ring it has not earned.
 */
/*
 * Room for the front-port label an interface is given at attach - "XGS front port Port1" and the
 * like. The kernel has no size of its own for this: if_allocdescr() takes whatever is asked for,
 * and ifconfig's own buffer is far larger than anything meant here.
 */
#define	OCTEP_DP_DESCR_LEN	32

#define	OCTEP_DP_CREDIT_UNIT	1

/*
 * The coprocessor's output watermark, and how many buffers the block fetches at once.
 *
 * The block stops sending to a ring whose doorbell reads under SDP_OUT_WMARK. That register is
 * the coprocessor's and the host cannot reach it: its kernel driver writes it when the port is
 * configured, 0x100 in the stock source and 0x400 in Sophos's patch to it, and on this appliance
 * the vendor's own register tool reads 0x400 on the coprocessor's console.
 *
 * It was measured before it was found. On 2026-10-08, a ring of 1,024 with the host held away for
 * a tenth of a second under 120,000 datagrams a second, the ceiling on the credit set to each of
 * these in turn, and what the registers read when the host came back:
 *
 *	ceiling	 1,024	doorbell 1,008	count      7	the ring delivers sixteen at a time
 *	ceiling	 1,040	doorbell 1,008	count     24
 *	ceiling	 2,048	doorbell 1,008	count  1,028	more than the ring holds
 *	ceiling	 4,096	doorbell 1,008	count  3,086	three times round it
 *	ceiling	16,384	doorbell 3,472	count 12,910	what this driver granted; still going
 *
 * and on a ring published at 512, the doorbell again at 1,008 under a ceiling of 1,520, and
 * nothing delivered at all under one of 1,008. So the block takes sixteen at a time for as long as
 * the doorbell reads the watermark or more, and what a ring can be given without the block ever
 * fetching a buffer the host has not taken is the ring, the watermark, less one fetch:
 * octep_dp_oq_first_grant.
 *
 * The watermark is a setting, dp.oq_wmark, because it is this firmware's and not the block's: an
 * image that writes the stock 0x100 would be given 768 buffers too many by this default and would
 * write round a full ring again (dp.oq_lapped), and one that writes more would be given a grant
 * under its mark and deliver nothing.
 */
#define	OCTEP_DP_OQ_FETCH	16
#define	OCTEP_DP_OQ_WMARK	1024

/*
 * How long a ring may sit with its next buffer empty and a later one full before the host stops
 * waiting for it, in ticks. The block writes in order and a DMA is visible within microseconds, so
 * a gap that lasts this long is a buffer that will never be written - see octep_dp_oq_resync.
 */
#define	OCTEP_DP_RESYNC_TICKS	2

/*
 * The longest packet the block will be believed to have written, which bounds how many buffers one
 * packet may be walked across - see octep_dp_oq_pass.
 *
 * A packet longer than a buffer is not refused and not cut: the block goes on into the next
 * buffer, and the one after, and counts the whole of it once. The vendor's host works out how
 * many it used from the length word (octeon_droq_get_bufcount, built with BUFPTR_ONLY_MODE): the
 * first buffer holds the two words in front and what is left of it, and every buffer after it is
 * all packet. The coprocessor's statistics messages are the packets that do it here - sized by
 * how many connections moved since the last one, and not by any MTU this host has.
 *
 * So the length word decides how far the read index moves, and a length word that is wrong moves
 * it wrongly. This is the most it may say - forty-one buffers - and a word above it is not taken
 * for a length at all: one buffer is consumed, as before, and it is counted (dp.rx_long).
 */
#define	OCTEP_DP_PKT_MAX	65535

/*
 * Where a ring's MSI-X vector lives, read out of the vendor's own host driver rather than guessed.
 *
 * octeon_tx_enable_msix_interrupts branches on the device id and takes a path of its own for
 * 0xa300 - this part. That path sets a count of sixteen named vectors from a static table, asks for
 * "rings + 16" messages in total, fills entries 0..15 with entry numbers 0..15, and then fills the
 * rest with `srn + i`. The per-ring loop that follows requests one interrupt per ring against
 * `entries[16 + ring]`, with a per-ring context and a per-ring name, and stores the vector in the
 * ring's own structure.
 *
 * So **ring n is MSI-X table entry 16 + n**, with srn 0 - and the device reporting exactly 80
 * messages for 64 rings is that arithmetic seen from the other side: 16 + 64.
 *
 * FreeBSD numbers the resources for pci_alloc_msix() from 1, so the rid is one more again.
 */
#define	OCTEP_DP_MSIX_RING_BASE	16
#define	OCTEP_DP_MSIX_RID(ring)	(OCTEP_DP_MSIX_RING_BASE + (ring) + 1)

/*
 * Receive-only rings armed beside the one this driver transmits on.
 *
 * The vendor's host driver runs eight input and eight output queues, and on its own appliance the
 * coprocessor spread received packets across all eight - ring 0 took about two percent of them. The
 * fast path picks its host queue as crc32c(tuple) % num_sp_txqs, and num_sp_txqs is 8 for this
 * assembly, so seven of every eight frames are aimed at a ring this driver has never published.
 *
 * A sibling is an output queue and nothing else: its own scatter list, buffers and info blocks,
 * programmed and enabled and credited, with no input side at all. Transmit stays on dp_ring.
 *
 * The count is a tunable and defaults to zero, so the driver behaves exactly as before until it is
 * asked for more. dev.octep.<n>.dp.siblings, read before dp.start.
 */
#define	OCTEP_DP_SIBLINGS_MAX	63		/* beside dp_ring, so every ring BAR0 holds */
#define	OCTEP_DP_SIBLINGS_DEF	7		/* the eight the published handshake gives the PF */

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
#define	OCTEP_META_START		0xc0		/* mode 0 only: debug filler, and it asks for IPsec */
/*
 * The vendor's own target application says the head of that 64-byte block is a signature,
 * not a pattern: apps_rxtx.h writes rte_cpu_to_be_64(METADATA_SIGNATURE) at PORT_TAG_SIZE
 * and reads it back the same way, and the same header defines
 *
 *	PORT_TAG_SIZE  2      METADATA_SIZE  64      PRIV_TAG_SIZE  66
 *
 * which is this driver's 66-byte header under the vendor's own names. The walking pattern
 * from 0xc0 came from reading the shipped binary rather than from that source.
 *
 * **The measurement is in, and the pattern lost.** Filling the block that way sets byte 3, which
 * the fast path reads as an egress security-association handle, so every frame was routed to IPsec
 * encryption and dropped before the wire. Mode 3, the vendor's own form, is what this driver sends
 * now, and frames reach a front port and a machine at the far end of the cable. dp.meta still
 * selects, because the other three modes are how that was established.
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

/*
 * What a returned frame actually looks like in a host buffer, read byte for byte out of one that
 * completed the whole loop - host ring, PortF1, fibre, PortF2, fast path, back into host memory:
 *
 *	+0x00	8	the SDP info qword; big-endian, and it carries the length of the rest
 *	+0x08	8	0x8003000000000000, the word worker_ordered prepends
 *	+0x10	2	the pport tag, big-endian - 2 was PortF2, the port it arrived on
 *	+0x12	64	the metadata; its first four bytes are the constant 0xb44399a2
 *	+0x52		the Ethernet header
 *
 * So the prefix is 82 bytes, not the 66 the target's own parser deals in: two more qwords sit in
 * front of that 2+64. The length matched exactly - 0x86 = 134 = 74 + 60, and 74 is the l2_len the
 * fast path sets.
 */
#define	OCTEP_RX_PREFIX_LEN	82

/*
 * The largest MTU a front port can carry, which is a property of the single buffer size this ring
 * is programmed with rather than of the hardware.
 *
 * Transmit stages OCTEP_TOTAL_TAG_LEN bytes of private header in front of the frame, so a buffer
 * holds OCTEP_DP_BUF_SIZE - OCTEP_TOTAL_TAG_LEN of frame. Receive is the tighter of the two: the
 * far side writes its own OCTEP_RX_PREFIX_LEN prefix into a buffer of the same size. Take the
 * smaller, less the 14-byte Ethernet header.
 *
 * This clamp used to read OCTEP_DP_BUF_SIZE - 128, which is 1474 - below the 1500 every one of
 * these interfaces actually runs at. So `ifconfig oxpN mtu 1500` failed with EINVAL, and an
 * interface lowered to 1474 could not be put back without a reboot.
 */
/*
 * ~~(OCTEP_DP_BUF_SIZE - OCTEP_RX_PREFIX_LEN - 14) = 1506~~ Since 2026-10-12 a port's MTU is any
 * value up to 9000, the fast path's own ceiling for a LIF (MAX_MTU in the vendor's lif_table.h):
 * the receive side assembles a frame the coprocessor wrote over several buffers (measured: a
 * 9,014-byte frame arrives in six), the transmit side has a buffer per slot that holds one, and
 * the port's LIF is told the new MTU on every change, since the fast path drops a frame longer
 * than the LIF's MTU before the ring ever sees it (FROM_WIRE_DROP_MTU_EXCEEDED, measured).
 */
#define	OCTEP_DP_IF_MTU_MAX	9000
/* The transmit slot buffers: a jumbo frame, its VLAN tag, and the private tag in front. */
#define	OCTEP_DP_TXBUF_SIZE	(OCTEP_DP_IF_MTU_MAX + ETHER_HDR_LEN + 4 + OCTEP_TOTAL_TAG_LEN)
#define	OCTEP_DP_TXBUF_STRIDE						\
	((OCTEP_DP_TXBUF_SIZE + OCTEP_DP_BUF_ALIGN - 1) & ~(OCTEP_DP_BUF_ALIGN - 1))

/*
 * The transmit rings. The port's eight rings (rppf, the vendor's default, OCTEP_SDP_RINGS_PER_PF)
 * all receive - ring 0 and its seven siblings - and until 2026-10-18 only ring 0 transmitted, under
 * sc->mtx, the lock every command to the coprocessor also takes. The vendor's host transmits on
 * all of its PF's queues (pf_queue_num_hint in its usfp_netdev_mv.c). Rings 1 to 7 now transmit
 * too, each with its own instruction ring, its own slot buffers and its own lock, and a frame
 * takes the ring its flow hashes to, so one connection's frames stay in order. Ring 0 keeps the
 * instruments (dp.xmit, the control messages) under sc->mtx, as before. hw.octep.txq at the
 * loader says how many of the seven: 0 is the old single ring.
 */

#define	OCTEP_RX_TAG_OFF	16
/*
 * The receive metadata, struct usfp_kn_md, and it is not a signature.
 *
 * OCTEP_RX_META_SIG was named for a constant that appeared in every frame at this offset. It is
 * the structure's first word, id_tag, and the structure is the coprocessor's own
 * coprocessor-to-host metadata - sixty-four bytes, which is exactly what is left of an 82-byte
 * prefix after sixteen bytes and a two-byte tag.
 *
 *     +0   uint32_t  id_tag
 *     +4   unused:8 | md_valid:8 | port:16
 *     +8   struct usfp_mflow_ident flow   id:25, rev:6, valid:1
 *     +12  struct usfp_dos_md dos
 *     +16  sa_index:16 | sa_rev:16
 *     +20  spare[11]
 *
 * md_valid reads 1 on this appliance, which is what confirms the offset: a layout guessed wrong
 * would not put a 1 in that byte.
 *
 * THE FLOW IDENT IS THE POINT. A frame the fast path punts carries the identity of the microflow
 * slot it chose, so the host does not compute a hash or invent an index - it is told which entry to
 * program. Measured with the offload gate open, the id differs on every punted frame of one ICMP
 * flow, which is consistent with the fast path allocating a fresh candidate each time because
 * nothing ever programs one. That reading is not yet confirmed; programming one and watching
 * whether the next frame of the same flow reports the same id is what would confirm it.
 */
#define	OCTEP_RX_META_OFF	18
#define	OCTEP_RX_META_SIG	0xb44399a2u	/* usfp_kn_md.id_tag, as this board sets it */
/*
 * The inspector's frames (discovery 4). On the inspector's function's ring the same sixty-four bytes
 * are struct usfp_dp_md from the vendor's metadata_dpi.h, headed by USFP_META_FP2IPS_MAGIC - read on
 * this board 2026-10-23 as csum_status 0x30, md_valid 0x18 (IPS and FWD), the connection's index as
 * the session, l3_offset 14, and in the forwarding block the next hop's two MACs, the egress port and
 * its MTU. The frame behind it is the one that arrived, before any of that is applied.
 */
#define	OCTEP_RX_IPS_SIG	0xc352a8b1u
/*
 * The pkind a frame handed back from the inspector is posted with. The PF's is 40 plus the number of
 * functions (OCTEP_DP_PKIND), so by the same rule the first function's is 40 - a reading of the
 * vendor's arithmetic, settable while it is measured.
 */
#define	OCTEP_DP_IPS_PKIND	40
/*
 * What the driver remembers of each connection the inspector is seeing, so a frame the inspector
 * lets through can be handed back with the metadata its kind arrived with: the frame's own key - the
 * address family, the protocol, both addresses and both ports, which also tells the two directions
 * apart - and the 66 bytes, tag and usfp_dp_md, that went in front of it. A frame passes through
 * Suricata as a copy in netmap's buffers, so nothing tied to the mbuf comes back with it.
 * Direct-mapped: a connection that collides with another loses its entry until its next frame
 * arrives, and a frame handed back without one is dropped and counted.
 */
#define	OCTEP_IPS_KEY_MAX	40
#define	OCTEP_IPS_TAB		16384		/* a power of two */
#define	OCTEP_IPS_PEND		512		/* connections waiting to be set to forward */
/*
 * Suricata's own stream depth, its default: what it reassembles and inspects of each direction of a
 * TCP connection before it stops looking. A connection whose frames the inspector has let through
 * this many bytes of is set to forward.
 */
#define	OCTEP_IPS_DEPTH		(1024 * 1024)
struct octep_ips_ent {
	uint8_t		 klen;
	uint8_t		 key[OCTEP_IPS_KEY_MAX];
	uint8_t		 hdr[2 + 64];		/* OCTEP_TOTAL_TAG_LEN */
};
#define	  OCTEP_RX_MD_VALID_OFF	(OCTEP_RX_META_OFF + 4)
#define	  OCTEP_RX_MD_FLOW_OFF	(OCTEP_RX_META_OFF + 8)
#define	  OCTEP_RX_MD_SA_OFF	(OCTEP_RX_META_OFF + 16)
#define	OCTEP_RX_DATA_OFF	16

/* ---------------------------------------------------------------- NetAgent */

/*
 * NetAgent is the control plane for the front ports - enumeration, link state, MTU, MAC, admin up
 * and down - and it is NOT the datapath. It is also Sophos's own and family-independent, which this
 * project asserted from where its source sits and has now measured on a second family: the header
 * this window publishes on OCTEON is identical, word for word, to the one docs/npuep/netagent.md records
 * for ARMADA.
 *
 * The protocol is described in docs/npuep/netagent.md and the offsets below carry the same names as
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
 * The operations, from docs/npuep/netagent.md where each was confirmed on ARMADA. SET is defined here so it
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

/*
 * The port's own MAC address, and it is real.
 *
 * An early sweep read this attribute as eight zero bytes and it was recorded as "unset" - but that
 * sweep ran with both cages empty and neither port raised. With the ports up it answers **two words
 * that are the six bytes little-endian**, so the first six bytes of the reply are the address as it
 * goes on the wire:
 *
 *	word 0   the first four bytes        word 1   the last two, in the low half
 *
 * Every front port shares its base with the appliance's own management NIC, and **the last byte is
 * the interface id** - the ports whose LIFs are installed against interfaces 10 and 11 answer with
 * 0x0a and 0x0b there. So a front port's address, its interface id and its pport tag are three
 * views of one number, and none of them has to be invented.
 */
#define	OCTEP_NWA_SUB_MAC	0x03
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
 * All-multicast, 0x46.
 *
 * The target's own attribute dispatcher implements STATE, MTU, MAC, AUTONEG, SPEED, DUPLEX,
 * PROMISC, PAUSE, FEATURES, ALLMULTI, MC_ADD, MC_DELETE, UC_ADD, UC_DELETE, RATE_LIMIT and
 * KSETTINGS - so this attribute exists on this coprocessor's firmware rather than being hoped
 * for, and one it does not implement answers with a clean failure rather than going quiet. The
 * sibling ARMADA driver in this tree names the same number, NWA_SUB_ALLMULTI 0x46, beside
 * NWA_SUB_PROMISC 0x45 and NWA_SUB_MCAST 0x4a.
 *
 * Why all-multicast rather than per-address: a LIF carries exactly one address and no mask -
 * LIF_ADD_UPDATE is eighteen bytes, six of them the port's own MAC - and the switch's filter
 * table has twelve entries for a port that must hold its unicast address first. One bit that says
 * "pass multicast" is what this driver can honestly ask for. 0x4a is the finer instrument and is
 * left for whoever needs it.
 */
#define	  OCTEP_NWA_SUB_ALLMULTI	0x46
#define	  OCTEP_NWA_ALLMULTI_OFF	0
#define	  OCTEP_NWA_ALLMULTI_ON		1

/*
 * Autonegotiation, 0x0c, counted out of `enum nwa_msg_port_attr` in Marvell's NetAgent host header
 * rather than guessed: STATE = 0, then OPER_STATE, MTU, MAC, SPEED, ACCEPT_FRAME_TYPE, LEARNING,
 * FLOOD, CAPABILITY, LINK_MODE, TYPE, FEC, AUTONEG, DUPLEX, STATS, before the enum jumps to 64.
 *
 * Counting matters more here than usual, because the neighbour at 0x0b is FEC - the one attribute
 * that stops the far side for good, with nothing short of a coprocessor reboot to bring it back.
 * The target implements this one: soca_process_port_attr_set dispatches AUTONEG to
 * soca_msg_port_autoneg_set, which calls soca_port_autoneg_set(port_id, param.autoneg), in
 * Sophos's own GPL drop of soc_agent.
 *
 * It is what a panel port needs. The eight copper sockets come up with their PHY control register
 * at 0x0000 - autonegotiation off, both speed-select bits clear, half duplex, which is 10 Mbit/s
 * half by definition - and that is the speed a cabled one reported. Asking the far side to turn
 * negotiation on is the supported way to fix it, and it needs neither the switch's MDIO nor a shell
 * on the other processor.
 */
#define	  OCTEP_NWA_SUB_AUTONEG		0x0c
#define	  OCTEP_NWA_AUTONEG_OFF		0
#define	  OCTEP_NWA_AUTONEG_ON		1

/*
 * The reply to a discover measured 2020 bytes here, so 64 words truncated it badly. 512 words is that
 * with room; it is not a protocol limit - the target advertises a maximum request near 32 KB.
 */
#define	OCTEP_NWA_MAX_WORDS	512
#define	OCTEP_NWA_IDLE_TRIES	100	/* x 10 ms, waiting for the previous transaction */
/*
 * How long octep_nwa_ack_settle() spins before it sleeps: 250 reads two microseconds apart, half a
 * millisecond with the mutex held. A target that lets go of the window as soon as it notices the
 * acknowledge shows inside this; one that needs longer is measured by the sleeping wait, at the
 * tick's resolution, and counted as slow. See issue #227.
 */
#define	OCTEP_NWA_ACK_SPIN	250
#define	OCTEP_NWA_ACK_SPIN_US	2
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

#define	OCTEP_DP_TXQ_MAX	7
struct octep_dp_txq {
	struct mtx		 mtx;
	struct octep_dma	 iq;
	struct octep_dma	 bufs;
	uint32_t		 ring;
	uint32_t		 prod;
	uint64_t		 posted;
	uint64_t		 full;
	int			 up;
} __aligned(CACHE_LINE_SIZE);

/*
 * A front port, presented to the stack.
 *
 * One of these per pport tag, which is the same tag the LIF was installed against: the return path
 * puts the tag in front of every frame it hands back, so an arriving frame names its own interface
 * and the receive path does not have to guess. See OCTEP_RX_TAG_OFF.
 */
/*
 * Thirteen: the appliance's twelve front ports, and the switch uplink.
 *
 * The twelve are the board file's twelve lifports and every one can now be reached - eight copper
 * panel ports and the two 1G cages behind the 88E6193X, each addressed by the tag its DSA header
 * carries, and the two 10G cages direct to BGX2 at tags 0x0001 and 0x0002. The thirteenth is the
 * uplink itself, which is not a front port but is worth being able to present.
 *
 * It was 4 while only three ports had ever been bound, and four was a limit nobody had met; the
 * moment the panel ports opened it was the only thing in the way of using them.
 *
 * It is a ceiling, not a count. Interfaces are created one at a time by dp.if_add and this driver
 * creates none on its own.
 *
 * 32 since 2026-10-16, when a 4x10G FleXi module (AMDA0207) made the XGS 3300's coprocessor report
 * sixteen ports and thirteen was in the way of the four it added. The largest the vendor's board
 * files describe on one coprocessor is twenty front ports plus a module's eight; NetAgent itself
 * reserves room for a hundred.
 */
#define	OCTEP_DP_IF_MAX		32

/* Room for a front port's name given by dp.if_label: "A1", "Port10", "F4" and the like. */
#define	OCTEP_DP_LABEL_LEN	16

/*
 * "Use the pport tag as the NetAgent port number too."
 *
 * They are different things and they only look alike. The tag is a handle this driver chooses and
 * PPORT_UPDATE binds to an interface id; the NetAgent port is the coprocessor's own index into its
 * three SerDes ports. For the two direct SFP+ cages the numbers happen to coincide - tag 1 is port
 * 1, tag 2 is port 2 - and the difference stayed invisible until a third interface was made for the
 * switch uplink, which is NetAgent port 0 and took tag 3. Asking port 3 for its address gets
 * nothing, and the interface silently fell back to a made-up one.
 */
#define	OCTEP_DP_IF_PORT_AUTO	0xffffffffu

/*
 * A punted frame's five-tuple, and what pf says about it.
 *
 * Kept in this driver's own terms rather than in pf's, so that nothing outside octep_pf.c has to
 * include net/pfvar.h - and so a kernel without pf still compiles and runs every other part of
 * this driver. Addresses are in network order, exactly as they sit in the frame; ports too, which
 * is also the order pf keeps them in, so neither is byte-swapped on the way to a lookup.
 */
struct octep_pf_tuple {
	uint32_t	sip;
	uint32_t	dip;
	uint16_t	sport;
	uint16_t	dport;
	uint8_t		af;		/* AF_INET, or AF_INET6 since 2026-10-18 */
	uint8_t		proto;
	/*
	 * An IPv6 connection's two addresses, network order, zero for IPv4. sip and dip then hold
	 * the four words of each folded together - what the hashes and the quick comparisons use -
	 * and octep_tuple_eq compares these in full, so a fold that happens to agree is never taken
	 * for the same connection.
	 */
	uint32_t	sip6[4];
	uint32_t	dip6[4];
};

/* Two tuples name the same direction of the same connection: every field, the IPv6 words too. */
static __inline int
octep_tuple_eq(const struct octep_pf_tuple *a, const struct octep_pf_tuple *b)
{

	if (a->sip != b->sip || a->dip != b->dip || a->sport != b->sport ||
	    a->dport != b->dport || a->proto != b->proto || a->af != b->af)
		return (0);
	return (a->af != AF_INET6 ||
	    (memcmp(a->sip6, b->sip6, sizeof(a->sip6)) == 0 &&
	    memcmp(a->dip6, b->dip6, sizeof(a->dip6)) == 0));
}

/* The fold that stands for an IPv6 address in the 32-bit fields. */
static __inline uint32_t
octep_fold6(const uint32_t w[4])
{

	return (w[0] ^ w[1] ^ w[2] ^ w[3]);
}

struct octep_pf_state {
	int	order;			/* which of the four key arrangements matched */
	uint8_t	direction;
	uint8_t	timeout;
	uint8_t	src_state;
	uint8_t	dst_state;
	uint16_t state_flags;
	char	ifname[16];		/* IFNAMSIZ, named here so pfvar.h is not needed */
	/*
	 * Both of pf's keys, which together ARE the translation.
	 *
	 * pf keeps a wire key and a stack key for every state: one side as the packet appears on
	 * the wire, the other as the host sees it. For a translated connection they differ, and
	 * the difference is exactly what struct usfp_nat_info wants - so a flow's NAT does not have
	 * to be computed or guessed, it has to be copied out of the state that already holds it.
	 * Identical keys mean the connection is not translated.
	 */
	uint32_t wire_addr[2];
	uint32_t stack_addr[2];
	uint16_t wire_port[2];
	uint16_t stack_port[2];
	/*
	 * The same translation, in the form struct usfp_nat_info wants.
	 *
	 * This is the one piece of knowledge that cost the most to find, so it lives here rather
	 * than in whatever script is asking. The NAT block is filled from the CONNECTION's point of
	 * view, not from the frame in hand: ipv4_orig_src is the machine that OPENED the
	 * connection, in its untranslated form, whichever direction the frame being looked at
	 * happens to be going. Filling it from the frame puts the translated address in both
	 * address fields and the translated port in both port fields - measured, by reading a
	 * forwarded frame.
	 *
	 * And the flag describes the connection, not the direction: a connection translated on its
	 * way out is do_snat, and the fast path applies the inverse to the reply. Setting do_dnat
	 * because the frame in hand needs its destination changed leaves the destination alone.
	 *
	 * nat_valid is 0 when the two keys agree, which is a connection that is not translated.
	 */
	int	 nat_valid;		/* at least one end is translated */
	int	 nat_snat;		/* the opener's address is rewritten: do_snat */
	int	 nat_dnat;		/* the responder's address is rewritten: do_dnat */
	int	 original;		/* the frame looked up goes from the opener to the responder */
	int	 lookup_dir;		/* PF_IN or PF_OUT: which of pf's two lists held the state */
	int	 keys_shared;		/* pf kept one key for both sides: nothing is translated */
	/*
	 * Filled whether or not anything is translated: orig_* is each end as the host sees it,
	 * nat_* each end as the wire sees it, and an untranslated end is the same in both. The
	 * opener is src, the responder dst, whichever way the frame looked up was going.
	 */
	uint32_t orig_src, orig_dst;	/* network order, as the sysctls take them */
	uint32_t nat_src, nat_dst;
	uint16_t orig_sport, orig_dport;
	uint16_t nat_sport, nat_dport;
	/*
	 * IPv6: the keys' addresses in full, and the two ends from them. No IPv6 connection is
	 * made with a translation - the fast path's NAT block has IPv4 addresses only - so for one
	 * the fields above hold folds, and orig is nat.
	 */
	uint8_t	 af;
	uint32_t wire_addr6[2][4];
	uint32_t stack_addr6[2][4];
	uint32_t orig_src6[4], orig_dst6[4];
	/*
	 * A TCP state's two peers as pf tracks them, copied under the state's lock with the keys:
	 * [0] is pf's src, which is the opener the fields above are named from, [1] the responder.
	 * The window the fast path checks for an offloaded connection is filled from them (issue
	 * #679). rewrites is set when pf changes something in every packet that the fast path
	 * cannot - a sequence number modulator (modulate state, synproxy) or a timestamp
	 * modulator (scrub reassemble tcp) - and such a connection is never offloaded.
	 */
	uint32_t seqlo[2], seqhi[2];
	uint16_t max_win[2];
	uint8_t	 wscale[2];
	int	 rewrites;
};

/*
 * What pf's states for a connection say about its window, all of them together:
 * octep_pf_tcp_scan's answer. A connection a forwarded TCP connection crosses two states, one
 * per interface, and each can carry its own rule's choices.
 */
struct octep_pf_tcp {
	int	states;		/* how many states were found */
	int	rewrites;	/* one of them modulates sequence numbers or timestamps */
	int	rule_sloppy;	/* one of them was made by a rule with keep state (sloppy) */
	int	sloppy;		/* one of them is sloppy for another reason: its numbers stopped */
};

/*
 * A resolved next hop: where a flow's frames go, as the host's routing table answers it.
 */
struct octep_nhop {
	uint8_t	 dmac[6];
	uint8_t	 smac[6];
	uint32_t iface;			/* the front port's LIF interface id */
	uint16_t mtu;
	uint16_t vlan;			/* the 802.1Q VLAN its frames leave with, 0 untagged */
	int	 ifname_unit;		/* which dp_if it resolved to, for reporting */
	/*
	 * Set when the egress port did not come from the route but from the flow's own record of
	 * where its frames arrive. The route named an interface this driver does not own - a bridge
	 * this appliance's front ports are members of - and the port came from the hint instead.
	 * Kept so that every instrument that prints a next hop says which of the two it is.
	 */
	int	 from_flow;
};

/*
 * One accelerated CONNECTION, and the table that holds them.
 *
 * The coprocessor keeps one connection entry per TCP connection and two microflows pointing at it,
 * one per direction - FLOW_CREATE_FP carries exactly that, and the vendor's host writes the same
 * connection index into both microflows. This driver used to make one entry per DIRECTION, each
 * with its own connection entry, so the far side saw two half-connections whose per-direction TCP
 * state never met; measured as CONN_RECLAIM_PENDING rising by a tenth of the forwarded count on a
 * working offload. Now an entry is a connection: one index, one revision, two ingress tuples, and a
 * microflow identity per direction that may be filled at creation or attached later.
 *
 * The connection index is this table's position. Its revision is per index and climbs on every
 * allocation of that index, because every command the far side accepts after creation checks it,
 * and a constant could not tell a reused index from the one before. Next hops are no longer this
 * index: they live in their own small table, keyed by egress port and neighbour and shared between
 * every connection that leaves the same way.
 *
 * The tuples are kept so the entry can be re-checked against pf without the frame that created it.
 * That is the whole of invalidation: when pf no longer has a state, both microflows must be taken
 * out of MF_ACTIVE and the connection reclaimed, or the coprocessor goes on forwarding a connection
 * the firewall has stopped tracking - the one failure here that is worse than no offload at all.
 *
 * The HOST's bounds are the board's own: the platform block of the XGS 3300 says 2,000,000
 * connections, 65,536 next hops and 4,001,450 microflows, and this table is that size on a host
 * whose memory holds it in a 16th (OCTEP_FLOW_RAM_SHARE). An entry with its share of the chains,
 * the free stack and the live list is 284 bytes, so sixteen gigabytes hold the 2,000,000 in about
 * 570 MB, eight about 1.8 million, four about 900,000; a 32nd was tried first and gave the XGS 3300
 * only 1,878,637 of them. hw.octep.flow_max at the loader sets it either way, never past
 * the ceiling; a board that declares less lowers dp_conn_max to it when the block is read, so the
 * far side is never asked for an index it has not. Only the XGS 3300 has been measured.
 *
 * 1,024 connections and 256 next hops were the bounds until 2026-10-17, when 1,024 connections
 * through the box at once filled the table: auto_full climbed, the rest stayed with the host, and
 * the host's CPU went from 3% to 45% while the rate fell from 9.4 to 4.1 Gbit/s. The table was an
 * array in the softc and a connection was found by reading every entry of it. It is now allocated
 * at attach; a connection is found through a chain of a hash table as large as the table itself;
 * an index is handed out from a stack of those not in use; and the passes the poll makes every
 * second walk a list of the connections that exist, not the two million places they could be in.
 * What the size costs is memory - about 570 MB of the XGS 3300's sixteen gigabytes - and
 * not time.
 */
#define	OCTEP_FLOW_DEFAULT	2000000
#define	OCTEP_FLOW_MIN		1024
#define	OCTEP_FLOW_CEIL		2000000
#define	OCTEP_FLOW_RAM_SHARE	16
#define	OCTEP_NHOP_MAX		65536
/*
 * Polls in which an identity this table has programmed was seen punted again, before the
 * connection entry is read back to learn whether the fast path has stopped using it. The count is
 * per POLL and not per frame, because the candidate ring holds one entry per tuple however many
 * frames arrive: a connection the fast path has handed back shows up once a second, so three
 * consecutive polls is three seconds. A healthy offloaded connection showed none in eight.
 */
#define	OCTEP_FLOW_PUNT_PROBE	3
/*
 * How often a connection in hardware is asked about, in seconds, so that pf's state for it can be
 * kept from running out; and how many reads of the far side one poll may spend on it. The period
 * has to be inside pf's shortest timer for a state that is in use. That is not udp.single's thirty
 * seconds, as this said at first: an ICMP echo state is on icmp.error, ten. A restamp is written at
 * the first asking after a frame, so a state is sure to survive a silence of its timer less one
 * period and the poll's own second; five leaves a ten-second state four. A full table at this
 * budget is gone round in four.
 */
#define	OCTEP_KA_PERIOD		5
#define	OCTEP_KA_READS_PER_POLL	512
/* How long a dropped connection is kept with pf's state gone and no frame seen: three visits. */
#define	OCTEP_DROP_HOLD		(3 * OCTEP_KA_PERIOD)
/*
 * How old, in milliseconds, a UDP state has to be before a connection is made on it with only one
 * of its peers seen more than once. Until then the opener's second datagram is waited for, which
 * puts the state on udp.multiple; a flow that has gone this long without one and is still sending
 * is a stream in one direction, and pf keeps that on udp.single for as long as it lasts. Long
 * enough for a question and its answer to have ended and for an opener that talks to have spoken
 * again. Not longer, because the connection is made with the identity the opener's one datagram
 * was punted under, and neither the fast path nor the candidate ring keeps that for ever.
 */
#define	OCTEP_PF_UDP_ONEWAY_MS	3000
/*
 * The fast path's own limit for packets in a row with the same acknowledgement, end and window
 * (USFP_MAX_RETRANS). A connection entry in RECLAIM_PENDING whose retrans counter stands there was
 * given back by that rule and by nothing else: the other reasons leave the counter where it was.
 */
#define	OCTEP_CONN_MAX_RETRANS	10
/*
 * The task the receive path kicks: not more often than this, and not the same connection asked
 * about more often than that. Both in ticks. A run is a handful of commands with the transmit
 * path's lock held, so the first bounds what a storm of give-backs can cost everything else.
 */
#define	OCTEP_FAST_HOLD		(hz / 200 > 0 ? hz / 200 : 1)
#define	OCTEP_FAST_PROBE_GAP	(hz / 50 > 0 ? hz / 50 : 1)
/*
 * Making a connection between polls. The receive path asks for it when a tuple has had 1 << this
 * many frames written to its place in the candidate table since it took the place - frames of
 * the slot's other tuples do not break the count - and again each time that count doubles, so a
 * flow that cannot be made is asked about a dozen times in its first hundred thousand frames and
 * not a hundred thousand. What the doubling forgets when another tuple takes the place, one bit a
 * slot remembers until the next poll: dp_fast_make_held. A run makes at most the
 * second figure, and all the runs of one second the third: a decision, where the poll's eight a
 * second was an accident of its budget. A command is answered in twenty-five microseconds and a
 * connection is three, so sixty-four is five milliseconds of the transmit path's lock in a second.
 *
 * Sixty-four a second was that figure until 2026-10-17, and it was what held 1,024 connections
 * through the box at a few hundred in hardware: the table had room, the making did not keep up.
 * The defaults are now taken from the command itself, measured on the XGS 3300 - 20.7 us on
 * average over 358,598 commands, so 62 us a connection: half of each second of the lock is
 * 8,000 connections, and one run makes sixteen, one millisecond of the lock at a time. Both are
 * dp.fast_make_per_sec and dp.fast_make_per_run, so what they cost the transmit path is measured
 * rather than assumed.
 */
#define	OCTEP_FAST_MAKE_LOG2	4
#define	OCTEP_FAST_MAKE_PER_RUN	16
#define	OCTEP_FAST_MAKE_PER_SEC	8000
/*
 * The candidate table: sixty-four slots, chosen by the tuple's hash, and room for four tuples in
 * each. A slot held one and whatever else hashed there took its place, so the two directions of a
 * connection whose tuples share a slot - one connection in sixty-four - never stood in the table
 * together and the connection was never made. One side entry for the missing direction answered
 * that for one connection at a time, and was kept from the rest by whatever had armed it and
 * could not use it: a connection whose other direction comes in by a port this driver does not
 * own (measured), one whose tuple counted in a bucket of the hot table with any held connection
 * (measured), an idle one that armed it again at every exchange (read, not run). With room for
 * both directions in the slot itself there is nothing to arm, and nothing to take turns at while
 * no more than four tuples are busy in the slot. With more, the quietest take turns at a place,
 * and dp.cand_evicted counts it.
 *
 * The last figure is how often octep_flow_cand_ident and dp.cand_table try a slot's busy word
 * before they call it a miss. The drain tries once a place and the receive path once a frame.
 *
 * Sixty-four slots until 2026-10-17: 256 places for every connection the whole box was waiting
 * to make, and with 1,024 connections arriving dp.cand_lost passed fourteen million. Now as many
 * places as a second of making needs - 4,096 slots of four, both tuples of 8,192 connections,
 * the 8,000 a second of OCTEP_FAST_MAKE_PER_SEC - one megabyte of the softc.
 */
#define	OCTEP_FLOW_CAND_SLOTS	4096
#define	OCTEP_FLOW_CAND_WAYS	4
#define	OCTEP_FLOW_CAND_MAX	(OCTEP_FLOW_CAND_SLOTS * OCTEP_FLOW_CAND_WAYS)
#define	OCTEP_CAND_TRIES	16
/* A revived connection is left alone this long, doubling while it keeps coming back, to this. */
#define	OCTEP_REVIVE_WAIT_MIN	(hz / 100 > 0 ? hz / 100 : 1)
#define	OCTEP_REVIVE_WAIT_MAX	(hz / 2)

/*
 * One punted frame the host might turn into a flow, and the table of them.
 *
 * WHY THIS EXISTS. The receive path used to keep exactly one of these, overwritten by every frame
 * that followed it, and the link poll acted on whatever was there once a second. So the automatic
 * trigger could learn at most ONE FLOW PER SECOND. Measured on a 186 Mbit/s download: 57,212 frames
 * punted in five seconds and three flows were accelerated - about one frame in eleven thousand. The
 * flow table was never the limit; it holds 62 and held 3.
 *
 * WHY A HASH AND NOT A QUEUE. A queue of every punted frame would be 11,000 entries a second of
 * which all but a handful are the same few connections, and it needs a head and a tail shared by
 * writers who do not share a lock. Indexing by the tuple gives the deduplication for nothing - the
 * same flow always lands in the same slot, so a download occupies one entry however many frames it
 * sends - and it needs no shared state between writers at all.
 *
 * WHO WRITES IT. The receive path, which holds no softc lock and runs on several rings at once under
 * MSI-X. So two writers can land on one slot, and a slot half-written by one and finished by another
 * would hand the poll a tuple assembled from two connections - a flow programmed from one
 * connection's addresses and another's ports forwards somebody else's traffic, which is the exact
 * hazard the single slot's sequence pairing was added to stop.
 *
 * A seqlock does not fix that. A seqlock makes a reader notice a writer; it does nothing about two
 * writers, who would both increment the generation and leave it even with the fields mixed. So each
 * slot carries a one-word trylock instead, taken by every writer AND by the reader. A writer that
 * cannot take it gives up and counts it - the next frame of that flow will try again, and on a link
 * busy enough for two rings to collide there will be another frame immediately.
 *
 * AND THAT IS ONLY HALF OF IT, which a review caught before any of this ran. The trylock guards
 * where a candidate is WRITTEN. It says nothing about where it is read FROM, and the first version
 * of this code assembled every candidate out of the one set of capture fields in the softc that all
 * eight rings overwrite without a lock - so an entry could hold one connection's addresses with
 * another's flow slot, which is the hazard above arriving by the back door. Everything a candidate
 * carries is now passed in by value from the ring servicer's own stack: see octep_flow_cand_push,
 * which reads nothing out of the softc, and octep_dp_oq_pass, which decodes the flow identity
 * from its own buffer rather than from the shared copy of it.
 *
 * `stamp` is bumped by every writer and `seen` is the reader's record of what it last acted on; both
 * are only ever touched inside the trylock, so neither needs to be atomic.
 *
 * The counters beside them are incremented without synchronisation, as every other statistic on this
 * path is. A lost increment on a count of offered frames is not worth an atomic in the receive path.
 */
/*
 * One TCP segment as the host saw it go by: the latest punted frame of a candidate's tuple. A
 * connection's window, when pf's numbers for it have stopped, is made from its two directions'
 * latest segments (octep_tcp_from_segs; issue #679). win is the header's, unscaled; len the
 * payload's; flags the header's flag byte.
 */
struct octep_tcp_seg {
	uint32_t	seq;
	uint32_t	ack;
	uint32_t	len;
	uint16_t	win;
	uint8_t		flags;
	uint8_t		valid;		/* 1 for a TCP segment whose header was read whole */
};

struct octep_flow_cand {
	uint32_t		 stamp;		/* bumped by every writer */
	uint32_t		 seen;		/* the stamp the reader last acted on */
	struct octep_pf_tuple	 tuple;
	struct octep_tcp_seg	 seg;		/* the frame's TCP header, for the window */
	uint32_t		 slot;
	uint32_t		 rev;
	int			 in_dif;
	uint16_t		 in_tag;
	/*
	 * The frame's two MACs, its destination and then its source, as on the wire: whether it was
	 * addressed to the host or is one a bridge carries past it (octep_dp_frame_bridged, discovery
	 * 9), and the two ends of a bridged connection's next hop. Zero for a frame that came out of a
	 * tunnel.
	 */
	uint8_t			 mac[12];
	/* the frame's 802.1Q VLAN, 0 for an untagged frame (discovery 10) */
	uint16_t		 vlan;
	/*
	 * The association that decrypted the frame on its way in, as the frame's own metadata named
	 * it - handle and revision - or 0 for a frame that arrived as it is. Such a candidate comes
	 * from octep_ipsec_rx, after the frame has been terminated, with the INNER packet's tuple.
	 */
	uint16_t		 sa;
	uint16_t		 sa_rev;
	/*
	 * A frame of this candidate's connection carried FIN or RST since the reader last
	 * took the candidate. Kept across the frames that follow it, because the frame that closes
	 * a connection is one and the acknowledgements after it are many.
	 */
	uint8_t			 closing;
	/*
	 * How many frames this place has taken from the tuple it holds since that tuple took it
	 * - 1 again the moment another tuple takes the place - and whether the receive path has
	 * asked, on the strength of that count, for the tuple to be made a connection between
	 * polls. The count is the tuple's own, which stamp minus seen is not: every writer of the
	 * place bumps the stamp, whatever its tuple.
	 */
	uint8_t			 due;
	uint32_t		 run;
	int			 when;		/* ticks at the last write: the oldest makes room */
};

/*
 * A slot: the tuples whose hash chose it, as many as there are places, and the one word every
 * writer and reader of any of them takes. One word and not one a place, because which place a
 * frame is written to is decided by looking at all of them - the place that holds its tuple, an
 * empty one, the one written longest ago - and two writers deciding that at once with a word each
 * could put one tuple in two places, or have one overwrite the other the moment it was written.
 * Every holder has the word inside a critical section: see octep_flow_cand_word_try.
 *
 * Each slot starts a cache line of its own, as each entry did: eight rings write these from eight
 * cores.
 */
struct octep_flow_cand_slot {
	volatile uint32_t	 busy;		/* 1 while a writer or a reader is inside */
	struct octep_flow_cand	 way[OCTEP_FLOW_CAND_WAYS];
} __aligned(CACHE_LINE_SIZE);

/* Where a tuple counts in dp_conn_hot: the candidate table's own mix, sixteen bits of it. */
#define	OCTEP_CONN_HOT_MAX	65536
static __inline uint32_t
octep_conn_hash32(const struct octep_pf_tuple *t)
{
	uint32_t h;

	h = t->sip ^ ((t->dip << 13) | (t->dip >> 19));
	h ^= ((uint32_t)t->sport << 16) | (uint32_t)t->dport;
	h ^= (uint32_t)t->proto;
	return (h * 0x9e3779b1u);
}

static __inline uint32_t
octep_conn_hot_slot(const struct octep_pf_tuple *t)
{

	return (octep_conn_hash32(t) >> 16);
}

/*
 * One direction of a connection, as the fast path identifies it: the microflow slot the fast path
 * chose for that direction's frames and its six-bit revision, both read from a punted frame's
 * metadata, never computed here. Programmed when the far side has been told to forward it.
 *
 * in_dif is which front port this direction's frames arrive on, taken from the pport tag of the
 * punted frame. It is kept for the OTHER direction's sake. The route to a machine on this
 * appliance's LAN resolves to bridge0, which eleven front ports are members of and which this
 * driver does not own - so the route says the frame must leave by L2 towards that address and does
 * not say by which port. The bridge knows and cannot be asked: bridge_rtlookup, bridge_lookup_member
 * and bridge_lookup_member_if are all static in if_bridge.c, and the only way the address cache
 * leaves the kernel is a copyout to userspace from the ioctl. A punted frame from that machine
 * arrived on a port; that is direct evidence of which port it is on, for this connection, for as
 * long as it lives. What it cannot see is a machine that moves to a different front port in the
 * middle of a connection: this direction keeps using the port it was last punted from until the
 * state expires and the sweep takes the connection out. The port is checked for link before it is
 * used, which covers a moved cable and not a moved machine.
 *
 * punts counts frames of this identity the fast path handed to the host while it was programmed.
 * A few are normal. OCTEP_FLOW_PUNT_PROBE of them make the drain read the connection entry back.
 */
#define	OCTEP_MF_NONE		0
#define	OCTEP_MF_PROGRAMMED	1
struct octep_conn_mf {
	uint8_t			 state;
	uint32_t		 slot;		/* the microflow the fast path chose, 25 bits */
	uint32_t		 rev;		/* its revision, 6 bits */
	uint32_t		 nhop;		/* index into dp_nhop; 0 is none */
	int			 in_dif;	/* index into dp_if, -1 when unknown */
	uint16_t		 in_tag;	/* the pport tag it was punted with */
	uint32_t		 punts;
	/*
	 * The microflow entry's own timestamp when the keep-alive last read it, and whether it
	 * has: the far side stamps an entry at every frame that hits it, so a stamp that has moved
	 * is traffic the host did not see.
	 */
	uint32_t		 ka_stamp;
	uint8_t			 ka_seen;
	/*
	 * A tunnelled connection's two directions are not alike. One arrives in the clear and
	 * leaves encrypted: its microflow names the outbound association, sa and sa_rev, as the far
	 * side stores them - the handle is the index plus one. The other arrives as ESP, is
	 * decrypted before the flow table is consulted, and is forwarded by a microflow that names
	 * NO association: a non-zero sa_index there would encrypt it again. dsa is the inbound
	 * association its frames were decrypted by when the direction was learned. It is never
	 * sent; it is kept so the connection goes when that association does - and it has to,
	 * because nothing on the far side ties that microflow to an association at all.
	 */
	uint16_t		 sa, sa_rev;
	uint16_t		 dsa, dsa_rev;
	/* 0x100 | the DSCP pf writes into this direction's frames, or 0: octep_pf_tos_scan */
	uint16_t		 dscp;
	/*
	 * This direction's frames arrive tagged, or its next hop names a VLAN (discovery 10): the
	 * microflow's bridge control then has the fast path write the next hop's VLAN - 0 takes a
	 * tag off - where a routed one otherwise leaves the frame's VLAN header as it came.
	 */
	uint8_t			 vlan_rw;
};

/*
 * A connection entry's TCP block, struct usfp_tcp_info: sixty bytes at offset 20 of the entry, so
 * at 24 of a FLOW_CREATE_FP request. The vendor's conn_table.h
 * gives the members and conn_common.c what the fast path does with them, which is Linux
 * conntrack's window check: for each direction the highest sequence sent (td_end), the highest it
 * may send (td_maxend, the other side's acknowledgement plus its window), the largest window it
 * advertised, scaled (td_maxwin), and the highest acknowledgement it sent (td_maxack). pf keeps
 * the same numbers under other names - seqlo, seqhi, max_win unscaled - which is why the one can
 * be filled from the other.
 *
 * The first 48 bytes are struct usfp_tcp_seq, and CONN_RECLAIM_FP answers with exactly them: the
 * fast path's numbers when it lets a connection go. Then the FIN tracking, and a word of flags
 * with the two window scales in its top bytes.
 *
 * The two liberal bits are the fast path's own way into a check from numbers that may be behind:
 * while either is set, a segment outside the window but within a gigabyte of it is let through
 * and moves the numbers, and each direction's bit clears at its first segment inside the window -
 * from then on the check is strict. A connection is sent with both set, because pf's numbers are
 * read a moment before the fast path takes over and the frames between are not in them. Measured
 * on both families, 2026-10-22: segments 64 MiB past the window, inside that span, sent eight
 * seconds into a stream, were dropped and counted (FROM_WIRE_DROP_TCP_SEQ_CHK_INVALID) - the check
 * was strict by then - and an ordinary stream had none dropped.
 */
#define	OCTEP_CONN_OFF_TCP		24
#define	OCTEP_CONN_TCP_LEN		60
#define	OCTEP_TCP_SEQ_LEN		48
#define	OCTEP_TCP_SEEN(d)		((d) * 16)	/* td_end +0, td_maxend +4, td_maxwin +8, td_maxack +12 */
#define	OCTEP_TCP_OFF_RETRANS		44
#define	OCTEP_TCP_OFF_FIN		45	/* last_dir bit 0, fin_orig_dir bit 4, fin_state bits 5-7 */
#define	  OCTEP_TCP_FIN_STATE(b)	(((b) >> 5) & 0x7)
#define	OCTEP_TCP_OFF_FLAGS		56
#define	  OCTEP_TCP_SEQ_CHK_ON		0x00000001u
#define	  OCTEP_TCP_LIBERAL_ORIG	0x00000002u
#define	  OCTEP_TCP_LIBERAL_REPLY	0x00000004u
#define	  OCTEP_TCP_SCALE_REPLY_SHIFT	16
#define	  OCTEP_TCP_SCALE_ORIG_SHIFT	24

/*
 * A take-back whose window goes to pf once the softc lock is dropped: pf's states are never
 * touched with it held (octep_dp_handback_run). The connection's two tuples, the fast path's
 * numbers as CONN_RECLAIM_FP answered them, and the two window scales the connection was sent
 * with, opener's first.
 */
#define	OCTEP_HB_MAX			32
struct octep_handback {
	uint32_t		 idx;		/* the connection's index, for dp.tcp_last */
	struct octep_pf_tuple	 tuple[2];
	uint8_t			 seq[OCTEP_TCP_SEQ_LEN];
	uint8_t			 scale[2];
};

/* The translation, in the connection's orientation, kept so a re-create needs no new pf read. */
struct octep_conn_nat {
	int			 snat;
	int			 dnat;
	uint32_t		 orig_src, orig_dst, nat_src, nat_dst;
	uint16_t		 orig_sport, orig_dport, nat_sport, nat_dport;
};

/*
 * What the owner keeps with the host. Two lists, both asked by octep_dp_flow_make before anything
 * is reserved, so a connection either names is forwarded by the host like any other:
 *
 *   held      connections handed back one at a time from the Flows page (dp.flow_release): both
 *             directions' tuples, kept until pf lets the state go, so the connection is not made
 *             again by its next punted frame;
 *   excluded  addresses, networks and ports never offloaded (dp.flow_exclude, from XGS: Settings),
 *             written as the canonical tokens scripts/xgs/flow_exclude.php makes of the owner's list
 *             - the kernel parses hex and decimal only, the addresses are validated in userland.
 */
#define	OCTEP_FLOW_HELD_MAX	256
#define	OCTEP_FLOW_EXCL_MAX	64
#define	OCTEP_FLOW_EXCL_TEXT	3072	/* 64 of the longest token, "a6:" 32 digits "/128" and a space */

struct octep_flow_held {
	int			 used;
	int			 pf_dir;
	uint8_t			 probe_dir;
	struct octep_pf_tuple	 tuple[2];
};

struct octep_flow_excl {
	uint8_t			 kind;		/* 4 an IPv4 network, 6 an IPv6 one, 'p' a port */
	uint8_t			 plen;		/* the network's prefix length */
	uint16_t		 port;		/* host order */
	uint32_t		 a4;		/* host order */
	uint32_t		 a6[4];		/* network order, as struct octep_pf_tuple holds them */
};

/*
 * The Captive Portal's clients as the fast path's live users (dp.live_users; discovery 5, made a
 * product 2026-10-24): each logged-in address with the user number scripts/xgs/cp_users.py gave its
 * session. A connection with either end listed is made with that user and keeps it when revived; a
 * user is added to the fast path's table when it first appears here and deleted when it goes, and
 * the fast path then gives back every connection carrying it at once - so a logout takes the
 * client's connections out of hardware with one command, and the driver does not make them again.
 *
 * What the coprocessor carried for a user's connections is counted here too, from its statistics
 * messages, so the Captive Portal's accounting can be told what ipfw never saw (cp_overlay.py): the
 * table is kept sorted by user, and the counts sit on the first entry of each user and pass to the
 * next table while the user stays. Index 0 is from the client, 1 to it.
 */
#define	OCTEP_LIVE_USERS_MAX	1024
#define	OCTEP_LIVE_USERS_TEXT	(OCTEP_LIVE_USERS_MAX * 44)	/* "a6:", 32 digits, "=65535", a space */

struct octep_live_user {
	uint8_t			 kind;		/* 4 or 6 */
	uint16_t		 luid;		/* 1 to 65535 */
	uint32_t		 a4;		/* host order */
	uint32_t		 a6[4];		/* network order, as struct octep_pf_tuple holds them */
	uint64_t		 pkts[2];	/* carried in hardware: from the client, to it */
	uint64_t		 bytes[2];
	time_t			 moved;		/* when they last moved, wall clock; 0 never */
};

/*
 * One VLAN subinterface's logical interface on the fast path (discovery 10): the front port, the
 * VLAN, and what it was made with, so a change is an update and a VLAN that goes is switched off.
 */
#define	OCTEP_VLAN_LIF_MAX	64
#define	OCTEP_VLAN_LIF_CHECK	30	/* seconds between two full adds, each a check and a repair */

struct octep_vlan_lif {
	int			 dif;		/* index into dp_if: the port */
	uint16_t		 vlan;
	uint16_t		 mtu;
	uint8_t			 mac[6];
	uint8_t			 fwd;
};

struct octep_conn {
	int			 used;
	uint32_t		 idx;		/* this table's position: the far side's conn_idx */
	uint16_t		 conn_rev;	/* per index, climbs on every allocation, never 0 */
	uint8_t			 probe_dir;	/* which tuple the sweep asks pf about */
	int			 pf_dir;	/* PF_IN or PF_OUT: the list that held the state */
	uint8_t			 ipsec;		/* 1 when an IPsec policy covers it: see octep_conn_mf */
	uint8_t			 enc_dir;	/* then, which direction leaves encrypted */
	uint8_t			 bridged;	/* a bridge carries it: made with nothing rewritten (discovery 9) */
	/*
	 * A command for this connection got no answer. That is "not yet", not "no": the descriptor
	 * stays posted and the far side may carry it out when it comes back. A microflow that
	 * exists with no record here is one nothing would ever take out - and for a plain
	 * connection that is not harmless either: it would go on forwarding in the clear after a
	 * tunnel came up over it, out of reach of the sweep that exists to stop that. So the entry
	 * is kept, tunnelled or plain, its directions counted as programmed, and the audit takes it
	 * out as soon as the far side answers. A takeout that was cut short the same way leaves the
	 * same mark, so whoever decided the connection should go does not have to decide again.
	 */
	uint8_t			 doomed;
	/*
	 * Its verdict is DROP on the coprocessor (discovery 3, XGS: Settings "Cut in hardware"): its
	 * frames die there. It is kept after pf has let its state go for as long as frames keep coming
	 * - drop_alive is when the keep-alive last saw a microflow's stamp move - so they go on dying
	 * there instead of reaching this host, where pf would drop them; once they stop it is taken out
	 * like any other connection pf no longer holds.
	 */
	uint8_t			 dropped;
	time_t			 drop_alive;
	/*
	 * The inspector's (discovery 4): made with the verdict SEND_TO_IPS, so every frame of it goes
	 * to the inspector until ips_bytes - what the inspector has let through - reaches dp.ips_depth,
	 * when it is asked (ips_req) to be set to forward and the rest of it runs in hardware.
	 */
	uint8_t			 ips;
	uint32_t		 ips_req;
	uint64_t		 ips_bytes;
	/* Its live user (dp.live_users), 0 for none: made with it, and revived with it while it lasts. */
	uint16_t		 luid;
	uint8_t			 lu_side;	/* 0 the client opened it, 1 it was opened to the client */
	/*
	 * A frame of it carrying FIN or RST has been handed back. From then on a give-back
	 * is the connection ending and is never answered by reviving it.
	 */
	uint8_t			 closing;
	/*
	 * When the far side was last asked about it, when it was last revived, and how long it is
	 * to be left alone after that - all in ticks. See octep_conn_probe.
	 */
	int			 probe_tick;
	int			 rv_tick;
	int			 rv_wait;
	/*
	 * The revivals of the last OCTEP_REVIVE_STREAK seconds: how many, and when the first of
	 * them was. Read only with dp.revive_hold on; see octep_conn_probe.
	 */
	int			 rv_first;
	u_int			 rv_streak;
	time_t			 ka_time;	/* when the keep-alive last asked about it */
	/*
	 * The two INGRESS tuples of the connection, indexed by direction: [0] is a frame from the
	 * opener as it arrives, [1] a frame from the responder as it arrives. Derived from pf's
	 * oriented state - the opener and responder each as the host sees them and as the wire
	 * sees them - and checked against the frame in hand when the entry is made.
	 */
	struct octep_pf_tuple	 tuple[2];
	/*
	 * The next tuple in the chain each of the two hangs in: (index << 1 | direction) + 1, 0 at
	 * the end. See dp_conn_head.
	 */
	uint32_t		 hnext[2];
	uint32_t		 live_pos;	/* its place in dp_conn_live */
	struct octep_conn_nat	 nat;
	struct octep_conn_mf	 mf[2];
	/*
	 * Its TCP window as FLOW_CREATE_FP carries it (issue #679): the connection entry's TCP block,
	 * laid out as OCTEP_CONN_TCP_LEN describes. tcp_on is 1 when the block was filled from live
	 * numbers - pf's when the connection was made, the latest segment the host saw each way when
	 * pf's had stopped or it was revived - and then the fast path checks the window, and the
	 * take-back hands its numbers back to pf. 0 sends the block as zeros with the check off,
	 * which is what every connection got before. tcp_able is 1 for a connection whose window may
	 * be checked - TCP, dp.tcp_window on, no rule's sloppy and nothing pf rewrites - so that each
	 * revival tries for a window again, whatever the one before managed. tcp_wait is when a revival
	 * began to wait for a segment of each direction (octep_conn_probe), 0 when none is waiting.
	 */
	uint8_t			 tcp_on;
	uint8_t			 tcp_able;
	uint8_t			 tcp[OCTEP_CONN_TCP_LEN];
	uint8_t			 tcp_scale[2];	/* the window scales pf recorded, opener's first */
	int			 tcp_wait;
	/*
	 * What the coprocessor counted for it, from its statistics messages (octep_dp_cmsg_rx). hw_raw
	 * is the SLOT's: the totals the coprocessor last sent for this index, each direction as it sent
	 * them - bytes' low word, then the bytes' bits 32 to 34 under the packet count shifted left by
	 * three - kept across the connections that use the slot, because the coprocessor does not start
	 * them again for a new one (octep_hw_diff). hw_pkts and hw_bytes are this connection's: what
	 * moved while it held the slot, [0] the opener's direction. hw_moved is when a count last grew,
	 * in seconds of uptime; 0 until a message has named it.
	 */
	uint32_t		 hw_raw[2][2];
	uint64_t		 hw_pkts[2];
	uint64_t		 hw_bytes[2];
	time_t			 hw_moved;
	uint8_t			 hw_seen;
	/* how much of that has been added to pf's states for it: see octep_flow_keepalive */
	uint64_t		 hw_pf_pkts[2];
	uint64_t		 hw_pf_bytes[2];
};

/*
 * The coprocessor's statistics message, CONN_STATS_BULK_UPDATE: control frame type 1 on tag 254.
 * After the four-byte control header come num_entries (u16), num_bursts (u8) and is_bidir (u8);
 * then each burst is a u32 - the first connection's index in its low 26 bits, the count less one
 * in the top six - and that many entries of eighteen bytes: per direction a u32 of bytes and a u32
 * of the bytes' bits 32 to 34 under the packet count shifted left by three, then the connection's
 * rev. All little-endian. Measured, see octep_dp_cmsg_rx.
 */
#define	OCTEP_CMSG_TYPE_LIF_STATS	0
#define	OCTEP_CMSG_TYPE_CONN_STATS	1
/*
 * A copy of a frame the fast path forwarded, MIRROR_PKT, sent while fw_cfg's FP_PKT_DUMP bit is
 * set: after the control header a byte of direction (0 arriving at the port, 1 leaving it), a
 * reserved byte and the port's tag (u16, little-endian), then the frame as it crossed that port
 * (the vendor's cmsg_types.h, struct cmsg_mrr_hdr). See octep_dp_mirror.
 */
#define	OCTEP_CMSG_TYPE_MIRROR		6
#define	OCTEP_CMSG_MRR_HLEN		8	/* the control header and struct cmsg_mrr_info */
#define	OCTEP_CMSG_MRR_IN		0
#define	OCTEP_CMSG_MRR_OUT		1
/*
 * The heartbeat: the host's CMSG_TYPE_WATCHDOG_FROM_HOST with a 32-bit sequence after the header,
 * and the fast path's CMSG_TYPE_WATCHDOG_TO_HOST carrying it back (the vendor's cmsg_types.h,
 * struct cmsg_watchdog_payload). See octep_hb_tick.
 */
#define	OCTEP_CMSG_TYPE_HB_FROM_HOST	8
#define	OCTEP_CMSG_TYPE_HB_TO_HOST	9
#define	OCTEP_HB_OFF		0
#define	OCTEP_HB_WAITING	1	/* asked, no answer yet since the heartbeat began */
#define	OCTEP_HB_ALIVE		2
#define	OCTEP_HB_LATE		3
#define	OCTEP_HB_DEAD		4
#define	OCTEP_CMSG_TYPES		10	/* the types the vendor names, 0 to 9: each counted on arrival */
#define	OCTEP_CMSG_KEEP			256	/* bytes of each type's last message kept, for dp.cmsg_last */
#define	OCTEP_HW_ENTRY_LEN		18
#define	OCTEP_LIF_ENTRY_LEN		16	/* LIF_STATS: two directions, no rev */
#define	OCTEP_HW_PKT_SHIFT		3
#define	OCTEP_HW_PKT_MASK		0x1fffffffu	/* 29 bits of packets */
#define	OCTEP_HW_BYTE_BITS		35

/*
 * A next hop the far side has been given: where a frame leaves and to whom. Keyed by what
 * octep_nhop_resolve returns - the egress LIF interface, the neighbour's address, our own address
 * on that egress and its MTU - so every connection that leaves the same way shares one entry, and
 * two hundred LAN-to-WAN connections cost two. refcnt is how many microflows name it; rev is per
 * index and climbs on every allocation, as the vendor's host does, because the far side's
 * microflow carries the next hop's revision beside its index.
 */
struct octep_nhop_ent {
	int			 used;
	uint32_t		 refcnt;
	uint8_t			 rev;
	uint32_t		 iface;
	uint8_t			 dmac[6];
	uint8_t			 smac[6];
	uint16_t		 mtu;
	uint16_t		 vlan;		/* 0 untagged */
};

/*
 * A security association mirrored to the coprocessor, one per index of its table. The index is the
 * record's position; the handle a microflow or a metadata block names is the index PLUS ONE
 * (docs/findings/the-handle-is-the-index-plus-one.md). The kernel offers associations through
 * if_ipsec_accel_methods and identifies each (interface, association) pair by a drv_spi of its own;
 * priv is a pointer to this record. Keys are copied for the one SA_ADD that installs them and
 * cleared as soon as it has been posted.
 */
#define	OCTEP_SA_MAX		64	/* coprocessor indices 1..63; 0 is never used */
#define	OCTEP_SA_COOLOFF	2	/* seconds an index rests after its SA_DEL, as the vendor's host does */
struct octep_sa {
	int		 used;
	int		 cooling;
	time_t		 cool_until;
	uint32_t	 idx;		/* the coprocessor index: this record's position */
	uint16_t	 rev;		/* per index, climbs on every install, never 0 */
	uint16_t	 drv_spi;	/* the kernel's handle for (interface, association) */
	int		 dir;		/* 0 encrypt (outbound), 1 decrypt (inbound) */
	int		 dif;		/* index into dp_if: the interface that took it */
	uint32_t	 lif;		/* that interface's LIF, (iface << 12) */
	uint32_t	 spi;		/* network byte order, compared against the wire as such */
	uint32_t	 src, dst;	/* outer addresses, network byte order; 0 when the outer header is IPv6 */
	int		 v6;		/* the outer header is IPv6: src6/dst6 hold the addresses, opt.ipv6 in the request */
	uint8_t		 src6[16], dst6[16];
	int		 keylen;	/* 16, 24 or 32 */
	uint8_t		 key[32];
	uint8_t		 salt[4];
	uint32_t	 win;		/* anti-replay window in packets, 0 off (encrypt side) */
	int		 natt;		/* UDP-encapsulated (NAT-T): the coprocessor adds and removes the UDP header */
	uint16_t	 natt_sport, natt_dport;	/* its ports, host order; the request builder writes them big-endian */
	int		 esn;		/* extended sequence numbers: the coprocessor keeps the 64-bit count, ext_seq in the request */
	/*
	 * The cipher suite, as the request's control word names it. GCM: cimode CTR, hash GF128_128,
	 * an 8-byte IV that is the coprocessor's counter, a 16-byte ICV, the salt in the auth-key
	 * field. CBC with an HMAC: cimode CBC, the HMAC's hash type, a 16-byte IV, the ICV the hash
	 * type's truncation (12, 16, 24 or 32), the HMAC key in the auth-key field, and the trailer
	 * padded to the 16-byte block. The overhead type is the request's option byte for it.
	 */
	int		 cbc;		/* 1 AES-CBC with an HMAC, 0 AES-GCM */
	int		 hash;		/* 11 GF128_128; 2 SHA1_96, 8 SHA256_128, 9 SHA384_192, 10 SHA512_256 */
	int		 ovh;		/* 2 GCM_128; 3 BLOCK_96, 4 BLOCK_128, 5 BLOCK_192, 6 BLOCK_256 */
	int		 ivlen, icvlen;
	int		 authkeylen;	/* 0 for GCM */
	uint8_t		 authkey[64];
	/*
	 * The number the coprocessor was started after. Inbound: the kernel's highest seen.
	 * Outbound: the kernel's own counter plus OCTEP_SA_SEQ_AHEAD, because the kernel's cipher
	 * was still numbering packets while the coprocessor was given the association.
	 */
	uint64_t	 seq;
	void		*sav;		/* the kernel's association: compared, never dereferenced */
	int		 ready;		/* 0 while SA_ADD or SA_DEL is in flight; see taken, below */
	uint64_t	 handed;	/* ESP frames handed to the coprocessor on it: each took a number */
	uint64_t	 bytes, packets;	/* the engine's counts at the last SA_GET_STATS */
	/*
	 * The engine counts what its flow table forwards on an association and not what the host
	 * hands it or is handed (measured: twenty pings each way by the host path, 0 and 0). Its
	 * counters belong to the INDEX, not to the association, and what they do when an index is
	 * reused was measured on 2026-10-08 and is not what anyone would guess: straight after
	 * SA_ADD they read 0 and 0, and with the first packet the engine itself takes through the
	 * association they read the previous occupant's totals again and count on from there. A
	 * baseline read after the install was therefore 0, and an association three minutes old
	 * told the kernel it had carried 6.4 GB. So base_* is the reading everything has been
	 * accounted for up to - at install, what the index read BEFORE the SA_ADD, or failing that
	 * the last thing this driver ever read there (ipsec_idx_* in the softc) - and it moves with
	 * every reading; pushed_* is what the kernel has been told for this association,
	 * cumulative, as ipsec_accel_drv_sa_lifetime_update wants it, and never backwards.
	 * stat_time is when the counts were last known to be settled, which bounds what one reading
	 * can plausibly add: see octep_ipsec_stats_account.
	 */
	uint64_t	 base_bytes, base_packets;
	uint64_t	 pushed_bytes, pushed_packets;
	/*
	 * Since #761 pushed_* is no longer built from those readings: vendor version 23 answers
	 * SA_GET_STATS with 0 and 0 for an association that has carried millions of packets (vendor version
	 * 21 on the XGS 136 does too). The coprocessor's statistics messages, which carry every
	 * connection's counts once a second on every version, are added to the association each
	 * direction names instead (octep_ipsec_flow_count), as the ARMADA driver has always done,
	 * and cnt_dirty says the kernel has not been told yet. base_*, bytes and packets keep the
	 * one reading install takes, before SA_ADD, which is also the test that the coprocessor answers.
	 */
	int		 cnt_dirty;
	time_t		 stat_time;
	uint32_t	 gen;		/* climbs on every install: tells a successor from this one */
	int		 polled;	/* the counts have been read since the last flow left it */
	/*
	 * A connection has been made to name this association, at some time in its life. Written
	 * where that happens and never cleared: whether flows have used numbers on it is not
	 * something to work out afterwards from the engine's counts, which come in batches of
	 * sixteen thousand packets and a second late - they are what this flag is there to cover.
	 */
	int		 flowed;
	int		 base_valid;	/* base_* is set: nothing is pushed, or attached, before */
	/*
	 * An outbound association goes through three moments, in this order, and each has a
	 * reader that must not act before it. READY: the coprocessor has it. TAKEN: the kernel's
	 * cipher has been swapped for this driver's, in the same hold of the softc lock as ready -
	 * until then octep_ipsec_xf_output passes the association's packets to the kernel's cipher,
	 * which is still the one encrypting for it. SETTLED: every packet that was inside the
	 * kernel's cipher at the swap is out, so the kernel's sequence counter can no longer move
	 * by itself - and only then may anything raise that counter, attach a connection to the
	 * association, or choose it as another's successor. An inbound one is settled when it is
	 * ready and is never taken.
	 */
	int		 taken;
	int		 settled;
	/*
	 * The reqid of the kernel's association head this one hangs from. With the two ends, the
	 * protocol and the mode - which are the same for everything in this table - it IS the head:
	 * the set key_allocsa_policy chooses within, and so what "the same tunnel" means. Two
	 * children between the same two gateways have the same ends and different reqids. By value
	 * and not by the head's address: the driver holds no reference on the head, and an address
	 * the kernel has freed and given to another head would compare equal. It is also how an
	 * inbound policy names the associations it accepts.
	 */
	uint32_t	 reqid;
};

/*
 * What a tunnelled connection needs beyond a plain one, worked out from the kernel's own policy
 * database and association table when the connection is about to be made - see
 * octep_ipsec_flow_resolve.
 */
struct octep_ipsec_flow {
	int		 enc_dir;	/* which of the connection's directions leaves encrypted */
	uint16_t	 out_sa;	/* the outbound association: its handle */
	uint16_t	 out_rev;	/* and its revision */
	int		 out_dif;	/* the interface it is on */
	uint32_t	 out_src, out_dst;	/* the tunnel's two ends, network byte order */
	uint32_t	 in_reqid;	/* what the inbound policy asks of the association, 0 for any */
	/*
	 * Which tunnel the policy names, for asking whether a connection made earlier is still in
	 * the same one: the identity of the kernel's association head when it chose an association,
	 * mirrored or not - its two ends and its reqid, by value - and otherwise what the outbound
	 * policy's request says: its ends, and its reqid when it names one.
	 */
	int		 out_chosen;
	uint32_t	 sah_src, sah_dst, sah_reqid;
	uint32_t	 req_src, req_dst, req_reqid;
};

/*
 * A tunnelled connection the fast path gave back while only its encrypting direction was in
 * hardware is left with the host for a while: see octep_ipsec_backoff_put in octep_dp.c.
 */
#define	OCTEP_IPSEC_BACKOFF_MAX		128
#define	OCTEP_IPSEC_BACKOFF_SECS	60
struct octep_ipsec_backoff {
	struct octep_pf_tuple	 t;
	time_t			 until;
};

/*
 * A connection revived OCTEP_REVIVE_STREAK times in as many seconds and given back again, left with
 * the host for a wait before it is made again - with dp.revive_hold on (issue 544): a second at
 * first, doubling while it keeps coming back within OCTEP_HOLD_CALM_SECS of its last wait, to
 * OCTEP_HOLD_WAIT_MAX_SECS. Both directions' tuples are held. See octep_hold_put in octep_dp.c.
 */
#define	OCTEP_REVIVE_STREAK		4
#define	OCTEP_HOLD_MAX			2048	/* a power of two */
#define	OCTEP_HOLD_WAYS			8	/* the places one hash chooses among */
#define	OCTEP_HOLD_WAIT_MAX_SECS	64
#define	OCTEP_HOLD_CALM_SECS		10
struct octep_flow_hold {
	struct octep_pf_tuple	 t;
	int			 until;		/* ticks; 0 when the place is free */
	int			 wait;		/* ticks, the length of the last wait */
};

/*
 * What the transmit path needs to hand the coprocessor one inner packet to encrypt. The coprocessor's
 * host path encrypts IN PLACE and wants the whole ESP frame already laid out - outer header, ESP
 * header, the IV's room, the plaintext, the trailer, the ICV's room - so the frame is built in the
 * transmit slot's own buffer from the mbuf, which holds the inner packet and nothing else, with the
 * tunnel's next hop in front.
 */
struct octep_esp_tx {
	uint32_t	 handle;	/* the association's handle: its index plus one */
	uint32_t	 spi, src, dst;	/* network byte order */
	uint8_t		 dmac[6], smac[6];	/* the tunnel's next hop */
	uint16_t	 mtu;		/* the IP MTU of the port the ESP frame leaves by, less the UDP header of a NAT-T association */
	uint8_t		 nexthdr;	/* the trailer's next-header byte: 4 for IPv4 inside */
	uint8_t		 natt;		/* UDP-encapsulated: the envelope carries the UDP header the coprocessor expects */
	uint16_t	 natt_sport, natt_dport;	/* host order */
	uint8_t		 ivlen, icvlen;	/* the envelope's IV room and ICV room: 8 and 16 for GCM, 16 and the HMAC's for CBC */
	uint8_t		 blk;		/* the trailer's alignment: 4 for GCM, 16 for CBC (RFC 4303 2.4) */
	uint8_t		 cbc;		/* the IV is this driver's to write, not the coprocessor's counter */
	uint8_t		 v6;		/* the outer header is IPv6, 40 bytes, from src6/dst6; src/dst are 0 */
	uint8_t		 accel;		/* the kernel's own offload path tagged it: its lifetime is ours to count (#774) */
	uint8_t		 src6[16], dst6[16];
};

struct octep_softc;
int	octep_nhop_resolve(struct octep_softc *, uint32_t, int, struct octep_nhop *);
int	octep_nhop_resolve6(struct octep_softc *, const uint8_t *, int, struct octep_nhop *);
int	octep_nhop_get(struct octep_softc *, const struct octep_nhop *, uint32_t *);
void	octep_nhop_put(struct octep_softc *, uint32_t);
void	octep_dp_flows_forget(struct octep_softc *);
void	octep_dp_handback_run(struct octep_softc *);

/* octep_ipsec.c */
struct octep_dp_if;
struct mbuf;
struct sysctl_ctx_list;
struct sysctl_oid_list;
void	octep_ipsec_if_attach(struct octep_softc *sc, if_t ifp);
void	octep_ipsec_attach(struct octep_softc *sc);
void	octep_ipsec_detach(struct octep_softc *sc);
int	octep_ipsec_detach_check(struct octep_softc *sc);
int	octep_ipsec_interposed(void);
int	octep_ipsec_send_inner(struct octep_softc *sc, struct octep_dp_if *dif, struct mbuf *m,
	    const struct octep_esp_tx *esp);
void	octep_ipsec_rx(struct octep_softc *sc, struct octep_dp_if *dif, struct mbuf *m,
	    uint32_t sa_word, uint32_t ident);
int	octep_ipsec_tx_prepare(struct octep_softc *sc, struct octep_dp_if *dif, struct mbuf *m,
	    struct octep_esp_tx *esp);
uint32_t octep_ipsec_wire_len(uint32_t inner, const struct octep_esp_tx *esp, uint32_t *padlen);
void	octep_ipsec_envelope(uint8_t *f, const struct octep_esp_tx *esp, struct mbuf *m,
	    uint32_t inner, uint32_t padlen);
int	octep_dp_tx(struct octep_dp_if *dif, struct mbuf *m, const struct octep_esp_tx *esp);
int	octep_ipsec_flow_resolve(struct octep_softc *sc, const struct octep_pf_tuple *tup,
	    struct octep_ipsec_flow *fi, int quiet, int want_sa);
int	octep_ipsec_handle_live(struct octep_softc *sc, uint32_t handle, uint32_t rev, int dir,
	    int need_ready);
int	octep_ipsec_flow_same_tunnel(struct octep_softc *sc, const struct octep_ipsec_flow *fi,
	    uint32_t handle);
void	octep_dp_flows_ipsec_audit(struct octep_softc *sc);
uint32_t octep_ipsec_spgen(void);
int	octep_ipsec_flow_live(struct octep_softc *sc, const struct octep_ipsec_flow *fi,
	    uint32_t dsa, uint32_t dsa_rev);
int	octep_ipsec_flow_dec_ok(struct octep_softc *sc, const struct octep_ipsec_flow *fi,
	    uint32_t dsa, uint32_t dsa_rev);
void	octep_ipsec_stats_poll(struct octep_softc *sc);
void	octep_ipsec_sa_flow_attached(struct octep_softc *sc, uint32_t handle);
void	octep_ipsec_flow_count(struct octep_softc *sc, const struct octep_conn *c,
	    const uint64_t *pk, const uint64_t *by);
int	octep_dp_tuple_from_ip(const uint8_t *ip, uint32_t len, struct octep_pf_tuple *t);
void	octep_flow_cand_put(struct octep_softc *sc, const struct octep_pf_tuple *t, uint32_t slot,
	    uint32_t rev, int in_dif, uint16_t tag, uint16_t sa, uint16_t sa_rev, uint8_t closing,
	    const struct octep_tcp_seg *seg, const uint8_t *mac, uint16_t vlan);
uint8_t	octep_dp_tcp_closing(const uint8_t *ip, uint32_t len);
void	octep_dp_tcp_seg(const uint8_t *ip, uint32_t len, struct octep_tcp_seg *s);
void	octep_dp_flows_sa_gone(struct octep_softc *sc, uint32_t handle, uint32_t rev,
	    uint32_t repl, uint32_t repl_rev);
void	octep_dp_flows_ipsec_out(struct octep_softc *sc, uint32_t mode);
uint32_t octep_dp_flows_on_sa(struct octep_softc *sc, uint32_t handle, uint32_t rev);
void	octep_ipsec_add_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
	    struct sysctl_oid_list *top);

bool	octep_pf_present(void);
void	octep_pf_retry(void);
int	octep_pf_state_exists(const struct octep_pf_tuple *, int, int *);
int	octep_pf_state_read(const struct octep_pf_tuple *, struct octep_pf_state *);
int	octep_pf_mark_sloppy(const struct octep_pf_tuple *);
int	octep_pf_settled(const struct octep_pf_tuple *);
int	octep_pf_touch(const struct octep_pf_tuple *);
int	octep_pf_count(const struct octep_pf_tuple *, const uint64_t *, const uint64_t *);
int	octep_pf_tcp_scan(const struct octep_pf_tuple *, struct octep_pf_tcp *);
int	octep_pf_tos_scan(const struct octep_pf_tuple *, uint16_t *);
int	octep_pf_handback(const struct octep_pf_tuple *, const uint8_t *, const uint8_t *);

struct octep_dp_if {
	if_t			 ifp;
	struct octep_softc	*sc;
	struct ifmedia		 media;
	/*
	 * The two receive filters the link poll reconciles: what the stack wants, and what the far
	 * side was last successfully told. Two fields each and nothing else - a failed request
	 * records nothing, so the pair still disagrees and the next sweep asks again.
	 */
	int			 filt_want;	/* 1 when the stack has joined any group */
	int			 filt_have;	/* what the far side was successfully told */
	int			 prom_want;	/* IFF_PROMISC, re-read from the ifp every poll */
	int			 prom_have;	/* what the far side was successfully told */
	/*
	 * And the logical interface's forwarding mode, by the same rule and for a sharper
	 * consequence: with the offload gate open, a bridge member left in L3 drops every frame
	 * addressed to the bridge. Read from the ifnet each poll, like the two above.
	 */
	int			 fwd_want;	/* L2 while this port is a bridge member, else L3 */
	int			 fwd_have;	/* what the far side was successfully told */
	uint32_t		 lif_iface;	/* this port's LIF index, staged by dp.if_iface */
	/* the coprocessor's count of this port's LIF (type 0) since attach: [0] arrived, [1] left */
	uint64_t		 lif_pkts[2];
	uint64_t		 lif_bytes[2];
	uint8_t			 lif_seen;
	uint16_t		 tag;
	uint32_t		 nwaport;	/* the NetAgent port, which is not always the tag */
	int			 link;		/* -1 unknown, 0 down, 1 up - polled, see below */
	uint32_t		 speed;	/* Mbit/s while the link is up, 0 when it is not */
	/*
	 * A cage - every port but the switch's RJ45 sockets, PortN - and what the module in it is:
	 * an Ethernet media subtype (net/if_media.h's number) for a gigabit and one for ten, 0 where
	 * it has not been read. The modules' memory is on the coprocessor's I2C, so the plugin reads
	 * it there and writes these in dp.module_media.
	 */
	uint8_t			 cage;
	u_int			 mod_media[2];
	uint8_t			 mac[6];
	uint64_t		 rx_packets;
	uint64_t		 rx_bytes;
	uint64_t		 rx_nobuf;	/* arrived, and there was no mbuf for it */
	uint64_t		 tx_packets;
	uint64_t		 tx_bytes;
	uint64_t		 tx_drops;
};

/* One MSI-X vector, hooked to one output ring. */
struct octep_dp_vec {
	struct octep_softc	*sc;
	struct resource		*res;
	void			*cookie;
	int			 rid;
	uint32_t		 ring;
	uint64_t		 count;		/* handler entries for this ring */
};

struct octep_dp_oq {
	struct octep_dma	slist;
	struct octep_dma	bufs;
	struct octep_dma	info;
	uint32_t		ring;
	int			armed;
	int			quiet;		/* octep_dp_ring_quiet's answer at the stop, -1 before it */
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
/*
 * The high ring's size, the vendor's RPC_DESC_PER_RING, and the place each of its commands is
 * written in host memory: one slot a descriptor, so a slot is free again when the descriptor is,
 * and large enough for FLOW_CREATE_FP's 256 bytes and the eight-byte head. The far side fetches
 * consecutive commands in one DMA of at most 32 KB (RPC_DMA_FROM_HOST_LIMIT in its rpc_handler.c)
 * and a slot that wraps to the start of the region is flagged NO_AGG_DMA, as the vendor's host
 * flags a payload that wraps its aggregate buffer.
 */
#define	OCTEP_RPC_HI_DESCS		256
#define	OCTEP_RPC_HI_SLOT		512

/* struct rpc_cmd_buf_desc and struct rpc_resp_buf_desc, in host memory */
#define	OCTEP_RPC_BUF_DESC_SIZE		8
#define	OCTEP_RPC_DATA_MAX_SIZE		4096
#define	OCTEP_RPC_RC_ERRNO_BIT		(1 << 15)
/* The far side is Linux, so the errno under that bit is Linux's: EAGAIN is 11 there, 35 here. */
#define	OCTEP_RPC_LINUX_EAGAIN		11
/*
 * A HANDLER'S errno is not under the errno bit. That bit is the transport's: rpc_handler.c sets
 * it only when a command could not be dispatched at all (reset_rpc, a negative errno from the
 * ring itself). A handler that wants to return an errno ENCODES it instead - sp2fp_helpers.h:
 * SP2FP_RC_ENCODE(err) is SP2FP_RC_MAX + -err for a negative err - and rpc_cmd_put stores that
 * positive value raw in the descriptor's rc. So the sixteen bits carry three encodings by range:
 * 0 is success, 1..SP2FP_RC_MAX are the SP2FP_RC_* refusal codes, above SP2FP_RC_MAX is
 * SP2FP_RC_MAX plus a Linux errno, and the errno bit is the transport's alone.
 *
 * SP2FP_RC_MAX is 20 on this appliance: the vendor header does not define it where this tree can
 * read it, but the coprocessor's own usfp_rh binary does - ipsec_add compares the add's result
 * with -11 and returns 0x1f, 31, and the vendor's x86 host compares the same command's rc with
 * 0x1f. The first version of the SA retry compared against the errno bit and 11 and could never
 * fire; a review caught it before it reached the hardware.
 */
#define	OCTEP_RPC_SP2FP_RC_MAX		20
#define	OCTEP_RPC_SP2FP_RC_EAGAIN	(OCTEP_RPC_SP2FP_RC_MAX + OCTEP_RPC_LINUX_EAGAIN)
#define	OCTEP_RPC_SA_RETRIES		5

/* struct usfp_fpop_req_table_read: s_index, num_entries, flags, e_index */
#define	OCTEP_RPC_REQ_LEN		12

/*
 * enum rpc_cmd_type. Named here are the commands this driver issues and no others: every read,
 * and the seven writes listed by octep_rpc_cmd_is_allowed_write below. The rest of that
 * enumeration - the flow and connection commands, the next-hop and QoS ones (the DoS ones only for a
 * probe, below), and the IPsec
 * commands beyond add and delete - is in the document and deliberately not here, so that a
 * mistyped rpc.cmd is refused by number rather than issued.
 */
/*
 * The three firewall-state commands, which are the first three in the enumeration.
 *
 * FW_CFG_PARAMS_SET carries the global configuration word whose bit 0 is FW_CFG_OFFLOAD; with that
 * bit clear the fast path forces every wire frame to the host without ever looking for a flow,
 * which is the whole of what FPCNTR_FROM_WIRE_TO_KN_FORCED counts. The two revision commands
 * publish the numbers the flow, connection and next-hop entries are checked against, and setting
 * the firewall revision invalidates every offloaded flow.
 *
 * Being first in the enumeration is not a required order: nothing in the coprocessor's modules
 * reads struct fw_state except these three setters, and PPORT_UPDATE, LIF_ADD_UPDATE, SA_ADD and
 * every read worked on this appliance before the driver could issue any of them. What fw_cfg gates
 * is acceleration.
 */
#define	OCTEP_RPC_CMD_FW_STATE_REV_SET		0
#define	OCTEP_RPC_CMD_FW_L3_FWD_STATE_REV_SET	1
#define	OCTEP_RPC_CMD_FW_CFG_PARAMS_SET		2
#define	OCTEP_RPC_CMD_LIF_ADD_UPDATE		3
/*
 * The next-hop table, which a flow's action points at.
 *
 * struct usfp_fpop_req_program_nhop is a 32-bit index then struct usfp_nhop_entry entire: two
 * bytes of is_resolved and revision, two reserved, then usfp_nhop_core_info - the two addresses,
 * the ethertype, a VLAN, the port tag, the flags, the interface and the MTU. Twenty-eight bytes
 * for the entry, which is what LO_NHOP_READ returns, and thirty-two for the request.
 *
 * It is what makes a flow able to leave: a microflow's action carries an nhop_index, and the
 * entry it names is the resolved neighbour and the port to send out of. On this appliance the
 * whole table reads back as zeros, so nothing has ever programmed one.
 */
#define	OCTEP_RPC_CMD_NHOP_PROGRAM		6
#define	OCTEP_RPC_CMD_NHOP_UPDATE		7
#define	  OCTEP_NHOP_FLAG_L3		0x01
#define	  OCTEP_NHOP_FLAG_IPSEC		0x02
#define	OCTEP_NHOP_REQ_LEN		32

/*
 * MFLOW_PROGRAM, which loads the action into a flow slot the fast path has already made.
 *
 * struct usfp_fpop_req_program_mflow is twenty-eight bytes: the four-byte identity, the twenty of
 * struct usfp_mflow_entry_opr, and a four-byte timeout.
 *
 *     +0   mf_ident      id:25, rev:6, valid:1
 *     +4   opr_fl        sa_index:16, action:4, rsvd:3, dir:1, bridge_control:4, state:4
 *     +8   opr_bf        l3_fwd_rev_num:16, dscp_override_val:8, dscp_override_en:1, rsvd:7
 *     +12  conn_index
 *     +16  fw_state_rev_num:16, conn_rev_num:16
 *     +20  nhop_index:24, nhop_rev_num:8
 *     +24  sa_rev_num:16, rsvd:16
 *     +28  mflow_timeout
 *
 * THE IDENTITY IS NOT INVENTED. struct usfp_mflow_state has fw_valid, "set by the fastpath firmware
 * to indicate a flow is present", and host_valid, "set by the host after a valid opr entries has
 * been loaded" - the vendor's own comments. The fast path makes the entry and punts the frame
 * carrying its identity in usfp_kn_md; the host loads the action into that slot. Programming one
 * the fast path has not made is refused, which is why MFLOW_PROGRAM alone cannot conjure a flow.
 *
 * THE STATE IS THE GATE, NOT THE ACTION. host_valid is necessary and nowhere near sufficient.
 * mflow_fpop_prog_both writes the opr and sets host_valid for any state the caller asks for, so
 * the write lands and reads back - action, connection, next hop, all of it - while the fast path
 * goes on punting every frame and counting FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE, whose own text is
 * "the microflow entry matched by this packet is disabled for offload". The vendor's host writes
 * one value there and never varies it: req->mf_opr.opr_fl.state = MF_ACTIVE, in
 * sp2fp_mflow_microflow_populate. Sweeping the four bits on the appliance, 2 is the only value at
 * which the counter stops: the traffic moves to FROM_WIRE_TO_KN_NHOP_UNRESOLVED, the next test in
 * the fast path's own order, because the sweep left the next hop at zero. So MF_ACTIVE is 2, and
 * a flow with any other state is inert however completely it is programmed.
 *
 * THE ACTION VALUE IS KNOWN ONLY FOR DROP. MF_ACT_DROP, MF_ACT_FWD, MF_ACT_IPS and MF_ACT_AUX are
 * used in the vendor's source and defined in a tree that is not in the GPL drop, and they are in
 * none of the binaries this project holds. With state 2, action 0 raises
 * FROM_WIRE_DROP_MFLOW_ACTION on the first frame, so MF_ACT_DROP is 0 and the declaration order
 * is at least right at its head. The other three are still inference: telling MF_ACT_FWD from
 * MF_ACT_IPS needs a resolved next hop, because without one every non-dropping action lands on
 * NHOP_UNRESOLVED and they look alike. rpc.mflow_action therefore still has no default.
 */
#define	OCTEP_RPC_CMD_MFLOW_PROGRAM		8
#define	OCTEP_RPC_CMD_MFLOW_INVALIDATE		9
#define	OCTEP_MFLOW_REQ_LEN		32

/* Measured, not inferred - see the comment above. */
#define	OCTEP_MFLOW_STATE_ACTIVE	2
#define	OCTEP_MFLOW_ACTION_DROP		0

/*
 * FLOW_CREATE_FP, the one command that can create a direction the host was never told about.
 *
 * struct usfp_fpop_req_flow_create is a connection and TWO microflows:
 *
 *     +0    struct usfp_fpop_req_conn_create conn        112 bytes, as CONN_CREATE_FP sends it
 *     +112  unsigned int mflow_valid                     bit 0 ORIG, bit 1 REPLY
 *     +116  struct usfp_fpop_req_program_mflow mflow_o   32
 *     +148  struct usfp_fpop_req_program_mflow mflow_r   32
 *                                                        = 180
 *
 * WHY IT EXISTS, which this project knew as a rule and not as a reason. A frame the fast path punts
 * carries its flow identity in usfp_kn_md, so the host can learn the identity of a flow arriving
 * from the wire. A frame the host TRANSMITS is never punted, so no identity is ever reported for
 * the other direction - and MFLOW_PROGRAM can only load an action into a slot whose identity it
 * already has. This command carries both directions and the host chooses both indices, which is the
 * only way the outbound direction can be named at all.
 *
 * Measured before it was written: a connection and one direction, every field read back as set -
 * fw_valid 1, host_valid 1, action, conn and nhop all as written - and every frame still counted on
 * FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE. One direction is not a flow.
 *
 * The two microflows share everything this driver stages except the identity and the next hop,
 * which differ by direction - so rpc.mflow_* describes the first and rpc.mflow2_* the second,
 * rather than thirty sysctls for what is one decision.
 */
#define	OCTEP_RPC_CMD_FLOW_CREATE_FP		10
#define	  OCTEP_FLOW_MFLOW_VALID_ORIG	0x1
#define	  OCTEP_FLOW_MFLOW_VALID_REPLY	0x2
#define	OCTEP_FLOW_REQ_LEN		256
#define	  OCTEP_FLOW_OFF_VALID		112
#define	  OCTEP_FLOW_OFF_MFLOW_O	116
#define	  OCTEP_FLOW_OFF_MFLOW_R	148

/*
 * Address translation lives in the CONNECTION, not in the microflow.
 *
 * This is why FLOW_CREATE_FP carries a whole connection entry and not just two identities, and it
 * is the difference between accelerating a routed flow and accelerating a real one: on an ordinary
 * appliance every connection out to the internet is translated, so a flow forwarded without its
 * translation leaves with a private source address and is dropped by the first router it meets.
 *
 * struct usfp_nat_info is six long words - the original pair of addresses, the translated pair,
 * then the original ports and the translated ports - and two bits in the connection's flags word
 * say which direction to apply: do_dnat at bit 19, do_snat at bit 20, beside the verdict at 21 and
 * the state at 30, all of which are confirmed by writing a connection and reading it back with
 * LO_CONN_READ.
 *
 * THE OFFSET IS COMPUTED AND IT AGREES WITH A MEASUREMENT, which is the only reason to trust it.
 *
 * Measured: mflow_valid is read at request offset 112 and programming a microflow there works, so
 * the connection ahead of it occupies 4 + 108, and LO_CONN_READ returns 116 bytes for one entry - the vendor computes it as the 8-byte
 * table-entry header plus the 108-byte connection, which this comment used to give as 108.
 *
 * Computed: atomic 8 + session_id 4 + qos[2] 8 + tcp 60 + nat 24 + lock 4 = 108. The tcp block is
 * where the arithmetic nearly went wrong - the vendor's header comments struct usfp_tcp_info as
 * "13 LW", which is 52 and would put the NAT block eight bytes earlier. It is 60:
 * usfp_tcp_seq is packed and 48 of its own (seen[2] alone is 32), usfp_fin_state is 8, and four
 * bytes of bitfields follow. Two independent ways of getting the same 108 is what makes the
 * offset below a fact rather than a hope, and the stale comment is why neither way was trusted
 * on its own.
 *
 * rpc.conn_nat_off stays settable anyway. It costs nothing, and the next structure whose comment
 * disagrees with its members will be found with it rather than argued about.
 */
/*
 * The values the driver uses when it programs a flow by itself, each one measured.
 *
 * One index for the next hop, the connection and the session, because this programs ONE flow at a
 * time and a second call replaces the first. Anything more is a table allocator, which is the next
 * piece of work and not a number to invent here.
 */
#define	  OCTEP_MFLOW_STATE_INACTIVE	1	/* anything but 2; the entry stays, unused */
#define	  OCTEP_FLOW_AUTO_TIMEOUT	60
/*
 * How many candidates are turned into flows per poll; the table of them is sized beside struct
 * octep_flow_cand.
 *
 * Eight per poll because each flow is three posted commands and each command can wait
 * OCTEP_RPC_CMD_WAIT_MS for its reply - so the drain stops at the first failure, which bounds a
 * poll's worst case to one timeout, exactly as it was when it programmed one flow.
 */
#define	  OCTEP_FLOW_PER_POLL		8
#define	  OCTEP_CONN_VERDICT_CUT_THRU	2	/* forward, rather than hand to an IPS we have none of */
#define	  OCTEP_CONN_STATE_VALID	1
#define	  OCTEP_MFLOW_ACTION_FWD	1
/*
 * The microflow's other actions, read 2026-10-23 from the vendor's host module as the verdicts were
 * (its microflow printer: "DROP" for 0, "FWD" for 1, "IPS" for 2, "AUX" for 3). With IPS the fast
 * path asks the connection's verdict for each frame: CUT_THRU sends it on to the wire
 * (FROM_WIRE_TO_WIRE_VERDICT_CUT_THRU), DROP drops it (FROM_WIRE_DROP_IPS_VERDICT), SEND_TO_IPS
 * hands it to the inspector's virtual function. With FWD the verdict is never asked - measured: a
 * connection set to DROP under FWD microflows went on at 5.1 Gbit/s, the drop counter at 0.
 */
#define	  OCTEP_MFLOW_ACTION_DROP	0
#define	  OCTEP_MFLOW_ACTION_IPS	2
/*
 * Which direction of a connection a microflow is - and the far side uses it as an ARRAY INDEX, not
 * as a label. Its per-direction TCP window state is tcp_seq.seen[dir], its QoS block is qos[dir],
 * and the window scale is chosen by it. The vendor's own conn_dir enum.
 *
 * This driver sent the constant 1 - the reply - for every flow it ever programmed, including the
 * ones going the other way. See octep_rpc_flow for what that cost and how the right value was
 * already in hand.
 */
#define	  OCTEP_CONN_DIR_ORIGINAL	0
#define	  OCTEP_CONN_DIR_REPLY		1
/* overwrite both MACs and decrement the TTL: bits 1, 2 and 3 of bridge_control */
#define	  OCTEP_BRCTL_ROUTED		0xe
/* nothing: a bridged connection's frames leave as they came, their MACs and TTL theirs (discovery 9) */
#define	  OCTEP_BRCTL_BRIDGED		0x0
/* routed, and the VLAN written from the next hop - 0 untagged (discovery 10) */
#define	  OCTEP_BRCTL_ROUTED_VLAN	(OCTEP_BRCTL_ROUTED | 0x1)

#define	  OCTEP_CONN_OFF_NAT		84	/* 4 + atomic 8 + session 4 + qos 8 + tcp 60 */
#define	  OCTEP_CONN_FLAG_DNAT		(1u << 19)
#define	  OCTEP_CONN_FLAG_SNAT		(1u << 20)

/*
 * What LO_MFLOW_READ answers with, and why reading it wrongly was so convincing.
 *
 * The reply is a packed array of struct usfp_table_entry, each carrying a
 * struct usfp_mflow_fpop_rd_data:
 *
 *     +0   int32_t idx          the table index this entry came from
 *     +4   int32_t resv
 *     +8   struct usfp_mflow_key       key     64 bytes
 *     +72  struct usfp_mflow_entry     entry   20 bytes
 *     +92  struct usfp_mflow_entry_opr opr     20 bytes
 *                                              = 112 per entry
 *
 * AND IT IS FILTERED. mflow_fpop_read() calls do_copy_mflow(), which with flags 0 copies only an
 * entry whose fw_valid is set, and packs what it copies - so an index that does not match is
 * skipped and the NEXT one takes its place. Asking for one index and reading what comes back as if
 * it were that index is how this project convinced itself that every slot in the table held the
 * same bytes. USFP_TABLE_FLAG_READ_ALL turns the filter off, and then idx at +0 is the index asked
 * for, every time.
 *
 * The key is the part worth having: lif_id, the two addresses, the ethertype, the protocol, the
 * ports and the addresses - so an entry says which flow it is, and a table dump says what the fast
 * path is tracking.
 */
#define	OCTEP_TABLE_FLAG_READ_ALL	0x0002
/*
 * 116, and it was 112 - which contradicted the three offsets below it by arithmetic alone.
 *
 * The vendor computes it as sizeof(struct usfp_table_entry) + sizeof(struct usfp_mflow_fpop_rd_data):
 * an 8-byte header of int32 idx and int32 reserved, then key 64 + entry 20 + opr 24. The opr offset
 * below says 92 and the opr is 24 bytes, so the entry cannot be shorter than 116, and a reply
 * measured on the appliance was 116. This is the loop stride in octep_rpc.c, so with 112 every entry
 * after the first in a multi-entry read was decoded four bytes early.
 */
#define	OCTEP_MFLOW_RD_ENT_LEN		116
#define	  OCTEP_MFLOW_RD_KEY_OFF	8
#define	  OCTEP_MFLOW_RD_ENTRY_OFF	72
#define	  OCTEP_MFLOW_RD_OPR_OFF	92
#define	  OCTEP_BRCTL_OVRWT_VLAN	0x1
#define	  OCTEP_BRCTL_OVRWT_DST_MAC	0x2
#define	  OCTEP_BRCTL_OVRWT_SRC_MAC	0x4
#define	  OCTEP_BRCTL_UPDATE_TTL	0x8
#define	OCTEP_RPC_CMD_PPORT_UPDATE		5
/*
 * The connection table. conn_fpop_conn_create bounds-checks conn_idx against the table size, takes
 * the entry's lock and copies the entry in - no other precondition, and nothing that has to exist
 * first. It is the one table the host can populate unaided, which is why it is here: a flow is
 * created by the fast path and never by the host, so a connection is the only thing left to try.
 */
#define	OCTEP_RPC_CMD_CONN_CREATE_FP		11
/*
 * struct usfp_fpop_req_conn_reclaim: the index and the revision the host believes the entry has,
 * eight bytes. The far side moves the entry to RECLAIMED and answers with the connection's final
 * TCP sequence state and per-direction byte and packet counts, 64 bytes. It accepts an entry that
 * is VALID or RECLAIM_PENDING and refuses one whose revision has moved - which is how a reused
 * index is kept from reclaiming its successor. This driver never sent it, so nothing it programmed
 * ever left RECLAIM_PENDING once the fast path put it there.
 */
#define	OCTEP_RPC_CMD_CONN_RECLAIM_FP		16
#define	OCTEP_CONN_RECLAIM_REQ_LEN		8
/*
 * A connection's TCP numbers read, the connection left where it is (issue #679): the reclaim's eight
 * bytes, and the answer the fast path's struct usfp_tcp_seq. Answered for an entry VALID or
 * RECLAIM_PENDING (conn_fpop_state_get in the vendor's conn_fpop.c); nothing in it is changed.
 */
#define	OCTEP_RPC_CMD_CONN_GET_FP_TCP_STATE	17
/*
 * struct usfp_fpop_req_conn_modify_verdict: the index, the revision the host holds for it and the
 * new verdict - eight bytes, the reclaim's shape. The far side refuses a verdict above 2, an index
 * past the table, a revision that has moved and an entry that is neither VALID nor RECLAIM_PENDING,
 * and writes nothing of the entry but the verdict's two bits (discovery 3, 2026-10-23).
 *
 * The three values are not in the source this project holds, whose debugfs names them without
 * numbers. They were read from the vendor's host module: its connection printer chooses "SEND TO
 * IPS" for 0, "DROP" for 1 and "CUT THRU" for 2 (the relocations of usfp_dfs_seq_print_conn), and
 * the coprocessor's handler refuses anything above 2. CUT_THRU at 2 is the value this driver has
 * forwarded with since its first flow, which agrees.
 */
#define	OCTEP_RPC_CMD_CONN_MODIFY_FP_VERDICT	13
/*
 * Live users (discovery 5): the coprocessor keeps a table of the users logged in, a bit for each of
 * 65,536 (the vendor's luid_fpop.c), and a connection carries its user's number in its entry - the
 * u16 after the flags word, which CONN_CREATE_FP has always sent as 0. ADD and DELETE take the
 * number as a u32. CONN_MODIFY_FP sets it on a connection in place (struct usfp_fpop_req_conn_modify):
 * u32 index, u16 revision, u16 0, u16 which fields - bit 0 the live user, bits 1 and 2 the two QoS
 * words - u16 the user, then the two QoS words. The table is read with LO_LUID_READ (41), a range a
 * multiple of 64 long, answered with its bits and nothing in front of them.
 */
#define	OCTEP_RPC_CMD_CONN_MODIFY_FP		12
#define	OCTEP_CONN_MODIFY_REQ_LEN		20
#define	  OCTEP_CONN_MODIFY_LUID		0x0001
/*
 * A priority and a meter for each direction (discovery 6): struct usfp_qos_info, a u32 whose fields
 * run from bit 0 - 13 reserved, the priority bit, the valid bit, then a 17-bit meter number. Sent with
 * bits 1 and 2 of the modify's field word, read back at +20 and +24 of a LO_CONN_READ record.
 */
#define	  OCTEP_CONN_MODIFY_QOS_ORIG		0x0002
#define	  OCTEP_CONN_MODIFY_QOS_REPLY		0x0004
#define	OCTEP_QOS_PRI				(1u << 13)
#define	OCTEP_QOS_VALID				(1u << 14)
#define	OCTEP_QOS_METER_SHIFT			15
#define	OCTEP_QOS_METER_MAX			((1u << 17) - 1)
#define	OCTEP_RPC_CMD_ADD_LIVE_UID		18
#define	OCTEP_RPC_CMD_DELETE_LIVE_UID		19
#define	OCTEP_LIVE_UID_REQ_LEN			4
#define	OCTEP_LUID_MAX				65536
#define	OCTEP_CONN_VERDICT_REQ_LEN		8
#define	  OCTEP_CONN_VERDICT_SEND_TO_IPS	0
#define	  OCTEP_CONN_VERDICT_DROP		1
/*
 * The two security-association writes. The whole SA block is 30 to 35 and the read is 42; this
 * driver issues the two that install and remove one, and reads the table back with 42.
 * docs/families/octeon-tx-rpc.md has the request layout and the algorithm numbers.
 */
#define	OCTEP_RPC_CMD_SA_ADD			30
#define	OCTEP_RPC_CMD_SA_DEL			31
/*
 * struct usfp_fpop_req_get_sa_stats: the index, four bytes. The far side answers with the
 * association's live counters - bytes and packets as 64-bit words, then the seconds since the
 * association was created - twenty bytes of content. A read in everything but its number, which
 * sits among the writes, so it is named in octep_rpc_cmd_is_read and needs no write gate. This is
 * the instrument issue #185 lacked: whether a frame the engine was asked to encrypt was counted
 * against its association at all.
 */
#define	OCTEP_RPC_CMD_SA_GET_STATS		32
#define	OCTEP_SA_STATS_REQ_LEN			4
#define	OCTEP_SA_STATS_RESP_LEN			20
/*
 * Discovery 14: the three that keep an offloaded association's state in step with the host's (the
 * vendor's sa_table.h and ipsec_fpop.c; the same sizes on vendor version 21, 22 and 23). SA_REPLAY_UPDATE
 * checks one inbound sequence number against the engine's window and takes it in - and on an
 * association whose window has not seen a frame yet, more than two seconds old, takes it out of
 * the fast path's lookup instead, the vendor's answer to an association installed on the wrong
 * port. SA_SEQ_UPDATE answers the engine's next outbound number and moves its counter on by one,
 * after switching the association's counter to atomic: the first ask answers -EAGAIN while the
 * workers take that in. SA_HOST_STAT_SYNC gives the engine the host's own counts of associations,
 * for its combined lifetime checks. Sent by hand only, as instruments, through the write gate.
 */
#define	OCTEP_RPC_CMD_SA_REPLAY_UPDATE		33
#define	OCTEP_RPC_CMD_SA_SEQ_UPDATE		34
#define	OCTEP_RPC_CMD_SA_HOST_STAT_SYNC		35
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
/*
 * The four counter clears, one to a counter array, in the vendor's order - debug, fast path, port,
 * direct forwarding - and with no request: the far side pushes a clear onto that array's ring and
 * the workers zero it (cntrs_fpop_clr). Vendor version 22's numbers, as every number here; sfosnum_wire
 * places them on 21 (46 to 48, no direct-forwarding clear) and on 23 (48 to 51). Sent by
 * rpc.counters_clear (discovery 13).
 */
#define	OCTEP_RPC_CMD_LO_WORKER_DBG_CNT_CLR	47
#define	OCTEP_RPC_CMD_LO_WORKER_SYS_CNT_CLR	48
#define	OCTEP_RPC_CMD_LO_WORKER_PORT_CNT_CLR	49
#define	OCTEP_RPC_CMD_LO_WORKER_DF_CNT_CLR	50
#define	OCTEP_RPC_CMD_MAX			51

/*
 * struct platform_info, the answer to PLATFORM_READ. Three 64-byte names and then the numbers,
 * read out of the vendor's include/platform_info.h rather than counted off a hex dump. These are
 * the bounds the rest of this driver should be asking for instead of carrying constants: the LIF
 * table is max_ifaces * 4096 entries, and both sides refuse an interface id above max_ifaces - 1.
 */
#define	OCTEP_PLATFORM_NAME_LEN		64
#define	OCTEP_PLATFORM_OFF_NAME		0
#define	OCTEP_PLATFORM_OFF_VERSION	64
#define	OCTEP_PLATFORM_OFF_ASSEMBLY	128
#define	OCTEP_PLATFORM_OFF_ID		192
#define	OCTEP_PLATFORM_OFF_CORES	193
#define	OCTEP_PLATFORM_OFF_MAX_IFACES	194
#define	OCTEP_PLATFORM_OFF_RPC_RINGS	195
/*
 * The table sizes, four 32-bit words after the four bytes at 192: def_mflow_to_secs at 196,
 * conn_not_usable_mflow_to_secs at 200, then max_conn_entries, max_nhop_entries, max_fw_rule_ids,
 * max_ipsec_sas, capabilities, num_mflows. Measured on this board as 10, 5, 2000000, 65536, 65536,
 * and 4001450, field for field against what the vendor's own tool printed under its firmware.
 */
#define	OCTEP_PLATFORM_OFF_DEF_MFLOW_TO	196
#define	OCTEP_PLATFORM_OFF_MAX_CONN	204
#define	OCTEP_PLATFORM_OFF_MAX_NHOP	208
#define	OCTEP_PLATFORM_OFF_MAX_SAS	216
/*
 * What follows capabilities depends on whose software answers (sfosnum.h). From vendor version 22 on:
 * num_mflows at 224, then num_pfs, num_vfs and num_workers at 228 to 230 - the 232 bytes this
 * board answers. Vendor version 21 answers 228, and at 224 it has num_pfs, not num_mflows, so num_mflows is
 * read only from a block of 232: taken from a 228-byte one it would be the number of PFs, and every
 * microflow slot past it refused.
 *
 * Until 2026-10-25 this said num_pfs and num_vfs were at 232 and 233, "past the end" of the 232
 * bytes. That was a miscount of the vendor's header: num_mflows is one word, and both are inside
 * the reply, at 228 and 229 - which the DWARF of this board's own usfp_rh.ko confirms.
 */
#define	OCTEP_PLATFORM_OFF_NUM_MFLOWS	SFOSNUM_PLAT_OFF_NUM_MFLOWS
#define	OCTEP_PLATFORM_INFO_WITH_SIZES	SFOSNUM_PLAT_LEN_21	/* max_conn, max_nhop, max_ipsec_sas */
#define	OCTEP_PLATFORM_INFO_WITH_MFLOWS	SFOSNUM_PLAT_LEN_22
#define	OCTEP_PLATFORM_OFF_NUM_PFS	SFOSNUM_PLAT_OFF_NUM_PFS
#define	OCTEP_PLATFORM_OFF_NUM_VFS	SFOSNUM_PLAT_OFF_NUM_VFS
#define	OCTEP_PLATFORM_OFF_NUM_WORKERS	SFOSNUM_PLAT_OFF_NUM_WORKERS
/* The minimum is what the decoder actually needs rather than the structure's nominal size. */
#define	OCTEP_PLATFORM_INFO_MIN		196

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
/*
 * The six bits above are the ones DWARF names. The handler wants EIGHT.
 *
 * `lif_fpop_add_update` in `usfp_rh.ko` loads the mask as a halfword from offset 0x10 of the
 * request and compares it against 0xff, not against the union of the named bits:
 *
 *     w28 = [x1, #0x10]          the mask
 *     w1  = (entry_state == 0)   the slot is free
 *     w0  = (w28 != 0xff)
 *     if (w1 && w0)  -> refuse   a NEW entry whose mask is not 0xff
 *     if (!w1 && !w0) -> refuse  an EXISTING entry whose mask IS 0xff
 *
 * So the rule already recorded - a new entry needs the full mask, an existing one a partial mask -
 * is right, and "full" is 0xff. Measured: LIF_ADD_UPDATE on an empty table returned rc 1 for 0x3f
 * and for each of the six bits alone, and rc 0 the moment the mask was 0xff.
 *
 * Bits 0x40 and 0x80 have no name here because DWARF does not give them one. They are required
 * anyway.
 */
#define	OCTEP_LIF_M_ALL			0x00ff
/* struct usfp_lif_entry's private tail: a one-byte mask at +14, df_enabled's bit at +15. */
#define	OCTEP_LIF_FP_PRIV_MASK_DF	0x01

/*
 * struct fw_state's fw_cfg word. The bit values are the vendor's own macro definitions, read out
 * of usfp_rh.ko's debug information rather than inferred, and FW_CFG_DEFAULT is defined there as
 * TCP_SEQ_CHK alone - so a coprocessor that has just started has offload OFF.
 *
 * Only OFFLOAD is given a name the driver uses. IPS and DROP_IF_IPS_OFF hand packets to an
 * intrusion-prevention stage this appliance does not run, and INJ_RECOVERY and FINTRACK belong to
 * the vendor's own connection tracking. FP_PKT_DUMP has the fast path send the host a copy of every
 * frame it forwards, as control message type 6 (octep_dp_mirror; measured 2026-10-25) - until then
 * this said "per-packet logging", which nobody had seen: it logs nothing, it sends. They are listed
 * so the word is readable, not so it can be assembled by guesswork.
 */
#define	OCTEP_FW_CFG_OFFLOAD		0x0001
#define	OCTEP_FW_CFG_TCP_SEQ_CHK	0x0002
#define	OCTEP_FW_CFG_IPS		0x0004
#define	OCTEP_FW_CFG_FINTRACK		0x0008
#define	OCTEP_FW_CFG_FP_PKT_DUMP	0x0010
#define	OCTEP_FW_CFG_INJ_RECOVERY	0x0020
#define	OCTEP_FW_CFG_DROP_IF_IPS_OFF	0x0800
#define	OCTEP_FW_CFG_DEFAULT		OCTEP_FW_CFG_TCP_SEQ_CHK

/* enum conn_state_type_t, read from usfp_rh.ko's DWARF */
#define	OCTEP_CONN_INVALID		0
#define	OCTEP_CONN_VALID		1
#define	OCTEP_CONN_RECLAIM_PENDING	2
#define	OCTEP_CONN_RECLAIMED		3
/*
 * struct usfp_fpop_req_conn_create: a four-byte index and struct usfp_conn_entry, which is 108
 * bytes with no tail padding - atomic 8, session 4, qos 8, tcp 60, nat 24, lock 4 - so the request
 * is 112. The handler refuses anything shorter than its own sizeof.
 *
 * It was 128 for a while, on the reasoning that the check is a less-than so a longer request is
 * harmless. It is harmless to the handler and misleading to a reader, and the right length is
 * knowable from the structure rather than by overshooting it.
 */
#define	OCTEP_CONN_REQ_LEN		112
/*
 * The handler reads a halfword, so the hardware would accept 0xffff. The driver accepts only the
 * union of the bits above - 0x083f, and the bits between them have no name anywhere in the
 * appliance's binaries - and refuses anything else rather than quietly trimming it, because an
 * unnamed bit in a firewall's configuration word is not something to set by accident. A span
 * would not do: 0x0fff would have let 0x040 through, and the first this project knew of it would
 * be whatever the fast path then did.
 */
#define	OCTEP_FW_CFG_NAMED		(OCTEP_FW_CFG_OFFLOAD | OCTEP_FW_CFG_TCP_SEQ_CHK | \
					 OCTEP_FW_CFG_IPS | OCTEP_FW_CFG_FINTRACK | \
					 OCTEP_FW_CFG_FP_PKT_DUMP | OCTEP_FW_CFG_INJ_RECOVERY | \
					 OCTEP_FW_CFG_DROP_IF_IPS_OFF)

static __inline int
octep_rpc_cmd_is_read(uint32_t cmd)
{

	return (cmd == OCTEP_RPC_CMD_PLATFORM_READ ||
	    cmd == OCTEP_RPC_CMD_SA_GET_STATS ||
	    cmd == OCTEP_RPC_CMD_CONN_GET_FP_TCP_STATE ||
	    (cmd >= OCTEP_RPC_CMD_LO_LIF_READ &&
	     cmd <= OCTEP_RPC_CMD_LO_WORKER_DF_CNT_READ));
}

/*
 * The DoS table, for a probe: does this coprocessor drop an address it is given?
 *
 * The vendor's enumeration has three DoS commands between the QoS ones and SA_ADD, so they are 27,
 * 28 and 29 counted either way - up from CONN_RECLAIM_FP at 16 or down from SA_ADD at 30. Their
 * payloads are the structures in dos_table.h: a policy (a packets-per-second ceiling, a type of
 * four bits and whether a source over it is blacklisted, then a blacklist duration for all of them),
 * and an entry (a family, a DoS type, an address in sixteen bytes, and a duration in seconds). What
 * is known of the types is two macros the XGS 136's helper module was built with: an even type is
 * a source and the next odd one its destination, and an entry lasts 30 seconds by default and 3,600
 * at most. No code that sends these was found anywhere - not in the GPL drop, not in the v22 host
 * module, not as a counter or a string of the v22 fast path - so they are reachable only through
 * rpc.cmd with rpc.allow_write, and what the far side answers is what this is for.
 *
 * ANSWERED, 2026-10-19, on the second XGS 3300 (v22): all three come back rc 0x8006, the
 * transport's errno, "the command was not dispatched" - the coprocessor has no handler for them -
 * in every form tried (types 0, 1 and 2, the address in either byte order, a policy of 1,000 packets
 * a second with blacklisting on), and a stream from the address entered ran on at full rate. This
 * firmware does not drop an address it is given. What it does drop is one connection: a microflow
 * with action 0 (OCTEP_MFLOW_ACTION_DROP). docs/measurements/dos-probe-2026-10-19.md.
 */
#define	OCTEP_RPC_CMD_DOS_SET_POLICY		27
#define	OCTEP_RPC_CMD_DOS_ADD_BLACKLIST		28
#define	OCTEP_RPC_CMD_DOS_CLEAR_BLACKLIST	29
#define	OCTEP_DOS_POLICY_LEN			12	/* struct usfp_dos_config */
#define	OCTEP_DOS_ENTRY_LEN			24	/* struct usfp_blacklist_entry */

/*
 * The seven QoS commands (discovery 7), 20 to 26 in the vendor's enumeration: add, remove and query a
 * meter, set and read a shaper rate, set and read a DWRR weight. Declared, and in the source no
 * handler is registered for them in the coprocessor's RPC module, as for the DoS ones; posted for the
 * probe only, each with up to seven u32 words from rpc.qos_args - the meter's request
 * (struct usfp_fpop_req_qos_add_meter: cir, cbs, eir, ebs, the radix point shift, the timestamp shift,
 * the scheduling priority) is the only layout the source gives.
 */
#define	OCTEP_RPC_CMD_QOS_ADD_METER		20
#define	OCTEP_RPC_CMD_QOS_REMOVE_METER		21
#define	OCTEP_RPC_CMD_QOS_QUERY_METER		22
#define	OCTEP_RPC_CMD_QOS_SET_SHAPER_RATE	23
#define	OCTEP_RPC_CMD_QOS_GET_SHAPER_RATE	24
#define	OCTEP_RPC_CMD_QOS_SET_DWRR_WEIGHT	25
#define	OCTEP_RPC_CMD_QOS_GET_DWRR_WEIGHT	26
#define	OCTEP_QOS_ARGS				7
#define	OCTEP_QOS_REQ_LEN			(OCTEP_QOS_ARGS * 4)

/*
 * The writes this driver will issue: the firewall state, which gates acceleration and whose revision
 * a ruleset reload bumps; a port mapping, which makes an ingress tag resolve to an interface; a
 * logical interface, which the wire-to-host gate finds; a security association; the three that
 * make up an accelerated flow - the next hop, the connection and the microflow - and a connection's
 * verdict changed in place (discovery 3, 2026-10-23). And the three DoS
 * commands and the seven QoS ones, for the probes above, which the driver itself never sends; and
 * the four counter clears, which rpc.counters_clear sends (discovery 13, 2026-10-25).
 * Everything else in the enumeration stays refused by number.
 */
static __inline int
octep_rpc_cmd_is_allowed_write(uint32_t cmd)
{

	return ((cmd >= OCTEP_RPC_CMD_QOS_ADD_METER && cmd <= OCTEP_RPC_CMD_QOS_GET_DWRR_WEIGHT) ||
	    cmd == OCTEP_RPC_CMD_DOS_SET_POLICY ||
	    cmd == OCTEP_RPC_CMD_DOS_ADD_BLACKLIST ||
	    cmd == OCTEP_RPC_CMD_DOS_CLEAR_BLACKLIST ||
	    cmd == OCTEP_RPC_CMD_LIF_ADD_UPDATE ||
	    cmd == OCTEP_RPC_CMD_PPORT_UPDATE ||
	    cmd == OCTEP_RPC_CMD_SA_ADD ||
	    cmd == OCTEP_RPC_CMD_SA_DEL ||
	    cmd == OCTEP_RPC_CMD_SA_REPLAY_UPDATE ||
	    cmd == OCTEP_RPC_CMD_SA_SEQ_UPDATE ||
	    cmd == OCTEP_RPC_CMD_SA_HOST_STAT_SYNC ||
	    (cmd >= OCTEP_RPC_CMD_LO_WORKER_DBG_CNT_CLR && cmd <= OCTEP_RPC_CMD_LO_WORKER_DF_CNT_CLR) ||
	    cmd == OCTEP_RPC_CMD_FW_STATE_REV_SET ||
	    cmd == OCTEP_RPC_CMD_FW_L3_FWD_STATE_REV_SET ||
	    cmd == OCTEP_RPC_CMD_FW_CFG_PARAMS_SET ||
	    cmd == OCTEP_RPC_CMD_CONN_CREATE_FP ||
	    cmd == OCTEP_RPC_CMD_CONN_RECLAIM_FP ||
	    cmd == OCTEP_RPC_CMD_CONN_MODIFY_FP_VERDICT ||
	    cmd == OCTEP_RPC_CMD_CONN_MODIFY_FP ||
	    cmd == OCTEP_RPC_CMD_ADD_LIVE_UID ||
	    cmd == OCTEP_RPC_CMD_DELETE_LIVE_UID ||
	    cmd == OCTEP_RPC_CMD_NHOP_PROGRAM ||
	    cmd == OCTEP_RPC_CMD_MFLOW_PROGRAM ||
	    cmd == OCTEP_RPC_CMD_FLOW_CREATE_FP);
}

static __inline const char *
octep_rpc_cmd_name(uint32_t cmd)
{

	switch (cmd) {
	case OCTEP_RPC_CMD_FW_STATE_REV_SET:		return ("FW_STATE_REV_SET");
	case OCTEP_RPC_CMD_FW_L3_FWD_STATE_REV_SET:	return ("FW_L3_FWD_STATE_REV_SET");
	case OCTEP_RPC_CMD_FW_CFG_PARAMS_SET:		return ("FW_CFG_PARAMS_SET");
	case OCTEP_RPC_CMD_LIF_ADD_UPDATE:		return ("LIF_ADD_UPDATE");
	case OCTEP_RPC_CMD_PPORT_UPDATE:		return ("PPORT_UPDATE");
	case OCTEP_RPC_CMD_NHOP_PROGRAM:		return ("NHOP_PROGRAM");
	case OCTEP_RPC_CMD_MFLOW_PROGRAM:		return ("MFLOW_PROGRAM");
	case OCTEP_RPC_CMD_FLOW_CREATE_FP:		return ("FLOW_CREATE_FP");
	case OCTEP_RPC_CMD_CONN_CREATE_FP:		return ("CONN_CREATE_FP");
	case OCTEP_RPC_CMD_CONN_RECLAIM_FP:		return ("CONN_RECLAIM_FP");
	case OCTEP_RPC_CMD_CONN_GET_FP_TCP_STATE:	return ("CONN_GET_FP_TCP_STATE");
	case OCTEP_RPC_CMD_CONN_MODIFY_FP_VERDICT:	return ("CONN_MODIFY_FP_VERDICT");
	case OCTEP_RPC_CMD_CONN_MODIFY_FP:		return ("CONN_MODIFY_FP");
	case OCTEP_RPC_CMD_ADD_LIVE_UID:		return ("ADD_LIVE_UID");
	case OCTEP_RPC_CMD_DELETE_LIVE_UID:		return ("DELETE_LIVE_UID");
	case OCTEP_RPC_CMD_QOS_ADD_METER:		return ("QOS_ADD_METER");
	case OCTEP_RPC_CMD_QOS_REMOVE_METER:		return ("QOS_REMOVE_METER");
	case OCTEP_RPC_CMD_QOS_QUERY_METER:		return ("QOS_QUERY_METER");
	case OCTEP_RPC_CMD_QOS_SET_SHAPER_RATE:	return ("QOS_SET_SHAPER_RATE");
	case OCTEP_RPC_CMD_QOS_GET_SHAPER_RATE:	return ("QOS_GET_SHAPER_RATE");
	case OCTEP_RPC_CMD_QOS_SET_DWRR_WEIGHT:	return ("QOS_SET_DWRR_WEIGHT");
	case OCTEP_RPC_CMD_QOS_GET_DWRR_WEIGHT:	return ("QOS_GET_DWRR_WEIGHT");
	case OCTEP_RPC_CMD_DOS_SET_POLICY:		return ("DOS_SET_POLICY");
	case OCTEP_RPC_CMD_DOS_ADD_BLACKLIST:		return ("DOS_ADD_BLACKLIST_ENTRY");
	case OCTEP_RPC_CMD_DOS_CLEAR_BLACKLIST:	return ("DOS_CLEAR_BLACKLIST");
	case OCTEP_RPC_CMD_SA_ADD:			return ("SA_ADD");
	case OCTEP_RPC_CMD_SA_DEL:			return ("SA_DEL");
	case OCTEP_RPC_CMD_SA_GET_STATS:		return ("SA_GET_STATS");
	case OCTEP_RPC_CMD_SA_REPLAY_UPDATE:		return ("SA_REPLAY_UPDATE");
	case OCTEP_RPC_CMD_SA_SEQ_UPDATE:		return ("SA_SEQ_UPDATE");
	case OCTEP_RPC_CMD_SA_HOST_STAT_SYNC:		return ("SA_HOST_STAT_SYNC");
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
	case OCTEP_RPC_CMD_LO_WORKER_DBG_CNT_CLR:	return ("LO_WORKER_DBG_CNT_CLR");
	case OCTEP_RPC_CMD_LO_WORKER_SYS_CNT_CLR:	return ("LO_WORKER_SYS_CNT_CLR");
	case OCTEP_RPC_CMD_LO_WORKER_PORT_CNT_CLR:	return ("LO_WORKER_PORT_CNT_CLR");
	case OCTEP_RPC_CMD_LO_WORKER_DF_CNT_CLR:	return ("LO_WORKER_DF_CNT_CLR");
	default:					return ("");
	}
}

struct octep_softc {
	device_t		 dev;
	struct mtx		 mtx;
	struct callout		 poll;
	struct sysctl_ctx_list	*sysctl_ctx;	/* this driver's sysctls, freed first in detach */

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

	/* The board's temperatures, written from userland for OPNsense's widget: octep_sysctl_temp() */
	int			 temp_dk[6];	/* deci-kelvin: the coprocessor's die, the inlet air, its memory,
					   the switch chip, the SFP+ cages' PHY, the expansion module's PHY */
	time_t			 temp_at[6];	/* time_uptime of the last write, 0 for never */

	/* what SDP says about the datapath ring budget - read, never written */
	uint64_t		 sdp_rinfo;
	uint32_t		 sdp_srn;
	uint32_t		 sdp_trs;
	uint32_t		 sdp_rpvf;
	uint32_t		 sdp_nvfs;
	uint32_t		 sdp_rings_mappable;	/* how many fit inside BAR0 */

	/* the SDP/EP-mode handshake; the datapath writes BAR0 too, a ring block at a time */
	struct callout		 sdp_poll;
	/*
	 * Link state, polled one port at a time.
	 *
	 * A front port's link lives on the far side and there is no interrupt for it, so it has to
	 * be asked for - and asking is a NetAgent round trip, which sleeps. That rules out a
	 * callout and is why this is a timeout_task on the thread taskqueue instead.
	 *
	 * One port per tick rather than all of them, because twelve round trips at once would put
	 * a burst of traffic on a control channel that a single misread wedges. At one a second a
	 * change is visible within twelve, which is faster than a cable gets replugged.
	 */
	struct timeout_task	 dp_link_task;
	uint32_t		 dp_link_next;
	int			 dp_link_running;
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
	uint32_t		 dp_siblings;	/* receive-only rings beside dp_ring */
	struct octep_dp_oq	 dp_sib[OCTEP_DP_SIBLINGS_MAX];
	uint32_t		 dp_time_threshold;
	uint32_t		 dp_credit_unit;	/* doorbell units per receive buffer */
	uint32_t		 dp_ack_cnts;		/* write R_OUT_CNTS back on service */
	uint32_t		 dp_resend;		/* write RESEND to R_OUT_CNTS when a visit ends (#361); 2 R_IN_CNTS too */
	uint64_t		 dp_resend_writes;	/* how many times it was written */
	uint32_t		 dp_istat_clear;	/* a pass clears its ring's R_OUT_INT_STATUS bit; 0 as the vendor (#361) */
	uint32_t		 dp_look_wake;		/* the look tries to wake a deaf ring before its visit (#361) */
	uint32_t		 dp_look_wake_us;	/* ... and waits this long for the handler */
	uint64_t		 dp_look_wakes;		/* tries made */
	uint64_t		 dp_look_woke;		/* ... after which the handler ran */
	uint64_t		 dp_msix_moved;		/* watchdog visits that found a ring's MSI-X entry not the kernel's */
	uint64_t		 dp_istat_reset;	/* R_OUT_INT_STATUS found set again right after a pass cleared it */
	uint64_t		 dp_istat_others;	/* passes that found other rings' bits in it, and left them */
	uint64_t		 dp_oq_left_up;		/* visits that left a ring empty with its status still up (#361) */
	uint32_t		 dp_intr_pkt;		/* R_OUT_INT_LEVELS packet threshold */
	uint32_t		 dp_oq_rsize;		/* entries published in R_OUT_SLIST_RSIZE */
	uint32_t		 dp_oq_grant;		/* first credit and ceiling, 0 to derive them */
	uint32_t		 dp_oq_wmark;		/* what the coprocessor's SDP_OUT_WMARK holds */
	uint32_t		 dp_oq_carry[OCTEP_DP_SIBLINGS_MAX + 1];	/* credit owed, not yet given */
	volatile int		 dp_oq_busy[OCTEP_DP_SIBLINGS_MAX + 1];
	uint32_t		 dp_oq_rd[OCTEP_DP_SIBLINGS_MAX + 1];	/* next buffer to read */
	int			 dp_oq_gap[OCTEP_DP_SIBLINGS_MAX + 1];	/* ticks when a gap was seen, 0 if none */
	int			 dp_oq_ahead[OCTEP_DP_SIBLINGS_MAX + 1];	/* ticks when a packet was seen with no count, 0 if none */
	uint32_t		 dp_oq_ahead_n[OCTEP_DP_SIBLINGS_MAX + 1];	/* ... and how many packets were there then */
	struct octep_dp_if	 dp_if[OCTEP_DP_IF_MAX];
	uint32_t		 dp_nif;
	uint32_t		 dp_if_port;		/* NetAgent port for the next if_add */
	uint32_t		 dp_if_iface;		/* LIF index for the next if_add */
	char			 dp_if_label[OCTEP_DP_LABEL_LEN];	/* name for the next if_add */
	uint64_t		 dp_rx_untagged;	/* arrived on no interface we carry */
	uint64_t		 dp_rx_untagged_other;	/* of those, not on the control channel's tag */
	/*
	 * The coprocessor's control messages read on arrival (octep_dp_cmsg_rx): how many of each
	 * type came, and for its connection statistics how many entries were applied to a connection
	 * of this table, how many named one that is gone or was made again since (another rev), and
	 * how many messages could not be read to their end.
	 */
	uint64_t		 dp_cmsg_rx[OCTEP_CMSG_TYPES];
	uint64_t		 dp_hw_applied;
	uint64_t		 dp_hw_stale;
	uint64_t		 dp_hw_bad;
	/*
	 * The fast path's copies of what it forwarded (type 6, octep_dp_mirror), from the receive
	 * path with no lock held: how many arrived for each direction, how many were given to a
	 * port's packet capture, how many found nothing listening there, named a tag no interface
	 * here carries, were too short to hold a frame, or found no mbuf.
	 */
	uint64_t		 dp_mirror[2];
	uint64_t		 dp_mirror_tapped;
	uint64_t		 dp_mirror_unheard;
	uint64_t		 dp_mirror_noport;
	uint64_t		 dp_mirror_bad;
	uint64_t		 dp_mirror_nomem;
	/*
	 * The head of each type's last message, from its Ethernet header on, for dp.cmsg_last: how the
	 * interface counts (type 0) are laid out is read here before anything relies on it.
	 */
	uint8_t			 dp_cmsg_last[OCTEP_CMSG_TYPES][OCTEP_CMSG_KEEP];
	uint16_t		 dp_cmsg_last_len[OCTEP_CMSG_TYPES];
	/*
	 * The heartbeat with the fast path (octep_hb_tick), under sc->mtx: the callout, the settings
	 * of heartbeat.*, where it stands, and what it has counted. The times are uptime.
	 */
	struct callout		 dp_hb_co;
	int			 dp_hb_running;		/* the callout is armed: set and cleared under mtx */
	int			 dp_hb_on;
	uint32_t		 dp_hb_period_ms;
	uint32_t		 dp_hb_late_ms;
	uint32_t		 dp_hb_dead_ms;
	int			 dp_hb_state;		/* OCTEP_HB_* */
	uint32_t		 dp_hb_seq;		/* the last sequence sent */
	sbintime_t		 dp_hb_sent_at;		/* when it was sent */
	sbintime_t		 dp_hb_answer_at;	/* when the last answer came, 0 for none yet */
	sbintime_t		 dp_hb_since;		/* when the heartbeat began, for the first answer */
	uint64_t		 dp_hb_sent;
	uint64_t		 dp_hb_answered;	/* answers carrying the sequence last sent */
	uint64_t		 dp_hb_unmatched;	/* answers carrying another: late, or not ours */
	uint64_t		 dp_hb_unsent;		/* beats the full ring left unsent */
	uint64_t		 dp_hb_lates;
	uint64_t		 dp_hb_deaths;
	uint64_t		 dp_hb_revivals;
	uint64_t		 dp_hb_rtt_us;		/* the last answer's time, sent to answered */
	uint64_t		 dp_hb_rtt_max_us;
	/*
	 * The coprocessor's per-port counts (type 0, LIF_STATS), kept per interface; dp_lif_apply 1
	 * also adds them to the interface's own counters (octep_dp_lif_count), 0 does not. And the
	 * connections whose counts were given to pf.
	 */
	uint32_t		 dp_lif_apply;
	uint64_t		 dp_lif_applied;
	uint64_t		 dp_lif_unknown;
	uint64_t		 dp_lif_vlan;	/* of them, a VLAN's LIF's (discovery 10) */
	/* FINs and RSTs held for pf to judge with the fast path's numbers (issue #679) */
	struct task		 dp_closing_task;
	struct mbufq		 dp_closing_q;
	uint32_t		 dp_closing_check;	/* dp.closing_check */
	int			 dp_closing_off;		/* the receive path is stopping: none held */
	uint64_t		 dp_closing_checked, dp_closing_passed;
	/* 1: the keep-alive adds each connection's counts to its pf states (octep_pf_count); 0 off */
	uint32_t		 dp_hw_to_pf;
	uint64_t		 dp_hw_pf_conns;
	uint64_t		 dp_hw_pf_states;
	/*
	 * The last received frame's whole prefix, kept so it can be read from userland.
	 *
	 * The far side writes OCTEP_RX_PREFIX_LEN bytes in front of every frame it delivers, and this
	 * driver reads four of them - the tag and a signature. The rest has never been looked at, and
	 * it is where the flow identifier would be if the coprocessor tells the host which flow a
	 * punted packet belongs to. That question is the whole of issue #185: the crypto path takes
	 * its SA index from a flow, a flow can only be programmed by (mflow_id, mflow_rev_num), and
	 * nothing else published says where a host learns those.
	 */
	uint8_t			 dp_rx_prefix[OCTEP_RX_PREFIX_LEN];
	uint64_t		 dp_rx_prefix_seq;	/* which frame it came from */
	/*
	 * The last punted frame's five-tuple, kept beside its prefix.
	 *
	 * Taken from the frame rather than from the prefix, because the prefix carries the flow's
	 * identity and not its addresses - and the identity is what to program, while the addresses
	 * are what pf can be asked about. Both halves of the one question, which is why they are
	 * captured in the same place and stamped with the same sequence number: a tuple read from a
	 * later frame than the slot it is paired with would program the wrong flow.
	 */
	struct octep_pf_tuple	 dp_rx_tuple;
	uint64_t		 dp_rx_tuple_seq;	/* 0 until a frame has been parsed */
	/*
	 * And that same frame's microflow slot, captured with it.
	 *
	 * Reading the slot from one sysctl and the tuple from another is a race, and not a
	 * theoretical one: on a link carrying a download, frames arrive between the two reads and
	 * the pair that comes back belongs to two different flows. A microflow programmed from one
	 * connection's slot with another connection's NAT mapping forwards somebody else's traffic
	 * to the wrong place, and the experiment looking for it reported nothing but noise. One
	 * read, one frame.
	 */
	uint32_t		 dp_rx_slot;
	uint32_t		 dp_rx_slot_rev;
	/*
	 * And the pport tag it arrived with, which names the front port it came in on.
	 *
	 * Captured here with the rest rather than looked up where the tag is decoded, because the
	 * decode happens a few lines later and only for a frame an interface claims - and this is
	 * wanted for the same frame the tuple and the slot came from, stamped with the same
	 * sequence number for the same reason.
	 */
	uint16_t		 dp_rx_tag;
	/*
	 * And the first bytes of the frame itself, which is the one thing no instrument on this
	 * appliance could reach.
	 *
	 * A frame the coprocessor forwards to a front port never enters the host, so nothing here
	 * can see what the fast path actually built - and the question that matters is exactly
	 * that: are the addresses translated, is the checksum fixed. Forwarding it to the host's
	 * own DPDK port instead makes it arrive on these rings, where the receive path copies this
	 * much of it before the tag lookup that would otherwise drop it as untagged.
	 *
	 * Sixty-four bytes: an Ethernet header, an IPv4 header with room for options, and a TCP
	 * header through its checksum and urgent pointer. Enough to answer the question and not
	 * enough to be a packet capture.
	 */
	uint8_t			 dp_rx_frame[64];
	uint32_t		 dp_rx_frame_len;
	/*
	 * And the same for the last frame this driver DROPPED because no interface owns its tag.
	 *
	 * dp_rx_frame holds whatever arrived most recently, which on a busy link is overwritten
	 * thousands of times a second - four hundred reads of it never caught one of the 285 frames
	 * that were known to be there. A frame the fast path forwarded to the host's own port is
	 * exactly a frame with a tag no interface owns, so capturing in that branch catches those
	 * and nothing else.
	 */
	uint8_t			 dp_rx_untag_frame[128];	/* far enough to reach a control message's own header */
	uint32_t		 dp_rx_untag_len;
	uint16_t		 dp_rx_untag_tag;
	/*
	 * Which tag to keep, because the untagged path has two users and only one is wanted.
	 *
	 * A control message arrives on tag 254 and is dropped here like anything else with no
	 * interface, and the link poll sends one every second - so a buffer that keeps the last
	 * untagged frame keeps a control message, which is what it did on the first attempt. Set
	 * this to the tag a next hop was pointed at and nothing else is captured. Zero keeps the
	 * first behaviour, any tag at all; OCTEP_DP_UNTAG_WANT_OTHER keeps any tag but the control
	 * channel's, for a frame whose tag is not known beforehand (discovery 22, Dragonfly's).
	 */
	uint32_t		 dp_rx_untag_want;
#define	OCTEP_DP_UNTAG_WANT_OTHER	0xffffffffu

	/*
	 * The accelerated flows, and whether to make them without being asked.
	 *
	 * dp_auto is off by default and stays that way until it has run by hand for a while. The
	 * candidate is the last punted frame the receive path thought worth offering: it is left
	 * here rather than acted on, because the receive path must not take a route lookup, and the
	 * link-poll task is already running and may.
	 */
	struct octep_conn	*dp_conn;		/* dp_conn_cap entries, allocated at attach */
	uint32_t		 dp_conn_cap;		/* hw.octep.flow_max, OCTEP_FLOW_DEFAULT unset */
	/*
	 * Where a connection is found: 1 << dp_conn_hbits chain heads, at least as many as the table
	 * has entries, by octep_conn_bucket of a tuple, each naming the first
	 * (index << 1 | direction) + 1 that hashes there. Each connection hangs in two, one per
	 * tuple. The indexes not in use, as a stack whose top is the lowest, so a quiet table uses
	 * its first entries as the array did. And the indexes in use, packed, for the passes: a
	 * connection that goes takes the last one's place, and dp_conn[i].live_pos says where each is.
	 */
	uint32_t		*dp_conn_head;
	uint32_t		 dp_conn_hbits;
	uint32_t		*dp_conn_free;
	uint32_t		 dp_conn_nfree;
	uint32_t		*dp_conn_live;
	uint32_t		 dp_conn_nlive;
	uint32_t		 dp_conn_used;		/* connections currently accelerated */
	uint32_t		 dp_conn_max;		/* dp_conn_cap, capped by the board */
	struct octep_nhop_ent	 dp_nhop[OCTEP_NHOP_MAX];
	uint32_t		 dp_nhop_used;
	uint32_t		 dp_nhop_max;		/* OCTEP_NHOP_MAX, capped by the board */
	uint32_t		 dp_nhop_hi;		/* one past the highest index ever handed out */
	uint64_t		 dp_flow_attached;	/* second directions attached to a live connection */
	uint64_t		 dp_flow_reclaimed;	/* connections the far side agreed to reclaim */
	uint64_t		 dp_flow_pending;	/* connections found RECLAIM_PENDING and taken out */
	/*
	 * The probe's other outcomes, which used to take a connection out without a trace (#287):
	 * a read that timed out (nothing done), a read that came back unusable, a far side that
	 * holds another revision for the index, a state that is neither VALID nor RECLAIM_PENDING;
	 * and the probes that found the connection fine. Plus the reclaims the far side refused.
	 */
	uint64_t		 dp_probe_valid;
	uint64_t		 dp_probe_timeout;
	uint64_t		 dp_probe_read_err;
	uint64_t		 dp_probe_rev_mismatch;
	uint64_t		 dp_probe_state_other;
	uint64_t		 dp_reclaim_refused;
	struct timeval		 dp_probe_last;		/* ppsratecheck state for the take-out line */
	int			 dp_probe_curpps;
	uint64_t		 dp_flow_tuple_mismatch; /* frames whose tuple was not the one pf implied */
	struct timeval		 dp_mismatch_last;	/* ppsratecheck state for the line that names one */
	int			 dp_mismatch_curpps;
	uint64_t		 dp_flow_rc_refused;	/* programming the far side answered with an error */
	uint64_t		 dp_nhop_shared;	/* next hops found already programmed */
	uint64_t		 dp_nhop_full;		/* times the next-hop table had no room */
	uint32_t		 dp_auto;
	/*
	 * dp.accel_dir: 0 accelerates either direction of a connection, 1 only the original
	 * direction, 2 only the reply. An instrument, not a policy: it exists so that one half of a
	 * connection can be offloaded while the other stays on the host, which is the measurement
	 * that tells the two halves' faults apart.
	 */
	/*
	 * Seconds a microflow this driver makes may stay idle before the fast path expires it -
	 * the mflow_timeout word of FLOW_CREATE_FP and MFLOW_PROGRAM. Issue #277 read the far
	 * side's handler as honouring the host's value only below the platform's ten seconds and
	 * expected ten to govern; the appliance expired a data-carrying connection's microflows
	 * between 53 and 63 seconds of silence with 60 sent, so this is a setting and not a
	 * constant, and dp.flow_timeout is how the next measurement changes it.
	 */
	uint32_t		 dp_flow_timeout;
	uint32_t		 dp_accel_dir;
	/*
	 * dp.accel_half: program a connection with only the direction in hand when the other has
	 * not been punted yet. Off by default, because a half-offloaded connection is handed back
	 * within a few frames; it exists for the measurements that want exactly that.
	 */
	uint32_t		 dp_accel_half;
	/*
	 * dp.pf_sloppy: mark both of pf's states for an accelerated connection sloppy, so pf stops
	 * judging TCP sequence numbers it can no longer see advance. On by default. It is a knob so
	 * that its effect can be measured on its own, with the same module, against the same traffic.
	 */
	uint32_t		 dp_pf_sloppy;
	/*
	 * dp.tcp_window (issue #679): 1, the default, has the fast path check the window of an
	 * offloaded TCP connection, filled from pf's numbers when it is made, and gives pf the fast
	 * path's numbers back when it is taken back - so the sloppy marking above lasts only as long
	 * as the hardware holds the connection and checks it. 0 sends every connection with the
	 * check off and leaves pf's states sloppy for good, as before.
	 *
	 * tcp_checked: made with the check; tcp_unchecked: TCP made without it - a rule asked for
	 * keep state (sloppy), or pf's numbers had stopped because the connection had been in
	 * hardware before and was not handed back with them; tcp_rewrites: not made at all, because
	 * pf modulates its sequence numbers or timestamps and the fast path cannot; tcp_handback:
	 * take-backs whose numbers pf took and whose sloppy mark was cleared; tcp_kept: take-backs
	 * left sloppy - the fast path had given the connection back already, or it was closing, or
	 * its numbers did not look like a window; tcp_lost: take-backs dropped for a full queue,
	 * left sloppy.
	 */
	uint32_t		 dp_tcp_window;
	uint32_t		 dp_tcp_revive;	/* dp.tcp_revive, see octep_conn_probe */
	uint64_t		 dp_tcp_revived;	/* checked connections revived without the check */
	uint64_t		 dp_tcp_from_segs;	/* windows made from the host's latest segments */
	uint64_t		 dp_tcp_seg_waits;	/* revivals that waited for a segment each way */
	uint64_t		 dp_tcp_checked;
	uint64_t		 dp_tcp_unchecked;
	uint64_t		 dp_tcp_rewrites;
	uint64_t		 dp_tcp_handback;
	uint64_t		 dp_tcp_kept;
	uint64_t		 dp_tcp_lost;
	/* take-backs waiting for the lock to be dropped, for octep_dp_handback_run */
	struct octep_handback	 dp_hb[OCTEP_HB_MAX];
	uint32_t		 dp_hb_n;
	/* dp.tcp_last: what the last take-back of a checked connection did with its window, and why */
	char			 dp_tcp_last[160];
	/* dp.tcp_segs_last: the last window made from segments, or why one could not be */
	char			 dp_tcp_segs_last[160];
	/*
	 * The kernel's IPsec offload contract - see octep_ipsec.c. The table is indexed by coprocessor
	 * index; ipsec_on is the gate the operator opens; ipsec_enc is enc0, held by reference, which
	 * decrypted frames are filtered on the way the kernel's own input path filters them.
	 */
	struct octep_sa		 ipsec_sa[OCTEP_SA_MAX];
	uint32_t		 ipsec_on;
	if_t			 ipsec_enc;
	uint64_t		 ipsec_sa_installed;
	int			 ipsec_installing;	/* outbound installs in hand: no detach */
	uint64_t		 ipsec_seq_overlap;	/* the kernel's counter reached a seed */
	uint64_t		 ipsec_install_us;	/* the last outbound install, gate to settled */
	uint64_t		 ipsec_settle_us;	/* of that, from the swap to settled */
	uint64_t		 ipsec_sa_let_go;	/* installs given up: cloned or let go meanwhile */
	uint64_t		 ipsec_sa_refused;
	int			 ipsec_esn_decrypt;	/* take an arriving ESN association: 1 unless hw.octep.ipsec_esn_decrypt=0 */
	uint64_t		 ipsec_esn_refused;	/* arriving ESN associations left to the kernel */
	uint64_t		 ipsec_sa_failed;
	uint64_t		 ipsec_sa_full;
	uint64_t		 ipsec_sa_removed;
	uint64_t		 ipsec_rx_done;
	uint64_t		 ipsec_rx_nosa;
	uint64_t		 ipsec_rx_nokey;
	uint64_t		 ipsec_rx_bad;
	uint64_t		 ipsec_rx_v6;
	uint64_t		 ipsec_rx_noenc;
	uint64_t		 ipsec_rx_blocked;
	uint64_t		 ipsec_rx_queuefail;
	uint64_t		 ipsec_tx_encrypt;
	uint64_t		 ipsec_tx_nosa;
	uint64_t		 ipsec_tx_bypass;
	uint64_t		 ipsec_flow_policy;
	/*
	 * Tunnel traffic on the flow path (issue 293). ipsec_flows is the operator's choice of how
	 * much of a tunnelled connection the coprocessor forwards by itself - 0 none, 1 the
	 * direction that leaves encrypted, 2 both - and the counters say what became of the
	 * connections a policy covers.
	 */
	uint32_t		 ipsec_flows;
	uint64_t		 ipsec_flow_made;	/* tunnelled connections put in hardware */
	uint64_t		 ipsec_flow_nosa;	/* covered, and the association is not mirrored */
	uint64_t		 ipsec_flow_shape;	/* covered, and not a shape this can carry */
	uint64_t		 ipsec_flow_clear;	/* a frame in the clear where the policy wants ESP */
	uint64_t		 ipsec_flow_wait;	/* waiting for the other direction */
	uint64_t		 ipsec_flow_repoint;	/* directions moved to a successor association */
	uint64_t		 ipsec_flow_gone;	/* connections taken out with their association */
	uint64_t		 ipsec_stat_polls;	/* SA_GET_STATS asked for an association */
	uint64_t		 ipsec_stat_pushed;	/* and counts handed to the kernel */
	uint64_t		 ipsec_stat_rebase;	/* readings that could not be the association's */
	/* The last non-zero reading of the engine's counters at each index: see struct octep_sa. */
	uint64_t		 ipsec_idx_bytes[OCTEP_SA_MAX];
	uint64_t		 ipsec_idx_packets[OCTEP_SA_MAX];
	uint32_t		 ipsec_sa_gen;
	uint32_t		 ipsec_poll_next;
	uint32_t		 ipsec_spgen_seen;	/* the policy generation the table was last checked at */
	uint64_t		 ipsec_flow_reval;	/* connections a policy change took out */
	uint64_t		 ipsec_flow_audit;	/* connections the once-a-second audit took out */
	uint64_t		 ipsec_flow_backoff;	/* connections left with the host after a hand-back */
	struct octep_ipsec_backoff ipsec_backoff[OCTEP_IPSEC_BACKOFF_MAX];
	struct octep_flow_hold	 *dp_hold;	/* OCTEP_HOLD_MAX places, dp.revive_hold */
	/*
	 * The outbound side - octep_ipsec_xf_output, which stands where the kernel's own cipher
	 * stood for a mirrored association, so nothing else can encrypt on it.
	 */
	uint64_t		 ipsec_out_taken;	/* packets handed to the coprocessor from there */
	uint64_t		 ipsec_out_orig;	/* calls passed on: the association is not mirrored */
	uint64_t		 ipsec_out_needfrag;	/* too big with DF set: answered, not sent */
	uint64_t		 ipsec_out_own_df;	/* this host's own, too big with DF: fragmented all the same (#644) */
	uint64_t		 ipsec_out_own_refused;	/* this host's own IPv6, too big: ENOBUFS (#644) */
	uint64_t		 ipsec_out_fragmented;	/* too big without DF: fragmented before the envelope */
	uint64_t		 ipsec_out_nonhop;	/* no next hop toward the tunnel's far end yet */
	uint64_t		 ipsec_out_drop;	/* leaving, a bundle, not IPv4 inside */
	uint64_t		 ipsec_tx_toobig;	/* an envelope the port's MTU or buffer cannot take */
	uint64_t		 dp_auto_made;		/* flows programmed without being asked */
	uint64_t		 dp_auto_gone;		/* flows invalidated when their state went */
	/*
	 * pf sees none of an accelerated connection's packets, so nothing restamps its state and
	 * it runs out under a connection that is alive: see octep_flow_keepalive.
	 */
	uint32_t		 dp_keepalive;		/* dp.keepalive */
	uint32_t		 dp_ka_next;		/* where the next pass starts */
	time_t			 dp_ka_hold;		/* no pass before this: the far side was silent */
	int			 dp_ka_silent;		/* and has been told about in the log */
	uint64_t		 dp_ka_reads;		/* microflows read back for it */
	uint64_t		 dp_ka_touched;		/* connections found in use: states restamped */
	uint64_t		 dp_ka_idle;		/* connections found quiet, or let go of */
	uint64_t		 dp_flow_unsettled;	/* not made yet: pf's states not on their long timer */
	uint64_t		 dp_auto_full;		/* times the table had no room */
	uint64_t		 dp_flow_forgot;	/* entries dropped because a reload discarded them */
	/*
	 * The candidates, and what became of them. dp_cand_evicted is the one worth watching: more
	 * busy tuples in a slot than it has places. dp_cand_clash counts writers that could not
	 * take a slot's word - another ring, the drain, the reader of the other direction or
	 * dp.cand_table was inside - and is the price of one word a slot.
	 */
	uint64_t		 dp_cand_pushed;	/* punted frames offered */
	uint64_t		 dp_cand_taken;		/* candidates the poll acted on */
	uint64_t		 dp_cand_known;		/* already in the flow table */
	uint64_t		 dp_cand_clash;		/* a writer found the slot busy and gave up */
	uint64_t		 dp_cand_lost;		/* an unread candidate was overwritten */
	uint64_t		 dp_cand_evicted;	/* ... by another tuple: slot full */
	uint64_t		 dp_cand_kept;		/* slots a discard could not empty */
	uint64_t		 dp_cand_aged;		/* make declined: other direction too old */
	uint32_t		 dp_cand_age;		/* dp.cand_age: seconds, 0 for no limit */
	/*
	 * Each slot on cache lines of its own, and the counters above it rather than after it:
	 * eight rings write these from eight cores, and two slots sharing a line would make them
	 * contend for no reason. 256 bytes times 64 slots is 16 KB on a softc that already holds
	 * kilobytes of ring state.
	 */
	struct octep_flow_cand_slot	 dp_cand[OCTEP_FLOW_CAND_SLOTS];
	/*
	 * How many connections in the table have a tuple that hashes to each of these. The receive
	 * path reads it without a lock to answer one question for nothing: can this punted frame
	 * belong to a connection we hold? Written under sc->mtx where a connection is made or
	 * freed. Zero is a certain no. Anything else is a perhaps, and only a reader for whom a
	 * wrong yes costs a wasted look may act on it: the kick, and the run it asks for, which
	 * then searches the table. The side entry took it for a yes about one connection, and was
	 * refused by every other connection that counted in the bucket.
	 *
	 * Sixty-five thousand places for at most two thousand tuples. There were four thousand,
	 * and with a thousand connections held two tuples in five that no connection holds read
	 * a perhaps - each of them a search of the whole table under sc->mtx by every run, until
	 * the poll had read the candidate. Three in a hundred now. Arithmetic, not a measurement;
	 * dp.fast_scans counts those searches that were for a candidate nobody had asked about.
	 * That was the whole table then. Since 2026-10-17 a search is a walk of one chain of
	 * dp_conn_head, which the same mix indexes, and the table holds 65,536 connections.
	 */
	uint16_t		 dp_conn_hot[OCTEP_CONN_HOT_MAX];
	struct taskqueue	*dp_fast_tq;
	struct task		 dp_fast_task;
	volatile u_int		 dp_fast_kick;		/* 1 while the task is queued or running */
	int			 dp_fast_hold;		/* ticks: no kick before this */
	uint32_t		 dp_fast;		/* dp.fast: the receive path kicks the task */
	uint32_t		 dp_revive;		/* dp.revive: rewrite in place, not take out */
	uint32_t		 dp_fast_make;		/* dp.fast_make: a run makes connections too */
	uint32_t		 dp_fast_make_log2;	/* frames in a row before it is asked, as a power */
	/*
	 * What the runs of this second may still make, and the candidate slots - one bit each -
	 * that are left to the poll until it has been round: a slot whose attempt between polls
	 * was refused, and both slots of a connection that has just been taken out. Written by the
	 * poll, which refills the one and clears the other, and by whoever spends or holds, with no
	 * lock of their own. The budget is spent with a compare and swap, so the refill is never
	 * written over by the last spending of the second before. What a race can still cost is a
	 * second with none when a run meets a full table as the poll refills, which is what a full
	 * table asks for; or - the bits being set with an atomic OR - one slot held a second longer
	 * than it need be.
	 */
	volatile u_int		 dp_fast_make_left;
	volatile uint64_t	 dp_fast_make_held[OCTEP_FLOW_CAND_SLOTS / 64];
	u_int			 dp_fast_make_per_sec;	/* dp.fast_make_per_sec */
	u_int			 dp_fast_make_per_run;	/* dp.fast_make_per_run */
	uint64_t		 dp_fast_kicks;
	uint64_t		 dp_fast_runs;
	uint64_t		 dp_fast_scans;		/* a run searched for nothing: nobody asked */
	uint64_t		 dp_flow_revived;	/* connections rewritten in place */
	uint64_t		 dp_revive_refused;	/* the rewrite was answered with a refusal */
	uint32_t		 dp_revive_hold;	/* dp.revive_hold: a streak ends in a wait */
	uint64_t		 dp_revive_held;	/* connections taken out at a streak's end */
	uint64_t		 dp_hold_skipped;	/* makes not tried while a wait ran */
	uint64_t		 dp_flow_closing;	/* given back with FIN or RST seen */
	uint64_t		 dp_fast_made;		/* connections made by a run, between polls */
	uint64_t		 dp_fast_make_tries;	/* attempts a run spent on a new connection */
	uint64_t		 dp_fast_make_spent;	/* asked for with the budget already spent */
	uint64_t		 dp_fast_make_refused;	/* attempts by a run that made nothing */
	uint64_t		 dp_flow_nostate;	/* not made: pf had no state for the tuple */
	uint64_t		 dp_flow_wait_other;	/* not made: no usable other direction */
	uint64_t		 dp_flow_nonhop;	/* not made: no next hop for the frame's direction */
	uint64_t		 dp_flow_v6_refused;	/* IPv6 not made: translated, or a policy covers it */
	uint64_t		 dp_flow_excluded;	/* times not made or taken out: the owner keeps it */
	uint64_t		 dp_flow_released;	/* connections handed back from the Flows page */
	struct octep_flow_held	 dp_held[OCTEP_FLOW_HELD_MAX];	/* under mtx */
	uint32_t		 dp_held_next;
	struct octep_flow_excl	 dp_excl[OCTEP_FLOW_EXCL_MAX];	/* under mtx */
	int			 dp_nexcl;
	char			 dp_excl_text[OCTEP_FLOW_EXCL_TEXT];
	char			 dp_release_result[160];
	char			 dp_verdict_result[160];
	char			 dp_luid_result[240];	/* the live-user instruments' last result */
	struct octep_live_user	 dp_lu[OCTEP_LIVE_USERS_MAX];	/* dp.live_users, under mtx */
	int			 dp_nlu;
	uint64_t		 dp_lu_added;	/* users added to the fast path's table */
	uint64_t		 dp_lu_deleted;	/* and deleted from it */
	uint64_t		 dp_lu_made;	/* connections made with a user */
	uint64_t		 dp_lu_set;	/* connections in hardware given their user in place */
	uint64_t		 dp_lu_out;	/* connections taken out because their user went */
	uint64_t		 dp_lu_failed;	/* commands about users the fast path did not answer 0 */
	uint32_t		 dp_mflow_action;	/* 2 makes flows with action IPS; else FWD */
	uint32_t		 dp_mflow_dscp_on;	/* 1: flows made with dp_mflow_dscp written into their frames */
	uint32_t		 dp_mflow_dscp;		/* discovery 8: the microflows' dscp_override_val */
	uint32_t		 dp_dscp_pf;		/* 1: flows carry pf's DSCP mark; 0: as before */
	uint64_t		 dp_flow_dscp;		/* made with pf's DSCP mark */
	uint64_t		 dp_flow_dscp_kept;	/* attempts refused: a mark the fast path cannot write */
	uint32_t		 dp_bridged;		/* 1: bridged connections made as bridged flows; 0: kept */
	/*
	 * VLAN subinterfaces (discovery 10). A front port's VLAN has a logical interface of its own on
	 * the fast path, (iface << 12) | vlan, which only the port's VLAN 0 had: without it a tagged
	 * frame goes to the host unlooked-up (FROM_WIRE_TO_KN_LIF_UNKNOWN_VLAN_L3_MODE). Each vlan(4)
	 * interface on a front port is given one here, checked every OCTEP_VLAN_LIF_CHECK seconds.
	 */
	uint32_t		 dp_vlan_flows;		/* 1: VLAN connections made in hardware; 0: kept */
	uint64_t		 dp_flow_vlan;		/* connections made with a VLAN in a direction */
	struct octep_vlan_lif	 dp_vlan_lif[OCTEP_VLAN_LIF_MAX];
	int			 dp_vlan_lif_n;
	time_t			 dp_vlan_lif_checked;
	uint64_t		 dp_vlan_lif_added;	/* logical interfaces made for a VLAN */
	uint64_t		 dp_vlan_lif_repaired;	/* ... made again: the fast path had lost them */
	uint64_t		 dp_vlan_lif_failed;	/* adds and updates the far side refused */
	uint64_t		 dp_vlan_lif_disabled;	/* ... switched off when their VLAN went */
	uint64_t		 dp_flow_bridged;	/* made as bridged flows */
	uint64_t		 dp_flow_bridged_kept;	/* attempts refused: left to the bridge */
	uint64_t		 dp_flow_dropped;	/* connections dropped by their verdict */
	uint64_t		 dp_drop_held;	/* sweep passes that kept a dropped one */
	uint64_t		 dp_flow_other_closing;	/* not made: the other direction is ending */
	uint64_t		 dp_rx_resync;	/* times a ring's read index was moved past a gap */
	uint64_t		 dp_oq_lapped;	/* passes that read a count larger than the ring */
	uint64_t		 dp_rx_short;	/* buffers stepped over that held something */
	uint64_t		 dp_rx_skipped;	/* empty buffers stepped over doing it */
	uint64_t		 dp_rx_spans;	/* packets that used more than one buffer */
	uint64_t		 dp_rx_span_bufs;	/* buffers they used after their first */
	uint64_t		 dp_rx_span_nobuf;	/* of those, dropped for want of a jumbo cluster */
	uint64_t		 dp_rx_long;	/* length words no packet could have */
	uint64_t		 dp_oq_behind;	/* packets taken that the count no longer held */
	uint64_t		 dp_oq_behind_runs;	/* passes that took them */
	uint32_t		 dp_oq_lose;	/* instrument: acknowledge this many and do not take them */
	uint32_t		 dp_oq_lose_ring;	/* ... on this ring */
	uint32_t		 dp_oq_lose_burst;	/* ... this many a pass at most, 0 for one */
	/*
	 * The head of the last packet that used more than one buffer, from the start of its
	 * first: the two words in front, the tag, the metadata and what follows. Long enough to
	 * reach a control message's own header, which is where its type is. Every ring's servicer
	 * writes it, so it is a sample and can be torn; the counters are what is relied on.
	 */
	uint8_t			 dp_rx_span_frame[128];
	uint32_t		 dp_rx_span_len;	/* its length word */
	uint32_t		 dp_rx_span_max;	/* the longest length word of any */
	uint32_t		 dp_rx_span_ring;
	uint16_t		 dp_rx_span_tag;
	uint64_t		 dp_credit_capped;	/* service passes whose credit the ceiling cut */
	int			 dp_msix_on;		/* vectors allocated and hooked */
	int			 dp_msix_spread;	/* each ring's vector bound to a CPU of its own */
	uint32_t		 dp_msix_bound;		/* how many were */
	int			 dp_msix_count;		/* what pci_alloc_msix() gave us */
	struct octep_dp_vec	 dp_vec[OCTEP_DP_SIBLINGS_MAX + 1];
	uint64_t		 dp_intr_taken;		/* handler entries, all rings */
	uint64_t		 dp_intr_drained;	/* passes beyond the first inside one hold of a ring */
	uint64_t		 dp_rxwd_runs;		/* watchdog entries that found work */
	volatile u_int		 dp_oq_owed[OCTEP_DP_SIBLINGS_MAX + 1];	/* a servicer was turned away */
	uint64_t		 dp_oq_declined;	/* servicers that found a ring held and left a note */
	uint64_t		 dp_oq_handed;		/* notes a holder found on its way out and honoured */
	uint64_t		 dp_oq_unseen;		/* passes that read a count and found no buffer */
	uint64_t		 dp_oq_bound;		/* visits that ended on their packet bound */
	volatile u_int		 dp_rxwd_soon;		/* the next tick was asked for */
	volatile u_int		 dp_oq_asked[OCTEP_DP_SIBLINGS_MAX + 1];	/* ... and by this ring */
	int			 dp_oq_seen[OCTEP_DP_SIBLINGS_MAX + 1];	/* ticks when a visit last let go */
	/*
	 * The ring as the last visit left it (#361), from what that visit's last pass read, so at no
	 * cost in register reads: the status after the pass's clear, the count it read, when the
	 * visit let go, and how many times the ring's handler had run by then. A stall prints them.
	 */
	uint64_t		 dp_oq_left_istat[OCTEP_DP_SIBLINGS_MAX + 1];
	uint64_t		 dp_oq_left_cnts[OCTEP_DP_SIBLINGS_MAX + 1];
	int			 dp_oq_left_at[OCTEP_DP_SIBLINGS_MAX + 1];
	uint64_t		 dp_oq_left_vcount[OCTEP_DP_SIBLINGS_MAX + 1];
	uint64_t		 dp_rxwd_rescues;	/* rings the watchdog found work in unasked */
	uint64_t		 dp_rxwd_stalls;	/* ... whose first pass held a stall's worth */
	/*
	 * The last few stalls, as they were counted: which ring, how many the first pass took,
	 * how many ticks since a servicer was last inside it, and when. A count that moves once
	 * in twenty million frames says nothing about which ring or what was waiting; this does.
	 */
	struct octep_dp_stall {
		int		 at;		/* ticks */
		uint32_t	 ring;
		uint32_t	 first;		/* what the first pass took */
		uint32_t	 idle;		/* ticks since a servicer let go of the ring */
		uint32_t	 visit;		/* what the whole visit took */
		uint64_t	 istat;		/* R_OUT_INT_STATUS read before the visit (#361) */
		uint64_t	 cnts;		/* R_OUT_CNTS read before the visit */
		uint64_t	 levels;	/* R_OUT_INT_LEVELS read before the visit */
		uint32_t	 vec_index;	/* the ring's MSI-X table index */
		uint32_t	 msix_mask;	/* the entry's mask bit, read before the visit */
		uint32_t	 msix_pending;	/* the PBA bit, read before the visit */
		uint64_t	 vec_count;	/* handler entries for the ring, so far */
		uint64_t	 left_istat;	/* the ring as the visit before left it */
		uint64_t	 left_cnts;
		uint32_t	 left_ago;	/* ticks between that and the stall */
		uint64_t	 left_vcount;	/* handler entries then */
		uint64_t	 msix_addr;	/* the ring's MSI-X entry as the table holds it */
		uint32_t	 msix_data;
		uint32_t	 msix_match;	/* 1 the kernel's address and data, 0 not, 2 unknown */
		uint32_t	 wake;		/* the look's try (dp.look_wake), 0 none */
		uint32_t	 wake_pending;	/* the PBA bit after it */
		uint64_t	 wake_vcount;	/* handler entries after it */
	}			 dp_stall_log[OCTEP_DP_STALL_LOG];
	uint32_t		 dp_stall_pos;		/* next entry to write */
	/* The look's visits, kept apart so that they do not push the stalls out (#361). */
	struct octep_dp_stall	 dp_look_log[OCTEP_DP_STALL_LOG];
	uint32_t		 dp_look_pos;
	uint32_t		 dp_rxwd_ticks;		/* watchdog period, 0 to take the default */
	int			 dp_rxwd_on;		/* the watchdog callout is live */
	/*
	 * The look between the watchdog's sweeps (#361): every dp_rxpeek_ticks the head buffer of
	 * each ring, in host memory, and a visit for a ring whose head has held the same packet since
	 * the last look with nobody inside - an interrupt the block raised and the host never had.
	 */
	uint32_t		 dp_rxpeek_ticks;	/* 0: no look, every run a sweep */
	int			 dp_rxwd_last;		/* ticks at the last sweep */
	uint32_t		 dp_rxpeek_head[OCTEP_DP_SIBLINGS_MAX + 1];	/* head + 1 holding a packet at the last look */
	uint64_t		 dp_rxpeek_found;	/* rings the look visited */
	uint64_t		 dp_rxpeek_taken;	/* ... and what those visits took */
	int			 dp_rxwd_prev;		/* ticks at the watchdog's last run */
	uint32_t		 dp_rxwd_gap_max;	/* the longest from one run's start to the next's */
	volatile int		 dp_rx_quiesce;	/* servicing suspended while the ifnets change */
	uint64_t		 dp_quiesce_waits;	/* quiesces that had to wait for a servicer */
	uint32_t		 dp_quiesce_max_us;	/* the longest such wait, microseconds */
	uint64_t		 dp_quiesce_refused;	/* quiesces a ring outlasted: what they were for was refused */
	uint32_t		 dp_ring_held;	/* dp.hold_ring's ring + 1, 0 when none is held (a test) */
	/*
	 * Memory goes back only once the coprocessor is known to have stopped with it (#962): a
	 * ring's IDLE bit after its enables are cleared, the management target's answer to
	 * GOING_DOWN, the RPC rings' done counts. What cannot be known is kept, never freed.
	 */
	uint64_t		 dp_kept_bytes;		/* kept since attach, for want of that */
	uint32_t		 dp_quiet_us_max;	/* the longest a ring took to go idle in the last stop */
	int			 dp_quiet_absent;	/* this teardown found the coprocessor off the bus */
	int			 dp_keep_test;		/* dp.keep_test: the next stop takes its rings as busy (a test) */
	int			 dp_stop_check;		/* dp.stop_check: stops look for writes after the stop (a test) */
	char			 dp_stop_check_res[192];	/* what the last such stop found */
	struct callout		 dp_rxwd;		/* services every ring, interrupt or not */
	uint32_t		 dp_time_threshold_set;
	uint32_t		 dp_pkind;
	uint32_t		 dp_dport;
	uint32_t		 dp_port_tag;
	uint32_t		 dp_iq_prod;		/* next instruction slot */
	struct octep_dma	 dp_txbuf;	/* the synthetic test frame, one buffer */
	struct octep_dma	 dp_txbufs;	/* IQ_DESCS buffers, one per instruction slot */

	/*
	 * The coprocessor gone from under the driver (#768): a count of the RPC ring, or the
	 * management link's target status, read as all ones - which none of them can be, and which is
	 * what every read of a device that has left the bus returns, as the coprocessor does while its
	 * own watchdog resets it. Set once (octep_gone), cleared only by a reload. From then on no
	 * command is posted or waited for, the management poll and the receive watchdog stop,
	 * nothing is transmitted, and no register or doorbell of it is written (since 2026-10-26): until 2026-10-26 a command posted then waited its two seconds with
	 * mtx held, counted in steps rather than by the clock while each read of the absent device
	 * itself stalled up to the root port's completion timeout (260 to 900 ms on the XGS 3300), and
	 * every thread wanting mtx - each frame sent on ring 0 among them - spun behind it until the
	 * host stopped and its watchdog restarted it.
	 */
	volatile u_int		 gone;
	u_int			 gone_at;	/* time_uptime when it was set; recover.sh's mark */

	/* the RPC facility: one command buffer, and what the last command returned */
	struct octep_dma		 rpc_cmd;
	uint32_t		 rpc_ready;
	uint32_t		 rpc_revision;
	uint32_t		 rpc_dbell;
	uint32_t		 rpc_shared;
	uint32_t		 rpc_desc_off;
	uint32_t		 rpc_desc_count;
	/*
	 * The high-priority ring - rings[0] of the state block - for the writes that put a flow in
	 * hardware, posted: no completion written back and nothing waited for, as the vendor's host
	 * posts FLOW_CREATE_FP, CONN_CREATE_FP and MFLOW_PROGRAM (rpc_post in its rpc_provider.c)
	 * on as many high rings as the platform block's rpc_rings says - one on the XGS 3300. It is
	 * configured once that block has been read; until then, on a board that asks for none, and
	 * with rpc.hi_post at 0, every command takes the low ring and is waited for.
	 */
	struct octep_dma	 rpc_hi_buf;
	uint32_t		 rpc_hi_want;		/* the platform's rpc_rings, 0 or 1 here */
	uint32_t		 rpc_hi_ready;
	uint32_t		 rpc_hi_desc_off;
	uint32_t		 rpc_hi_dbell;
	uint32_t		 rpc_hi_post;		/* rpc.hi_post */
	uint64_t		 rpc_hi_posted;		/* our copy of rings[0].posted */
	uint64_t		 rpc_hi_done;		/* rings[0].done when last read */
	uint64_t		 rpc_hi_posts;
	uint64_t		 rpc_hi_full;
	uint64_t		 rpc_hi_drains;
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
	/* The next-hop entry a NHOP_PROGRAM carries. See OCTEP_RPC_CMD_NHOP_PROGRAM. */
	uint32_t		 rpc_nhop_index;
	uint8_t			 rpc_nhop_dmac[6];
	uint8_t			 rpc_nhop_smac[6];
	uint32_t		 rpc_nhop_ethtype;
	uint32_t		 rpc_nhop_vlan;
	uint32_t		 rpc_nhop_tag;
	uint32_t		 rpc_nhop_flags;
	uint32_t		 rpc_nhop_iface;
	uint32_t		 rpc_nhop_mtu;
	uint32_t		 rpc_nhop_resolved;
	uint32_t		 rpc_nhop_rev;
	/* The microflow a MFLOW_PROGRAM carries. See OCTEP_RPC_CMD_MFLOW_PROGRAM. */
	uint32_t		 rpc_mflow_id;
	uint32_t		 rpc_mflow_rev;
	uint32_t		 rpc_mflow_valid;
	uint32_t		 rpc_mflow_action;
	uint32_t		 rpc_mflow_dscp;	/* 0x100 | the value to write into every frame, or 0 */
	uint32_t		 rpc_mflow_dir;
	uint32_t		 rpc_mflow_state;
	uint32_t		 rpc_mflow_brctl;
	uint32_t		 rpc_mflow_conn;
	uint32_t		 rpc_mflow_conn_rev;
	uint32_t		 rpc_mflow_fw_rev;
	uint32_t		 rpc_mflow_nhop;
	uint32_t		 rpc_mflow_nhop_rev;
	uint32_t		 rpc_mflow_sa;
	uint32_t		 rpc_mflow_sa_rev;
	uint32_t		 rpc_mflow_timeout;
	/* The second direction a FLOW_CREATE_FP carries, and its valid mask. */
	uint32_t		 rpc_flow_len;
	uint32_t		 rpc_flow_valid;
	uint32_t		 rpc_mflow2_id;
	uint32_t		 rpc_mflow2_rev;
	uint32_t		 rpc_mflow2_valid;
	uint32_t		 rpc_mflow2_dir;
	uint32_t		 rpc_mflow2_nhop;
	uint32_t		 rpc_mflow2_nhop_rev;
	uint32_t		 rpc_mflow2_sa;		/* the second direction's association, */
	uint32_t		 rpc_mflow2_sa_rev;	/* which is never the first's: see octep_conn_mf */

	/*
	 * struct usfp_fpop_req_conn_create: a 32-bit index then struct usfp_conn_entry entire.
	 * The entry's bit-fields, little-endian and least-significant first, are rev_num:16,
	 * rsvd:3, do_dnat:1, do_snat:1, verdict:2, ips_vf:7, state:2 - so CONN_VALID lands in the
	 * top two bits.
	 */
	uint32_t		 rpc_conn_idx;
	uint32_t		 rpc_conn_rev;
	uint32_t		 rpc_conn_verdict;
	uint32_t		 rpc_luid;		/* ADD/DELETE_LIVE_UID, CONN_MODIFY_FP's user */
	uint32_t		 rpc_conn_modify;	/* CONN_MODIFY_FP's field bits; 0 is the user alone */
	uint32_t		 rpc_conn_qos[2];	/* and its two QoS words */
	uint32_t		 rpc_conn_state;
	uint32_t		 rpc_conn_session;
	uint32_t		 rpc_conn_luid;		/* FLOW_CREATE_FP's live user */

	/*
	 * struct usfp_nat_info, and where in the connection entry to put it.
	 *
	 * Addresses and ports are staged in network order, the way they sit in a frame and the way
	 * pf keeps them, so a value read out of dp.pf_state can be written here unchanged.
	 */
	uint32_t		 rpc_conn_nat_off;
	uint32_t		 rpc_conn_snat;
	uint32_t		 rpc_conn_dnat;
	uint32_t		 rpc_conn_orig_src;
	uint32_t		 rpc_conn_orig_dst;
	uint32_t		 rpc_conn_orig_sport;
	uint32_t		 rpc_conn_orig_dport;
	uint32_t		 rpc_conn_nat_src;
	uint32_t		 rpc_conn_nat_dst;
	uint32_t		 rpc_conn_nat_sport;
	uint32_t		 rpc_conn_nat_dport;
	/*
	 * The connection's TCP block, borrowed from the table entry by octep_rpc_flow_create and put
	 * back to off after the post, so a FLOW_CREATE_FP made by hand never carries one nobody
	 * typed. Zero leaves the block as zeros, the window unchecked.
	 */
	uint32_t		 rpc_conn_tcp_on;
	uint8_t			 rpc_conn_tcp[OCTEP_CONN_TCP_LEN];

	/* struct fw_state, which the three firewall-state commands write one field of each. */
	uint32_t		 rpc_fw_cfg;
	uint32_t		 rpc_fw_rev;
	uint32_t		 rpc_fw_l3_rev;

	/*
	 * Bumping the firewall revision, which is how a ruleset reload discards every offloaded
	 * flow at once. See octep_rpc_fw_rev_bump_task.
	 *
	 * It is a task and not a sysctl that posts inline, because the writer is a ruleset reload
	 * holding a file lock that every other reload queues behind, and a posted command waits up
	 * to OCTEP_RPC_CMD_WAIT_MS for its reply. The sysctl enqueues and returns.
	 *
	 * The three counters are the whole report. A reload that cannot bump - no handshake yet,
	 * which is the common case for the first of the three reloads in a boot - must be silent,
	 * because a line printed once per reload is printed forever.
	 */
	struct task		 rpc_bump_task;
	uint64_t		 rpc_fw_rev_bumps;	/* posted, and the far side took it */
	uint64_t		 rpc_fw_rev_bump_fail;	/* posted and refused */
	uint64_t		 rpc_fw_rev_bump_early;	/* asked before the facility was up */
	/*
	 * Set while the bump, or the keep-alive, is posting a command whose failure has already
	 * been reported once, so that octep_rpc_post says nothing. A command a person asked for
	 * always speaks.
	 */
	int			 rpc_quiet;
	/*
	 * Set while this driver is posting a write of its own, so that octep_rpc_post does not
	 * consult rpc.allow_write for it.
	 *
	 * THIS EXISTS BECAUSE THE OBVIOUS ALTERNATIVE LEAKS. The three internal writers used to open
	 * rpc.allow_write and put it back afterwards, which looks safe because it is done under the
	 * lock - and is not, because the sysctl that a script writes is a plain integer and takes no
	 * lock at all. Measured: the bring-up fires a revision bump and then shuts the gate, the task
	 * ran in between, saved the gate as open and restored it open, and the appliance came up from
	 * a cold boot with the write gate standing open. A flag says what is actually true - that
	 * this particular post is the driver's own - and leaves the operator's knob alone.
	 */
	int			 rpc_internal;
	/*
	 * The board's own table sizes, read once from the platform block when the facility is
	 * up. Zero until then, which the users treat as "not known, use the host's bound".
	 */
	int			 rpc_plat_learned;
	uint32_t		 rpc_plat_def_mflow_to;
	uint32_t		 rpc_plat_max_conn;
	uint32_t		 rpc_plat_max_nhop;
	uint32_t		 rpc_plat_num_mflows;
	uint32_t		 rpc_plat_max_sas;	/* rpc.board_sas: the associations it holds */
	char			 rpc_plat_name[OCTEP_PLATFORM_NAME_LEN];	/* rpc.platform */
	/*
	 * Whose software the coprocessor runs, and so how its commands are numbered (sfosnum.h):
	 * found with the platform block, which is looked for at 36 and on. Until it is found the
	 * shift is unknown and no command from 36 on is sent.
	 */
	int			 rpc_cmd_shift;		/* rpc.cmd_shift: -1 unknown */
	int			 rpc_sfos;		/* rpc.sfos: 21, 22, 23; 0 unknown */
	uint32_t		 rpc_plat_len;		/* rpc.platform_len */
	char			 rpc_plat_version[OCTEP_PLATFORM_NAME_LEN];	/* rpc.platform_version */
	int			 rpc_cmd_as_written;	/* rpc.cmd_as_written: hand posts unmoved */
	int			 rpc_probe_wire;	/* > 0: this post goes as this number, no payload */
	int			 rpc_last_wire;		/* the number the last post went as; -1 not sent */
	uint64_t		 rpc_refused;		/* posted writes the far side answered with rc != 0 */
	uint64_t		 rpc_sa_retries;	/* SA_ADD posted again after an encoded -EAGAIN */
	/*
	 * And set by detach before it drains the task, because the sysctl that enqueues it is still
	 * live during detach - the tree belongs to the device and newbus frees it afterwards. Without
	 * it, a write landing between the drain and the free would post into a command buffer that is
	 * gone.
	 */
	int			 rpc_bump_stop;

	/*
	 * The security association this driver can install, field for field as
	 * struct usfp_fpop_req_sa_add defines it. The keys here are test material and nothing
	 * else: a sysctl is readable by root and visible in a core dump, so a production key has
	 * no business passing through one.
	 */
	uint32_t		 rpc_sa_idx;	/* the index; the handle a microflow names is this plus one */
	uint32_t		 rpc_sa_rev;	/* the revision, which the lookup compares */
	uint32_t		 rpc_sa_lif;
	uint32_t		 rpc_sa_spi;
	uint32_t		 rpc_sa_dir;	/* 0 outbound/encrypt, 1 inbound/decrypt */
	uint32_t		 rpc_sa_cipher;	/* 2 AES128, 4 AES256 */
	uint32_t		 rpc_sa_cimode;	/* 1 CBC, 4 CTR */
	uint32_t		 rpc_sa_hash;	/* 8 SHA256_128, 11 GF128_128 for GCM */
	uint32_t		 rpc_sa_mode;	/* 0 transport, 1 tunnel */
	uint32_t		 rpc_sa_proto;
	uint32_t		 rpc_sa_arw;	/* anti-replay window enable */
	uint32_t		 rpc_sa_esn;	/* extended sequence numbers: ctrl bit 18, ext_seq */
	uint32_t		 rpc_sa_win;
	uint32_t		 rpc_sa_free;	/* SA_DEL: free the entry as well as clearing it */
	/* the DoS probe's fields: see OCTEP_RPC_CMD_DOS_SET_POLICY */
	uint32_t		 rpc_dos_family;	/* AF_INET or AF_INET6, as dos_addr was given */
	uint8_t			 rpc_dos_addr[16];	/* network order; IPv4 in the first four */
	uint32_t		 rpc_dos_type;
	uint32_t		 rpc_dos_duration;	/* seconds */
	uint32_t		 rpc_qos_args[OCTEP_QOS_ARGS];	/* the QoS probe's words (rpc.qos_args) */
	uint32_t		 rpc_dos_max_pps;
	uint32_t		 rpc_dos_bl;		/* blacklist a source over the ceiling */
	uint32_t		 rpc_dos_addr_le;	/* write the IPv4 address little-endian */
	uint32_t		 rpc_sa_src[4];
	uint32_t		 rpc_sa_dst[4];
	uint8_t			 rpc_sa_key[32];
	uint8_t			 rpc_sa_authkey[64];
	uint32_t		 rpc_sa_opt;	/* overhead type in bits 24..31, udp_enable bit 22 */
	uint32_t		 rpc_sa_nat_sport;	/* host order; the builder writes them big-endian */
	uint32_t		 rpc_sa_nat_dport;
	uint64_t		 rpc_sa_seq;	/* the far side's counter starts at this plus one */
	uint64_t		 rpc_sa_hbytes;	/* SA_HOST_STAT_SYNC: the host's count of bytes */
	uint64_t		 rpc_sa_hpkts;	/* and of packets */
	uint32_t		 rpc_commands;
	uint32_t		 rpc_timeouts;
	uint32_t		 rpc_last_cmd;
	uint32_t		 rpc_last_sa_idx;	/* the index the last SA command named; rpc.sa_idx may have moved on */
	/* How long the far side takes to answer a command, in microseconds of waiting. */
	uint32_t		 rpc_wait_last;
	uint32_t		 rpc_wait_max;
	uint64_t		 rpc_wait_sum;
	uint64_t		 rpc_wait_n;
	uint16_t		 rpc_last_rc;
	uint16_t		 rpc_last_seed;
	uint16_t		 rpc_last_len;
	uint8_t			 rpc_last_done;
	int			 rpc_last_error;
	uint8_t			 rpc_last_reply[OCTEP_RPC_DATA_MAX_SIZE];		/* one frame, for the test transmit */
	uint64_t		 dp_tx_posted;
	struct octep_dp_txq	 dp_txq[OCTEP_DP_TXQ_MAX];
	uint32_t		 dp_ntxq;		/* transmit rings besides ring 0 in use */
	/* The inspector's function's ring, both ways (discovery 4): see octep_dp_ips_rx. */
	struct octep_dp_txq	 dp_ips_txq;
	int			 dp_ips_on;
	uint32_t		 dp_ips_ring;
	uint32_t		 dp_ips_loop;		/* 1: hand every frame straight back */
	uint32_t		 dp_ips_pkind;
	uint64_t		 dp_ips_rx;
	uint64_t		 dp_ips_back;
	uint64_t		 dp_ips_dropped;
	if_t			 dp_ips_ifp;		/* oxips0, where the inspector reads them */
	struct octep_ips_ent	*dp_ips_tab;
	struct mtx		 dp_ips_tab_mtx;
	uint64_t		 dp_ips_up;		/* given to oxips0 */
	uint64_t		 dp_ips_passed;		/* came back through oxips0 */
	uint64_t		 dp_ips_nomd;		/* came back with no metadata kept */
	uint64_t		 dp_ips_unkeyed;	/* no key could be read from it */
	uint32_t		 dp_ips_new;		/* 1: make new connections inspected */
	uint64_t		 dp_ips_depth;		/* bytes let through before forward */
	uint64_t		 dp_ips_made;		/* connections made inspected */
	uint64_t		 dp_ips_cut;		/* of those, set to forward since */
	uint32_t		 dp_ips_pend[OCTEP_IPS_PEND];
	uint32_t		 dp_ips_npend;
	struct task		 dp_ips_done_task;
	uint32_t		 dp_txq_want;		/* hw.octep.txq */
	uint64_t		 dp_tx_iq_full;	/* frames refused for want of an input-ring slot */
	uint64_t		 dp_rx_seen;

	/* NetAgent - the control plane, reads only so far */
	int			 nwa_ready;
	/*
	 * One transaction in the NetAgent window at a time, and sc->mtx cannot be that on its own
	 * - octep_nwa_wait() drops it on every tick, so the mutex is not held across the request
	 * and its reply. The mutex protects this flag, the flag serialises the window, and the
	 * wait sleeps on its address so a caller that is queued can be woken. See issue #224 and
	 * docs/findings/nwa-transaction-lock.md.
	 */
	int			 nwa_busy;
	uint32_t		 nwa_body;	/* request offset inside the window */
	uint32_t		 nwa_max_req;
	uint64_t		 nwa_commands;
	uint64_t		 nwa_timeouts;
	/*
	 * Issue #227. A transaction ends by writing ACK and returns without waiting for the target
	 * to go idle, so the next caller can find the window still held and octep_nwa_release()
	 * acknowledges it a second time. These measure that second acknowledge - how often, and how
	 * long from ACK to idle - and nwa_ack_wait switches on the wait inside the transaction that
	 * would make the second one unnecessary, measured the same way. Whether the target lets go
	 * in microseconds or in milliseconds decides which shape is right, and until these existed
	 * that was a guess.
	 */
	uint64_t		 nwa_releases;		/* windows found held when a transaction began */
	uint64_t		 nwa_release_slow;	/* of those, how many outlived the spin and slept */
	uint32_t		 nwa_release_us_last;	/* ACK to idle, microseconds, the last time */
	uint32_t		 nwa_release_us_max;
	int			 nwa_ack_wait;		/* 1: wait for idle after our own ACK, and measure it */
	uint64_t		 nwa_ack_waits;
	uint64_t		 nwa_ack_slow;
	uint32_t		 nwa_ack_us_last;
	uint32_t		 nwa_ack_us_max;
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
	/*
	 * The last DISCOVER's answer, kept apart from nwa_last_*. The link poll asks the window every
	 * round and overwrote that record the instant a discover released it, so the coprocessor's
	 * list of ports - every port it has, an expansion module's included, each with the tag the
	 * host has to use for it - could not be read from the host at all. nwa.ports formats it.
	 */
	int			 nwa_disc_error;
	int			 nwa_disc_words;
	uint32_t		 nwa_disc_marker;
	uint32_t		 nwa_disc_status;
	uint32_t		 nwa_disc_len;
	uint32_t		 nwa_disc_reply[OCTEP_NWA_MAX_WORDS];
	/* what the next request will carry; set by sysctl, because the tags are not known */
	uint32_t		 nwa_req_op;
	uint32_t		 nwa_req_sub;
	uint32_t		 nwa_req_port;
	uint32_t		 nwa_req_param;
	uint32_t		 nwa_req_param2;
	uint32_t		 dp_meta_mode;
	uint32_t		 dp_meta_mode_set;
	uint32_t		 dp_sib_base;
	uint64_t		 dp_rx_done;
	uint8_t			 dp_dst_mac[6];
	uint32_t		 dp_peek_off;
	uint32_t		 dp_cmd;
	uint32_t		 dp_cmd_p1;
	uint32_t		 dp_cmd_p2;
	uint32_t		 dp_cmd_p3;
	uint32_t		 dp_cmd_more;
	uint32_t		 dp_cmd_fsz;
	uint32_t		 dp_meta_b0;
	/*
	 * Which interface may ask the coprocessor to encrypt, and with which association.
	 * dp_sa_if is an interface index and -1 means none, which is the default and the only
	 * safe default: the metadata byte that asks for encryption is read for every frame, and a
	 * handle that names no association is a frame dropped rather than a frame sent.
	 */
	int			 dp_sa_if;
	uint32_t		 dp_sa_idx;
	/*
	 * A metadata template, which is how a field whose offset is in doubt gets settled from
	 * the command line rather than from a rebuild. When tpl_len is non-zero these bytes are
	 * copied over the metadata block after it is cleared and before the association handle
	 * is written, so a sweep can put one byte at a time anywhere in the 64 and watch which
	 * counter moves.
	 */
	uint8_t			 dp_meta_tpl[64];
	uint32_t		 dp_meta_tpl_len;
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
uint64_t octep_sdp_publish_rinfo(struct octep_softc *sc);
int	octep_sdp_enable_vfs(struct octep_softc *sc, uint16_t nvfs);
void	octep_sdp_read_rinfo(struct octep_softc *sc, int verbose);
void	octep_sdp_add_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
	    struct sysctl_oid_list *top);
void	octep_sdp_handshake_stop(struct octep_softc *sc);

/* octep_dp.c */
uint32_t octep_dp_service(struct octep_softc *sc);
void	octep_dp_refresh_int_levels(struct octep_softc *sc);
int	octep_dp_start(struct octep_softc *sc);
/* Both may refuse, and a caller that drops the refusal does not build (octep_dp.c, __result_use_check). */
int	octep_dp_stop(struct octep_softc *sc) __result_use_check;
int	octep_dp_rx_quiesce_or_refuse(struct octep_softc *sc, const char *what) __result_use_check;
void	octep_dp_table_free(struct octep_softc *sc);
void	octep_dp_add_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
	    struct sysctl_oid_list *top);

/* octep_nwa.c */
int	octep_nwa_probe(struct octep_softc *sc, int verbose);
int	octep_nwa_port_mac(struct octep_softc *sc, uint32_t port, uint8_t *mac);
int	octep_nwa_port_link(struct octep_softc *sc, uint32_t port, int *up);
int	octep_nwa_port_filter(struct octep_softc *sc, uint32_t port, int on);
int	octep_nwa_port_promisc(struct octep_softc *sc, uint32_t port, int on);
int	octep_rpc_lif_fwd(struct octep_softc *sc, uint32_t iface, uint32_t vlan, uint32_t fwd);
int	octep_rpc_lif_mtu(struct octep_softc *sc, uint32_t iface, uint32_t vlan, uint32_t mtu);
int	octep_rpc_lif_set(struct octep_softc *sc, uint32_t iface, uint32_t vlan, const uint8_t *mac,
	    uint32_t mtu, uint32_t fwd, uint32_t offload_dis, uint32_t mask, uint16_t *rc);
int	octep_rpc_sa_install(struct octep_softc *sc, const struct octep_sa *s);
int	octep_rpc_sa_remove(struct octep_softc *sc, uint32_t idx, int free_entry);
int	octep_rpc_sa_stats(struct octep_softc *sc, uint32_t idx, uint64_t *bytes,
	    uint64_t *packets);
struct octep_pf_state;
struct octep_nhop;
/*
 * The flow programmers, one command each, all called with sc->mtx held. Each returns an errno:
 * ETIMEDOUT when the far side did not answer, EIO when it answered with a non-zero rc, which this
 * driver sees because its descriptors are posted without the POST flag that would hide it.
 */
int	octep_rpc_nhop_program(struct octep_softc *sc, uint32_t idx, uint8_t rev,
	    const struct octep_nhop *nh);
int	octep_rpc_flow_create(struct octep_softc *sc, const struct octep_conn *c, uint32_t mask);
int	octep_rpc_mflow_set(struct octep_softc *sc, const struct octep_conn *c, int dir,
	    uint32_t state);
int	octep_rpc_conn_reclaim(struct octep_softc *sc, const struct octep_conn *c,
	    uint8_t *seq);
int	octep_rpc_conn_tcp_state(struct octep_softc *sc, uint32_t idx, uint16_t rev, uint8_t *seq);
int	octep_rpc_live_uid(struct octep_softc *sc, int add, uint32_t uid);
int	octep_rpc_conn_luid(struct octep_softc *sc, const struct octep_conn *c, uint32_t uid);
int	octep_rpc_conn_qos(struct octep_softc *sc, const struct octep_conn *c, uint32_t which,
	    uint32_t qos_orig, uint32_t qos_reply);
int	octep_rpc_conn_peek(struct octep_softc *sc, uint32_t idx, uint32_t *flags, uint32_t *luid);
int	octep_rpc_luid_read(struct octep_softc *sc, uint8_t *out, int len);
int	octep_rpc_conn_verdict(struct octep_softc *sc, const struct octep_conn *c,
	    uint32_t verdict);
int	octep_ips_wanted(struct octep_softc *sc, const struct octep_conn *c);
int	octep_rpc_conn_read(struct octep_softc *sc, uint32_t idx, uint32_t *state,
	    uint32_t *rev, uint32_t *retrans);
int	octep_rpc_mflow_peek(struct octep_softc *sc, uint32_t slot, uint32_t *valid,
	    uint32_t *rev, uint32_t *stamp);
void	octep_rpc_platform_learn(struct octep_softc *sc);
void	octep_gone(struct octep_softc *sc, const char *what);
int	octep_rpc_quiet(struct octep_softc *sc);
int	octep_nwa_port_speed(struct octep_softc *sc, uint32_t port, uint32_t *mbit);
/*
 * What one NetAgent transaction returned, as the caller's own copy. nwa_last_* in the softc is the
 * operator's record of the LAST transaction and is overwritten by the next; a caller that read it
 * after dropping the lock could be reading another caller's reply, which is issue #224. The first
 * words are all any caller needs.
 */
#define	OCTEP_NWA_REPLY_WORDS	8
struct octep_nwa_reply {
	int		words;
	uint32_t	marker;
	uint32_t	status;
	uint32_t	len;
	uint32_t	data[OCTEP_NWA_REPLY_WORDS];
};
int	octep_nwa_request(struct octep_softc *sc, uint32_t op, uint32_t sub, uint32_t port,
	    uint32_t param, uint32_t param2, struct octep_nwa_reply *out);
void	octep_nwa_add_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
	    struct sysctl_oid_list *top);

/* octep_mgmt.c */
void	octep_rpc_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
	    struct sysctl_oid *node);
int	octep_dma_alloc(struct octep_softc *sc, struct octep_dma *d, bus_size_t size,
	    bus_size_t align, const char *what);
void	octep_dma_free(struct octep_dma *d);
void	octep_dma_keep(struct octep_softc *sc, struct octep_dma *d);
int	octep_mgmt_start(struct octep_softc *sc);
void	octep_mgmt_stop(struct octep_softc *sc);
void	octep_if_detach(struct octep_softc *sc);
int	octep_mgmt_if_create(struct octep_softc *sc);
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
