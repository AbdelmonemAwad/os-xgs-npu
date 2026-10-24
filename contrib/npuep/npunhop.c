/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * Resolve one destination to a front port and a MAC, for the ARMADA driver's flows.
 *
 * The OCTEON TX driver's octep_nhop_resolve, with the front ports asked of npugiu instead of the
 * datapath's own table. Everything the comments there say holds here: the net epoch is not
 * optional, fib 0 is the only one this appliance uses, a route out of an interface this driver
 * does not own is refused with ENETUNREACH unless the caller's hint names a front port with link,
 * and EWOULDBLOCK means arpresolve has just asked the neighbour and the caller should try again.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <machine/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_private.h>
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

#include "npuep.h"
#include "npuflow.h"

/*
 * Whether a MAC is one of this host's own: an interface's address - a bridge's among them - or a
 * CARP or VRRP group's (00:00:5e:00:01 and :02), which the host answers for too. A frame addressed
 * to one of them is for the host, which routes it. The OCTEON TX driver's octep_mac_is_host.
 */
static int
npuep_mac_cb(if_t ifp, void *arg)
{
	const uint8_t *lla;

	if (if_getaddrlen(ifp) != ETHER_ADDR_LEN || (lla = (const uint8_t *)if_getlladdr(ifp)) == NULL)
		return (0);
	return (memcmp(lla, arg, ETHER_ADDR_LEN) == 0);
}

static int
npuep_mac_is_host(const uint8_t *m)
{
	struct epoch_tracker et;
	uint8_t mac[ETHER_ADDR_LEN];
	int hit;

	if (m[0] == 0x00 && m[1] == 0x00 && m[2] == 0x5e && m[3] == 0x00 &&
	    (m[4] == 0x01 || m[4] == 0x02))
		return (1);
	memcpy(mac, m, sizeof(mac));
	CURVNET_SET_QUIET(vnet0);
	NET_EPOCH_ENTER(et);
	hit = if_foreach(npuep_mac_cb, mac);
	NET_EPOCH_EXIT(et);
	CURVNET_RESTORE();
	return (hit != 0);
}

/*
 * A frame this appliance BRIDGES rather than routes (discovery 9, issue 723): it came in on a
 * member of a bridge and is addressed to another machine, not to the host. 1 for such a frame; -1
 * for one on a member addressed to a group, which the bridge sends to every port and no one
 * microflow can carry; 0 for every other frame - and for one that carried no MACs, all zero.
 */
int
npuep_frame_bridged(int port, const uint8_t *mac)
{
	struct ifnet *ifp;
	int i, any = 0;

	if (mac == NULL || port < 0 || port >= npugiu_port_count() ||
	    (ifp = (struct ifnet *)npugiu_port_ifp(port)) == NULL || ifp->if_bridge == NULL)
		return (0);
	for (i = 0; i < 12; i++)
		any |= mac[i];
	if (any == 0)
		return (0);
	if ((mac[0] & 1) != 0)
		return (-1);
	return (!npuep_mac_is_host(mac));
}

/*
 * Whether two front ports are members of one bridge: the OCTEON TX driver's octep_ports_one_bridge,
 * which says why a port's if_bridge is its own entry in its bridge and not the bridge, and why the
 * head of if_bridge.c's struct bridge_iflist is restated and checked here.
 */
struct npuep_bridge_member {	/* the head of if_bridge.c's struct bridge_iflist */
	void		*bif_next[2];	/* CK_LIST_ENTRY */
	struct ifnet	*bif_ifp;	/* the member */
	void		*bif_sc;	/* its bridge */
};

static void *
npuep_port_bridge(struct ifnet *ifp)
{
	const struct npuep_bridge_member *b = ifp->if_bridge;

	return ((b != NULL && b->bif_ifp == ifp) ? b->bif_sc : NULL);
}

int
npuep_ports_one_bridge(int a, int b)
{
	struct epoch_tracker et;
	struct ifnet *x, *y;
	void *bx, *by;
	int n = npugiu_port_count();

	if (a < 0 || b < 0 || a >= n || b >= n)
		return (0);
	x = (struct ifnet *)npugiu_port_ifp(a);
	y = (struct ifnet *)npugiu_port_ifp(b);
	if (x == NULL || y == NULL)
		return (0);
	NET_EPOCH_ENTER(et);
	bx = npuep_port_bridge(x);
	by = npuep_port_bridge(y);
	NET_EPOCH_EXIT(et);
	return (bx != NULL && bx == by);
}

/*
 * A bridged connection's next hop: out of the port the other direction's frames come in on, which is
 * where the bridge has learned the far end is, named with the frame's own two MACs - which the
 * microflow's bridge control then leaves as they are, as it leaves the TTL (NPURPC_BRCTL_BRIDGED).
 * The route plays no part.
 */
int
npuep_nhop_bridged(int out_port, const uint8_t *mac, struct npuep_nhop *out)
{

	bzero(out, sizeof(*out));
	out->port = -1;
	if (mac == NULL || out_port < 0 || out_port >= npugiu_port_count() ||
	    npugiu_port_ifp(out_port) == NULL || npugiu_port_link(out_port) != 1)
		return (ENETUNREACH);
	memcpy(out->dmac, mac, ETHER_ADDR_LEN);
	memcpy(out->smac, mac + ETHER_ADDR_LEN, ETHER_ADDR_LEN);
	out->iface = npugiu_port_iface(out_port);
	out->tag = npugiu_port_tag(out_port);
	out->mtu = (uint16_t)if_getmtu((if_t)npugiu_port_ifp(out_port));
	out->port = out_port;
	out->from_hint = 1;
	return (0);
}

int
npuep_nhop_resolve(uint32_t dst, int hint_port, struct npuep_nhop *out)
{
	struct sockaddr_in sin;
	struct epoch_tracker et;
	struct nhop_object *nh;
	struct ifnet *ifp;
	u_char lh[LLE_MAX_LINKHDR];
	uint32_t flags;
	int i, n, port, is_gw, error;

	bzero(out, sizeof(*out));
	out->port = -1;

	NET_EPOCH_ENTER(et);

	nh = fib4_lookup(0, (struct in_addr){ .s_addr = dst }, 0, NHR_NONE, 0);
	if (nh == NULL) {
		NET_EPOCH_EXIT(et);
		return (EHOSTUNREACH);
	}
	ifp = nh->nh_ifp;
	/*
	 * A destination this host owns routes by lo0, and nothing is resolved for it: it is never a
	 * flow's way out, and asking lo0 for a neighbour makes the kernel print "looutput: af=35
	 * unexpected" (AF_ARP) - once a second, for as long as such a candidate sits in the table,
	 * which is what the sweep did after a tunnelled connection to the appliance itself was punted
	 * with a microflow identity (2026-10-14). Refused here, before the hint port can stand in.
	 */
	if ((if_getflags((if_t)ifp) & IFF_LOOPBACK) != 0) {
		NET_EPOCH_EXIT(et);
		return (EHOSTUNREACH);
	}
	is_gw = (nh->nh_flags & NHF_GATEWAY) != 0;

	/* Which of the front ports the route leaves by, if any. */
	port = -1;
	n = npugiu_port_count();
	for (i = 0; i < n; i++) {
		if ((struct ifnet *)npugiu_port_ifp(i) == ifp) {
			port = i;
			break;
		}
	}

	/*
	 * The bridge: a route to the LAN leaves by the bridge the front ports are members of, and
	 * the bridge cannot be asked which member holds the address. The caller passes the port the
	 * flow's own frames arrive on, and it is taken only while that port has link.
	 */
	if (port < 0 && hint_port >= 0 && hint_port < n && npugiu_port_ifp(hint_port) != NULL &&
	    npugiu_port_link(hint_port) == 1) {
		port = hint_port;
		out->from_hint = 1;
	}
	if (port < 0) {
		NET_EPOCH_EXIT(et);
		return (ENETUNREACH);
	}

	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(sin);
	if (is_gw && nh->gw_sa.sa_family == AF_INET)
		sin.sin_addr = nh->gw4_sa.sin_addr;
	else
		sin.sin_addr.s_addr = dst;

	/*
	 * The source MAC and the MTU belong to the interface the route chose - the bridge, for a
	 * LAN destination - not to the member port the frame leaves by: the machine at the far end
	 * has the bridge's address in its ARP cache for its gateway. Filled BEFORE the neighbour is
	 * asked, so a caller told EWOULDBLOCK - the neighbour has just been asked for - still knows
	 * which port the route leaves by and its MTU, which is all the IPsec accept needs to decide.
	 */
	memcpy(out->smac, if_getlladdr((if_t)ifp), ETHER_ADDR_LEN);
	out->iface = npugiu_port_iface(port);
	out->tag = npugiu_port_tag(port);
	out->mtu = (uint16_t)if_getmtu((if_t)ifp);
	out->port = port;

	flags = 0;
	/*
	 * Into a link header's room, and only the neighbour's address out of it. With a valid entry
	 * the resolver copies the whole header it keeps prepared - destination, source and type,
	 * fourteen bytes on Ethernet - and given the six of dmac it ran on over smac and iface: every
	 * IPv4 next hop named interface 8, the first byte of 0x0800, which on the XGS 136 is an empty
	 * cage, and every connection made in hardware stopped (issue 483).
	 */
	error = arpresolve(ifp, is_gw, NULL, (struct sockaddr *)&sin, lh, &flags, NULL);
	if (error == 0)
		memcpy(out->dmac, lh, ETHER_ADDR_LEN);
	NET_EPOCH_EXIT(et);
	return (error);
}

/*
 * The same for an IPv6 destination: the route from fib6_lookup and the neighbour from nd6_resolve,
 * which solicits it on the way out as arpresolve asks, and answers EWOULDBLOCK until it has replied.
 * The address family rides in the upper half of the second argument (LLE_SF, as ether_output
 * passes it); with a plain is_gw the lookup is made for family 0 and answers EWOULDBLOCK for ever -
 * the OCTEON TX driver's octep_nhop_resolve6 records that measurement.
 */
int
npuep_nhop_resolve6(const uint32_t *dst6, int hint_port, struct npuep_nhop *out)
{
	struct sockaddr_in6 sin6;
	struct in6_addr d6;
	struct epoch_tracker et;
	struct nhop_object *nh;
	struct ifnet *ifp;
	u_char lh[LLE_MAX_LINKHDR];
	uint32_t flags;
	int i, n, port, is_gw, error;

	bzero(out, sizeof(*out));
	out->port = -1;
	memcpy(&d6, dst6, sizeof(d6));

	NET_EPOCH_ENTER(et);
	nh = fib6_lookup(0, &d6, 0, NHR_NONE, 0);
	if (nh == NULL) {
		NET_EPOCH_EXIT(et);
		return (EHOSTUNREACH);
	}
	ifp = nh->nh_ifp;
	if ((if_getflags((if_t)ifp) & IFF_LOOPBACK) != 0) {
		NET_EPOCH_EXIT(et);
		return (EHOSTUNREACH);
	}
	is_gw = (nh->nh_flags & NHF_GATEWAY) != 0;

	port = -1;
	n = npugiu_port_count();
	for (i = 0; i < n; i++) {
		if ((struct ifnet *)npugiu_port_ifp(i) == ifp) {
			port = i;
			break;
		}
	}
	if (port < 0 && hint_port >= 0 && hint_port < n && npugiu_port_ifp(hint_port) != NULL &&
	    npugiu_port_link(hint_port) == 1) {
		port = hint_port;
		out->from_hint = 1;
	}
	if (port < 0) {
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

	memcpy(out->smac, if_getlladdr((if_t)ifp), ETHER_ADDR_LEN);
	out->iface = npugiu_port_iface(port);
	out->tag = npugiu_port_tag(port);
	out->mtu = (uint16_t)if_getmtu((if_t)ifp);
	out->port = port;

	flags = 0;
	/* Into a link header's room, as above: nd6_resolve copies the whole prepared header too. */
	error = nd6_resolve(ifp, LLE_SF(AF_INET6, is_gw), NULL, (struct sockaddr *)&sin6, lh,
	    &flags, NULL);
	if (error == 0)
		memcpy(out->dmac, lh, ETHER_ADDR_LEN);
	NET_EPOCH_EXIT(et);
	return (error);
}
