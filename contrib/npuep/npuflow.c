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

struct npuflow_softc {
	struct mtx		 mtx;
	device_t		 dev;
	struct npuep_flow_cand	 cand[NPUFLOW_CAND_MAX];
	uint32_t		 next;		/* the slot the next new candidate takes */
	uint64_t		 pushed, replaced, seen_again;
	struct taskqueue	*tq;
	struct timeout_task	 task;
	int			 period;	/* ticks between sweeps; 0 stops them */
	int			 detaching;
	/*
	 * One accelerated flow at a time: flow.accelerate turns a candidate into a FLOW_CREATE_FP
	 * and keeps what it needs to take it back. A proof, not the sweep making flows by itself -
	 * that is a later slice. The fixed table indices are the vendor's reserved-for-reuse range
	 * (next hop and connection both start at 2); one flow uses two next hops and one connection.
	 */
	uint8_t			 accel_rev;	/* bumped each time so the far side sees a fresh entry */
	uint32_t		 accel_mf_o, accel_mf_r;	/* the two microflow ids, for deaccelerate */
	int			 accel_on;
	char			 accel_result[640];
};

#define	NPUFLOW_NHOP_O		2	/* the forward direction's next-hop index */
#define	NPUFLOW_NHOP_R		3	/* the reply direction's */
#define	NPUFLOW_CONN_IDX	2	/* the one connection */

static struct npuflow_softc *npuflow_sc;

#define	FLOW_LOCK(sc)	mtx_lock(&(sc)->mtx)
#define	FLOW_UNLOCK(sc)	mtx_unlock(&(sc)->mtx)

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
    uint32_t nhop_rev, uint32_t conn_idx, uint32_t conn_rev, uint32_t timeout)
{

	le32enc(p + 0, (id & 0x01ffffffu) | ((rev & 0x3fu) << 25) | (1u << 31));
	le32enc(p + 4, ((uint32_t)NPURPC_MFLOW_ACTION_FWD << 16) | ((dir & 1u) << 23) |
	    ((uint32_t)NPURPC_BRCTL_ROUTED << 24) | ((uint32_t)NPURPC_MFLOW_STATE_ACTIVE << 28));
	le32enc(p + 8, 0);
	le32enc(p + 12, conn_idx);
	le32enc(p + 16, (conn_rev & 0xffffu) << 16);	/* fw_state_rev 0; conn_rev in the high half */
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
 * Turn one candidate into a flow the fast path forwards itself: resolve both directions' next
 * hops, program them, and send one FLOW_CREATE_FP carrying the connection and both microflows.
 * By hand through dev.npuep.0.flow.accelerate - a proof that the whole chain works on this
 * family, not the sweep making flows on its own. One flow at a time; a second call replaces it.
 */
static void
npuflow_accelerate(struct npuflow_softc *sc, uint32_t id, struct sbuf *sb)
{
	struct npuep_flow_cand c, r;
	struct npuep_pf_state st;
	struct npuep_pf_tuple tup[2];
	struct npuep_nhop nho, nhr;
	uint8_t req[NPURPC_FLOW_REQ_LEN], *n;
	uint32_t flags, oid, orev, rid, rrev;
	int i, d, ri, lan_port, err, rc;

	if (!npuep_pf_present()) {
		sbuf_cat(sb, "pf is not loaded\n");
		return;
	}
	/* Find the candidate by its microflow id, and copy it. */
	FLOW_LOCK(sc);
	for (i = 0, d = -1; i < NPUFLOW_CAND_MAX; i++)
		if (sc->cand[i].frames != 0 && sc->cand[i].id == id) { d = i; break; }
	if (d < 0) { FLOW_UNLOCK(sc); sbuf_printf(sb, "no candidate with id %u\n", id); return; }
	c = sc->cand[d];
	FLOW_UNLOCK(sc);

	if (!npuep_pf_state_read(&c.tuple, &st) || !npuep_pf_settled(&c.tuple)) {
		sbuf_cat(sb, "pf has no settled state for that flow\n");
		return;
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
		sbuf_cat(sb, "the reply direction has not been seen yet - send a little traffic "
		    "back the other way and try again\n");
		return;
	}

	/* The original direction's id is whichever candidate is direction 0. */
	oid = (d == 0) ? c.id : r.id;  orev = (d == 0) ? c.rev : r.rev;
	rid = (d == 0) ? r.id : c.id;  rrev = (d == 0) ? r.rev : c.rev;
	lan_port = (d == 0) ? c.port : r.port;	/* the port the original flow entered on */

	/* Both next hops: the original out toward the destination, the reply back to the opener. */
	err = npuep_nhop_resolve(tup[0].dip, -1, &nho);
	if (err != 0) { sbuf_printf(sb, "the original direction's next hop did not resolve (%d)\n", err); return; }
	err = npuep_nhop_resolve(st.orig_src, lan_port, &nhr);
	if (err != 0) { sbuf_printf(sb, "the reply direction's next hop did not resolve (%d) - the opener may have left the ARP cache\n", err); return; }

	sc->accel_rev++;
	if (sc->accel_rev == 0) sc->accel_rev = 1;
	if ((err = npuflow_nhop_program(NPUFLOW_NHOP_O, sc->accel_rev, &nho)) != 0 ||
	    (err = npuflow_nhop_program(NPUFLOW_NHOP_R, sc->accel_rev, &nhr)) != 0) {
		sbuf_printf(sb, "NHOP_PROGRAM failed (%d)\n", err);
		return;
	}

	/* The connection, with the translation read off pf's two keys. */
	memset(req, 0, sizeof(req));
	le32enc(req + 0, NPUFLOW_CONN_IDX);
	flags = (sc->accel_rev & 0xffffu) |
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

	/* The two microflows: original on next hop O, reply on next hop R, both active and forwarding. */
	le32enc(req + NPURPC_FLOW_OFF_VALID, NPURPC_FLOW_VALID_ORIG | NPURPC_FLOW_VALID_REPLY);
	npuflow_put_mflow(req + NPURPC_FLOW_OFF_MFLOW_O, oid, orev, 0, NPUFLOW_NHOP_O,
	    sc->accel_rev, NPUFLOW_CONN_IDX, sc->accel_rev, 60);
	npuflow_put_mflow(req + NPURPC_FLOW_OFF_MFLOW_R, rid, rrev, 1, NPUFLOW_NHOP_R,
	    sc->accel_rev, NPUFLOW_CONN_IDX, sc->accel_rev, 60);

	i = npurpc_send(RPC_CMD_FLOW_CREATE_FP, req, sizeof(req), NULL, 0, &rc);
	if (i < 0) { sbuf_printf(sb, "FLOW_CREATE_FP transport failed (%d)\n", -i); return; }
	if (rc != 0) { sbuf_printf(sb, "the coprocessor refused FLOW_CREATE_FP, rc 0x%x\n", rc); return; }

	sc->accel_mf_o = oid; sc->accel_mf_r = rid; sc->accel_on = 1;
	sbuf_printf(sb, "accelerated: original mflow %u out next hop %u (iface %u %02x:%02x:%02x:%02x:%02x:%02x), "
	    "reply mflow %u out next hop %u; %s%s%s connection %u\n",
	    oid, NPUFLOW_NHOP_O, nho.iface, nho.dmac[0], nho.dmac[1], nho.dmac[2], nho.dmac[3],
	    nho.dmac[4], nho.dmac[5], rid, NPUFLOW_NHOP_R,
	    st.nat_snat ? "snat" : "", st.nat_snat && st.nat_dnat ? "+" : "", st.nat_dnat ? "dnat" : "",
	    (unsigned)NPUFLOW_CONN_IDX);
}

/* Take the accelerated flow's action away again (MFLOW_INVALIDATE both directions). */
static void
npuflow_deaccelerate(struct npuflow_softc *sc, struct sbuf *sb)
{
	uint8_t pl[20];
	int rc, k;

	if (!sc->accel_on) { sbuf_cat(sb, "no flow is accelerated\n"); return; }
	for (k = 0; k < 2; k++) {
		uint32_t mf = k == 0 ? sc->accel_mf_o : sc->accel_mf_r;

		memset(pl, 0, sizeof(pl));
		le32enc(pl + 0, 0);		/* invalidation_type 0 = by the microflow the data names */
		le32enc(pl + 4, mf);
		(void)npurpc_send(RPC_CMD_MFLOW_INVALIDATE, pl, sizeof(pl), NULL, 0, &rc);
	}
	sc->accel_on = 0;
	sbuf_printf(sb, "invalidated microflows %u and %u\n", sc->accel_mf_o, sc->accel_mf_r);
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
	if (strcmp(in, "off") == 0 || strcmp(in, "0") == 0)
		npuflow_deaccelerate(sc, &sb);
	else {
		id = strtol(in, NULL, 0);
		if (id <= 0)
			sbuf_cat(&sb, "write a candidate's id to accelerate it, or \"off\"\n");
		else
			npuflow_accelerate(sc, (uint32_t)id, &sb);
	}
	sbuf_finish(&sb);
	sbuf_delete(&sb);
	return (0);
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
		    "or \"off\" to take it back; reading shows the last result");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_pushed", CTLFLAG_RD,
		    &sc->pushed, 0, "candidates made from punted frames");
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "cand_replaced", CTLFLAG_RD,
		    &sc->replaced, 0, "candidates pushed out by newer ones");
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
	taskqueue_free(sc->tq);
	mtx_destroy(&sc->mtx);
	free(sc, M_DEVBUF);
}
