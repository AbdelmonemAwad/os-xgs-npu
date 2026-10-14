/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The kernel's IPsec offload contract on the ARMADA front ports, first slice.
 *
 * FreeBSD 15 ships `struct if_ipsec_accel_methods` and, under `options IPSEC_OFFLOAD`, the glue that
 * offers every installed association to each interface advertising IFCAP2_IPSEC_OFFLOAD. The OCTEON
 * TX driver answers that contract in full - it installs the association on its coprocessor, takes
 * over the outbound transform, and terminates decrypted frames on receive (contrib/octep/octep_ipsec.c).
 * This family's coprocessor speaks the same control protocol and has its own crypto engine (EIP-197),
 * so the same capability belongs here - but the data path is several slices of work (the per-frame
 * crypto marker in the GIU metadata, the engine's SA_ADD, the receive termination and the transmit
 * envelope), and none of it can be claimed until it is measured against a tunnel.
 *
 * So this slice does the one thing that stands on its own and is safe without the rest: it advertises
 * the capability and records every association the kernel offers to a front port that owns one of its
 * ends - proving this driver is in the offload path - and then DECLINES each, so the kernel keeps
 * doing the crypto in software exactly as before. Accepting an association (returning 0) without a
 * data path would tell the kernel to stop encrypting while the coprocessor never started, which is a
 * black hole; declining is the only honest answer until SA_ADD and the data path are built. What the
 * kernel has offered is on dev.npuep.0.ipsec.sas.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sbuf.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/endian.h>
#include <sys/epoch.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_private.h>

#include <netinet/in.h>
#include <netinet/in_var.h>

#include <netipsec/ipsec.h>
#include <netipsec/keydb.h>
#include <netipsec/key.h>
#include <netipsec/ipsec_offload.h>

#include "npuipsec.h"

#define	NPUIPSEC_SEEN_MAX	16

struct npuipsec_seen {
	int		used;
	uint32_t	spi;		/* host order, for the page */
	int		dir;		/* 0 encrypt (this port owns the source), 1 decrypt (the dest) */
	uint8_t		family;		/* the outer header's: AF_INET or AF_INET6 */
	uint8_t		alg;		/* sav->alg_enc */
	char		ifname[IFNAMSIZ];
	int		when;		/* ticks at the offer */
};

static struct mtx		npuipsec_mtx;
MTX_SYSINIT(npuipsec, &npuipsec_mtx, "npuipsec", MTX_DEF);

static struct npuipsec_seen	npuipsec_seen[NPUIPSEC_SEEN_MAX];
static uint32_t			npuipsec_next;
static uint64_t			npuipsec_offered, npuipsec_offered_enc, npuipsec_offered_dec;

/*
 * Which end of the association this interface is: the one owning the destination decrypts (the
 * inbound association), the one owning the source encrypts (the outbound), every other interface
 * declines. The kernel's SADB has no direction field, so this is the only way to tell them apart -
 * the OCTEON TX driver's octep_ipsec_direction, byte for byte.
 */
static int
npuipsec_direction(if_t ifp, const struct secasindex *saidx)
{
	struct epoch_tracker et;
	struct ifaddr *ifa;
	int dir = -1;

	if (saidx->dst.sa.sa_family != saidx->src.sa.sa_family ||
	    (saidx->dst.sa.sa_family != AF_INET && saidx->dst.sa.sa_family != AF_INET6))
		return (-1);
	NET_EPOCH_ENTER(et);
	ifa = ifa_ifwithaddr(&saidx->dst.sa);
	if (ifa != NULL && ifa->ifa_ifp == ifp)
		dir = 1;
	else {
		ifa = ifa_ifwithaddr(&saidx->src.sa);
		if (ifa != NULL && ifa->ifa_ifp == ifp)
			dir = 0;
	}
	NET_EPOCH_EXIT(et);
	return (dir);
}

static int
npuep_ipsec_sa_newkey(if_t ifp, void *savp, u_int drv_spi, void **privp)
{
	struct secasvar *sav = savp;
	const struct secasindex *saidx;
	struct npuipsec_seen *e;
	int dir;

	*privp = NULL;
	if (sav == NULL || sav->sah == NULL)
		return (EOPNOTSUPP);
	saidx = &sav->sah->saidx;
	dir = npuipsec_direction(ifp, saidx);
	if (dir < 0)
		return (EOPNOTSUPP);		/* not this port's association */

	mtx_lock(&npuipsec_mtx);
	npuipsec_offered++;
	if (dir == 0)
		npuipsec_offered_enc++;
	else
		npuipsec_offered_dec++;
	e = &npuipsec_seen[npuipsec_next % NPUIPSEC_SEEN_MAX];
	npuipsec_next++;
	e->used = 1;
	e->spi = ntohl(sav->spi);
	e->dir = dir;
	e->family = (uint8_t)saidx->dst.sa.sa_family;
	e->alg = sav->alg_enc;
	strlcpy(e->ifname, if_name(ifp), sizeof(e->ifname));
	e->when = ticks;
	mtx_unlock(&npuipsec_mtx);

	/*
	 * Recorded, and declined: the kernel keeps the crypto in software. Returning 0 here without a
	 * data path would be the black hole the file comment warns of. SA_ADD is the next slice.
	 */
	return (EOPNOTSUPP);
}

/* Never reached for an association we accepted, because we accept none yet. */
static int
npuep_ipsec_sa_deinstall(if_t ifp, u_int drv_spi, void *priv)
{

	return (0);
}

static int
npuep_ipsec_sa_cnt(if_t ifp, void *savp, uint32_t drv_spi, void *priv, struct seclifetime *lt)
{

	return (EOPNOTSUPP);
}

static int
npuep_ipsec_hwassist(if_t ifp, void *savp, u_int drv_spi, void *priv)
{

	/* The stack completes every checksum before a frame reaches us. */
	return (0);
}

static int
npuep_ipsec_spdadd(if_t ifp, void *sp, void *inp, void **priv)
{

	*priv = NULL;
	return (0);
}

static int
npuep_ipsec_spddel(if_t ifp, void *sp, void *priv)
{

	return (0);
}

static const struct if_ipsec_accel_methods npuep_ipsec_methods = {
	.if_spdadd = npuep_ipsec_spdadd,
	.if_spddel = npuep_ipsec_spddel,
	.if_sa_newkey = npuep_ipsec_sa_newkey,
	.if_sa_deinstall = npuep_ipsec_sa_deinstall,
	.if_sa_cnt = npuep_ipsec_sa_cnt,
	.if_hwassist = npuep_ipsec_hwassist,
};

void
npuep_ipsec_if_attach(if_t ifp)
{

	/*
	 * The methods first, then the capability: the kernel reads the capability and then dereferences
	 * the table, and may offer an association the moment the bit is visible.
	 */
	if_setipsec_accel_methods(ifp, &npuep_ipsec_methods);
	if_setcapabilities2bit(ifp, IFCAP2_BIT(IFCAP2_IPSEC_OFFLOAD), 0);
	if_setcapenable2bit(ifp, IFCAP2_BIT(IFCAP2_IPSEC_OFFLOAD), 0);
}

static int
npuep_ipsec_sysctl_sas(SYSCTL_HANDLER_ARGS)
{
	struct npuipsec_seen c;
	struct sbuf *sb;
	int i, error, n = 0;

	sb = sbuf_new_for_sysctl(NULL, NULL, 2048, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_printf(sb, "offered %ju (encrypt %ju decrypt %ju) | recorded then declined - the data "
	    "path is a later slice, so the kernel still does the crypto in software\n",
	    (uintmax_t)npuipsec_offered, (uintmax_t)npuipsec_offered_enc,
	    (uintmax_t)npuipsec_offered_dec);
	for (i = 0; i < NPUIPSEC_SEEN_MAX; i++) {
		mtx_lock(&npuipsec_mtx);
		c = npuipsec_seen[i];
		mtx_unlock(&npuipsec_mtx);
		if (!c.used)
			continue;
		n++;
		sbuf_printf(sb, "%-6s spi 0x%08x %s %s alg %u %ds ago\n", c.ifname, c.spi,
		    c.dir == 1 ? "decrypt" : "encrypt", c.family == AF_INET6 ? "inet6" : "inet",
		    c.alg, (ticks - c.when) / hz);
	}
	if (n == 0)
		sbuf_cat(sb, "no association offered to a front port yet\n");
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

void
npuep_ipsec_add_sysctls(struct sysctl_ctx_list *ctx, struct sysctl_oid *parent)
{
	struct sysctl_oid *node;

	node = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(parent), OID_AUTO, "ipsec",
	    CTLFLAG_RD | CTLFLAG_MPSAFE, NULL, "the kernel's IPsec offload offers to the front ports");
	if (node == NULL)
		return;
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sas",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, NULL, 0, npuep_ipsec_sysctl_sas, "A",
	    "associations the kernel has offered to a front port (recorded; declined for now)");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "offered", CTLFLAG_RD,
	    &npuipsec_offered, 0, "associations the kernel offered to a front port that owns an end");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "offered_encrypt", CTLFLAG_RD,
	    &npuipsec_offered_enc, 0, "of those, the outbound (this port owns the source)");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "offered_decrypt", CTLFLAG_RD,
	    &npuipsec_offered_dec, 0, "of those, the inbound (this port owns the destination)");
}
