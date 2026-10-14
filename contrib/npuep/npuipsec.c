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
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sbuf.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/endian.h>
#include <sys/epoch.h>
#include <machine/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_private.h>
#include <net/pfkeyv2.h>

#include <netinet/in.h>
#include <netinet/in_var.h>

#include <netipsec/ipsec.h>
#include <netipsec/keydb.h>
#include <netipsec/key.h>
#include <netipsec/ipsec_offload.h>

#include "npuep.h"
#include "npuflow.h"
#include "npuipsec.h"

/*
 * SA_ADD (RPC_CMD_SA_ADD), the OCTEON TX driver's 192-byte struct usfp_fpop_req_sa_add, read from
 * that family's module and confirmed byte for byte against this one's (docs/npuep/rpc-payloads.md).
 * The key and address words are big-endian; the scalars beside them are little. For GCM the AES key
 * sits in the key field and the four-byte AEAD salt in the first word of the auth-key field.
 */
#define	NPUIPSEC_SA_REQ_LEN	192
#define	NPUIPSEC_SA_OFF_IDX	0	/* le32 */
#define	NPUIPSEC_SA_OFF_LIF	4	/* le32: iface << 12 */
#define	NPUIPSEC_SA_OFF_KEY	8	/* 32 bytes */
#define	NPUIPSEC_SA_OFF_AUTHKEY	40	/* 64 bytes; for GCM the first 4 are the salt */
#define	NPUIPSEC_SA_OFF_CTRL	104	/* le32 */
#define	NPUIPSEC_SA_OFF_OPT	108	/* le32 */
#define	NPUIPSEC_SA_OFF_WIN	112	/* le32 */
#define	NPUIPSEC_SA_OFF_SPI	116	/* be32 */
#define	NPUIPSEC_SA_OFF_SEQ	120	/* le64 */
#define	NPUIPSEC_SA_OFF_SRC	128	/* 4 x be32 */
#define	NPUIPSEC_SA_OFF_DST	144	/* 4 x be32 */
#define	NPUIPSEC_SA_OFF_REV	184	/* le16 */

/* ctrl field bits, the OCTEON TX layout. */
#define	NPUIPSEC_HASH_GF128_128	11	/* GCM's hash */
#define	NPUIPSEC_CIMODE_CTR	4	/* GCM runs the block cipher in counter mode */
#define	NPUIPSEC_OVH_GCM_128	2	/* overhead type, opt bits 24..31 */
#define	NPUIPSEC_SA_IDX_MAX	16	/* probe indices 1..15; 0 is never used */

#define	NPUIPSEC_SEEN_MAX	16

struct npuipsec_seen {
	int		used;
	uint32_t	spi;		/* host order, for the page */
	int		dir;		/* 0 encrypt (this port owns the source), 1 decrypt (the dest) */
	uint8_t		family;		/* the outer header's: AF_INET or AF_INET6 */
	uint8_t		alg;		/* sav->alg_enc */
	int		probed;		/* 1 if SA_ADD/SA_DEL were posted for it */
	int		engine_rc;	/* the engine's answer to SA_ADD: 0 = accepted */
	char		ifname[IFNAMSIZ];
	int		when;		/* ticks at the offer */
};

static struct mtx		npuipsec_mtx;
MTX_SYSINIT(npuipsec, &npuipsec_mtx, "npuipsec", MTX_DEF);

static struct npuipsec_seen	npuipsec_seen[NPUIPSEC_SEEN_MAX];
static uint32_t			npuipsec_next;
static uint64_t			npuipsec_offered, npuipsec_offered_enc, npuipsec_offered_dec;

/*
 * The probe: a test aid, off by default. With dev.npuep.0.ipsec.probe set, an offered AES-GCM
 * association is marshalled into SA_ADD and handed to the engine to see that it is accepted, then
 * taken straight back out with SA_DEL - and still declined to the kernel, so software crypto runs.
 * It proves the SA_ADD bytes are right without a data path. It is for a manual (fake-peer)
 * association: probing a live inbound tunnel would have the engine decrypt frames the receive path
 * does not yet terminate. The data path is the next slice.
 */
static int			npuipsec_probe_on;
static uint8_t			npuipsec_rev[NPUIPSEC_SA_IDX_MAX];	/* per index, never 0 */
static uint64_t			npuipsec_probe_ok, npuipsec_probe_fail;

/*
 * Accepting an association for real, the next step past the probe: with dev.npuep.0.ipsec.accept_decrypt
 * set, an offered AES-GCM DECRYPT association (this port owns the destination) is installed on the
 * engine with SA_ADD and KEPT - and accepted to the kernel (newkey returns 0), so the engine
 * decrypts the inbound ESP in place and punts the frame with the association's handle (index + 1) in
 * the metadata, where giu.last_meta reads it. Off by default, because without the receive
 * termination those decrypted frames are not delivered - this slice is to SEE the crypto marker, not
 * to carry the tunnel. One entry per installed association; the handle is the index plus one, the
 * OCTEON TX convention the fast path enforces (a handle of 0 is refused).
 */
struct npuipsec_sa {
	int		used;
	uint32_t	idx;		/* engine index 1..15 */
	uint8_t		rev;
	uint32_t	spi;		/* host order */
	int		dir;
};
static int			npuipsec_accept_decrypt;
static struct npuipsec_sa	npuipsec_sa_tab[NPUIPSEC_SA_IDX_MAX];
static uint64_t			npuipsec_accepted, npuipsec_accept_fail, npuipsec_removed;

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

/* The LIF interface id of the front port this ifp is, or 0. */
static uint8_t
npuipsec_iface_of(if_t ifp)
{
	int i, n;

	n = npugiu_port_count();
	for (i = 0; i < n; i++)
		if ((if_t)npugiu_port_ifp(i) == ifp)
			return (npugiu_port_iface(i));
	return (0);
}

/*
 * Build and post one GCM SA_ADD, read the engine's answer. Returns the engine's rc (0 = accepted)
 * or a negative errno on a transport failure. No lock held; this sleeps in the RPC. The association
 * stays installed on the engine; the caller frees it with npuipsec_sa_del.
 */
static int
npuipsec_sa_add_gcm(if_t ifp, struct secasvar *sav, int dir, uint32_t idx, uint8_t rev)
{
	const struct secasindex *saidx = &sav->sah->saidx;
	uint8_t pl[NPUIPSEC_SA_REQ_LEN];
	uint32_t ctrl, opt, cipher;
	int keylen, n, rc;

	keylen = _KEYLEN(sav->key_enc) - 4;		/* the 4-byte salt rides at the end, RFC 4106 */
	if (keylen != 16 && keylen != 24 && keylen != 32)
		return (-EINVAL);
	cipher = (keylen == 16) ? 2 : (keylen == 24) ? 3 : 4;	/* AES128/192/256 */

	memset(pl, 0, sizeof(pl));
	le32enc(pl + NPUIPSEC_SA_OFF_IDX, idx);
	le32enc(pl + NPUIPSEC_SA_OFF_LIF, (uint32_t)npuipsec_iface_of(ifp) << 12);
	memcpy(pl + NPUIPSEC_SA_OFF_KEY, sav->key_enc->key_data, keylen);
	memcpy(pl + NPUIPSEC_SA_OFF_AUTHKEY, sav->key_enc->key_data + keylen, 4);	/* the AEAD salt */
	ctrl = ((uint32_t)NPUIPSEC_HASH_GF128_128 & 0xf) |
	    (((uint32_t)NPUIPSEC_CIMODE_CTR & 0xf) << 4) | ((cipher & 0xf) << 8) |
	    (1u << 12) /* tunnel */ | (1u << 14) /* ESP */ |
	    (((uint32_t)dir & 1u) << 16) | (1u << 31) /* valid */;
	le32enc(pl + NPUIPSEC_SA_OFF_CTRL, ctrl);
	opt = ((uint32_t)NPUIPSEC_OVH_GCM_128 << 24);
	le32enc(pl + NPUIPSEC_SA_OFF_OPT, opt);
	le32enc(pl + NPUIPSEC_SA_OFF_WIN, 0);
	be32enc(pl + NPUIPSEC_SA_OFF_SPI, ntohl(sav->spi));
	le64enc(pl + NPUIPSEC_SA_OFF_SEQ, 0);
	be32enc(pl + NPUIPSEC_SA_OFF_SRC, ntohl(saidx->src.sin.sin_addr.s_addr));
	be32enc(pl + NPUIPSEC_SA_OFF_DST, ntohl(saidx->dst.sin.sin_addr.s_addr));
	le16enc(pl + NPUIPSEC_SA_OFF_REV, rev);

	n = npurpc_send(RPC_CMD_SA_ADD, pl, sizeof(pl), NULL, 0, &rc);
	explicit_bzero(pl, sizeof(pl));			/* the key does not linger on the stack */
	if (n < 0)
		return (n);
	return (rc);
}

/* Take an engine association out: SA_DEL invalidate (0) then free (1). No lock held. */
static void
npuipsec_sa_del(uint32_t idx)
{
	uint8_t d[8];
	int k, delrc;

	for (k = 0; k < 2; k++) {
		memset(d, 0, sizeof(d));
		le32enc(d + 0, idx);
		le32enc(d + 4, (uint32_t)k);
		(void)npurpc_send(RPC_CMD_SA_DEL, d, sizeof(d), NULL, 0, &delrc);
	}
}

/* The probe: install, see it accepted, and take it straight back. Returns the engine's rc. */
static int
npuipsec_probe_gcm(if_t ifp, struct secasvar *sav, int dir, uint32_t idx, uint8_t rev)
{
	int rc;

	rc = npuipsec_sa_add_gcm(ifp, sav, dir, idx, rev);
	npuipsec_sa_del(idx);
	return (rc);
}

/* Reserve an engine index 1..15 (the table slot is the index), bump its revision. mtx held. */
static struct npuipsec_sa *
npuipsec_sa_alloc(uint32_t *idx, uint8_t *rev, uint32_t spi, int dir)
{
	struct npuipsec_sa *s;
	int i;

	for (i = 1; i < NPUIPSEC_SA_IDX_MAX; i++) {
		s = &npuipsec_sa_tab[i];
		if (s->used)
			continue;
		npuipsec_rev[i] = (uint8_t)(npuipsec_rev[i] + 1);
		if (npuipsec_rev[i] == 0)
			npuipsec_rev[i] = 1;
		s->used = 1;
		s->idx = (uint32_t)i;
		s->rev = npuipsec_rev[i];
		s->spi = spi;
		s->dir = dir;
		*idx = s->idx;
		*rev = s->rev;
		return (s);
	}
	return (NULL);
}

/* mtx held. */
static void
npuipsec_sa_free(struct npuipsec_sa *s)
{

	if (s != NULL)
		s->used = 0;
}

static int
npuep_ipsec_sa_newkey(if_t ifp, void *savp, u_int drv_spi, void **privp)
{
	struct secasvar *sav = savp;
	const struct secasindex *saidx;
	struct npuipsec_seen *e;
	struct npuipsec_sa *s;
	uint32_t si, idx = 0;
	uint8_t rev = 0;
	int dir, rc, gcm4;

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
	si = npuipsec_next % NPUIPSEC_SEEN_MAX;
	npuipsec_next++;
	e = &npuipsec_seen[si];
	e->used = 1;
	e->spi = ntohl(sav->spi);
	e->dir = dir;
	e->family = (uint8_t)saidx->dst.sa.sa_family;
	e->alg = sav->alg_enc;
	e->probed = 0;
	e->engine_rc = 0;
	strlcpy(e->ifname, if_name(ifp), sizeof(e->ifname));
	e->when = ticks;
	mtx_unlock(&npuipsec_mtx);

	gcm4 = (sav->alg_enc == SADB_X_EALG_AESGCM16 && saidx->dst.sa.sa_family == AF_INET);

	/*
	 * Accept the decrypt association for real, if asked (off by default). The engine is given the
	 * association and keeps it, and the kernel is told we own it (return 0), so the engine decrypts
	 * the inbound ESP in place and punts the frame with the handle in the metadata. Without the
	 * receive termination the frame is not delivered, so this is for SEEING the marker, not carrying
	 * the tunnel - hence gated. Only the decrypt direction: the encrypt side is the transmit slice.
	 */
	if (npuipsec_accept_decrypt && gcm4 && dir == 1) {
		mtx_lock(&npuipsec_mtx);
		s = npuipsec_sa_alloc(&idx, &rev, ntohl(sav->spi), dir);
		mtx_unlock(&npuipsec_mtx);
		if (s == NULL) {
			mtx_lock(&npuipsec_mtx);
			npuipsec_accept_fail++;
			mtx_unlock(&npuipsec_mtx);
			return (EOPNOTSUPP);
		}
		rc = npuipsec_sa_add_gcm(ifp, sav, dir, idx, rev);
		if (rc == 0) {
			mtx_lock(&npuipsec_mtx);
			npuipsec_seen[si].probed = 1;
			npuipsec_seen[si].engine_rc = 0;
			npuipsec_accepted++;
			mtx_unlock(&npuipsec_mtx);
			*privp = s;		/* the kernel hands this back on deinstall */
			return (0);		/* ACCEPTED: the engine decrypts and marks the frame */
		}
		npuipsec_sa_del(idx);
		mtx_lock(&npuipsec_mtx);
		npuipsec_sa_free(s);
		npuipsec_accept_fail++;
		npuipsec_seen[si].probed = 1;
		npuipsec_seen[si].engine_rc = rc;
		mtx_unlock(&npuipsec_mtx);
		return (EOPNOTSUPP);	/* refused: leave it to the kernel's software crypto */
	}

	/*
	 * The probe (a test aid, off by default): install, see the engine accept it, take it straight
	 * back, and still decline - the SA_ADD bytes proven without a data path.
	 */
	if (npuipsec_probe_on && gcm4) {
		mtx_lock(&npuipsec_mtx);
		s = npuipsec_sa_alloc(&idx, &rev, ntohl(sav->spi), dir);
		mtx_unlock(&npuipsec_mtx);
		if (s != NULL) {
			rc = npuipsec_probe_gcm(ifp, sav, dir, idx, rev);
			mtx_lock(&npuipsec_mtx);
			npuipsec_sa_free(s);
			npuipsec_seen[si].probed = 1;
			npuipsec_seen[si].engine_rc = rc;
			if (rc == 0)
				npuipsec_probe_ok++;
			else
				npuipsec_probe_fail++;
			mtx_unlock(&npuipsec_mtx);
		}
	}

	/*
	 * Otherwise recorded and DECLINED: the kernel keeps the crypto in software. Returning 0 without
	 * a data path would be the black hole the file comment warns of.
	 */
	return (EOPNOTSUPP);
}

/* The kernel is done with an association we accepted: take it off the engine and free the slot. */
static int
npuep_ipsec_sa_deinstall(if_t ifp, u_int drv_spi, void *priv)
{
	struct npuipsec_sa *s = priv;

	if (s == NULL)
		return (0);
	npuipsec_sa_del(s->idx);
	mtx_lock(&npuipsec_mtx);
	npuipsec_sa_free(s);
	npuipsec_removed++;
	mtx_unlock(&npuipsec_mtx);
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
	sbuf_printf(sb, "offered %ju (encrypt %ju decrypt %ju) | probe %s (ok %ju fail %ju) | "
	    "accept_decrypt %s (accepted %ju fail %ju removed %ju)\n",
	    (uintmax_t)npuipsec_offered, (uintmax_t)npuipsec_offered_enc,
	    (uintmax_t)npuipsec_offered_dec, npuipsec_probe_on ? "on" : "off",
	    (uintmax_t)npuipsec_probe_ok, (uintmax_t)npuipsec_probe_fail,
	    npuipsec_accept_decrypt ? "on" : "off", (uintmax_t)npuipsec_accepted,
	    (uintmax_t)npuipsec_accept_fail, (uintmax_t)npuipsec_removed);
	for (i = 0; i < NPUIPSEC_SEEN_MAX; i++) {
		mtx_lock(&npuipsec_mtx);
		c = npuipsec_seen[i];
		mtx_unlock(&npuipsec_mtx);
		if (!c.used)
			continue;
		n++;
		sbuf_printf(sb, "%-6s spi 0x%08x %s %s alg %u %ds ago", c.ifname, c.spi,
		    c.dir == 1 ? "decrypt" : "encrypt", c.family == AF_INET6 ? "inet6" : "inet",
		    c.alg, (ticks - c.when) / hz);
		if (c.probed)
			sbuf_printf(sb, " | SA_ADD %s",
			    c.engine_rc == 0 ? "accepted by the engine" :
			    (c.engine_rc < 0 ? "not sent" : "refused"));
		sbuf_cat(sb, "\n");
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
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "probe", CTLFLAG_RW,
	    &npuipsec_probe_on, 0, "test aid: hand each offered AES-GCM association to the engine "
	    "(SA_ADD) and take it straight back (SA_DEL), to see it is accepted; still declined to "
	    "the kernel. For a manual association, not a live tunnel");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "probe_ok", CTLFLAG_RD,
	    &npuipsec_probe_ok, 0, "probes the engine accepted (SA_ADD rc 0)");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "probe_fail", CTLFLAG_RD,
	    &npuipsec_probe_fail, 0, "probes the engine refused or that did not send");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "accept_decrypt", CTLFLAG_RW,
	    &npuipsec_accept_decrypt, 0, "accept an offered AES-GCM DECRYPT association for real "
	    "(SA_ADD, kept, returned to the kernel) so the engine decrypts inbound ESP in place and "
	    "marks the frame - off by default; without the receive termination the frames are not "
	    "delivered, so this is to see the crypto marker, not carry the tunnel");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "accepted", CTLFLAG_RD,
	    &npuipsec_accepted, 0, "decrypt associations installed on the engine and kept");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "accept_fail", CTLFLAG_RD,
	    &npuipsec_accept_fail, 0, "decrypt associations the engine refused, or no free index");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "removed", CTLFLAG_RD,
	    &npuipsec_removed, 0, "accepted associations the kernel later removed (SA_DEL)");
}
