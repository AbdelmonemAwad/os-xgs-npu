/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * Where a flow's frames should be sent, asked of the host's own routing table.
 *
 * The last thing a flow needs that the driver did not have. The slot comes from the punted frame's
 * metadata, the translation from pf's two keys, the verdict from pf's state - and the next hop is a
 * destination MAC and an egress front port, which are a route lookup and an address resolution and
 * nothing more exotic. The host already knows both, because it is routing this very connection.
 *
 * WHY NOT A SECOND TABLE. The obvious alternative is to keep our own map of address to MAC, filled
 * from the frames we see. It would be wrong within seconds of a neighbour changing and would be
 * wrong in exactly the way that is hardest to notice - traffic to one host going to another. The
 * kernel's table is the one the rest of the system acts on; asking it is the only way to be wrong
 * at the same time as everything else.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <machine/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <net/route.h>
#include <net/vnet.h>
#include <net/route/nhop.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/in_fib.h>
#include <netinet/ip6.h>
#include <netinet6/in6_var.h>
#include <netinet6/in6_fib.h>
#include <netinet6/nd6.h>
#include <net/if_llatbl.h>
#include <net/if_vlan_var.h>

#include "octep.h"

/*
 * The front port and the VLAN under a vlan(4) interface (discovery 10): the dp_if index of its parent
 * and its tag, or -1 when it is not a VLAN of one of this driver's ports - or when VLAN connections
 * are not made (dp.vlan_flows 0), which leaves such a route as it was, with the host. In the net
 * epoch, as the route is.
 */
static int
octep_nhop_vlan_port(struct octep_softc *sc, struct ifnet *ifp, uint16_t *vlan)
{
	struct ifnet *parent;
	int i;

	if (sc->dp_vlan_flows == 0 || if_gettype((if_t)ifp) != IFT_L2VLAN ||
	    vlan_trunkdev_p == NULL || vlan_tag_p == NULL)
		return (-1);
	parent = VLAN_TRUNKDEV(ifp);
	if (parent == NULL || VLAN_TAG(ifp, vlan) != 0 || *vlan == 0)
		return (-1);
	for (i = 0; i < OCTEP_DP_IF_MAX; i++)
		if (sc->dp_if[i].ifp != NULL && (struct ifnet *)sc->dp_if[i].ifp == parent)
			return (i);
	return (-1);
}

/*
 * Whether a next hop can be found on this interface by ARP or neighbour discovery: a port, a VLAN, a
 * bridge or a lagg - Ethernet underneath. A tunnel's interface (a VTI's ipsecN, gif, wg) is not, nor
 * is lo0: asked for a neighbour, a tunnel sends the request into itself, and the ARMADA driver's sweep
 * panicked in m_copydata under ipsec_transmit on an XGS 126 whose gateway named a VTI and an address
 * on it (#792). The same hint-port fallback is here, so the same refusal, before it.
 */
static int
octep_nhop_ether(struct ifnet *ifp)
{

	switch (if_gettype((if_t)ifp)) {
	case IFT_ETHER:
	case IFT_L2VLAN:
	case IFT_BRIDGE:
	case IFT_IEEE8023ADLAG:
		return (1);
	default:
		return (0);
	}
}

/*
 * Resolve one destination to a front port and a MAC.
 *
 * dst is in network order, as everything else in this driver's flow path is.
 *
 * hint_dif is a front port to fall back on when the route leaves by an interface this driver does
 * not own, or -1 for none. It exists for one case and should be used for no other: a destination on
 * this appliance's own LAN routes to the bridge its front ports are members of, and the bridge
 * cannot be asked which member holds the address. The caller passes the port the flow's own frames
 * arrive on - see struct octep_flow, which holds it and says what it cannot see.
 *
 * Returns 0 and fills *out on success. The failures are all ordinary and all worth telling apart,
 * because each one means something different about what to do next: no route at all, a route out of
 * an interface this driver does not own and no usable hint, or a neighbour whose address is not
 * known yet - and the last of those fixes itself, because arpresolve sends the request on the way
 * out.
 */
int
octep_nhop_resolve(struct octep_softc *sc, uint32_t dst, int hint_dif,
    struct octep_nhop *out)
{
	struct sockaddr_in sin;
	struct epoch_tracker et;
	struct nhop_object *nh;
	struct octep_dp_if *dif;
	struct ifnet *ifp;
	uint32_t flags;
	int i, is_gw, error;

	bzero(out, sizeof(*out));

	/*
	 * The net epoch, and this is not optional.
	 *
	 * fib4_lookup returns a nhop_object borrowed from the routing table and valid only while
	 * the epoch is held - the caller is expected to be inside it, which every in-tree caller
	 * is, because they are all on a packet path that entered it long before. This driver's
	 * caller is a taskqueue, which is not. Calling without it reads a route that may be freed
	 * underneath, and the appliance stopped responding the first time this ran.
	 */
	NET_EPOCH_ENTER(et);

	/*
	 * fib 0, which is the only one this appliance uses. A box with more than one routing table
	 * would want the fib the connection belongs to, and pf knows it - but asking for a fib that
	 * does not exist is worse than not asking, so this says plainly which one it used.
	 */
	nh = fib4_lookup(0, (struct in_addr){ .s_addr = dst }, 0, NHR_NONE, 0);
	if (nh == NULL) {
		NET_EPOCH_EXIT(et);
		return (EHOSTUNREACH);
	}

	ifp = nh->nh_ifp;
	if (!octep_nhop_ether(ifp)) {
		NET_EPOCH_EXIT(et);
		return (ENETUNREACH);
	}
	is_gw = (nh->nh_flags & NHF_GATEWAY) != 0;

	/*
	 * Which of our front ports that interface is.
	 *
	 * A route out of igb0 or lo0 resolves perfectly well and is no use here: the coprocessor can
	 * only send out of a port it owns. Saying so is better than guessing a port.
	 */
	dif = NULL;
	for (i = 0; i < OCTEP_DP_IF_MAX; i++) {
		if (sc->dp_if[i].ifp == NULL)
			continue;
		if ((struct ifnet *)sc->dp_if[i].ifp == ifp) {
			dif = &sc->dp_if[i];
			break;
		}
	}

	/*
	 * A VLAN subinterface of a front port (discovery 10): its frames leave by the port, tagged. The
	 * address, the MAC and the MTU below stay the VLAN interface's, which the route chose.
	 */
	if (dif == NULL && (i = octep_nhop_vlan_port(sc, ifp, &out->vlan)) >= 0)
		dif = &sc->dp_if[i];

	/*
	 * The bridge, and the only reason this function takes a hint.
	 *
	 * The route is right and names an interface that is not a port: eleven of this appliance's
	 * front ports are members of one bridge, and a route to the LAN leaves by the bridge. The
	 * address resolution below still works - the ARP entry for that machine lives on the bridge,
	 * which is the L3 interface the rest of the system routes through - so the one thing missing
	 * is the port, and the caller has it.
	 *
	 * The link is checked because a port with no link cannot be where the machine is, whatever
	 * was recorded. It costs one load and turns a moved cable from silent misdelivery into an
	 * ordinary refusal.
	 */
	if (dif == NULL && hint_dif >= 0 && hint_dif < OCTEP_DP_IF_MAX &&
	    sc->dp_if[hint_dif].ifp != NULL &&
	    sc->dp_if[hint_dif].lif_iface != OCTEP_DP_IF_PORT_AUTO &&
	    sc->dp_if[hint_dif].link == 1) {
		dif = &sc->dp_if[hint_dif];
		i = hint_dif;
		out->from_flow = 1;
	}

	if (dif == NULL || dif->lif_iface == OCTEP_DP_IF_PORT_AUTO) {
		NET_EPOCH_EXIT(et);
		return (ENETUNREACH);
	}

	/*
	 * The address to resolve is the gateway's when there is one and the destination's when the
	 * destination is on the wire - which is what is_gw says and what arpresolve wants told.
	 */
	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(sin);
	if (is_gw && nh->gw_sa.sa_family == AF_INET)
		sin.sin_addr = nh->gw4_sa.sin_addr;
	else
		sin.sin_addr.s_addr = dst;

	flags = 0;
	/*
	 * With a valid entry arpresolve copies the whole prepared header - fourteen bytes, the type
	 * as well - so it writes past dmac over smac and the first bytes of iface. Harmless here only
	 * because both are written below, after it; the XGS 136's driver filled iface first and every
	 * one of its next hops named interface 8 (issue 483). Keep them after the call.
	 */
	error = arpresolve(ifp, is_gw, NULL, (struct sockaddr *)&sin, out->dmac, &flags, NULL);
	if (error != 0) {
		NET_EPOCH_EXIT(et);
		/*
		 * EWOULDBLOCK is the ordinary case for a neighbour that has not been asked yet, and
		 * arpresolve has just asked. It is worth distinguishing, because the right response
		 * is to try again shortly rather than to decide the flow cannot be accelerated.
		 */
		return (error);
	}

	/*
	 * The source MAC and the MTU belong to the interface the route chose, not to the port the
	 * frame leaves by. For every route out of a front port those are the same interface and it
	 * makes no difference; for a route out of a bridge it is the whole difference, because the
	 * machine on the other end has the bridge's address in its ARP cache for its gateway and a
	 * frame from the member port's own address is one it was never told to expect.
	 */
	memcpy(out->smac, if_getlladdr((if_t)ifp), ETHER_ADDR_LEN);
	out->iface = dif->lif_iface;
	out->mtu = (uint16_t)if_getmtu((if_t)ifp);
	out->ifname_unit = i;

	NET_EPOCH_EXIT(et);
	return (0);
}

/*
 * The same for an IPv6 destination - the far end of a tunnel whose outer header is IPv6, which is
 * the only caller: the flow path is IPv4. dst6 is the 16 bytes of the address as on the wire. The
 * route comes from fib6_lookup and the neighbour from nd6_resolve, which sends the solicitation
 * on the way out exactly as arpresolve sends the request, and answers EWOULDBLOCK until the
 * neighbour has replied. No hint: the association's own interface is checked by the caller.
 */
int
octep_nhop_resolve6(struct octep_softc *sc, const uint8_t *dst6, int hint_dif,
    struct octep_nhop *out)
{
	struct sockaddr_in6 sin6;
	struct in6_addr d6;
	struct epoch_tracker et;
	struct nhop_object *nh;
	struct octep_dp_if *dif;
	struct ifnet *ifp;
	uint32_t flags;
	int i, is_gw, error;

	bzero(out, sizeof(*out));
	memcpy(&d6, dst6, sizeof(d6));
	NET_EPOCH_ENTER(et);
	nh = fib6_lookup(0, &d6, 0, NHR_NONE, 0);
	if (nh == NULL) {
		NET_EPOCH_EXIT(et);
		return (EHOSTUNREACH);
	}
	ifp = nh->nh_ifp;
	if (!octep_nhop_ether(ifp)) {
		NET_EPOCH_EXIT(et);
		return (ENETUNREACH);
	}
	is_gw = (nh->nh_flags & NHF_GATEWAY) != 0;
	dif = NULL;
	for (i = 0; i < OCTEP_DP_IF_MAX; i++) {
		if (sc->dp_if[i].ifp == NULL)
			continue;
		if ((struct ifnet *)sc->dp_if[i].ifp == ifp) {
			dif = &sc->dp_if[i];
			break;
		}
	}
	/* a VLAN subinterface of a front port, as octep_nhop_resolve takes it */
	if (dif == NULL && (i = octep_nhop_vlan_port(sc, ifp, &out->vlan)) >= 0)
		dif = &sc->dp_if[i];
	if (dif == NULL && hint_dif >= 0 && hint_dif < OCTEP_DP_IF_MAX &&
	    sc->dp_if[hint_dif].ifp != NULL &&
	    sc->dp_if[hint_dif].lif_iface != OCTEP_DP_IF_PORT_AUTO &&
	    sc->dp_if[hint_dif].link == 1) {
		dif = &sc->dp_if[hint_dif];
		i = hint_dif;
		out->from_flow = 1;
	}
	if (dif == NULL || dif->lif_iface == OCTEP_DP_IF_PORT_AUTO) {
		NET_EPOCH_EXIT(et);
		return (ENETUNREACH);
	}
	bzero(&sin6, sizeof(sin6));
	sin6.sin6_family = AF_INET6;
	sin6.sin6_len = sizeof(sin6);
	if (is_gw && nh->gw_sa.sa_family == AF_INET6)
		sin6.sin6_addr = nh->gw6_sa.sin6_addr;
	else
		sin6.sin6_addr = d6;
	flags = 0;
	/*
	 * The second argument carries the address family in its upper half (LLE_SF, as ether_output
	 * passes it): with a plain is_gw the lookup is made for family 0, finds nothing, and answers
	 * EWOULDBLOCK for ever while ndp shows the neighbour reachable - measured 2026-10-12, five
	 * pings, five refusals, out_nonhop 5.
	 */
	error = nd6_resolve(ifp, LLE_SF(AF_INET6, is_gw), NULL, (struct sockaddr *)&sin6, out->dmac,
	    &flags, NULL);
	if (error != 0) {
		NET_EPOCH_EXIT(et);
		return (error);
	}
	memcpy(out->smac, if_getlladdr((if_t)ifp), ETHER_ADDR_LEN);
	out->iface = dif->lif_iface;
	out->mtu = (uint16_t)if_getmtu((if_t)ifp);
	out->ifname_unit = i;
	NET_EPOCH_EXIT(et);
	return (0);
}

/*
 * The next-hop table: one far-side entry per way of leaving, shared by every connection that
 * leaves that way.
 *
 * This is NOT the table the comment above warns against. That one would decide where a frame
 * goes; this one only remembers what the host's own route and ARP answered, keyed by the whole
 * answer - egress interface, neighbour, our address there, MTU - so that two connections to the
 * same neighbour out of the same port program one entry and not two. A changed answer is a
 * different key and gets a different entry; the old one is freed when its last connection goes.
 *
 * Indices start at 2 for the reason the connection table's do. Each index keeps its revision
 * across reuse, as the vendor's host keeps one in its freelist, because a microflow carries the
 * next hop's revision beside its index and the far side checks it.
 *
 * Called with the softc lock held; a miss posts NHOP_PROGRAM from inside it.
 */
int
octep_nhop_get(struct octep_softc *sc, const struct octep_nhop *nh, uint32_t *idx)
{
	struct octep_nhop_ent *e;
	uint32_t i, free_i;
	int err;

	mtx_assert(&sc->mtx, MA_OWNED);
	free_i = 0;
	/*
	 * Only as far as the highest index ever handed out: the table is the board's 65,536 next
	 * hops, a firewall uses a handful, and this runs for every connection that is made.
	 */
	if (sc->dp_nhop_hi < 2)
		sc->dp_nhop_hi = 2;
	for (i = 2; i < sc->dp_nhop_hi && i < sc->dp_nhop_max && i < OCTEP_NHOP_MAX; i++) {
		e = &sc->dp_nhop[i];
		if (!e->used) {
			if (free_i == 0)
				free_i = i;
			continue;
		}
		if (e->iface == nh->iface && e->mtu == nh->mtu && e->vlan == nh->vlan &&
		    memcmp(e->dmac, nh->dmac, ETHER_ADDR_LEN) == 0 &&
		    memcmp(e->smac, nh->smac, ETHER_ADDR_LEN) == 0) {
			e->refcnt++;
			sc->dp_nhop_shared++;
			*idx = i;
			return (0);
		}
	}
	if (free_i == 0 && sc->dp_nhop_hi < sc->dp_nhop_max && sc->dp_nhop_hi < OCTEP_NHOP_MAX)
		free_i = sc->dp_nhop_hi;
	if (free_i == 0) {
		sc->dp_nhop_full++;
		return (ENOSPC);
	}
	e = &sc->dp_nhop[free_i];
	e->rev = (uint8_t)(e->rev + 1);
	if (e->rev == 0)
		e->rev = 1;
	err = octep_rpc_nhop_program(sc, free_i, e->rev, nh);
	if (err != 0)
		return (err);
	e->used = 1;
	e->refcnt = 1;
	if (free_i >= sc->dp_nhop_hi)
		sc->dp_nhop_hi = free_i + 1;
	e->iface = nh->iface;
	e->mtu = nh->mtu;
	e->vlan = nh->vlan;
	memcpy(e->dmac, nh->dmac, ETHER_ADDR_LEN);
	memcpy(e->smac, nh->smac, ETHER_ADDR_LEN);
	sc->dp_nhop_used++;
	*idx = free_i;
	return (0);
}

/*
 * One connection fewer names this next hop. At zero the host's slot is freed; the far side's entry
 * is left in place, named by nothing, and overwritten with the next revision when the index is
 * reused. Called with the softc lock held.
 */
void
octep_nhop_put(struct octep_softc *sc, uint32_t idx)
{
	struct octep_nhop_ent *e;

	mtx_assert(&sc->mtx, MA_OWNED);
	if (idx < 2 || idx >= OCTEP_NHOP_MAX)
		return;
	e = &sc->dp_nhop[idx];
	if (!e->used)
		return;
	if (e->refcnt > 0)
		e->refcnt--;
	if (e->refcnt == 0) {
		e->used = 0;
		if (sc->dp_nhop_used > 0)
			sc->dp_nhop_used--;
	}
}
