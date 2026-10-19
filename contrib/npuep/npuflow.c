/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The ARMADA driver's flow table, first slice: the candidates.
 *
 * Once the offload gate is open and an interface entry lets its frames past, the fast path makes
 * a microflow entry for every connection it sees and punts the frames to the host with that
 * entry's identity in the sixty-four bytes of metadata in front of each (measured on the XGS 136,
 * docs/findings/a-flow-dropped-by-the-armada.md). The receive path hands each such frame's tuple
 * and identity here; this keeps them as candidates, and a task asks pf and the routing table what
 * it would take to accelerate each one - whether pf holds a settled state for it, how that state
 * is translated, and where its next hop is.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sbuf.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <machine/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/vnet.h>
#include <net/pfvar.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>

#include "npuep.h"
#include "npuflow.h"

/*
 * A next-hop table entry, shared by every flow direction that leaves the same way: keyed by the
 * whole resolved answer (egress interface, MTU, the two MACs, the family), reference-counted,
 * freed when its last flow lets go. Many internet-bound flows resolve to one WAN gateway, so
 * without sharing the table would fill at about half its size in flows; with it they cost one
 * entry. The revision is kept across reuse, as the far side expects, because a microflow carries
 * it beside the index.
 */
struct npuflow_nhop_ent {
	int		used;
	uint32_t	refcnt;
	uint8_t		rev;
	uint8_t		iface;
	uint16_t	mtu;
	uint16_t	etype;		/* ETHERTYPE_IP or ETHERTYPE_IPV6: what NHOP_PROGRAM says */
	uint8_t		dmac[6];
	uint8_t		smac[6];
};

/* One flow this driver has handed to the fast path: a connection and its two microflows. */
struct npuflow_conn {
	int		used;
	int		making;		/* reserved, FLOW_CREATE_FP not acknowledged yet - keepalive leaves it */
	uint16_t	conn_rev;	/* bumped each reuse so a stale command is refused, not misapplied */
	uint8_t		probe_dir;	/* which of the two tuples the originating candidate was */
	int		pf_dir;		/* PF_IN/PF_OUT: the list pf found the state in, for the keepalive */
	int		snat, dnat;
	struct npuep_pf_tuple tuple[2];	/* [0] original (opener->responder), [1] reply */
	uint32_t	mf_id[2];	/* the two microflow ids, as the fast path made them */
	uint32_t	mf_rev[2];
	uint32_t	nhop[2];	/* the next-hop table indices, freed on takeout */
	int		when;		/* ticks at creation, and at each revival */
	int		rv_tick;	/* ticks at the last revival, 0 for none */
	int		rv_wait;	/* how long after it the next may be: doubles while they keep coming */
	int		rv_streak;	/* revivals each within a second of the last */
	/*
	 * The chains and the live list. Each direction's tuple is linked into the chain of its own
	 * hash; a link is (index << 1 | direction) + 1, so zero ends a chain. live_pos is where the
	 * entry sits in the list of connections that exist, which every pass walks.
	 */
	uint32_t	hnext[2];
	uint32_t	live_pos;
	/*
	 * What the coprocessor counted for it, from its statistics messages (npuflow_cmsg_rx). hw_raw
	 * is the SLOT's: the totals the coprocessor last sent for this index, each direction as it sent
	 * them - bytes' low word, then the bytes' bits 32 to 34 under the packet count shifted left by
	 * three - kept across the connections that use the slot, because the coprocessor does not start
	 * them again for a new one: measured, the second of two streams through slot 2 found it at
	 * 3,056,282,322 bytes (docs/measurements/xgs136-counts-2026-10-19.md). hw_pkts and hw_bytes
	 * are this connection's: what moved while it held the slot, [0] the opener's direction.
	 * hw_moved is when a count last grew, in seconds of uptime; 0 until a message has named it.
	 * hw_pf_* is how much of that the keepalive has added to pf's states for it.
	 */
	uint32_t	hw_raw[2][2];
	uint64_t	hw_pkts[2];
	uint64_t	hw_bytes[2];
	uint64_t	hw_pf_pkts[2];
	uint64_t	hw_pf_bytes[2];
	time_t		hw_moved;
	uint8_t		hw_seen;
};

/*
 * The coprocessor's control messages - the vendor's cmsg_types.h - arrive on tag 254 with no port
 * of their own: an Ethernet header of zeros with type 0xEFEF, then a header of four bytes (padding,
 * type, version) and, for the statistics, num_entries (u16), num_bursts (u8) and is_bidir (u8);
 * then each burst, a u32 with the first index in its low 26 bits and the count less one in the top
 * six, and that many entries. A connection's (type 1, CONN_STATS) is eighteen bytes: for each
 * direction a u32 of bytes and a u32 of the bytes' bits 32 to 34 under the packets shifted left by
 * three, then the slot's revision; an interface's (type 0, LIF_STATS) is the same without the
 * revision. All little-endian; the layout of the XGS 3300's, read off the XGS 136's own frames
 * before it was trusted (docs/measurements/xgs136-counts-2026-10-19.md).
 */
#define	NPUFLOW_CMSG_TAG		254
#define	NPUFLOW_CMSG_ETHERTYPE		0xefef
#define	NPUFLOW_CMSG_VERSION		1
#define	NPUFLOW_CMSG_TYPES		10	/* the types the vendor names, 0 to 9: each counted */
#define	NPUFLOW_CMSG_LIF_STATS		0
#define	NPUFLOW_CMSG_CONN_STATS		1
#define	NPUFLOW_HW_ENTRY_LEN		18
#define	NPUFLOW_LIF_ENTRY_LEN		16
#define	NPUFLOW_HW_PKT_SHIFT		3
#define	NPUFLOW_HW_PKT_MASK		0x1fffffffu	/* 29 bits of packets */
#define	NPUFLOW_HW_BYTE_BITS		35

/*
 * The tables are the board's own size. The XGS 136's platform block says 1,500,000 connections
 * and 65,536 next hops, and they are allocated at that size once the block has been read - on a
 * host whose memory holds them in a sixteenth (NPUFLOW_RAM_SHARE), which the XGS 136's eight
 * gigabytes do; hw.npuep.flow_max at the loader sets the size either way, never past the ceiling.
 * A board whose block never answers gets NPUFLOW_FLOW_DEFAULT after NPUFLOW_BOARD_WAIT seconds.
 * Only the XGS 136 has been measured.
 *
 * 256 connections and 128 next hops in fixed arrays were the bounds until 2026-10-18, "bounded for
 * a bench", each found by reading the whole table. A connection is now found through a chain of a
 * hash table as large as the table itself, an index is handed out from a stack of those not in
 * use, and the passes walk a list of the connections that exist - the OCTEON TX driver's table,
 * which took 1,024 connections at once at 9.4 Gbit/s after the same change.
 *
 * Indices 0 and 1 are never handed out, the reason the OCTEON TX driver gives: the hand-programmed
 * experiments used 1 and bring-up uses 0, so starting at 2 keeps a flow this driver made from
 * colliding with either.
 */
#define	NPUFLOW_FLOW_DEFAULT	1500000
#define	NPUFLOW_FLOW_MIN	1024
#define	NPUFLOW_FLOW_CEIL	2000000
#define	NPUFLOW_NHOP_DEFAULT	65536
#define	NPUFLOW_NHOP_CEIL	65536
#define	NPUFLOW_RAM_SHARE	16
#define	NPUFLOW_BOARD_WAIT	120	/* seconds to wait for the platform block before guessing */
#define	NPUFLOW_PRINT_MAX	4096	/* flow.flows prints the first ones; the rest are counted */

/*
 * At most this many new flows made automatically per sweep, flow.make_per_sweep. Eight were the
 * bound while every command waited ten milliseconds for its answer; with the answer spun for, a
 * make is two commands of tens of microseconds each.
 */
#define	NPUFLOW_PER_SWEEP	256
/*
 * And at most this many connections asked of pf by the keepalive per sweep, from where the last
 * sweep stopped: a million connections asked every second would be the whole of a core.
 */
#define	NPUFLOW_KA_PER_SWEEP	16384
/* A candidate not punted for this long has been made, or has ended: its slot is cleared. */
#define	NPUFLOW_CAND_AGE	10	/* seconds */
#define	NPUFLOW_MFLOW_TO	60	/* a microflow's idle timeout in seconds, as measured on octep */

/*
 * What the owner keeps with the host - the OCTEON TX driver's two lists, field for field (octep.h):
 * connections handed back one at a time from the Flows page (flow.release), kept until pf lets the
 * state go, and addresses, networks and ports never offloaded (flow.exclude), in the canonical
 * tokens scripts/xgs/flow_exclude.php writes. Both are asked by npuflow_make and by the automatic
 * make before anything is reserved.
 */
#define	NPUFLOW_HELD_MAX	256
#define	NPUFLOW_EXCL_MAX	64
#define	NPUFLOW_EXCL_TEXT	3072	/* 64 of the longest token, "a6:" 32 digits "/128" and a space */

struct npuflow_held {
	int			 used;
	int			 pf_dir;
	uint8_t			 probe_dir;
	struct npuep_pf_tuple	 tuple[2];
};

struct npuflow_excl {
	uint8_t			 kind;		/* 4 an IPv4 network, 6 an IPv6 one, 'p' a port */
	uint8_t			 plen;
	uint16_t		 port;		/* host order */
	uint32_t		 a4;		/* host order */
	uint32_t		 a6[4];		/* network order, as struct npuep_pf_tuple holds them */
};

struct npuflow_softc {
	struct mtx		 mtx;	/* the candidate and flow tables' fields, for the readers */
	struct sx		 sx;	/* serialises make/takeout/sweep: each sleeps inside an RPC */
	device_t		 dev;
	struct npuep_flow_cand	 cand[NPUFLOW_CAND_MAX];
	uint64_t		 pushed, replaced, seen_again, aged;
	struct taskqueue	*tq;
	struct timeout_task	 task;
	int			 period;	/* ticks between sweeps; 0 stops them */
	int			 detaching;
	int			 attached_at;	/* ticks, for NPUFLOW_BOARD_WAIT */
	/*
	 * The flow table. flow.auto lets the sweep turn settled, resolved candidates into
	 * FLOW_CREATE_FP on its own, keep each while pf still holds its state, and take it back when
	 * pf lets go; flow.accelerate still makes one by hand. Every flow owns one connection index
	 * and two next-hop indices out of these tables.
	 */
	int			 autom;		/* flow.auto */
	int			 per_sweep;	/* flow.make_per_sweep */
	uint32_t		 board_conn, board_nhop;	/* from the platform block, 0 until read */
	struct npuflow_conn	*conn;		/* conn_cap entries, NULL until the board is known */
	uint32_t		 conn_cap, conn_max;
	uint32_t		*conn_head;	/* 1 << conn_hbits chain heads */
	uint32_t		 conn_hbits;
	uint32_t		*conn_free;	/* a stack of the indices not in use */
	uint32_t		 conn_nfree;
	uint32_t		*conn_live;	/* the indices in use, in no order */
	uint32_t		 conn_nlive;
	uint32_t		 ka_next;	/* where in the live list the keepalive goes on from */
	struct npuflow_nhop_ent	*nhop;		/* nhop_cap entries */
	uint32_t		 nhop_cap, nhop_hi;	/* nothing at or above nhop_hi is in use */
	uint32_t		 conn_used, nhop_used;
	uint64_t		 made, taken, auto_made, auto_gone, flow_full, nhop_full, v6_refused;
	uint64_t		 remade;	/* let go by the fast path, made again from new identities */
	uint64_t		 revived, revive_failed, fast_runs;
	uint64_t		 remake_held;	/* given back soon after made: left with the host a while */
	int			 fast_last;	/* ticks at the last let-go pass a kick ran */
	uint64_t		 held_us_last, held_us_max;	/* the sweep's upkeep, under sx */
	uint64_t		 sloppy_marked;	/* pf states told to stop judging sequence numbers */
	int			 pf_sloppy;	/* 1 (default) marks them; 0 only to measure without */
	struct task		 fast_task;	/* the receive path's kick: a connection was let go */
	volatile u_int		 fast_kicked;
	uint32_t		 mk_id[NPUFLOW_CAND_MAX];	/* automake's list, under sx */
	uint16_t		 mk_slot[NPUFLOW_CAND_MAX];
	uint32_t		 rm_ci[NPUFLOW_CAND_MAX];	/* the connections let go: revive or remake */
	uint32_t		 rm_id[NPUFLOW_CAND_MAX];
	uint16_t		 rm_slot[NPUFLOW_CAND_MAX];
	uint8_t			 rm_revive[NPUFLOW_CAND_MAX];
	/*
	 * The firewall revision barrier. Every flow is stamped with fw_rev; flow.fw_rev_bump raises
	 * it and posts FW_STATE_REV_SET, which makes the fast path void the whole microflow table at
	 * once - the only thing a ruleset reload must do, so the coprocessor never forwards under rules
	 * that no longer exist. The OPNsense firewall hook writes it on every reload.
	 */
	uint16_t		 fw_rev;
	uint64_t		 fw_rev_bumps, fw_rev_bump_fail;
	char			 accel_result[640];
	/* What the owner keeps with the host, under mtx. */
	struct npuflow_held	 held[NPUFLOW_HELD_MAX];
	uint32_t		 held_next;
	struct npuflow_excl	 excl[NPUFLOW_EXCL_MAX];
	int			 nexcl;
	char			 excl_text[NPUFLOW_EXCL_TEXT];
	uint64_t		 excluded, released;
	char			 release_result[160];
	/*
	 * The coprocessor's control messages, read on arrival (npuflow_cmsg_rx), under mtx: how many
	 * of each type; of the connection counts, how many were applied to a connection here, how many
	 * named one gone, and how many messages could not be read to their end. hw_to_pf 1 has the
	 * keepalive add each connection's counts to its pf states (npuep_pf_count). The per-port
	 * counts (type 0) kept by front port, and lif_apply 1 also adds them to the port's interface.
	 */
	uint64_t		 cmsg_rx[NPUFLOW_CMSG_TYPES];
	uint64_t		 hw_applied, hw_stale, hw_bad;
	int			 hw_to_pf;
	uint64_t		 hw_pf_conns, hw_pf_states;
	int			 lif_apply;
	uint64_t		 lif_applied, lif_unknown;
	uint64_t		 lif_pkts[NPUEP_NFRONT][2], lif_bytes[NPUEP_NFRONT][2];
	uint8_t			 lif_seen[NPUEP_NFRONT];
};

static struct npuflow_softc *npuflow_sc;

#define	FLOW_LOCK(sc)	mtx_lock(&(sc)->mtx)
#define	FLOW_UNLOCK(sc)	mtx_unlock(&(sc)->mtx)

static void	npuflow_keepalive(struct npuflow_softc *);
static void	npuflow_automake(struct npuflow_softc *);
static void	npuflow_letgo(struct npuflow_softc *);
static void	npuflow_fast(void *, int);
static int	npuflow_conn_find(struct npuflow_softc *, const struct npuep_pf_tuple *);
static void	npuflow_takeout_how(struct npuflow_softc *, int, int);
static int	npuflow_sysctl_auto(SYSCTL_HANDLER_ARGS);
static int	npuflow_make(struct npuflow_softc *, uint32_t, int, struct sbuf *);
static void	npuflow_takeout(struct npuflow_softc *, int);
static int	npuflow_takeout_all(struct npuflow_softc *);
static int	npuflow_kept(struct npuflow_softc *, const struct npuep_pf_tuple *, int);
static int	npuflow_kept_locked(struct npuflow_softc *, const struct npuep_pf_tuple *, int);
static void	npuflow_held_sweep(struct npuflow_softc *);

/*
 * The tuple out of an IPv4 header - the OCTEON TX driver's octep_dp_tuple_from_ip. Fragments and
 * options are not candidates; an ICMP echo is keyed as pf keys it, the echo id in sport and the
 * virtual type 8 in dport.
 */
int
npuflow_tuple_from_ip(const uint8_t *ip, uint32_t len, struct npuep_pf_tuple *t)
{
	uint32_t ihl;

	if (len < 20 || (ip[0] >> 4) != 4)
		return (0);
	ihl = (uint32_t)(ip[0] & 0x0f) * 4;
	if (ihl < 20 || (ip[6] & 0x3f) != 0 || ip[7] != 0)
		return (0);
	bzero(t, sizeof(*t));
	t->af = AF_INET;
	t->proto = ip[9];
	memcpy(&t->sip, ip + 12, 4);
	memcpy(&t->dip, ip + 16, 4);
	if ((t->proto == IPPROTO_TCP || t->proto == IPPROTO_UDP) && len >= ihl + 4) {
		memcpy(&t->sport, ip + ihl, 2);
		memcpy(&t->dport, ip + ihl + 2, 2);
	}
	if (t->proto == IPPROTO_ICMP && len >= ihl + 6 && (ip[ihl] == 0 || ip[ihl] == 8)) {
		memcpy(&t->sport, ip + ihl + 4, 2);
		t->dport = htons(8);
	}
	return (1);
}

/*
 * The tuple out of an IPv6 header: TCP or UDP directly after the forty bytes, which is every frame
 * of a connection worth having - one with extension headers in front of its transport is left to
 * the host. The addresses are kept in full and their folds stand in sip and dip.
 */
int
npuflow_tuple_from_ip6(const uint8_t *ip, uint32_t len, struct npuep_pf_tuple *t)
{

	if (len < 40 + 4 || (ip[0] >> 4) != 6)
		return (0);
	if (ip[6] != IPPROTO_TCP && ip[6] != IPPROTO_UDP)
		return (0);
	bzero(t, sizeof(*t));
	t->af = AF_INET6;
	t->proto = ip[6];
	memcpy(t->sip6, ip + 8, 16);
	memcpy(t->dip6, ip + 24, 16);
	t->sip = npuflow_fold6(t->sip6);
	t->dip = npuflow_fold6(t->dip6);
	memcpy(&t->sport, ip + 40, 2);
	memcpy(&t->dport, ip + 42, 2);
	return (1);
}

/* Two tuples name the same direction of the same connection: every field, the IPv6 words too. */
static int
npuflow_tuple_same(const struct npuep_pf_tuple *a, const struct npuep_pf_tuple *b)
{

	if (a->sip != b->sip || a->dip != b->dip || a->sport != b->sport ||
	    a->dport != b->dport || a->proto != b->proto || a->af != b->af)
		return (0);
	return (a->af != AF_INET6 ||
	    (memcmp(a->sip6, b->sip6, sizeof(a->sip6)) == 0 &&
	    memcmp(a->dip6, b->dip6, sizeof(a->dip6)) == 0));
}

/* The tuple's hash, the OCTEON TX driver's octep_conn_hash32: chains and candidate slots. */
static uint32_t
npuflow_hash32(const struct npuep_pf_tuple *t)
{
	uint32_t h;

	h = t->sip ^ ((t->dip << 13) | (t->dip >> 19));
	h ^= ((uint32_t)t->sport << 16) | (uint32_t)t->dport;
	h ^= (uint32_t)t->proto;
	return (h * 0x9e3779b1u);
}

static uint32_t
npuflow_cand_slot(const struct npuep_pf_tuple *t)
{

	return ((npuflow_hash32(t) >> 16) & (NPUFLOW_CAND_MAX - 1));
}

/*
 * Does this packet carry a TCP FIN or RST? The OCTEON TX driver's octep_dp_tcp_closing, for the
 * same reason: the fast path gives a connection back for FIN, RST and its retransmission rule
 * alike, and only the frame tells them apart - a connection that is ending is not revived.
 */
int
npuflow_tcp_closing(const uint8_t *ip, uint32_t len, int v6)
{
	uint32_t off;

	if (v6) {
		if (len < 40 + 14 || (ip[0] >> 4) != 6 || ip[6] != IPPROTO_TCP)
			return (0);
		off = 40;
	} else {
		if (len < 20 || (ip[0] >> 4) != 4 || ip[9] != IPPROTO_TCP)
			return (0);
		off = (uint32_t)(ip[0] & 0x0f) * 4;
		if (off < 20 || len < off + 14)
			return (0);
	}
	return ((ip[off + 13] & 0x05) != 0);	/* FIN 0x01, RST 0x04 */
}

/*
 * From the receive path, for every punted frame whose metadata named a microflow. Called with
 * the datapath's lock held and nothing else; this takes its own lock only, never sleeps, and
 * finds the tuple's slot by its hash - a newer flow that lands on an occupied slot takes it.
 *
 * And if the table holds a connection with this tuple, made a quarter of a second ago or more,
 * this frame is news that cannot wait for the next sweep: a connection in hardware sends the host
 * nothing, so the fast path has let it go, and every frame of it crosses the host until it is
 * revived. The fast task is kicked - once, until it has run.
 */
void
npuflow_cand_push(int port, uint16_t tag, uint32_t id, uint32_t rev,
    const struct npuep_pf_tuple *t, int closing)
{
	struct npuflow_softc *sc = npuflow_sc;
	struct npuep_flow_cand *c;
	int ci, kick = 0;

	if (sc == NULL || id == 0)
		return;
	c = &sc->cand[npuflow_cand_slot(t)];
	FLOW_LOCK(sc);
	if (c->frames != 0 && npuflow_tuple_same(&c->tuple, t)) {
		/* The same connection; the fast path may have given it a new entry since. */
		c->id = id;
		c->rev = rev;
		c->tag = tag;
		c->port = port;
		c->when = ticks;
		c->frames++;
		c->closing = closing ? 1 : 0;
		sc->seen_again++;
	} else {
		if (c->frames != 0)
			sc->replaced++;
		bzero(c, sizeof(*c));
		c->tuple = *t;
		c->id = id;
		c->rev = rev;
		c->tag = tag;
		c->port = port;
		c->when = ticks;
		c->frames = 1;
		c->closing = closing ? 1 : 0;
		sc->pushed++;
	}
	if (sc->autom && !sc->detaching) {
		ci = npuflow_conn_find(sc, t);
		/*
		 * At most one kick every hundredth of a second: each runs the let-go pass over the
		 * whole candidate table under the flow lock this path takes for every punted frame,
		 * and a connection given back and waiting to be revived punts every frame - measured
		 * with a wait in the table, 131,974 passes in 12 seconds and the table held for up to
		 * 201 milliseconds at a time.
		 */
		kick = ci >= 0 && !sc->conn[ci].making &&
		    (int)(ticks - sc->conn[ci].when) >= hz / 4 &&
		    (u_int)(ticks - sc->fast_last) >= (u_int)(hz / 100 > 0 ? hz / 100 : 1);
	}
	FLOW_UNLOCK(sc);
	if (kick && atomic_cmpset_int(&sc->fast_kicked, 0, 1))
		taskqueue_enqueue(sc->tq, &sc->fast_task);
}

static int
npuflow_nhop_for(const struct npuep_pf_tuple *t, const uint32_t *dst6, uint32_t dst, int hint,
    struct npuep_nhop *nh)
{

	if (t->af == AF_INET6)
		return (npuep_nhop_resolve6(dst6, hint, nh));
	return (npuep_nhop_resolve(dst, hint, nh));
}

/*
 * The tables, sized by the board, allocated by the sweep - which holds sc->sx, so nothing makes,
 * takes out or walks a flow meanwhile, and may sleep, so M_WAITOK cannot fail. The pointers are
 * published under the lock the readers take.
 */
static void
npuflow_tables_alloc(struct npuflow_softc *sc)
{
	struct npuflow_conn *conn;
	struct npuflow_nhop_ent *nhop;
	uint32_t *head, *fr, *live, hbits, cap, ncap, i;
	uintmax_t ram_cap;
	size_t per;
	int want, set;

	per = sizeof(*conn) + 3 * sizeof(uint32_t);
	ram_cap = ptoa((uintmax_t)physmem) / NPUFLOW_RAM_SHARE / per;
	want = sc->board_conn != 0 ? (int)MIN(sc->board_conn, NPUFLOW_FLOW_CEIL) :
	    NPUFLOW_FLOW_DEFAULT;
	if (ram_cap < (uintmax_t)want)
		want = (int)ram_cap;
	set = TUNABLE_INT_FETCH("hw.npuep.flow_max", &want);
	if (want < NPUFLOW_FLOW_MIN)
		want = NPUFLOW_FLOW_MIN;
	if (want > NPUFLOW_FLOW_CEIL)
		want = NPUFLOW_FLOW_CEIL;
	cap = (uint32_t)want;
	ncap = sc->board_nhop != 0 ? MIN(sc->board_nhop, NPUFLOW_NHOP_CEIL) : NPUFLOW_NHOP_DEFAULT;
	if (ncap < 128)
		ncap = 128;
	for (hbits = 16; ((uint32_t)1 << hbits) < cap; hbits++)
		;
	conn = malloc((size_t)cap * sizeof(*conn), M_DEVBUF, M_WAITOK | M_ZERO);
	head = malloc(((size_t)1 << hbits) * sizeof(*head), M_DEVBUF, M_WAITOK | M_ZERO);
	fr = malloc((size_t)cap * sizeof(*fr), M_DEVBUF, M_WAITOK);
	live = malloc((size_t)cap * sizeof(*live), M_DEVBUF, M_WAITOK | M_ZERO);
	nhop = malloc((size_t)ncap * sizeof(*nhop), M_DEVBUF, M_WAITOK | M_ZERO);
	/* Popped from the top, so the first index handed out is 2. */
	for (i = 0; i + 2 < cap; i++)
		fr[i] = cap - 1 - i;

	FLOW_LOCK(sc);
	sc->conn = conn;
	sc->conn_cap = cap;
	sc->conn_max = cap;
	sc->conn_head = head;
	sc->conn_hbits = hbits;
	sc->conn_free = fr;
	sc->conn_nfree = cap - 2;
	sc->conn_live = live;
	sc->conn_nlive = 0;
	sc->ka_next = 0;
	sc->nhop = nhop;
	sc->nhop_cap = ncap;
	sc->nhop_hi = 2;
	FLOW_UNLOCK(sc);
	device_printf(sc->dev, "flow table: %u connections, %u chains, %u next hops, %ju bytes - "
	    "%s (the board's %u; a 16th of %ju MB of memory holds %ju)\n", cap, 1u << hbits, ncap,
	    (uintmax_t)((size_t)cap * (sizeof(*conn) + 2 * sizeof(uint32_t)) +
	    ((size_t)1 << hbits) * sizeof(uint32_t) + (size_t)ncap * sizeof(*nhop)),
	    set ? "hw.npuep.flow_max" : (sc->board_conn != 0 ? "this host's own size" :
	    "the board never said; the family's default"), sc->board_conn,
	    ptoa((uintmax_t)physmem) >> 20, ram_cap);
}

static void
npuflow_tables_free(struct npuflow_softc *sc)
{

	free(sc->conn, M_DEVBUF);
	free(sc->conn_head, M_DEVBUF);
	free(sc->conn_free, M_DEVBUF);
	free(sc->conn_live, M_DEVBUF);
	free(sc->nhop, M_DEVBUF);
	sc->conn = NULL;
	sc->nhop = NULL;
	sc->conn_cap = sc->conn_nlive = sc->conn_nfree = sc->nhop_cap = 0;
}

/*
 * From the RPC side, once the platform block has been read: the board's two sizes. The tables are
 * allocated at that size by the next sweep; if they already exist - the block came later than
 * NPUFLOW_BOARD_WAIT - the board's figure only lowers what this host will use.
 */
void
npuflow_board(uint32_t max_conn, uint32_t max_nhop)
{
	struct npuflow_softc *sc = npuflow_sc;

	if (sc == NULL)
		return;
	FLOW_LOCK(sc);
	sc->board_conn = max_conn;
	sc->board_nhop = max_nhop;
	if (sc->conn != NULL && max_conn != 0 && max_conn < sc->conn_max)
		sc->conn_max = max_conn;
	FLOW_UNLOCK(sc);
}

/*
 * The sweep: for every candidate, what pf and the routing table say. It sleeps (pf's lookups
 * take locks, the route lookup enters the net epoch), so it copies the candidate out, asks, and
 * writes the answers back under the lock. The next hop asked for is the frame's destination -
 * the way the frame itself would leave - with the frame's own port as the bridge hint.
 */
static void
npuflow_sweep(void *arg, int pending __unused)
{
	struct npuflow_softc *sc = arg;
	struct npuep_flow_cand c;
	struct npuep_pf_state st;
	struct npuep_nhop nh;
	sbintime_t t0;
	int i, settled, have, err;

	/*
	 * CURVNET_SET, and this task died without it: pf's state hash and the routing table are
	 * reached through VNET variables that resolve against curvnet, and a taskqueue thread has
	 * none set, so the first pf lookup from here faulted - "page fault ... pf_find_state_all,
	 * npuep_pf_state_read, npuflow_sweep", out of this appliance's own textdump, 2026-10-14.
	 * octep_dp.c records the identical fault and the same fix; it is also the lesson of
	 * octep #292. vnet0 is the default vnet the front ports live in.
	 */
	CURVNET_SET(vnet0);
	for (i = 0; i < NPUFLOW_CAND_MAX; i++) {
		FLOW_LOCK(sc);
		if (sc->cand[i].frames != 0 &&
		    (ticks - sc->cand[i].when) > NPUFLOW_CAND_AGE * hz) {
			sc->cand[i].frames = 0;
			sc->aged++;
		}
		c = sc->cand[i];
		FLOW_UNLOCK(sc);
		if (c.frames == 0)
			continue;
		have = npuep_pf_state_read(&c.tuple, &st);
		settled = have ? npuep_pf_settled(&c.tuple) : 0;
		err = npuflow_nhop_for(&c.tuple, c.tuple.dip6, c.tuple.dip, c.port, &nh);
		FLOW_LOCK(sc);
		if (sc->cand[i].frames != 0 && sc->cand[i].id == c.id) {
			sc->cand[i].pf_seen = have ? 1 : 0;
			sc->cand[i].pf_settled = settled ? 1 : 0;
			if (have)
				sc->cand[i].st = st;
			sc->cand[i].nh_err = err;
			sc->cand[i].nh_ok = (err == 0);
			if (err == 0)
				sc->cand[i].nh = nh;
		}
		FLOW_UNLOCK(sc);
	}
	/*
	 * The flow table's own upkeep, under the lock that serialises everything that posts a flow
	 * command - the keepalive here, an automatic make here, a by-hand make from the sysctl. The
	 * keepalive runs whether or not flow.auto is set: a flow made by hand whose state pf has
	 * dropped must be taken out just as much as one the sweep made, or the fast path forwards on
	 * a connection the firewall has finished with. Both run inside curvnet, which the keepalive's
	 * pf lookups and the make's route lookups need as much as the annotate pass above.
	 */
	sx_xlock(&sc->sx);
	t0 = sbinuptime();
	if (sc->conn == NULL && !sc->detaching && (sc->board_conn != 0 ||
	    (ticks - sc->attached_at) > NPUFLOW_BOARD_WAIT * hz))
		npuflow_tables_alloc(sc);
	npuflow_held_sweep(sc);
	npuflow_keepalive(sc);
	if (sc->autom) {
		npuflow_letgo(sc);
		npuflow_automake(sc);
	}
	/* How long everything else that posts a flow command waited behind this pass. */
	sc->held_us_last = (uint64_t)sbttous(sbinuptime() - t0);
	if (sc->held_us_last > sc->held_us_max)
		sc->held_us_max = sc->held_us_last;
	sx_xunlock(&sc->sx);
	CURVNET_RESTORE();
	if (sc->period > 0 && !sc->detaching)
		taskqueue_enqueue_timeout(sc->tq, &sc->task, sc->period);
}

/*
 * The original and reply tuples a connection has, from the pf state - the OCTEON TX driver's
 * octep_conn_tuples. The original is the opener's own addresses to the responder as the wire sees
 * it; the reply is the responder back to the opener's public form. An IPv6 connection is never
 * translated here (a translated one is refused), so its ends in full are the same both ways.
 */
static void
npuflow_tuples(const struct npuep_pf_state *st, const struct npuep_pf_tuple *like,
    struct npuep_pf_tuple tup[2])
{

	tup[0] = *like;
	tup[0].sip = st->orig_src;  tup[0].sport = st->orig_sport;
	tup[0].dip = st->nat_dst;   tup[0].dport = st->nat_dport;
	tup[1] = *like;
	tup[1].sip = st->orig_dst;  tup[1].sport = st->orig_dport;
	tup[1].dip = st->nat_src;   tup[1].dport = st->nat_sport;
	if (like->proto == IPPROTO_ICMP) {
		tup[1].sport = st->nat_sport;
		tup[1].dport = st->orig_dport;
	}
	if (like->af == AF_INET6) {
		memcpy(tup[0].sip6, st->orig_src6, 16);
		memcpy(tup[0].dip6, st->orig_dst6, 16);
		memcpy(tup[1].sip6, st->orig_dst6, 16);
		memcpy(tup[1].dip6, st->orig_src6, 16);
	}
}

/* A candidate whose stored tuple matches, or -1. Caller holds FLOW_LOCK. */
static int
npuflow_find_tuple(struct npuflow_softc *sc, const struct npuep_pf_tuple *t)
{
	int i = (int)npuflow_cand_slot(t);

	if (sc->cand[i].frames != 0 && npuflow_tuple_same(&sc->cand[i].tuple, t))
		return (i);
	return (-1);
}

/* One microflow of a FLOW_CREATE_FP, 32 bytes, the OCTEON TX layout confirmed by hand here. */
static void
npuflow_put_mflow(uint8_t *p, uint32_t id, uint32_t rev, uint32_t dir, uint32_t nhop,
    uint32_t nhop_rev, uint32_t conn_idx, uint32_t conn_rev, uint32_t fw_rev, uint32_t timeout)
{

	le32enc(p + 0, (id & 0x01ffffffu) | ((rev & 0x3fu) << 25) | (1u << 31));
	le32enc(p + 4, ((uint32_t)NPURPC_MFLOW_ACTION_FWD << 16) | ((dir & 1u) << 23) |
	    ((uint32_t)NPURPC_BRCTL_ROUTED << 24) | ((uint32_t)NPURPC_MFLOW_STATE_ACTIVE << 28));
	le32enc(p + 8, 0);				/* l3_fwd_rev_num unused, as octep leaves it */
	le32enc(p + 12, conn_idx);
	/* fw_state_rev_num in the low half (the revision barrier), conn_rev_num in the high half. */
	le32enc(p + 16, (fw_rev & 0xffffu) | ((conn_rev & 0xffffu) << 16));
	le32enc(p + 20, (nhop & 0x00ffffffu) | ((nhop_rev & 0xffu) << 24));
	le32enc(p + 24, 0);				/* sa_rev */
	le32enc(p + 28, timeout);
}

/*
 * The connection block of a FLOW_CREATE_FP, with the translation read off pf's two keys. An IPv6
 * connection carries the folds of its addresses here and no translation flag; the fast path
 * matches its frames by the microflow it made from them, not by this block.
 */
static void
npuflow_req_conn(uint8_t *req, uint32_t ci, uint32_t conn_rev, const struct npuep_pf_state *st)
{
	uint8_t *n;
	uint32_t flags;

	le32enc(req + 0, ci);
	flags = (conn_rev & 0xffffu) |
	    ((uint32_t)NPURPC_CONN_VERDICT_CUT_THRU << 21) |
	    ((uint32_t)NPURPC_CONN_STATE_VALID << 30);
	if (st->nat_snat) flags |= NPURPC_CONN_FLAG_SNAT;
	if (st->nat_dnat) flags |= NPURPC_CONN_FLAG_DNAT;
	le32enc(req + 4, flags);
	n = req + NPURPC_FLOW_OFF_CONN_NAT;
	le32enc(n + 0, st->orig_src);  le32enc(n + 4, st->orig_dst);
	le32enc(n + 8, st->nat_src);   le32enc(n + 12, st->nat_dst);
	le16enc(n + 16, st->orig_dport); le16enc(n + 18, st->orig_sport);
	le16enc(n + 20, st->nat_dport);  le16enc(n + 22, st->nat_sport);
}

/* Program one next-hop table entry (NHOP_PROGRAM, 32 bytes). */
static int
npuflow_nhop_program(uint32_t idx, uint8_t rev, const struct npuep_nhop *nh, uint16_t etype)
{
	uint8_t pl[NPURPC_NHOP_REQ_LEN];
	int rc, n;

	memset(pl, 0, sizeof(pl));
	le32enc(pl + 0, idx);
	pl[4] = 1;			/* is_resolved */
	pl[5] = rev;
	memcpy(pl + 8, nh->dmac, 6);
	memcpy(pl + 14, nh->smac, 6);
	be16enc(pl + 20, etype);
	/* pport_tag at +24 is left 0: the far side fills it from the interface id. */
	pl[26] = NPURPC_NHOP_FLAG_L3;
	pl[27] = nh->iface;
	le16enc(pl + 28, nh->mtu);
	n = npurpc_send(RPC_CMD_NHOP_PROGRAM, pl, sizeof(pl), NULL, 0, &rc);
	if (n < 0)
		return (-n);
	return (rc != 0 ? EIO : 0);
}

/*
 * The connection and next-hop tables. Every quick field access here holds FLOW_LOCK; the sleeping
 * work - the route lookup, NHOP_PROGRAM, FLOW_CREATE_FP, CONN_RECLAIM_FP - is done with the lock
 * dropped, because npurpc_send may sleep waiting for its answer and a thread cannot sleep holding
 * a default mutex. make, takeout and the sweep are serialised against each other by sc->sx
 * instead, which is sleepable, so two of them never touch the tables at once even though each lets
 * the lock go for its commands.
 */

static uint32_t
npuflow_conn_bucket(struct npuflow_softc *sc, const struct npuep_pf_tuple *t)
{

	return (npuflow_hash32(t) >> (32 - sc->conn_hbits));
}

/* A connection whose stored tuple matches t in either direction, or -1. Caller holds FLOW_LOCK. */
static int
npuflow_conn_find(struct npuflow_softc *sc, const struct npuep_pf_tuple *t)
{
	uint32_t e, idx, d;

	if (sc->conn == NULL)
		return (-1);
	for (e = sc->conn_head[npuflow_conn_bucket(sc, t)]; e != 0;
	    e = sc->conn[idx].hnext[d]) {
		idx = (e - 1) >> 1;
		d = (e - 1) & 1;
		if (npuflow_tuple_same(&sc->conn[idx].tuple[d], t))
			return ((int)idx);
	}
	return (-1);
}

static void
npuflow_conn_link(struct npuflow_softc *sc, uint32_t idx)
{
	uint32_t b, d;

	for (d = 0; d < 2; d++) {
		b = npuflow_conn_bucket(sc, &sc->conn[idx].tuple[d]);
		sc->conn[idx].hnext[d] = sc->conn_head[b];
		sc->conn_head[b] = (idx << 1 | d) + 1;
	}
}

static void
npuflow_conn_unlink(struct npuflow_softc *sc, uint32_t idx)
{
	uint32_t b, d, me, e, *pp;

	for (d = 0; d < 2; d++) {
		me = (idx << 1 | d) + 1;
		b = npuflow_conn_bucket(sc, &sc->conn[idx].tuple[d]);
		for (pp = &sc->conn_head[b]; (e = *pp) != 0;
		    pp = &sc->conn[(e - 1) >> 1].hnext[(e - 1) & 1]) {
			if (e == me) {
				*pp = sc->conn[idx].hnext[d];
				break;
			}
		}
		sc->conn[idx].hnext[d] = 0;
	}
}

/* Reserve a connection index for a flow being made. Caller holds FLOW_LOCK. Returns -1 if full. */
static int
npuflow_conn_alloc(struct npuflow_softc *sc, const struct npuep_pf_tuple tup[2], int d,
    const struct npuep_pf_state *st)
{
	struct npuflow_conn *c;
	uint32_t i;

	if (sc->conn == NULL || sc->conn_nfree == 0 || sc->conn_used >= sc->conn_max) {
		sc->flow_full++;
		return (-1);
	}
	i = sc->conn_free[--sc->conn_nfree];
	c = &sc->conn[i];
	c->conn_rev = (uint16_t)(c->conn_rev + 1);
	if (c->conn_rev == 0)
		c->conn_rev = 1;
	c->used = 1;
	c->making = 1;		/* the FLOW_CREATE_FP has not gone out yet */
	c->probe_dir = (uint8_t)d;
	c->pf_dir = st->lookup_dir;
	c->snat = st->nat_snat;
	c->dnat = st->nat_dnat;
	c->tuple[0] = tup[0];
	c->tuple[1] = tup[1];
	c->mf_id[0] = c->mf_id[1] = 0;
	c->mf_rev[0] = c->mf_rev[1] = 0;
	c->nhop[0] = c->nhop[1] = 0;
	c->when = ticks;
	/* this connection's counts start at nothing; hw_raw is the slot's, and stays */
	memset(c->hw_pkts, 0, sizeof(c->hw_pkts));
	memset(c->hw_bytes, 0, sizeof(c->hw_bytes));
	memset(c->hw_pf_pkts, 0, sizeof(c->hw_pf_pkts));
	memset(c->hw_pf_bytes, 0, sizeof(c->hw_pf_bytes));
	c->hw_moved = 0;
	c->hw_seen = 0;
	npuflow_conn_link(sc, i);
	c->live_pos = sc->conn_nlive;
	sc->conn_live[sc->conn_nlive++] = i;
	sc->conn_used++;
	return ((int)i);
}

/* Give a connection index back. Caller holds FLOW_LOCK; the next hops are freed separately. */
static void
npuflow_conn_free(struct npuflow_softc *sc, int idx)
{
	struct npuflow_conn *c;
	uint32_t last;

	if (sc->conn == NULL || idx < 2 || (uint32_t)idx >= sc->conn_cap || !sc->conn[idx].used)
		return;
	c = &sc->conn[idx];
	npuflow_conn_unlink(sc, (uint32_t)idx);
	last = sc->conn_live[--sc->conn_nlive];
	sc->conn_live[c->live_pos] = last;
	sc->conn[last].live_pos = c->live_pos;
	sc->conn_free[sc->conn_nfree++] = (uint32_t)idx;
	c->used = 0;
	c->making = 0;
	if (sc->conn_used > 0)
		sc->conn_used--;
}

/*
 * Find or reserve the next-hop entry for a resolved answer. Caller holds FLOW_LOCK. On a match an
 * existing entry's reference count rises and *fresh is 0 - it is already programmed, nothing to do;
 * otherwise a free slot is taken, its key stored, its revision bumped, and *fresh is 1, meaning the
 * caller must post NHOP_PROGRAM for it (with the lock dropped) and, if that fails, put it back.
 * Returns the index, or -1 if the table is full. The sweep serialises every maker, so a fresh entry
 * is always programmed before another maker's lookup can match it. Only the entries below nhop_hi
 * are read: a table of 65,536 is mostly never used.
 */
static int
npuflow_nhop_get(struct npuflow_softc *sc, const struct npuep_nhop *nh, uint16_t etype,
    uint8_t *rev, int *fresh)
{
	struct npuflow_nhop_ent *e;
	uint32_t i;
	int free_i = -1;

	if (sc->nhop == NULL)
		return (-1);
	for (i = 2; i < sc->nhop_hi; i++) {
		e = &sc->nhop[i];
		if (!e->used) {
			if (free_i < 0)
				free_i = (int)i;
			continue;
		}
		if (e->iface == nh->iface && e->mtu == nh->mtu && e->etype == etype &&
		    memcmp(e->dmac, nh->dmac, 6) == 0 && memcmp(e->smac, nh->smac, 6) == 0) {
			e->refcnt++;
			*rev = e->rev;
			*fresh = 0;
			return ((int)i);
		}
	}
	if (free_i < 0 && sc->nhop_hi < sc->nhop_cap)
		free_i = (int)sc->nhop_hi++;
	if (free_i < 0) {
		sc->nhop_full++;
		return (-1);
	}
	e = &sc->nhop[free_i];
	e->rev = (uint8_t)(e->rev + 1);
	if (e->rev == 0)
		e->rev = 1;
	e->used = 1;
	e->refcnt = 1;
	e->iface = nh->iface;
	e->mtu = nh->mtu;
	e->etype = etype;
	memcpy(e->dmac, nh->dmac, 6);
	memcpy(e->smac, nh->smac, 6);
	sc->nhop_used++;
	*rev = e->rev;
	*fresh = 1;
	return (free_i);
}

/* One flow fewer names this next hop; the slot is freed at the last. Caller holds FLOW_LOCK. */
static void
npuflow_nhop_put(struct npuflow_softc *sc, int idx)
{
	struct npuflow_nhop_ent *e;

	if (sc->nhop == NULL || idx < 2 || (uint32_t)idx >= sc->nhop_cap)
		return;
	e = &sc->nhop[idx];
	if (!e->used)
		return;
	if (e->refcnt > 0)
		e->refcnt--;
	if (e->refcnt == 0) {
		e->used = 0;
		if (sc->nhop_used > 0)
			sc->nhop_used--;
	}
}

/*
 * Take one connection out of the fast path (CONN_RECLAIM_FP), by its index and the revision this
 * host made it under - the OCTEON TX driver's octep_rpc_conn_reclaim. The far side marks it
 * reclaimed, and its microflows then hand every frame to the host again, where pf decides. A
 * revision that no longer matches - the index made again since - is refused there, so a late one
 * cannot take out the connection that followed. Posted on the high ring: nothing reads the answer.
 *
 * Before 2026-10-18 this was two MFLOW_INVALIDATEs of invalidation type 0, by microflow. The far
 * side's handler has no such type - it invalidates by interface, by address or by revision - and
 * refused every one ("invalid mflow invalidate type 0" on the coprocessor's console), so nothing
 * was ever taken out: a connection whose pf state was killed went on through the coprocessor at
 * the line's rate. No lock held.
 */
static void
npuflow_conn_reclaim(uint32_t ci, uint32_t conn_rev)
{
	uint8_t pl[8];

	memset(pl, 0, sizeof(pl));
	le32enc(pl + 0, ci);
	le16enc(pl + 4, (uint16_t)conn_rev);
	(void)npurpc_send_posted(RPC_CMD_CONN_RECLAIM_FP, pl, sizeof(pl));
}

/*
 * Turn one candidate into a flow the fast path forwards itself: resolve both directions' next hops,
 * program them, and send one FLOW_CREATE_FP carrying the connection and both microflows. Called by
 * the by-hand sysctl and by the automatic sweep, both under sc->sx. On any failure every table
 * index reserved is given straight back, so a refusal leaves nothing half-made. sb may be NULL.
 * slot is where the candidate is expected, or -1 to look for it.
 */
static int
npuflow_make(struct npuflow_softc *sc, uint32_t id, int slot, struct sbuf *sb)
{
	struct npuep_flow_cand c, r;
	struct npuep_pf_state st;
	struct npuep_pf_tuple tup[2];
	struct npuep_nhop nho, nhr;
	uint8_t req[NPURPC_FLOW_REQ_LEN];
	uint32_t oid, orev, rid, rrev, conn_rev, fw_rev;
	uint16_t etype;
	uint8_t nho_rev, nhr_rev;
	int i, d, ri, lan_port, err, rc, ci, nio, nir, fresh_o, fresh_r, marked;

	if (!npuep_pf_present()) {
		if (sb != NULL) sbuf_cat(sb, "pf is not loaded\n");
		return (ENXIO);
	}
	/* Find the candidate by its microflow id, and copy it. */
	FLOW_LOCK(sc);
	d = -1;
	if (slot >= 0 && slot < NPUFLOW_CAND_MAX && sc->cand[slot].frames != 0 &&
	    sc->cand[slot].id == id)
		d = slot;
	for (i = 0; d < 0 && i < NPUFLOW_CAND_MAX; i++)
		if (sc->cand[i].frames != 0 && sc->cand[i].id == id)
			d = i;
	if (d < 0) {
		FLOW_UNLOCK(sc);
		if (sb != NULL) sbuf_printf(sb, "no candidate with id %u\n", id);
		return (ENOENT);
	}
	c = sc->cand[d];
	FLOW_UNLOCK(sc);

	if (!npuep_pf_state_read(&c.tuple, &st) || !npuep_pf_settled(&c.tuple)) {
		if (sb != NULL) sbuf_cat(sb, "pf has no settled state for that flow\n");
		return (EAGAIN);
	}
	/*
	 * An IPv6 connection that pf translates is left with the host: the connection block carries
	 * IPv4 addresses for the translation, and NPTv6 or NAT66 has not been measured on this
	 * coprocessor. An untranslated one - every IPv6 connection of a routed network - is made.
	 */
	if (c.tuple.af == AF_INET6 && st.nat_valid) {
		sc->v6_refused++;
		if (sb != NULL) sbuf_cat(sb, "an IPv6 connection pf translates stays with the host\n");
		return (EACCES);
	}
	etype = (c.tuple.af == AF_INET6) ? ETHERTYPE_IPV6 : ETHERTYPE_IP;
	npuflow_tuples(&st, &c.tuple, tup);
	d = st.original ? 0 : 1;	/* which direction the found candidate is */
	/*
	 * The owner's choice, on both directions now that pf has said what they are: the reply
	 * direction of a translated connection carries the outside addresses, so an excluded inside
	 * host is found on the original one.
	 */
	if (npuflow_kept(sc, tup, 2)) {
		sc->excluded++;
		if (sb != NULL)
			sbuf_cat(sb, "not accelerated: the owner keeps this connection with the host\n");
		return (EPERM);
	}

	/* The reply direction's candidate - its microflow id is needed to name both directions. */
	FLOW_LOCK(sc);
	ri = npuflow_find_tuple(sc, &tup[d ^ 1]);
	if (ri >= 0)
		r = sc->cand[ri];
	FLOW_UNLOCK(sc);
	if (ri < 0) {
		if (sb != NULL) sbuf_cat(sb, "the reply direction has not been seen yet - send a "
		    "little traffic back the other way and try again\n");
		return (EAGAIN);
	}

	/* The original direction's id is whichever candidate is direction 0. */
	oid = (d == 0) ? c.id : r.id;  orev = (d == 0) ? c.rev : r.rev;
	rid = (d == 0) ? r.id : c.id;  rrev = (d == 0) ? r.rev : c.rev;
	lan_port = (d == 0) ? c.port : r.port;	/* the port the original flow entered on */

	/* Both next hops, resolved with the lock dropped: the original out toward the destination,
	 * the reply back to the opener by the flow's own port as the bridge hint. */
	err = npuflow_nhop_for(&tup[0], tup[0].dip6, tup[0].dip, -1, &nho);
	if (err != 0) {
		if (sb != NULL) sbuf_printf(sb, "the original direction's next hop did not resolve (%d)\n", err);
		return (err);
	}
	err = npuflow_nhop_for(&tup[0], st.orig_src6, st.orig_src, lan_port, &nhr);
	if (err != 0) {
		if (sb != NULL) sbuf_printf(sb, "the reply direction's next hop did not resolve (%d) - "
		    "the opener may have left the neighbour cache\n", err);
		return (err);
	}

	/* Reserve a connection and two next-hop indices in one hold of the lock. */
	FLOW_LOCK(sc);
	if (npuflow_conn_find(sc, &tup[0]) >= 0) {
		FLOW_UNLOCK(sc);
		if (sb != NULL) sbuf_cat(sb, "that connection is already accelerated\n");
		return (EEXIST);
	}
	ci = npuflow_conn_alloc(sc, tup, d, &st);
	if (ci < 0) {
		FLOW_UNLOCK(sc);
		if (sb != NULL) sbuf_cat(sb, sc->conn == NULL ? "the flow table is not allocated "
		    "yet - the board's platform block has not been read\n" : "the flow table is full\n");
		return (ENOSPC);
	}
	nio = npuflow_nhop_get(sc, &nho, etype, &nho_rev, &fresh_o);
	nir = (nio >= 0) ? npuflow_nhop_get(sc, &nhr, etype, &nhr_rev, &fresh_r) : -1;
	conn_rev = sc->conn[ci].conn_rev;
	fw_rev = sc->fw_rev;		/* the revision this flow is stamped with, for the barrier */
	if (nio < 0 || nir < 0) {
		if (nio >= 0)
			npuflow_nhop_put(sc, nio);
		npuflow_conn_free(sc, ci);
		FLOW_UNLOCK(sc);
		if (sb != NULL) sbuf_cat(sb, "the next-hop table is full\n");
		return (ENOSPC);
	}
	FLOW_UNLOCK(sc);

	/* Program only what we freshly reserved; a shared next hop is already in hardware. */
	err = 0;
	if (fresh_o)
		err = npuflow_nhop_program(nio, nho_rev, &nho, etype);
	if (err == 0 && fresh_r)
		err = npuflow_nhop_program(nir, nhr_rev, &nhr, etype);
	if (err != 0) {
		FLOW_LOCK(sc);
		npuflow_nhop_put(sc, nio);
		npuflow_nhop_put(sc, nir);
		npuflow_conn_free(sc, ci);
		FLOW_UNLOCK(sc);
		if (sb != NULL) sbuf_printf(sb, "NHOP_PROGRAM failed (%d)\n", err);
		return (err);
	}

	/* The connection (npuflow_req_conn). */
	memset(req, 0, sizeof(req));
	npuflow_req_conn(req, (uint32_t)ci, conn_rev, &st);

	/* The two microflows: original on next hop nio, reply on next hop nir, both active, forwarding. */
	le32enc(req + NPURPC_FLOW_OFF_VALID, NPURPC_FLOW_VALID_ORIG | NPURPC_FLOW_VALID_REPLY);
	npuflow_put_mflow(req + NPURPC_FLOW_OFF_MFLOW_O, oid, orev, 0, (uint32_t)nio, nho_rev,
	    (uint32_t)ci, conn_rev, fw_rev, NPUFLOW_MFLOW_TO);
	npuflow_put_mflow(req + NPURPC_FLOW_OFF_MFLOW_R, rid, rrev, 1, (uint32_t)nir, nhr_rev,
	    (uint32_t)ci, conn_rev, fw_rev, NPUFLOW_MFLOW_TO);

	i = npurpc_send(RPC_CMD_FLOW_CREATE_FP, req, sizeof(req), NULL, 0, &rc);
	if (i < 0 || rc != 0) {
		FLOW_LOCK(sc);
		npuflow_nhop_put(sc, nio);
		npuflow_nhop_put(sc, nir);
		npuflow_conn_free(sc, ci);
		FLOW_UNLOCK(sc);
		if (sb != NULL) {
			if (i < 0) sbuf_printf(sb, "FLOW_CREATE_FP transport failed (%d)\n", -i);
			else sbuf_printf(sb, "the coprocessor refused FLOW_CREATE_FP, rc 0x%x\n", rc);
		}
		return (i < 0 ? -i : EIO);
	}

	/* Commit: the flow is live. */
	FLOW_LOCK(sc);
	sc->conn[ci].mf_id[0] = oid;  sc->conn[ci].mf_rev[0] = orev;
	sc->conn[ci].mf_id[1] = rid;  sc->conn[ci].mf_rev[1] = rrev;
	sc->conn[ci].nhop[0] = (uint32_t)nio;
	sc->conn[ci].nhop[1] = (uint32_t)nir;
	sc->conn[ci].when = ticks;
	sc->conn[ci].making = 0;
	FLOW_UNLOCK(sc);
	sc->made++;

	/*
	 * From here on pf sees none of this connection's frames, so it must stop judging their
	 * sequence numbers (npuep_pf_mark_sloppy): the first frame to come back to it - handed back
	 * with flow.release, given back by the fast path, or a FIN at the end - would otherwise be
	 * measured against a window pf never saw advance and dropped as a bad state. From both
	 * tuples, because a translated connection's two states share no key arrangement. The OCTEON
	 * TX driver has done this since #287 (its dp.pf_sloppy); this one had the function and never
	 * called it, and until 2026-10-18 a connection handed back to the host stopped dead there.
	 */
	marked = sc->pf_sloppy ? npuep_pf_mark_sloppy(&tup[0]) + npuep_pf_mark_sloppy(&tup[1]) : 0;
	sc->sloppy_marked += (uint64_t)marked;
	if (sb != NULL)
		sbuf_printf(sb, "accelerated connection %d (IPv%d): original mflow %u out next hop %u "
		    "(iface %u %02x:%02x:%02x:%02x:%02x:%02x), reply mflow %u out next hop %u; %s%s%s; "
		    "%d pf state(s) marked sloppy\n",
		    ci, c.tuple.af == AF_INET6 ? 6 : 4, oid, nio, nho.iface, nho.dmac[0], nho.dmac[1],
		    nho.dmac[2], nho.dmac[3], nho.dmac[4], nho.dmac[5], rid, nir,
		    st.nat_snat ? "snat" : "", st.nat_snat && st.nat_dnat ? "+" : "",
		    st.nat_dnat ? "dnat" : "", marked);
	return (0);
}

/*
 * Take one flow back: give its table indices up and reclaim the connection from the fast path. The
 * slot is freed first, under the lock, so nothing reuses it before the command goes out; that goes
 * out with the lock dropped, under the revision the connection was made with. Called under sc->sx.
 */
static void
npuflow_takeout(struct npuflow_softc *sc, int ci)
{

	npuflow_takeout_how(sc, ci, 1);
}

/*
 * invalidate 0 frees the indices and sends nothing: for a connection the fast path has already
 * given back, which a remake follows at once.
 */
static void
npuflow_takeout_how(struct npuflow_softc *sc, int ci, int invalidate)
{
	uint32_t rev;

	FLOW_LOCK(sc);
	if (sc->conn == NULL || ci < 2 || (uint32_t)ci >= sc->conn_cap || !sc->conn[ci].used ||
	    sc->conn[ci].making) {
		FLOW_UNLOCK(sc);
		return;
	}
	rev = sc->conn[ci].conn_rev;
	npuflow_nhop_put(sc, (int)sc->conn[ci].nhop[0]);
	npuflow_nhop_put(sc, (int)sc->conn[ci].nhop[1]);
	npuflow_conn_free(sc, ci);
	FLOW_UNLOCK(sc);

	if (invalidate)
		npuflow_conn_reclaim((uint32_t)ci, rev);
	sc->taken++;
}

/*
 * Take every live flow back. Caller holds sc->sx. Returns how many. The live list is walked from
 * its end: a takeout moves the last entry into the freed place, which the walk has already passed.
 */
static int
npuflow_takeout_all(struct npuflow_softc *sc)
{
	int i, k, live, n = 0;

	for (k = (int)sc->conn_nlive - 1; k >= 0; k--) {
		FLOW_LOCK(sc);
		if ((uint32_t)k >= sc->conn_nlive) {
			FLOW_UNLOCK(sc);
			continue;
		}
		i = (int)sc->conn_live[k];
		live = sc->conn[i].used && !sc->conn[i].making;
		FLOW_UNLOCK(sc);
		if (live) {
			npuflow_takeout(sc, i);
			n++;
		}
	}
	return (n);
}

/*
 * Drop the driver's record of every flow without sending anything: after FW_STATE_REV_SET the far
 * side has already voided its whole microflow table, so these connections no longer exist there -
 * reclaiming them again would be a command per flow for nothing. Only the local indices are
 * freed. Caller holds sc->sx.
 */
static void
npuflow_forget_all(struct npuflow_softc *sc)
{
	int i;

	FLOW_LOCK(sc);
	while (sc->conn_nlive > 0) {
		i = (int)sc->conn_live[sc->conn_nlive - 1];
		npuflow_nhop_put(sc, (int)sc->conn[i].nhop[0]);
		npuflow_nhop_put(sc, (int)sc->conn[i].nhop[1]);
		npuflow_conn_free(sc, i);
	}
	FLOW_UNLOCK(sc);
}

/*
 * The revision barrier. Raise the firewall revision and post FW_STATE_REV_SET, which the far side
 * answers by voiding its entire microflow table - so a ruleset reload discards every offloaded flow
 * at once, and nothing forwards under rules that no longer exist. On success the driver forgets its
 * own table (the far side has dropped it) and the sweep remakes what the new rules still allow,
 * stamped with the new revision; on a refusal the revision is put back, which only means new flows
 * are refused until it takes, never that stale ones are kept. Caller holds sc->sx.
 */
static void
npuflow_fw_rev_bump(struct npuflow_softc *sc)
{
	uint8_t pl[2];
	uint16_t prev;
	int n, rc;

	prev = sc->fw_rev;
	sc->fw_rev = (uint16_t)((prev + 1) & 0xffffu);
	le16enc(pl, sc->fw_rev);
	n = npurpc_send(RPC_CMD_FW_STATE_REV_SET, pl, sizeof(pl), NULL, 0, &rc);
	if (n >= 0 && rc == 0) {
		npuflow_forget_all(sc);
		sc->fw_rev_bumps++;
	} else {
		sc->fw_rev = prev;
		sc->fw_rev_bump_fail++;
	}
}

/*
 * One direction of a count as the coprocessor last sent it: what moved since the message before.
 *
 * The coprocessor sends running totals in widths of its own - 35 bits of bytes and 29 of packets -
 * so the difference from the last message is taken in those widths, and a total that wrapped is
 * still the right difference. raw is the SLOT's, kept across the connections that use it and moved
 * by every message that names the slot, whoever it is for; only what moved is given to the
 * connection. The OCTEON TX driver's octep_hw_diff, after its lesson (issue #515): taking a new
 * connection's first message as all new gave a reused slot the history of every connection before.
 */
static int
npuflow_hw_diff(uint32_t raw[2], uint32_t lo, uint32_t olp, uint64_t *pkts, uint64_t *bytes)
{
	uint64_t b0, b1;
	uint32_t pk;

	b1 = ((uint64_t)(olp & ((1u << NPUFLOW_HW_PKT_SHIFT) - 1)) << 32) | lo;
	b0 = ((uint64_t)(raw[1] & ((1u << NPUFLOW_HW_PKT_SHIFT) - 1)) << 32) | raw[0];
	b1 = (b1 - b0) & ((1ull << NPUFLOW_HW_BYTE_BITS) - 1);
	pk = ((olp >> NPUFLOW_HW_PKT_SHIFT) - (raw[1] >> NPUFLOW_HW_PKT_SHIFT)) & NPUFLOW_HW_PKT_MASK;
	raw[0] = lo;
	raw[1] = olp;
	*bytes = b1;
	*pkts = pk;
	return (pk != 0 || b1 != 0);
}

/*
 * The coprocessor's count of one port (type 0, LIF_STATS). A front port's own LIF is its iface_id
 * shifted left by twelve, VLAN 0 (npuep.h, RPC_LIF_IFACE_SHIFT): measured, a stream in on Port3 and
 * out of Port8 was counted for 8192 and 28672, the iface_ids 2 and 7 of npuep_front_ports. Not a
 * running total: each message carries what moved since the one before, as on the XGS 3300. The
 * first direction is what came in on the port, the second what left by it; and only what the
 * coprocessor forwarded itself, which the interface's own counters never see - so flow.lif_apply
 * adds it there. Caller holds FLOW_LOCK; the port lookups take no lock.
 */
static void
npuflow_lif_count(struct npuflow_softc *sc, uint32_t lif, const uint8_t *p)
{
	uint64_t pk[2], by[2];
	uint32_t olp;
	if_t ifp;
	int d, n = -1, port;

	if ((lif & 0xfff) == 0)
		for (port = 0; port < npugiu_port_count(); port++)
			if (npugiu_port_iface(port) == (lif >> RPC_LIF_IFACE_SHIFT)) {
				n = port;
				break;
			}
	if (n < 0 || n >= NPUEP_NFRONT) {
		sc->lif_unknown++;
		return;
	}
	for (d = 0; d < 2; d++) {
		olp = le32dec(p + 8 * d + 4);
		pk[d] = olp >> NPUFLOW_HW_PKT_SHIFT;
		by[d] = ((uint64_t)(olp & ((1u << NPUFLOW_HW_PKT_SHIFT) - 1)) << 32) |
		    le32dec(p + 8 * d);
	}
	/*
	 * What came in is counted with each frame's FCS, four bytes a frame more than an interface
	 * counts it; what went out is counted as an interface counts it. Measured on 2026-10-19: on F1,
	 * 2,070,425,030 bytes in for 1,363,925 frames of 1,514 bytes - 1,517.97 a frame - and on Port8
	 * 64 bytes in for each 60-byte acknowledgement, while the same frames leaving Port8 and F1 were
	 * counted at 1,514 and 54. The XGS 3300's coprocessor counts both ways without the FCS.
	 */
	if (by[0] >= 4 * pk[0])
		by[0] -= 4 * pk[0];
	for (d = 0; d < 2; d++) {
		sc->lif_pkts[n][d] += pk[d];
		sc->lif_bytes[n][d] += by[d];
	}
	sc->lif_seen[n] = 1;
	ifp = npugiu_port_ifp(n);
	if (sc->lif_apply != 1 || ifp == NULL)
		return;
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, pk[0]);
	if_inc_counter(ifp, IFCOUNTER_IBYTES, by[0]);
	if_inc_counter(ifp, IFCOUNTER_OPACKETS, pk[1]);
	if_inc_counter(ifp, IFCOUNTER_OBYTES, by[1]);
	sc->lif_applied++;
}

/*
 * A control message from the coprocessor, from the receive path (npugiu.c) for a frame on tag 254,
 * which no port claims - freed unread until 2026-10-19. Each type is counted; the connection counts
 * (type 1) are applied to this table and the port counts (type 0) to the ports.
 *
 * The coprocessor counts every connection it forwards - the packets and bytes this host never sees
 * once a connection is in hardware - and sends, unasked, about seven times a second while anything
 * is in hardware, an entry for each connection that carried anything. An entry whose revision is
 * not the connection's own names one that has gone: its slot's totals still move
 * (npuflow_hw_diff), and it is counted and given to nobody. The revision is the conn_rev this
 * driver sent with the connection (npuflow_req_conn), which it raises each time it reserves the
 * slot again; measured, the messages' revision moved during a stream - 1 to 2, 3 to 5 - so it is
 * taken from each message, never assumed.
 */
void
npuflow_cmsg_rx(const uint8_t *f, uint32_t flen)
{
	struct npuflow_softc *sc = npuflow_sc;
	struct npuflow_conn *c;
	const uint8_t *p, *end;
	uint64_t pk[2], by[2];
	uint32_t nb, i, k, hdr, idx, cnt, ne, seen;
	uint16_t rev;
	uint8_t type;
	int d, moved;

	if (sc == NULL || flen < ETHER_HDR_LEN + 8 || be16dec(f + 12) != NPUFLOW_CMSG_ETHERTYPE)
		return;
	p = f + ETHER_HDR_LEN;
	end = f + flen;
	type = p[2];
	FLOW_LOCK(sc);
	if (type < NPUFLOW_CMSG_TYPES)
		sc->cmsg_rx[type]++;
	if ((type != NPUFLOW_CMSG_CONN_STATS && type != NPUFLOW_CMSG_LIF_STATS) ||
	    p[3] != NPUFLOW_CMSG_VERSION) {
		FLOW_UNLOCK(sc);
		return;
	}
	ne = le16dec(p + 4);
	nb = p[6];
	p += 8;
	seen = 0;
	for (i = 0; i < nb; i++) {
		if (end - p < 4)
			goto short_msg;
		hdr = le32dec(p);
		p += 4;
		idx = hdr & 0x03ffffffu;
		cnt = (hdr >> 26) + 1;
		if (type == NPUFLOW_CMSG_LIF_STATS) {
			if ((size_t)(end - p) < (size_t)cnt * NPUFLOW_LIF_ENTRY_LEN)
				goto short_msg;
			for (k = 0; k < cnt; k++, p += NPUFLOW_LIF_ENTRY_LEN) {
				seen++;
				npuflow_lif_count(sc, idx + k, p);
			}
			continue;
		}
		if ((size_t)(end - p) < (size_t)cnt * NPUFLOW_HW_ENTRY_LEN)
			goto short_msg;
		for (k = 0; k < cnt; k++, p += NPUFLOW_HW_ENTRY_LEN) {
			seen++;
			rev = le16dec(p + 16);
			if (sc->conn == NULL || idx + k >= sc->conn_cap) {
				sc->hw_stale++;
				continue;
			}
			/* the slot's totals move whoever they are for: see npuflow_hw_diff */
			c = &sc->conn[idx + k];
			moved = 0;
			for (d = 0; d < 2; d++)
				moved |= npuflow_hw_diff(c->hw_raw[d], le32dec(p + 8 * d),
				    le32dec(p + 8 * d + 4), &pk[d], &by[d]);
			if (!c->used || c->conn_rev != rev) {
				sc->hw_stale++;
				continue;
			}
			for (d = 0; d < 2; d++) {
				c->hw_pkts[d] += pk[d];
				c->hw_bytes[d] += by[d];
			}
			c->hw_seen = 1;
			if (moved)
				c->hw_moved = time_uptime;
			sc->hw_applied++;
		}
	}
	if (seen != ne)
		sc->hw_bad++;
	FLOW_UNLOCK(sc);
	return;
short_msg:
	sc->hw_bad++;
	FLOW_UNLOCK(sc);
}

static int
npuflow_sysctl_cmsg_rx(SYSCTL_HANDLER_ARGS)
{
	struct npuflow_softc *sc = arg1;
	uint64_t n[NPUFLOW_CMSG_TYPES], ap, st, bad;
	struct sbuf *sb;
	int error, t;

	FLOW_LOCK(sc);
	memcpy(n, sc->cmsg_rx, sizeof(n));
	ap = sc->hw_applied;
	st = sc->hw_stale;
	bad = sc->hw_bad;
	FLOW_UNLOCK(sc);

	sb = sbuf_new_for_sysctl(NULL, NULL, 256, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);
	sbuf_cat(sb, "by type:");
	for (t = 0; t < NPUFLOW_CMSG_TYPES; t++)
		if (n[t] != 0)
			sbuf_printf(sb, " %d: %ju", t, (uintmax_t)n[t]);
	sbuf_printf(sb, "\nconnection counts: %ju applied, %ju for a connection gone, %ju messages "
	    "not read to their end\n", (uintmax_t)ap, (uintmax_t)st, (uintmax_t)bad);
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/* The coprocessor's count of each front port (type 0), as npuflow_lif_count keeps it. */
static int
npuflow_sysctl_lif_counted(SYSCTL_HANDLER_ARGS)
{
	struct npuflow_softc *sc = arg1;
	struct sbuf *sb;
	uint64_t pk[2], by[2];
	if_t ifp;
	int error, n, seen;

	sb = sbuf_new_for_sysctl(NULL, NULL, 1024, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);
	for (n = 0; n < NPUEP_NFRONT; n++) {
		FLOW_LOCK(sc);
		seen = sc->lif_seen[n];
		memcpy(pk, sc->lif_pkts[n], sizeof(pk));
		memcpy(by, sc->lif_bytes[n], sizeof(by));
		FLOW_UNLOCK(sc);
		ifp = npugiu_port_ifp(n);
		if (seen)
			sbuf_printf(sb, "%s (%s)  in %ju packets %ju bytes  out %ju packets %ju bytes\n",
			    ifp != NULL ? if_name(ifp) : "-", npuep_front_ports[n].label,
			    (uintmax_t)pk[0], (uintmax_t)by[0], (uintmax_t)pk[1], (uintmax_t)by[1]);
	}
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/*
 * The keepalive: a flow lives only while pf still holds its state. A connection in hardware sends
 * the host nothing, so without this a flow would forward sixty seconds past the firewall finishing
 * with it, and a reused five-tuple inside that window would go on the strength of a connection that
 * is gone. pf is asked the cheap way, by the tuple and the list that found the state. Under sc->sx,
 * inside curvnet. A pure-hardware TCP flow does not idle out of pf (its established timer is hours),
 * so this does not churn a steady download; a short-lived UDP flow it may, and that is correct.
 *
 * At most NPUFLOW_KA_PER_SWEEP connections a sweep, going on from where the last one stopped: with
 * a full table a connection pf has dropped is found within a minute and a half, not a second.
 */
/*
 * What the coprocessor counted for a connection since it was last given to pf, given now when
 * flow.hw_to_pf asks: pf sees none of these packets, so its state - and the States page, and the
 * rule's counters - would otherwise stop at the moment the flow was made. From the keepalive, every
 * sweep, and from the let-go pass just before it takes a connection out to make it again: the
 * counts since the last sweep belong to the connection being taken out and were dropped with it
 * until 2026-10-19 - measured, 117,531 of 1,377,761 packets of a stream whose connection the fast
 * path gave back and the driver made again 73 times. Under sc->sx, inside curvnet.
 */
static void
npuflow_give_pf(struct npuflow_softc *sc, int ci)
{
	struct npuep_pf_tuple tup[2];
	uint64_t gp[2], gb[2];
	uint16_t crev;
	int d, ns, give = 0;

	FLOW_LOCK(sc);
	if (sc->hw_to_pf != 1 || sc->conn == NULL || ci < 2 || (uint32_t)ci >= sc->conn_cap ||
	    !sc->conn[ci].used) {
		FLOW_UNLOCK(sc);
		return;
	}
	tup[0] = sc->conn[ci].tuple[0];
	tup[1] = sc->conn[ci].tuple[1];
	crev = sc->conn[ci].conn_rev;
	for (d = 0; d < 2; d++) {
		gp[d] = sc->conn[ci].hw_pkts[d] - sc->conn[ci].hw_pf_pkts[d];
		gb[d] = sc->conn[ci].hw_bytes[d] - sc->conn[ci].hw_pf_bytes[d];
		if (gp[d] != 0 || gb[d] != 0)
			give = 1;
	}
	FLOW_UNLOCK(sc);
	if (!give || (ns = npuep_pf_count(tup, gp, gb)) <= 0)
		return;
	FLOW_LOCK(sc);
	if (sc->conn[ci].used && sc->conn[ci].conn_rev == crev) {
		for (d = 0; d < 2; d++) {
			sc->conn[ci].hw_pf_pkts[d] += gp[d];
			sc->conn[ci].hw_pf_bytes[d] += gb[d];
		}
	}
	sc->hw_pf_conns++;
	sc->hw_pf_states += (uint64_t)ns;
	FLOW_UNLOCK(sc);
}

static void
npuflow_keepalive(struct npuflow_softc *sc)
{
	struct npuep_pf_tuple t;
	int i, pf_dir, gone, budget, kept = 0;
	uint32_t k;

	if (!npuep_pf_present() || sc->conn == NULL)
		return;
	for (budget = NPUFLOW_KA_PER_SWEEP; budget > 0; budget--) {
		FLOW_LOCK(sc);
		if (sc->conn_nlive == 0 || (uint32_t)kept >= sc->conn_nlive) {
			FLOW_UNLOCK(sc);
			break;		/* every connection asked once this sweep */
		}
		k = sc->ka_next;
		if (k >= sc->conn_nlive)
			k = 0;
		sc->ka_next = k + 1;
		i = (int)sc->conn_live[k];
		if (!sc->conn[i].used || sc->conn[i].making) {
			FLOW_UNLOCK(sc);
			kept++;
			continue;
		}
		t = sc->conn[i].tuple[sc->conn[i].probe_dir];
		pf_dir = sc->conn[i].pf_dir;
		FLOW_UNLOCK(sc);
		gone = !npuep_pf_state_exists(&t, pf_dir, NULL);
		if (!gone) {
			/* and what the coprocessor counted for it since the last visit, to pf */
			npuflow_give_pf(sc, i);
			kept++;
			continue;
		}
		npuflow_takeout(sc, i);
		sc->auto_gone++;
		/*
		 * The takeout moved the last live connection into place k: it is asked next, not
		 * passed over. Until 2026-10-18 it was passed over, and the walk stopped once it had
		 * asked as many as were still live - so a thousand connections pf dropped at once left
		 * the fast path by halves, a sweep each, the last of them ten seconds later.
		 */
		FLOW_LOCK(sc);
		sc->ka_next = k;
		FLOW_UNLOCK(sc);
	}
}

/*
 * The automatic make: every candidate the last sweep found settled and resolved, and that is not
 * already a flow, is handed to the fast path - up to flow.make_per_sweep a sweep, the rest next
 * time. make itself re-reads pf and re-resolves, so a candidate that has gone stale between the
 * sweep and here is refused there and simply not counted. Once a flow is made its frames stop
 * being punted, so the candidate ages out and is not tried again. Under sc->sx, inside curvnet.
 */
static void
npuflow_automake(struct npuflow_softc *sc)
{
	int i, nids = 0, max = sc->per_sweep;

	if (sc->conn == NULL)
		return;
	if (max < 1)
		max = 1;
	if (max > NPUFLOW_CAND_MAX)
		max = NPUFLOW_CAND_MAX;
	FLOW_LOCK(sc);
	for (i = 0; i < NPUFLOW_CAND_MAX && nids < max; i++) {
		struct npuep_flow_cand *c = &sc->cand[i];

		if (c->frames == 0 || !c->pf_settled || !c->nh_ok)
			continue;
		/* In the table already: npuflow_letgo's, if the fast path has let it go. */
		if (npuflow_conn_find(sc, &c->tuple) >= 0)
			continue;
		/* Given back soon after it was made: with the host until its wait is over. */
		if (c->hold_until != 0 && (int)(ticks - c->hold_until) < 0)
			continue;
		if (npuflow_kept_locked(sc, &c->tuple, 1)) {
			sc->excluded++;	/* the owner keeps it with the host: an attempt, counted */
			continue;
		}
		sc->mk_id[nids] = c->id;
		sc->mk_slot[nids] = (uint16_t)i;
		nids++;
	}
	FLOW_UNLOCK(sc);
	for (i = 0; i < nids; i++)
		if (npuflow_make(sc, sc->mk_id[i], sc->mk_slot[i], NULL) == 0) {
			sc->auto_made++;
			FLOW_LOCK(sc);
			sc->cand[sc->mk_slot[i]].mk_tick = ticks != 0 ? ticks : 1;
			FLOW_UNLOCK(sc);
		}
}

#define	NPUFLOW_REVIVE_WAIT_MIN	(hz / 100 > 0 ? hz / 100 : 1)
#define	NPUFLOW_REVIVE_WAIT_MAX	(hz / 2)
#define	NPUFLOW_REVIVE_STREAK	4
/*
 * A connection the fast path gives back within NPUFLOW_REMAKE_CALM of being made stays with the host
 * for a wait before it is made again - a second, doubling each time, to a minute: npuflow_letgo.
 */
#define	NPUFLOW_REMAKE_WAIT_MIN	hz
#define	NPUFLOW_REMAKE_WAIT_MAX	(64 * hz)
#define	NPUFLOW_REMAKE_CALM	(NPUFLOW_CAND_AGE * hz)

/*
 * REVIVED IN PLACE, the OCTEON TX driver's octep_conn_probe on this family. The fast path's
 * FLOW_CREATE_FP copies the request's connection entry over the table's without looking at what is
 * there, so the same command this driver makes a connection with, sent again for the same index and
 * the same revision with no microflow in it, puts a connection the fast path gave back to VALID, and
 * the microflows still pointing at it forward the next frame. One command, and the pf state read for
 * the translation. Under sc->sx.
 */
static int
npuflow_revive(struct npuflow_softc *sc, int ci)
{
	struct npuep_pf_tuple t0;
	struct npuep_pf_state st;
	uint8_t req[NPURPC_FLOW_REQ_LEN];
	uint32_t conn_rev;
	int n, since;

	FLOW_LOCK(sc);
	if (sc->conn == NULL || ci < 2 || (uint32_t)ci >= sc->conn_cap || !sc->conn[ci].used ||
	    sc->conn[ci].making) {
		FLOW_UNLOCK(sc);
		return (ENOENT);
	}
	t0 = sc->conn[ci].tuple[0];
	conn_rev = sc->conn[ci].conn_rev;
	FLOW_UNLOCK(sc);
	if (!npuep_pf_state_read(&t0, &st))
		return (ENOENT);
	memset(req, 0, sizeof(req));
	npuflow_req_conn(req, (uint32_t)ci, conn_rev, &st);
	le32enc(req + NPURPC_FLOW_OFF_VALID, 0);	/* the connection alone, no microflow */
	/*
	 * Posted on the high ring, as the XGS 3300 posts its flow writes: a give-back is answered
	 * at once and the next one is not held behind its wait. A refusal is not seen - the frames
	 * that keep reaching the host are the sign, and the streak takes the connection out.
	 */
	n = npurpc_send_posted(RPC_CMD_FLOW_CREATE_FP, req, sizeof(req));
	if (n < 0)
		return (-n);
	FLOW_LOCK(sc);
	if (sc->conn[ci].used && sc->conn[ci].conn_rev == conn_rev) {
		struct npuflow_conn *c = &sc->conn[ci];

		since = ticks - c->rv_tick;
		if (c->rv_tick != 0 && since < hz) {
			c->rv_wait = imin(imax(c->rv_wait * 2, NPUFLOW_REVIVE_WAIT_MIN),
			    NPUFLOW_REVIVE_WAIT_MAX);
			c->rv_streak++;
		} else {
			c->rv_wait = NPUFLOW_REVIVE_WAIT_MIN;
			c->rv_streak = 0;
		}
		c->rv_tick = ticks;
		c->when = ticks;
	}
	FLOW_UNLOCK(sc);
	return (0);
}

/*
 * The connections the fast path has let go, from the sweep and from the receive path's kick. A
 * frame of one punted a quarter of a second or more after it was made, or last revived, means the
 * fast path let it go: given it back - its retransmission rule, measured 2026-10-18 at a few times
 * a minute on a download through the XGS 136, every frame then CONN_RECLAIM_PENDING - or timed a
 * microflow out and made a new one, inactive, under another identity. Before 2026-10-18 the table
 * still said hardware and never made it again: the download crossed the host from its third
 * second to its last.
 *
 * - Under the identity this driver loaded, and not ending: revived in place (npuflow_revive),
 *   waiting a little longer each time the give-backs keep coming, as the OCTEON TX driver does.
 * - Under a new identity, or revived four times in four seconds and given back again: taken out
 *   without a command - the fast path has given it back already - and made again from the
 *   identities its frames now carry - at once if it had been in hardware a while, and if it was
 *   given back soon after it was made, only once a wait is over, doubling each time; it is with
 *   the host meanwhile, and npuflow_automake makes it when the wait ends.
 * - Ending, its last frame a FIN or a RST: left to pf, whose keepalive takes it out.
 * Under sc->sx, inside curvnet.
 */
static void
npuflow_letgo(struct npuflow_softc *sc)
{
	struct npuep_flow_cand *cd;
	struct npuflow_conn *cn;
	int i, k, ci, d, held, nrm = 0, max = NPUFLOW_CAND_MAX;

	if (sc->conn == NULL)
		return;
	FLOW_LOCK(sc);
	for (i = 0; i < NPUFLOW_CAND_MAX && nrm < max; i++) {
		struct npuep_flow_cand *c = &sc->cand[i];

		if (c->frames == 0 || c->closing)
			continue;
		ci = npuflow_conn_find(sc, &c->tuple);
		if (ci < 0)
			continue;
		cn = &sc->conn[ci];
		if (cn->making || (int)(c->when - cn->when) < hz / 4 ||
		    (u_int)(ticks - c->when) >= (u_int)(2 * hz))
			continue;
		for (k = 0; k < nrm && sc->rm_ci[k] != (uint32_t)ci; k++)
			;
		if (k < nrm)
			continue;
		d = npuflow_tuple_same(&cn->tuple[0], &c->tuple) ? 0 : 1;
		if (c->id == cn->mf_id[d] && cn->rv_streak < NPUFLOW_REVIVE_STREAK) {
			if (cn->rv_tick != 0 && (u_int)(ticks - cn->rv_tick) < (u_int)cn->rv_wait)
				continue;	/* not yet: a revival a moment ago */
			sc->rm_revive[nrm] = 1;
		} else
			sc->rm_revive[nrm] = 0;
		sc->rm_ci[nrm] = (uint32_t)ci;
		sc->rm_id[nrm] = c->id;
		sc->rm_slot[nrm] = (uint16_t)i;
		nrm++;
	}
	FLOW_UNLOCK(sc);
	for (i = 0; i < nrm; i++) {
		if (sc->rm_revive[i]) {
			if (npuflow_revive(sc, (int)sc->rm_ci[i]) == 0) {
				sc->revived++;
				continue;
			}
			sc->revive_failed++;
		}
		/* what it carried since the last sweep, to pf before it goes: see npuflow_give_pf */
		npuflow_give_pf(sc, (int)sc->rm_ci[i]);
		npuflow_takeout_how(sc, (int)sc->rm_ci[i], 0);
		/*
		 * Made again at once only if it had been in hardware a while. Given back within
		 * NPUFLOW_REMAKE_CALM of being made, it stays with the host for a wait first - a second,
		 * doubling each time, to a minute - and npuflow_automake makes it once the wait is over.
		 * Until 2026-10-19 it was made again at once, every time, and making it again is what
		 * gave it back: the frames still in the host's path are overtaken by the first the fast
		 * path forwards, the receiver answers the reordering with a run of duplicate
		 * acknowledgements, and the fast path's rule - ten packets in a row with the same
		 * acknowledgement, end and window, USFP_MAX_RETRANS in the vendor's conn_table.h - gives
		 * it back again. Measured, the XGS 3300 sending to a PC through the 136: made again 29
		 * times in 15 seconds, 2,110 segments retransmitted with none lost, 789 Mbit/s in hardware
		 * against 887 through the host (docs/measurements/xgs136-giveback-2026-10-19.md). Taken
		 * out while it waits, so its frames are the host's like any other's: kept in the table
		 * given back, each of them woke this pass - 131,974 passes in 12 seconds, measured.
		 */
		FLOW_LOCK(sc);
		cd = &sc->cand[sc->rm_slot[i]];
		held = 0;
		if (cd->frames != 0 && cd->mk_tick != 0 &&
		    (u_int)(ticks - cd->mk_tick) < (u_int)NPUFLOW_REMAKE_CALM) {
			cd->hold_wait = cd->hold_wait == 0 ? NPUFLOW_REMAKE_WAIT_MIN :
			    imin(cd->hold_wait * 2, NPUFLOW_REMAKE_WAIT_MAX);
			cd->hold_until = ticks + cd->hold_wait;
			if (cd->hold_until == 0)
				cd->hold_until = 1;
			sc->remake_held++;
			held = 1;
		}
		FLOW_UNLOCK(sc);
		if (held)
			continue;
		if (npuflow_make(sc, sc->rm_id[i], sc->rm_slot[i], NULL) == 0) {
			sc->remade++;
			FLOW_LOCK(sc);
			sc->cand[sc->rm_slot[i]].mk_tick = ticks != 0 ? ticks : 1;
			FLOW_UNLOCK(sc);
		}
	}
}

/* The receive path's kick (npuflow_cand_push): the let-go connections now, not at the next sweep. */
static void
npuflow_fast(void *arg, int pending __unused)
{
	struct npuflow_softc *sc = arg;

	CURVNET_SET(vnet0);
	sx_xlock(&sc->sx);
	atomic_store_rel_int(&sc->fast_kicked, 0);
	if (!sc->detaching && sc->autom)
		npuflow_letgo(sc);
	sc->fast_runs++;
	sc->fast_last = ticks;
	sx_xunlock(&sc->sx);
	CURVNET_RESTORE();
}

/*
 * What the owner keeps with the host. The OCTEON TX driver's octep_flow_* functions, the same
 * questions on the same tuple.
 */

/* Is this IPv6 address, network order, in the network an exclusion names? */
static int
npuflow_v6_in(const uint32_t *a, const struct npuflow_excl *x)
{
	uint32_t m;
	int w, bits;

	for (w = 0, bits = x->plen; w < 4 && bits > 0; w++, bits -= 32) {
		m = bits >= 32 ? 0xffffffffu : htonl(0xffffffffu << (32 - bits));
		if ((a[w] & m) != (x->a6[w] & m))
			return (0);
	}
	return (1);
}

/* Either address in a listed network, or either port a listed one? Under FLOW_LOCK. */
static int
npuflow_excl_names(const struct npuflow_softc *sc, const struct npuep_pf_tuple *t)
{
	const struct npuflow_excl *x;
	uint32_t m;
	int i;

	for (i = 0; i < sc->nexcl; i++) {
		x = &sc->excl[i];
		switch (x->kind) {
		case 'p':
			if (ntohs(t->sport) == x->port || ntohs(t->dport) == x->port)
				return (1);
			break;
		case 4:
			if (t->af != AF_INET)
				break;
			m = x->plen == 0 ? 0 : 0xffffffffu << (32 - x->plen);
			if ((ntohl(t->sip) & m) == (x->a4 & m) || (ntohl(t->dip) & m) == (x->a4 & m))
				return (1);
			break;
		case 6:
			if (t->af == AF_INET6 && (npuflow_v6_in(t->sip6, x) || npuflow_v6_in(t->dip6, x)))
				return (1);
			break;
		}
	}
	return (0);
}

/* n tuples, any of them held or excluded? Under FLOW_LOCK. */
static int
npuflow_kept_locked(struct npuflow_softc *sc, const struct npuep_pf_tuple *tup, int n)
{
	int i, k;

	for (i = 0; i < n; i++) {
		if (npuflow_excl_names(sc, &tup[i]))
			return (1);
		for (k = 0; k < NPUFLOW_HELD_MAX; k++)
			if (sc->held[k].used && (npuflow_tuple_same(&sc->held[k].tuple[0], &tup[i]) ||
			    npuflow_tuple_same(&sc->held[k].tuple[1], &tup[i])))
				return (1);
	}
	return (0);
}

static int
npuflow_kept(struct npuflow_softc *sc, const struct npuep_pf_tuple *tup, int n)
{
	int kept;

	FLOW_LOCK(sc);
	kept = npuflow_kept_locked(sc, tup, n);
	FLOW_UNLOCK(sc);
	return (kept);
}

/* The canonical list: octep_flow_excl_parse's, token for token. */
static int
npuflow_excl_parse(const char *in, struct npuflow_excl *x, int *np)
{
	const char *p = in;
	char word[9], *end;
	u_long v;
	int n = 0, w;

	for (;;) {
		while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n')
			p++;
		if (*p == '\0')
			break;
		if (n == NPUFLOW_EXCL_MAX)
			return (E2BIG);
		memset(&x[n], 0, sizeof(x[n]));
		if (strncmp(p, "p:", 2) == 0) {
			v = strtoul(p + 2, &end, 10);
			if (end == p + 2 || v == 0 || v > 65535)
				return (EINVAL);
			x[n].kind = 'p';
			x[n].port = (uint16_t)v;
			p = end;
		} else if (strncmp(p, "a4:", 3) == 0) {
			if (strnlen(p + 3, 9) < 9 || p[11] != '/')
				return (EINVAL);
			memcpy(word, p + 3, 8);
			word[8] = '\0';
			v = strtoul(word, &end, 16);
			if (end != word + 8)
				return (EINVAL);
			x[n].a4 = (uint32_t)v;
			p += 12;
			v = strtoul(p, &end, 10);
			if (end == p || v > 32)
				return (EINVAL);
			x[n].kind = 4;
			x[n].plen = (uint8_t)v;
			p = end;
		} else if (strncmp(p, "a6:", 3) == 0) {
			p += 3;
			if (strnlen(p, 33) < 33 || p[32] != '/')
				return (EINVAL);
			for (w = 0; w < 4; w++, p += 8) {
				memcpy(word, p, 8);
				word[8] = '\0';
				v = strtoul(word, &end, 16);
				if (end != word + 8)
					return (EINVAL);
				x[n].a6[w] = htonl((uint32_t)v);
			}
			p++;
			v = strtoul(p, &end, 10);
			if (end == p || v > 128)
				return (EINVAL);
			x[n].kind = 6;
			x[n].plen = (uint8_t)v;
			p = end;
		} else
			return (EINVAL);
		if (*p != '\0' && *p != ' ' && *p != ',' && *p != '\t' && *p != '\n')
			return (EINVAL);
		n++;
	}
	*np = n;
	return (0);
}

/*
 * flow.exclude: the list. Refused whole when it does not parse; in force at once when it does, and
 * the connections in hardware it names go back to the host in the same write.
 */
static int
npuflow_sysctl_exclude(SYSCTL_HANDLER_ARGS)
{
	struct npuflow_softc *sc = arg1;
	struct npuflow_excl *x;
	char *in;
	int error, n, k, i, hit;

	if (req->newptr == NULL)
		return (sysctl_handle_string(oidp, sc->excl_text, sizeof(sc->excl_text), req));
	in = malloc(NPUFLOW_EXCL_TEXT, M_TEMP, M_WAITOK | M_ZERO);
	x = malloc(sizeof(*x) * NPUFLOW_EXCL_MAX, M_TEMP, M_WAITOK | M_ZERO);
	n = 0;
	error = sysctl_handle_string(oidp, in, NPUFLOW_EXCL_TEXT, req);
	if (error == 0)
		error = npuflow_excl_parse(in, x, &n);
	if (error == 0) {
		sx_xlock(&sc->sx);
		FLOW_LOCK(sc);
		memcpy(sc->excl, x, sizeof(*x) * n);
		sc->nexcl = n;
		strlcpy(sc->excl_text, in, sizeof(sc->excl_text));
		FLOW_UNLOCK(sc);
		/* From the end of the live list, as npuflow_takeout_all walks it. */
		for (k = (sc->conn == NULL) ? -1 : (int)sc->conn_nlive - 1; k >= 0; k--) {
			FLOW_LOCK(sc);
			if ((uint32_t)k >= sc->conn_nlive) {
				FLOW_UNLOCK(sc);
				continue;
			}
			i = (int)sc->conn_live[k];
			hit = sc->conn[i].used && !sc->conn[i].making &&
			    (npuflow_excl_names(sc, &sc->conn[i].tuple[0]) ||
			    npuflow_excl_names(sc, &sc->conn[i].tuple[1]));
			FLOW_UNLOCK(sc);
			if (hit) {
				npuflow_takeout(sc, i);
				sc->excluded++;
			}
		}
		sx_xunlock(&sc->sx);
	}
	free(x, M_TEMP);
	free(in, M_TEMP);
	return (error);
}

/*
 * flow.release: write a connection's index - flow.flows's first column, the Flows page's Slot - to
 * hand it back to the host and keep it there until pf lets its state go. Reading shows the last.
 */
static int
npuflow_sysctl_release(SYSCTL_HANDLER_ARGS)
{
	struct npuflow_softc *sc = arg1;
	struct npuflow_held *h;
	char in[16], *end;
	u_long ci;
	int error, k;

	if (req->newptr == NULL)
		return (sysctl_handle_string(oidp, sc->release_result, sizeof(sc->release_result), req));
	in[0] = '\0';
	error = sysctl_handle_string(oidp, in, sizeof(in), req);
	if (error != 0)
		return (error);
	ci = strtoul(in, &end, 10);
	sx_xlock(&sc->sx);
	FLOW_LOCK(sc);
	if (end == in || sc->conn == NULL || ci < 2 || ci >= sc->conn_cap || !sc->conn[ci].used ||
	    sc->conn[ci].making) {
		FLOW_UNLOCK(sc);
		sx_xunlock(&sc->sx);
		snprintf(sc->release_result, sizeof(sc->release_result),
		    "no connection %s in hardware\n", in);
		return (0);
	}
	for (k = 0; k < NPUFLOW_HELD_MAX && sc->held[k].used; k++)
		;
	if (k == NPUFLOW_HELD_MAX)
		k = (int)(sc->held_next++ % NPUFLOW_HELD_MAX);	/* all in use: the oldest place */
	h = &sc->held[k];
	h->used = 1;
	h->pf_dir = sc->conn[ci].pf_dir;
	h->probe_dir = sc->conn[ci].probe_dir;
	h->tuple[0] = sc->conn[ci].tuple[0];
	h->tuple[1] = sc->conn[ci].tuple[1];
	FLOW_UNLOCK(sc);
	npuflow_takeout(sc, (int)ci);
	sc->released++;
	sx_xunlock(&sc->sx);
	snprintf(sc->release_result, sizeof(sc->release_result),
	    "connection %lu handed back to the host, and kept there until pf lets it go\n", ci);
	return (0);
}

/*
 * flow.auto. On this family the switch is two: the maker itself, and the front ports' interface
 * entries, which the driver keeps shut - every frame to the host - so that past them the fast path
 * names no connection and the maker would have nothing to make (issue 483). Turning it on opens
 * them; turning it off shuts them again, and a connection still in hardware is then reached by its
 * frames no more: they go to the host, pf sees them, and its state ends as any other.
 */
static int
npuflow_sysctl_auto(SYSCTL_HANDLER_ARGS)
{
	struct npuflow_softc *sc = arg1;
	int error, v;

	v = sc->autom;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (v != 0 && v != 1)
		return (EINVAL);
	sc->autom = v;
	(void)npurpc_entries(v);
	return (0);
}

/* A held connection is let go once pf has let its state go. Under sc->sx, inside curvnet. */
static void
npuflow_held_sweep(struct npuflow_softc *sc)
{
	struct npuep_pf_tuple t;
	int i, pf_dir;

	if (!npuep_pf_present())
		return;
	for (i = 0; i < NPUFLOW_HELD_MAX; i++) {
		FLOW_LOCK(sc);
		if (!sc->held[i].used) {
			FLOW_UNLOCK(sc);
			continue;
		}
		t = sc->held[i].tuple[sc->held[i].probe_dir];
		pf_dir = sc->held[i].pf_dir;
		FLOW_UNLOCK(sc);
		if (npuep_pf_state_exists(&t, pf_dir, NULL))
			continue;
		FLOW_LOCK(sc);
		if (sc->held[i].used && npuflow_tuple_same(&sc->held[i].tuple[sc->held[i].probe_dir], &t))
			sc->held[i].used = 0;
		FLOW_UNLOCK(sc);
	}
}

static int
npuflow_sysctl_accelerate(SYSCTL_HANDLER_ARGS)
{
	struct npuflow_softc *sc = arg1;
	struct sbuf sb;
	char in[32];
	int error;
	long id;

	if (req->newptr == NULL)
		return (sysctl_handle_string(oidp, sc->accel_result, sizeof(sc->accel_result), req));
	in[0] = '\0';
	error = sysctl_handle_string(oidp, in, sizeof(in), req);
	if (error != 0)
		return (error);
	sbuf_new(&sb, sc->accel_result, sizeof(sc->accel_result), SBUF_FIXEDLEN);
	sx_xlock(&sc->sx);
	if (strcmp(in, "off") == 0 || strcmp(in, "0") == 0)
		sbuf_printf(&sb, "took %d flow(s) back\n", npuflow_takeout_all(sc));
	else {
		id = strtol(in, NULL, 0);
		if (id <= 0)
			sbuf_cat(&sb, "write a candidate's id to accelerate it, or \"off\"\n");
		else {
			CURVNET_SET(vnet0);
			(void)npuflow_make(sc, (uint32_t)id, -1, &sb);
			CURVNET_RESTORE();
		}
	}
	sx_xunlock(&sc->sx);
	sbuf_finish(&sb);
	sbuf_delete(&sb);
	return (0);
}

/*
 * dev.npuep.0.flow.fw_rev_bump: write anything to raise the firewall revision, void every offloaded
 * flow on the far side, and forget them here. Write-only, the value ignored - the OPNsense firewall
 * hook writes it on every ruleset reload. What happened is in flow.fw_rev and the two counters.
 */
static int
npuflow_sysctl_fw_rev_bump(SYSCTL_HANDLER_ARGS)
{
	struct npuflow_softc *sc = arg1;
	int error, v = 0;

	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	sx_xlock(&sc->sx);
	npuflow_fw_rev_bump(sc);
	sx_xunlock(&sc->sx);
	return (0);
}

/* One address of a tuple as the pages print it: IPv4 dotted, IPv6 in full and in brackets. */
#define	NPUFLOW_ADDR_TEXT	(INET6_ADDRSTRLEN + 2)

static const char *
npuflow_addr(char buf[2][NPUFLOW_ADDR_TEXT], const struct npuep_pf_tuple *t, int dst)
{
	uint32_t a;

	if (t->af == AF_INET6) {
		snprintf(buf[0], NPUFLOW_ADDR_TEXT, "[%s]",
		    ip6_sprintf(buf[1], (const struct in6_addr *)(dst ? t->dip6 : t->sip6)));
	} else {
		a = dst ? t->dip : t->sip;
		snprintf(buf[0], NPUFLOW_ADDR_TEXT, "%u.%u.%u.%u", a & 0xff, (a >> 8) & 0xff,
		    (a >> 16) & 0xff, (a >> 24) & 0xff);
	}
	return (buf[0]);
}

/* dev.npuep.0.flow.flows: the connections the fast path forwards for, one a line. */
static int
npuflow_sysctl_flows(SYSCTL_HANDLER_ARGS)
{
	struct npuflow_softc *sc = arg1;
	struct npuflow_conn c;
	struct sbuf *sb;
	char a[4][2][NPUFLOW_ADDR_TEXT];
	int i, k, error, n = 0;

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_printf(sb, "auto %s | connections %u of %u (board %u) next hops %u of %u | made %ju "
	    "(auto %ju) taken %ju gone %ju | flow-full %ju nhop-full %ju v6-refused %ju | fw_rev %u "
	    "bumps %ju/%ju\n",
	    sc->autom ? "on" : "off", sc->conn_used, sc->conn_max, sc->board_conn, sc->nhop_used,
	    sc->nhop_cap, (uintmax_t)sc->made, (uintmax_t)sc->auto_made, (uintmax_t)sc->taken,
	    (uintmax_t)sc->auto_gone, (uintmax_t)sc->flow_full, (uintmax_t)sc->nhop_full,
	    (uintmax_t)sc->v6_refused, sc->fw_rev, (uintmax_t)sc->fw_rev_bumps,
	    (uintmax_t)sc->fw_rev_bump_fail);
	for (k = 0; ; k++) {
		FLOW_LOCK(sc);
		if (sc->conn == NULL || (uint32_t)k >= sc->conn_nlive) {
			FLOW_UNLOCK(sc);
			break;
		}
		i = (int)sc->conn_live[k];
		c = sc->conn[i];
		FLOW_UNLOCK(sc);
		if (!c.used)
			continue;
		n++;
		if (n > NPUFLOW_PRINT_MAX)
			continue;
		sbuf_printf(sb, "%3d rev %u %s%s%s %ds | orig mflow %u nhop %u %s:%u -> %s:%u | "
		    "reply mflow %u nhop %u %s:%u -> %s:%u%s\n",
		    i, c.conn_rev, c.snat ? "snat" : "", c.snat && c.dnat ? "+" : "",
		    c.dnat ? "dnat" : (c.snat ? "" : "plain"), (ticks - c.when) / hz,
		    c.mf_id[0], c.nhop[0],
		    npuflow_addr(a[0], &c.tuple[0], 0), ntohs(c.tuple[0].sport),
		    npuflow_addr(a[1], &c.tuple[0], 1), ntohs(c.tuple[0].dport),
		    c.mf_id[1], c.nhop[1],
		    npuflow_addr(a[2], &c.tuple[1], 0), ntohs(c.tuple[1].sport),
		    npuflow_addr(a[3], &c.tuple[1], 1), ntohs(c.tuple[1].dport),
		    c.making ? " (being made)" : "");
		/* what the coprocessor counted for it, the XGS 3300's line: see npuflow_cmsg_rx */
		if (!c.hw_seen)
			sbuf_cat(sb, "      counted  nothing yet\n");
		else
			sbuf_printf(sb, "      counted  orig %ju packets %ju bytes  reply %ju packets %ju bytes"
			    "  moved %jd s ago\n",
			    (uintmax_t)c.hw_pkts[0], (uintmax_t)c.hw_bytes[0],
			    (uintmax_t)c.hw_pkts[1], (uintmax_t)c.hw_bytes[1],
			    c.hw_moved != 0 ? (intmax_t)(time_uptime - c.hw_moved) : (intmax_t)-1);
	}
	if (n > NPUFLOW_PRINT_MAX)
		sbuf_printf(sb, "and %d more connections, not printed\n", n - NPUFLOW_PRINT_MAX);
	if (n == 0)
		sbuf_cat(sb, "no flows accelerated\n");
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

static int
npuflow_sysctl_candidates(SYSCTL_HANDLER_ARGS)
{
	struct npuflow_softc *sc = arg1;
	struct npuep_flow_cand c;
	struct sbuf *sb;
	char a[2][2][NPUFLOW_ADDR_TEXT];
	int i, error, n = 0;

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_printf(sb, "pushed %ju replaced %ju seen again %ju aged %ju | pf %s\n",
	    (uintmax_t)sc->pushed, (uintmax_t)sc->replaced, (uintmax_t)sc->seen_again,
	    (uintmax_t)sc->aged, npuep_pf_present() ? "present" : "not found");
	for (i = 0; i < NPUFLOW_CAND_MAX; i++) {
		FLOW_LOCK(sc);
		c = sc->cand[i];
		FLOW_UNLOCK(sc);
		if (c.frames == 0)
			continue;
		n++;
		sbuf_printf(sb, "%4d id %u rev %u port %d tag 0x%04x frames %u %ds ago | proto %u "
		    "%s:%u -> %s:%u | pf %s%s",
		    i, c.id, c.rev, c.port, c.tag, c.frames, (ticks - c.when) / hz, c.tuple.proto,
		    npuflow_addr(a[0], &c.tuple, 0), ntohs(c.tuple.sport),
		    npuflow_addr(a[1], &c.tuple, 1), ntohs(c.tuple.dport),
		    c.pf_seen ? "state" : "none", c.pf_settled ? " settled" : "");
		if (c.pf_seen)
			sbuf_printf(sb, " dir %s%s%s on %s", c.st.direction == PF_IN ? "in" : "out",
			    c.st.nat_snat ? " snat" : "", c.st.nat_dnat ? " dnat" : "", c.st.ifname);
		if (c.nh_ok)
			sbuf_printf(sb, " | nhop port %d iface %u tag 0x%04x mtu %u %02x:%02x:%02x:%02x:%02x:%02x%s",
			    c.nh.port, c.nh.iface, c.nh.tag, c.nh.mtu, c.nh.dmac[0], c.nh.dmac[1],
			    c.nh.dmac[2], c.nh.dmac[3], c.nh.dmac[4], c.nh.dmac[5],
			    c.nh.from_hint ? " (by the flow's port)" : "");
		else
			sbuf_printf(sb, " | nhop error %d", c.nh_err);
		sbuf_cat(sb, "\n");
	}
	if (n == 0)
		sbuf_cat(sb, "no candidates: no punted frame has named a microflow yet\n");
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

static int
npuflow_sysctl_period(SYSCTL_HANDLER_ARGS)
{
	struct npuflow_softc *sc = arg1;
	int v, error;

	v = sc->period;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (v < 0 || v > 60 * hz)
		return (EINVAL);
	sc->period = v;
	if (v > 0)
		taskqueue_enqueue_timeout(sc->tq, &sc->task, 0);
	return (0);
}

int
npuflow_attach(device_t dev, struct sysctl_ctx_list *ctx, struct sysctl_oid *parent)
{
	struct npuflow_softc *sc;
	struct sysctl_oid *node;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->dev = dev;
	mtx_init(&sc->mtx, "npuflow", NULL, MTX_DEF);
	sx_init(&sc->sx, "npuflowx");
	sc->tq = taskqueue_create("npuflow", M_WAITOK, taskqueue_thread_enqueue, &sc->tq);
	TIMEOUT_TASK_INIT(sc->tq, &sc->task, 0, npuflow_sweep, sc);
	TASK_INIT(&sc->fast_task, 0, npuflow_fast, sc);
	taskqueue_start_threads(&sc->tq, 1, PI_NET, "npuflow");
	sc->period = hz;
	sc->per_sweep = NPUFLOW_PER_SWEEP;
	sc->pf_sloppy = 1;
	sc->hw_to_pf = 1;
	sc->lif_apply = 1;
	sc->attached_at = ticks;
	strlcpy(sc->accel_result, "nothing accelerated yet\n", sizeof(sc->accel_result));
	npuflow_sc = sc;

	node = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(parent), OID_AUTO, "flow",
	    CTLFLAG_RD | CTLFLAG_MPSAFE, NULL, "the driver's side of the fast path's flows");
	if (node != NULL) {
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "candidates",
		    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, npuflow_sysctl_candidates,
		    "A", "the flows the fast path named in punted frames, with what pf and the "
		    "routing table say about each");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "period",
		    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, npuflow_sysctl_period, "I",
		    "ticks between sweeps of the candidates; 0 stops them");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "accelerate",
		    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, npuflow_sysctl_accelerate,
		    "A", "write a candidate's id to hand that flow to the fast path (FLOW_CREATE_FP), "
		    "or \"off\" to take every flow back; reading shows the last result");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "exclude",
		    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, npuflow_sysctl_exclude,
		    "A", "addresses, networks and ports never offloaded, as scripts/xgs/flow_exclude.php "
		    "writes the owner's list: a4:<8 hex digits>/<length>, a6:<32 hex digits>/<length> "
		    "and p:<port>, separated by spaces. A list that does not parse is refused whole; one "
		    "that does takes out at once what it names. Empty is none");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "release",
		    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, npuflow_sysctl_release,
		    "A", "write a connection's index (flow.flows's first column) to hand it back to the "
		    "host and keep it there until pf lets its state go; reading shows the last result");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "excluded", CTLFLAG_RD,
		    &sc->excluded, 0, "times a connection was not made, or was taken out, because the "
		    "owner keeps it with the host - an attempt each, not a count of connections");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "released", CTLFLAG_RD,
		    &sc->released, 0, "connections handed back to the host one at a time, from the "
		    "Flows page");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "auto",
		    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, npuflow_sysctl_auto, "I",
		    "let the sweep accelerate settled, resolved candidates on its own and take each "
		    "back when pf lets go of its state; 1 also opens the front ports' interface "
		    "entries, without which the fast path names no connection, and 0 shuts them");
		SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "make_per_sweep",
		    CTLFLAG_RW, &sc->per_sweep, 0, "at most this many flows made automatically per "
		    "sweep (1 to 1024)");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flows",
		    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, npuflow_sysctl_flows,
		    "A", "the connections the fast path forwards for, with their microflows and next hops");
		SYSCTL_ADD_U32(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_cap", CTLFLAG_RD,
		    &sc->conn_cap, 0, "connections the table was allocated for: the board's, capped "
		    "by a 16th of the memory, or hw.npuep.flow_max; 0 until the board is known");
		SYSCTL_ADD_U32(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conn_max", CTLFLAG_RD,
		    &sc->conn_max, 0, "connections this host will hand to the fast path at most");
		SYSCTL_ADD_U32(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "conns", CTLFLAG_RD,
		    &sc->conn_used, 0, "connections the fast path forwards now");
		SYSCTL_ADD_U32(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhop_cap", CTLFLAG_RD,
		    &sc->nhop_cap, 0, "next hops the table was allocated for: the board's");
		SYSCTL_ADD_U32(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "nhops", CTLFLAG_RD,
		    &sc->nhop_used, 0, "next hops programmed now");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_pushed", CTLFLAG_RD,
		    &sc->pushed, 0, "candidates made from punted frames");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_replaced", CTLFLAG_RD,
		    &sc->replaced, 0, "candidates pushed out by newer ones on the same slot");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_aged", CTLFLAG_RD,
		    &sc->aged, 0, "candidates cleared after ten seconds without a punted frame");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "auto_made", CTLFLAG_RD,
		    &sc->auto_made, 0, "flows the sweep accelerated without being asked");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "auto_gone", CTLFLAG_RD,
		    &sc->auto_gone, 0, "flows taken back because pf no longer held their state");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "remade", CTLFLAG_RD,
		    &sc->remade, 0, "connections the fast path let go of while pf still held them, "
		    "made again from the identities their frames came back under");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "revived", CTLFLAG_RD,
		    &sc->revived, 0, "connections the fast path gave back, put back to VALID in place "
		    "with their microflows as they were");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "revive_failed", CTLFLAG_RD,
		    &sc->revive_failed, 0, "revivals the far side did not take, remade instead");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fast_runs", CTLFLAG_RD,
		    &sc->fast_runs, 0, "times the receive path's kick ran the let-go pass");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "remake_held", CTLFLAG_RD,
		    &sc->remake_held, 0, "connections the fast path gave back soon after they were made, "
		    "left with the host for a wait before they are made again");
		SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "pf_sloppy", CTLFLAG_RW,
		    &sc->pf_sloppy, 0, "1 (the default) tells pf to stop judging the sequence numbers of "
		    "every connection made in hardware, so its frames are not dropped when they come back "
		    "to the host; 0 leaves pf judging them, for measuring only");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sloppy_marked", CTLFLAG_RD,
		    &sc->sloppy_marked, 0, "pf states of connections made in hardware told to stop judging "
		    "sequence numbers, so the frames that come back to the host are not dropped");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cmsg_rx",
		    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, npuflow_sysctl_cmsg_rx, "A",
		    "the coprocessor's control messages read on arrival, by type, and of its connection "
		    "counts how many were applied to a connection here, how many named one gone, and how "
		    "many messages could not be read to their end");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hw_counted", CTLFLAG_RD,
		    &sc->hw_applied, 0, "connection counts from the coprocessor applied to this table "
		    "since attach");
		SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hw_to_pf", CTLFLAG_RW,
		    &sc->hw_to_pf, 0, "1 (the default): every sweep, add what the coprocessor counted for "
		    "each connection to its pf states and the rule that passed it, as its packets would "
		    "have; 0 off");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hw_pf_conns", CTLFLAG_RD,
		    &sc->hw_pf_conns, 0, "connections whose counts were added to pf");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "hw_pf_states", CTLFLAG_RD,
		    &sc->hw_pf_states, 0, "pf states those counts were added to");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_counted",
		    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, npuflow_sysctl_lif_counted, "A",
		    "what the coprocessor forwarded itself on each port (type 0), in and out, since attach "
		    "- the traffic the interface's own counters do not see");
		SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_apply", CTLFLAG_RW,
		    &sc->lif_apply, 0, "1 (the default): add the coprocessor's port counts - what it "
		    "forwarded itself, which the interfaces' own counters never see - to those counters; "
		    "0 no");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_applied", CTLFLAG_RD,
		    &sc->lif_applied, 0, "port counts added to an interface");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "lif_unknown", CTLFLAG_RD,
		    &sc->lif_unknown, 0, "port counts for a LIF no front port here has");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "held_us_last", CTLFLAG_RD,
		    &sc->held_us_last, 0, "microseconds the last sweep held the flow table for its "
		    "upkeep - the keepalive, the takeouts, the makes");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "held_us_max", CTLFLAG_RW,
		    &sc->held_us_max, 0, "the longest such hold since it was last set; write 0 to start "
		    "again");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_full", CTLFLAG_RD,
		    &sc->flow_full, 0, "makes refused because the table was full or not allocated");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "v6_refused", CTLFLAG_RD,
		    &sc->v6_refused, 0, "IPv6 connections left with the host because pf translates them");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fw_rev_bump",
		    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0, npuflow_sysctl_fw_rev_bump, "I",
		    "write anything to raise the firewall revision and void every offloaded flow "
		    "(FW_STATE_REV_SET); the OPNsense firewall hook writes it on every ruleset reload");
		SYSCTL_ADD_U16(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fw_rev", CTLFLAG_RD,
		    &sc->fw_rev, 0, "the current firewall revision every flow is stamped with");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fw_rev_bumps", CTLFLAG_RD,
		    &sc->fw_rev_bumps, 0, "revision bumps the far side took");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "fw_rev_bump_fail", CTLFLAG_RD,
		    &sc->fw_rev_bump_fail, 0, "revision bumps the far side refused");
	}
	taskqueue_enqueue_timeout(sc->tq, &sc->task, sc->period);
	return (0);
}

void
npuflow_detach(void)
{
	struct npuflow_softc *sc = npuflow_sc;

	if (sc == NULL)
		return;
	sc->detaching = 1;
	sc->period = 0;
	npuflow_sc = NULL;
	while (taskqueue_cancel_timeout(sc->tq, &sc->task, NULL) != 0)
		taskqueue_drain_timeout(sc->tq, &sc->task);
	taskqueue_drain_timeout(sc->tq, &sc->task);
	taskqueue_drain(sc->tq, &sc->fast_task);
	/*
	 * The sweep is drained, so nothing else is making flows; take back what is in hardware before
	 * the table goes away, or the fast path forwards on connections whose host is gone. A flow
	 * made by hand needs this as much as one the sweep made.
	 */
	sx_xlock(&sc->sx);
	(void)npuflow_takeout_all(sc);
	sx_xunlock(&sc->sx);
	taskqueue_free(sc->tq);
	npuflow_tables_free(sc);
	sx_destroy(&sc->sx);
	mtx_destroy(&sc->mtx);
	free(sc, M_DEVBUF);
}
