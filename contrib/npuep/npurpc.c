/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The control-message channel.
 *
 * This is the fourth of the coprocessor's five facilities and the last one this driver learned to
 * speak. It matters because of an asymmetry in the datapath: on transmit the host names the
 * destination port itself, in the two-byte tag at the front of every frame, and that works - 189
 * frames left front port 8 and nowhere else, steered by nothing but the tag. On receive the
 * coprocessor's fastpath has to decide which host interface a frame belongs to, and nothing has
 * ever told it. The table it consults is filled over this channel.
 *
 * Unlike every other facility here, none of the layout below was inferred. Sophos ships
 * usfp_rh.ko - their own target-side handler, not Marvell's sample - built with -g3, so the
 * compiler recorded every structure, field name and offset, and every #define. See npuep.h for
 * the offsets and docs/npuep/rpc.md for how they were read out.
 *
 * What the channel is: a DMA ring, not a mailbox. The host writes a command into its OWN memory,
 * puts a sixteen-byte descriptor carrying that physical address into a ring inside the BAR,
 * advances a producer index and rings a doorbell. The target fetches the command by DMA, runs it,
 * writes the answer back by DMA and advances a consumer index. There is no doorbell in the other
 * direction, so the host polls.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/systm.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include "npuep.h"
#include "npuflow.h"
#include "sfosnum.h"

/*
 * Where this driver puts things inside the window. The megabyte is ours to lay out - the target
 * reads the descriptor area at whatever offset the ring says - so these are chosen, not measured.
 * Well clear of the 232-byte state block, and page-aligned because it costs nothing.
 */
#define	NPURPC_DESC_OFF		0x1000
#define	NPURPC_DESC_COUNT	32
#define	NPURPC_HI_DESC_OFF	0x2000
#define	NPURPC_HI_DESC_COUNT	32

/* How much of a hand-written command, and of its answer, the workbench sysctl will carry. */
/*
 * The hand-command buffer. 64 was enough for the three commands this driver sends and far too
 * small for the ones worth asking: the fastpath's counter tables answer with an array of u64, and
 * the counters that say why a frame was dropped are spread from 23 to 42 - so a 64-byte cap
 * returned the first eight and silently truncated the rest, which is a good way to read a zero
 * and believe it.
 */
#define	NPURPC_RAW_MAX		512

/*
 * The formatted answer: the header text plus three characters per response byte.
 *
 * It is malloc'd, not automatic, and that is the whole point of naming it here. When
 * NPURPC_RAW_MAX was widened from 64 to 512 so the fastpath's counter tables could be read whole,
 * this string grew with it to about 1.8KB - and with in[400] and pl[512] beside it the frame came
 * to roughly 2.7KB in one function. The machine panicked on the first command sent afterwards.
 *
 * npunwa_sysctl_probe carries a comment warning about exactly this, written in an earlier session
 * and not read before the mistake was repeated here.
 */
#define	NPURPC_CMD_OUT		(NPURPC_RAW_MAX * 3 + 256)

/* How long to wait for the target to accept the configuration, in hundredths of a second. */
#define	NPURPC_OPEN_WAIT	500

/* And for one command to be answered. */
#define	NPURPC_CMD_WAIT		300

/*
 * But spun for the first two milliseconds, in twenty-microsecond steps, the way the OCTEON TX
 * driver waits. The target answers in tens of microseconds - 20.7 on average over 358,598 commands
 * on the XGS 3300 - and the hundredth-of-a-second pause this loop began with made every command
 * cost ten milliseconds whatever it was: the flow maker, two commands a connection, could make a
 * few dozen a second at most. A command still unanswered after the spin is waited for with the
 * pause, as before, so a slow one costs what it always did.
 */
#define	NPURPC_SPIN_US		2000
#define	NPURPC_SPIN_STEP	20

/*
 * The command numbers move with the coprocessor's software (issue 736): every number in this driver
 * is SFOS 22's, SFOS 23 moved every command from 36 on one higher, and SFOS 21 numbers every
 * command from 46 on one lower (sfosnum.h; this said "SFOS 21's and 22's" until 2026-10-25, true
 * only below 46). The coprocessor
 * keeps the software the appliance's last SFOS left on it, so which numbering it speaks is the
 * owner's history, not the board. The driver finds it: PLATFORM_READ is looked for at 36, then 37
 * and on, with no payload - it answers a block of at least NPURPC_PLAT_LEN bytes whose name starts
 * "XGS_", which nothing else does - and every command from 36 on is sent moved by what it found. A
 * numbering it cannot place sends none of them. The versions, the numbers and the block's layout
 * are sfosnum.h's, shared with the OCTEON TX driver.
 */

/*
 * An instrument: the platform block looked for from 36 + this on. On SFOS 21's and 22's numbering
 * any value from 1 makes the block unfindable, which is how the path for a numbering this driver
 * cannot place was run on the XGS 136. 0, the default, looks from 36.
 */
static int npurpc_platform_test = 0;
TUNABLE_INT("hw.npuep.rpc_platform_test", &npurpc_platform_test);
#define	NPURPC_PLAT_OFF_ID	192
#define	NPURPC_PLAT_OFF_CORES	193
#define	NPURPC_PLAT_OFF_IFACES	194
#define	NPURPC_PLAT_OFF_RINGS	195
#define	NPURPC_PLAT_OFF_MFLOW_TO 196
#define	NPURPC_PLAT_OFF_CONN	204
#define	NPURPC_PLAT_OFF_NHOP	208
#define	NPURPC_PLAT_OFF_FWRULES	212
#define	NPURPC_PLAT_OFF_SAS	216
#define	NPURPC_PLAT_OFF_CAPS	220
/*
 * The shortest block taken: SFOS 21's, which ends at num_pfs and three reserved bytes - what the XGS
 * 136 answers. SFOS 22 and 23 answer 232, num_mflows at 224 and num_pfs after it (sfosnum.h).
 */
#define	NPURPC_PLAT_LEN		SFOSNUM_PLAT_LEN_21

/*
 * What to tell the coprocessor each interface's MTU is. Larger than the host interfaces' 1500 on
 * purpose: this number only ever gates a drop - the fastpath discards anything longer - so the
 * cost of it being too small is silent loss, and the cost of it being large is nothing.
 */
#define	NPURPC_LIF_MTU		9000

/* How often to look again, and for how long, while the coprocessor is still starting up. */
#define	NPURPC_PROG_RETRY	2	/* seconds */
#define	NPURPC_PROG_TRIES	45	/* so ninety seconds in all */
/*
 * How long to wait, a second at a time, for the coprocessor's software to claim the channel's
 * window when it has not by attach (issue 736). The heartbeat's first answer comes about six
 * seconds after attach on these boards; the same two minutes as the facility table's wait.
 */
#define	NPURPC_CLAIM_WAIT	120

/*
 * An instrument for that wait: the window taken as unclaimed for this many seconds after attach,
 * whatever it reads. The race of issue 736 does not happen on the XGS 136 - its channel attaches
 * after the claim, with or without its inspection function - so this is how the wait was run on
 * real hardware. 0, the default, reads the window and nothing else.
 */
static int npurpc_claim_test = 0;
TUNABLE_INT("hw.npuep.rpc_claim_test", &npurpc_claim_test);

struct npurpc_softc {
	struct npuep_facility	 fac;
	/*
	 * A sleepable lock, not a mutex, and it has to be: a command holds it from the moment it is
	 * posted until the target answers, and the wait between is a pause() - the host polls, there
	 * is no inbound doorbell. A thread that sleeps holding a default mutex panics the kernel the
	 * instant another thread blocks on that mutex ("sleeping thread holds npurpc", seen once the
	 * flow sweep began posting commands beside the port-programming task and a sysctl). An sx is
	 * held across the sleep legally, and a second caller waits on it without the panic. Every
	 * caller here is a taskqueue or a sysctl, all free to sleep; none holds another lock across
	 * it - the flow code drops its own lock before every npurpc_send.
	 */
	struct sx		 lock;

	/*
	 * One command buffer in host memory. The descriptors in the window point at it, so it has
	 * to satisfy the same 36-bit addressing ceiling as every other DMA on this device - see
	 * docs/npuep/mvmgmt.md for where that number came from.
	 */
	bus_dma_tag_t		 cmd_tag;
	bus_dmamap_t		 cmd_map;
	void			*cmd_vaddr;
	bus_addr_t		 cmd_paddr;

	/*
	 * And one slot per high-ring descriptor, for the commands posted there without waiting
	 * (npurpc_post): a slot is written again only once the target's done index has passed it.
	 */
	bus_dma_tag_t		 hi_tag;
	bus_dmamap_t		 hi_map;
	void			*hi_vaddr;
	bus_addr_t		 hi_paddr;
	uint64_t		 hi_posted;	/* our count; reset with the layout */
	uint64_t		 hi_commands, hi_full, hi_fallback;
	int			 hi_off;	/* 1 stops posting: every command waits again */
	char			 hi_probe_result[200];

	int			 opened;	/* the magic has been written and taken */
	int			 stalled;	/* a command went unanswered; post no more */
	int			 claiming;	/* waiting for the coprocessor to claim the window */
	int			 claim_tries;	/* seconds waited for that so far */
	int			 claim_waited;	/* it was not claimed at attach: say when it is */
	int			 claimed;	/* seen claimed once: not looked at again */
	int			 cmd_shift;	/* commands from 36 on are sent this much higher; -1 unknown */
	int			 cmd_raw;	/* this command's number goes as written: the platform search */
	int			 cmd_as_written;	/* hand-sent commands go as written (sysctl) */
	int			 sfos;		/* 21, 22, 23: whose software the coprocessor runs; 0 unknown */
	time_t			 attached_at;	/* time_uptime at attach: the open's deadline counts from it */
	int			 open_tries;	/* attempts to open the channel (issue 736) */
	int			 dbell_dropped;	/* configuration doorbells skipped, hw.npuep.rpc_dbell_drop */

	/*
	 * Programming the front ports runs on its own thread, not a callout: every pass sends
	 * commands and every command waits for an answer, and a callout may not sleep. This
	 * driver has already taken that panic once, in the network agent.
	 */
	struct taskqueue	*tq;
	struct timeout_task	 task;
	int			 stop;
	int			 tries;
	int			 verified;	/* how many ports read back correct */
	int			 wiped;		/* said once, when the table is cleared under us */
	int			 announced;	/* success is worth saying on the edge, not every pass */
	uint64_t		 mac_updates;	/* entries given a port's new address (issue 730) */

	/* The last hand-written exchange, kept so a read can report it. */
	uint8_t			 raw_cmd;
	int			 raw_wire;	/* the number it went as; -1 not sent */
	int			 raw_plen;
	int			 raw_err;
	int			 raw_rc;
	int			 raw_rlen;
	uint8_t			 raw_resp[NPURPC_RAW_MAX];
	uint64_t		 posted;	/* our own count, never read back */
	uint64_t		 commands, answers, timeouts;
	/* How long the answers took, in microseconds: the spin's count, or past it the pauses'. */
	uint64_t		 wait_sum, wait_n;
	uint32_t		 wait_max, wait_last;

	/*
	 * The board's own numbers, read once with PLATFORM_READ: the coprocessor states how many
	 * connections, next hops and associations it holds, and the flow table is sized by them.
	 */
	int			 plat_learned;
	uint8_t			 plat_id, plat_cores, plat_ifaces, plat_rings;
	uint32_t		 plat_mflow_to, plat_conn, plat_nhop, plat_fwrules, plat_sas;
	uint32_t		 plat_caps, plat_mflows;	/* plat_mflows: SFOS 22 on, else 0 */
	int			 plat_len;	/* bytes the platform block came back as */
	uint8_t			 plat_pfs;
	char			 plat_name[64], plat_ver[64], plat_asm[64];
};

static struct npurpc_softc *npurpc_sc;

/*
 * Whether the front ports' interface entries are wanted open: set by the flow maker (flow.auto)
 * through npurpc_entries, read by the programming below. Shut - OFFLOAD_DISABLED, every frame to the
 * host - unless the owner turned automatic acceleration on, because past a shut entry the fast path
 * names no connection and the flow maker has nothing to make (issue 483, parity). Module-wide, not
 * in the softc: the setting can arrive before the channel has attached.
 */
static int npurpc_want_open;

/*
 * Whether the front ports' interface table is confirmed, and if not why, in one line: read as
 * dev.npuep.0.iftable, and printed by verify.sh (issue 736). An owner whose ports carry nothing
 * pastes what verify.sh printed, and this line is what makes that paste enough on a board this
 * project has never seen - no support bundle, no second round of questions. So every way the
 * table can fail to be confirmed writes its own reason here, with the numbers the coprocessor
 * gave. Module-wide, not in the softc: the first reasons are met before there is one. No
 * addresses in it - it is meant to be pasted in public.
 */
static struct mtx npurpc_why_mtx;
MTX_SYSINIT(npurpc_why, &npurpc_why_mtx, "npurpc why", MTX_DEF);
static char npurpc_why_text[NPURPC_WHY_LEN] =
    "not reached yet: the control-message channel has not attached";

void
npurpc_why_set(const char *why)
{
	mtx_lock(&npurpc_why_mtx);
	strlcpy(npurpc_why_text, why, sizeof(npurpc_why_text));
	mtx_unlock(&npurpc_why_mtx);
}

void
npurpc_why_get(char *out, size_t len)
{
	mtx_lock(&npurpc_why_mtx);
	strlcpy(out, npurpc_why_text, len);
	mtx_unlock(&npurpc_why_mtx);
}

/*
 * The SFOS the platform block showed, for the parts of the driver that build requests whose layout
 * is the version's (npuipsec's SA_ADD). Read without the lock: an int the channel's opening sets
 * once, before any association can be offered; 0 before then and when it was not found.
 */
int
npurpc_sfos(void)
{
	struct npurpc_softc *sc = npurpc_sc;

	return (sc != NULL ? sc->sfos : 0);
}

#define	RPC_LOCK(sc)	sx_xlock(&(sc)->lock)
#define	RPC_UNLOCK(sc)	sx_xunlock(&(sc)->lock)

static uint32_t
rpc_rd(struct npurpc_softc *sc, bus_size_t o)
{
	if (npuep_gone)		/* see npuep_gone in npuep.h (#768) */
		return (0xFFFFFFFFU);
	return (bus_read_4(sc->fac.res, sc->fac.off + o));
}

static void
rpc_wr(struct npurpc_softc *sc, bus_size_t o, uint32_t v)
{
	bus_write_4(sc->fac.res, sc->fac.off + o, v);
}

/*
 * The first eight bytes of the state block are one field to the target: it compares the whole
 * quadword against its cached copy to decide whether the configuration changed. Writing it in two
 * halves would let it read a magic belonging to one configuration beside a ring count belonging to
 * another, so it is written whole.
 */
static void
rpc_wr8(struct npurpc_softc *sc, bus_size_t o, uint64_t v)
{
	bus_write_8(sc->fac.res, sc->fac.off + o, v);
}

static void
rpc_barrier(struct npurpc_softc *sc)
{
	bus_barrier(sc->fac.res, sc->fac.off, sc->fac.size,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static void
npurpc_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	if (error == 0 && nseg == 1)
		*(bus_addr_t *)arg = segs[0].ds_addr;
}

/*
 * Lay out the rings in the window.
 *
 * Two of them, and the second is not an optimisation - it is the difference between a channel that
 * works and one that answers a single command and then takes the coprocessor down with it.
 *
 * The rpc facility on this board has exactly one host-to-target doorbell, and the target's
 * interrupt handler treats that as meaning the high-priority ring shares it: on every doorbell
 * that is not a configuration change it schedules high ring zero's tasklet, whatever the host has
 * declared. If the host declared no high rings, that ring's context was never filled in, so the
 * tasklet runs against facility zero, doorbell zero. Facility zero is ctrl, which has no
 * host-to-target doorbells at all, so enabling it fails - and the failure path reschedules the
 * tasklet unconditionally. It re-arms itself forever, printing two unratelimited errors per pass
 * to a 115200 console, on the one core left to housekeeping by isolcpus - the same core that runs
 * the low ring's work item. The low ring is never serviced again.
 *
 * That is not a hypothesis. It is what happened here four times in a row, and the vendor's own
 * host avoids it by declaring one high ring, which its boot log records as "Allocated DMA dev #0
 * to hi ring #1 on doorbell 3.0".
 *
 * So high ring zero is declared, sharing this facility's doorbell, with its indices equal and its
 * descriptor area empty. The target's ring walker returns immediately when posted equals done, so
 * the ring carries nothing; it exists so that the tasklet has a doorbell it is allowed to enable.
 *
 * Everything the target can read is cleared first, including the four high-ring slots this driver
 * would otherwise never touch. They are bound to the target's ring contexts when its own module
 * loads, not when a ring is declared, so whatever an earlier operating system left in them is live.
 */
static void
npurpc_layout_one(struct npurpc_softc *sc, bus_size_t r, bus_size_t desc, uint32_t count,
    uint32_t cfg)
{
	uint32_t i;

	for (i = 0; i < count * 16; i += 4)
		rpc_wr(sc, desc + i, 0);

	rpc_wr(sc, r + RPC_RING_POSTED, 0);
	rpc_wr(sc, r + RPC_RING_POSTED + 4, 0);
	rpc_wr(sc, r + RPC_RING_DONE, 0);
	rpc_wr(sc, r + RPC_RING_DONE + 4, 0);

	rpc_wr(sc, r + RPC_RING_OFFSET, (uint32_t)desc);
	rpc_wr(sc, r + RPC_RING_DESC_OFFSET, (uint32_t)desc);
	rpc_wr(sc, r + RPC_RING_DESC_COUNT, count);
	rpc_wr(sc, r + RPC_RING_CFG, cfg);
}

static void
npurpc_layout_ring(struct npurpc_softc *sc)
{
	bus_size_t o;

	sx_assert(&sc->lock, SA_XLOCKED);

	/* The whole state block, not just the parts this driver fills in. */
	for (o = 0; o < RPC_STATE_SIZE; o += 4)
		rpc_wr(sc, o, 0);

	/*
	 * ring_num 0, facility index 3 - the control-message channel's own - doorbell 0, not
	 * shared. Doorbell 0 is this facility's: the host-to-target doorbells are handed out in
	 * facility order and every facility before this one declares none, which is also why it is
	 * the only one the target has armed.
	 */
	npurpc_layout_one(sc, RPC_ST_RING_LO, NPURPC_DESC_OFF, NPURPC_DESC_COUNT,
	    (0) | (3 << 8) | (0 << 16) | (0 << 24));

	/* ring_num 1, same facility, same doorbell, and shared - which is what says so. */
	npurpc_layout_one(sc, RPC_ST_RINGS, NPURPC_HI_DESC_OFF, NPURPC_HI_DESC_COUNT,
	    (1) | (3 << 8) | (0 << 16) | (1 << 24));
	sc->hi_posted = 0;

	rpc_barrier(sc);
}

/*
 * Ring the channel's doorbell for a configuration just written - or, under hw.npuep.rpc_dbell_drop,
 * an instrument, not: that many of these doorbells are skipped from attach on, which is how a
 * doorbell the target never heard was made to happen on an appliance where it does not (issue 736).
 */
static int npurpc_dbell_drop = 0;
TUNABLE_INT("hw.npuep.rpc_dbell_drop", &npurpc_dbell_drop);

static void
npurpc_cfg_dbell(struct npurpc_softc *sc)
{
	if (sc->dbell_dropped < npurpc_dbell_drop) {
		sc->dbell_dropped++;
		return;
	}
	(void)npuep_ring_dbell(sc->fac.parent, sc->fac.dbell);
}

/*
 * Wait for the target to say it has taken a configuration.
 *
 * It clears reconfig_done when it starts and sets it when it has finished, so this is only
 * meaningful after writing a configuration word with that byte zero - which both writes below do.
 * The previous version of this waited for the byte to be non-zero without ever having cleared it,
 * which the state the channel starts in satisfies immediately. It measured nothing.
 *
 * The doorbell is rung again every half second while it waits (issue 736). The target's handler
 * (usfp_rh/rpc_handler.c) sets reconfig_done - the claim npurpc_attach waits for - at the start of
 * its own setup, before the interrupt that reads a new configuration is registered, and that
 * interrupt is the only place it looks. A doorbell rung in between is lost, and nothing rings it
 * again: on the second XGS 126 run by its owner the window was claimed three seconds after attach,
 * the configuration written and rung at once, and never acknowledged. A doorbell with the
 * configuration unchanged costs the target one look at the word.
 */
#define	NPURPC_OPEN_RERING	50	/* polls, so half a second */

static int
npurpc_wait_reconfig(struct npurpc_softc *sc)
{
	int i;

	for (i = 0; i < NPURPC_OPEN_WAIT; i++) {
		if ((rpc_rd(sc, RPC_ST_CFG_REVISION) >> 24) != 0)
			return (0);
		pause("npurpco", hz / 100);
		if (i % NPURPC_OPEN_RERING == NPURPC_OPEN_RERING - 1)
			npurpc_cfg_dbell(sc);
	}
	return (ETIMEDOUT);
}

/*
 * Open the channel: publish the rings, then hand the target a configuration in two steps.
 *
 * Two steps because that is what the vendor's host does - its boot log shows "updating ring
 * configuration: 0" and then "updating ring configuration: 1015fd7d3ab00", which is this same
 * quadword carrying the magic, revision 0x015f, one active high ring and reconfig_done clear. The
 * zero first tears down whatever the target was holding, so the second is taken from a known state
 * rather than merged into one this driver did not create.
 *
 * The rings are laid out and both descriptor areas cleared BEFORE any of it, because the
 * configuration is what makes the target start reading descriptors out of this window. Written
 * against an unconfigured ring it would send the coprocessor to whatever address happened to be
 * lying there - host physical zero, as the window stands.
 */
static int
npurpc_open(struct npurpc_softc *sc, int loud)
{
	char why[NPURPC_WHY_LEN];
	uint64_t cfg;
	uint32_t st;
	int err;

	sx_assert(&sc->lock, SA_XLOCKED);

	npurpc_layout_ring(sc);

	rpc_wr8(sc, RPC_ST_CFG_MAGIC, 0);
	rpc_barrier(sc);
	npurpc_cfg_dbell(sc);
	if ((err = npurpc_wait_reconfig(sc)) != 0) {
		if (loud)
			device_printf(sc->fac.dev, "rpc: the target did not acknowledge the empty "
			    "configuration - state %#010x\n", rpc_rd(sc, RPC_ST_CFG_REVISION));
		snprintf(why, sizeof(why), "the channel did not open: the coprocessor did not "
		    "acknowledge the empty configuration (state %#010x)",
		    rpc_rd(sc, RPC_ST_CFG_REVISION));
		npurpc_why_set(why);
		return (err);
	}

	cfg = (uint64_t)RPC_STATE_CFG_MAGIC |		/* cfg_magic       */
	    ((uint64_t)RPC_CFG_REVISION << 32) |	/* cfg_revision    */
	    ((uint64_t)1 << 48);			/* active_hi_rings */
	rpc_wr8(sc, RPC_ST_CFG_MAGIC, cfg);
	rpc_barrier(sc);
	npurpc_cfg_dbell(sc);
	if ((err = npurpc_wait_reconfig(sc)) != 0) {
		if (loud)
			device_printf(sc->fac.dev,
			    "rpc: the target did not take the configuration - magic reads %#010x, "
			    "state %#010x\n",
			    rpc_rd(sc, RPC_ST_CFG_MAGIC), rpc_rd(sc, RPC_ST_CFG_REVISION));
		snprintf(why, sizeof(why), "the channel did not open: the coprocessor did not take "
		    "the configuration (magic %#010x, state %#010x)",
		    rpc_rd(sc, RPC_ST_CFG_MAGIC), rpc_rd(sc, RPC_ST_CFG_REVISION));
		npurpc_why_set(why);
		return (err);
	}

	if (rpc_rd(sc, RPC_ST_CFG_MAGIC) != RPC_STATE_CFG_MAGIC) {
		if (loud)
			device_printf(sc->fac.dev, "rpc: the target acknowledged but the magic did not "
			    "stick - reads %#010x\n", rpc_rd(sc, RPC_ST_CFG_MAGIC));
		snprintf(why, sizeof(why), "the channel did not open: the coprocessor acknowledged "
		    "the configuration but its magic did not stick (reads %#010x)",
		    rpc_rd(sc, RPC_ST_CFG_MAGIC));
		npurpc_why_set(why);
		return (EIO);
	}

	sc->opened = 1;
	sc->stalled = 0;
	npurpc_why_set("the channel is open; the table is being written and read back");
	st = rpc_rd(sc, RPC_ST_CFG_REVISION);
	device_printf(sc->fac.dev,
	    "rpc: channel open - revision %u, %u high ring sharing doorbell %d, "
	    "descriptors at +%#x x%d\n",
	    st & 0xFFFF, (st >> 16) & 0xFF, sc->fac.dbell, NPURPC_DESC_OFF, NPURPC_DESC_COUNT);
	return (0);
}

static int
npurpc_alloc_cmd(struct npurpc_softc *sc)
{
	bus_addr_t pa = 0;
	int err;

	err = bus_dma_tag_create(sc->fac.parent_tag, 64, 0, NPUEP_DMA_LOWADDR,
	    BUS_SPACE_MAXADDR, NULL, NULL, RPC_DATA_MAX_SIZE, 1, RPC_DATA_MAX_SIZE, 0,
	    NULL, NULL, &sc->cmd_tag);
	if (err != 0)
		return (err);

	err = bus_dmamem_alloc(sc->cmd_tag, &sc->cmd_vaddr,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT, &sc->cmd_map);
	if (err != 0)
		return (err);

	err = bus_dmamap_load(sc->cmd_tag, sc->cmd_map, sc->cmd_vaddr, RPC_DATA_MAX_SIZE,
	    npurpc_dmamap_cb, &pa, BUS_DMA_NOWAIT);
	if (err != 0 || pa == 0)
		return (err != 0 ? err : ENOMEM);

	sc->cmd_paddr = pa;

	pa = 0;
	err = bus_dma_tag_create(sc->fac.parent_tag, 64, 0, NPUEP_DMA_LOWADDR,
	    BUS_SPACE_MAXADDR, NULL, NULL, NPURPC_HI_DESC_COUNT * RPC_DATA_MAX_SIZE, 1,
	    NPURPC_HI_DESC_COUNT * RPC_DATA_MAX_SIZE, 0, NULL, NULL, &sc->hi_tag);
	if (err != 0)
		return (err);
	err = bus_dmamem_alloc(sc->hi_tag, &sc->hi_vaddr,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT, &sc->hi_map);
	if (err != 0)
		return (err);
	err = bus_dmamap_load(sc->hi_tag, sc->hi_map, sc->hi_vaddr,
	    NPURPC_HI_DESC_COUNT * RPC_DATA_MAX_SIZE, npurpc_dmamap_cb, &pa, BUS_DMA_NOWAIT);
	if (err != 0 || pa == 0)
		return (err != 0 ? err : ENOMEM);
	sc->hi_paddr = pa;
	return (0);
}

static void
npurpc_free_cmd(struct npurpc_softc *sc)
{
	if (sc->cmd_vaddr != NULL) {
		bus_dmamap_unload(sc->cmd_tag, sc->cmd_map);
		bus_dmamem_free(sc->cmd_tag, sc->cmd_vaddr, sc->cmd_map);
		sc->cmd_vaddr = NULL;
	}
	if (sc->cmd_tag != NULL) {
		bus_dma_tag_destroy(sc->cmd_tag);
		sc->cmd_tag = NULL;
	}
	if (sc->hi_vaddr != NULL) {
		bus_dmamap_unload(sc->hi_tag, sc->hi_map);
		bus_dmamem_free(sc->hi_tag, sc->hi_vaddr, sc->hi_map);
		sc->hi_vaddr = NULL;
	}
	if (sc->hi_tag != NULL) {
		bus_dma_tag_destroy(sc->hi_tag);
		sc->hi_tag = NULL;
	}
}

/*
 * Post one command and wait for the answer.
 *
 * The command goes in host memory; only a descriptor naming it goes in the window. The order is
 * the protocol: fill the buffer, fill the descriptor, barrier, advance the producer index,
 * barrier, ring. Anything the target is meant to see must be visible before the index that says
 * it is there, and the index before the doorbell that says to look.
 *
 * The indices are running counts, not flags. The target takes the descriptor at
 * done & (desc_count - 1) and then increments done, so a command's slot is fixed by its sequence
 * number and desc_count has to be a power of two. Thirty-two is.
 *
 * Two fields here were wrong for as long as this channel has existed, and both came from reading
 * the names rather than the code. payload_len is the payload alone: the target adds the eight-byte
 * command header itself when it sizes the fetch, so including it here made every fetch eight bytes
 * too long. And the post flag does not mean "this descriptor is posted" - it means "post only, do
 * not answer". With it set the target runs the command and advances done without ever calling the
 * routine that writes a response, which is exactly what we saw: a command the target completed,
 * whose answer buffer still held what this driver had put in it.
 */
/*
 * The number a command goes on the wire as: this driver's own - SFOS 22's - placed by what the
 * coprocessor's numbering was found to be (sfosnum_wire). -1 when a command cannot be placed - from
 * 36 on before the platform block is found, or when it was not, and from 46 on before the version
 * is known - and must not be sent.
 */
static int
npurpc_wire_cmd(struct npurpc_softc *sc, uint8_t cmd)
{
	if (sc->cmd_raw)
		return (cmd);
	return (sfosnum_wire(cmd, sc->cmd_shift, sc->sfos));
}

static int
npurpc_command(struct npurpc_softc *sc, uint8_t cmd, const void *payload, int plen,
    void *resp, int rlen, int *rcout)
{
	uint8_t *b = sc->cmd_vaddr;
	bus_size_t d;
	uint64_t want;
	uint32_t lo, hi, us;
	uint16_t rc, rpl;
	int i, wire;

	sx_assert(&sc->lock, SA_XLOCKED);

	if (!sc->opened)
		return (-ENXIO);
	if ((wire = npurpc_wire_cmd(sc, cmd)) < 0)
		return (-EOPNOTSUPP);
	/*
	 * One unanswered command is the whole of the evidence that the target has stopped
	 * servicing this ring, and posting another on top of it is how a stall became an hour of
	 * dead coprocessor. Refuse until the channel is opened again.
	 */
	if (sc->stalled)
		return (-ESHUTDOWN);
	if (plen < 0 || plen + RPC_CMD_PAYLOAD > RPC_DATA_MAX_SIZE)
		return (-EINVAL);

	memset(b, 0, RPC_DATA_MAX_SIZE);
	le16enc(b + RPC_CMD_RESP_BUFF_SZ, (uint16_t)(RPC_DATA_MAX_SIZE / 2));
	b[RPC_CMD_CMD] = (uint8_t)wire;
	if (plen > 0)
		memcpy(b + RPC_CMD_PAYLOAD, payload, plen);

	bus_dmamap_sync(sc->cmd_tag, sc->cmd_map, BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	/* The slot this command will be taken from, and the sequence number that says so. */
	want = sc->posted + 1;
	d = NPURPC_DESC_OFF + (bus_size_t)((want - 1) & (NPURPC_DESC_COUNT - 1)) * 16;

	/* The descriptor: where the command is and how long its payload is. */
	rpc_wr(sc, d + RPC_BD_DMA_ADDR, (uint32_t)(sc->cmd_paddr & 0xFFFFFFFFU));
	rpc_wr(sc, d + RPC_BD_DMA_ADDR + 4, (uint32_t)(sc->cmd_paddr >> 32));
	rpc_wr(sc, d + RPC_BD_PAYLOAD_LEN,
	    (uint32_t)plen | ((uint32_t)RPC_DESC_NO_AGG_DMA << 16));
	rpc_wr(sc, d + 12, 0);
	rpc_barrier(sc);

	rpc_wr(sc, RPC_ST_RING_LO + RPC_RING_POSTED, (uint32_t)want);
	rpc_wr(sc, RPC_ST_RING_LO + RPC_RING_POSTED + 4, (uint32_t)(want >> 32));
	rpc_barrier(sc);

	sc->commands++;
	(void)npuep_ring_dbell(sc->fac.parent, sc->fac.dbell);

	lo = hi = 0;
	for (us = 0; us < NPURPC_SPIN_US; us += NPURPC_SPIN_STEP) {
		lo = rpc_rd(sc, RPC_ST_RING_LO + RPC_RING_DONE);
		hi = rpc_rd(sc, RPC_ST_RING_LO + RPC_RING_DONE + 4);
		if (((uint64_t)lo | ((uint64_t)hi << 32)) >= want)
			break;
		DELAY(NPURPC_SPIN_STEP);
	}
	i = 0;
	if (us >= NPURPC_SPIN_US) {
		for (; i < NPURPC_CMD_WAIT; i++) {
			lo = rpc_rd(sc, RPC_ST_RING_LO + RPC_RING_DONE);
			hi = rpc_rd(sc, RPC_ST_RING_LO + RPC_RING_DONE + 4);
			if (((uint64_t)lo | ((uint64_t)hi << 32)) >= want)
				break;
			pause("npurpcc", hz / 100);
		}
		us += i * 10000;
	}
	if (i >= NPURPC_CMD_WAIT) {
		char why[NPURPC_WHY_LEN];

		sc->timeouts++;
		sc->stalled = 1;
		device_printf(sc->fac.dev,
		    "rpc: command %u went unanswered - posted %ju, done %ju. No more will be sent "
		    "until the channel is reopened.\n",
		    cmd, (uintmax_t)want, (uintmax_t)((uint64_t)lo | ((uint64_t)hi << 32)));
		snprintf(why, sizeof(why), "the channel stalled: command %u went unanswered "
		    "(posted %ju, done %ju), and nothing more is sent on it", cmd, (uintmax_t)want,
		    (uintmax_t)((uint64_t)lo | ((uint64_t)hi << 32)));
		npurpc_why_set(why);
		return (-ETIMEDOUT);
	}

	sc->posted = want;
	sc->answers++;
	sc->wait_last = us;
	if (us > sc->wait_max)
		sc->wait_max = us;
	sc->wait_sum += us;
	sc->wait_n++;

	bus_dmamap_sync(sc->cmd_tag, sc->cmd_map, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	/*
	 * done having advanced says the target finished with the descriptor; this byte says it
	 * wrote an answer. They are not the same thing, and keeping them apart is what turned a
	 * silent wrong answer into a visible one last time.
	 */
	if (b[RPC_RESP_DESC_DONE] == 0) {
		device_printf(sc->fac.dev,
		    "rpc: command %u completed without an answer - descriptor_done clear\n", cmd);
		return (-EIO);
	}

	rc = le16dec(b + RPC_RESP_RC);
	rpl = le16dec(b + RPC_RESP_PAYLOAD_LEN);
	if (rcout != NULL)
		*rcout = rc;

	if (resp != NULL && rlen > 0) {
		if (rpl > rlen)
			rpl = rlen;
		if (RPC_RESP_PAYLOAD + rpl <= RPC_DATA_MAX_SIZE)
			memcpy(resp, b + RPC_RESP_PAYLOAD, rpl);
	}
	return (rpl);		/* how much came back, so a caller cannot print what did not */
}

/*
 * The flow side's way in (npuflow.c): post one command and take the lock for it. npurpc_command
 * runs with RPC_LOCK held; everything else here already holds it, so this is the one caller that
 * does not, and it takes it. Returns npurpc_command's reply length (negative on a transport
 * failure), with the target's rc in *rc. There is no coprocessor until the channel is open; a
 * caller before then gets -ENXIO and says so.
 */
int
npurpc_send(uint8_t cmd, const void *payload, int plen, void *resp, int rlen, int *rc)
{
	struct npurpc_softc *sc = npurpc_sc;
	int n;

	if (sc == NULL)
		return (-ENXIO);
	RPC_LOCK(sc);
	n = npurpc_command(sc, cmd, payload, plen, resp, rlen, rc);
	RPC_UNLOCK(sc);
	return (n);
}

/*
 * POSTED on the high ring, not waited for: the command and its payload copied into the slot its
 * descriptor names, the descriptor written, the posted index raised and the doorbell rung - the one
 * this facility has, which the target's handler takes as the high ring's too (see the layout). The
 * XGS 3300's flow writes go this way (octep_rpc_post_hi), and each one waited for was 138
 * microseconds of the flow table's lock here. Nothing is read back: a slot is reused only after the
 * target's done index has passed it, and a full ring says so to the caller, which waits instead.
 * Caller holds the lock. Returns 0 posted, -EAGAIN full, -ENXIO when there is no high ring to
 * post on.
 */
static int
npurpc_post(struct npurpc_softc *sc, uint8_t cmd, const void *payload, int plen)
{
	uint64_t done, want;
	uint32_t idx;
	uint8_t *b;
	bus_size_t d;
	bus_addr_t pa;
	int wire;

	sx_assert(&sc->lock, SA_XLOCKED);
	if (!sc->opened || sc->stalled || sc->hi_off || sc->hi_vaddr == NULL)
		return (-ENXIO);
	if ((wire = npurpc_wire_cmd(sc, cmd)) < 0)
		return (-EOPNOTSUPP);
	if (plen < 0 || plen + RPC_CMD_PAYLOAD > RPC_DATA_MAX_SIZE)
		return (-EINVAL);
	done = (uint64_t)rpc_rd(sc, RPC_ST_RINGS + RPC_RING_DONE) |
	    ((uint64_t)rpc_rd(sc, RPC_ST_RINGS + RPC_RING_DONE + 4) << 32);
	if (sc->hi_posted - done >= NPURPC_HI_DESC_COUNT - 1) {
		sc->hi_full++;
		return (-EAGAIN);
	}
	idx = (uint32_t)(sc->hi_posted & (NPURPC_HI_DESC_COUNT - 1));
	b = (uint8_t *)sc->hi_vaddr + (size_t)idx * RPC_DATA_MAX_SIZE;
	pa = sc->hi_paddr + (bus_addr_t)idx * RPC_DATA_MAX_SIZE;
	/*
	 * Cleared to 64 bytes past the payload, and never past the slot: the check above lets a payload
	 * fill the slot to its last byte, and 64 more would be the next slot's - one the target may not
	 * have taken yet - or, after the last slot, past the region. Every caller posts a fixed request
	 * far smaller than a slot; until 2026-10-19 nothing stopped one that was not.
	 */
	memset(b, 0, MIN(RPC_CMD_PAYLOAD + plen + 64, RPC_DATA_MAX_SIZE));
	le16enc(b + RPC_CMD_RESP_BUFF_SZ, (uint16_t)(RPC_DATA_MAX_SIZE / 2));
	b[RPC_CMD_CMD] = (uint8_t)wire;
	if (plen > 0)
		memcpy(b + RPC_CMD_PAYLOAD, payload, plen);
	bus_dmamap_sync(sc->hi_tag, sc->hi_map, BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	want = sc->hi_posted + 1;
	d = NPURPC_HI_DESC_OFF + (bus_size_t)idx * 16;
	rpc_wr(sc, d + RPC_BD_DMA_ADDR, (uint32_t)(pa & 0xFFFFFFFFU));
	rpc_wr(sc, d + RPC_BD_DMA_ADDR + 4, (uint32_t)(pa >> 32));
	rpc_wr(sc, d + RPC_BD_PAYLOAD_LEN, (uint32_t)plen | ((uint32_t)RPC_DESC_NO_AGG_DMA << 16));
	rpc_wr(sc, d + 12, 0);
	rpc_barrier(sc);
	rpc_wr(sc, RPC_ST_RINGS + RPC_RING_POSTED, (uint32_t)want);
	rpc_wr(sc, RPC_ST_RINGS + RPC_RING_POSTED + 4, (uint32_t)(want >> 32));
	rpc_barrier(sc);
	(void)npuep_ring_dbell(sc->fac.parent, sc->fac.dbell);
	sc->hi_posted = want;
	sc->hi_commands++;
	return (0);
}

/*
 * A command the caller does not need the answer to: posted on the high ring when it can be, waited
 * for on the low ring when it cannot (a full ring, no ring, or the high ring turned off). The flow
 * side's way in for MFLOW_INVALIDATE and a revival.
 */
int
npurpc_send_posted(uint8_t cmd, const void *payload, int plen)
{
	struct npurpc_softc *sc = npurpc_sc;
	int n, rc = 0;

	if (sc == NULL)
		return (-ENXIO);
	RPC_LOCK(sc);
	n = npurpc_post(sc, cmd, payload, plen);
	if (n != 0) {
		sc->hi_fallback++;
		n = npurpc_command(sc, cmd, payload, plen, NULL, 0, &rc);
		if (n >= 0 && rc != 0)
			n = -EIO;
		else if (n > 0)
			n = 0;
	}
	RPC_UNLOCK(sc);
	return (n);
}

/*
 * rpc_channel.hi_probe: post one LO_LIF_READ - a read, it changes nothing - on the high ring and
 * watch the target's done index for a second. The first thing to ask of a ring that has never
 * carried a command on this family.
 */
static int
npurpc_sysctl_hi_probe(SYSCTL_HANDLER_ARGS)
{
	struct npurpc_softc *sc = arg1;
	uint8_t rq[RPC_TBL_REQ_SIZE];
	uint64_t done0, done;
	char go[16];
	int err, i, n;

	if (req->newptr == NULL)
		return (sysctl_handle_string(oidp, sc->hi_probe_result,
		    sizeof(sc->hi_probe_result), req));
	/* Whatever was written starts it; its value means nothing. */
	go[0] = '\0';
	err = sysctl_handle_string(oidp, go, sizeof(go), req);
	if (err != 0)
		return (err);
	memset(rq, 0, sizeof(rq));
	le16enc(rq + RPC_TBL_NUM_ENTRIES, 1);
	RPC_LOCK(sc);
	done0 = (uint64_t)rpc_rd(sc, RPC_ST_RINGS + RPC_RING_DONE) |
	    ((uint64_t)rpc_rd(sc, RPC_ST_RINGS + RPC_RING_DONE + 4) << 32);
	n = npurpc_post(sc, RPC_CMD_LO_LIF_READ, rq, sizeof(rq));
	done = done0;
	for (i = 0; n == 0 && i < 100; i++) {
		done = (uint64_t)rpc_rd(sc, RPC_ST_RINGS + RPC_RING_DONE) |
		    ((uint64_t)rpc_rd(sc, RPC_ST_RINGS + RPC_RING_DONE + 4) << 32);
		if (done >= sc->hi_posted)
			break;
		pause("npurpch", hz / 100);
	}
	snprintf(sc->hi_probe_result, sizeof(sc->hi_probe_result),
	    "post %d; high ring posted %ju, done %ju -> %ju after %d ms%s\n", n,
	    (uintmax_t)sc->hi_posted, (uintmax_t)done0, (uintmax_t)done, i * 10,
	    (n == 0 && done >= sc->hi_posted) ? " - taken" : " - NOT taken");
	RPC_UNLOCK(sc);
	return (0);
}

/*
 * Open or shut every front port's interface entry, for the flow maker, and say how many read back
 * as asked. Kept as the wanted state too, so the programming pass - which runs again for a minute
 * after the coprocessor's own startup wipes its interface table - writes them the same way. Before
 * the channel has attached nothing is sent and the programming applies it.
 */
/*
 * The front ports took other addresses - their own, from the network agent (issue 730) - after
 * their interface entries were made with the ones npugiu gave them at attach. The programming
 * pass runs again from its start, and finds each entry there with the old address, which it
 * updates; on a boot it is still running, after a late load it had stopped.
 */
void
npurpc_front_macs_changed(void)
{
	struct npurpc_softc *sc = npurpc_sc;

	if (sc == NULL)
		return;
	RPC_LOCK(sc);
	/* No thread when the channel would not open at attach: nothing to program then. */
	if (!sc->stop && sc->tq != NULL) {
		sc->tries = 0;
		sc->announced = 0;
		taskqueue_enqueue_timeout(sc->tq, &sc->task, 1);
	}
	RPC_UNLOCK(sc);
}

int
npurpc_entries(int open)
{
	struct npurpc_softc *sc = npurpc_sc;
	uint8_t pl[RPC_LIF_REQ_SIZE], back[12];
	uint16_t want;
	int i, n, rc, verified = 0;

	npurpc_want_open = open ? 1 : 0;
	if (sc == NULL)
		return (0);
	want = RPC_LIF_FWD_L2 | (open ? 0 : RPC_LIF_OFFLOAD_DISABLED);
	RPC_LOCK(sc);
	if (!sc->opened || sc->stalled) {
		RPC_UNLOCK(sc);
		return (0);
	}
	for (i = 0; i < npuep_nfront; i++) {
		uint32_t idx = (uint32_t)npuep_front_ports[i].iface_id << RPC_LIF_IFACE_SHIFT;

		memset(pl, 0, sizeof(pl));
		le32enc(pl + RPC_LIF_INDEX, idx);
		le16enc(pl + RPC_LIF_FLAGS, want);
		le16enc(pl + RPC_LIF_UPDATE_MASK, RPC_LIF_UPDATE_OFFLOAD);
		rc = 0;
		if (npurpc_command(sc, RPC_CMD_LIF_ADD_UPDATE, pl, RPC_LIF_REQ_SIZE, NULL, 0, &rc) < 0)
			continue;
		memset(pl, 0, RPC_TBL_REQ_SIZE);
		le32enc(pl + RPC_TBL_S_INDEX, idx);
		le16enc(pl + RPC_TBL_NUM_ENTRIES, 1);
		memset(back, 0, sizeof(back));
		rc = 0;
		n = npurpc_command(sc, RPC_CMD_LO_LIF_READ, pl, RPC_TBL_REQ_SIZE, back, sizeof(back),
		    &rc);
		if (n == (int)sizeof(back) && rc == 0 && le16dec(back + 8) == want)
			verified++;
	}
	RPC_UNLOCK(sc);
	device_printf(sc->fac.dev, "rpc: the front ports' interface entries %s for the flow maker, "
	    "%d of %d read back\n", open ? "opened" : "shut", verified, npuep_nfront);
	return (verified);
}

/*
 * Ask the target to read one entry out of its interface table.
 *
 * Chosen because it changes nothing. The first command over a channel that has never carried one
 * should not also be the first command that configures something: if it goes wrong there are two
 * explanations, and no way to tell them apart.
 */
static int
npurpc_sysctl_probe(SYSCTL_HANDLER_ARGS)
{
	struct npurpc_softc *sc = arg1;
	uint8_t rq[RPC_TBL_REQ_SIZE], out[64];
	char buf[512];
	uint64_t posted, done;
	int err, rc = 0, go = 0, i, n, rlen = 0;

	/*
	 * Zeroed before use, and not as tidiness. The first run of this printed sixteen bytes of
	 * whatever was on the stack and they looked exactly like data - two plausible kernel
	 * pointers - because the answer carried no payload, nothing was copied in, and the buffer
	 * was never initialised. Reporting uninitialised memory as though the far side had sent it
	 * is the most misleading thing an instrument can do.
	 */
	memset(out, 0, sizeof(out));

	err = sysctl_handle_int(oidp, &go, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	memset(rq, 0, sizeof(rq));
	le32enc(rq + RPC_TBL_S_INDEX, 0);
	le16enc(rq + RPC_TBL_NUM_ENTRIES, 1);
	le32enc(rq + RPC_TBL_E_INDEX, 0);

	RPC_LOCK(sc);
	rlen = npurpc_command(sc, RPC_CMD_LO_LIF_READ, rq, sizeof(rq), out, sizeof(out), &rc);
	err = rlen < 0 ? rlen : 0;
	posted = (uint64_t)rpc_rd(sc, RPC_ST_RING_LO + RPC_RING_POSTED) |
	    ((uint64_t)rpc_rd(sc, RPC_ST_RING_LO + RPC_RING_POSTED + 4) << 32);
	done = (uint64_t)rpc_rd(sc, RPC_ST_RING_LO + RPC_RING_DONE) |
	    ((uint64_t)rpc_rd(sc, RPC_ST_RING_LO + RPC_RING_DONE + 4) << 32);
	RPC_UNLOCK(sc);

	n = snprintf(buf, sizeof(buf),
	    "rpc: LIF_READ -> %s, rc %#x%s; ring now posted %ju done %ju",
	    err == 0 ? "answered" : "no answer", rc,
	    (rc & RPC_RC_ERRNO) ? " (an errno, so the target refused it)" : "",
	    (uintmax_t)posted, (uintmax_t)done);
	if (err == 0) {
		n += snprintf(buf + n, sizeof(buf) - n, "; payload %d bytes", rlen);
		for (i = 0; i < rlen && i < 16 && n < (int)sizeof(buf) - 6; i++)
			n += snprintf(buf + n, sizeof(buf) - n, " %02x", out[i]);
	}
	device_printf(sc->fac.dev, "%s\n", buf);
	return (err);
}

/*
 * Send one command by hand, and report what came back.
 *
 * The channel defines forty-five commands and this driver knows the payload of exactly one of
 * them. The rest have to be learned, and the only way to learn a payload is to send one and read
 * the answer. The first hexadecimal number is the command, the rest are payload bytes:
 *
 *	sysctl dev.npuep.0.rpc_channel.command="25 00 00 00 00 01 00 00 00 00 00 00 00"
 *	sysctl -n dev.npuep.0.rpc_channel.command
 *
 * Unlike the network agent's probe, this does not refuse commands that write. It cannot: the
 * commands worth learning here are precisely the ones that fill in the interface table, and a
 * read-only version of this would have nothing to say. So it is sharp. A mistyped command number
 * is still a real command, and the fourteen front ports are downstream of it. It is an instrument
 * for a workbench, not a knob for a running firewall - which is also why it reports the command it
 * is about to send before sending it, so a mistake is visible in the log even if the target never
 * answers.
 */
/*
 * Tell the coprocessor about every front port.
 *
 * Two commands each and nothing else - there is no enable bit, no start command and no queue
 * index anywhere in either of them. A logical interface says what an interface is; binding a port
 * tag to it says which wire reaches it. Until both exist the fastpath has nothing to look up and
 * every received frame is dropped before it is even counted, which is what an empty table looks
 * like from the host: no traffic and no drops.
 *
 * OFFLOAD_DISABLED is the field that matters. The fastpath's chain ends "offload_disabled == 1 ->
 * host", so setting it hands every frame to this end rather than letting the coprocessor route,
 * which is the whole point of running the firewall here - until the owner turns the flow maker on:
 * then it is cleared (npurpc_entries), because past a shut entry the fast path names no connection
 * for the maker to hand back, and the maker decides what it forwards. L2 forwarding mode is chosen
 * for the same reason and one more: the destination-MAC check only runs in L3 mode, so in L2 the
 * address below never gates anything.
 */
/*
 * Program every front port, then read every one back, and believe only the read.
 *
 * Returns the number of ports that verified.
 *
 * The read-back is not belt and braces, it is the protocol. The coprocessor's own userspace
 * fastpath starts in response to THIS host's handshake, and as part of its startup it zeroes the
 * whole logical-interface table - about thirteen seconds after the handshake on this board. This
 * driver opens the control channel a fraction of a second after that same handshake, so the first
 * pass writes into a table that is about to be cleared. Every command is accepted and answered
 * with rc 0, and nothing survives; the counters said "14 of 14 made" over an empty table for an
 * hour before this was understood.
 *
 * The port bindings do survive, because the fastpath only maps those two tables and clears
 * neither. That asymmetry is worth remembering: pport state is not evidence that the interfaces
 * are still there.
 *
 * Read before writing, because creating is not idempotent: update_mask 0x00FF on an entry that is
 * still valid is refused with rc 1. Creating over a CLEARED entry is fine - the target notes that
 * the identifier was already allocated, zeroes the entry and fills it in anyway.
 */
/*
 * Say what the last read of one port's entry found, without the addresses (npurpc_why_set), and
 * what the coprocessor answered the create and the binding written before it - their rc was never
 * looked at, and on a board whose firmware refuses them it is the whole story. The first port that
 * did not verify in a pass is the one described: the others almost always fail the same way, and
 * one line has to fit what verify.sh prints.
 */
static void
npurpc_miss(char *out, size_t len, const struct npuep_front_port *fp, int n, int rc,
    const uint8_t *back, const uint8_t *mac, uint16_t want, int crc, int brc)
{
	static const uint8_t zero[12];
	char wrote[64];

	if (crc >= 0)
		snprintf(wrote, sizeof(wrote), "; written with create rc %d, binding rc %d",
		    crc, brc);
	else
		wrote[0] = '\0';

	if (n < 0)
		snprintf(out, len, "%s (interface %u): the read of its entry was not answered "
		    "(error %d)", fp->label, fp->iface_id, n);
	else if (rc != 0)
		snprintf(out, len, "%s (interface %u): the read of its entry was refused (rc %d)",
		    fp->label, fp->iface_id, rc);
	else if (n != 12)
		snprintf(out, len, "%s (interface %u): the read of its entry answered %d bytes, "
		    "12 expected", fp->label, fp->iface_id, n);
	else if (memcmp(back, zero, sizeof(zero)) == 0)
		snprintf(out, len, "%s (interface %u): its entry reads empty after it was written",
		    fp->label, fp->iface_id);
	else if (memcmp(back, mac, 6) != 0)
		snprintf(out, len, "%s (interface %u): its entry holds another address "
		    "(forwarding field %#06x)", fp->label, fp->iface_id, le16dec(back + 8));
	else
		snprintf(out, len, "%s (interface %u): its entry's forwarding field reads %#06x, "
		    "%#06x wanted", fp->label, fp->iface_id, le16dec(back + 8), want);
	strlcat(out, wrote, len);
}

static int
npurpc_program_front_ports(struct npurpc_softc *sc, char *miss, size_t misslen)
{
	uint8_t pl[RPC_LIF_REQ_SIZE], mac[6], back[12];
	uint16_t want;
	int i, rc, n, verified = 0;

	sx_assert(&sc->lock, SA_XLOCKED);
	want = RPC_LIF_FWD_L2 | (npurpc_want_open ? 0 : RPC_LIF_OFFLOAD_DISABLED);

	if (!sc->opened || sc->stalled)
		return (0);
	if (npugiu_front_mac(0, mac) != 0) {
		snprintf(miss, misslen, "the front ports have no addresses yet");
		return (0);
	}

	for (i = 0; i < npuep_nfront; i++) {
		const struct npuep_front_port *fp = &npuep_front_ports[i];
		uint32_t idx = (uint32_t)fp->iface_id << RPC_LIF_IFACE_SHIFT;
		int pass, got = 0, rn = 0, rrc = 0, crc = -1, brc = -1;

		if (npugiu_front_mac(i, mac) != 0) {
			if (miss[0] == '\0')
				snprintf(miss, misslen, "%s has no address yet", fp->label);
			continue;
		}

		for (pass = 0; pass < 2; pass++) {
			memset(pl, 0, RPC_TBL_REQ_SIZE);
			le32enc(pl + RPC_TBL_S_INDEX, idx);
			le16enc(pl + RPC_TBL_NUM_ENTRIES, 1);
			memset(back, 0, sizeof(back));
			rc = 0;
			n = npurpc_command(sc, RPC_CMD_LO_LIF_READ, pl, RPC_TBL_REQ_SIZE,
			    back, sizeof(back), &rc);
			rn = n;
			rrc = rc;

			if (n == (int)sizeof(back) && rc == 0 &&
			    memcmp(back, mac, sizeof(mac)) == 0 && le16dec(back + 8) == want) {
				verified++;
				got = 1;
				break;
			}
			if (pass != 0)
				break;		/* written and still not there; next round */

			/*
			 * There, and only the entry's offload field other than wanted - the flow maker
			 * was turned on or off: that field alone, because a create over a valid entry
			 * is refused.
			 */
			if (n == (int)sizeof(back) && rc == 0 && memcmp(back, mac, sizeof(mac)) == 0) {
				memset(pl, 0, sizeof(pl));
				le32enc(pl + RPC_LIF_INDEX, idx);
				le16enc(pl + RPC_LIF_FLAGS, want);
				le16enc(pl + RPC_LIF_UPDATE_MASK, RPC_LIF_UPDATE_OFFLOAD);
				rc = 0;
				(void)npurpc_command(sc, RPC_CMD_LIF_ADD_UPDATE, pl, RPC_LIF_REQ_SIZE,
				    NULL, 0, &rc);
				continue;
			}

			/*
			 * There, ours (forwarding L2), and with another address: the port took its own
			 * from the network agent after the entry was made (npunwa_take_macs, issue 730).
			 * The address and the offload field together, as an update - the fastpath's
			 * lif_fpop takes LIF_M_MAC on a valid entry - since a create over it is refused.
			 */
			if (n == (int)sizeof(back) && rc == 0 &&
			    (le16dec(back + 8) & (RPC_LIF_FWD_L2 | RPC_LIF_FWD_L3)) == RPC_LIF_FWD_L2) {
				memset(pl, 0, sizeof(pl));
				le32enc(pl + RPC_LIF_INDEX, idx);
				memcpy(pl + RPC_LIF_MAC, mac, sizeof(mac));
				le16enc(pl + RPC_LIF_FLAGS, want);
				le16enc(pl + RPC_LIF_UPDATE_MASK, RPC_LIF_UPDATE_MAC |
				    RPC_LIF_UPDATE_OFFLOAD);
				rc = 0;
				(void)npurpc_command(sc, RPC_CMD_LIF_ADD_UPDATE, pl, RPC_LIF_REQ_SIZE,
				    NULL, 0, &rc);
				sc->mac_updates++;
				continue;
			}

			if (sc->verified == npuep_nfront && !sc->wiped) {
				device_printf(sc->fac.dev,
				    "rpc: the coprocessor's fastpath cleared the interface table "
				    "during its own startup - programming it again\n");
				sc->wiped = 1;
			}

			memset(pl, 0, sizeof(pl));
			le32enc(pl + RPC_LIF_INDEX, idx);
			memcpy(pl + RPC_LIF_MAC, mac, sizeof(mac));
			le16enc(pl + RPC_LIF_MTU, NPURPC_LIF_MTU);
			le16enc(pl + RPC_LIF_FLAGS, want);
			le16enc(pl + RPC_LIF_UPDATE_MASK, RPC_LIF_CREATE);
			rc = 0;
			n = npurpc_command(sc, RPC_CMD_LIF_ADD_UPDATE, pl, RPC_LIF_REQ_SIZE,
			    NULL, 0, &rc);
			if (n < 0)
				break;
			crc = rc;

			/*
			 * And the binding. It survives a table wipe, so this is usually a no-op -
			 * but there is no command to read one back, so the only way to be sure is
			 * to write it.
			 */
			memset(pl, 0, RPC_PPORT_REQ_SIZE);
			pl[RPC_PPORT_IFACE] = fp->iface_id;
			le16enc(pl + RPC_PPORT_TAG, fp->tag);
			rc = 0;
			n = npurpc_command(sc, RPC_CMD_PPORT_UPDATE, pl, RPC_PPORT_REQ_SIZE,
			    NULL, 0, &rc);
			if (n >= 0)
				brc = rc;
		}

		if (!got && miss[0] == '\0')
			npurpc_miss(miss, misslen, fp, rn, rrc, back, mac, want, crc, brc);
	}

	return (verified);
}

/*
 * Keep at it until the coprocessor has stopped rearranging its own memory underneath us.
 *
 * A fixed delay would be a guess about a boot this host does not control, and the fastpath's
 * startup varies. Verification is the only thing that is right on a slow boot, a fast one, and a
 * driver loaded onto a coprocessor that has been up for hours and will never clear anything again.
 */
/*
 * Read the board's platform block, once. PLATFORM_READ changes nothing - the far side copies the
 * block its own startup filled in - so it is safe on the first pass, before the ports are
 * programmed. The XGS 136 answers 228 bytes: XGS_DT2, four cores, 128 interfaces, one high ring,
 * 1,500,000 connections, 65,536 next hops, 16,384 associations, a ten-second microflow timeout. The
 * flow table is told the two sizes, so a board that holds fewer is never asked for more. Where the
 * block answered, how long it is and its version field say whose software the coprocessor runs
 * (sfosnum.h). Caller holds the lock.
 */
static void
npurpc_platform_learn(struct npurpc_softc *sc)
{
	uint8_t b[NPURPC_RAW_MAX];
	char why[NPURPC_WHY_LEN];
	int n, rc, shift;

	/*
	 * Found by its answer: sent as written, no payload, at 36 and on, and on SFOS 21's and 22's
	 * numbering the first try is the right one. A number below the right one is another command
	 * given nothing to work on: the handlers in the vendor's source refuse a short payload before
	 * they do anything (usfp_rh's lif_rpc.c), and SFOS 23's 36, SA_IF_MTU_SET, was sent so at
	 * every boot of the XGS 126 that runs that numbering, which carried traffic all along. The
	 * search stops at the first block: what lies past it is not looked at.
	 */
	sc->cmd_raw = 1;
	for (shift = npurpc_platform_test; shift <= SFOSNUM_SHIFT_MAX; shift++) {
		memset(b, 0, sizeof(b));
		rc = -1;
		n = npurpc_command(sc, SFOSNUM_PLATFORM_READ + shift, NULL, 0, b, sizeof(b), &rc);
		if (n >= NPURPC_PLAT_LEN && rc == 0 && memcmp(b, "XGS_", 4) == 0)
			break;
	}
	sc->cmd_raw = 0;
	if (shift > SFOSNUM_SHIFT_MAX) {
		sc->cmd_shift = SFOSNUM_SHIFT_UNKNOWN;
		device_printf(sc->fac.dev, "rpc: no platform block at commands %d to %d - the "
		    "coprocessor's software numbers its commands in a way this driver does not know; "
		    "none from %d on is sent\n", SFOSNUM_PLATFORM_READ + npurpc_platform_test,
		    SFOSNUM_PLATFORM_READ + SFOSNUM_SHIFT_MAX, SFOSNUM_FIRST_MOVABLE);
		snprintf(why, sizeof(why), "the coprocessor's software numbers its commands in a way "
		    "this driver does not know (no platform block at %d to %d): the table can be "
		    "written but not read back", SFOSNUM_PLATFORM_READ + npurpc_platform_test,
		    SFOSNUM_PLATFORM_READ + SFOSNUM_SHIFT_MAX);
		npurpc_why_set(why);
		sc->plat_learned = 1;	/* asked once: asking again gives the same */
		return;
	}
	sc->cmd_shift = shift;
	sc->plat_len = n;
	memcpy(sc->plat_name, b + SFOSNUM_PLAT_OFF_NAME, sizeof(sc->plat_name) - 1);
	memcpy(sc->plat_ver, b + SFOSNUM_PLAT_OFF_VERSION, sizeof(sc->plat_ver) - 1);
	memcpy(sc->plat_asm, b + SFOSNUM_PLAT_OFF_ASSEMBLY, sizeof(sc->plat_asm) - 1);
	sc->sfos = sfosnum_version(shift, n, sfosnum_version_named(sc->plat_ver));
	if (sc->sfos != 0)
		device_printf(sc->fac.dev, "rpc: the coprocessor runs SFOS %d's software - platform "
		    "block at command %d, %d bytes, version %s%s\n", sc->sfos,
		    SFOSNUM_PLATFORM_READ + shift, n, sc->plat_ver, shift != 0 ?
		    "; every command from 36 on is sent one higher" : "");
	else
		device_printf(sc->fac.dev, "rpc: the platform block answered at command %d, %d bytes, "
		    "version %s - software this driver has not seen; commands from %d on are sent %d "
		    "higher, as the block was found, and the interface table's read-back checks it\n",
		    SFOSNUM_PLATFORM_READ + shift, n, sc->plat_ver, SFOSNUM_FIRST_MOVABLE, shift);
	sc->plat_id = b[NPURPC_PLAT_OFF_ID];
	sc->plat_cores = b[NPURPC_PLAT_OFF_CORES];
	sc->plat_ifaces = b[NPURPC_PLAT_OFF_IFACES];
	sc->plat_rings = b[NPURPC_PLAT_OFF_RINGS];
	sc->plat_mflow_to = le32dec(b + NPURPC_PLAT_OFF_MFLOW_TO);
	sc->plat_conn = le32dec(b + NPURPC_PLAT_OFF_CONN);
	sc->plat_nhop = le32dec(b + NPURPC_PLAT_OFF_NHOP);
	sc->plat_fwrules = le32dec(b + NPURPC_PLAT_OFF_FWRULES);
	sc->plat_sas = le32dec(b + NPURPC_PLAT_OFF_SAS);
	sc->plat_caps = le32dec(b + NPURPC_PLAT_OFF_CAPS);
	/* num_mflows from SFOS 22 on; on 21 its place holds num_pfs. */
	if (n >= SFOSNUM_PLAT_LEN_22) {
		sc->plat_mflows = le32dec(b + SFOSNUM_PLAT_OFF_NUM_MFLOWS);
		sc->plat_pfs = b[SFOSNUM_PLAT_OFF_NUM_PFS];
	} else
		sc->plat_pfs = b[SFOSNUM_PLAT_OFF_NUM_PFS_21];
	sc->plat_learned = 1;
	device_printf(sc->fac.dev, "rpc: platform %s (id %u, assembly %s): %u cores, %u interfaces, "
	    "%u high ring(s), %u connections, %u next hops, %u associations, microflow timeout %u s\n",
	    sc->plat_name, sc->plat_id, sc->plat_asm, sc->plat_cores, sc->plat_ifaces,
	    sc->plat_rings, sc->plat_conn, sc->plat_nhop, sc->plat_sas, sc->plat_mflow_to);
	npuflow_board(sc->plat_conn, sc->plat_nhop);
}

static void
npurpc_task(void *arg, int pending)
{
	struct npurpc_softc *sc = arg;
	char miss[NPURPC_WHY_LEN], why[NPURPC_WHY_LEN];
	int done, stop, give_up = 0;

	sx_xlock(&sc->lock);
	if (sc->stop) {
		sx_xunlock(&sc->lock);
		return;
	}

	/* Waiting for the window to be claimed (npurpc_attach): a second at a time. */
	if (sc->claiming) {
		uint32_t cfg = rpc_rd(sc, RPC_ST_CFG_REVISION);

		/*
		 * Once claimed, claimed: a failed open below leaves the configuration word it wrote -
		 * zero, reconfig_done with it - and read again here that would be taken for a window
		 * nobody has claimed, and waited on for good. Measured so on the XGS 136 on
		 * 2026-10-24, the first build of this retry: two minutes, then no channel at all.
		 */
		sc->claim_tries++;
		if (!sc->claimed && ((cfg >> 24) == 0 || sc->claim_tries < npurpc_claim_test)) {
			stop = sc->stop;
			if (sc->claim_tries >= NPURPC_CLAIM_WAIT) {
				device_printf(sc->fac.dev, "rpc: nothing claimed this window in %d "
				    "seconds (state %#010x) - leaving it alone\n", sc->claim_tries,
				    cfg);
				snprintf(why, sizeof(why), "the channel was not opened: nothing on "
				    "the coprocessor claimed its window in %d s (state %#010x)",
				    sc->claim_tries, cfg);
				npurpc_why_set(why);
				sc->claiming = 0;
				stop = 1;
			}
			sx_xunlock(&sc->lock);
			if (!stop)
				taskqueue_enqueue_timeout(sc->tq, &sc->task, hz);
			return;
		}
		sc->claimed = 1;
		if (sc->claim_waited) {
			device_printf(sc->fac.dev, "rpc: the window was claimed after %d seconds\n",
			    sc->claim_tries);
			sc->claim_waited = 0;
		}

		/*
		 * And opened - again, a second after a failed attempt, until the same two minutes from
		 * attach have passed (issue 736): a claim says the target's handler has started, not
		 * that it listens yet. The first failure is said in full, the rest only counted.
		 */
		sc->open_tries++;
		if (npurpc_open(sc, sc->open_tries == 1) != 0) {
			stop = sc->stop;
			if (time_uptime - sc->attached_at >= NPURPC_CLAIM_WAIT) {
				npurpc_why_get(miss, sizeof(miss));	/* the last attempt's reason */
				snprintf(why, sizeof(why), "%s - in %d attempts over %jd s", miss,
				    sc->open_tries, (intmax_t)(time_uptime - sc->attached_at));
				npurpc_why_set(why);
				device_printf(sc->fac.dev, "rpc: the channel did not open in %d attempts "
				    "over %jd seconds - leaving it\n", sc->open_tries,
				    (intmax_t)(time_uptime - sc->attached_at));
				sc->claiming = 0;
				stop = 1;
			} else {
				snprintf(why, sizeof(why), "not yet - the channel's configuration was not "
				    "acknowledged on attempt %d; trying again", sc->open_tries);
				npurpc_why_set(why);
			}
			sx_xunlock(&sc->lock);
			if (!stop)
				taskqueue_enqueue_timeout(sc->tq, &sc->task, hz);
			return;
		}
		sc->claiming = 0;
		if (sc->open_tries > 1)
			device_printf(sc->fac.dev, "rpc: the channel opened on attempt %d, %jd seconds "
			    "after attach\n", sc->open_tries, (intmax_t)(time_uptime - sc->attached_at));
	}

	/*
	 * Not open - it gave up above, or a port's address changed (npurpc_front_macs_changed)
	 * after it did: nothing to program, and nothing to count towards a "gave up" that would
	 * say the ports were tried.
	 */
	if (!sc->opened) {
		sx_xunlock(&sc->lock);
		return;
	}

	if (!sc->plat_learned)
		npurpc_platform_learn(sc);

	miss[0] = '\0';
	sc->verified = npurpc_program_front_ports(sc, miss, sizeof(miss));
	sc->tries++;

	done = (sc->verified == npuep_nfront);
	/*
	 * On the edge, not on every pass. This keeps checking for a minute after it first
	 * succeeds, because the clearing lands seconds later - and a line every two seconds for a
	 * minute is how a message worth reading becomes one nobody reads.
	 */
	if (done && !sc->announced) {
		device_printf(sc->fac.dev,
		    "rpc: all %d front ports have an interface and a binding, read back and "
		    "confirmed\n", npuep_nfront);
		sc->announced = 1;
	} else if (!done)
		sc->announced = 0;

	if (!done && sc->tries >= NPURPC_PROG_TRIES) {
		give_up = 1;
		device_printf(sc->fac.dev,
		    "rpc: gave up after %d seconds - only %d of %d front ports read back "
		    "correctly\n", NPURPC_PROG_TRIES * NPURPC_PROG_RETRY, sc->verified,
		    npuep_nfront);
	}

	/*
	 * Keep looking for a while even once they are all there. The clearing happens seconds
	 * after the channel opens, so stopping at the first success would stop just before it.
	 */
	if (done && sc->tries >= NPURPC_PROG_TRIES / 2)
		give_up = 1;

	/*
	 * The line verify.sh prints. A stalled channel already said why where it stalled, and a
	 * numbering this driver cannot place said so where it was looked for; either is the more
	 * exact, so it is left standing.
	 */
	if (done) {
		if (sc->sfos != 0)
			snprintf(why, sizeof(why), "confirmed: all %d front ports have an interface "
			    "and a binding, read back - the coprocessor runs SFOS %d's software",
			    npuep_nfront, sc->sfos);
		else
			snprintf(why, sizeof(why), "confirmed: all %d front ports have an interface "
			    "and a binding, read back - on coprocessor software this driver has not "
			    "seen, its commands from 36 on numbered %d higher", npuep_nfront,
			    sc->cmd_shift);
		npurpc_why_set(why);
	} else if (!sc->stalled && sc->cmd_shift != SFOSNUM_SHIFT_UNKNOWN && miss[0] != '\0') {
		snprintf(why, sizeof(why), "%s %d of %d front ports read back after %d s; the first "
		    "that did not: %s", give_up ? "gave up -" : "not yet -", sc->verified,
		    npuep_nfront, sc->tries * NPURPC_PROG_RETRY, miss);
		npurpc_why_set(why);
	}

	stop = sc->stop || give_up;
	sx_xunlock(&sc->lock);

	if (!stop)
		taskqueue_enqueue_timeout(sc->tq, &sc->task, hz * NPURPC_PROG_RETRY);
}

static int
npurpc_sysctl_command(SYSCTL_HANDLER_ARGS)
{
	struct npurpc_softc *sc = arg1;
	uint8_t pl[NPURPC_RAW_MAX];
	char in[400], *out = NULL, *q, *end;
	unsigned long v;
	int err, n = 0, i, k = 0, cmd = -1;

	if (req->newptr == NULL)
		goto report;

	in[0] = '\0';
	err = sysctl_handle_string(oidp, in, sizeof(in), req);
	if (err != 0)
		return (err);

	memset(pl, 0, sizeof(pl));
	for (q = in; *q != '\0'; ) {
		while (*q == ' ' || *q == '\t' || *q == ',')
			q++;
		if (*q == '\0')
			break;
		v = strtoul(q, &end, 16);
		if (end == q)
			return (EINVAL);
		q = end;
		if (cmd < 0) {
			if (v > 0xFF)
				return (EINVAL);
			cmd = (int)v;
			continue;
		}
		if (v > 0xFF || n >= NPURPC_RAW_MAX)
			return (EINVAL);
		pl[n++] = (uint8_t)v;
	}
	if (cmd < 0)
		return (EINVAL);

	device_printf(sc->fac.dev, "rpc: sending command %d with %d payload bytes\n", cmd, n);

	RPC_LOCK(sc);
	sc->raw_cmd = (uint8_t)cmd;
	sc->raw_plen = n;
	sc->raw_rc = 0;
	memset(sc->raw_resp, 0, sizeof(sc->raw_resp));
	/*
	 * In this driver's numbering, moved like its own commands, so a tool that asks for command 44
	 * gets the counters on every version (sfosnum.h); as written only when command_as_written is
	 * set, which is for looking at a numbering, not for using one.
	 */
	sc->cmd_raw = sc->cmd_as_written;
	sc->raw_wire = npurpc_wire_cmd(sc, (uint8_t)cmd);
	sc->raw_rlen = npurpc_command(sc, (uint8_t)cmd, pl, n, sc->raw_resp,
	    (int)sizeof(sc->raw_resp), &sc->raw_rc);
	sc->cmd_raw = 0;
	sc->raw_err = sc->raw_rlen < 0 ? -sc->raw_rlen : 0;
	if (sc->raw_rlen < 0)
		sc->raw_rlen = 0;
	RPC_UNLOCK(sc);

report:
	RPC_LOCK(sc);
	if (sc->raw_cmd == 0 && sc->raw_plen == 0 && sc->raw_rlen == 0 && sc->raw_err == 0) {
		RPC_UNLOCK(sc);
		return (sysctl_handle_string(oidp, "nothing sent yet", 17, req));
	}
	RPC_UNLOCK(sc);

	/* M_WAITOK can sleep, so it happens with no lock held. */
	out = malloc(NPURPC_CMD_OUT, M_DEVBUF, M_WAITOK);

	RPC_LOCK(sc);
	k = snprintf(out, NPURPC_CMD_OUT, "command %u, ", sc->raw_cmd);
	if (sc->raw_wire < 0 && sc->cmd_shift >= 0 && sc->sfos != 0)
		k += snprintf(out + k, NPURPC_CMD_OUT - k, "not sent: SFOS %d's software has no "
		    "such command, ", sc->sfos);
	else if (sc->raw_wire < 0 && sc->cmd_shift >= 0)
		k += snprintf(out + k, NPURPC_CMD_OUT - k, "not sent: from %d on the numbers depend "
		    "on the SFOS version, and the coprocessor's is not known, ", SFOSNUM_FIRST_NOT_21);
	else if (sc->raw_wire < 0)
		k += snprintf(out + k, NPURPC_CMD_OUT - k, "not sent: the coprocessor's numbering "
		    "from %d on is not known, ", SFOSNUM_FIRST_MOVABLE);
	else if (sc->raw_wire != sc->raw_cmd)
		k += snprintf(out + k, NPURPC_CMD_OUT - k, "sent as %d, ", sc->raw_wire);
	k += snprintf(out + k, NPURPC_CMD_OUT - k, "%d payload bytes -> %s", sc->raw_plen,
	    sc->raw_err != 0 ? "no answer" : "answered");
	if (sc->raw_err != 0)
		k += snprintf(out + k, NPURPC_CMD_OUT - k, " (errno %d)", sc->raw_err);
	else {
		k += snprintf(out + k, NPURPC_CMD_OUT - k, ", rc %#x%s, %d bytes back:",
		    sc->raw_rc, (sc->raw_rc & RPC_RC_ERRNO) ?
		    " (an errno, so the target refused it)" : "", sc->raw_rlen);
		for (i = 0; i < sc->raw_rlen && k < NPURPC_CMD_OUT - 6; i++)
			k += snprintf(out + k, NPURPC_CMD_OUT - k, " %02x", sc->raw_resp[i]);
	}
	RPC_UNLOCK(sc);

	err = sysctl_handle_string(oidp, out, NPURPC_CMD_OUT, req);
	free(out, M_DEVBUF);
	return (err);
}

/*
 * rpc_channel.counters_clear: write 0, 1, 2 or 3 to zero the coprocessor's debug, fast-path, port or
 * direct-forwarding counters, so a measurement read with tools/npuep-fpcnt.sh can start from zero
 * (discovery 13, 2026-10-25). The vendor's four LO_WORKER_*_CNT_CLR, with no request, as its host
 * sends them (cntrs_clr): SFOS 22's numbers 47 to 50, placed by sfosnum_wire - 46 to 48 on SFOS 21,
 * which has no direct-forwarding counters, and 48 to 51 on 23. The answer is EOPNOTSUPP where the
 * command cannot be placed, EIO when the far side refused it, and the rc is in rpc_channel.command's
 * report only for a command sent by hand.
 */
static int
npurpc_sysctl_counters_clear(SYSCTL_HANDLER_ARGS)
{
	struct npurpc_softc *sc = arg1;
	uint8_t resp[16];
	int err, n, rc, v;

	v = -1;
	err = sysctl_handle_int(oidp, &v, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);
	if (v < 0 || v > 3)
		return (EINVAL);
	RPC_LOCK(sc);
	rc = 0;
	n = npurpc_command(sc, (uint8_t)(SFOSNUM_DBG_CNT_CLR + v), NULL, 0, resp, (int)sizeof(resp),
	    &rc);
	RPC_UNLOCK(sc);
	if (n < 0)
		return (-n);
	return (rc != 0 ? EIO : 0);
}

static int
npurpc_sysctl_state(SYSCTL_HANDLER_ARGS)
{
	struct npurpc_softc *sc = arg1;
	char buf[512];
	uint64_t posted, done, hposted, hdone;
	uint32_t magic, cfg, hcfg;
	bus_size_t r = RPC_ST_RING_LO, h = RPC_ST_RINGS;

	RPC_LOCK(sc);
	magic = rpc_rd(sc, RPC_ST_CFG_MAGIC);
	cfg = rpc_rd(sc, RPC_ST_CFG_REVISION);
	posted = (uint64_t)rpc_rd(sc, r + RPC_RING_POSTED) |
	    ((uint64_t)rpc_rd(sc, r + RPC_RING_POSTED + 4) << 32);
	done = (uint64_t)rpc_rd(sc, r + RPC_RING_DONE) |
	    ((uint64_t)rpc_rd(sc, r + RPC_RING_DONE + 4) << 32);
	hposted = (uint64_t)rpc_rd(sc, h + RPC_RING_POSTED) |
	    ((uint64_t)rpc_rd(sc, h + RPC_RING_POSTED + 4) << 32);
	hdone = (uint64_t)rpc_rd(sc, h + RPC_RING_DONE) |
	    ((uint64_t)rpc_rd(sc, h + RPC_RING_DONE + 4) << 32);
	hcfg = rpc_rd(sc, h + RPC_RING_CFG);
	RPC_UNLOCK(sc);

	snprintf(buf, sizeof(buf),
	    "%s%s  magic %#010x  revision %u  hi_rings %u  reconfig_done %u\n"
	    "  low ring:  posted %ju done %ju (this driver has posted %ju)\n"
	    "  high ring: posted %ju done %ju  num %u fclt %u dbell %u shared %u\n"
	    "  commands %ju answered %ju timed out %ju",
	    sc->opened ? "open" : "not open", sc->stalled ? ", STALLED" : "",
	    magic, cfg & 0xFFFF, (cfg >> 16) & 0xFF, (cfg >> 24) & 0xFF,
	    (uintmax_t)posted, (uintmax_t)done, (uintmax_t)sc->posted,
	    (uintmax_t)hposted, (uintmax_t)hdone,
	    hcfg & 0xFF, (hcfg >> 8) & 0xFF, (hcfg >> 16) & 0xFF, (hcfg >> 24) & 0xFF,
	    (uintmax_t)sc->commands, (uintmax_t)sc->answers, (uintmax_t)sc->timeouts);

	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}

static void
npurpc_add_sysctls(struct npurpc_softc *sc)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->fac.dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->fac.dev);
	struct sysctl_oid_list *child = SYSCTL_CHILDREN(tree);
	struct sysctl_oid *node;

	node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "rpc_channel",
	    CTLFLAG_RD | CTLFLAG_MPSAFE, NULL, "the control-message channel");
	if (node == NULL)
		return;
	child = SYSCTL_CHILDREN(node);

	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "open", CTLFLAG_RD, &sc->opened, 0,
	    "the target has taken this host's configuration");
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "cmd_shift", CTLFLAG_RD, &sc->cmd_shift, 0,
	    "how much higher the coprocessor numbers its commands from 36 on than SFOS 21 and 22 "
	    "do: 0, 1 for SFOS 23's numbering, -1 when it could not be found (issue 736)");
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "sfos", CTLFLAG_RD, &sc->sfos, 0,
	    "the SFOS whose software the coprocessor runs - 21, 22 or 23 - from its platform block; "
	    "0 when not known (sfosnum.h)");
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "platform_len", CTLFLAG_RD, &sc->plat_len, 0,
	    "bytes the platform block came back as: 228 from SFOS 21, 232 from 22 and 23");
	SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "platform_version", CTLFLAG_RD, sc->plat_ver, 0,
	    "the platform block's version field: a git hash from SFOS 21, UNKNOWN_VERSION after it");
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "command_as_written", CTLFLAG_RW, &sc->cmd_as_written,
	    0, "1 sends a command given to rpc_channel.command by the number written; 0, the default, "
	    "moves it from 36 on as the driver's own are moved (cmd_shift)");
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "open_tries", CTLFLAG_RD, &sc->open_tries, 0,
	    "attempts it took to open the channel - more than one when the target claimed the "
	    "window before it listened (issue 736)");
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "dbell_dropped", CTLFLAG_RD, &sc->dbell_dropped, 0,
	    "configuration doorbells skipped under hw.npuep.rpc_dbell_drop, the instrument");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "mac_updates", CTLFLAG_RD, &sc->mac_updates, 0,
	    "front-port interface entries given the port's new address after it took its own from "
	    "the network agent (issue 730)");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "state",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, npurpc_sysctl_state, "A",
	    "the channel and its ring, read live");
	strlcpy(sc->hi_probe_result, "not probed yet\n", sizeof(sc->hi_probe_result));
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "hi_probe",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, npurpc_sysctl_hi_probe, "A",
	    "write anything to post one harmless read on the high ring and watch it taken; reading "
	    "shows what the last write saw");
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "hi_off", CTLFLAG_RW, &sc->hi_off, 0,
	    "1 posts nothing on the high ring: every command is waited for on the low one");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "hi_commands", CTLFLAG_RD, &sc->hi_commands, 0,
	    "commands posted on the high ring without a wait");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "hi_full", CTLFLAG_RD, &sc->hi_full, 0,
	    "times the high ring was full and the command waited on the low ring instead");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "hi_fallback", CTLFLAG_RD, &sc->hi_fallback, 0,
	    "commands meant for the high ring that were waited for on the low one");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "probe",
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, npurpc_sysctl_probe, "I",
	    "write anything to send one harmless read command and report what came back");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "command",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, npurpc_sysctl_command, "A",
	    "send one command by hand: the command number - SFOS 22's, placed like the driver's own "
	    "on the coprocessor's numbering - then its payload, all hexadecimal");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "counters_clear",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0, npurpc_sysctl_counters_clear, "I",
	    "write 0, 1, 2 or 3 to zero the coprocessor's debug, fast-path, port or direct-forwarding "
	    "counters - SFOS 21's software has no direct-forwarding ones (discovery 13)");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "wait_us_sum", CTLFLAG_RD, &sc->wait_sum, 0,
	    "microseconds spent waiting for answers, all commands");
	SYSCTL_ADD_U64(ctx, child, OID_AUTO, "wait_n", CTLFLAG_RD, &sc->wait_n, 0,
	    "answers waited for; wait_us_sum / wait_n is the average");
	SYSCTL_ADD_U32(ctx, child, OID_AUTO, "wait_us_max", CTLFLAG_RD, &sc->wait_max, 0,
	    "the longest wait for an answer, microseconds");
	SYSCTL_ADD_U32(ctx, child, OID_AUTO, "wait_us_last", CTLFLAG_RD, &sc->wait_last, 0,
	    "the last wait for an answer, microseconds");
	SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "platform", CTLFLAG_RD, sc->plat_name, 0,
	    "the board's platform name, from PLATFORM_READ");
	SYSCTL_ADD_U32(ctx, child, OID_AUTO, "board_conn", CTLFLAG_RD, &sc->plat_conn, 0,
	    "connections the board holds, from its platform block");
	SYSCTL_ADD_U32(ctx, child, OID_AUTO, "board_nhop", CTLFLAG_RD, &sc->plat_nhop, 0,
	    "next hops the board holds, from its platform block");
	SYSCTL_ADD_U32(ctx, child, OID_AUTO, "board_sas", CTLFLAG_RD, &sc->plat_sas, 0,
	    "IPsec associations the board holds, from its platform block");
	SYSCTL_ADD_U8(ctx, child, OID_AUTO, "board_cores", CTLFLAG_RD, &sc->plat_cores, 0,
	    "the coprocessor's cores, from its platform block");
	SYSCTL_ADD_U8(ctx, child, OID_AUTO, "board_rings", CTLFLAG_RD, &sc->plat_rings, 0,
	    "high-priority rings the board asks for, from its platform block");
	SYSCTL_ADD_U32(ctx, child, OID_AUTO, "board_mflow_to", CTLFLAG_RD, &sc->plat_mflow_to, 0,
	    "the board's default microflow timeout, seconds");
}

int
npurpc_attach(struct npuep_facility *fac)
{
	struct npurpc_softc *sc;
	char why[NPURPC_WHY_LEN];
	uint32_t cfg;
	int err;

	if (npurpc_sc != NULL)
		return (EBUSY);
	if (fac->size < NPURPC_DESC_OFF + NPURPC_DESC_COUNT * 16) {
		device_printf(fac->dev, "rpc: window is only %ju bytes\n",
		    (uintmax_t)fac->size);
		return (ENXIO);
	}

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->fac = *fac;
	sx_init(&sc->lock, "npurpc");
	sc->cmd_shift = SFOSNUM_SHIFT_UNKNOWN;	/* until the platform block is found */

	err = npurpc_alloc_cmd(sc);
	if (err != 0) {
		device_printf(fac->dev, "rpc: no DMA buffer for commands (%d)\n", err);
		snprintf(why, sizeof(why), "the channel was not opened: no DMA buffer for its "
		    "commands (error %d)", err);
		npurpc_why_set(why);
		npurpc_free_cmd(sc);
		sx_destroy(&sc->lock);
		free(sc, M_DEVBUF);
		return (err);
	}

	npurpc_sc = sc;

	/*
	 * The target publishes reconfig_done before anything else happens, and it is the only
	 * evidence that something is listening on the other end of this window. Without it there
	 * is no point writing a magic word at it.
	 *
	 * Not yet is not never (issue 736). The coprocessor's software claims the window in the
	 * same seconds as this attach, and which comes first is a race: on the first XGS 126 run by
	 * its owner it had claimed it by second 15 of the boot and the channel opened; on the second,
	 * at the same second 15, it had not, the driver looked once and left the window alone, and no
	 * front port carried a frame for the whole boot. So the programming thread waits for the
	 * claim, a second at a time, and opens the channel when it comes.
	 */
	cfg = rpc_rd(sc, RPC_ST_CFG_REVISION);
	if ((cfg >> 24) == 0 || npurpc_claim_test > 0) {
		device_printf(fac->dev, "rpc: nothing has claimed this window yet (state %#010x%s) - "
		    "waiting up to %d seconds for the coprocessor's software\n", cfg,
		    npurpc_claim_test > 0 ? ", taken as unclaimed: hw.npuep.rpc_claim_test" : "",
		    NPURPC_CLAIM_WAIT);
		npurpc_why_set("not yet - nothing on the coprocessor has claimed the channel's "
		    "window; waiting for it");
		sc->claim_waited = 1;
	}

	/*
	 * Opened on the programming thread too, claimed already or not: the open is retried there
	 * when the target has claimed the window but does not answer yet (issue 736), and a thread
	 * is there for a port's address changing whatever came of it (npurpc_front_macs_changed).
	 *
	 * And the table not here either. Attach runs within a fraction of a second of the handshake
	 * that starts the coprocessor's own software, and that software clears the table this would
	 * be writing.
	 */
	sc->claiming = 1;
	sc->attached_at = time_uptime;
	sc->tq = taskqueue_create("npurpc", M_WAITOK, taskqueue_thread_enqueue, &sc->tq);
	TIMEOUT_TASK_INIT(sc->tq, &sc->task, 0, npurpc_task, sc);
	taskqueue_start_threads(&sc->tq, 1, PI_NET, "npurpc");
	taskqueue_enqueue_timeout(sc->tq, &sc->task, sc->claim_waited ? hz : 0);

	npurpc_add_sysctls(sc);

	/*
	 * A channel that would not open is not a reason to fail: everything else on this device
	 * keeps working without it, and leaving the driver attached leaves the state readable,
	 * which is what anyone looking into it would want.
	 */
	return (0);
}

void
npurpc_detach(void)
{
	struct npurpc_softc *sc = npurpc_sc;

	if (sc == NULL)
		return;

	/*
	 * Stop the programming thread first, and keep cancelling until there is nothing left to
	 * cancel. One drain is not enough on its own: a pass that started before the flag was
	 * published can requeue after the drain returns, and then taskqueue_free waits forever for
	 * a thread that keeps being handed work. That is a hung, uninterruptible unload, and this
	 * driver has already produced one.
	 */
	RPC_LOCK(sc);
	sc->stop = 1;
	RPC_UNLOCK(sc);
	/*
	 * And unpublished, so that a caller from another facility - the network agent's task,
	 * telling the interface table a port moved (npurpc_front_macs_changed), which detaches
	 * after this one - finds no channel rather than one about to be freed.
	 */
	npurpc_sc = NULL;

	if (sc->tq != NULL) {
		while (taskqueue_cancel_timeout(sc->tq, &sc->task, NULL) != 0)
			taskqueue_drain_timeout(sc->tq, &sc->task);
		taskqueue_drain_timeout(sc->tq, &sc->task);
		taskqueue_free(sc->tq);
		sc->tq = NULL;
	}

	/*
	 * Withdraw before freeing. The target stops fetching descriptors when the magic is gone,
	 * and the command buffer it might otherwise DMA into is freed immediately after this.
	 */
	RPC_LOCK(sc);
	if (sc->opened) {
		rpc_wr(sc, RPC_ST_CFG_MAGIC, 0);
		rpc_barrier(sc);
		sc->opened = 0;
	}
	RPC_UNLOCK(sc);

	npurpc_free_cmd(sc);
	sx_destroy(&sc->lock);
	free(sc, M_DEVBUF);
	npurpc_sc = NULL;
}
