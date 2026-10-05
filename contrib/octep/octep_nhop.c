/*-
 * SPDX-License-Identifier: BSD-2-Clause
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

#include "octep.h"

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
