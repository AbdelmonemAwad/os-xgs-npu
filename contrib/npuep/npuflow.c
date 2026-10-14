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
};

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
