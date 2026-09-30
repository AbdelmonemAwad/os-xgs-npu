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
 * was running it - and three writes, each named and each gated behind rpc.allow_write: the ring
 * configuration, PPORT_UPDATE, and LIF_ADD_UPDATE. The last two are what open the return
 * direction, and this header said the opposite of that for as long as they did not work. Nothing
 * here writes a flow or a connection.
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
static int
octep_rpc_post(struct octep_softc *sc)
{
	uint8_t *buf, *p;
	uint16_t reqlen;
	bus_size_t desc;
	uint64_t posted, done;
	uint32_t idx;
	uint16_t rc, plen;
	int i;

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
		    "commands, and two writes that install a port mapping and a logical interface. "
		    "Nothing else\n", sc->rpc_cmd_num);
		return (EPERM);
	}
	if (!octep_rpc_cmd_is_read(sc->rpc_cmd_num) && sc->rpc_allow_write == 0) {
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
		le16enc(p + 14, (uint16_t)(sc->rpc_lif_df & 1));
		le16enc(p + 16, (uint16_t)sc->rpc_lif_mask);
		reqlen = 18;
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
	sc->rpc_last_rc = 0;
	sc->rpc_last_len = 0;
	sc->rpc_last_error = 0;

	for (i = 0; i < OCTEP_RPC_CMD_WAIT_MS; i++) {
		done = octep_rpc_rd8(sc, OCTEP_RPC_STATE_RING_LO + OCTEP_RPC_RING_DONE);
		if (done > posted) {
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
		DELAY(1000);
	}

	sc->rpc_last_error = ETIMEDOUT;
	sc->rpc_timeouts++;
	device_printf(sc->dev, "rpc: command %u timed out after %d ms; posted %ju, done %ju\n",
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
	    sc->rpc_last_rc,
	    (sc->rpc_last_rc & OCTEP_RPC_RC_ERRNO_BIT) ? " (an errno, not a length)" :
	    (sc->rpc_last_rc == 0 ? " (ok)" : ""),
	    sc->rpc_last_done, sc->rpc_last_seed, sc->rpc_last_len);

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

void
octep_rpc_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
    struct sysctl_oid *node)
{

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "configure",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_NEEDGIANT, sc, 0,
	    octep_sysctl_rpc_configure, "I",
	    "write 1 to publish a ring configuration. This makes the target tear its RPC rings "
	    "down and build them again. One of the three writes this facility permits, the others "
	    "being PPORT_UPDATE and LIF_ADD_UPDATE");
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

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "allow_write",
	    CTLFLAG_RW, &sc->rpc_allow_write, 0,
	    "set to 1 before a command that changes state on the far side. Two are permitted at "
	    "all - 5 PPORT_UPDATE and 3 LIF_ADD_UPDATE - and every other write is refused by number");
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
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmd",
	    CTLFLAG_RW, &sc->rpc_cmd_num, 0,
	    "which command to post: 36 platform, 37 lif, 38 conn, 39 nhop, 40 mflow, 41 luid, "
	    "42 sa, 43 dbg counters, 44 sys counters, 45 port counters, 46 dragonfly counters, "
	    "5 PPORT_UPDATE, 3 LIF_ADD_UPDATE. The last two write, and are refused unless "
	    "allow_write is set; everything else that writes is refused outright");
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
