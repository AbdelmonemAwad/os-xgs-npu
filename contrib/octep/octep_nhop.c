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
 * Returns 0 and fills *out on success. The failures are all ordinary and all worth telling apart,
 * because each one means something different about what to do next: no route at all, a route out of
 * an interface this driver does not own, or a neighbour whose address is not known yet - and the
 * last of those fixes itself, because arpresolve sends the request on the way out.
 */
int
octep_nhop_resolve(struct octep_softc *sc, uint32_t dst, struct octep_nhop *out)
{
	struct sockaddr_in sin;
	struct nhop_object *nh;
	struct octep_dp_if *dif;
	struct ifnet *ifp;
	uint32_t flags;
	int i, is_gw, error;

	bzero(out, sizeof(*out));

	/*
	 * fib 0, which is the only one this appliance uses. A box with more than one routing table
	 * would want the fib the connection belongs to, and pf knows it - but asking for a fib that
	 * does not exist is worse than not asking, so this says plainly which one it used.
	 */
	nh = fib4_lookup(0, (struct in_addr){ .s_addr = dst }, 0, NHR_NONE, 0);
	if (nh == NULL)
		return (EHOSTUNREACH);

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
	if (dif == NULL)
		return (ENETUNREACH);
	if (dif->lif_iface == OCTEP_DP_IF_PORT_AUTO)
		return (ENETUNREACH);

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
		/*
		 * EWOULDBLOCK is the ordinary case for a neighbour that has not been asked yet, and
		 * arpresolve has just asked. It is worth distinguishing, because the right response
		 * is to try again shortly rather than to decide the flow cannot be accelerated.
		 */
		return (error);
	}

	memcpy(out->smac, if_getlladdr(dif->ifp), ETHER_ADDR_LEN);
	out->iface = dif->lif_iface;
	out->mtu = (uint16_t)if_getmtu(dif->ifp);
	out->ifname_unit = i;

	return (0);
}
