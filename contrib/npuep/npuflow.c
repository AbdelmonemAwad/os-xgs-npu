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
 * and identity here; this keeps the last few as candidates, and a task asks pf and the routing
 * table what it would take to accelerate each one - whether pf holds a settled state for it, how
 * that state is translated, and where its next hop is. Nothing is sent to the coprocessor in
 * this slice: dev.npuep.0.flow.candidates shows what the next one would send.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
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

#include "npuep.h"
#include "npuflow.h"

/*
 * A next-hop table entry, shared by every flow direction that leaves the same way: keyed by the
 * whole resolved answer (egress interface, MTU, the two MACs), reference-counted, freed when its
 * last flow lets go. Many internet-bound flows resolve to one WAN gateway, so without sharing the
 * table (128) would fill at about sixty flows; with it they cost one entry. The revision is kept
 * across reuse, as the far side expects, because a microflow carries it beside the index.
 */
struct npuflow_nhop_ent {
	int		used;
	uint32_t	refcnt;
	uint8_t		rev;
	uint8_t		iface;
	uint16_t	mtu;
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
	uint32_t	mf_id[2];	/* the two microflow ids, for MFLOW_INVALIDATE on takeout */
	uint32_t	mf_rev[2];
	uint32_t	nhop[2];	/* the next-hop table indices, freed on takeout */
	int		when;		/* ticks at creation */
};

/*
 * The tables are bounded for a bench and well under what the XGS 136 reports (its max_conn_entries
 * is in the millions). Indices 0 and 1 are never handed out, the reason the OCTEON TX driver gives:
 * the hand-programmed experiments used 1 and bring-up uses 0, so starting at 2 keeps a flow this
 * driver made from colliding with either.
 */
#define	NPUFLOW_FLOW_MAX	256
#define	NPUFLOW_NHOP_MAX	128
#define	NPUFLOW_PER_SWEEP	8	/* at most this many new flows made automatically per sweep */
#define	NPUFLOW_MFLOW_TO	60	/* a microflow's idle timeout in seconds, as measured on octep */

struct npuflow_softc {
	struct mtx		 mtx;	/* the candidate and flow tables' fields, for the readers */
	struct sx		 sx;	/* serialises make/takeout/sweep: each sleeps inside an RPC */
	device_t		 dev;
	struct npuep_flow_cand	 cand[NPUFLOW_CAND_MAX];
	uint32_t		 next;		/* the slot the next new candidate takes */
	uint64_t		 pushed, replaced, seen_again;
	struct taskqueue	*tq;
	struct timeout_task	 task;
	int			 period;	/* ticks between sweeps; 0 stops them */
	int			 detaching;
	/*
	 * The flow table. flow.auto lets the sweep turn settled, resolved candidates into
	 * FLOW_CREATE_FP on its own, keep each while pf still holds its state, and take it back when
	 * pf lets go; flow.accelerate still makes one by hand. Every flow owns one connection index
	 * and two next-hop indices out of these tables.
	 */
	int			 autom;		/* flow.auto */
	struct npuflow_conn	 conn[NPUFLOW_FLOW_MAX];
	struct npuflow_nhop_ent	 nhop[NPUFLOW_NHOP_MAX];
	uint32_t		 conn_used, nhop_used;
	uint64_t		 made, taken, auto_made, auto_gone, flow_full, nhop_full;
	/*
	 * The firewall revision barrier. Every flow is stamped with fw_rev; flow.fw_rev_bump raises
	 * it and posts FW_STATE_REV_SET, which makes the fast path void the whole microflow table at
	 * once - the only thing a ruleset reload must do, so the coprocessor never forwards under rules
	 * that no longer exist. The OPNsense firewall hook writes it on every reload.
	 */
	uint16_t		 fw_rev;
	uint64_t		 fw_rev_bumps, fw_rev_bump_fail;
	char			 accel_result[640];
};

static struct npuflow_softc *npuflow_sc;

#define	FLOW_LOCK(sc)	mtx_lock(&(sc)->mtx)
#define	FLOW_UNLOCK(sc)	mtx_unlock(&(sc)->mtx)

static void	npuflow_keepalive(struct npuflow_softc *);
static void	npuflow_automake(struct npuflow_softc *);
static int	npuflow_make(struct npuflow_softc *, uint32_t, struct sbuf *);
static void	npuflow_takeout(struct npuflow_softc *, int);
static int	npuflow_takeout_all(struct npuflow_softc *);

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

static int
npuflow_tuple_same(const struct npuep_pf_tuple *a, const struct npuep_pf_tuple *b)
{

	return (a->sip == b->sip && a->dip == b->dip && a->sport == b->sport &&
	    a->dport == b->dport && a->proto == b->proto && a->af == b->af);
}

/*
 * From the receive path, for every punted frame whose metadata named a microflow. Called with
 * the datapath's lock held and nothing else; this takes its own lock only, never sleeps, and
 * keeps a bounded table - the oldest entry makes room.
 */
void
npuflow_cand_push(int port, uint16_t tag, uint32_t id, uint32_t rev,
    const struct npuep_pf_tuple *t)
{
	struct npuflow_softc *sc = npuflow_sc;
	struct npuep_flow_cand *c, *oldest;
	int i;

	if (sc == NULL || id == 0)
		return;
	FLOW_LOCK(sc);
	oldest = NULL;
	for (i = 0; i < NPUFLOW_CAND_MAX; i++) {
		c = &sc->cand[i];
		if (c->frames != 0 && c->id == id && npuflow_tuple_same(&c->tuple, t)) {
			c->rev = rev;
			c->when = ticks;
			c->frames++;
			sc->seen_again++;
			FLOW_UNLOCK(sc);
			return;
		}
		if (c->frames == 0) {
			oldest = c;
			break;
		}
		if (oldest == NULL || (int)(oldest->when - c->when) > 0)
			oldest = c;
	}
	if (oldest->frames != 0)
		sc->replaced++;
	bzero(oldest, sizeof(*oldest));
	oldest->tuple = *t;
	oldest->id = id;
	oldest->rev = rev;
	oldest->tag = tag;
	oldest->port = port;
	oldest->when = ticks;
	oldest->frames = 1;
	sc->pushed++;
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
		c = sc->cand[i];
		FLOW_UNLOCK(sc);
		if (c.frames == 0)
			continue;
		have = npuep_pf_state_read(&c.tuple, &st);
		settled = have ? npuep_pf_settled(&c.tuple) : 0;
		err = npuep_nhop_resolve(c.tuple.dip, c.port, &nh);
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
	npuflow_keepalive(sc);
	if (sc->autom)
		npuflow_automake(sc);
	sx_xunlock(&sc->sx);
	CURVNET_RESTORE();
	if (sc->period > 0 && !sc->detaching)
		taskqueue_enqueue_timeout(sc->tq, &sc->task, sc->period);
}

/*
 * The original and reply tuples a connection has, from the pf state - the OCTEON TX driver's
 * octep_conn_tuples. The original is the opener's own addresses to the responder as the wire sees
 * it; the reply is the responder back to the opener's public form.
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
}

/* A candidate whose stored tuple matches, or -1. Caller holds FLOW_LOCK. */
static int
npuflow_find_tuple(struct npuflow_softc *sc, const struct npuep_pf_tuple *t)
{
	int i;

	for (i = 0; i < NPUFLOW_CAND_MAX; i++)
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

/* Program one next-hop table entry (NHOP_PROGRAM, 32 bytes). */
static int
npuflow_nhop_program(uint32_t idx, uint8_t rev, const struct npuep_nhop *nh)
{
	uint8_t pl[NPURPC_NHOP_REQ_LEN];
	int rc, n;

	memset(pl, 0, sizeof(pl));
	le32enc(pl + 0, idx);
	pl[4] = 1;			/* is_resolved */
	pl[5] = rev;
	memcpy(pl + 8, nh->dmac, 6);
	memcpy(pl + 14, nh->smac, 6);
	be16enc(pl + 20, ETHERTYPE_IP);
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
 * work - the route lookup, NHOP_PROGRAM, FLOW_CREATE_FP, MFLOW_INVALIDATE - is done with the lock
 * dropped, because npurpc_send polls the ring with pause() and a frame cannot sleep holding a
 * default mutex. make, takeout and the sweep are serialised against each other by sc->sx instead,
 * which is sleepable, so two of them never touch the tables at once even though each lets the lock
 * go for its commands.
 */

/* A connection whose stored tuple matches t in either direction, or -1. Caller holds FLOW_LOCK. */
static int
npuflow_conn_find(struct npuflow_softc *sc, const struct npuep_pf_tuple *t)
{
	int i, d;

	for (i = 2; i < NPUFLOW_FLOW_MAX; i++) {
		if (!sc->conn[i].used)
			continue;
		for (d = 0; d < 2; d++)
			if (npuflow_tuple_same(&sc->conn[i].tuple[d], t))
				return (i);
	}
	return (-1);
}

/* Reserve a connection index for a flow being made. Caller holds FLOW_LOCK. Returns -1 if full. */
static int
npuflow_conn_alloc(struct npuflow_softc *sc, const struct npuep_pf_tuple tup[2], int d,
    const struct npuep_pf_state *st)
{
	struct npuflow_conn *c;
	int i;

	for (i = 2; i < NPUFLOW_FLOW_MAX; i++) {
		c = &sc->conn[i];
		if (c->used)
			continue;
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
		sc->conn_used++;
		return (i);
	}
	sc->flow_full++;
	return (-1);
}

/* Give a connection index back. Caller holds FLOW_LOCK; the next hops are freed separately. */
static void
npuflow_conn_free(struct npuflow_softc *sc, int idx)
{

	if (idx < 2 || idx >= NPUFLOW_FLOW_MAX || !sc->conn[idx].used)
		return;
	sc->conn[idx].used = 0;
	sc->conn[idx].making = 0;
	if (sc->conn_used > 0)
		sc->conn_used--;
}

/*
 * Find or reserve the next-hop entry for a resolved answer. Caller holds FLOW_LOCK. On a match an
 * existing entry's reference count rises and *fresh is 0 - it is already programmed, nothing to do;
 * otherwise a free slot is taken, its key stored, its revision bumped, and *fresh is 1, meaning the
 * caller must post NHOP_PROGRAM for it (with the lock dropped) and, if that fails, put it back.
 * Returns the index, or -1 if the table is full. The sweep serialises every maker, so a fresh entry
 * is always programmed before another maker's lookup can match it.
 */
static int
npuflow_nhop_get(struct npuflow_softc *sc, const struct npuep_nhop *nh, uint8_t *rev, int *fresh)
{
	struct npuflow_nhop_ent *e;
	int i, free_i = -1;

	for (i = 2; i < NPUFLOW_NHOP_MAX; i++) {
		e = &sc->nhop[i];
		if (!e->used) {
			if (free_i < 0)
				free_i = i;
			continue;
		}
		if (e->iface == nh->iface && e->mtu == nh->mtu &&
		    memcmp(e->dmac, nh->dmac, 6) == 0 && memcmp(e->smac, nh->smac, 6) == 0) {
			e->refcnt++;
			*rev = e->rev;
			*fresh = 0;
			return (i);
		}
	}
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

	if (idx < 2 || idx >= NPUFLOW_NHOP_MAX)
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

/* Take one microflow out of the fast path (MFLOW_INVALIDATE). No lock held; this posts a command. */
static void
npuflow_mflow_invalidate(uint32_t mf)
{
	uint8_t pl[20];
	int rc;

	if (mf == 0)
		return;
	memset(pl, 0, sizeof(pl));
	le32enc(pl + 0, 0);		/* invalidation_type 0 = by the microflow the data names */
	le32enc(pl + 4, mf);
	(void)npurpc_send(RPC_CMD_MFLOW_INVALIDATE, pl, sizeof(pl), NULL, 0, &rc);
}

/*
 * Turn one candidate into a flow the fast path forwards itself: resolve both directions' next hops,
 * program them, and send one FLOW_CREATE_FP carrying the connection and both microflows. Called by
 * the by-hand sysctl and by the automatic sweep, both under sc->sx. On any failure every table
 * index reserved is given straight back, so a refusal leaves nothing half-made. sb may be NULL.
 */
static int
npuflow_make(struct npuflow_softc *sc, uint32_t id, struct sbuf *sb)
{
	struct npuep_flow_cand c, r;
	struct npuep_pf_state st;
	struct npuep_pf_tuple tup[2];
	struct npuep_nhop nho, nhr;
	uint8_t req[NPURPC_FLOW_REQ_LEN], *n;
	uint32_t flags, oid, orev, rid, rrev, conn_rev, fw_rev;
	uint8_t nho_rev, nhr_rev;
	int i, d, ri, lan_port, err, rc, ci, nio, nir, fresh_o, fresh_r;

	if (!npuep_pf_present()) {
		if (sb != NULL) sbuf_cat(sb, "pf is not loaded\n");
		return (ENXIO);
	}
	/* Find the candidate by its microflow id, and copy it. */
	FLOW_LOCK(sc);
	for (i = 0, d = -1; i < NPUFLOW_CAND_MAX; i++)
		if (sc->cand[i].frames != 0 && sc->cand[i].id == id) { d = i; break; }
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
	npuflow_tuples(&st, &c.tuple, tup);
	d = st.original ? 0 : 1;	/* which direction the found candidate is */

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
	err = npuep_nhop_resolve(tup[0].dip, -1, &nho);
	if (err != 0) {
		if (sb != NULL) sbuf_printf(sb, "the original direction's next hop did not resolve (%d)\n", err);
		return (err);
	}
	err = npuep_nhop_resolve(st.orig_src, lan_port, &nhr);
	if (err != 0) {
		if (sb != NULL) sbuf_printf(sb, "the reply direction's next hop did not resolve (%d) - "
		    "the opener may have left the ARP cache\n", err);
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
		if (sb != NULL) sbuf_cat(sb, "the flow table is full\n");
		return (ENOSPC);
	}
	nio = npuflow_nhop_get(sc, &nho, &nho_rev, &fresh_o);
	nir = (nio >= 0) ? npuflow_nhop_get(sc, &nhr, &nhr_rev, &fresh_r) : -1;
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
		err = npuflow_nhop_program(nio, nho_rev, &nho);
	if (err == 0 && fresh_r)
		err = npuflow_nhop_program(nir, nhr_rev, &nhr);
	if (err != 0) {
		FLOW_LOCK(sc);
		npuflow_nhop_put(sc, nio);
		npuflow_nhop_put(sc, nir);
		npuflow_conn_free(sc, ci);
		FLOW_UNLOCK(sc);
		if (sb != NULL) sbuf_printf(sb, "NHOP_PROGRAM failed (%d)\n", err);
		return (err);
	}

	/* The connection, with the translation read off pf's two keys. */
	memset(req, 0, sizeof(req));
	le32enc(req + 0, (uint32_t)ci);
	flags = (conn_rev & 0xffffu) |
	    ((uint32_t)NPURPC_CONN_VERDICT_CUT_THRU << 21) |
	    ((uint32_t)NPURPC_CONN_STATE_VALID << 30);
	if (st.nat_snat) flags |= NPURPC_CONN_FLAG_SNAT;
	if (st.nat_dnat) flags |= NPURPC_CONN_FLAG_DNAT;
	le32enc(req + 4, flags);
	n = req + NPURPC_FLOW_OFF_CONN_NAT;
	le32enc(n + 0, st.orig_src);  le32enc(n + 4, st.orig_dst);
	le32enc(n + 8, st.nat_src);   le32enc(n + 12, st.nat_dst);
	le16enc(n + 16, st.orig_dport); le16enc(n + 18, st.orig_sport);
	le16enc(n + 20, st.nat_dport);  le16enc(n + 22, st.nat_sport);

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
	if (sb != NULL)
		sbuf_printf(sb, "accelerated connection %d: original mflow %u out next hop %u (iface %u "
		    "%02x:%02x:%02x:%02x:%02x:%02x), reply mflow %u out next hop %u; %s%s%s\n",
		    ci, oid, nio, nho.iface, nho.dmac[0], nho.dmac[1], nho.dmac[2], nho.dmac[3],
		    nho.dmac[4], nho.dmac[5], rid, nir,
		    st.nat_snat ? "snat" : "", st.nat_snat && st.nat_dnat ? "+" : "",
		    st.nat_dnat ? "dnat" : "");
	return (0);
}

/*
 * Take one flow back: give its table indices up and invalidate both its microflows. The slot is
 * freed first, under the lock, so nothing reuses it before the commands go out; those go out with
 * the lock dropped. Called under sc->sx.
 */
static void
npuflow_takeout(struct npuflow_softc *sc, int ci)
{
	uint32_t mf0, mf1;

	FLOW_LOCK(sc);
	if (ci < 2 || ci >= NPUFLOW_FLOW_MAX || !sc->conn[ci].used || sc->conn[ci].making) {
		FLOW_UNLOCK(sc);
		return;
	}
	mf0 = sc->conn[ci].mf_id[0];
	mf1 = sc->conn[ci].mf_id[1];
	npuflow_nhop_put(sc, (int)sc->conn[ci].nhop[0]);
	npuflow_nhop_put(sc, (int)sc->conn[ci].nhop[1]);
	npuflow_conn_free(sc, ci);
	FLOW_UNLOCK(sc);

	npuflow_mflow_invalidate(mf0);
	npuflow_mflow_invalidate(mf1);
	sc->taken++;
}

/* Take every live flow back. Caller holds sc->sx. Returns how many. */
static int
npuflow_takeout_all(struct npuflow_softc *sc)
{
	int i, live, n = 0;

	for (i = 2; i < NPUFLOW_FLOW_MAX; i++) {
		FLOW_LOCK(sc);
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
 * invalidating them again would be a command per flow for nothing. Only the local indices are
 * freed. Caller holds sc->sx.
 */
static void
npuflow_forget_all(struct npuflow_softc *sc)
{
	int i;

	FLOW_LOCK(sc);
	for (i = 2; i < NPUFLOW_FLOW_MAX; i++) {
		if (!sc->conn[i].used)
			continue;
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
 * The keepalive: a flow lives only while pf still holds its state. A connection in hardware sends
 * the host nothing, so without this a flow would forward sixty seconds past the firewall finishing
 * with it, and a reused five-tuple inside that window would go on the strength of a connection that
 * is gone. pf is asked the cheap way, by the tuple and the list that found the state. Under sc->sx,
 * inside curvnet. A pure-hardware TCP flow does not idle out of pf (its established timer is hours),
 * so this does not churn a steady download; a short-lived UDP flow it may, and that is correct.
 */
static void
npuflow_keepalive(struct npuflow_softc *sc)
{
	struct npuep_pf_tuple t;
	int i, pf_dir, gone;

	if (!npuep_pf_present())
		return;
	for (i = 2; i < NPUFLOW_FLOW_MAX; i++) {
		FLOW_LOCK(sc);
		if (!sc->conn[i].used || sc->conn[i].making) {
			FLOW_UNLOCK(sc);
			continue;
		}
		t = sc->conn[i].tuple[sc->conn[i].probe_dir];
		pf_dir = sc->conn[i].pf_dir;
		FLOW_UNLOCK(sc);
		gone = !npuep_pf_state_exists(&t, pf_dir, NULL);
		if (gone) {
			npuflow_takeout(sc, i);
			sc->auto_gone++;
		}
	}
}

/*
 * The automatic make: every candidate the last sweep found settled and resolved, and that is not
 * already a flow, is handed to the fast path - up to a few a sweep, the rest next time. make itself
 * re-reads pf and re-resolves, so a candidate that has gone stale between the sweep and here is
 * refused there and simply not counted. Once a flow is made its frames stop being punted, so the
 * candidate ages out and is not tried again. Under sc->sx, inside curvnet.
 */
static void
npuflow_automake(struct npuflow_softc *sc)
{
	uint32_t ids[NPUFLOW_PER_SWEEP];
	int i, nids = 0;

	FLOW_LOCK(sc);
	for (i = 0; i < NPUFLOW_CAND_MAX && nids < NPUFLOW_PER_SWEEP; i++) {
		struct npuep_flow_cand *c = &sc->cand[i];

		if (c->frames == 0 || !c->pf_settled || !c->nh_ok)
			continue;
		if (npuflow_conn_find(sc, &c->tuple) >= 0)
			continue;
		ids[nids++] = c->id;
	}
	FLOW_UNLOCK(sc);
	for (i = 0; i < nids; i++)
		if (npuflow_make(sc, ids[i], NULL) == 0)
			sc->auto_made++;
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
		else
			(void)npuflow_make(sc, (uint32_t)id, &sb);
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

/* dev.npuep.0.flow.flows: the connections the fast path forwards for, one a line. */
static int
npuflow_sysctl_flows(SYSCTL_HANDLER_ARGS)
{
	struct npuflow_softc *sc = arg1;
	struct npuflow_conn c;
	struct sbuf *sb;
	int i, error, n = 0;

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_printf(sb, "auto %s | connections %u next hops %u | made %ju (auto %ju) taken %ju "
	    "gone %ju | flow-full %ju nhop-full %ju | fw_rev %u bumps %ju/%ju\n",
	    sc->autom ? "on" : "off", sc->conn_used, sc->nhop_used, (uintmax_t)sc->made,
	    (uintmax_t)sc->auto_made, (uintmax_t)sc->taken, (uintmax_t)sc->auto_gone,
	    (uintmax_t)sc->flow_full, (uintmax_t)sc->nhop_full, sc->fw_rev,
	    (uintmax_t)sc->fw_rev_bumps, (uintmax_t)sc->fw_rev_bump_fail);
	for (i = 2; i < NPUFLOW_FLOW_MAX; i++) {
		FLOW_LOCK(sc);
		c = sc->conn[i];
		FLOW_UNLOCK(sc);
		if (!c.used)
			continue;
		n++;
		sbuf_printf(sb, "%3d rev %u %s%s%s %ds | orig mflow %u nhop %u %u.%u.%u.%u:%u -> "
		    "%u.%u.%u.%u:%u | reply mflow %u nhop %u %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u%s\n",
		    i, c.conn_rev, c.snat ? "snat" : "", c.snat && c.dnat ? "+" : "",
		    c.dnat ? "dnat" : (c.snat ? "" : "plain"), (ticks - c.when) / hz,
		    c.mf_id[0], c.nhop[0],
		    c.tuple[0].sip & 0xff, (c.tuple[0].sip >> 8) & 0xff, (c.tuple[0].sip >> 16) & 0xff,
		    (c.tuple[0].sip >> 24) & 0xff, ntohs(c.tuple[0].sport),
		    c.tuple[0].dip & 0xff, (c.tuple[0].dip >> 8) & 0xff, (c.tuple[0].dip >> 16) & 0xff,
		    (c.tuple[0].dip >> 24) & 0xff, ntohs(c.tuple[0].dport),
		    c.mf_id[1], c.nhop[1],
		    c.tuple[1].sip & 0xff, (c.tuple[1].sip >> 8) & 0xff, (c.tuple[1].sip >> 16) & 0xff,
		    (c.tuple[1].sip >> 24) & 0xff, ntohs(c.tuple[1].sport),
		    c.tuple[1].dip & 0xff, (c.tuple[1].dip >> 8) & 0xff, (c.tuple[1].dip >> 16) & 0xff,
		    (c.tuple[1].dip >> 24) & 0xff, ntohs(c.tuple[1].dport),
		    c.making ? " (being made)" : "");
	}
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
	int i, error, n = 0;

	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_printf(sb, "pushed %ju replaced %ju seen again %ju | pf %s\n",
	    (uintmax_t)sc->pushed, (uintmax_t)sc->replaced, (uintmax_t)sc->seen_again,
	    npuep_pf_present() ? "present" : "not found");
	for (i = 0; i < NPUFLOW_CAND_MAX; i++) {
		FLOW_LOCK(sc);
		c = sc->cand[i];
		FLOW_UNLOCK(sc);
		if (c.frames == 0)
			continue;
		n++;
		sbuf_printf(sb, "%2d id %u rev %u port %d tag 0x%04x frames %u %ds ago | proto %u "
		    "%u.%u.%u.%u:%u -> %u.%u.%u.%u:%u | pf %s%s",
		    i, c.id, c.rev, c.port, c.tag, c.frames, (ticks - c.when) / hz, c.tuple.proto,
		    c.tuple.sip & 0xff, (c.tuple.sip >> 8) & 0xff, (c.tuple.sip >> 16) & 0xff,
		    (c.tuple.sip >> 24) & 0xff, ntohs(c.tuple.sport),
		    c.tuple.dip & 0xff, (c.tuple.dip >> 8) & 0xff, (c.tuple.dip >> 16) & 0xff,
		    (c.tuple.dip >> 24) & 0xff, ntohs(c.tuple.dport),
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
	taskqueue_start_threads(&sc->tq, 1, PI_NET, "npuflow");
	sc->period = hz;
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
		SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "auto",
		    CTLFLAG_RW, &sc->autom, 0, "let the sweep accelerate settled, resolved candidates "
		    "on its own and take each back when pf lets go of its state");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flows",
		    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, npuflow_sysctl_flows,
		    "A", "the connections the fast path forwards for, with their microflows and next hops");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_pushed", CTLFLAG_RD,
		    &sc->pushed, 0, "candidates made from punted frames");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_replaced", CTLFLAG_RD,
		    &sc->replaced, 0, "candidates pushed out by newer ones");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "auto_made", CTLFLAG_RD,
		    &sc->auto_made, 0, "flows the sweep accelerated without being asked");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "auto_gone", CTLFLAG_RD,
		    &sc->auto_gone, 0, "flows taken back because pf no longer held their state");
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
	/*
	 * The sweep is drained, so nothing else is making flows; take back what is in hardware before
	 * the table goes away, or the fast path forwards on connections whose host is gone. A flow
	 * made by hand needs this as much as one the sweep made.
	 */
	sx_xlock(&sc->sx);
	(void)npuflow_takeout_all(sc);
	sx_xunlock(&sc->sx);
	taskqueue_free(sc->tq);
	sx_destroy(&sc->sx);
	mtx_destroy(&sc->mtx);
	free(sc, M_DEVBUF);
}
