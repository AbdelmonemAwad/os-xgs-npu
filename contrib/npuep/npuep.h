/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * What npuep.c hands to the pieces built on top of it.
 *
 * The facility layer finds where each facility lives by reading the map the coprocessor
 * publishes, so a consumer must never work its offset out from a constant. It is given one.
 */

#ifndef _NPUEP_H_
#define _NPUEP_H_

/*
 * One facility's window, already located and range-checked by npuep.c.
 *
 * `res` is the BAR resource the window is inside and `off` is the absolute offset within it, so
 * a consumer reads and writes with bus_space_*_4(rman_get_bustag(res), rman_get_bushandle(res),
 * off + field). Nothing here is a pointer into the window, deliberately: this is MMIO on the
 * far side of a PCIe link, and a plain C dereference of it is how the vendor driver does it and
 * is not how FreeBSD should.
 */
/*
 * The smallest mvmgmt window that can hold struct pci_net_shared_cfg. A facility smaller than
 * this cannot be what it claims to be, and the size came from the coprocessor, so it is checked
 * where the map is read rather than trusted downstream.
 */
#define	NPUEP_MGMT_MIN_SIZE	0x28

struct npuep_facility {
	device_t	 dev;
	struct resource	*res;		/* the BAR this facility's window is in */
	struct resource	*bar2;		/* BAR2, where the GIU's other functions' configuration is */
	bus_size_t	 off;		/* absolute offset of the window within it */
	bus_size_t	 size;		/* how much of it is ours */
	bus_dma_tag_t	 parent_tag;	/* parent for any DMA the consumer allocates */

	/*
	 * The MSI-X vectors npuep allocated for THIS facility. The coprocessor is told a vector
	 * id per queue and signals us by writing the message itself, so a consumer must hand it
	 * a number npuep actually owns - inventing one points the device at a table entry
	 * belonging to something else.
	 */
	int		 first_msix;	/* index of this facility's first vector */
	int		 nmsix;		/* how many it has */

	/*
	 * And, for the one facility that has to interrupt the target rather than be interrupted
	 * by it: who owns the register window the doorbells live in, and which doorbell is this
	 * facility's. Everything else here polls and leaves both unused.
	 */
	struct npuep_softc *parent;
	int		 dbell;
};

/*
 * The smallest giu window that can hold struct agnic_config_mem. Same rule as the mvmgmt
 * minimum: the size came from the coprocessor, so it is checked where the map is read.
 */
#define	NPUEP_GIU_MIN_SIZE	0x400

/* The smallest nwa window that can hold the mailbox header, the request and its reply. */
#define	NPUEP_NWA_MIN_SIZE	0x100

/*
 * The fourth facility, and the last one this driver does not speak.
 *
 * The vendor's host reaches it through usfp_firewall.ko, which its module dependencies show
 * sitting on mv_armada_drv - the facility layer - alongside the three modules whose facilities we
 * already implement. Its own boot message is `usfp_firewall_cmsg_init: real_dev: ... mv-pcimux0`,
 * so it is a control-message channel attached to the trunk interface. On the evidence, this is
 * what tells the coprocessor's fastpath where to send a frame, which is the one thing still
 * standing between a configured datapath and a frame arriving on it.
 *
 * A megabyte, one host-to-target doorbell, one DMA engine. Nothing is written to it here: this
 * only captures the window so it can be read.
 */
#define	NPUEP_RPC_MIN_SIZE	0x100

/*
 * The control facility's map, from the vendor's facility_conf.h. We already read its cookie and
 * handshake; what was never read is the rest, and the rest is how a host interrupts the target.
 *
 *	struct ctrl_map {
 *		u32 cookie;
 *		u32 handshake;
 *		u32 h2t_dbell_cnt;
 *		struct dbell_msg h2t_dbell_msg[];   // u64 address, u32 data - twelve bytes each
 *	};
 *
 * To ring doorbell n the host writes that entry's `data` to that entry's `address`. Every other
 * facility we have implemented polls, so this has never been needed - but the control-message
 * channel has one host-to-target doorbell and no way back, so it is needed there.
 */
#define	CTRL_H2T_DBELL_CNT	0x08	/* u32 */
/*
 * And the array starts at 0x10, not 0x0c. Three u32 fields come before it, but its first member
 * is a u64, so the compiler pads to the next eight-byte boundary. Reading it at 0x0c produces an
 * address of 0x0028004000000000 and a data word of zero, which is what this did until the raw
 * words were printed beside the interpretation.
 */
#define	CTRL_H2T_DBELL_MSG	0x10
#define	CTRL_DBELL_MSG_SIZE	16	/* a u64 then a u32, padded - not twelve */
#define	  CTRL_DBELL_ADDR	0x00	/* u64 */
#define	  CTRL_DBELL_DATA	0x08	/* u32 */
#define	CTRL_DBELL_MAX		16	/* more than the five this board has */

/*
 * And the address in each entry is an OFFSET, not somewhere to write directly. The vendor's host
 * adds its target-register BAR to it:
 *
 *	dbell_msg->address = map_dbell->address + trgt_reg_addr;	  facility_host.c
 *	trgt_reg_addr = (u64)fclts_cnf->trg_reg_bar.vaddr;
 *
 * and its own sysfs calls that BAR "bar4 target reg". So ringing a doorbell is an MMIO write into
 * BAR4 - a register on the endpoint - and NOT a write to a host physical address. The difference
 * matters more than most: this board publishes offset 0x280040, and 0x280040 as a host physical
 * address is low system memory. Nothing was written there until this was settled.
 */
#define	NPUEP_DBELL_BAR		4

struct npuep_softc;
int	npuep_ring_dbell(struct npuep_softc *sc, int n);

/*
 * ---------------------------------------------------------------------------------------
 * The front ports: fourteen on the XGS 126 and 136, fewer on the other models of the family.
 *
 * Which model it is decides which ports there are and what each is called on the metal, so there
 * is a table per model, and npuep_ports_select() picks one by the assembly number in the planar
 * serial when the driver attaches - see npuep.c. What follows is the XGS 136's, which is the one
 * this was written against and the one every other model's is read in the light of.
 *
 * Two families, and they do not follow one rule. Ten hang off an integrated switch and carry
 * identifiers of the form 0x8000 + n * 0x100; the remaining four are separate MACs on the SoC and
 * carry 0x0001 to 0x0004. The vendor's own kernel names them accordingly - pport_l0s0p1 through
 * p10 for the switch, and pport_l1 through l4 for the rest - and that is why the coprocessor's
 * port-to-interface table has sixty-five thousand entries rather than sixteen.
 *
 * The logical interface numbers here are the vendor's, read out of its boot log rather than
 * invented. Keeping them means the fastpath sees the assignment it was built expecting, and it is
 * also what makes the tag order below look wrong when it is not: 0x0002 is Port12, not Port10.
 * ---------------------------------------------------------------------------------------
 */
struct npuep_front_port {
	const char	*label;		/* what is printed on the chassis */
	uint16_t	 tag;		/* the coprocessor's identifier for it */
	uint8_t		 iface_id;	/* the logical interface the vendor binds it to */
	uint8_t		 unit;		/* this host's interface number for it */
};

#define	NPUEP_NFRONT	14	/* the most front ports a model has: arrays are this long */
extern const struct npuep_front_port *npuep_front_ports;	/* this model's */
extern int npuep_nfront;					/* ... and how many */
extern const char *npuep_model;
extern const char *npuep_assembly;
/*
 * The coprocessor taken as gone (#768): set when a read answers all ones - its link went down as it
 * reset - or the heartbeat with its fast path dies. From then on every read helper of every
 * facility answers all ones without touching the device: a read while the coprocessor boots again -
 * its link back, its software not yet - never completes and stops the host (measured on the XGS 136
 * on 2026-10-26: an unload 26 s into that boot). Cleared at attach; dev.npuep.0.gone.
 */
extern volatile u_int npuep_gone;
void	npuep_set_gone(const char *why);
void	npuep_ports_select(void);

/*
 * ---------------------------------------------------------------------------------------
 * The control-message channel's state block, at the start of its window.
 *
 * These are not inferred. Sophos ships usfp_rh.ko built with -g3, so the compiler recorded every
 * field name and offset, and `readelf --debug-dump=info` reads them straight back out. The
 * constants come from --debug-dump=macro on the same file.
 *
 *	struct rpc_state {			struct rpc_ring {
 *		u32 cfg_magic;			    u64 posted;
 *		u16 cfg_revision;		    u64 done;
 *		u8  active_hi_rings;		    u32 ring_offset;
 *		u8  reconfig_done;		    u32 desc_offset;
 *		u64 zero_pad[8];		    u32 desc_count;
 *		struct rpc_ring ring_lo;	    union ring_hw_cfg r_cfg;
 *		struct rpc_ring rings[];	};
 *	};
 *
 * The three fields the target actually touches on each pass are confirmed twice over: the
 * disassembly of Marvell's sample handler reads its ring at +0x00 and +0x08 and takes a window
 * offset from +0x14, which is exactly posted, done and desc_offset.
 * ---------------------------------------------------------------------------------------
 */
#define	RPC_ST_CFG_MAGIC	0x00	/* u32 - the HOST writes this to open the channel */
#define	  RPC_STATE_CFG_MAGIC	0xD7D3AB00U
#define	RPC_ST_CFG_REVISION	0x04	/* u16 - written by the HOST, not the target */
#define	  RPC_CFG_REVISION	0x015F	/* what the vendor's own host writes here */
#define	RPC_ST_ACTIVE_HI_RINGS	0x06	/* u8  */
#define	RPC_ST_RECONFIG_DONE	0x07	/* u8  */
#define	RPC_ST_RING_LO		0x48	/* the low-priority ring */
#define	RPC_ST_RINGS		0x68	/* and the high-priority ones */
#define	RPC_STATE_SIZE		232

#define	RPC_RING_POSTED		0x00	/* u64 */
#define	RPC_RING_DONE		0x08	/* u64 */
#define	RPC_RING_OFFSET		0x10	/* u32 */
#define	RPC_RING_DESC_OFFSET	0x14	/* u32 */
#define	RPC_RING_DESC_COUNT	0x18	/* u32 */
#define	RPC_RING_CFG		0x1c	/* u8 ring_num, u8 f_index, u8 dbell, u8 shared */
#define	RPC_RING_SIZE		32

#define	RPC_LO_RINGS_MAX	1
#define	RPC_HI_RINGS_MAX	4

/* A command, in host memory, pointed at by a descriptor in the window. */
#define	RPC_CMD_RESP_BUFF_SZ	0x00	/* u16 */
#define	RPC_CMD_CMD		0x03	/* u8 - an index into a 255-entry handler table */
#define	RPC_CMD_PAYLOAD		0x08
/* The descriptor itself, sixteen bytes, in the window. */
#define	RPC_BD_DMA_ADDR		0x00	/* u64 - a HOST physical address */
#define	RPC_BD_PAYLOAD_LEN	0x08	/* u16 */
#define	RPC_BD_FLAGS		0x0a	/* u16 */
#define	  RPC_DESC_POST_FLAG	1
#define	  RPC_DESC_NO_AGG_DMA	2

/*
 * The commands worth naming, by SFOS 21's and 22's numbers, which npurpc moves from 36 on to the
 * coprocessor's own when it sends them (sfosnum.h).
 */
#define	RPC_CMD_FW_STATE_REV_SET 0	/* set the firewall revision; the far side then voids the table */
#define	RPC_CMD_SA_ADD		30	/* install an IPsec association on the crypto engine (192 bytes) */
#define	RPC_CMD_SA_DEL		31	/* take one out: u32 index, u32 free_entry (0 invalidate, 1 free) */
#define	RPC_CMD_LIF_ADD_UPDATE	3	/* make a logical interface */
#define	RPC_CMD_LIF_DELETE	4
#define	RPC_CMD_PPORT_UPDATE	5	/* bind a port tag to one: u8 iface, u8 rsvd, u16 tag */
#define	RPC_CMD_NHOP_PROGRAM	6	/* a next-hop table entry: the way a flow leaves */
#define	RPC_CMD_MFLOW_INVALIDATE 9	/* take a microflow's action away */
#define	RPC_CMD_FLOW_CREATE_FP	10	/* a connection and both microflows in one request */
#define	RPC_CMD_CONN_RECLAIM_FP	16	/* take a connection back: u32 index, u16 revision, u16 0 */
/*
 * A connection's TCP numbers read, the connection left where it is: the same eight bytes, and the
 * answer the fast path's struct usfp_tcp_seq. Answered for a connection in hardware and for one it
 * has given back and not yet been asked to take out (conn_fpop_state_get in the vendor's
 * conn_fpop.c, conn_checknlock refusing only INVALID and RECLAIMED).
 */
#define	RPC_CMD_CONN_GET_FP_TCP_STATE	17
/* a connection's verdict in place: u32 index, u16 revision, u16 verdict (octep.h, discovery 3) */
#define	RPC_CMD_CONN_MODIFY_FP_VERDICT 13
/*
 * Live users (discovery 5): the coprocessor keeps a table of the users logged in, a bit for each of
 * 65,536, and a connection carries its user's number in its entry (the u16 at +8, after the flags).
 * ADD and DELETE take the number as a u32. CONN_MODIFY_FP sets it on a connection in place: u32
 * index, u16 revision, u16 0, u16 which fields (bit 0 the live user, bits 1 and 2 the two QoS
 * words), u16 the user, then the two QoS words. The read answers with the table's bits for a range
 * a multiple of 64 long, and nothing in front of them. The vendor's luid_fpop.c, luid_rpc.c and
 * conn_table.h; the numbers follow its list from FW_STATE_REV_SET at 0, as 13 does.
 */
#define	RPC_CMD_CONN_MODIFY_FP	12
#define	RPC_CMD_ADD_LIVE_UID	18
#define	RPC_CMD_DELETE_LIVE_UID	19
#define	RPC_CMD_LO_LUID_READ	41
#define	NPURPC_CONN_MODIFY_LEN	20
#define	NPURPC_CONN_MODIFY_LUID	0x0001
#define	NPURPC_LUID_MAX		65536
#define	RPC_CMD_LO_LIF_READ	37	/* read the interface table back - no side effects */
#define	RPC_CMD_LO_CONN_READ	38	/* read the connection table back - no side effects */
#define	RPC_CMD_LO_MFLOW_READ	40	/* read the microflow table back */

/*
 * The values that make a flow forward in hardware, read from the OCTEON TX driver's octep.h where
 * they were measured on that family and confirmed here by hand (docs/findings, 2026-10-14). The
 * two coprocessors speak one control protocol, so these are the same numbers on both.
 */
#define	NPURPC_MFLOW_STATE_ACTIVE	2	/* the only state at which the fast path acts on it */
#define	NPURPC_MFLOW_ACTION_FWD		1
#define	NPURPC_MFLOW_ACTION_IPS		2	/* asks the connection's verdict (octep.h) */
#define	NPURPC_BRCTL_ROUTED		0xe
#define	NPURPC_BRCTL_BRIDGED		0x0	/* nothing rewritten: a bridge carries it (discovery 9) */
/* routed, and the next hop's VLAN written - added, replaced or taken off (discovery 10) */
#define	NPURPC_BRCTL_ROUTED_VLAN	(NPURPC_BRCTL_ROUTED | 0x1)
#define	NPURPC_CONN_VERDICT_CUT_THRU	2
#define	NPURPC_CONN_VERDICT_DROP	1	/* its frames die on the coprocessor (octep.h) */
#define	NPURPC_CONN_VERDICT_SEND_TO_IPS	0	/* to the inspector's virtual function (octep.h) */
#define	NPURPC_CONN_STATE_VALID		1
#define	NPURPC_CONN_FLAG_DNAT		(1u << 19)
#define	NPURPC_CONN_FLAG_SNAT		(1u << 20)
#define	NPURPC_FLOW_VALID_ORIG		0x1
#define	NPURPC_FLOW_VALID_REPLY		0x2
#define	NPURPC_FLOW_REQ_LEN		180
#define	NPURPC_FLOW_OFF_CONN_NAT	84	/* the NAT block inside the request's connection */
#define	NPURPC_FLOW_OFF_VALID		112
#define	NPURPC_FLOW_OFF_MFLOW_O		116
#define	NPURPC_FLOW_OFF_MFLOW_R		148
#define	NPURPC_MFLOW_REQ_LEN		32
#define	NPURPC_NHOP_REQ_LEN		32
#define	NPURPC_NHOP_FLAG_L3		0x01
#define	NPURPC_TBL_FLAG_READ_ALL	0x0002
#define	NPURPC_MFLOW_RD_ENT_LEN		112	/* on this family: a 4-byte idx header, not 8 */

/* npurpc.c: post one command, taking the channel lock for it (the flow side's way in). */
int	npurpc_send(uint8_t cmd, const void *payload, int plen, void *resp, int rlen, int *rc);
/* npurpc.c: the same, posted on the high ring without waiting when it can be. */
int	npurpc_send_posted(uint8_t cmd, const void *payload, int plen);
/* npurpc.c: open or shut every front port's interface entry for the flow maker; ports verified. */
int	npurpc_entries(int open);
/* npurpc.c: the front ports took other addresses; program their interface entries again. */
void	npurpc_front_macs_changed(void);
/* npurpc.c: why the front ports' interface table is not confirmed, or that it is (issue 736). */
#define	NPURPC_WHY_LEN	256
void	npurpc_why_set(const char *why);
void	npurpc_why_get(char *out, size_t len);
/* npurpc.c: the SFOS whose software the coprocessor runs - 21, 22, 23 - or 0 (sfosnum.h). */
int	npurpc_sfos(void);

/*
 * The answer, written back into the same host buffer the command came from. rc carries an errno
 * in its low bits when RPC_RC_ERRNO is set, so a non-zero rc is not automatically a transport
 * failure - the target may simply be refusing the request, which is itself an answer.
 */
#define	RPC_RESP_RC		0x00	/* u16 */
#define	  RPC_RC_ERRNO		(1U << 15)
#define	RPC_RESP_DESC_DONE	0x02	/* u8  */
#define	RPC_RESP_MAGIC_SEED	0x04	/* u16 */
#define	RPC_RESP_PAYLOAD_LEN	0x06	/* u16 */
#define	RPC_RESP_PAYLOAD	0x08

/*
 * Making a logical interface, eighteen bytes. The index is not an index into anything the host
 * chooses freely: it is (iface_id << 12) | vlan_id, so one interface owns a block of 4096 and an
 * untagged port is its vlan zero. update_mask is a selector, and the one value that means "create
 * this" rather than "change these fields" is 0x00FF.
 */
#define	RPC_LIF_INDEX		0x00	/* u32 */
#define	  RPC_LIF_IFACE_SHIFT	12
#define	RPC_LIF_MAC		0x04	/* u8[6] */
#define	RPC_LIF_MTU		0x0a	/* u16 - not range checked by the target */
#define	RPC_LIF_FLAGS		0x0c	/* u16 */
#define	  RPC_LIF_FWD_L2		0x0001	/* fwd_mode, bits 0-1. Zero means DROP. */
#define	  RPC_LIF_FWD_L3		0x0002
#define	  RPC_LIF_ADMIN_DISABLED	0x0004	/* drops everything */
#define	  RPC_LIF_OFFLOAD_DISABLED	0x0008	/* gives everything to the host - what we want */
#define	RPC_LIF_UPDATE_MASK	0x10	/* u16 */
#define	  RPC_LIF_CREATE		0x00FF
#define	  RPC_LIF_UPDATE_OFFLOAD	0x0010	/* offload_disabled alone, the rest left as it is */
#define	  RPC_LIF_UPDATE_MAC	0x0001	/* and the others an update names, octep.h's OCTEP_LIF_M_* */
#define	  RPC_LIF_UPDATE_MTU	0x0002
#define	  RPC_LIF_UPDATE_FWD	0x0004
#define	  RPC_LIF_UPDATE_ADMIN	0x0008
#define	RPC_LIF_REQ_SIZE	18

/*
 * Binding a front port to one, four bytes. The tag here is LITTLE endian, which is the opposite
 * of the same value in the datapath's frame header - that one is big endian. They are the same
 * number and they are written the other way round, and nothing warns.
 */
#define	RPC_PPORT_IFACE		0x00	/* u8 */
#define	RPC_PPORT_TAG		0x02	/* u16 */
#define	RPC_PPORT_REQ_SIZE	4

/* Every LO_*_READ takes the same twelve bytes. */
#define	RPC_TBL_S_INDEX		0x00	/* u32 */
#define	RPC_TBL_NUM_ENTRIES	0x04	/* u16 */
#define	RPC_TBL_FLAGS		0x06	/* u16 */
#define	RPC_TBL_E_INDEX		0x08	/* u32 */
#define	RPC_TBL_REQ_SIZE	12

#define	RPC_DATA_MAX_SIZE	4096

/*
 * Thirty-six bits, and it is the device's limit rather than the bus's - see docs/npuep/mvmgmt.md. Every
 * facility that hands the coprocessor a host address is bound by it.
 */
#define	NPUEP_DMA_LOWADDR	0xFFFFFFFFFULL

int	npumgmt_attach(struct npuep_facility *fac);
void	npumgmt_detach(void);

int	npugiu_attach(struct npuep_facility *fac);
void	npugiu_goodbye(void);
void	npugiu_detach(void);
void	npugiu_doorbell(int index);

int	npunwa_attach(struct npuep_facility *fac);
void	npunwa_detach(void);

/* What address the datapath gave one front port's interface. ENXIO if it never came up. */
int	npugiu_front_mac(int idx, uint8_t *out);
/* One front port takes the address given; sleeps, so no lock held. */
int	npugiu_front_mac_set(int idx, const uint8_t *mac);
/*
 * Tell the stack a front port's carrier changed. The network agent is the only thing that knows,
 * and the interfaces belong to the giu facility, so this is the seam between them.
 */
void	npugiu_link_change(int idx, int up, int speed);
/*
 * Open or close a front port's catch-all in the switch. A bridge member needs this: its job is to
 * receive frames addressed to somebody else, and without it the switch delivers only what is
 * addressed to that port itself.
 *
 * Sleeps. The caller must hold no driver lock.
 */
int	npunwa_set_promisc(int idx, int on);
/*
 * What one front port's interface is asking for, -1 if there is no interface. Read by the agent's
 * link poll so a refused request is retried rather than left standing.
 */
int	npugiu_promisc_wanted(int idx);
int	npugiu_allmulti_wanted(int idx);
int	npugiu_mtu_wanted(int idx);

int	npurpc_attach(struct npuep_facility *fac);
void	npurpc_detach(void);
/* npuflow.h has it too; npuep_detach calls it while the control-message channel is up (#780). */
void	npuflow_detach(void);

#endif /* _NPUEP_H_ */
