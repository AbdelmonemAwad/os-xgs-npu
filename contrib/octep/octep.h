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

/*
 * The softc below holds a timeout_task and each front port an ifmedia, both by value, so the
 * definitions have to be here rather than in whichever file happens to include this one.
 */
#include <sys/taskqueue.h>
#include <net/if_media.h>

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
 * descriptor, sixteen descriptors at a time. See OCTEP_DP_CREDIT_UNIT.
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
#define	OCTEP_DP_OQ_INTR_PKT	8
#define	OCTEP_DP_OQ_INTR_TIME	2		/* microseconds */

/*
 * How many times one handler entry may go round the ring before it gives up and leaves the rest
 * to the watchdog.
 *
 * One pass is not enough, and that was the whole defect: a pass takes at most RSIZE packets, and
 * the block raises its interrupt when R_OUT_CNTS crosses the level from below rather than while
 * it sits above it. A download arriving faster than one pass can drain leaves the count above the
 * level with nothing left to re-cross it, and the receive path stops for good - measured on this
 * appliance with 1,532 packets sitting in host memory, eight MSI-X vectors frozen, and the first
 * hop 100% unreachable until the rings were drained by hand.
 *
 * Sixteen rounds is 4,096 packets at the default RSIZE, which is more than a gigabit line
 * delivers between two interrupts, and still a bound rather than a promise.
 */
#define	OCTEP_DP_OQ_DRAIN_ROUNDS	16

/*
 * The receive watchdog's period, in ticks.
 *
 * The drain loop above is the fix; this is the net under it. Nothing else in this driver ever
 * looks at an output ring - there was no periodic receive path at all - so a single lost edge took
 * the appliance's WAN away until the module was reloaded. Twenty times a second costs one register
 * read per armed ring and bounds that failure at 50 ms instead of forever.
 */
#define	OCTEP_DP_RXWD_TICKS		(hz / 20)

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
 * The vendor ships 2048 input and 4096 output descriptors. This driver uses 256 of each for a first
 * bring-up: it must be a power of two (the index arithmetic requires it, not the hardware), and 256
 * output descriptors at 1536 bytes is 384 KiB of coherent memory rather than 6 MiB. Raise it once
 * something has run.
 */
#define	OCTEP_DP_IQ_DESCS	256
#define	OCTEP_DP_OQ_DESCS	256

/*
 * How many doorbell units one receive buffer is credited with, and why the credit needs a ceiling.
 *
 * Sixteen is what makes the block write at all. Granted the ring in units of one - 256 - it fetches
 * one batch of sixteen descriptors, writes one packet and stops, which is the one-packet-per-ring
 * fault this constant was introduced to cure; measured again on 2026-10-02 with 400 frames offered
 * and 8 delivered. Granted 256 x 16 it runs.
 *
 * But the block does not spend sixteen per packet. It fetches the scatter list sixteen descriptors
 * at a time and debits the doorbell by one per descriptor - measured under traffic from the
 * register's two halves, the low half the credit and the high half the block's byte offset into the
 * list, sixteen bytes a descriptor:
 *
 *	~235 packets on one ring, sixteen returned for each: offset +224 descriptors, credit +3536
 *	~234 packets on one ring, one returned for each:     offset +240 descriptors, credit -6
 *
 * So returning sixteen per packet added fifteen credits for every packet a ring carried, without
 * limit: the ring that carried a day's downloads held 1,026,112. With that much the block wrote over
 * buffers the host had not taken; the frames in them were lost without a drop counter moving, and a
 * download stopped dead while ping and SSH carried on. The credit is therefore never allowed above
 * the grant the ring was armed with - see octep_dp_oq_service.
 *
 * The unit is a tunable because it is a measurement and not a datasheet reading.
 */
/*
 * Room for the front-port label an interface is given at attach - "XGS front port Port1" and the
 * like. The kernel has no size of its own for this: if_allocdescr() takes whatever is asked for,
 * and ifconfig's own buffer is far larger than anything meant here.
 */
#define	OCTEP_DP_DESCR_LEN	32

#define	OCTEP_DP_CREDIT_UNIT	16

/*
 * How long a ring may sit with its next buffer empty and a later one full before the host stops
 * waiting for it, in ticks. The block writes in order and a DMA is visible within microseconds, so
 * a gap that lasts this long is a buffer that will never be written - see octep_dp_oq_resync.
 */
#define	OCTEP_DP_RESYNC_TICKS	2

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
#define	OCTEP_DP_IF_MTU_MAX	(OCTEP_DP_BUF_SIZE - OCTEP_RX_PREFIX_LEN - 14)

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
#define	  OCTEP_RX_MD_VALID_OFF	(OCTEP_RX_META_OFF + 4)
#define	  OCTEP_RX_MD_FLOW_OFF	(OCTEP_RX_META_OFF + 8)
#define	  OCTEP_RX_MD_SA_OFF	(OCTEP_RX_META_OFF + 16)
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
 */
#define	OCTEP_DP_IF_MAX		13

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
	uint16_t		 tag;
	uint32_t		 nwaport;	/* the NetAgent port, which is not always the tag */
	int			 link;		/* -1 unknown, 0 down, 1 up - polled, see below */
	uint32_t		 speed;	/* Mbit/s while the link is up, 0 when it is not */
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
 * enum rpc_cmd_type. Named here are the commands this driver issues and no others: every read,
 * and the seven writes listed by octep_rpc_cmd_is_allowed_write below. The rest of that
 * enumeration - the flow and connection commands, the next-hop, QoS and DoS ones, and the IPsec
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
 * THE ACTION VALUE IS NOT KNOWN. MF_ACT_DROP, MF_ACT_FWD, MF_ACT_IPS and MF_ACT_AUX are used in
 * the vendor's source and defined in a tree that is not in the GPL drop, and they are in none of
 * the binaries this project holds. The order they appear in the vendor's own printer suggests
 * 0, 1, 2, 3 and that is inference, not knowledge - so rpc.mflow_action has no default and the
 * caller must say. Two counters settle it without guessing twice:
 * FROM_WIRE_DROP_MFLOW_ACTION is a value the fast path understood and refused, and
 * FROM_WIRE_DROP_MFLOW_UNSUPPORTED_ACTION is one it did not understand at all.
 */
#define	OCTEP_RPC_CMD_MFLOW_PROGRAM		8
#define	OCTEP_RPC_CMD_MFLOW_INVALIDATE		9
#define	OCTEP_MFLOW_REQ_LEN		32
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
 * The two security-association writes. The whole SA block is 30 to 35 and the read is 42; this
 * driver issues the two that install and remove one, and reads the table back with 42.
 * docs/families/octeon-tx-rpc.md has the request layout and the algorithm numbers.
 */
#define	OCTEP_RPC_CMD_SA_ADD			30
#define	OCTEP_RPC_CMD_SA_DEL			31
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
#define	OCTEP_PLATFORM_OFF_NUM_PFS	232
#define	OCTEP_PLATFORM_OFF_NUM_VFS	233
/*
 * The reply is 232 bytes on this board, measured - so num_pfs and num_vfs, which a reading of the
 * vendor's header put at 232 and 233, are past its end and are not in it. The three names and the
 * four bytes at 192 are, and those are the ones the bounds come from. The minimum is what the
 * decoder actually needs rather than the structure's nominal size.
 */
#define	OCTEP_PLATFORM_INFO_MIN		196
#define	OCTEP_PLATFORM_INFO_WITH_PFS	234

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
 * intrusion-prevention stage this appliance does not run, FP_PKT_DUMP turns on per-packet logging
 * in the fast path, and INJ_RECOVERY and FINTRACK belong to the vendor's own connection tracking.
 * They are listed so the word is readable, not so it can be assembled by guesswork.
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
 * struct usfp_fpop_req_conn_create: a four-byte index and struct usfp_conn_entry, whose fields add
 * to 104. The handler refuses anything shorter than its own sizeof, which is the compiler's and so
 * carries the entry's tail padding; 108 was refused and 112 is the size with that padding.
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
	    (cmd >= OCTEP_RPC_CMD_LO_LIF_READ &&
	     cmd <= OCTEP_RPC_CMD_LO_WORKER_DF_CNT_READ));
}

/*
 * The writes this driver will issue: the firewall state, which gates acceleration; a port mapping,
 * which makes an ingress tag resolve to an interface; a logical interface, which the wire-to-host
 * gate finds; and a security association. Everything else in the enumeration - the flow,
 * connection, next-hop, QoS and DoS commands - stays refused by number.
 */
static __inline int
octep_rpc_cmd_is_allowed_write(uint32_t cmd)
{

	return (cmd == OCTEP_RPC_CMD_LIF_ADD_UPDATE ||
	    cmd == OCTEP_RPC_CMD_PPORT_UPDATE ||
	    cmd == OCTEP_RPC_CMD_SA_ADD ||
	    cmd == OCTEP_RPC_CMD_SA_DEL ||
	    cmd == OCTEP_RPC_CMD_FW_STATE_REV_SET ||
	    cmd == OCTEP_RPC_CMD_FW_L3_FWD_STATE_REV_SET ||
	    cmd == OCTEP_RPC_CMD_FW_CFG_PARAMS_SET ||
	    cmd == OCTEP_RPC_CMD_CONN_CREATE_FP ||
	    cmd == OCTEP_RPC_CMD_NHOP_PROGRAM ||
	    cmd == OCTEP_RPC_CMD_MFLOW_PROGRAM);
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
	case OCTEP_RPC_CMD_CONN_CREATE_FP:		return ("CONN_CREATE_FP");
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
	uint32_t		 dp_intr_pkt;		/* R_OUT_INT_LEVELS packet threshold */
	uint32_t		 dp_oq_rsize;		/* entries published in R_OUT_SLIST_RSIZE */
	uint32_t		 dp_oq_grant;		/* first credit, 0 to derive from the unit */
	volatile int		 dp_oq_busy[OCTEP_DP_SIBLINGS_MAX + 1];
	uint32_t		 dp_oq_rd[OCTEP_DP_SIBLINGS_MAX + 1];	/* next buffer to read */
	int			 dp_oq_gap[OCTEP_DP_SIBLINGS_MAX + 1];	/* ticks when a gap was seen, 0 if none */
	struct octep_dp_if	 dp_if[OCTEP_DP_IF_MAX];
	uint32_t		 dp_nif;
	uint32_t		 dp_if_port;		/* NetAgent port for the next if_add */
	uint32_t		 dp_if_iface;		/* LIF index for the next if_add */
	uint64_t		 dp_rx_untagged;	/* arrived on no interface we carry */
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
	uint64_t		 dp_rx_resync;	/* times a ring's read index was moved past a gap */
	uint64_t		 dp_rx_skipped;	/* empty buffers stepped over doing it */
	uint64_t		 dp_credit_capped;	/* service passes whose credit the ceiling cut */
	int			 dp_msix_on;		/* vectors allocated and hooked */
	int			 dp_msix_count;		/* what pci_alloc_msix() gave us */
	struct octep_dp_vec	 dp_vec[OCTEP_DP_SIBLINGS_MAX + 1];
	uint64_t		 dp_intr_taken;		/* handler entries, all rings */
	uint64_t		 dp_intr_drained;	/* extra service rounds inside a handler */
	uint64_t		 dp_rxwd_runs;		/* watchdog entries that found work */
	uint32_t		 dp_rxwd_ticks;		/* watchdog period, 0 to take the default */
	int			 dp_rxwd_on;		/* the watchdog callout is live */
	volatile int		 dp_rx_quiesce;	/* servicing suspended while the ifnets change */
	uint64_t		 dp_quiesce_waits;	/* quiesces that had to wait for a servicer */
	uint32_t		 dp_quiesce_max_us;	/* the longest such wait, microseconds */
	struct callout		 dp_rxwd;		/* services every ring, interrupt or not */
	uint32_t		 dp_time_threshold_set;
	uint32_t		 dp_pkind;
	uint32_t		 dp_dport;
	uint32_t		 dp_port_tag;
	uint32_t		 dp_iq_prod;		/* next instruction slot */
	struct octep_dma	 dp_txbuf;	/* the synthetic test frame, one buffer */
	struct octep_dma	 dp_txbufs;	/* IQ_DESCS buffers, one per instruction slot */

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

	/*
	 * struct usfp_fpop_req_conn_create: a 32-bit index then struct usfp_conn_entry entire.
	 * The entry's bit-fields, little-endian and least-significant first, are rev_num:16,
	 * rsvd:3, do_dnat:1, do_snat:1, verdict:2, ips_vf:7, state:2 - so CONN_VALID lands in the
	 * top two bits.
	 */
	uint32_t		 rpc_conn_idx;
	uint32_t		 rpc_conn_rev;
	uint32_t		 rpc_conn_verdict;
	uint32_t		 rpc_conn_state;
	uint32_t		 rpc_conn_session;

	/* struct fw_state, which the three firewall-state commands write one field of each. */
	uint32_t		 rpc_fw_cfg;
	uint32_t		 rpc_fw_rev;
	uint32_t		 rpc_fw_l3_rev;

	/*
	 * The security association this driver can install, field for field as
	 * struct usfp_fpop_req_sa_add defines it. The keys here are test material and nothing
	 * else: a sysctl is readable by root and visible in a core dump, so a production key has
	 * no business passing through one.
	 */
	uint32_t		 rpc_sa_idx;	/* the index, and the handle a frame names */
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
	uint32_t		 rpc_sa_win;
	uint32_t		 rpc_sa_free;	/* SA_DEL: free the entry as well as clearing it */
	uint32_t		 rpc_sa_src[4];
	uint32_t		 rpc_sa_dst[4];
	uint8_t			 rpc_sa_key[32];
	uint8_t			 rpc_sa_authkey[64];
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
	uint64_t		 dp_tx_iq_full;	/* frames refused for want of an input-ring slot */
	uint64_t		 dp_rx_seen;

	/* NetAgent - the control plane, reads only so far */
	int			 nwa_ready;
	/*
	 * One transaction in the NetAgent window at a time, and sc->mtx cannot be that on its own
	 * - octep_nwa_wait() drops it on every tick, so the mutex is not held across the request
	 * and its reply. The mutex protects this flag, the flag serialises the window, and the
	 * wait sleeps on its address so a caller that is queued can be woken. See issue #224 and
	 * docs/nwa-transaction-lock.md.
	 */
	int			 nwa_busy;
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
void	octep_dp_stop(struct octep_softc *sc);
void	octep_dp_add_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
	    struct sysctl_oid_list *top);

/* octep_nwa.c */
int	octep_nwa_probe(struct octep_softc *sc, int verbose);
int	octep_nwa_port_mac(struct octep_softc *sc, uint32_t port, uint8_t *mac);
int	octep_nwa_port_link(struct octep_softc *sc, uint32_t port, int *up);
int	octep_nwa_port_filter(struct octep_softc *sc, uint32_t port, int on);
int	octep_nwa_port_promisc(struct octep_softc *sc, uint32_t port, int on);
int	octep_rpc_lif_fwd(struct octep_softc *sc, uint32_t iface, uint32_t vlan, uint32_t fwd);
int	octep_nwa_port_speed(struct octep_softc *sc, uint32_t port, uint32_t *mbit);
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
