/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * The RPC facility: the channel that programs the coprocessor's fast path.
 *
 * Everything here is written against usfp_rh.ko as shipped on the appliance - the module's own DWARF
 * for the structures and constants, and a disassembly for the ordering. docs/families/octeon-tx-rpc.md
 * is that reading written out, and this file is the part of it that can be executed.
 *
 * WHY THIS EXISTS. A frame arriving at a front port is dropped by the fast path unless its ingress
 * tag resolves to a LIF - FPCNTR_FROM_WIRE_DROP_LIF_LU_NULL and four siblings - while a frame from
 * the host passes no such gate. That is the whole asymmetry this project has been measuring. LIFs
 * are installed over this facility and nowhere else.
 *
 * WHAT IT DOES. Reads whose answers are checkable - the fast path's own counter arrays and its
 * platform block, held against numbers captured from this same board while the vendor's firmware
 * was running it - and the writes, each named and each gated behind rpc.allow_write: the ring
 * configuration, the three firewall-state commands, PPORT_UPDATE, LIF_ADD_UPDATE, SA_ADD, SA_DEL,
 * and the three that make up an accelerated flow - NHOP_PROGRAM, CONN_CREATE_FP and FLOW_CREATE_FP,
 * with MFLOW_PROGRAM to change one afterwards. PPORT_UPDATE and LIF_ADD_UPDATE are what open the
 * return direction, and this header said the opposite of that for as long as they did not work.
 * octep_rpc_cmd_is_allowed_write is the list that decides, and these sentences follow it rather than
 * the other way round.
 *
 * THE ONE THING THAT IS NOT A READ is the ring configuration, which has to be written into the
 * window before any command can be posted, and which makes the target tear its RPC rings down and
 * build them again. That is why it is a separate, deliberate sysctl and not something attach does.
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
#include <sys/endian.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "octep.h"

/* ------------------------------------------------------------------ the window */

static bus_size_t
octep_rpc_base(struct octep_softc *sc)
{

	return ((bus_size_t)sc->fclt[OCTEP_FCLT_RPC].offset);
}

static uint32_t
octep_rpc_rd(struct octep_softc *sc, bus_size_t off)
{

	return (bus_read_4(sc->bar2, octep_rpc_base(sc) + off));
}

static void
octep_rpc_wr(struct octep_softc *sc, bus_size_t off, uint32_t v)
{

	bus_write_4(sc->bar2, octep_rpc_base(sc) + off, v);
}

static uint64_t
octep_rpc_rd8(struct octep_softc *sc, bus_size_t off)
{

	return ((uint64_t)octep_rpc_rd(sc, off) |
	    ((uint64_t)octep_rpc_rd(sc, off + 4) << 32));
}

/*
 * A 64-bit store, low half first.
 *
 * The cfg word is the one place where the halves must not be seen separately: the target's doorbell
 * handler compares the whole 64-bit word against a cached copy and reconfigures on any difference,
 * so a torn write is a reconfiguration on a half-written word. octep_rpc_wr_cfg below is what
 * writes it, and it is the only caller that needs the barrier.
 */
static void
octep_rpc_wr8(struct octep_softc *sc, bus_size_t off, uint64_t v)
{

	octep_rpc_wr(sc, off, (uint32_t)v);
	octep_rpc_wr(sc, off + 4, (uint32_t)(v >> 32));
}

/* ------------------------------------------------------------------ configuration */

/*
 * Publish last.
 *
 * The target compares only the cfg word, so every ring field has to be in place before it is
 * written. That is the whole ordering requirement and it is read out of irq_handler:
 *
 *	ldr x1, [x19, #4080]	the host-visible struct rpc_state
 *	ldr x3, [x19, #4088]	the target's cached copy of the cfg word
 *	ldr x2, [x1]		the live cfg word
 *	cmp x3, x2
 *	b.eq ...		unchanged: process rings instead
 *
 * reconfig_done is the target's to write, never ours; it clears nothing and acknowledges everything.
 * rpc_handler_init sets it at module load, which is why an unconfigured coprocessor reads
 * cfg_magic 0 with reconfig_done 1 - up, nothing pending.
 */
static int
octep_rpc_configure(struct octep_softc *sc)
{
	bus_size_t ring;
	uint64_t cfg;
	uint32_t rcfg;
	int i;

	mtx_assert(&sc->mtx, MA_OWNED);

	/* Defaults, so a first run needs no knobs: eight descriptors one page into the window. */
	if (sc->rpc_desc_off == 0)
		sc->rpc_desc_off = PAGE_SIZE;
	if (sc->rpc_desc_count == 0)
		sc->rpc_desc_count = 8;
	if (sc->rpc_resp_sz == 0)
		sc->rpc_resp_sz = OCTEP_RPC_DATA_MAX_SIZE - OCTEP_RPC_BUF_DESC_SIZE;
	/*
	 * NO_AGG_DMA, and NOT the post flag.
	 *
	 * Measured, and it is the opposite of what the names suggest at first glance. With flags 1
	 * the target consumes the descriptor and advances `done` and writes nothing back; with 2 it
	 * writes the response header, the payload and the four-byte done magic; with 3 it behaves
	 * like 1. So POST means posted in the PCIe sense - fire and forget, no completion - and a
	 * command that wants an answer must leave it clear.
	 */
	if (sc->rpc_desc_flags == 0)
		sc->rpc_desc_flags = OCTEP_RPC_DESC_NO_AGG_DMA;
	if (sc->rpc_lif_mtu == 0)
		sc->rpc_lif_mtu = 1500;
	if (sc->rpc_lif_mask == 0)
		sc->rpc_lif_mask = OCTEP_LIF_M_ALL;
	if (sc->rpc_lif_fwd == 0)
		sc->rpc_lif_fwd = OCTEP_LIF_FWD_MODE_L3;
	/*
	 * Zero is a meaningful value for fw_cfg, so default it to the coprocessor's own
	 * FW_CFG_DEFAULT instead of leaving a post to clear bits nobody chose to clear.
	 */
	if (sc->rpc_fw_cfg == 0)
		sc->rpc_fw_cfg = OCTEP_FW_CFG_DEFAULT;
	/*
	 * And rpc.cmd must not default to a write. newbus zeroes the softc, command 0 is
	 * FW_STATE_REV_SET, and that command tells the far side to invalidate every offloaded flow:
	 * before these three commands existed, a post with rpc.cmd untouched was refused by number,
	 * and it has to stay that way. Default it to a read. Setting rpc.cmd=0 afterwards is
	 * deliberate and still works, which is the distinction that matters.
	 */
	if (sc->rpc_cmd_num == 0)
		sc->rpc_cmd_num = OCTEP_RPC_CMD_LO_WORKER_SYS_CNT_READ;

	if (sc->fclt[OCTEP_FCLT_RPC].size == 0) {
		device_printf(sc->dev, "rpc: the coprocessor has not published this facility\n");
		return (ENXIO);
	}
	if (sc->rpc_desc_count == 0 ||
	    (sc->rpc_desc_count & (sc->rpc_desc_count - 1)) != 0) {
		device_printf(sc->dev, "rpc: desc_count %u is not a power of two - the target "
		    "masks the index with desc_count-1\n", sc->rpc_desc_count);
		return (EINVAL);
	}
	if (sc->rpc_desc_off < OCTEP_RPC_STATE_SIZE ||
	    sc->rpc_desc_off + sc->rpc_desc_count * OCTEP_RPC_BAR_DESC_SIZE >
	    sc->fclt[OCTEP_FCLT_RPC].size) {
		device_printf(sc->dev, "rpc: desc_offset 0x%x does not leave room for %u "
		    "descriptors inside the window, past the %d-byte state block\n",
		    sc->rpc_desc_off, sc->rpc_desc_count, OCTEP_RPC_STATE_SIZE);
		return (EINVAL);
	}
	if (sc->rpc_cmd.vaddr == NULL) {
		int err = octep_dma_alloc(sc, &sc->rpc_cmd, OCTEP_RPC_DATA_MAX_SIZE,
		    OCTEP_RPC_DATA_MAX_SIZE, "rpc command");

		if (err != 0) {
			device_printf(sc->dev, "rpc: no command buffer: %d\n", err);
			return (err);
		}
	}

	/*
	 * The low-priority ring, which is the one a read wants: its handler runs from a workqueue
	 * with a millisecond budget rather than from a tasklet, and nothing here is latency bound.
	 */
	ring = OCTEP_RPC_STATE_RING_LO;
	octep_rpc_wr8(sc, ring + OCTEP_RPC_RING_POSTED, 0);
	octep_rpc_wr8(sc, ring + OCTEP_RPC_RING_DONE, 0);
	octep_rpc_wr(sc, ring + OCTEP_RPC_RING_OFFSET, 0);
	octep_rpc_wr(sc, ring + OCTEP_RPC_RING_DESC_OFF, sc->rpc_desc_off);
	octep_rpc_wr(sc, ring + OCTEP_RPC_RING_DESC_CNT, sc->rpc_desc_count);

	rcfg = OCTEP_RPC_RCFG(0, 0, sc->rpc_dbell, sc->rpc_shared);
	octep_rpc_wr(sc, ring + OCTEP_RPC_RING_CFG, rcfg);

	/* The four high-priority rings stay unconfigured; active_hi_rings below says none. */
	for (i = 0; i < OCTEP_RPC_HI_RINGS_MAX; i++) {
		bus_size_t hi = OCTEP_RPC_STATE_RINGS + i * OCTEP_RPC_RING_SIZE;

		octep_rpc_wr8(sc, hi + OCTEP_RPC_RING_POSTED, 0);
		octep_rpc_wr8(sc, hi + OCTEP_RPC_RING_DONE, 0);
		octep_rpc_wr(sc, hi + OCTEP_RPC_RING_OFFSET, 0);
		octep_rpc_wr(sc, hi + OCTEP_RPC_RING_DESC_OFF, 0);
		octep_rpc_wr(sc, hi + OCTEP_RPC_RING_DESC_CNT, 0);
		octep_rpc_wr(sc, hi + OCTEP_RPC_RING_CFG, 0);
	}

	/* Everything above has to be visible before the word that is compared. */
	bus_barrier(sc->bar2, octep_rpc_base(sc), OCTEP_RPC_STATE_SIZE,
	    BUS_SPACE_BARRIER_WRITE);

	sc->rpc_revision++;
	cfg = (uint64_t)OCTEP_RPC_STATE_CFG_MAGIC |
	    ((uint64_t)(sc->rpc_revision & 0xffff) << 32) |
	    ((uint64_t)0 << 48) |		/* active_hi_rings */
	    ((uint64_t)0 << 56);		/* reconfig_done - the target's, cleared by us */
	octep_rpc_wr8(sc, OCTEP_RPC_STATE_CFG, cfg);
	bus_barrier(sc->bar2, octep_rpc_base(sc), 8, BUS_SPACE_BARRIER_WRITE);

	device_printf(sc->dev, "rpc: wrote cfg 0x%016jx; ring_lo descs %u at window+0x%x, "
	    "dbell %u, shared %u\n", (uintmax_t)cfg, sc->rpc_desc_count, sc->rpc_desc_off,
	    sc->rpc_dbell, sc->rpc_shared);

	/*
	 * Now ring the doorbell, because the comparison happens in the doorbell handler and nothing
	 * else polls the word. Without this the configuration sits in the window unread.
	 */
	(void)octep_ring_dbell(sc, sc->fclt[OCTEP_FCLT_RPC].dbell_start + sc->rpc_dbell);

	/* refresh_cfg quiesces, reallocates and re-requests interrupts before it acknowledges. */
	for (i = 0; i < OCTEP_RPC_CFG_WAIT_MS; i++) {
		uint64_t live = octep_rpc_rd8(sc, OCTEP_RPC_STATE_CFG);

		if ((live >> 56) & 0xff) {
			sc->rpc_ready = 1;
			device_printf(sc->dev, "rpc: the target acknowledged after %d ms; "
			    "cfg now 0x%016jx\n", i, (uintmax_t)live);
			return (0);
		}
		DELAY(1000);
	}

	device_printf(sc->dev, "rpc: no acknowledgement after %d ms; cfg reads 0x%016jx. The "
	    "target sets reconfig_done only after it has re-enabled every doorbell, so either the "
	    "doorbell number or the shared flag is wrong for this board\n",
	    OCTEP_RPC_CFG_WAIT_MS, (uintmax_t)octep_rpc_rd8(sc, OCTEP_RPC_STATE_CFG));
	return (ETIMEDOUT);
}

/* ------------------------------------------------------------------ one command */

/*
 * Post one command and wait for its answer.
 *
 * The buffer is ours, in host memory, and its physical address goes in the descriptor: the target
 * reads it with the DPI engine through mv_pci_sync_dma and works on a staging copy. What bounds a
 * batch is the span between the first descriptor's buffer and the others, not any region the target
 * owns - process_ring takes the first buffer address as its base and refuses anything more than
 * 0x8fff beyond it. One command at a time never meets that limit.
 */
/*
 * One struct usfp_fpop_req_program_mflow, twenty-eight bytes.
 *
 * Shared by MFLOW_PROGRAM and by both halves of FLOW_CREATE_FP, because the same twenty-eight bytes
 * written in three places is three places to get a bitfield wrong - and this driver has already
 * paid for that once, when a two-byte field written as one halfword put the DF bit in a mask.
 *
 * The identity, the next hop and the association are arguments because they are what differs
 * between the two directions of a flow; everything else is staged once and describes both. The
 * association differs always: of a tunnelled connection's two microflows exactly one names one.
 */
static void
octep_rpc_put_mflow(struct octep_softc *sc, uint8_t *p, uint32_t id, uint32_t rev,
    uint32_t valid, uint32_t dir, uint32_t nhop, uint32_t nhop_rev, uint32_t sa, uint32_t sa_rev)
{

	le32enc(p + 0, (id & 0x01ffffffu) | ((rev & 0x3fu) << 25) |
	    ((valid & 1u) << 31));
	le32enc(p + 4, (sa & 0xffffu) |
	    ((sc->rpc_mflow_action & 0xfu) << 16) |
	    ((dir & 1u) << 23) |
	    ((sc->rpc_mflow_brctl & 0xfu) << 24) |
	    ((sc->rpc_mflow_state & 0xfu) << 28));
	le32enc(p + 8, 0);
	le32enc(p + 12, sc->rpc_mflow_conn);
	le32enc(p + 16, (sc->rpc_mflow_fw_rev & 0xffffu) |
	    ((sc->rpc_mflow_conn_rev & 0xffffu) << 16));
	le32enc(p + 20, (nhop & 0x00ffffffu) | ((nhop_rev & 0xffu) << 24));
	le32enc(p + 24, sa_rev & 0xffffu);
	le32enc(p + 28, sc->rpc_mflow_timeout);
}

static int
octep_rpc_post(struct octep_softc *sc)
{
	uint8_t *buf, *p;
	uint16_t reqlen;
	bus_size_t desc;
	uint64_t posted, done;
	uint32_t idx;
	uint16_t rc, plen;
	uint32_t us, step;

	mtx_assert(&sc->mtx, MA_OWNED);

	if (!sc->rpc_ready) {
		device_printf(sc->dev, "rpc: not configured - write 1 to rpc.configure first\n");
		return (ENXIO);
	}
	if (sc->rpc_cmd_num >= OCTEP_RPC_CMD_MAX) {
		device_printf(sc->dev, "rpc: command %u is past RPC_CMD_MAX\n", sc->rpc_cmd_num);
		return (EINVAL);
	}
	if (!octep_rpc_cmd_is_read(sc->rpc_cmd_num) &&
	    !octep_rpc_cmd_is_allowed_write(sc->rpc_cmd_num)) {
		device_printf(sc->dev, "rpc: command %u is refused. This driver issues the read "
		    "commands, and the writes that set the firewall state, install a port mapping "
		    "or a logical interface, and add or remove a security association. Nothing "
		    "else\n", sc->rpc_cmd_num);
		return (EPERM);
	}
	/*
	 * The gate, which the driver's own writes do not go through.
	 *
	 * rpc_internal is set only by this file's own writers, only under this lock, and only around
	 * a single post - so a write from outside still has to open the gate deliberately, and the
	 * gate's value is never touched on their behalf. See the field's comment for the cold boot
	 * that came up with it standing open when they opened it instead.
	 */
	if (!octep_rpc_cmd_is_read(sc->rpc_cmd_num) && sc->rpc_allow_write == 0 &&
	    sc->rpc_internal == 0) {
		device_printf(sc->dev, "rpc: command %u changes state on the far side. Set "
		    "rpc.allow_write=1 first, deliberately\n", sc->rpc_cmd_num);
		return (EPERM);
	}

	buf = (uint8_t *)sc->rpc_cmd.vaddr;
	memset(buf, OCTEP_RPC_BUF_POISON, OCTEP_RPC_DATA_MAX_SIZE);

	/* struct rpc_cmd_buf_desc, the same head for every command */
	le16enc(buf + 0, (uint16_t)sc->rpc_resp_sz);
	buf[2] = 0;
	buf[3] = (uint8_t)sc->rpc_cmd_num;
	le32enc(buf + 4, 0);

	p = buf + OCTEP_RPC_BUF_DESC_SIZE;
	switch (sc->rpc_cmd_num) {
	case OCTEP_RPC_CMD_CONN_CREATE_FP: {
		uint32_t flags;

		/*
		 * struct usfp_fpop_req_conn_create. The index, then struct usfp_conn_entry entire:
		 * an eight-byte atomic block, a session id, two QoS words, a TCP block, a NAT block,
		 * and the entry lock last. The far side copies everything up to the lock and leaves
		 * the lock alone - conn_entry_copy exists for that - so the tail of this request is
		 * zero and stays zero.
		 *
		 * Why only the atomic block is filled: everything after it describes work this host
		 * is not asking for. QoS, NAT and the TCP sequence state all belong to a connection
		 * the vendor's slow path has already classified, and sending a guess at them would
		 * be asking the fast path to act on numbers nobody measured.
		 */
		/*
		 * Clear the whole entry first. octep_rpc_post poisons the command buffer with
		 * OCTEP_RPC_BUF_POISON so an unwritten field is loud rather than plausibly zero,
		 * and that is right for a request whose fields are all set - but this one sets only
		 * the atomic block, so everything after it went to the far side as 0x5a. It read
		 * that as a QoS block with its valid bit up and a meaningless meter, and a TCP
		 * sequence block with a window scale of 90 and ninety retransmissions. Zero is what
		 * "this host is not asking for any of that" looks like.
		 */
		memset(p, 0, OCTEP_CONN_REQ_LEN);
		le32enc(p + 0, sc->rpc_conn_idx);
		flags = (sc->rpc_conn_rev & 0xffffU) |
		    ((sc->rpc_conn_verdict & 0x3U) << 21) |
		    ((sc->rpc_conn_state & 0x3U) << 30);
		le32enc(p + 4, flags);
		le16enc(p + 8, 0);			/* live_uid */
		le16enc(p + 10, 0);			/* session_rev */
		le32enc(p + 12, sc->rpc_conn_session);
		reqlen = OCTEP_CONN_REQ_LEN;
		break;
	}

	case OCTEP_RPC_CMD_FW_CFG_PARAMS_SET:
		/*
		 * The global configuration word. fw_state_set_cfg_params refuses a request shorter
		 * than four bytes - `cmp w4, #3; b.ls` on the length - and then reads a HALFWORD
		 * from offset zero, which fw_state_fpop_cfg_params_set stores as the whole 32-bit
		 * fw_cfg. So the request is four bytes and only the low sixteen bits can be set.
		 *
		 * Bit 0 is FW_CFG_OFFLOAD. Until it is set the fast path forces every frame it takes
		 * off the wire to the host without a flow lookup, which is why no flow has ever
		 * existed on this appliance and why nothing the crypto path needs can be reached.
		 */
		if ((sc->rpc_fw_cfg & ~(uint32_t)OCTEP_FW_CFG_NAMED) != 0) {
			device_printf(sc->dev, "rpc: fw_cfg 0x%x sets a bit the vendor does not name; "
			    "the named bits are 0x%x\n", sc->rpc_fw_cfg,
			    (unsigned)OCTEP_FW_CFG_NAMED);
			return (EINVAL);
		}
		le16enc(p + 0, (uint16_t)sc->rpc_fw_cfg);
		le16enc(p + 2, 0);
		reqlen = 4;
		break;

	case OCTEP_RPC_CMD_FW_STATE_REV_SET:
		/*
		 * Two bytes - `cmp w4, #1; b.ls` - holding the firewall revision. Note what
		 * fw_state_fpop_rev_set does after storing it: it calls mflow_fpop_invalidate_issue
		 * over the whole table. Bumping this revision THROWS AWAY every offloaded flow, which
		 * is exactly what a ruleset reload has to do, and is the reason the field exists.
		 */
		le16enc(p + 0, (uint16_t)sc->rpc_fw_rev);
		reqlen = 2;
		break;

	case OCTEP_RPC_CMD_FW_L3_FWD_STATE_REV_SET:
		/* The same two bytes for the layer-three forwarding revision, and no invalidate. */
		le16enc(p + 0, (uint16_t)sc->rpc_fw_l3_rev);
		reqlen = 2;
		break;

	case OCTEP_RPC_CMD_PPORT_UPDATE:
		/*
		 * struct usfp_fpop_req_update_pport. Four bytes, and the handler refuses anything
		 * shorter. It writes both directions at once - iface2pport[iface] = tag and
		 * pport2iface[tag] = iface - so one of these is the whole mapping for one port.
		 */
		p[0] = (uint8_t)sc->rpc_lif_iface;
		p[1] = 0;
		le16enc(p + 2, (uint16_t)sc->rpc_lif_tag);
		reqlen = 4;
		break;

	case OCTEP_RPC_CMD_NHOP_PROGRAM:
		/*
		 * struct usfp_fpop_req_program_nhop: a 32-bit index, then struct usfp_nhop_entry.
		 *
		 * The entry is twenty-eight bytes, which LO_NHOP_READ confirms by returning exactly
		 * that - two bytes of is_resolved and revision, two reserved, then the twenty-four
		 * of usfp_nhop_core_info. So the request is thirty-two, and the handler refuses
		 * anything shorter.
		 *
		 * pport_tag is a 16-bit bitfield in a 32-bit unit, and the two bytes after it are
		 * plain uint8_t members that gcc packs into the same unit - so the tag is at +24,
		 * the flags at +26 and the interface at +27, not at +28. Getting that wrong is the
		 * class of error that put the DF bit in the LIF's update mask; see the fp_priv note
		 * below.
		 *
		 * WHAT IT IS FOR. A microflow's action carries an nhop_index, and this is the entry
		 * it names: the resolved neighbour's address, our address, and the port to send out
		 * of. Without one, a flow has nowhere to send a frame it matches.
		 */
		le32enc(p + 0, sc->rpc_nhop_index);
		p[4] = (uint8_t)(sc->rpc_nhop_resolved & 1);
		p[5] = (uint8_t)sc->rpc_nhop_rev;
		le16enc(p + 6, 0);
		memcpy(p + 8, sc->rpc_nhop_dmac, ETHER_ADDR_LEN);
		memcpy(p + 14, sc->rpc_nhop_smac, ETHER_ADDR_LEN);
		be16enc(p + 20, (uint16_t)sc->rpc_nhop_ethtype);
		be16enc(p + 22, (uint16_t)sc->rpc_nhop_vlan);
		le16enc(p + 24, (uint16_t)sc->rpc_nhop_tag);
		p[26] = (uint8_t)sc->rpc_nhop_flags;
		p[27] = (uint8_t)sc->rpc_nhop_iface;
		le16enc(p + 28, (uint16_t)sc->rpc_nhop_mtu);
		le16enc(p + 30, 0);
		reqlen = OCTEP_NHOP_REQ_LEN;
		break;

	case OCTEP_RPC_CMD_FLOW_CREATE_FP: {
		uint32_t cflags;

		/*
		 * The connection and both microflows in one request. See
		 * OCTEP_RPC_CMD_FLOW_CREATE_FP in octep.h for the layout and for why this command
		 * is the only one that can create the outbound direction.
		 *
		 * Cleared first for the reason CONN_CREATE_FP is cleared first: the command buffer
		 * is poisoned so an unwritten field is loud, and this request leaves the QoS, TCP
		 * and NAT blocks of the connection deliberately zero. Zero is what "this host is
		 * not asking for that" looks like; 0x5a is a QoS meter nobody chose.
		 */
		memset(p, 0, OCTEP_FLOW_REQ_LEN);

		/* The connection, exactly as CONN_CREATE_FP builds it. */
		le32enc(p + 0, sc->rpc_conn_idx);
		cflags = (sc->rpc_conn_rev & 0xffffU) |
		    ((sc->rpc_conn_verdict & 0x3U) << 21) |
		    ((sc->rpc_conn_state & 0x3U) << 30);
		if (sc->rpc_conn_dnat != 0)
			cflags |= OCTEP_CONN_FLAG_DNAT;
		if (sc->rpc_conn_snat != 0)
			cflags |= OCTEP_CONN_FLAG_SNAT;
		le32enc(p + 4, cflags);
		le32enc(p + 12, sc->rpc_conn_session);

		/*
		 * struct usfp_nat_info, six long words at a settable base.
		 *
		 * The addresses go in as whole words and the ports in pairs, in the order the header
		 * declares: the two original addresses, the two translated ones, then the original
		 * ports and the translated ports. Written with le32enc of values that are already in
		 * network order, which is the same thing as laying the four bytes down in order - the
		 * convention everywhere else in this builder, and the one the frame and pf both use.
		 *
		 * Nothing is written at all unless a direction is asked for. A NAT block of zeros in a
		 * connection that does not translate is harmless, but it is also eight words of
		 * somebody else's structure being overwritten on a guessed offset, and there is no
		 * reason to do that until the offset has been confirmed.
		 */
		if ((sc->rpc_conn_snat != 0 || sc->rpc_conn_dnat != 0) &&
		    sc->rpc_conn_nat_off + 24 <= OCTEP_FLOW_REQ_LEN) {
			uint8_t *n = p + sc->rpc_conn_nat_off;

			/*
			 * Bounded, because the offset comes from outside. An unbounded one would let
			 * a sysctl decide how far past a 256-byte request this writes, which is the
			 * same defect as a far side's length field deciding how far a host reads -
			 * found once in this driver's receive path already, so not left here.
			 */
			le32enc(n + 0, sc->rpc_conn_orig_src);
			le32enc(n + 4, sc->rpc_conn_orig_dst);
			le32enc(n + 8, sc->rpc_conn_nat_src);
			le32enc(n + 12, sc->rpc_conn_nat_dst);
			le16enc(n + 16, (uint16_t)sc->rpc_conn_orig_dport);
			le16enc(n + 18, (uint16_t)sc->rpc_conn_orig_sport);
			le16enc(n + 20, (uint16_t)sc->rpc_conn_nat_dport);
			le16enc(n + 22, (uint16_t)sc->rpc_conn_nat_sport);
		}

		le32enc(p + OCTEP_FLOW_OFF_VALID, sc->rpc_flow_valid);

		/* The first direction, from rpc.mflow_*. */
		octep_rpc_put_mflow(sc, p + OCTEP_FLOW_OFF_MFLOW_O, sc->rpc_mflow_id,
		    sc->rpc_mflow_rev, sc->rpc_mflow_valid, sc->rpc_mflow_dir,
		    sc->rpc_mflow_nhop, sc->rpc_mflow_nhop_rev, sc->rpc_mflow_sa,
		    sc->rpc_mflow_sa_rev);

		/* And the second, which differs in identity, next hop and association. */
		octep_rpc_put_mflow(sc, p + OCTEP_FLOW_OFF_MFLOW_R, sc->rpc_mflow2_id,
		    sc->rpc_mflow2_rev, sc->rpc_mflow2_valid, sc->rpc_mflow2_dir,
		    sc->rpc_mflow2_nhop, sc->rpc_mflow2_nhop_rev, sc->rpc_mflow2_sa,
		    sc->rpc_mflow2_sa_rev);

		reqlen = sc->rpc_flow_len != 0 ? (uint16_t)sc->rpc_flow_len :
		    OCTEP_FLOW_REQ_LEN;
		break;
	}

	case OCTEP_RPC_CMD_CONN_RECLAIM_FP:
		/* struct usfp_fpop_req_conn_reclaim: the index, then the revision this host holds for it. */
		le32enc(p + 0, sc->rpc_conn_idx);
		le16enc(p + 4, (uint16_t)sc->rpc_conn_rev);
		le16enc(p + 6, 0);
		reqlen = OCTEP_CONN_RECLAIM_REQ_LEN;
		break;

	case OCTEP_RPC_CMD_MFLOW_PROGRAM:
		/*
		 * struct usfp_fpop_req_program_mflow, twenty-eight bytes of content in a
		 * thirty-two byte request. The identity comes from a punted frame's metadata, not
		 * from a hash computed here - see OCTEP_RPC_CMD_MFLOW_PROGRAM in octep.h.
		 *
		 * rpc.mflow_action has no default on purpose. The four-bit action's values are not
		 * in any source or binary this project holds, and a wrong one is not inert: the
		 * fast path has a counter for an action it refuses and another for one it does not
		 * recognise, which is how to find the right value without guessing twice.
		 */
		octep_rpc_put_mflow(sc, p, sc->rpc_mflow_id, sc->rpc_mflow_rev,
		    sc->rpc_mflow_valid, sc->rpc_mflow_dir, sc->rpc_mflow_nhop,
		    sc->rpc_mflow_nhop_rev, sc->rpc_mflow_sa, sc->rpc_mflow_sa_rev);
		reqlen = OCTEP_MFLOW_REQ_LEN;
		break;

	case OCTEP_RPC_CMD_LIF_ADD_UPDATE:
		/*
		 * Eighteen bytes: a 32-bit index, then struct usfp_lif_entry entire.
		 *
		 * DWARF gives struct usfp_lif_config as sixteen bytes with an 8-bit update mask at
		 * offset 14, and that is the module's internal form rather than the wire's. The
		 * handler refuses anything shorter than 18 and reads the mask as a 16-bit word at
		 * offset 16, which is where usfp_lif_entry keeps the field its own definition calls
		 * reserved. Sixteen bytes is refused with rc 1; eighteen is accepted.
		 *
		 * The index is the addressing the whole fast path uses - (iface_id << 12) | vlan -
		 * and the mask says which of the six fields this call is setting, so one call can
		 * raise offload_disabled without disturbing the rest.
		 */
		le32enc(p + 0, ((uint32_t)(sc->rpc_lif_iface & 0x7f) << 12) |
		    (sc->rpc_lif_vlan & 0xfff));
		memcpy(p + 4, sc->rpc_lif_mac, ETHER_ADDR_LEN);
		le16enc(p + 10, (uint16_t)sc->rpc_lif_mtu);
		le16enc(p + 12, (uint16_t)((sc->rpc_lif_fwd & 0x3) |
		    ((sc->rpc_lif_admin_dis & 1) << 2) |
		    ((sc->rpc_lif_offload_dis & 1) << 3) |
		    ((sc->rpc_lif_reppid & 0xfff) << 4)));
		/*
		 * The two fp_priv bytes, which are not a halfword.
		 *
		 * struct usfp_lif_entry's tail is a one-byte update mask at +14 and a one-byte
		 * field whose bit 0 is df_enabled at +15, and the vendor sets the mask to
		 * LIF_FP_PRIV_MASK_DF on every add - fw_fp_add_lif does it unconditionally. Writing
		 * the two as one little-endian halfword put the DF bit in the mask byte and left
		 * the field itself zero, so DF could never be turned on and the failure was silent,
		 * because nothing reads it back. It is inert today, with lif_df at 0; it would not
		 * have been the day somebody tried the deep-inspection bit.
		 */
		p[14] = OCTEP_LIF_FP_PRIV_MASK_DF;
		p[15] = (uint8_t)(sc->rpc_lif_df & 1);
		le16enc(p + 16, (uint16_t)sc->rpc_lif_mask);
		reqlen = 18;
		break;

	case OCTEP_RPC_CMD_SA_ADD: {
		uint32_t ctrl, opt;

		/*
		 * struct usfp_fpop_req_sa_add, 192 bytes. The layout, the bit positions and the
		 * algorithm numbers are in docs/families/octeon-tx-rpc.md, read out of the
		 * module's own DWARF rather than guessed.
		 *
		 * The key and address fields are big-endian by declaration - __be32 - and the
		 * scalars beside them are not, which is why this builder mixes be and le on
		 * purpose rather than by accident.
		 */
		ctrl = (sc->rpc_sa_hash & 0xf) | ((sc->rpc_sa_cimode & 0xf) << 4) |
		    ((sc->rpc_sa_cipher & 0xf) << 8) | ((sc->rpc_sa_mode & 0x3) << 12) |
		    ((sc->rpc_sa_proto & 0x3) << 14) | ((sc->rpc_sa_dir & 1) << 16) |
		    ((sc->rpc_sa_arw & 1) << 17) | (1u << 31);	/* valid */
		opt = sc->rpc_sa_opt;
		le32enc(p + 0, sc->rpc_sa_idx);
		le32enc(p + 4, sc->rpc_sa_lif);
		memcpy(p + 8, sc->rpc_sa_key, sizeof(sc->rpc_sa_key));
		memcpy(p + 40, sc->rpc_sa_authkey, sizeof(sc->rpc_sa_authkey));
		le32enc(p + 104, ctrl);
		le32enc(p + 108, opt);
		le32enc(p + 112, sc->rpc_sa_win);
		be32enc(p + 116, sc->rpc_sa_spi);
		le64enc(p + 120, sc->rpc_sa_seq);	/* the far side starts at this plus one */
		for (int k = 0; k < 4; k++) {
			be32enc(p + 128 + k * 4, sc->rpc_sa_src[k]);
			be32enc(p + 144 + k * 4, sc->rpc_sa_dst[k]);
		}
		be16enc(p + 160, (uint16_t)sc->rpc_sa_nat_dport);
		be16enc(p + 162, (uint16_t)sc->rpc_sa_nat_sport);
		le64enc(p + 168, 0);			/* no hard byte lifetime */
		le64enc(p + 176, 0);			/* no hard packet lifetime */
		/*
		 * rev_num, and it is not spare. sadb_hw_entry_get's fourth and last check compares
		 * the stored revision against the one the frame's path carries, so an association
		 * can be present and valid and still be refused on this field alone - which is
		 * indistinguishable from a wrong index, because both end at
		 * CRYPTO_DROP_SADB_PRE_ERR.
		 *
		 * The vendor's host keeps a counter per index and pre-increments it on every
		 * install - usfp_ipsec.c does "lx->rev += 1" before building the request - so its
		 * first association at a fresh index carries 1, never 0. This driver sent 0 for as
		 * long as the field was thought to be reserved.
		 */
		le16enc(p + 184, (uint16_t)sc->rpc_sa_rev);
		reqlen = 192;
		break;
	}

	case OCTEP_RPC_CMD_SA_DEL:
		/* struct usfp_fpop_req_sa_del: the index, and whether to free the entry. */
		le32enc(p + 0, sc->rpc_sa_idx);
		le32enc(p + 4, sc->rpc_sa_free);
		reqlen = 8;
		break;

	case OCTEP_RPC_CMD_SA_GET_STATS:
		/* struct usfp_fpop_req_get_sa_stats: the index alone. */
		le32enc(p + 0, sc->rpc_sa_idx);
		reqlen = OCTEP_SA_STATS_REQ_LEN;
		break;

	default:
		/* struct usfp_fpop_req_table_read, which every LO_*_READ takes */
		le32enc(p + 0, sc->rpc_s_index);
		le16enc(p + 4, (uint16_t)sc->rpc_num_entries);
		le16enc(p + 6, (uint16_t)sc->rpc_req_flags);
		le32enc(p + 8, sc->rpc_e_index);
		reqlen = OCTEP_RPC_REQ_LEN;
		break;
	}

	bus_dmamap_sync(sc->rpc_cmd.tag, sc->rpc_cmd.map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	posted = octep_rpc_rd8(sc, OCTEP_RPC_STATE_RING_LO + OCTEP_RPC_RING_POSTED);
	idx = (uint32_t)(posted & (sc->rpc_desc_count - 1));
	desc = sc->rpc_desc_off + idx * OCTEP_RPC_BAR_DESC_SIZE;

	/* struct rpc_cmd_bar_desc */
	octep_rpc_wr8(sc, desc + 0, (uint64_t)sc->rpc_cmd.paddr);
	octep_rpc_wr(sc, desc + 8,
	    (uint32_t)reqlen | ((uint32_t)sc->rpc_desc_flags << 16));
	octep_rpc_wr(sc, desc + 12, 0);
	bus_barrier(sc->bar2, octep_rpc_base(sc) + desc, OCTEP_RPC_BAR_DESC_SIZE,
	    BUS_SPACE_BARRIER_WRITE);

	octep_rpc_wr8(sc, OCTEP_RPC_STATE_RING_LO + OCTEP_RPC_RING_POSTED, posted + 1);
	bus_barrier(sc->bar2, octep_rpc_base(sc), OCTEP_RPC_STATE_SIZE,
	    BUS_SPACE_BARRIER_WRITE);

	(void)octep_ring_dbell(sc, sc->fclt[OCTEP_FCLT_RPC].dbell_start + sc->rpc_dbell);

	sc->rpc_last_cmd = sc->rpc_cmd_num;
	sc->rpc_last_sa_idx = sc->rpc_sa_idx;
	sc->rpc_last_rc = 0;
	sc->rpc_last_len = 0;
	sc->rpc_last_error = 0;

	/*
	 * The wait, with sc->mtx held, which is the transmit path's lock. It used to look once a
	 * millisecond, so a command cost a millisecond however soon it was answered - two hundred
	 * commands in two hundred milliseconds, measured - and a connection made of three commands
	 * held every front port's transmit for three. The far side answers in a fraction of that.
	 * So: every twenty microseconds for the first two milliseconds, then as before.
	 */
	for (us = 0; us < OCTEP_RPC_CMD_WAIT_MS * 1000u; us += step) {
		done = octep_rpc_rd8(sc, OCTEP_RPC_STATE_RING_LO + OCTEP_RPC_RING_DONE);
		if (done > posted) {
			sc->rpc_wait_last = us;
			if (us > sc->rpc_wait_max)
				sc->rpc_wait_max = us;
			sc->rpc_wait_sum += us;
			sc->rpc_wait_n++;
			bus_dmamap_sync(sc->rpc_cmd.tag, sc->rpc_cmd.map,
			    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

			/* struct rpc_resp_buf_desc, written over the head of our buffer */
			rc = le16dec(buf + 0);
			plen = le16dec(buf + 6);
			sc->rpc_last_rc = rc;
			sc->rpc_last_done = buf[2];
			sc->rpc_last_seed = le16dec(buf + 4);
			sc->rpc_last_len = plen;
			if (plen > OCTEP_RPC_DATA_MAX_SIZE - OCTEP_RPC_BUF_DESC_SIZE)
				plen = OCTEP_RPC_DATA_MAX_SIZE - OCTEP_RPC_BUF_DESC_SIZE;
			memcpy(sc->rpc_last_reply, buf + OCTEP_RPC_BUF_DESC_SIZE, plen);
			sc->rpc_commands++;
			return (0);
		}
		step = (us < 2000) ? 20 : 1000;
		DELAY(step);
	}

	sc->rpc_last_error = ETIMEDOUT;
	sc->rpc_timeouts++;
	/*
	 * Said once per timeout for a command a person asked for, and once in total for one this
	 * driver posts by itself on a schedule.
	 *
	 * The revision bump is posted on every ruleset reload, of which three happen during a boot
	 * and one on every WAN lease renewal. If the far side stops answering, an unguarded line
	 * here is a line in the log for as long as the condition lasts - which is the same reasoning
	 * the receive path records for its own suppressed retry. The counters say how many; this
	 * says what, and saying it a thousand times adds nothing.
	 */
	if (sc->rpc_quiet == 0)
		device_printf(sc->dev,
		    "rpc: command %u timed out after %d ms; posted %ju, done %ju\n",
		    sc->rpc_cmd_num, OCTEP_RPC_CMD_WAIT_MS, (uintmax_t)(posted + 1),
		    (uintmax_t)octep_rpc_rd8(sc, OCTEP_RPC_STATE_RING_LO + OCTEP_RPC_RING_DONE));
	return (ETIMEDOUT);
}

/* ------------------------------------------------------------------ sysctls */

static int
octep_sysctl_rpc_configure(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, val = 0;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	mtx_lock(&sc->mtx);
	error = octep_rpc_configure(sc);
	mtx_unlock(&sc->mtx);
	return (error);
}

static int
octep_sysctl_rpc_post(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, val = 0;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	mtx_lock(&sc->mtx);
	error = octep_rpc_post(sc);
	/*
	 * The SA_ADD handler waits for the fast path's RCU grace period and answers -EAGAIN when
	 * it is not over yet - an index still being quiesced after a delete. It is the only SA
	 * command that does: SA_DEL's one errno is -ENODEV and its handler maps that to success,
	 * and the other -EAGAIN in the vendor's source is SA_SEQ_UPDATE, which this driver does
	 * not issue. The errno arrives ENCODED, SP2FP_RC_MAX plus 11 - see OCTEP_RPC_SP2FP_RC_MAX
	 * for why it is not under the errno bit, and what the first version of this got wrong.
	 *
	 * An -EAGAIN read as a refusal was how one negative about SA_ADD came to be published, so
	 * the retry lives here: a few more posts, a couple of milliseconds apart, counted in
	 * rpc.sa_retries - which is what the vendor's own caller does with the same answer.
	 *
	 * Measured on the appliance with the fast path idle: the two-stage delete followed by an
	 * add in the next command answered 0 six times out of six, and the retry never ran. The
	 * grace period is over before the next RPC can arrive when the workers are idle; the
	 * -EAGAIN is for a busy fast path, and this branch is tested by its encoding, not by a
	 * refusal caught in the act. What the same test did catch: SA_DEL with free=1 on a
	 * still-valid entry answers ok and frees nothing, and the next add is refused with rc 2.
	 */
	if (error == 0 && sc->rpc_cmd_num == OCTEP_RPC_CMD_SA_ADD) {
		int again;

		for (again = 0; again < OCTEP_RPC_SA_RETRIES &&
		    sc->rpc_last_rc == OCTEP_RPC_SP2FP_RC_EAGAIN; again++) {
			sc->rpc_sa_retries++;
			DELAY(2000);
			error = octep_rpc_post(sc);
			if (error != 0)
				break;
		}
	}
	mtx_unlock(&sc->mtx);
	return (error);
}

static int
octep_sysctl_rpc_state(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct sbuf *sb;
	uint64_t cfg;
	uint32_t rcfg;
	int error;

	sb = sbuf_new_for_sysctl(NULL, NULL, 1024, req);
	if (sb == NULL)
		return (ENOMEM);

	mtx_lock(&sc->mtx);
	cfg = octep_rpc_rd8(sc, OCTEP_RPC_STATE_CFG);
	rcfg = octep_rpc_rd(sc, OCTEP_RPC_STATE_RING_LO + OCTEP_RPC_RING_CFG);
	sbuf_printf(sb, "\ncfg 0x%016jx\n", (uintmax_t)cfg);
	sbuf_printf(sb, "  magic 0x%08x%s  revision %u  active_hi_rings %u  reconfig_done %u\n",
	    (uint32_t)cfg, (uint32_t)cfg == OCTEP_RPC_STATE_CFG_MAGIC ? " (as expected)" : "",
	    (uint32_t)((cfg >> 32) & 0xffff), (uint32_t)((cfg >> 48) & 0xff),
	    (uint32_t)((cfg >> 56) & 0xff));
	sbuf_printf(sb, "\nring_lo\n");
	sbuf_printf(sb, "  posted %ju  done %ju\n",
	    (uintmax_t)octep_rpc_rd8(sc, OCTEP_RPC_STATE_RING_LO + OCTEP_RPC_RING_POSTED),
	    (uintmax_t)octep_rpc_rd8(sc, OCTEP_RPC_STATE_RING_LO + OCTEP_RPC_RING_DONE));
	sbuf_printf(sb, "  ring_offset 0x%x  desc_offset 0x%x  desc_count %u\n",
	    octep_rpc_rd(sc, OCTEP_RPC_STATE_RING_LO + OCTEP_RPC_RING_OFFSET),
	    octep_rpc_rd(sc, OCTEP_RPC_STATE_RING_LO + OCTEP_RPC_RING_DESC_OFF),
	    octep_rpc_rd(sc, OCTEP_RPC_STATE_RING_LO + OCTEP_RPC_RING_DESC_CNT));
	sbuf_printf(sb, "  r_cfg 0x%08x: ring_num %u  facility %u  dbell %u  shared %u\n",
	    rcfg, rcfg & 0xff, (rcfg >> 8) & 0xff, (rcfg >> 16) & 0xff, (rcfg >> 24) & 0xff);
	sbuf_printf(sb, "\n%u command(s) answered, %u timed out\n",
	    sc->rpc_commands, sc->rpc_timeouts);
	mtx_unlock(&sc->mtx);

	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/*
 * What a return code means, by its range - the three encodings that share the sixteen bits are
 * explained at OCTEP_RPC_SP2FP_RC_MAX. Said in words beside the number, because a 31 read as a
 * refusal is how a negative about SA_ADD was once published.
 */
static const char *
octep_rpc_rc_meaning(uint16_t rc)
{
	if (rc == 0)
		return (" (ok)");
	if (rc & OCTEP_RPC_RC_ERRNO_BIT)
		return (" (the transport's errno: the command was not dispatched)");
	if (rc == OCTEP_RPC_SP2FP_RC_EAGAIN)
		return (" (the handler's -EAGAIN: not ready yet, post again)");
	if (rc > OCTEP_RPC_SP2FP_RC_MAX)
		return (" (the handler's -errno, rc minus 20)");
	return (" (an SP2FP_RC refusal code)");
}

static int
octep_sysctl_rpc_last(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct sbuf *sb;
	int error, i, n;

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);

	mtx_lock(&sc->mtx);
	if (sc->rpc_commands == 0 && sc->rpc_timeouts == 0) {
		sbuf_cat(sb, "\nnothing has been asked yet - write 1 to rpc.post\n");
		goto out;
	}
	sbuf_printf(sb, "\ncmd %u %s\n", sc->rpc_last_cmd,
	    octep_rpc_cmd_name(sc->rpc_last_cmd));
	if (sc->rpc_last_error != 0) {
		sbuf_printf(sb, "did not complete: error %d\n", sc->rpc_last_error);
		goto out;
	}
	sbuf_printf(sb, "rc 0x%04x%s  descriptor_done %u  magic_seed 0x%04x  payload %u bytes\n",
	    sc->rpc_last_rc, octep_rpc_rc_meaning(sc->rpc_last_rc),
	    sc->rpc_last_done, sc->rpc_last_seed, sc->rpc_last_len);

	/*
	 * struct usfp_fpop_resp_get_sa_stats: bytes, packets, seconds since the association was
	 * installed. Labelled with the index the command NAMED, not with rpc.sa_idx as it stands now:
	 * the operator stages the next command's index before reading this one's answer.
	 */
	if (sc->rpc_last_cmd == OCTEP_RPC_CMD_SA_GET_STATS && sc->rpc_last_rc == 0 &&
	    sc->rpc_last_len >= OCTEP_SA_STATS_RESP_LEN) {
		const uint8_t *b = sc->rpc_last_reply;

		sbuf_printf(sb, "  association %u: %ju bytes, %ju packets, created %u s ago\n",
		    sc->rpc_last_sa_idx, (uintmax_t)le64dec(b + 0), (uintmax_t)le64dec(b + 8),
		    le32dec(b + 16));
	}

	/*
	 * PLATFORM_READ answers struct platform_info, and it is the one reply worth naming rather
	 * than dumping: every table bound this driver carries as a constant is in it. The LIF table
	 * is max_ifaces * 4096 entries, an interface id above max_ifaces - 1 is refused by both
	 * sides, and num_pfs with num_vfs is what sizes the coprocessor's host ports.
	 *
	 * The layout is three 64-byte names and then the numbers, read out of
	 * vendor-source-usfp/include/platform_info.h.
	 */
	if (sc->rpc_last_cmd == OCTEP_RPC_CMD_PLATFORM_READ &&
	    sc->rpc_last_len >= OCTEP_PLATFORM_INFO_MIN) {
		const uint8_t *b = sc->rpc_last_reply;
		char nm[OCTEP_PLATFORM_NAME_LEN + 1];

		memcpy(nm, b + OCTEP_PLATFORM_OFF_NAME, OCTEP_PLATFORM_NAME_LEN);
		nm[OCTEP_PLATFORM_NAME_LEN] = '\0';
		sbuf_printf(sb, "  platform   %s\n", nm);
		memcpy(nm, b + OCTEP_PLATFORM_OFF_VERSION, OCTEP_PLATFORM_NAME_LEN);
		nm[OCTEP_PLATFORM_NAME_LEN] = '\0';
		sbuf_printf(sb, "  version    %s\n", nm);
		memcpy(nm, b + OCTEP_PLATFORM_OFF_ASSEMBLY, OCTEP_PLATFORM_NAME_LEN);
		nm[OCTEP_PLATFORM_NAME_LEN] = '\0';
		sbuf_printf(sb, "  assembly   %s\n", nm);
		sbuf_printf(sb, "  id %u  cores %u  max_ifaces %u  rpc_rings %u\n",
		    b[OCTEP_PLATFORM_OFF_ID], b[OCTEP_PLATFORM_OFF_CORES],
		    b[OCTEP_PLATFORM_OFF_MAX_IFACES], b[OCTEP_PLATFORM_OFF_RPC_RINGS]);
		if (sc->rpc_last_len >= OCTEP_PLATFORM_INFO_WITH_PFS)
			sbuf_printf(sb, "  num_pfs %u  num_vfs %u\n",
			    b[OCTEP_PLATFORM_OFF_NUM_PFS],
			    b[OCTEP_PLATFORM_OFF_NUM_VFS]);
		else
			sbuf_printf(sb, "  (the reply is %u bytes and stops before "
			    "num_pfs)\n", sc->rpc_last_len);
		sbuf_printf(sb, "  so the LIF table holds %u entries, and an interface id "
		    "above %u is refused\n",
		    (unsigned)b[OCTEP_PLATFORM_OFF_MAX_IFACES] * 4096,
		    b[OCTEP_PLATFORM_OFF_MAX_IFACES] ?
		    b[OCTEP_PLATFORM_OFF_MAX_IFACES] - 1 : 0);
	}

	/*
	 * A microflow read, decoded. See OCTEP_MFLOW_RD_ENT_LEN for the layout and for what reading
	 * it as raw words cost.
	 *
	 * Ask with rpc.req_flags = OCTEP_TABLE_FLAG_READ_ALL to see every index. With flags 0 the
	 * far side returns only entries whose fw_valid is set and packs them, so the index you get
	 * is not the index you asked for - which is a thing worth knowing before trusting a dump.
	 */
	if (sc->rpc_last_cmd == OCTEP_RPC_CMD_LO_MFLOW_READ &&
	    sc->rpc_last_len >= OCTEP_MFLOW_RD_ENT_LEN) {
		uint32_t off;

		for (off = 0; off + OCTEP_MFLOW_RD_ENT_LEN <= sc->rpc_last_len;
		    off += OCTEP_MFLOW_RD_ENT_LEN) {
			const uint8_t *e = sc->rpc_last_reply + off;
			const uint8_t *k = e + OCTEP_MFLOW_RD_KEY_OFF;
			const uint8_t *en = e + OCTEP_MFLOW_RD_ENTRY_OFF;
			const uint8_t *op = e + OCTEP_MFLOW_RD_OPR_OFF;
			uint32_t mst = le32dec(en + 12);
			uint32_t fl = le32dec(op + 0);

			sbuf_printf(sb, "  entry %d\n", (int)le32dec(e + 0));
			sbuf_printf(sb, "    lif %u  %02x:%02x:%02x:%02x:%02x:%02x <- "
			    "%02x:%02x:%02x:%02x:%02x:%02x  ethertype 0x%04x\n",
			    le32dec(k + 0),
			    k[4], k[5], k[6], k[7], k[8], k[9],
			    k[10], k[11], k[12], k[13], k[14], k[15],
			    be16dec(k + 16));
			sbuf_printf(sb, "    family %u  proto %u  dport %u  sport %u\n",
			    k[18], k[19], be16dec(k + 20), be16dec(k + 22));
			sbuf_printf(sb, "    src %u.%u.%u.%u  dst %u.%u.%u.%u\n",
			    k[24], k[25], k[26], k[27], k[28], k[29], k[30], k[31]);
			sbuf_printf(sb, "    timeout %u  lbinfo 0x%02x  rev %u  fw_valid %u  "
			    "host_valid %u\n", le32dec(en + 4), mst & 0xff,
			    (mst >> 8) & 0xff, (mst >> 16) & 0xff, (mst >> 24) & 0xff);
			sbuf_printf(sb, "    action %u  dir %u  brctl %u  state %u  sa %u  "
			    "conn %u  nhop %u rev %u\n",
			    (fl >> 16) & 0xf, (fl >> 23) & 1, (fl >> 24) & 0xf,
			    (fl >> 28) & 0xf, fl & 0xffff, le32dec(op + 8),
			    le32dec(op + 16) & 0x00ffffff, le32dec(op + 16) >> 24);
		}
	}

	n = sc->rpc_last_len / 8;
	if (n > OCTEP_RPC_MAX_REPLY_WORDS)
		n = OCTEP_RPC_MAX_REPLY_WORDS;
	for (i = 0; i < n; i++) {
		uint64_t v = le64dec(sc->rpc_last_reply + i * 8);

		if (v != 0)
			sbuf_printf(sb, "  [%3d] 0x%016jx  %ju\n", i, (uintmax_t)v, (uintmax_t)v);
	}
	if (n > 0)
		sbuf_printf(sb, "(%d of %u words shown; zeros omitted)\n", n, sc->rpc_last_len / 8);
out:
	mtx_unlock(&sc->mtx);
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/*
 * The command buffer, raw.
 *
 * Worth having permanently: when a command completes and the head of the buffer still holds what we
 * put there, the question is whether the far side wrote anywhere at all, and no decoded view can
 * answer that. The buffer is poisoned before each command so an untouched byte is visible as one.
 */
static int
octep_sysctl_rpc_buf(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct sbuf *sb;
	uint8_t *b;
	int error, i, j, n;

	sb = sbuf_new_for_sysctl(NULL, NULL, 2048, req);
	if (sb == NULL)
		return (ENOMEM);

	mtx_lock(&sc->mtx);
	if (sc->rpc_cmd.vaddr == NULL) {
		sbuf_cat(sb, "\nno buffer yet\n");
		goto out;
	}
	b = (uint8_t *)sc->rpc_cmd.vaddr;
	bus_dmamap_sync(sc->rpc_cmd.tag, sc->rpc_cmd.map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	sbuf_printf(sb, "\nbuffer at pa 0x%016jx, first 128 bytes\n",
	    (uintmax_t)sc->rpc_cmd.paddr);
	n = 128;
	for (i = 0; i < n; i += 16) {
		sbuf_printf(sb, "  +%03x ", i);
		for (j = 0; j < 16; j++)
			sbuf_printf(sb, "%02x%s", b[i + j], j == 7 ? "  " : " ");
		sbuf_cat(sb, "\n");
	}
	for (i = 0, j = 0; i < OCTEP_RPC_DATA_MAX_SIZE; i++)
		if (b[i] != OCTEP_RPC_BUF_POISON)
			j++;
	sbuf_printf(sb, "%d of %d bytes differ from the poison\n", j, OCTEP_RPC_DATA_MAX_SIZE);
out:
	mtx_unlock(&sc->mtx);
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

static int
octep_hexval(char c)
{

	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	return (-1);
}

/*
 * A key, as hex. Test material only - see the comment on rpc_sa_key in octep.h.
 */
static int
octep_sysctl_rpc_sa_key(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	uint8_t *dst = (arg2 == 0) ? sc->rpc_sa_key : sc->rpc_sa_authkey;
	size_t dlen = (arg2 == 0) ? sizeof(sc->rpc_sa_key) : sizeof(sc->rpc_sa_authkey);
	char buf[2 * 64 + 1];
	int error, i, hi, lo;

	for (i = 0; i < (int)dlen; i++)
		snprintf(buf + i * 2, 3, "%02x", dst[i]);
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	memset(dst, 0, dlen);
	for (i = 0; i < (int)dlen; i++) {
		hi = octep_hexval(buf[i * 2]);
		lo = octep_hexval(buf[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			break;
		dst[i] = (uint8_t)((hi << 4) | lo);
	}
	return (0);
}

/* One IPv4 address into the first word of a four-word field, which is where IPv6 would go. */
static int
octep_sysctl_rpc_sa_addr(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	uint32_t *dst = (arg2 == 0) ? sc->rpc_sa_src : sc->rpc_sa_dst;
	char buf[64];
	int error, a, b, c, d;

	snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (dst[0] >> 24) & 0xff,
	    (dst[0] >> 16) & 0xff, (dst[0] >> 8) & 0xff, dst[0] & 0xff);
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (sscanf(buf, "%d.%d.%d.%d", &a, &b, &c, &d) != 4)
		return (EINVAL);
	memset(dst, 0, 4 * sizeof(uint32_t));
	dst[0] = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
	return (0);
}

/* The two next-hop addresses, in the same form as rpc.lif_mac and for the same reason. */
static int
octep_sysctl_rpc_nhop_mac(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	uint8_t *dst = arg2 == 0 ? sc->rpc_nhop_dmac : sc->rpc_nhop_smac;
	char buf[18];
	unsigned int m[6];
	int error, i;

	mtx_lock(&sc->mtx);
	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
	    dst[0], dst[1], dst[2], dst[3], dst[4], dst[5]);
	mtx_unlock(&sc->mtx);

	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (sscanf(buf, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6)
		return (EINVAL);
	mtx_lock(&sc->mtx);
	for (i = 0; i < 6; i++)
		dst[i] = (uint8_t)m[i];
	mtx_unlock(&sc->mtx);
	return (0);
}

static int
octep_sysctl_rpc_lif_mac(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	char buf[18];
	unsigned int m[6];
	int error, i;

	mtx_lock(&sc->mtx);
	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
	    sc->rpc_lif_mac[0], sc->rpc_lif_mac[1], sc->rpc_lif_mac[2],
	    sc->rpc_lif_mac[3], sc->rpc_lif_mac[4], sc->rpc_lif_mac[5]);
	mtx_unlock(&sc->mtx);

	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (sscanf(buf, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6)
		return (EINVAL);
	mtx_lock(&sc->mtx);
	for (i = 0; i < 6; i++)
		sc->rpc_lif_mac[i] = (uint8_t)m[i];
	mtx_unlock(&sc->mtx);
	return (0);
}

/*
 * Set one logical interface's forwarding mode, and nothing else about it.
 *
 * WHY THIS EXISTS. A LIF carries one address, and the fast path drops any frame whose destination
 * is not it - FPCNTR_FROM_WIRE_DROP_LIF_NOT_MY_MAC. A bridge member receives frames addressed to
 * the BRIDGE, so in L3 mode every one of them is discarded before the flow table is consulted.
 * Measured on this appliance with the offload gate open: twenty pings to a host behind a bridged
 * front port, 0 of 20 returned and 35 frames dropped on that counter; the same test after this call
 * with LIF_FWD_MODE_L2, 20 of 20 at 0.66 ms. Sophos's own driver picks the mode the same way, from
 * the device's flags - L3, or BOTH for a bridge, or L2 for a bridge member.
 *
 * It is a SELECTIVE update: update_mask is OCTEP_LIF_M_FWD alone, so the address, the MTU and the
 * rest of the entry are left exactly as the bring-up script installed them. An add would have to
 * carry all of them and would be a second place for them to be wrong.
 *
 * It saves and restores the staging fields it borrows. They are the sysctl surface's scratch, and
 * an operator part-way through composing a request by hand should not find this driver's values in
 * it - the same class of surprise as issue #224, in a different place.
 *
 * allow_write is deliberately not consulted, and not moved either: the post is flagged as this
 * driver's own. That gate exists so a human writing to the far side has to say so first; this is the
 * driver maintaining a table it already owns, on its own schedule, and borrowing the gate to do it
 * is how a cold boot once came up with the gate standing open.
 */
int
octep_rpc_lif_fwd(struct octep_softc *sc, uint32_t iface, uint32_t vlan, uint32_t fwd)
{
	uint32_t s_iface, s_vlan, s_fwd, s_mask, s_cmd;
	int err;

	if (fwd > OCTEP_LIF_FWD_MODE_BOTH)
		return (EINVAL);

	mtx_lock(&sc->mtx);
	s_iface = sc->rpc_lif_iface;
	s_vlan = sc->rpc_lif_vlan;
	s_fwd = sc->rpc_lif_fwd;
	s_mask = sc->rpc_lif_mask;
	s_cmd = sc->rpc_cmd_num;

	sc->rpc_lif_iface = iface;
	sc->rpc_lif_vlan = vlan;
	sc->rpc_lif_fwd = fwd;
	sc->rpc_lif_mask = OCTEP_LIF_M_FWD;
	sc->rpc_cmd_num = OCTEP_RPC_CMD_LIF_ADD_UPDATE;
	sc->rpc_internal = 1;

	err = octep_rpc_post(sc);

	sc->rpc_lif_iface = s_iface;
	sc->rpc_lif_vlan = s_vlan;
	sc->rpc_lif_fwd = s_fwd;
	sc->rpc_lif_mask = s_mask;
	sc->rpc_cmd_num = s_cmd;
	sc->rpc_internal = 0;
	mtx_unlock(&sc->mtx);

	return (err);
}

/*
 * The security association writers the kernel's offload contract drives - octep_ipsec.c - built on
 * the same staging the operator's rpc.sa_* sysctls fill, because the request builder reads it from
 * there. Unlike octep_rpc_lif_fwd these do not put the operator's staging back afterwards: a key has
 * just passed through it, so every field is cleared instead. The operator's instrument stages its own
 * values before each command anyway.
 */
static void
octep_rpc_sa_clear(struct octep_softc *sc)
{
	mtx_assert(&sc->mtx, MA_OWNED);
	sc->rpc_sa_idx = 0;
	sc->rpc_sa_rev = 0;
	sc->rpc_sa_lif = 0;
	sc->rpc_sa_spi = 0;
	sc->rpc_sa_dir = 0;
	sc->rpc_sa_cipher = 0;
	sc->rpc_sa_cimode = 0;
	sc->rpc_sa_hash = 0;
	sc->rpc_sa_mode = 0;
	sc->rpc_sa_proto = 0;
	sc->rpc_sa_arw = 0;
	sc->rpc_sa_win = 0;
	sc->rpc_sa_free = 0;
	sc->rpc_sa_opt = 0;
	sc->rpc_sa_nat_sport = 0;
	sc->rpc_sa_nat_dport = 0;
	sc->rpc_sa_seq = 0;
	memset(sc->rpc_sa_src, 0, sizeof(sc->rpc_sa_src));
	memset(sc->rpc_sa_dst, 0, sizeof(sc->rpc_sa_dst));
	explicit_bzero(sc->rpc_sa_key, sizeof(sc->rpc_sa_key));
	explicit_bzero(sc->rpc_sa_authkey, sizeof(sc->rpc_sa_authkey));
}

/*
 * SA_ADD from a record. The algorithm numbers are the vendor's own enums (docs/families/
 * octeon-tx-rpc.md): GF128_128 is hash 11, CTR is cimode 4, AES-128/192/256 are cipher 2/3/4 by key
 * length, tunnel is mode 1 and ESP proto 1 - not the kernel's IPSEC_MODE_TUNNEL, which is 2. The
 * option word carries the GCM-128 overhead type the vendor sends (2, bits 24..31) rather than
 * leaving the handler to its default. UDP encapsulation is not sent: the caller refuses an
 * association that has it. The sequence is where the kernel's own counter stood, so a peer that has
 * already seen packets from the kernel's cipher is not shown their numbers again.
 * The anti-replay window is only enabled on the decrypt side, as the vendor does. Posted with the
 * same -EAGAIN retry as the operator's SA_ADD: an index still in its grace period answers rc 31 a
 * few times before it takes.
 */
int
octep_rpc_sa_install(struct octep_softc *sc, const struct octep_sa *s)
{
	uint32_t s_cmd;
	int err, again;

	mtx_lock(&sc->mtx);
	s_cmd = sc->rpc_cmd_num;
	sc->rpc_internal = 1;

	sc->rpc_sa_idx = s->idx;
	sc->rpc_sa_rev = s->rev;
	sc->rpc_sa_lif = s->lif;
	sc->rpc_sa_spi = ntohl(s->spi);		/* the builder writes it big-endian again */
	sc->rpc_sa_dir = (uint32_t)s->dir;
	sc->rpc_sa_cipher = s->keylen == 16 ? 2 : (s->keylen == 24 ? 3 : 4);
	sc->rpc_sa_cimode = 4;
	sc->rpc_sa_hash = 11;
	sc->rpc_sa_mode = 1;
	sc->rpc_sa_proto = 1;
	sc->rpc_sa_arw = (s->dir == 1 && s->win != 0) ? 1 : 0;
	sc->rpc_sa_win = s->win;
	sc->rpc_sa_free = 0;
	sc->rpc_sa_opt = 2u << 24;
	sc->rpc_sa_nat_sport = 0;
	sc->rpc_sa_nat_dport = 0;
	sc->rpc_sa_seq = s->seq;
	memset(sc->rpc_sa_src, 0, sizeof(sc->rpc_sa_src));
	memset(sc->rpc_sa_dst, 0, sizeof(sc->rpc_sa_dst));
	sc->rpc_sa_src[0] = ntohl(s->src);
	sc->rpc_sa_dst[0] = ntohl(s->dst);
	memset(sc->rpc_sa_key, 0, sizeof(sc->rpc_sa_key));
	memcpy(sc->rpc_sa_key, s->key, s->keylen);
	memset(sc->rpc_sa_authkey, 0, sizeof(sc->rpc_sa_authkey));
	memcpy(sc->rpc_sa_authkey, s->salt, sizeof(s->salt));
	sc->rpc_cmd_num = OCTEP_RPC_CMD_SA_ADD;

	err = octep_rpc_post(sc);
	for (again = 0; err == 0 && again < OCTEP_RPC_SA_RETRIES &&
	    sc->rpc_last_rc == OCTEP_RPC_SP2FP_RC_EAGAIN; again++) {
		sc->rpc_sa_retries++;
		DELAY(2000);
		err = octep_rpc_post(sc);
	}
	/*
	 * The request still holds the key: the command buffer is poisoned before the NEXT command and
	 * not after this one, and rpc.buf prints it. So the two key fields are cleared - and only
	 * those, and only when the far side has answered. A command that timed out is still on the
	 * ring, and a buffer wiped whole under it would be read later as command 0, which is
	 * FW_STATE_REV_SET with revision 0; and a facility that was never configured has no buffer
	 * at all. Review caught both before this ran.
	 */
	if (err == 0 && sc->rpc_cmd.vaddr != NULL)
		explicit_bzero((uint8_t *)sc->rpc_cmd.vaddr + OCTEP_RPC_BUF_DESC_SIZE + 8, 32 + 64);
	if (err == 0 && sc->rpc_last_rc != 0) {
		sc->rpc_refused++;
		err = EIO;
	}
	octep_rpc_sa_clear(sc);
	sc->rpc_cmd_num = s_cmd;
	sc->rpc_internal = 0;
	mtx_unlock(&sc->mtx);
	return (err);
}

/* SA_DEL, one stage: free_entry 0 invalidates, 1 frees. The caller sends them in that order. */
int
octep_rpc_sa_remove(struct octep_softc *sc, uint32_t idx, int free_entry)
{
	uint32_t s_cmd;
	int err;

	mtx_lock(&sc->mtx);
	s_cmd = sc->rpc_cmd_num;
	sc->rpc_internal = 1;
	sc->rpc_sa_idx = idx;
	sc->rpc_sa_free = free_entry ? 1 : 0;
	sc->rpc_cmd_num = OCTEP_RPC_CMD_SA_DEL;
	err = octep_rpc_post(sc);
	if (err == 0 && sc->rpc_last_rc != 0) {
		sc->rpc_refused++;
		err = EIO;
	}
	sc->rpc_sa_idx = 0;
	sc->rpc_sa_free = 0;
	sc->rpc_cmd_num = s_cmd;
	sc->rpc_internal = 0;
	mtx_unlock(&sc->mtx);
	return (err);
}

/* SA_GET_STATS: the engine's cumulative bytes and packets for one index. */
int
octep_rpc_sa_stats(struct octep_softc *sc, uint32_t idx, uint64_t *bytes, uint64_t *packets)
{
	uint32_t s_cmd;
	int err;

	mtx_lock(&sc->mtx);
	s_cmd = sc->rpc_cmd_num;
	sc->rpc_internal = 1;
	sc->rpc_sa_idx = idx;
	sc->rpc_cmd_num = OCTEP_RPC_CMD_SA_GET_STATS;
	err = octep_rpc_post(sc);
	if (err == 0 && (sc->rpc_last_rc != 0 || sc->rpc_last_len < OCTEP_SA_STATS_RESP_LEN))
		err = EIO;
	if (err == 0) {
		*bytes = le64dec(sc->rpc_last_reply + 0);
		*packets = le64dec(sc->rpc_last_reply + 8);
		/*
		 * Remembered per index, when it says anything: an index reads 0 and 0 from its
		 * SA_ADD until the engine first uses the association, and then reads what its
		 * previous occupants left. This is what the next occupant is measured from when the
		 * index says nothing for itself.
		 */
		if (idx < OCTEP_SA_MAX && (*bytes != 0 || *packets != 0)) {
			sc->ipsec_idx_bytes[idx] = *bytes;
			sc->ipsec_idx_packets[idx] = *packets;
		}
	}
	sc->rpc_sa_idx = 0;
	sc->rpc_cmd_num = s_cmd;
	sc->rpc_internal = 0;
	mtx_unlock(&sc->mtx);
	return (err);
}

/*
 * The flow programmers.
 *
 * Each one is a single command, built from a connection entry in this driver's table rather than
 * from the rpc.* staging sysctls - which it borrows and puts back, so an operator half-way through
 * setting something up by hand does not find it gone. All are called with sc->mtx held, because
 * octep_rpc_post is, and all return an errno: ETIMEDOUT when the far side did not answer, EIO when
 * it answered a write with a non-zero rc. The rc is visible because this driver posts its
 * descriptors without the POST flag; with that flag set the reply is never read back and every
 * refusal is invisible - the vendor host's own blind spot - so the check is skipped when the flag
 * is on, and the operator who set it has chosen that.
 *
 * WHY SEPARATE COMMANDS. The connection block and the microflows are different objects on the far
 * side. FLOW_CREATE_FP copies the connection into its table and then programs whichever microflows
 * the mask selects; MFLOW_PROGRAM programs one microflow and never touches the connection table.
 * The previous programmer sent a FLOW_CREATE_FP to turn a flow OFF, which re-copied whatever
 * connection block happened to be staged over the live entry before touching the microflow.
 * Taking a microflow out is MFLOW_PROGRAM with the same identity and state INACTIVE, nothing else;
 * and a second direction joins a live connection the same way, with state ACTIVE and the
 * connection's index and revision, which is what the vendor's host does with one connection for
 * both of its microflows.
 */
static int
octep_rpc_post_write(struct octep_softc *sc)
{
	int err;

	err = octep_rpc_post(sc);
	if (err != 0)
		return (err);
	if ((sc->rpc_desc_flags & OCTEP_RPC_DESC_POST_FLAG) == 0 && sc->rpc_last_rc != 0) {
		sc->rpc_refused++;
		return (EIO);
	}
	return (0);
}

int
octep_rpc_nhop_program(struct octep_softc *sc, uint32_t idx, uint8_t rev,
    const struct octep_nhop *nh)
{
	uint32_t s_cmd;
	int err;

	mtx_assert(&sc->mtx, MA_OWNED);
	s_cmd = sc->rpc_cmd_num;
	sc->rpc_internal = 1;

	sc->rpc_nhop_index = idx;
	sc->rpc_nhop_rev = rev;
	sc->rpc_nhop_resolved = 1;
	sc->rpc_nhop_mtu = nh->mtu;
	memcpy(sc->rpc_nhop_dmac, nh->dmac, 6);
	memcpy(sc->rpc_nhop_smac, nh->smac, 6);
	sc->rpc_nhop_ethtype = ETHERTYPE_IP;
	sc->rpc_nhop_vlan = 0;
	sc->rpc_nhop_tag = 0;		/* the far side fills it from the interface */
	sc->rpc_nhop_flags = OCTEP_NHOP_FLAG_L3;
	sc->rpc_nhop_iface = nh->iface;
	sc->rpc_cmd_num = OCTEP_RPC_CMD_NHOP_PROGRAM;
	err = octep_rpc_post_write(sc);

	sc->rpc_cmd_num = s_cmd;
	sc->rpc_internal = 0;
	return (err);
}

/*
 * The microflow fields every programmer shares, from one direction of a connection entry. The
 * identity is the fast path's own - the slot and revision read from a punted frame - and the
 * connection and next-hop revisions are the ones this table holds for the indices it names.
 */
static void
octep_rpc_stage_mflow(struct octep_softc *sc, const struct octep_conn *c, int dir, uint32_t state)
{
	const struct octep_conn_mf *m = &c->mf[dir];

	sc->rpc_mflow_id = m->slot;
	sc->rpc_mflow_rev = m->rev;
	sc->rpc_mflow_valid = 1;
	sc->rpc_mflow_dir = (uint32_t)dir;
	sc->rpc_mflow_action = OCTEP_MFLOW_ACTION_FWD;
	sc->rpc_mflow_state = state;
	sc->rpc_mflow_brctl = OCTEP_BRCTL_ROUTED;
	sc->rpc_mflow_conn = c->idx;
	sc->rpc_mflow_conn_rev = c->conn_rev;
	/* dp.flow_timeout, OCTEP_FLOW_AUTO_TIMEOUT unless an operator changed it; see octep.h */
	sc->rpc_mflow_timeout = sc->dp_flow_timeout != 0 ? sc->dp_flow_timeout :
	    OCTEP_FLOW_AUTO_TIMEOUT;
	sc->rpc_mflow_fw_rev = sc->rpc_fw_rev;
	/*
	 * The association this direction's microflow names: the outbound one of a tunnelled
	 * connection's encrypting direction, and nothing for every other microflow there is.
	 * INACTIVE carries it too, unchanged - the far side copies the operation block whole.
	 */
	sc->rpc_mflow_sa = m->sa;
	sc->rpc_mflow_sa_rev = m->sa_rev;
	sc->rpc_mflow_nhop = m->nhop;
	sc->rpc_mflow_nhop_rev = (m->nhop < OCTEP_NHOP_MAX) ? sc->dp_nhop[m->nhop].rev : 0;
}

/*
 * Create the connection and program the microflows the mask selects - both when both directions
 * have been punted, one when only one has. The connection block is complete for both directions
 * either way, because the translation lives in it and not in the microflow, so nothing about it
 * changes when the second direction arrives later by octep_rpc_mflow_set.
 */
int
octep_rpc_flow_create(struct octep_softc *sc, const struct octep_conn *c, uint32_t mask)
{
	uint32_t s_cmd, s_sa[4];
	int err;

	mtx_assert(&sc->mtx, MA_OWNED);
	s_cmd = sc->rpc_cmd_num;
	sc->rpc_internal = 1;
	/*
	 * The association fields are borrowed like the command number and put back like it: left as
	 * the last tunnelled connection set them, an operator's next hand-made flow would name an
	 * association nobody typed.
	 */
	s_sa[0] = sc->rpc_mflow_sa;
	s_sa[1] = sc->rpc_mflow_sa_rev;
	s_sa[2] = sc->rpc_mflow2_sa;
	s_sa[3] = sc->rpc_mflow2_sa_rev;

	sc->rpc_conn_idx = c->idx;
	sc->rpc_conn_rev = c->conn_rev;
	sc->rpc_conn_verdict = OCTEP_CONN_VERDICT_CUT_THRU;
	sc->rpc_conn_state = OCTEP_CONN_STATE_VALID;
	sc->rpc_conn_session = c->idx;
	sc->rpc_conn_snat = c->nat.snat;
	sc->rpc_conn_dnat = c->nat.dnat;
	sc->rpc_conn_orig_src = c->nat.orig_src;
	sc->rpc_conn_orig_sport = c->nat.orig_sport;
	sc->rpc_conn_orig_dst = c->nat.orig_dst;
	sc->rpc_conn_orig_dport = c->nat.orig_dport;
	sc->rpc_conn_nat_src = c->nat.nat_src;
	sc->rpc_conn_nat_sport = c->nat.nat_sport;
	sc->rpc_conn_nat_dst = c->nat.nat_dst;
	sc->rpc_conn_nat_dport = c->nat.nat_dport;

	/* mflow_o is the original direction, from mf[0]; mflow_r the reply, from mf[1]. */
	octep_rpc_stage_mflow(sc, c, OCTEP_CONN_DIR_ORIGINAL, OCTEP_MFLOW_STATE_ACTIVE);
	sc->rpc_mflow_valid = (mask & OCTEP_FLOW_MFLOW_VALID_ORIG) ? 1 : 0;
	sc->rpc_mflow2_id = c->mf[1].slot;
	sc->rpc_mflow2_rev = c->mf[1].rev;
	sc->rpc_mflow2_valid = (mask & OCTEP_FLOW_MFLOW_VALID_REPLY) ? 1 : 0;
	sc->rpc_mflow2_dir = OCTEP_CONN_DIR_REPLY;
	sc->rpc_mflow2_nhop = c->mf[1].nhop;
	sc->rpc_mflow2_nhop_rev = (c->mf[1].nhop < OCTEP_NHOP_MAX) ?
	    sc->dp_nhop[c->mf[1].nhop].rev : 0;
	sc->rpc_mflow2_sa = c->mf[1].sa;
	sc->rpc_mflow2_sa_rev = c->mf[1].sa_rev;
	sc->rpc_flow_valid = mask;
	sc->rpc_cmd_num = OCTEP_RPC_CMD_FLOW_CREATE_FP;
	err = octep_rpc_post_write(sc);

	sc->rpc_mflow_sa = s_sa[0];
	sc->rpc_mflow_sa_rev = s_sa[1];
	sc->rpc_mflow2_sa = s_sa[2];
	sc->rpc_mflow2_sa_rev = s_sa[3];
	sc->rpc_cmd_num = s_cmd;
	sc->rpc_internal = 0;
	return (err);
}

/*
 * Program one direction of a live connection: ACTIVE attaches it, INACTIVE takes it out. Same
 * identity, same connection index and revision, same next hop - the far side's handler copies the
 * operation block over the microflow's and checks the identity's six-bit revision, nothing more.
 */
int
octep_rpc_mflow_set(struct octep_softc *sc, const struct octep_conn *c, int dir, uint32_t state)
{
	uint32_t s_cmd, s_sa[2];
	int err;

	mtx_assert(&sc->mtx, MA_OWNED);
	s_cmd = sc->rpc_cmd_num;
	sc->rpc_internal = 1;
	s_sa[0] = sc->rpc_mflow_sa;
	s_sa[1] = sc->rpc_mflow_sa_rev;

	octep_rpc_stage_mflow(sc, c, dir, state);
	sc->rpc_cmd_num = OCTEP_RPC_CMD_MFLOW_PROGRAM;
	err = octep_rpc_post_write(sc);

	sc->rpc_mflow_sa = s_sa[0];
	sc->rpc_mflow_sa_rev = s_sa[1];
	sc->rpc_cmd_num = s_cmd;
	sc->rpc_internal = 0;
	return (err);
}

int
octep_rpc_conn_reclaim(struct octep_softc *sc, const struct octep_conn *c)
{
	uint32_t s_cmd;
	int err;

	mtx_assert(&sc->mtx, MA_OWNED);
	s_cmd = sc->rpc_cmd_num;
	sc->rpc_internal = 1;

	sc->rpc_conn_idx = c->idx;
	sc->rpc_conn_rev = c->conn_rev;
	sc->rpc_cmd_num = OCTEP_RPC_CMD_CONN_RECLAIM_FP;
	err = octep_rpc_post_write(sc);

	sc->rpc_cmd_num = s_cmd;
	sc->rpc_internal = 0;
	return (err);
}

/*
 * Read one connection entry back and return its state and revision.
 *
 * READ_ALL, because the default read filters out entries the far side considers finished - and a
 * RECLAIM_PENDING entry whose FIN tracking is done is exactly the one this is asked about. The
 * reply is the entry's index, four reserved bytes, then the 108-byte entry; its first word is the
 * atomic flags, revision in the low sixteen bits and state in the top two. An entry the far side
 * did not return comes back with a negative index.
 *
 * And, for a caller that wants to know WHY an entry is not VALID, the entry's own count of
 * identical packets in a row: byte 64 of the entry, which is tcp_seq.retrans in the vendor's
 * struct usfp_conn_entry, read against the device's handler as well as the header. The fast path
 * leaves it at its limit when its retransmission rule is what gave the connection back, and does
 * not touch it for any other reason.
 */
int
octep_rpc_conn_read(struct octep_softc *sc, uint32_t idx, uint32_t *state, uint32_t *rev,
    uint32_t *retrans)
{
	uint32_t s_cmd, s_s, s_e, s_n, s_f, flags;
	int err;

	mtx_assert(&sc->mtx, MA_OWNED);
	s_cmd = sc->rpc_cmd_num;
	s_s = sc->rpc_s_index;
	s_e = sc->rpc_e_index;
	s_n = sc->rpc_num_entries;
	s_f = sc->rpc_req_flags;
	sc->rpc_internal = 1;

	sc->rpc_s_index = idx;
	sc->rpc_e_index = idx;
	sc->rpc_num_entries = 1;
	sc->rpc_req_flags = OCTEP_TABLE_FLAG_READ_ALL;
	sc->rpc_cmd_num = OCTEP_RPC_CMD_LO_CONN_READ;
	err = octep_rpc_post(sc);

	sc->rpc_cmd_num = s_cmd;
	sc->rpc_s_index = s_s;
	sc->rpc_e_index = s_e;
	sc->rpc_num_entries = s_n;
	sc->rpc_req_flags = s_f;
	sc->rpc_internal = 0;
	if (err != 0)
		return (err);
	if (sc->rpc_last_len < 12 || (int32_t)le32dec(sc->rpc_last_reply) < 0 ||
	    le32dec(sc->rpc_last_reply) != idx)
		return (ENOENT);
	flags = le32dec(sc->rpc_last_reply + 8);
	*state = (flags >> 30) & 0x3;
	*rev = flags & 0xffff;
	if (retrans != NULL)
		*retrans = (sc->rpc_last_len >= 8 + 64 + 1) ? sc->rpc_last_reply[8 + 64] : 0;
	return (0);
}

/*
 * Read one microflow entry back: is it there and usable, at which revision, and what does its own
 * timestamp say.
 *
 * READ_ALL, so that the index returned is the index asked for whether or not the entry is valid -
 * see OCTEP_TABLE_FLAG_READ_ALL for what trusting a filtered read cost. The record is the index,
 * four reserved bytes, the 64-byte key, then the entry: its timestamp, its timeout, its flags, and
 * the state word whose bytes are load-balance info, revision, fw_valid and host_valid. The far
 * side stamps the entry at every frame that hits it, before it decides anything about the frame,
 * so two reads with different stamps are frames the host did not see.
 */
int
octep_rpc_mflow_peek(struct octep_softc *sc, uint32_t slot, uint32_t *valid, uint32_t *rev,
    uint32_t *stamp)
{
	const uint8_t *en;
	uint32_t s_cmd, s_s, s_e, s_n, s_f, mst;
	int err;

	mtx_assert(&sc->mtx, MA_OWNED);
	s_cmd = sc->rpc_cmd_num;
	s_s = sc->rpc_s_index;
	s_e = sc->rpc_e_index;
	s_n = sc->rpc_num_entries;
	s_f = sc->rpc_req_flags;
	sc->rpc_internal = 1;

	sc->rpc_s_index = slot;
	sc->rpc_e_index = slot;
	sc->rpc_num_entries = 1;
	sc->rpc_req_flags = OCTEP_TABLE_FLAG_READ_ALL;
	sc->rpc_cmd_num = OCTEP_RPC_CMD_LO_MFLOW_READ;
	err = octep_rpc_post(sc);

	sc->rpc_cmd_num = s_cmd;
	sc->rpc_s_index = s_s;
	sc->rpc_e_index = s_e;
	sc->rpc_num_entries = s_n;
	sc->rpc_req_flags = s_f;
	sc->rpc_internal = 0;
	if (err != 0)
		return (err);
	if (sc->rpc_last_len < OCTEP_MFLOW_RD_ENT_LEN || le32dec(sc->rpc_last_reply) != slot)
		return (ENOENT);
	en = sc->rpc_last_reply + OCTEP_MFLOW_RD_ENTRY_OFF;
	mst = le32dec(en + 12);
	*stamp = le32dec(en + 0);
	*rev = (mst >> 8) & 0xff;
	*valid = (((mst >> 16) & 0xff) != 0 && ((mst >> 24) & 0xff) != 0);
	return (0);
}

/*
 * Read the board's table sizes once, from the platform block, and cap this driver's own bounds by
 * them. Called from the link poll until it has succeeded, because the facility comes up a minute
 * into the boot and nothing else runs at that moment. A reply that stops before the sizes is taken
 * as "not known" and said so once; the host's bounds then stand.
 */
void
octep_rpc_platform_learn(struct octep_softc *sc)
{
	uint32_t s_cmd, s_s, s_e, s_n, s_f;
	const uint8_t *b;
	int err;

	mtx_lock(&sc->mtx);
	if (sc->rpc_plat_learned != 0 || sc->rpc_ready == 0) {
		mtx_unlock(&sc->mtx);
		return;
	}
	s_cmd = sc->rpc_cmd_num;
	s_s = sc->rpc_s_index;
	s_e = sc->rpc_e_index;
	s_n = sc->rpc_num_entries;
	s_f = sc->rpc_req_flags;
	sc->rpc_internal = 1;
	sc->rpc_s_index = 0;
	sc->rpc_e_index = 0;
	sc->rpc_num_entries = 1;
	sc->rpc_req_flags = 0;
	sc->rpc_cmd_num = OCTEP_RPC_CMD_PLATFORM_READ;
	err = octep_rpc_post(sc);
	sc->rpc_cmd_num = s_cmd;
	sc->rpc_s_index = s_s;
	sc->rpc_e_index = s_e;
	sc->rpc_num_entries = s_n;
	sc->rpc_req_flags = s_f;
	sc->rpc_internal = 0;
	if (err != 0) {
		mtx_unlock(&sc->mtx);
		return;
	}
	sc->rpc_plat_learned = 1;
	if (sc->rpc_last_len >= OCTEP_PLATFORM_INFO_WITH_SIZES) {
		b = sc->rpc_last_reply;
		sc->rpc_plat_def_mflow_to = le32dec(b + OCTEP_PLATFORM_OFF_DEF_MFLOW_TO);
		sc->rpc_plat_max_conn = le32dec(b + OCTEP_PLATFORM_OFF_MAX_CONN);
		sc->rpc_plat_max_nhop = le32dec(b + OCTEP_PLATFORM_OFF_MAX_NHOP);
		sc->rpc_plat_num_mflows = le32dec(b + OCTEP_PLATFORM_OFF_NUM_MFLOWS);
		if (sc->rpc_plat_max_conn != 0 && sc->rpc_plat_max_conn < sc->dp_conn_max)
			sc->dp_conn_max = sc->rpc_plat_max_conn;
		if (sc->rpc_plat_max_nhop != 0 && sc->rpc_plat_max_nhop < sc->dp_nhop_max)
			sc->dp_nhop_max = sc->rpc_plat_max_nhop;
		device_printf(sc->dev, "rpc: the board holds %u connections, %u next hops and %u "
		    "microflows, microflow timeout %u s; this host will use at most %u and %u\n",
		    sc->rpc_plat_max_conn, sc->rpc_plat_max_nhop, sc->rpc_plat_num_mflows,
		    sc->rpc_plat_def_mflow_to, sc->dp_conn_max, sc->dp_nhop_max);
	} else {
		device_printf(sc->dev, "rpc: the platform block is %u bytes and stops before its "
		    "table sizes; using this host's own bounds, %u connections and %u next hops\n",
		    sc->rpc_last_len, sc->dp_conn_max, sc->dp_nhop_max);
	}
	mtx_unlock(&sc->mtx);
}

/*
 * Bump the firewall revision, which throws away every offloaded flow.
 *
 * This is what a ruleset reload needs and the only thing it needs. fw_state_fpop_rev_set stores the
 * halfword and then calls mflow_fpop_invalidate_issue over the whole table, so one command with no
 * operand discards everything the old ruleset authorised. The host does not have to know which
 * flows existed, which is the point: it cannot.
 *
 * WHY A TASK. The caller is filter_configure_sync(), holding a lock on the generated ruleset that
 * every other reload queues behind, and a posted command waits up to OCTEP_RPC_CMD_WAIT_MS for its
 * reply. Charging two seconds of that to a reload would stall every reload behind it. The sysctl
 * enqueues this and returns, and because taskqueue_enqueue coalesces a task that has not started
 * yet, a burst of reloads - three happen during one boot - collapses into one or two bumps, each of
 * which discards the whole table anyway.
 *
 * WHY IT DOES NOT NEED rpc.allow_write. The gate exists so that nothing writes the coprocessor's
 * forwarding state by accident. This is not a general write: it is one command with no operand,
 * which cannot be aimed at anything else, and it is issued by the driver rather than handed to it.
 * It opens and restores the gate under the lock, exactly as the flow programmer does, so no other
 * user of the gate can see it move.
 *
 * THE REVISION IS SIXTEEN BITS and wraps at 65536 bumps. A stale entry's revision can match again
 * after a wrap - but every bump invalidates the whole table, so reaching a wrap with a stale entry
 * still in it would need 65536 bumps in which the invalidate never worked, and in that case the
 * revision is not what is wrong.
 *
 * NOT command 1. FW_L3_FWD_STATE_REV_SET is one along in the enumeration, one letter apart in the
 * sysctls, and does not invalidate: it would change a number and discard nothing.
 */
static void
octep_rpc_fw_rev_bump_task(void *arg, int pending __unused)
{
	struct octep_softc *sc = arg;
	uint32_t s_cmd, prev;
	int err;

	mtx_lock(&sc->mtx);

	/*
	 * Silent when there is nothing to tell, which is a normal state and not a failure. The
	 * first of a boot's three reloads runs before the coprocessor has been handshaken; the
	 * module may also have declined to attach, or be loaded with no datapath. The counter is
	 * the report.
	 */
	if (sc->rpc_bump_stop != 0) {
		mtx_unlock(&sc->mtx);
		return;
	}
	if (!sc->rpc_ready) {
		sc->rpc_fw_rev_bump_early++;
		mtx_unlock(&sc->mtx);
		return;
	}

	s_cmd = sc->rpc_cmd_num;
	prev = sc->rpc_fw_rev;

	sc->rpc_internal = 1;
	sc->rpc_fw_rev = (prev + 1) & 0xffffu;
	sc->rpc_cmd_num = OCTEP_RPC_CMD_FW_STATE_REV_SET;
	/*
	 * Loud the first time it fails and silent after that. Every other print in octep_rpc_post is
	 * unreachable from here - the command is 0, it is in the allowed-write list, the gate is open
	 * two lines up, and rpc_ready was checked - so the only one this suppresses is the timeout.
	 */
	sc->rpc_quiet = (sc->rpc_fw_rev_bump_fail != 0);
	err = octep_rpc_post(sc);
	sc->rpc_quiet = 0;

	if (err == 0) {
		/*
		 * And the value every flow programmed from now on must carry. These two are one
		 * number in two places; they are kept equal here because this is the only thing that
		 * changes either of them.
		 */
		sc->rpc_mflow_fw_rev = sc->rpc_fw_rev;
		sc->rpc_fw_rev_bumps++;
		/*
		 * And this driver's own record of them, which is now a list of flows that do not
		 * exist. Only on success: a bump the far side did not take discarded nothing.
		 */
		octep_dp_flows_forget(sc);
	} else {
		/*
		 * Put it back. The field means "the revision the far side is checking against", and
		 * a post that failed did not change what the far side checks. Either way the failure
		 * is on the safe side: host and far side disagreeing means new flows are refused, not
		 * that stale ones are kept.
		 */
		sc->rpc_fw_rev = prev;
		sc->rpc_fw_rev_bump_fail++;
	}

	sc->rpc_cmd_num = s_cmd;
	sc->rpc_internal = 0;
	mtx_unlock(&sc->mtx);
}

/*
 * The sysctl a ruleset reload writes. It enqueues and returns; see the task above for why.
 *
 * Write-only and the value is ignored - there is nothing to say but "the ruleset changed". Reading
 * it would have to mean something, and the thing worth reading is rpc.fw_rev and the three counters
 * beside it.
 */
static int
octep_sysctl_rpc_fw_rev_bump(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	int error, v;

	v = 0;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	/*
	 * Under the lock, against detach. A write that gets past this check has enqueued before
	 * detach drains, so the drain waits for it; one arriving after the flag is set does not
	 * enqueue at all. There is no third case.
	 */
	mtx_lock(&sc->mtx);
	if (sc->rpc_bump_stop == 0)
		taskqueue_enqueue(taskqueue_thread, &sc->rpc_bump_task);
	mtx_unlock(&sc->mtx);
	return (0);
}

void
octep_rpc_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
    struct sysctl_oid *node)
{

	TASK_INIT(&sc->rpc_bump_task, 0, octep_rpc_fw_rev_bump_task, sc);

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "configure",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_rpc_configure, "I",
	    "write 1 to publish a ring configuration. This makes the target tear its RPC rings "
	    "down and build them again. It is a write like the posted ones, and rpc.cmd lists which "
	    "of those are permitted");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "post",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_rpc_post, "I",
	    "write 1 to post the command in rpc.cmd with the range in rpc.s_index and friends");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "state",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_rpc_state, "A", "the cfg word and ring_lo, as the window has them");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "buf",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_rpc_buf, "A",
	    "the command buffer, raw. Poisoned before each command, so an untouched byte shows");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "last",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_rpc_last, "A", "what the last command returned");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "wait_last_us",
	    CTLFLAG_RD, &sc->rpc_wait_last, 0,
	    "microseconds the last answered command was waited for, with the transmit path's lock "
	    "held. In steps of twenty for the first two milliseconds and of a thousand after");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "wait_max_us",
	    CTLFLAG_RW, &sc->rpc_wait_max, 0, "the longest such wait; write 0 to start again");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "wait_sum_us",
	    CTLFLAG_RD, &sc->rpc_wait_sum, 0, "all such waits added up");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "wait_n",
	    CTLFLAG_RD, &sc->rpc_wait_n, 0, "and how many there were: the two give the mean");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_retries",
	    CTLFLAG_RD, &sc->rpc_sa_retries, 0,
	    "SA_ADD commands posted again because the far side answered an encoded -EAGAIN (rc 31): "
	    "its RCU grace period for that index was not over. SA_DEL never answers it. The vendor's "
	    "own caller retries the same way");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "refused",
	    CTLFLAG_RD, &sc->rpc_refused, 0,
	    "writes this driver posted for itself that the far side answered with a non-zero rc. "
	    "Each one was a flow, next hop or reclaim it then did not record as done");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "allow_write",
	    CTLFLAG_RW, &sc->rpc_allow_write, 0,
	    "set to 1 before a command that changes state on the far side. Seven are permitted at "
	    "all - 0, 1 and 2 for the firewall state, 3 LIF_ADD_UPDATE, 5 PPORT_UPDATE, 30 SA_ADD "
	    "and 31 SA_DEL - and every other write is refused by number. This stays set until it is "
	    "cleared, so clear it when the writing is done");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_iface",
	    CTLFLAG_RW, &sc->rpc_lif_iface, 0,
	    "the interface id, seven bits. It is the high half of a LIF index and the key of the "
	    "port mapping");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_vlan",
	    CTLFLAG_RW, &sc->rpc_lif_vlan, 0, "the VLAN, twelve bits, the low half of a LIF index");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_tag",
	    CTLFLAG_RW, &sc->rpc_lif_tag, 0,
	    "the port tag PPORT_UPDATE binds to lif_iface: 0x0001 and 0x0002 are the two SFP cages, "
	    "0x8100 to 0x8a00 the switch ports");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_mac",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_rpc_lif_mac, "A",
	    "the address this interface answers to. FROM_WIRE_DROP_LIF_NOT_MY_MAC is what a wrong "
	    "one looks like from the other side");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_mtu",
	    CTLFLAG_RW, &sc->rpc_lif_mtu, 0, "bytes");

	/*
	 * The next-hop entry a NHOP_PROGRAM carries. A flow's action names one of these by index,
	 * and it is what tells the fast path where a matched frame goes: the neighbour's address,
	 * ours, and the port. The whole table reads back as zeros on this appliance.
	 */
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_index",
	    CTLFLAG_RW, &sc->rpc_nhop_index, 0, "which entry NHOP_PROGRAM writes");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_dmac",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_rpc_nhop_mac, "A", "the neighbour this next hop reaches");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_smac",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_NEEDGIANT, sc, 1,
	    octep_sysctl_rpc_nhop_mac, "A", "the address the frame leaves with, which is the "
	    "egress port's own");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_ethtype",
	    CTLFLAG_RW, &sc->rpc_nhop_ethtype, 0, "0x0800 for IPv4, 0x86dd for IPv6");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_vlan",
	    CTLFLAG_RW, &sc->rpc_nhop_vlan, 0, "0 for untagged");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_tag",
	    CTLFLAG_RW, &sc->rpc_nhop_tag, 0,
	    "the egress port tag as staged, and the far side IGNORES it: its handler overwrites the "
	    "field from its own iface-to-tag table on every program and update. To point a next hop "
	    "at the host's own port, remap the egress interface's tag with PPORT_UPDATE first");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_flags",
	    CTLFLAG_RW, &sc->rpc_nhop_flags, 0, "bit 0 L3, bit 1 IPsec");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_iface",
	    CTLFLAG_RW, &sc->rpc_nhop_iface, 0, "the egress logical interface");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_mtu",
	    CTLFLAG_RW, &sc->rpc_nhop_mtu, 0, "bytes");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_resolved",
	    CTLFLAG_RW, &sc->rpc_nhop_resolved, 0,
	    "1 when the neighbour's address is known. An unresolved entry is one the fast path "
	    "cannot send through");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_rev",
	    CTLFLAG_RW, &sc->rpc_nhop_rev, 0,
	    "the revision a microflow's action has to match, as the LIF and the flow do");

	/*
	 * The microflow a MFLOW_PROGRAM carries. mflow_id is read from a punted frame's metadata -
	 * dp.rx_prefix prints it - and not computed here.
	 */
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_id",
	    CTLFLAG_RW, &sc->rpc_mflow_id, 0,
	    "the slot to program, as the punted frame reported it");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_rev",
	    CTLFLAG_RW, &sc->rpc_mflow_rev, 0, "the revision the frame reported with it");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_valid",
	    CTLFLAG_RW, &sc->rpc_mflow_valid, 0, "the valid bit of the identity, normally 1");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_action",
	    CTLFLAG_RW, &sc->rpc_mflow_action, 0,
	    "four bits. MF_ACT_DROP is 0, measured: with mflow_state at 2, action 0 raises "
	    "FROM_WIRE_DROP_MFLOW_ACTION on the first frame. MF_ACT_FWD, MF_ACT_IPS and MF_ACT_AUX "
	    "are named in the vendor's source and defined in a tree it does not ship, and telling "
	    "them apart needs a resolved next hop - without one they all land on NHOP_UNRESOLVED");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_dir",
	    CTLFLAG_RW, &sc->rpc_mflow_dir, 0,
	    "which direction of the connection: 0 the original, 1 the reply. The far side indexes per-direction state by it - the TCP window in seen[dir], the QoS block, the window scale - so a flow carrying the wrong one is matched against the opposite direction and its frames are built wrong and transmitted, with nothing counting a drop");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_state",
	    CTLFLAG_RW, &sc->rpc_mflow_state, 0,
	    "four bits, and the one that decides whether the flow is used at all. MF_ACTIVE is 2: "
	    "at any other value the write still lands and reads back, and the fast path still punts "
	    "every frame onto FROM_WIRE_TO_KN_MFLOW_NOT_ACTIVE");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_brctl",
	    CTLFLAG_RW, &sc->rpc_mflow_brctl, 0,
	    "bridge control: bit 0 overwrite VLAN, 1 overwrite dst MAC, 2 overwrite src MAC, "
	    "3 update TTL");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_conn",
	    CTLFLAG_RW, &sc->rpc_mflow_conn, 0, "the connection entry this flow belongs to");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_conn_rev",
	    CTLFLAG_RW, &sc->rpc_mflow_conn_rev, 0, "and its revision");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_fw_rev",
	    CTLFLAG_RW, &sc->rpc_mflow_fw_rev, 0,
	    "the firewall state revision. A ruleset reload bumps it and invalidates every flow "
	    "that still carries the old one, which is the mechanism a reload needs");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_nhop",
	    CTLFLAG_RW, &sc->rpc_mflow_nhop, 0, "the next-hop entry a matched frame leaves by");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_nhop_rev",
	    CTLFLAG_RW, &sc->rpc_mflow_nhop_rev, 0, "and its revision, as rpc.nhop_rev set it");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_sa",
	    CTLFLAG_RW, &sc->rpc_mflow_sa, 0, "the association's handle: its SA_ADD index plus one; 0 for no offload");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_sa_rev",
	    CTLFLAG_RW, &sc->rpc_mflow_sa_rev, 0,
	    "and its revision - this is the field #185 turned out to be waiting on");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow_timeout",
	    CTLFLAG_RW, &sc->rpc_mflow_timeout, 0, "seconds");

	/*
	 * The second direction, for FLOW_CREATE_FP. Everything else about it is the mflow_* above:
	 * the two directions of one flow share the action, the connection and the timeout, and
	 * differ in which slot they are and which way out they point.
	 */
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_len",
	    CTLFLAG_RW, &sc->rpc_flow_len, 0,
	    "how many bytes FLOW_CREATE_FP declares. The handler refuses anything shorter than its "
	    "own sizeof and says nothing about what it wanted, so this exists to find that size by "
	    "bisection. It has not found it: every length from 180 to 256 is accepted, including one "
	    "that an earlier module refused, so the sizeof is at most 180 and the earlier refusal "
	    "had some other cause. Zero means OCTEP_FLOW_REQ_LEN");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_valid",
	    CTLFLAG_RW, &sc->rpc_flow_valid, 0,
	    "which directions FLOW_CREATE_FP carries: bit 0 the original, bit 1 the reply. A flow "
	    "with one direction programmed IS used - forwarded, then handed back within a few "
	    "frames as a half-offloaded connection; measured, see docs/one-connection-two-microflows.md");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow2_id",
	    CTLFLAG_RW, &sc->rpc_mflow2_id, 0, "the second direction's slot");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow2_rev",
	    CTLFLAG_RW, &sc->rpc_mflow2_rev, 0, "and its revision");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow2_valid",
	    CTLFLAG_RW, &sc->rpc_mflow2_valid, 0, "the valid bit of its identity");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow2_dir",
	    CTLFLAG_RW, &sc->rpc_mflow2_dir, 0, "normally the opposite of mflow_dir");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow2_nhop",
	    CTLFLAG_RW, &sc->rpc_mflow2_nhop, 0,
	    "the next hop the second direction leaves by, which is a different port from the "
	    "first - that is what makes it the other direction");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow2_nhop_rev",
	    CTLFLAG_RW, &sc->rpc_mflow2_nhop_rev, 0, "and its revision");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow2_sa",
	    CTLFLAG_RW, &sc->rpc_mflow2_sa, 0,
	    "the association the second direction of a hand-made FLOW_CREATE_FP names, as "
	    "rpc.mflow_sa is the first's; 0 for none");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "mflow2_sa_rev",
	    CTLFLAG_RW, &sc->rpc_mflow2_sa_rev, 0, "and its revision");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_fwd",
	    CTLFLAG_RW, &sc->rpc_lif_fwd, 0,
	    "forwarding mode: 0 invalid, 1 L2, 2 L3, 3 both. Zero is what an unused entry holds, so "
	    "it is also how the gate recognises one");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_admin_dis",
	    CTLFLAG_RW, &sc->rpc_lif_admin_dis, 0, "administratively down");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_offload_dis",
	    CTLFLAG_RW, &sc->rpc_lif_offload_dis, 0,
	    "the bit that makes the fast path punt to the host instead of accelerating. This is the "
	    "one the return direction needs");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_reppid",
	    CTLFLAG_RW, &sc->rpc_lif_reppid, 0, "representor port id, twelve bits");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_df",
	    CTLFLAG_RW, &sc->rpc_lif_df, 0, "the deep-inspection bit the vendor calls DF");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_mask",
	    CTLFLAG_RW, &sc->rpc_lif_mask, 0,
	    "which fields the update carries: 0x01 MAC, 0x02 MTU, 0x04 forwarding mode, "
	    "0x08 admin, 0x10 offload, 0x20 representor - and two more the handler requires that "
	    "have no name, 0x40 and 0x80. A NEW entry is refused unless the mask is 0xff exactly; "
	    "an EXISTING one is refused if it IS 0xff");

	/*
	 * struct fw_state. All three are writes, so rpc.allow_write must be set first.
	 *
	 * Nothing else is gated on these: PPORT_UPDATE, LIF_ADD_UPDATE, SA_ADD and every read were
	 * verified on this appliance before the driver could issue them at all, and with
	 * FW_CFG_OFFLOAD clear the from-wire path still resolves a tag and a LIF and delivers every
	 * frame to the host. What fw_cfg gates is acceleration, and only that.
	 */
	/*
	 * The connection table. A flow is created by the fast path and never by the host, so this
	 * is what is left to publish: a connection the fast path can find. Writes, so
	 * rpc.allow_write first.
	 */
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_idx",
	    CTLFLAG_RW, &sc->rpc_conn_idx, 0,
	    "which slot in the connection table to write. Bounds-checked by the far side against "
	    "the table size and nothing else");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_rev",
	    CTLFLAG_RW, &sc->rpc_conn_rev, 0,
	    "the connection's revision, which a flow entry carries and is checked against");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_verdict",
	    CTLFLAG_RW, &sc->rpc_conn_verdict, 0,
	    "two bits; 2, CUT_THRU, is what this driver sends and what was measured to forward. The "
	    "vendor's source names three verdicts and gives none of them a number");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_state",
	    CTLFLAG_RW, &sc->rpc_conn_state, 0,
	    "0 invalid, 1 valid, 2 reclaim pending, 3 reclaimed. The vendor writes 1");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_session",
	    CTLFLAG_RW, &sc->rpc_conn_session, 0, "the session id this connection belongs to");

	/*
	 * struct usfp_nat_info. Nothing is written unless snat or dnat is set, so these are inert
	 * until asked for - which matters, because the base offset is not yet confirmed.
	 */
	sc->rpc_conn_nat_off = OCTEP_CONN_OFF_NAT;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_nat_off",
	    CTLFLAG_RW, &sc->rpc_conn_nat_off, 0,
	    "where struct usfp_nat_info starts in a FLOW_CREATE_FP request. The default is where the "
	    "vendor's declared member sizes put it, and those sum to eight bytes less than the "
	    "connection entry measurably is - so this is settable to find the real base by writing a "
	    "marker and reading it back with LO_CONN_READ. A value that would write past the request "
	    "is ignored");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_snat",
	    CTLFLAG_RW, &sc->rpc_conn_snat, 0,
	    "do_snat, bit 20 of the connection's flags: translate the source. Nothing in the NAT "
	    "block is written unless this or conn_dnat is set");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_dnat",
	    CTLFLAG_RW, &sc->rpc_conn_dnat, 0, "do_dnat, bit 19: translate the destination");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_orig_src",
	    CTLFLAG_RW, &sc->rpc_conn_orig_src, 0, "ipv4_orig_src, in network order");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_orig_dst",
	    CTLFLAG_RW, &sc->rpc_conn_orig_dst, 0, "ipv4_orig_dest, in network order");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_orig_sport",
	    CTLFLAG_RW, &sc->rpc_conn_orig_sport, 0, "orig_src_port, in network order");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_orig_dport",
	    CTLFLAG_RW, &sc->rpc_conn_orig_dport, 0, "orig_dest_port, in network order");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_nat_src",
	    CTLFLAG_RW, &sc->rpc_conn_nat_src, 0, "ipv4_nat_src, in network order");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_nat_dst",
	    CTLFLAG_RW, &sc->rpc_conn_nat_dst, 0, "ipv4_nat_dest, in network order");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_nat_sport",
	    CTLFLAG_RW, &sc->rpc_conn_nat_sport, 0, "nat_src_port, in network order");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_nat_dport",
	    CTLFLAG_RW, &sc->rpc_conn_nat_dport, 0, "nat_dest_port, in network order");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fw_cfg",
	    CTLFLAG_RW, &sc->rpc_fw_cfg, 0,
	    "the global configuration word: 0x001 OFFLOAD, 0x002 TCP_SEQ_CHK, 0x004 IPS, "
	    "0x008 FINTRACK, 0x010 FP_PKT_DUMP, 0x020 INJ_RECOVERY, 0x800 DROP_IF_IPS_OFF. There is "
	    "no command that reads this word back, so a write replaces a value nobody has seen: "
	    "always set every bit you want rather than the one you are changing. OFFLOAD is measured "
	    "to be clear after a coprocessor start - every wire frame is forced to the host - and "
	    "what the other bits hold at that point has not been read");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fw_rev",
	    CTLFLAG_RW, &sc->rpc_fw_rev, 0,
	    "the firewall revision the flow and connection entries are checked against. Setting it "
	    "invalidates every offloaded flow, which is what a ruleset reload needs");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fw_l3_rev",
	    CTLFLAG_RW, &sc->rpc_fw_l3_rev, 0,
	    "the layer-three forwarding revision, checked separately and with no invalidate");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fw_rev_bump",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0,
	    octep_sysctl_rpc_fw_rev_bump, "I",
	    "write anything to increment rpc.fw_rev and post it, which discards every offloaded "
	    "flow. This is what a ruleset reload needs: it takes no operand, it does not need "
	    "rpc.allow_write because it cannot be aimed at anything else, and it returns before the "
	    "command is posted so that a reload is never charged the wait. What happened is in "
	    "rpc.fw_rev and the three counters beside it");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fw_rev_bumps",
	    CTLFLAG_RD, &sc->rpc_fw_rev_bumps, 0,
	    "revision bumps the far side took, each one having discarded the whole flow table");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fw_rev_bump_fail",
	    CTLFLAG_RD, &sc->rpc_fw_rev_bump_fail, 0,
	    "bumps posted and refused. The flow table may still hold flows the old ruleset "
	    "authorised, which is the one failure here that matters");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fw_rev_bump_early",
	    CTLFLAG_RD, &sc->rpc_fw_rev_bump_early, 0,
	    "bumps asked for before the RPC facility was up. Not a failure: there are no flows to "
	    "discard before the handshake, and three ruleset reloads happen during a boot");

	/*
	 * The security association. Everything here is a field of struct usfp_fpop_req_sa_add,
	 * and the numbers are the vendor's own enums - see docs/families/octeon-tx-rpc.md.
	 *
	 * Installing one is a write, so rpc.allow_write must be set first, deliberately.
	 */
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_idx",
	    CTLFLAG_RW, &sc->rpc_sa_idx, 0,
	    "the association index. A frame names this handle in its metadata, so it is also what dp.sa_idx carries");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_rev",
	    CTLFLAG_RW, &sc->rpc_sa_rev, 0,
	    "the association revision, compared by the lookup. The vendor's host pre-increments a "
	    "counter per index, so its first association at a fresh index carries 1 and never 0");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_lif",
	    CTLFLAG_RW, &sc->rpc_sa_lif, 0,
	    "the logical interface this association belongs to");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_spi",
	    CTLFLAG_RW, &sc->rpc_sa_spi, 0,
	    "the security parameter index, big-endian on the wire");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_dir",
	    CTLFLAG_RW, &sc->rpc_sa_dir, 0,
	    "0 outbound and encrypting, 1 inbound and decrypting");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_cipher",
	    CTLFLAG_RW, &sc->rpc_sa_cipher, 0,
	    "0 none, 1 3DES, 2 AES128, 3 AES192, 4 AES256, 8 ChaCha20");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_cimode",
	    CTLFLAG_RW, &sc->rpc_sa_cimode, 0,
	    "0 ECB, 1 CBC, 2 CFB, 3 OFB, 4 CTR. AES-GCM is CTR");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_hash",
	    CTLFLAG_RW, &sc->rpc_sa_hash, 0,
	    "0 none, 2 SHA1_96, 8 SHA256_128, 11 GF128_128 which is what AES-GCM authenticates with");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_mode",
	    CTLFLAG_RW, &sc->rpc_sa_mode, 0,
	    "0 transport, 1 tunnel");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_proto",
	    CTLFLAG_RW, &sc->rpc_sa_proto, 0,
	    "the protocol field, two bits");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_arw",
	    CTLFLAG_RW, &sc->rpc_sa_arw, 0,
	    "enable the anti-replay window");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_win",
	    CTLFLAG_RW, &sc->rpc_sa_win, 0,
	    "the anti-replay window size");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_opt",
	    CTLFLAG_RW, &sc->rpc_sa_opt, 0,
	    "the option word: overhead type in bits 24..31 (2 is GCM-128, which the handler also "
	    "takes 0 to mean), udp_enable bit 22 with the two NAT-T ports below");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_nat_sport",
	    CTLFLAG_RW, &sc->rpc_sa_nat_sport, 0, "UDP encapsulation source port, host order");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_nat_dport",
	    CTLFLAG_RW, &sc->rpc_sa_nat_dport, 0, "UDP encapsulation destination port, host order");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_seq",
	    CTLFLAG_RW, &sc->rpc_sa_seq, 0,
	    "the sequence number the association starts after: the far side's counter is this "
	    "plus one for an encrypt association, and its window head for a decrypt one");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_free",
	    CTLFLAG_RW, &sc->rpc_sa_free, 0,
	    "SA_DEL only, and SA_DEL is TWO stages: post with 0 first, which invalidates the entry, then "
	    "with 1, which frees it and arms the fast path's grace period for the index. Measured: a 1 "
	    "sent to a still-valid entry answers ok and frees nothing - the far side logs 'Attempted to "
	    "free a valid SA' and the next SA_ADD on that index is refused with rc 2");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_src",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_rpc_sa_addr, "A",
	    "tunnel source address");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_dst",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_NEEDGIANT, sc, 1,
	    octep_sysctl_rpc_sa_addr, "A",
	    "tunnel destination address");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_key",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_rpc_sa_key, "A",
	    "the cipher key as hex, 32 bytes. Test material only: a sysctl is readable and a production key has no business passing through one");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_authkey",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_NEEDGIANT, sc, 1,
	    octep_sysctl_rpc_sa_key, "A",
	    "the authentication key as hex, 64 bytes, or the AEAD salt in its first word");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmd",
	    CTLFLAG_RW, &sc->rpc_cmd_num, 0,
	    "which command to post. Reads: 32 sa stats, 36 platform, 37 lif, 38 conn, 39 nhop, 40 mflow, "
	    "41 luid, 42 sa, 43 dbg counters, 44 sys counters, 45 port counters, 46 dragonfly "
	    "counters. Writes: 0 FW_STATE_REV_SET, 1 FW_L3_FWD_STATE_REV_SET, 2 FW_CFG_PARAMS_SET, "
	    "3 LIF_ADD_UPDATE, 5 PPORT_UPDATE, 6 NHOP_PROGRAM, 8 MFLOW_PROGRAM, 11 CONN_CREATE_FP, "
	    "10 FLOW_CREATE_FP, 30 SA_ADD, 31 SA_DEL - every one of those is refused unless "
	    "allow_write is set, and everything else in the enumeration is refused outright. SA_ADD "
	    "answers rc 2 for an index already in use and installs nothing, so read rpc.last before "
	    "believing anything measured after it");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "s_index",
	    CTLFLAG_RW, &sc->rpc_s_index, 0, "first index wanted");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "e_index",
	    CTLFLAG_RW, &sc->rpc_e_index, 0, "last index wanted");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "num_entries",
	    CTLFLAG_RW, &sc->rpc_num_entries, 0, "how many entries");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "desc_flags",
	    CTLFLAG_RW, &sc->rpc_desc_flags, 0,
	    "the descriptor flags. 2, NO_AGG_DMA, is what gets an answer; 1, POST, means posted in "
	    "the PCIe sense and the target writes nothing back. Measured, all three combinations");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "req_flags",
	    CTLFLAG_RW, &sc->rpc_req_flags, 0,
	    "the flags usfp_table_print.sh passes as I, C and D - include invalid, clear, describe");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "resp_sz",
	    CTLFLAG_RW, &sc->rpc_resp_sz, 0, "how much room to declare for the answer");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "dbell",
	    CTLFLAG_RW, &sc->rpc_dbell, 0,
	    "which of the facility's doorbells the low ring uses, as an index rather than an SPI. "
	    "The target divides it by the DMA device count to choose an engine");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "shared",
	    CTLFLAG_RW, &sc->rpc_shared, 0,
	    "the ring's shared flag. Zero takes the target down the path that frees and re-requests "
	    "the doorbell interrupt; this is the one field here that is a reading rather than a "
	    "measurement, so it is settable");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "desc_offset",
	    CTLFLAG_RW, &sc->rpc_desc_off, 0,
	    "where in the window the descriptor ring goes, past the 232-byte state block");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "desc_count",
	    CTLFLAG_RW, &sc->rpc_desc_count, 0,
	    "how many descriptors. Must be a power of two - the target masks with desc_count-1");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "ready",
	    CTLFLAG_RD, &sc->rpc_ready, 0, "whether the target has acknowledged a configuration");
}
