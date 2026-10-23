/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 */

/*
 * The inspector's side on the XGS 136 (discovery 4).
 *
 * The fast path hands a connection whose verdict is "send to inspection" to one of the coprocessor's
 * other GIU functions, frame by frame: the frame as it arrived, behind a two-byte tag and sixty-four
 * bytes of usfp_dp_md - the IPS block and the forwarding block, which the fast path applies when the
 * frame comes back. npugiu brings that function up (npugiu_attach_vf) and gives each such frame to
 * npuips_rx; handing one back is npugiu_ips_send with the same sixty-six bytes in front.
 *
 * The inspector is whatever reads oxips0. Suricata in IPS mode does, through netmap, which replaces
 * the interface's input function with its own while it runs and calls the one it replaced -
 * npuips_input - with each frame it lets through, netmap's way of passing a frame on to the host's
 * stack. So a frame goes to oxips0 as input while something has taken that function over; while
 * nothing has, and ips.loop says to pass it, it is handed straight back and its connection set to
 * forward (npuflow_ips_unwatched): an inspector that stops costs its connections nothing.
 *
 * What is kept of each connection is the sixty-six bytes its frames arrived with, by the frame's own
 * key - the address family, the protocol, both addresses and both ports, which also tells the two
 * directions apart - since a frame passes through Suricata as a copy in netmap's buffers and nothing
 * tied to the mbuf comes back with it. The same as the XGS 3300's (octep_dp.c, octep_ips_*).
 */

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/hash.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/ethernet.h>

#include <netinet/in.h>

#include <machine/atomic.h>

#include "npuep.h"
#include "npugiu.h"
#include "npuflow.h"
#include "npuips.h"

#define	NPUIPS_HDR		(2 + 64)	/* the tag and usfp_dp_md, as NPUGIU_HDR_LEN */
#define	NPUIPS_KEY_MAX		40
#define	NPUIPS_TAB		16384		/* a power of two */
#define	NPUIPS_MTU_MAX		9000

struct npuips_ent {
	uint8_t		 klen;
	uint8_t		 key[NPUIPS_KEY_MAX];
	uint8_t		 hdr[NPUIPS_HDR];
};

static if_t		 npuips_ifp;
static struct npuips_ent *npuips_tab;
static struct mtx	 npuips_mtx;
static int		 npuips_loop = 1;
static uint64_t		 npuips_n_rx, npuips_n_up, npuips_n_passed, npuips_n_back;
static uint64_t		 npuips_n_nomd, npuips_n_unkeyed, npuips_n_dropped;

static void	npuips_input(if_t ifp, struct mbuf *m);

/*
 * The frame's key: its address family, protocol, both addresses and, where there are some, both
 * ports. Returns its length, or 0 for a frame that is not IP.
 */
static uint32_t
npuips_key(const uint8_t *f, uint32_t len, uint8_t *key)
{
	const uint8_t *ip;
	uint32_t ihl, k = 0;
	uint8_t proto;

	if (len < ETHER_HDR_LEN + 20)
		return (0);
	ip = f + ETHER_HDR_LEN;
	switch (be16dec(f + 12)) {
	case ETHERTYPE_IP:
		ihl = (uint32_t)(ip[0] & 0x0f) * 4;
		if (ihl < 20 || len < ETHER_HDR_LEN + ihl)
			return (0);
		proto = ip[9];
		key[k++] = 4;
		key[k++] = proto;
		memcpy(key + k, ip + 12, 8);
		k += 8;
		if ((proto == IPPROTO_TCP || proto == IPPROTO_UDP || proto == IPPROTO_SCTP) &&
		    (be16dec(ip + 6) & 0x1fff) == 0 && len >= ETHER_HDR_LEN + ihl + 4) {
			memcpy(key + k, ip + ihl, 4);
			k += 4;
		}
		return (k);
	case ETHERTYPE_IPV6:
		if (len < ETHER_HDR_LEN + 40)
			return (0);
		proto = ip[6];
		key[k++] = 6;
		key[k++] = proto;
		memcpy(key + k, ip + 8, 32);
		k += 32;
		if ((proto == IPPROTO_TCP || proto == IPPROTO_UDP || proto == IPPROTO_SCTP) &&
		    len >= ETHER_HDR_LEN + 44) {
			memcpy(key + k, ip + 40, 4);
			k += 4;
		}
		return (k);
	default:
		return (0);
	}
}

static void
npuips_remember(const uint8_t *key, uint32_t klen, const uint8_t *hdr)
{
	struct npuips_ent *e = &npuips_tab[hash32_buf(key, klen, HASHINIT) & (NPUIPS_TAB - 1)];

	mtx_lock(&npuips_mtx);
	e->klen = (uint8_t)klen;
	memcpy(e->key, key, klen);
	memcpy(e->hdr, hdr, NPUIPS_HDR);
	mtx_unlock(&npuips_mtx);
}

static int
npuips_recall(const uint8_t *key, uint32_t klen, uint8_t *hdr)
{
	struct npuips_ent *e = &npuips_tab[hash32_buf(key, klen, HASHINIT) & (NPUIPS_TAB - 1)];
	int found;

	mtx_lock(&npuips_mtx);
	found = (e->klen == klen && memcmp(e->key, key, klen) == 0);
	if (found)
		memcpy(hdr, e->hdr, NPUIPS_HDR);
	mtx_unlock(&npuips_mtx);
	return (found);
}

/* The session in a frame's usfp_dp_md, which is its connection's index. */
static __inline uint32_t
npuips_session(const uint8_t *hdr)
{

	return (le32dec(hdr + 2 + 8));
}

int
npuips_listening(void)
{
	if_t ifp = npuips_ifp;

	return (ifp != NULL && (if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0 &&
	    if_getinputfn(ifp) != npuips_input);
}

/*
 * A frame from the inspector's function, `total` bytes at `src`, the sixty-six in front. Called by
 * npugiu's receive path with the function's lock held. Returns an mbuf for that path to give oxips0
 * with the rest of its pass, or NULL when the frame was handed straight back or dropped here.
 */
struct mbuf *
npuips_rx(const uint8_t *src, int total)
{
	const uint8_t *f = src + NPUIPS_HDR;
	uint8_t key[NPUIPS_KEY_MAX];
	uint32_t flen, klen;
	struct mbuf *m;

	atomic_add_64(&npuips_n_rx, 1);
	if (total < NPUIPS_HDR + ETHER_HDR_LEN) {
		atomic_add_64(&npuips_n_dropped, 1);
		return (NULL);
	}
	flen = (uint32_t)total - NPUIPS_HDR;

	if (npuips_listening()) {
		klen = npuips_key(f, flen, key);
		if (klen == 0) {
			atomic_add_64(&npuips_n_unkeyed, 1);
			atomic_add_64(&npuips_n_dropped, 1);
			return (NULL);
		}
		m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, flen <= MCLBYTES ? MCLBYTES :
		    flen <= MJUMPAGESIZE ? MJUMPAGESIZE : MJUM9BYTES);
		if (m == NULL || flen > MJUM9BYTES) {
			if (m != NULL)
				m_freem(m);
			atomic_add_64(&npuips_n_dropped, 1);
			return (NULL);
		}
		npuips_remember(key, klen, src);
		memcpy(mtod(m, uint8_t *), f, flen);
		m->m_len = m->m_pkthdr.len = (int)flen;
		m->m_pkthdr.rcvif = npuips_ifp;
		m->m_nextpkt = NULL;
		if_inc_counter(npuips_ifp, IFCOUNTER_IPACKETS, 1);
		if_inc_counter(npuips_ifp, IFCOUNTER_IBYTES, flen);
		atomic_add_64(&npuips_n_up, 1);
		return (m);
	}

	if (npuips_loop == 0 || npugiu_ips_send(src, NULL, f, (int)flen) != 0) {
		atomic_add_64(&npuips_n_dropped, 1);
		return (NULL);
	}
	atomic_add_64(&npuips_n_back, 1);
	npuflow_ips_unwatched(npuips_session(src));
	return (NULL);
}

/*
 * oxips0's input function: a frame the inspector let through. Handed back with the sixty-six bytes
 * its connection's frames arrived with in this direction, or dropped and counted when none is kept.
 */
static void
npuips_input(if_t ifp, struct mbuf *m)
{
	uint8_t head[ETHER_HDR_LEN + 60 + 4], key[NPUIPS_KEY_MAX], hdr[NPUIPS_HDR];
	uint32_t flen, hlen, klen;

	(void)ifp;
	while (m != NULL) {
		struct mbuf *n = m->m_nextpkt;

		m->m_nextpkt = NULL;
		flen = (uint32_t)m->m_pkthdr.len;
		hlen = flen < sizeof(head) ? flen : (uint32_t)sizeof(head);
		m_copydata(m, 0, hlen, (caddr_t)head);
		klen = npuips_key(head, hlen, key);
		if (klen == 0) {
			atomic_add_64(&npuips_n_unkeyed, 1);
			atomic_add_64(&npuips_n_dropped, 1);
		} else if (!npuips_recall(key, klen, hdr)) {
			atomic_add_64(&npuips_n_nomd, 1);
			atomic_add_64(&npuips_n_dropped, 1);
		} else if (npugiu_ips_send(hdr, m, NULL, (int)flen) != 0) {
			atomic_add_64(&npuips_n_dropped, 1);
		} else {
			atomic_add_64(&npuips_n_passed, 1);
			atomic_add_64(&npuips_n_back, 1);
			npuflow_ips_passed(npuips_session(hdr), flen);
		}
		m_freem(m);
		m = n;
	}
}

/*
 * oxips0 itself: an Ethernet interface with no address of its own. What the stack would send through
 * it is dropped - nothing on this host has anything to say to the inspector's function but the frames
 * handed back above.
 */
static void
npuips_if_init(void *arg)
{
	if_t ifp = arg;

	if_setdrvflagbits(ifp, IFF_DRV_RUNNING, IFF_DRV_OACTIVE);
}

static int
npuips_if_ioctl(if_t ifp, u_long cmd, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq *)data;

	switch (cmd) {
	case SIOCSIFFLAGS:
		if ((if_getflags(ifp) & IFF_UP) != 0)
			if_setdrvflagbits(ifp, IFF_DRV_RUNNING, 0);
		else
			if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
		return (0);
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > NPUIPS_MTU_MAX)
			return (EINVAL);
		if_setmtu(ifp, ifr->ifr_mtu);
		return (0);
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		return (0);
	default:
		return (ether_ioctl(ifp, cmd, data));
	}
}

static int
npuips_if_transmit(if_t ifp, struct mbuf *m)
{

	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	m_freem(m);
	return (0);
}

static void
npuips_if_qflush(if_t ifp)
{

	(void)ifp;
}

void
npuips_attach(device_t dev)
{
	uint8_t mac[ETHER_ADDR_LEN] = { 0x02, 0x0c, 0xe0, 0x83, 0x1e, 0x00 };
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
	struct sysctl_oid *node;
	if_t ifp;

	if (npuips_ifp != NULL)
		return;
	npuips_tab = malloc(sizeof(*npuips_tab) * NPUIPS_TAB, M_DEVBUF, M_WAITOK | M_ZERO);
	mtx_init(&npuips_mtx, "npuips", NULL, MTX_DEF);
	mac[5] = (uint8_t)device_get_unit(dev);
	ifp = if_alloc(IFT_ETHER);
	if_initname(ifp, "oxips", device_get_unit(dev));
	if_setsoftc(ifp, NULL);
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
	if_setinitfn(ifp, npuips_if_init);
	if_setioctlfn(ifp, npuips_if_ioctl);
	if_settransmitfn(ifp, npuips_if_transmit);
	if_setqflushfn(ifp, npuips_if_qflush);
	if_setmtu(ifp, ETHERMTU);
	ether_ifattach(ifp, mac);
	if_setbaudrate(ifp, 0);
	/* After ether_ifattach, which sets ether_input: netmap keeps whatever is here when it attaches. */
	if_setinputfn(ifp, npuips_input);
	npuips_ifp = ifp;

	node = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO, "ips",
	    CTLFLAG_RD | CTLFLAG_MPSAFE, NULL, "the inspector's function and oxips0 (discovery 4)");
	if (node != NULL) {
		struct sysctl_oid_list *k = SYSCTL_CHILDREN(node);

		SYSCTL_ADD_INT(ctx, k, OID_AUTO, "loop", CTLFLAG_RW, &npuips_loop, 0,
		    "while nothing reads oxips0: 1 hands every frame straight back and sets its "
		    "connection to forward, 0 drops them");
		SYSCTL_ADD_U64(ctx, k, OID_AUTO, "rx", CTLFLAG_RD, &npuips_n_rx, 0,
		    "frames the fast path sent the inspector");
		SYSCTL_ADD_U64(ctx, k, OID_AUTO, "up", CTLFLAG_RD, &npuips_n_up, 0,
		    "of those, given to oxips0 for an inspector reading it");
		SYSCTL_ADD_U64(ctx, k, OID_AUTO, "passed", CTLFLAG_RD, &npuips_n_passed, 0,
		    "frames the inspector let through oxips0, handed back");
		SYSCTL_ADD_U64(ctx, k, OID_AUTO, "back", CTLFLAG_RD, &npuips_n_back, 0,
		    "frames handed back to the fast path, let through or with nothing reading");
		SYSCTL_ADD_U64(ctx, k, OID_AUTO, "nomd", CTLFLAG_RD, &npuips_n_nomd, 0,
		    "frames let through with no metadata kept for their connection, dropped");
		SYSCTL_ADD_U64(ctx, k, OID_AUTO, "unkeyed", CTLFLAG_RD, &npuips_n_unkeyed, 0,
		    "frames that were not IP, so had no key, dropped");
		SYSCTL_ADD_U64(ctx, k, OID_AUTO, "dropped", CTLFLAG_RD, &npuips_n_dropped, 0,
		    "frames dropped here");
	}
	device_printf(dev, "giu: %s, the inspector's interface\n", if_name(ifp));
}

void
npuips_detach(void)
{

	if (npuips_ifp != NULL) {
		ether_ifdetach(npuips_ifp);
		if_free(npuips_ifp);
		npuips_ifp = NULL;
	}
	if (npuips_tab != NULL) {
		free(npuips_tab, M_DEVBUF);
		npuips_tab = NULL;
		mtx_destroy(&npuips_mtx);
	}
}
