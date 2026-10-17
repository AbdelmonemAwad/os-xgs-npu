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
#include <sys/mbuf.h>
#include <machine/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_private.h>
#include <net/ethernet.h>
#include <net/bpf.h>
#include <net/pfil.h>
#include <net/netisr.h>
#include <net/vnet.h>
#include <net/pfkeyv2.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <machine/in_cksum.h>

#include <netipsec/ipsec.h>
#include <netipsec/keydb.h>
#include <netipsec/key.h>
#include <netipsec/xform.h>
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
#define	NPUIPSEC_SA_OFF_NATDPORT	160	/* be16: the far end's UDP port of a NAT-T association */
#define	NPUIPSEC_SA_OFF_NATSPORT	162	/* be16: this end's */
#define	NPUIPSEC_SA_OFF_REV	184	/* le16 */

/* ctrl field bits, the OCTEON TX layout. */
#define	NPUIPSEC_HASH_GF128_128	11	/* GCM's hash */
#define	NPUIPSEC_CIMODE_CTR	4	/* GCM runs the block cipher in counter mode */
#define	NPUIPSEC_CIMODE_CBC	1	/* AES-CBC with an HMAC */
#define	NPUIPSEC_IV_MIN		8	/* the smallest IV any taken association has (GCM); CBC's is 16 */
#define	NPUIPSEC_ICV_MIN	12	/* the smallest ICV (SHA1_96); GCM 16, SHA2 up to 32 */
#define	NPUIPSEC_OVH_GCM_128	2	/* overhead type, opt bits 24..31 */
#define	NPUIPSEC_SA_IDX_MAX	16	/* probe indices 1..15; 0 is never used */
/*
 * The largest replay window the engine's association holds: USFP_SA_RWIN_MAX in the vendor's
 * sa_table.h, the OCTEON TX's figure, with a bitmap of twice that. Nothing in SA_ADD checks the size
 * it is sent - the bucket index is masked, so a larger window would be accepted and tracked wrongly
 * rather than refused - so the check is here, and such an association stays with the kernel.
 */
#define	NPUIPSEC_SA_WIN_MAX	4096

#define	NPUIPSEC_SEEN_MAX	16

struct npuipsec_seen {
	int		used;
	uint32_t	spi;		/* host order, for the page */
	int		dir;		/* 0 encrypt (this port owns the source), 1 decrypt (the dest) */
	uint8_t		family;		/* the outer header's: AF_INET or AF_INET6 */
	uint8_t		alg;		/* sav->alg_enc */
	int		probed;		/* 1 if SA_ADD/SA_DEL were posted for it */
	int		engine_rc;	/* the engine's answer to SA_ADD: 0 = accepted */
	uint32_t	win;		/* the decrypt side's replay window in packets, as the engine was asked (0 none) */
	int		win_refused;	/* 1 if the window kept it with the kernel: not a power of two, or above the engine's */
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
static uint64_t			npuipsec_win_refused;	/* decrypt associations left to the kernel for their window */

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
	uint32_t	dst;		/* the outer destination, network order: the kernel's key for the association */
	/* The encrypt side (dir 0), taken since the transmit slice: */
	uint32_t	src;		/* the outer source, network order */
	void		*sav;		/* the kernel's association: compared, never dereferenced from the data path */
	int		port;		/* the front port's index: the ESP frame leaves by it */
	int		ready;		/* the engine has it; a packet may be handed over */
	int		taken;		/* the kernel's cipher has been taken from it */
	uint64_t	seq;		/* the number the engine was started after */
	uint64_t	handed;		/* ESP frames handed to the engine on it: each took a number */
	uint16_t	drv_spi;	/* the kernel's handle for (interface, association): its offload path names it by this */
	int		natt;		/* UDP-encapsulated (RFC 3948): the engine removes the header on receive, the envelope writes it */
	uint16_t	natt_sport, natt_dport;	/* host order */
	uint8_t		ivlen, icvlen;	/* the envelope's IV room and ICV room: 8 and 16 for GCM, 16 and the HMAC's for CBC */
	uint8_t		blk;		/* the trailer's alignment: 4 for GCM, 16 for CBC (RFC 4303 2.4) */
	uint8_t		cbc;		/* the IV is this driver's to write (random), not the engine's counter */
	uint32_t	win;		/* the decrypt side's replay window in packets; the engine checks it */
};

/*
 * What an association's algorithms are to the engine, and what its envelope looks like: the
 * control word's fields and the options' overhead type as the vendor's own host sets them
 * (generate_usfp_control), and the IV, ICV and trailer alignment the frames carry. AES-GCM-16, or
 * AES-CBC with HMAC-SHA-1/256/384/512 - the ICV is the kernel's truncation, half the hash for SHA-2
 * (RFC 4868). The OCTEON TX took the same set.
 */
struct npuipsec_alg {
	uint32_t	cipher, hash, cimode, ovh;
	uint8_t		ivlen, icvlen, blk, cbc;
	int		keylen, authkeylen;	/* the cipher key (without GCM's salt); the HMAC key for CBC */
};

static int
npuipsec_alg_of(const struct secasvar *sav, struct npuipsec_alg *a)
{
	int keylen;

	bzero(a, sizeof(*a));
	if (sav->key_enc == NULL)
		return (EOPNOTSUPP);
	if (sav->alg_enc == SADB_X_EALG_AESGCM16) {
		keylen = _KEYLEN(sav->key_enc) - 4;	/* the 4-byte salt rides at the end, RFC 4106 */
		a->hash = NPUIPSEC_HASH_GF128_128;
		a->cimode = NPUIPSEC_CIMODE_CTR;
		a->ovh = NPUIPSEC_OVH_GCM_128;
		a->ivlen = 8;			/* the explicit IV after the ESP header */
		a->icvlen = 16;			/* GCM-16 */
		a->blk = 4;
	} else if (sav->alg_enc == SADB_X_EALG_AES && sav->key_auth != NULL) {
		keylen = _KEYLEN(sav->key_enc);
		switch (sav->alg_auth) {
		case SADB_AALG_SHA1HMAC:	a->hash = 2;  a->ovh = 3; a->icvlen = 12; break;	/* SHA1_96, BLOCK_96 */
		case SADB_X_AALG_SHA2_256:	a->hash = 8;  a->ovh = 4; a->icvlen = 16; break;	/* SHA256_128, BLOCK_128 */
		case SADB_X_AALG_SHA2_384:	a->hash = 9;  a->ovh = 5; a->icvlen = 24; break;	/* SHA384_192, BLOCK_192 */
		case SADB_X_AALG_SHA2_512:	a->hash = 10; a->ovh = 6; a->icvlen = 32; break;	/* SHA512_256, BLOCK_256 */
		default:
			return (EOPNOTSUPP);
		}
		a->cimode = NPUIPSEC_CIMODE_CBC;
		a->ivlen = 16;
		a->blk = 16;
		a->cbc = 1;
		a->authkeylen = _KEYLEN(sav->key_auth);
		if (a->authkeylen <= 0 || a->authkeylen > 64)
			return (EOPNOTSUPP);
	} else
		return (EOPNOTSUPP);
	if (keylen != 16 && keylen != 24 && keylen != 32)
		return (EOPNOTSUPP);
	a->keylen = keylen;
	a->cipher = (keylen == 16) ? 2 : (keylen == 24) ? 3 : 4;	/* AES128/192/256 */
	return (0);
}
static int			npuipsec_accept_decrypt;
static struct npuipsec_sa	npuipsec_sa_tab[NPUIPSEC_SA_IDX_MAX];
static uint64_t			npuipsec_accepted, npuipsec_accept_fail, npuipsec_removed;

/*
 * The receive termination's envelope: AES-GCM-16 only for now, as the SA_ADD above is - an 8-byte
 * explicit IV after the 8-byte ESP header, a 16-byte ICV at the end; and RFC 3948's 8-byte UDP header
 * an association behind a NAT rides on. The OCTEON TX driver's constants.
 */
#define	NPUIPSEC_ESP_HDRLEN	8
#define	NPUIPSEC_SA_IVLEN	8
#define	NPUIPSEC_SA_ICVLEN	16
#define	NPUIPSEC_NATT_UDPLEN	8u
/*
 * The DLT_ENC header enc(4) puts in front of a tapped packet, and its two flags: struct enchdr in
 * sys/net/if_enc.h, written out so the module does not depend on a header that is not installed
 * everywhere. A capture on enc0 then decodes a frame this driver delivered exactly as one the kernel
 * decrypted.
 */
struct npuipsec_enchdr {
	uint32_t	af;
	uint32_t	spi;
	uint32_t	flags;
};
#define	NPUIPSEC_ENC_M_CONF	0x0400	/* payload was encrypted */
#define	NPUIPSEC_ENC_M_AUTH	0x0800	/* payload was authenticated */
static if_t			npuipsec_enc;		/* enc0, looked up once and kept */
/* dev.npuep.0.ipsec.rx_*: what became of each frame the engine decrypted and handed up. */
static uint64_t			npuipsec_rx_done, npuipsec_rx_nosa, npuipsec_rx_nokey, npuipsec_rx_bad,
				npuipsec_rx_noenc, npuipsec_rx_blocked, npuipsec_rx_queuefail;
static uint32_t			npuipsec_rx_last_word;	/* the last sa word a marked frame carried */

/*
 * The transmit side. ONE ENCRYPTOR PER ASSOCIATION, the OCTEON TX driver's rule: an ESP sequence
 * number may be used once, the engine keeps its own counter for an association it encrypts on and
 * the kernel keeps another, so once the engine has an association every packet of it must go there.
 * The kernel's own offload output path covers only a packet generated here and routed out of the
 * association's interface; a forwarded packet is still encrypted in software. So the association's
 * ESP transform is interposed: where the kernel would call esp_output it calls npuipsec_xf_output,
 * which lays the envelope out and hands the frame to the port with the metadata that asks the
 * engine to encrypt it. The kernel does everything else it always did - policy, the association,
 * enc0's capture and rules, the outer header this discards.
 */
#define	NPUIPSEC_SA_SEQ_AHEAD		(1u << 20)	/* the engine starts this far past the kernel's counter */
#define	NPUIPSEC_SA_SEQ_SEED_MAX	(1u << 31)	/* a counter past this is not taken: the engine's is 32 bits */
#define	NPUIPSEC_SAV_F_CLONED		0x80000000u	/* key.c's private mark on an association it has cloned */
static int			npuipsec_accept_encrypt;
static const struct xformsw	*npuipsec_esp_orig;	/* the kernel's ESP transform, kept for what is not ours */
static struct xformsw		 npuipsec_esp_xformsw;	/* its copy, with the output step taken */
static uint64_t			npuipsec_accepted_enc, npuipsec_accept_enc_fail, npuipsec_removed_enc;
static uint64_t			npuipsec_tx_taken, npuipsec_tx_orig, npuipsec_tx_drop, npuipsec_tx_nonhop,
				npuipsec_tx_toobig, npuipsec_tx_nobuf, npuipsec_tx_accel, npuipsec_tx_bypass,
				npuipsec_tx_nosa, npuipsec_tx_fragmented, npuipsec_tx_needfrag, npuipsec_tx_natt;
static int	npuipsec_xf_output(struct mbuf *m, struct secpolicy *sp, struct secasvar *sav, u_int idx,
		    int skip, int protoff);

/* An association the kernel has cloned for a changed address, or let die: nothing of it is touched. */
static inline int
npuipsec_sav_let_go(const struct secasvar *sav)
{
	return (sav->state == SADB_SASTATE_DEAD || (sav->flags & NPUIPSEC_SAV_F_CLONED) != 0);
}

/* The front port's index this ifp is, or -1. */
static int
npuipsec_port_index_of(if_t ifp)
{
	int i, n;

	n = npugiu_port_count();
	for (i = 0; i < n; i++)
		if ((if_t)npugiu_port_ifp(i) == ifp)
			return (i);
	return (-1);
}

/*
 * Take the association's output step: a copy of the kernel's ESP transform whose xf_output is
 * npuipsec_xf_output and whose other members are the kernel's own, so decryption and cleanup are
 * untouched and the one thing that changes is who is asked to encrypt. The kernel runs if_sa_newkey
 * on its single offload taskqueue with a reference on the association, so two of these never race.
 */
static bool
npuipsec_interpose(struct secasvar *sav)
{
	const struct xformsw *cur = sav->tdb_xform;

	if (cur == NULL)
		return (false);
	if (cur == &npuipsec_esp_xformsw)
		return (true);
	if (npuipsec_esp_orig == NULL) {
		if (cur->xf_type != XF_ESP || cur->xf_output == NULL || cur->xf_cleanup == NULL)
			return (false);
		npuipsec_esp_xformsw = *cur;
		memset(&npuipsec_esp_xformsw.chain, 0, sizeof(npuipsec_esp_xformsw.chain));
		npuipsec_esp_xformsw.xf_cntr = 0;
		npuipsec_esp_xformsw.xf_output = npuipsec_xf_output;
		npuipsec_esp_orig = cur;
	} else if (cur != npuipsec_esp_orig)
		return (false);
	sav->tdb_xform = &npuipsec_esp_xformsw;
	return (true);
}

static bool
npuipsec_can_interpose(const struct secasvar *sav)
{
	const struct xformsw *cur = sav->tdb_xform;

	if (cur == NULL)
		return (false);
	if (cur == &npuipsec_esp_xformsw)
		return (true);
	if (npuipsec_esp_orig == NULL)
		return (cur->xf_type == XF_ESP && cur->xf_output != NULL && cur->xf_cleanup != NULL);
	return (cur == npuipsec_esp_orig);
}

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
 * Build and post one SA_ADD, read the engine's answer. Returns the engine's rc (0 = accepted) or a
 * negative errno on a transport failure. No lock held; this sleeps in the RPC. The association stays
 * installed on the engine; the caller frees it with npuipsec_sa_del. For GCM the AES key sits in the
 * key field and the four-byte salt in the first word of the auth-key field; for CBC the HMAC key is
 * the auth-key field.
 */
/*
 * The replay window the kernel keeps is in BYTES of bitmap (keydb.h: "window size, i.g. 4 bytes" -
 * that is 32 packets, strongSwan's default); the engine wants packets, and a power of two no larger
 * than NPUIPSEC_SA_WIN_MAX. Only the decrypt side checks replay: the encrypt side's window is 0 and
 * its anti-replay bit off, as the vendor sends it. Until this was sent the decrypt association went
 * to the engine with a window of 0 and the bit off, and the termination does not advance the
 * kernel's window either - so an offloaded inbound association had NO replay check at all.
 */
static uint32_t
npuipsec_win_of(const struct secasvar *sav, int dir)
{

	return ((dir == 1 && sav->replay != NULL) ? sav->replay->wsize * 8 : 0);
}

static int
npuipsec_win_ok(uint32_t win)
{

	return (win == 0 || ((win & (win - 1)) == 0 && win <= NPUIPSEC_SA_WIN_MAX));
}

static int
npuipsec_sa_add(if_t ifp, struct secasvar *sav, int dir, uint32_t idx, uint8_t rev, uint64_t seq)
{
	const struct secasindex *saidx = &sav->sah->saidx;
	struct npuipsec_alg a;
	uint8_t pl[NPUIPSEC_SA_REQ_LEN];
	uint32_t ctrl, opt, win;
	int n, rc;

	if (npuipsec_alg_of(sav, &a) != 0)
		return (-EINVAL);
	win = npuipsec_win_of(sav, dir);
	if (!npuipsec_win_ok(win))
		return (-EINVAL);		/* never sent: the engine would track it wrongly */

	memset(pl, 0, sizeof(pl));
	le32enc(pl + NPUIPSEC_SA_OFF_IDX, idx);
	le32enc(pl + NPUIPSEC_SA_OFF_LIF, (uint32_t)npuipsec_iface_of(ifp) << 12);
	memcpy(pl + NPUIPSEC_SA_OFF_KEY, sav->key_enc->key_data, a.keylen);
	if (a.cbc)
		memcpy(pl + NPUIPSEC_SA_OFF_AUTHKEY, sav->key_auth->key_data, (size_t)a.authkeylen);
	else
		memcpy(pl + NPUIPSEC_SA_OFF_AUTHKEY, sav->key_enc->key_data + a.keylen, 4);	/* the AEAD salt */
	ctrl = (a.hash & 0xf) | ((a.cimode & 0xf) << 4) | ((a.cipher & 0xf) << 8) |
	    (1u << 12) /* tunnel */ | (1u << 14) /* ESP */ |
	    (((uint32_t)dir & 1u) << 16) | ((win != 0 ? 1u : 0u) << 17) /* anti-replay */ |
	    (1u << 31) /* valid */;
	le32enc(pl + NPUIPSEC_SA_OFF_CTRL, ctrl);
	opt = (a.ovh << 24);
	if (sav->natt != NULL) {
		/*
		 * Behind a NAT (RFC 3948): udp_enable, bit 22 of the options, and the two ports - what
		 * the vendor's host sets for a state with encapsulation, and what the OCTEON TX driver
		 * measured: the engine removes the UDP header on receive and expects it in the frame it
		 * is handed to encrypt. The kernel keeps the ports in network order; the request wants
		 * them big-endian on the wire, so they go through host order as the OCTEON TX's do.
		 */
		opt |= 1u << 22;
		be16enc(pl + NPUIPSEC_SA_OFF_NATDPORT, ntohs(sav->natt->dport));
		be16enc(pl + NPUIPSEC_SA_OFF_NATSPORT, ntohs(sav->natt->sport));
	}
	le32enc(pl + NPUIPSEC_SA_OFF_OPT, opt);
	le32enc(pl + NPUIPSEC_SA_OFF_WIN, win);
	be32enc(pl + NPUIPSEC_SA_OFF_SPI, ntohl(sav->spi));
	le64enc(pl + NPUIPSEC_SA_OFF_SEQ, seq);	/* inbound 0; outbound the number the engine starts after */
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

	rc = npuipsec_sa_add(ifp, sav, dir, idx, rev, 0);
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
	struct npuipsec_alg alg;
	int dir, rc, ok4;

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
	e->win = npuipsec_win_of(sav, dir);
	e->win_refused = 0;
	strlcpy(e->ifname, if_name(ifp), sizeof(e->ifname));
	e->when = ticks;
	mtx_unlock(&npuipsec_mtx);

	/* AES-GCM-16, or AES-CBC with an HMAC the engine knows, over IPv4: what this driver takes. */
	ok4 = (npuipsec_alg_of(sav, &alg) == 0 && saidx->dst.sa.sa_family == AF_INET);

	/*
	 * Accept the decrypt association for real, if asked (off by default). The engine is given the
	 * association and keeps it, and the kernel is told we own it (return 0), so the engine decrypts
	 * the inbound ESP in place and punts the frame with the handle in the metadata. Without the
	 * receive termination the frame is not delivered, so this is for SEEING the marker, not carrying
	 * the tunnel - hence gated. Only the decrypt direction: the encrypt side is the transmit slice.
	 */
	if (npuipsec_accept_decrypt && ok4 && dir == 1 &&
	    (sav->natt == NULL || (sav->natt->sport != 0 && sav->natt->dport != 0))) {
		mtx_lock(&npuipsec_mtx);
		if (!npuipsec_win_ok(npuipsec_win_of(sav, dir))) {
			/* A window the engine cannot hold: the kernel keeps the association and its own check. */
			npuipsec_seen[si].win_refused = 1;
			npuipsec_win_refused++;
			mtx_unlock(&npuipsec_mtx);
			return (EOPNOTSUPP);
		}
		s = npuipsec_sa_alloc(&idx, &rev, ntohl(sav->spi), dir);
		if (s != NULL) {
			s->dst = saidx->dst.sin.sin_addr.s_addr;	/* what npuipsec_rx looks the kernel's association up by */
			s->win = npuipsec_win_of(sav, dir);
			s->drv_spi = (uint16_t)drv_spi;
			s->natt = (sav->natt != NULL);
			s->natt_sport = s->natt ? ntohs(sav->natt->sport) : 0;
			s->natt_dport = s->natt ? ntohs(sav->natt->dport) : 0;
			s->ivlen = alg.ivlen; s->icvlen = alg.icvlen; s->blk = alg.blk; s->cbc = alg.cbc;
		}
		mtx_unlock(&npuipsec_mtx);
		if (s == NULL) {
			mtx_lock(&npuipsec_mtx);
			npuipsec_accept_fail++;
			mtx_unlock(&npuipsec_mtx);
			return (EOPNOTSUPP);
		}
		rc = npuipsec_sa_add(ifp, sav, dir, idx, rev, 0);
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
	 * Accept the encrypt association for real, if asked (off by default): the engine is given it
	 * and the kernel's cipher is taken from it, so every packet of it is encrypted by the engine
	 * from a frame this driver lays out (npuipsec_xf_output). The order is the OCTEON TX driver's:
	 * the engine first, while the kernel's cipher still runs; the cipher taken second, when there
	 * is something to take it. Plain tunnel-mode ESP over IPv4, AES-GCM-16 or AES-CBC with an HMAC: no NAT-T
	 * and no extended sequence numbers; NAT-T is taken, the envelope writes the UDP header.
	 */
	if (npuipsec_accept_encrypt && ok4 && dir == 0 &&
	    (sav->natt == NULL || (sav->natt->sport != 0 && sav->natt->dport != 0)) &&
	    (sav->flags & SADB_X_SAFLAGS_ESN) == 0 && saidx->mode == IPSEC_MODE_TUNNEL) {
		struct npuep_nhop nh;
		uint64_t seq = 0, klow = 0;
		int port, gone, err;

		/*
		 * Owning the source is not enough: the ESP frame leaves by the route to the far end,
		 * and if that is another port every packet would be dropped for want of a next hop. A
		 * neighbour that is merely not resolved yet is fine - the lookup has just asked for it
		 * - as long as the route leaves by this port.
		 */
		port = npuipsec_port_index_of(ifp);
		err = npuep_nhop_resolve(saidx->dst.sin.sin_addr.s_addr, -1, &nh);
		if (port < 0 || (err != 0 && err != EWOULDBLOCK) || nh.port != port ||
		    !npuipsec_can_interpose(sav) || npuipsec_sav_let_go(sav)) {
			mtx_lock(&npuipsec_mtx);
			npuipsec_accept_enc_fail++;
			mtx_unlock(&npuipsec_mtx);
			return (EOPNOTSUPP);
		}
		mtx_lock(&npuipsec_mtx);
		s = npuipsec_sa_alloc(&idx, &rev, ntohl(sav->spi), dir);
		if (s != NULL) {
			s->dst = saidx->dst.sin.sin_addr.s_addr;
			s->src = saidx->src.sin.sin_addr.s_addr;
			s->sav = sav;
			s->port = port;
			s->drv_spi = (uint16_t)drv_spi;
			s->natt = (sav->natt != NULL);
			s->natt_sport = s->natt ? ntohs(sav->natt->sport) : 0;
			s->natt_dport = s->natt ? ntohs(sav->natt->dport) : 0;
			s->ivlen = alg.ivlen; s->icvlen = alg.icvlen; s->blk = alg.blk; s->cbc = alg.cbc;
			s->ready = s->taken = 0;
			s->seq = s->handed = 0;
		}
		mtx_unlock(&npuipsec_mtx);
		if (s == NULL) {
			mtx_lock(&npuipsec_mtx);
			npuipsec_accept_enc_fail++;
			mtx_unlock(&npuipsec_mtx);
			return (EOPNOTSUPP);
		}
		/*
		 * The kernel's IV counter is moved into the upper half of its space, under the lock
		 * esp_output takes its numbers under: from there every IV the kernel still uses is one
		 * the engine, whose IV is a 32-bit sequence number, cannot. The engine is started a
		 * million numbers past the kernel's sequence counter, and past its old IV counter.
		 */
		gone = npuipsec_sav_let_go(sav);
		if (!gone) {
			SECASVAR_WLOCK(sav);
			if (npuipsec_sav_let_go(sav))
				gone = 1;
			else {
				if ((sav->cntr >> 63) == 0)
					klow = sav->cntr;
				sav->cntr |= (uint64_t)1 << 63;
			}
			SECASVAR_WUNLOCK(sav);
		}
		if (!gone && sav->replay != NULL) {
			SECREPLAY_LOCK(sav->replay);
			seq = sav->replay->count;
			SECREPLAY_UNLOCK(sav->replay);
		}
		if (klow > seq)
			seq = klow;
		if (gone || seq > NPUIPSEC_SA_SEQ_SEED_MAX) {
			mtx_lock(&npuipsec_mtx);
			npuipsec_sa_free(s);
			npuipsec_accept_enc_fail++;
			mtx_unlock(&npuipsec_mtx);
			return (EOPNOTSUPP);	/* the kernel keeps it, from an upper-half IV: as good as any */
		}
		seq += NPUIPSEC_SA_SEQ_AHEAD;
		mtx_lock(&npuipsec_mtx);
		s->seq = seq;
		mtx_unlock(&npuipsec_mtx);
		rc = npuipsec_sa_add(ifp, sav, dir, idx, rev, seq);
		if (rc != 0) {
			mtx_lock(&npuipsec_mtx);
			npuipsec_sa_free(s);
			npuipsec_accept_enc_fail++;
			npuipsec_seen[si].probed = 1;
			npuipsec_seen[si].engine_rc = rc;
			mtx_unlock(&npuipsec_mtx);
			return (EOPNOTSUPP);	/* nothing was taken: the kernel is still the only encryptor */
		}
		/* Ready, taken and the transform swapped in one hold: no reader sees them apart. */
		mtx_lock(&npuipsec_mtx);
		gone = npuipsec_sav_let_go(sav);
		if (!gone) {
			s->ready = 1;
			s->taken = npuipsec_interpose(sav) ? 1 : 0;
		}
		if (gone || !s->taken) {
			s->ready = 0;
			mtx_unlock(&npuipsec_mtx);
			npuipsec_sa_del(idx);
			mtx_lock(&npuipsec_mtx);
			npuipsec_sa_free(s);
			npuipsec_accept_enc_fail++;
			mtx_unlock(&npuipsec_mtx);
			return (EOPNOTSUPP);
		}
		npuipsec_seen[si].probed = 1;
		npuipsec_seen[si].engine_rc = 0;
		npuipsec_accepted_enc++;
		mtx_unlock(&npuipsec_mtx);
		/* The packets already inside the kernel's cipher finish there, with the kernel's numbers. */
		NET_EPOCH_WAIT();
		*privp = s;
		return (0);		/* ACCEPTED: the engine encrypts, from frames this driver lays out */
	}

	/*
	 * The probe (a test aid, off by default): install, see the engine accept it, take it straight
	 * back, and still decline - the SA_ADD bytes proven without a data path.
	 */
	if (npuipsec_probe_on && ok4) {
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
	struct secasvar *sav;
	uint64_t past;
	int enc;

	if (s == NULL)
		return (0);
	/* Not ready first: from here a packet for it goes to the kernel's cipher, not to an engine about to forget it. */
	mtx_lock(&npuipsec_mtx);
	enc = (s->dir == 0);
	sav = s->sav;
	past = s->seq + s->handed + 1;
	s->ready = 0;
	mtx_unlock(&npuipsec_mtx);
	npuipsec_sa_del(s->idx);
	/*
	 * The kernel gets its counter back in a state it can use: once the record is gone the
	 * interposed output step passes this association's packets to the kernel's cipher, whose
	 * counter is where it was taken - numbers the peer has seen. The engine used one per frame
	 * handed over, so the kernel resumes after the last of them. The kernel still references
	 * the association across this call; one it has cloned or let die is left alone.
	 */
	if (enc && sav != NULL && !npuipsec_sav_let_go(sav) && sav->replay != NULL) {
		SECREPLAY_LOCK(sav->replay);
		if (sav->replay->count < past)
			sav->replay->count = past;
		SECREPLAY_UNLOCK(sav->replay);
	}
	mtx_lock(&npuipsec_mtx);
	s->sav = NULL;
	npuipsec_sa_free(s);
	if (enc)
		npuipsec_removed_enc++;
	else
		npuipsec_removed++;
	mtx_unlock(&npuipsec_mtx);
	return (0);
}

/* enc0, looked up once and kept; the reference is held for the module's life. */
static if_t
npuipsec_enc_get(void)
{
	if_t ifp;

	if (npuipsec_enc != NULL)
		return (npuipsec_enc);
	ifp = ifunit_ref("enc0");
	if (ifp == NULL)
		return (NULL);
	mtx_lock(&npuipsec_mtx);
	if (npuipsec_enc == NULL) {
		npuipsec_enc = ifp;
		ifp = NULL;
	}
	mtx_unlock(&npuipsec_mtx);
	if (ifp != NULL)
		if_rele(ifp);
	return (npuipsec_enc);
}

/*
 * The receive termination: a frame the engine decrypted on the way in. Consumes the mbuf.
 *
 * What arrives is the OCTEON TX's shape, byte for byte - measured 2026-10-14 on npup7 with tcpdump -X
 * against a live AES-GCM-16 tunnel:
 *
 *   Ethernet 14 | outer IPv4 | ESP: SPI, sequence | 8 bytes where the IV was, now L2 residue |
 *   the inner packet, plaintext | pad, pad length, next header | ICV 16
 *
 * same length as the frame on the wire, and the metadata word at +16 names the association that did
 * it: the handle (the SA_ADD index plus one) in the low half, its revision in the high. The kernel has
 * no entry for that shape - esp_input would run the cipher over the plaintext and drop it - so this
 * strips the front down to the inner packet and the tail up to the pad length the trailer states, and
 * then does what the kernel's own input callback does after esp_input_cb: records what was done
 * (PACKET_TAG_IPSEC_IN_DONE, which also makes ip_input skip the port's filter rules for it), marks it
 * decrypted (which the inbound policy check requires), counts it on the kernel's association, shows it
 * to enc0's capture and firewall rules the way enc_hhook does, and queues it to IP. The replay window is
 * the engine's, which checked it (since the window slice: before it the association was sent with a
 * window of 0 and the bit off, and there was no check at all - measured with a replayed frame); the
 * kernel's own window for this association is not advanced.
 * octep_ipsec_rx, with this family's table: IPv4 outer and inner, GCM's envelope.
 *
 * Called from the GIU tick with its lock DROPPED (the frame was taken off the ring under the lock and
 * kept on a second chain for this), in the callout thread, which has no vnet - hence CURVNET_SET, the
 * lesson the OCTEON TX driver paid for with a page fault in key_allocsa.
 */
void
npuipsec_rx(if_t ifp, struct mbuf *m, uint32_t sa_word)
{
	struct epoch_tracker et;
	struct secasvar *sav = NULL;
	struct npuipsec_sa *s;
	struct m_tag *mtag;
	struct xform_history *xh;
	struct npuipsec_enchdr eh;
	union sockaddr_union su;
	if_t enc, rcvif;
	const uint8_t *p;
	uint32_t handle, spi, ihl, udp, front, back, len, ivlen, icvlen;
	uint16_t etype;
	uint8_t tail[2], proto;

	npuipsec_rx_last_word = sa_word;
	handle = sa_word & 0xffffu;
	len = m->m_pkthdr.len;
	p = mtod(m, const uint8_t *);	/* the receive copy is one mbuf, sized for the frame */
	/* The floors here are the smallest IV and ICV any association has; the exact ones are checked below. */
	if (len < ETHER_HDR_LEN + 20 + NPUIPSEC_ESP_HDRLEN + NPUIPSEC_IV_MIN + 20 + 2 + NPUIPSEC_ICV_MIN)
		goto bad;
	etype = be16dec(p + 12);
	if (etype != ETHERTYPE_IP || (p[ETHER_HDR_LEN] >> 4) != 4)
		goto bad;	/* an IPv6 outer header is not taken yet: the SA_ADD above is IPv4 only */
	ihl = (uint32_t)(p[ETHER_HDR_LEN] & 0x0f) * 4;
	proto = p[ETHER_HDR_LEN + 9];
	/*
	 * Plain ESP, or ESP behind a UDP header (NAT-T, RFC 3948): whether the frame handed back still
	 * carries that header is the engine's to decide, so both shapes are taken. A NAT-T keep-alive -
	 * one byte of 0xff - is far below the length floor and goes to bad with everything else short.
	 */
	udp = (proto == IPPROTO_UDP) ? NPUIPSEC_NATT_UDPLEN : 0;
	if (ihl < 20 || (proto != IPPROTO_ESP && udp == 0) ||
	    len < ETHER_HDR_LEN + ihl + udp + NPUIPSEC_ESP_HDRLEN + NPUIPSEC_IV_MIN + 20 + 2 + NPUIPSEC_ICV_MIN)
		goto bad;
	memcpy(&spi, p + ETHER_HDR_LEN + ihl + udp, 4);	/* network order, compared as such */
	if (udp != 0 && spi == 0)
		goto bad;	/* an IKE packet on port 4500 has a zero non-ESP marker there */

	mtx_lock(&npuipsec_mtx);
	s = (handle >= 2 && handle - 1 < NPUIPSEC_SA_IDX_MAX) ? &npuipsec_sa_tab[handle - 1] : NULL;
	if (s == NULL || !s->used || s->dir != 1 || s->spi != ntohl(spi) ||
	    s->rev != (uint8_t)(sa_word >> 16)) {
		/*
		 * The revision too: an index is reused, and the revision is what tells its occupants
		 * apart. The vendor's host drops on the same mismatch.
		 */
		mtx_unlock(&npuipsec_mtx);
		npuipsec_rx_nosa++;
		goto drop;
	}
	bzero(&su, sizeof(su));
	su.sin.sin_len = sizeof(struct sockaddr_in);
	su.sin.sin_family = AF_INET;
	su.sin.sin_addr.s_addr = s->dst;
	ivlen = s->ivlen;
	icvlen = s->icvlen;
	mtx_unlock(&npuipsec_mtx);
	if (len < ETHER_HDR_LEN + ihl + udp + NPUIPSEC_ESP_HDRLEN + ivlen + 20 + 2 + icvlen) {
		npuipsec_rx_bad++;
		goto drop;
	}

	CURVNET_SET(ifp->if_vnet);
	NET_EPOCH_ENTER(et);
	sav = key_allocsa(&su, IPPROTO_ESP, spi);
	if (sav == NULL) {
		/* The kernel's association is gone - a rekey the engine has not heard of yet. */
		npuipsec_rx_nokey++;
		goto drop_epoch;
	}

	front = ETHER_HDR_LEN + ihl + udp + NPUIPSEC_ESP_HDRLEN + ivlen;
	m_copydata(m, len - icvlen - 2, 2, tail);	/* pad length, next header */
	back = icvlen + 2 + tail[0];
	/* The inner packet's family, from the trailer: IPv4 only here, IPv6 inside is a later slice. */
	if (tail[1] != IPPROTO_IPV4 || len < front + back + 20) {
		npuipsec_rx_bad++;
		goto drop_epoch;
	}
	m_adj(m, (int)front);
	m_adj(m, -(int)back);
	p = mtod(m, const uint8_t *);
	if ((p[0] >> 4) != 4 || be16dec(p + 2) != m->m_pkthdr.len) {
		npuipsec_rx_bad++;
		goto drop_epoch;
	}

	mtag = m_tag_get(PACKET_TAG_IPSEC_IN_DONE, sizeof(*xh), M_NOWAIT);
	if (mtag == NULL) {
		npuipsec_rx_bad++;
		goto drop_epoch;
	}
	xh = (struct xform_history *)(mtag + 1);
	bcopy(&sav->sah->saidx.dst, &xh->dst, sav->sah->saidx.dst.sa.sa_len);
	xh->spi = sav->spi;
	xh->proto = IPPROTO_ESP;
	xh->mode = sav->sah->saidx.mode;
	m_tag_prepend(m, mtag);
	m->m_flags |= M_DECRYPTED;
	key_sa_recordxfer(sav, m);

	/*
	 * enc0, as enc_hhook does it at IPSEC_ENC_AFTER: the capture with the DLT_ENC header, and the
	 * inbound filter rules with the packet looking as if it arrived on enc0 - which is where
	 * OPNsense's IPsec rules live. An appliance without enc0 gets the frame dropped, not delivered
	 * unfiltered.
	 */
	enc = npuipsec_enc_get();
	if (enc == NULL) {
		npuipsec_rx_noenc++;
		goto drop_epoch;
	}
	if ((if_getflags(enc) & IFF_UP) != 0) {
		if (bpf_peers_present_if(enc)) {
			eh.af = AF_INET;
			eh.spi = sav->spi;
			eh.flags = NPUIPSEC_ENC_M_CONF | NPUIPSEC_ENC_M_AUTH;
			bpf_mtap2_if(enc, &eh, sizeof(eh), m);
		}
		if (PFIL_HOOKED_IN(V_inet_pfil_head)) {
			rcvif = m->m_pkthdr.rcvif;
			m->m_pkthdr.rcvif = enc;
			if (pfil_mbuf_in(V_inet_pfil_head, &m, enc, NULL) != PFIL_PASS) {
				/* Consumed by the filter, as enc_hhook reports it. */
				npuipsec_rx_blocked++;
				m = NULL;
				goto drop_epoch;
			}
			m->m_pkthdr.rcvif = rcvif;
		}
	}
	if (netisr_queue_src(NETISR_IP, (uintptr_t)sav->spi, m) != 0)
		npuipsec_rx_queuefail++;	/* netisr freed it */
	else
		npuipsec_rx_done++;
	m = NULL;
drop_epoch:
	if (sav != NULL)
		key_freesav(&sav);
	NET_EPOCH_EXIT(et);
	CURVNET_RESTORE();
drop:
	if (m != NULL)
		m_freem(m);
	return;
bad:
	npuipsec_rx_bad++;
	m_freem(m);
}

/*
 * The transmit side's per-association constants, copied out of the record under the lock for one
 * packet: the handle, the outer addresses, NAT-T, and the envelope's shape.
 */
struct npuipsec_tx {
	uint32_t	handle, spi, src, dst;	/* spi, src and dst in network order */
	uint16_t	sport, dport;		/* NAT-T, host order */
	uint8_t		natt, ivlen, icvlen, blk, cbc;
};

static void
npuipsec_tx_of(const struct npuipsec_sa *s, struct npuipsec_tx *t)
{
	t->handle = s->idx + 1;
	t->spi = htonl(s->spi);
	t->src = s->src;
	t->dst = s->dst;
	t->natt = (uint8_t)s->natt;
	t->sport = s->natt_sport;
	t->dport = s->natt_dport;
	t->ivlen = s->ivlen;
	t->icvlen = s->icvlen;
	t->blk = s->blk != 0 ? s->blk : 4;
	t->cbc = s->cbc;
}

/*
 * The largest inner packet whose envelope still fits the port's IP MTU: an outer IPv4 header, the
 * NAT-T header if any, the ESP header and the IV in front; the trailer aligned to the cipher's block
 * and the ICV behind. 0 when nothing fits.
 */
static uint32_t
npuipsec_inner_max(const struct npuipsec_tx *t, uint32_t mtu)
{
	uint32_t room, front;

	front = 20 + (t->natt ? NPUIPSEC_NATT_UDPLEN : 0) + NPUIPSEC_ESP_HDRLEN + t->ivlen;
	if (mtu < front + t->icvlen + 2 + 20 + 8)
		return (0);
	room = mtu - front - t->icvlen;
	room -= room % t->blk;
	return (room - 2);
}

/*
 * Lay the ESP frame out for the engine to encrypt in place, from the inner packet m (not consumed):
 * the tunnel's next hop, an outer IPv4 header (the engine replaces it with the association's own
 * template), the NAT-T header when the association has one, the ESP header with the SPI and a zero
 * sequence, the IV's room, the inner packet, the RFC 4303 trailer aligned to the cipher's block, the
 * ICV's room - as the OCTEON TX driver lays it out. GCM: the engine writes its counter into the IV's
 * room, and the sequence and the ICV. CBC: the IV is random and explicit, written here as the
 * kernel's own esp_output writes it. The tag npugiu_encap turns into the fast path's metadata rides
 * on the frame. NULL with *errp set when the envelope would not fit or there is no buffer.
 */
static struct mbuf *
npuipsec_wrap(const struct npuipsec_tx *t, const struct npuep_nhop *nh, struct mbuf *m, int *errp)
{
	struct npuipsec_tx_tag *tt;
	struct mbuf *n;
	struct ip oip;
	uint8_t *f, *p;
	uint32_t i, inner, inner_max, padlen, flen, udp;

	udp = t->natt ? NPUIPSEC_NATT_UDPLEN : 0;
	inner = (uint32_t)m->m_pkthdr.len;
	inner_max = npuipsec_inner_max(t, nh->mtu);
	if (inner > inner_max) {
		npuipsec_tx_toobig++;
		*errp = EMSGSIZE;
		return (NULL);
	}
	padlen = (t->blk - ((inner + 2) % t->blk)) % t->blk;
	flen = ETHER_HDR_LEN + (uint32_t)sizeof(oip) + udp + NPUIPSEC_ESP_HDRLEN + t->ivlen + inner +
	    padlen + 2 + t->icvlen;
	/*
	 * A cluster of the envelope's size, not m_get2: that serves at most a page (4096 here) and
	 * answered NULL for every envelope above it - through a gigabit link at MTU 6000 or 9000 not
	 * one segment left encrypted, while 4000, an envelope of about 4073 bytes, ran at line rate
	 * (2026-10-17, issue 422). inner_max above keeps flen within the port's MTU, which a 16 KB
	 * cluster covers.
	 */
	n = (flen > MJUM16BYTES) ? NULL : m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR,
	    flen <= MCLBYTES ? MCLBYTES : flen <= MJUMPAGESIZE ? MJUMPAGESIZE :
	    flen <= MJUM9BYTES ? MJUM9BYTES : MJUM16BYTES);
	if (n == NULL) {
		npuipsec_tx_nobuf++;
		*errp = ENOBUFS;
		return (NULL);
	}
	tt = (struct npuipsec_tx_tag *)m_tag_alloc(NPUIPSEC_MTAG_COOKIE, NPUIPSEC_MTAG_TX,
	    sizeof(*tt) - sizeof(struct m_tag), M_NOWAIT);
	if (tt == NULL) {
		m_freem(n);
		npuipsec_tx_nobuf++;
		*errp = ENOBUFS;
		return (NULL);
	}
	tt->handle = t->handle;
	tt->mtu = nh->mtu;
	m_tag_prepend(n, &tt->tag);

	f = mtod(n, uint8_t *);
	memcpy(f, nh->dmac, ETHER_ADDR_LEN);
	memcpy(f + ETHER_ADDR_LEN, nh->smac, ETHER_ADDR_LEN);
	be16enc(f + 2 * ETHER_ADDR_LEN, ETHERTYPE_IP);
	p = f + ETHER_HDR_LEN;
	bzero(&oip, sizeof(oip));
	oip.ip_v = IPVERSION;
	oip.ip_hl = sizeof(oip) >> 2;
	oip.ip_len = htons((uint16_t)(flen - ETHER_HDR_LEN));
	oip.ip_ttl = 64;
	oip.ip_p = udp ? IPPROTO_UDP : IPPROTO_ESP;
	oip.ip_src.s_addr = t->src;
	oip.ip_dst.s_addr = t->dst;
	oip.ip_sum = in_cksum_hdr(&oip);
	memcpy(p, &oip, sizeof(oip));
	p += sizeof(oip);
	if (udp != 0) {
		/*
		 * The NAT-T header, RFC 3948 2.1: the association's two ports, the length of what
		 * follows the IP header, and no checksum - handed in as the vendor's host hands it:
		 * with udp_enable the engine expects it here (the OCTEON TX measured the refusal
		 * without it).
		 */
		be16enc(p, t->sport);
		be16enc(p + 2, t->dport);
		be16enc(p + 4, (uint16_t)(udp + NPUIPSEC_ESP_HDRLEN + t->ivlen + inner + padlen + 2 + t->icvlen));
		be16enc(p + 6, 0);
		p += udp;
		npuipsec_tx_natt++;
	}
	memcpy(p, &t->spi, 4);
	bzero(p + 4, 4);			/* the sequence: the engine's to write */
	p += NPUIPSEC_ESP_HDRLEN;
	if (t->cbc)
		arc4rand(p, t->ivlen, 0);	/* the IV: random and explicit, the host's to write for CBC */
	else
		bzero(p, t->ivlen);		/* the IV's room: the engine writes its counter there */
	p += t->ivlen;
	m_copydata(m, 0, (int)inner, (caddr_t)p);
	p += inner;
	for (i = 0; i < padlen; i++)
		*p++ = (uint8_t)(i + 1);
	*p++ = (uint8_t)padlen;
	*p++ = IPPROTO_IPV4;
	bzero(p, t->icvlen);
	n->m_pkthdr.len = n->m_len = (int)flen;
	return (n);
}

/*
 * Send the inner packet m (consumed) through the engine: wrapped whole when the envelope fits the
 * port's MTU, else fragmented first - the OCTEON TX driver's octep_ipsec_send_inner. A packet that
 * must not be fragmented (DF), or one no fragment of which would fit, is answered with ICMP
 * "fragmentation needed" naming the size that does fit when it was forwarded - it has the interface
 * it arrived on; one this host generated has none and is dropped, its sender told by the error.
 * Each fragment leaves as its own ESP frame and takes its own number on the engine. The kernel's
 * ip_fragment leaves the header checksum to an interface that offloads it, and this one does not,
 * so it is redone here. `s` is the record the frames are counted on, checked by the handle under
 * the lock since the caller dropped it. Each frame goes through the port's transmit.
 */
static int
npuipsec_send_inner(if_t ifp, struct npuipsec_sa *s, const struct npuipsec_tx *t,
    const struct npuep_nhop *nh, struct mbuf *m)
{
	struct mbuf *n, *next, *e;
	struct ip *ip;
	uint32_t inner, inner_max;
	int err = 0, sent = 0;

	inner = (uint32_t)m->m_pkthdr.len;
	inner_max = npuipsec_inner_max(t, nh->mtu);
	if (inner <= inner_max) {
		e = npuipsec_wrap(t, nh, m, &err);
		m_freem(m);
		if (e == NULL)
			return (err);
		mtx_lock(&npuipsec_mtx);
		if (s != NULL && s->used && s->idx + 1 == t->handle)
			s->handed++;
		mtx_unlock(&npuipsec_mtx);
		return (if_transmit(ifp, e));
	}

	/* Too big. The header has to be in one piece for what follows. */
	m = m_pullup(m, MIN(m->m_pkthdr.len, 68));
	if (m == NULL) {
		npuipsec_tx_nobuf++;
		return (ENOBUFS);
	}
	ip = mtod(m, struct ip *);
	if ((ip->ip_off & htons(IP_DF)) != 0 || inner_max < sizeof(struct ip) + 8) {
		npuipsec_tx_needfrag++;
		if (m->m_pkthdr.rcvif != NULL && inner_max >= sizeof(struct ip) + 8)
			icmp_error(m, ICMP_UNREACH, ICMP_UNREACH_NEEDFRAG, 0, (int)inner_max);
		else
			m_freem(m);
		return (EMSGSIZE);
	}
	err = ip_fragment(ip, &m, (int)inner_max, 0);
	if (err != 0) {
		for (n = m; n != NULL; n = next) {
			next = n->m_nextpkt;
			n->m_nextpkt = NULL;
			m_freem(n);
		}
		npuipsec_tx_drop++;
		return (err);
	}
	npuipsec_tx_fragmented++;
	for (n = m; n != NULL; n = next) {
		next = n->m_nextpkt;
		n->m_nextpkt = NULL;
		if (n->m_len < (int)sizeof(struct ip) && (n = m_pullup(n, sizeof(struct ip))) == NULL)
			continue;
		ip = mtod(n, struct ip *);
		ip->ip_sum = 0;
		ip->ip_sum = in_cksum(n, ip->ip_hl << 2);
		e = npuipsec_wrap(t, nh, n, &err);
		m_freem(n);
		if (e == NULL)
			continue;
		mtx_lock(&npuipsec_mtx);
		if (s != NULL && s->used && s->idx + 1 == t->handle)
			s->handed++;
		mtx_unlock(&npuipsec_mtx);
		if (if_transmit(ifp, e) == 0)
			sent++;
	}
	return (sent != 0 ? 0 : (err != 0 ? err : EMSGSIZE));
}

/*
 * The kernel's output step for a mirrored association, in esp_output's place: the door a FORWARDED
 * packet comes through (ipsec4_forward has no output interface, so the kernel's own offload path does
 * not take it). The kernel has chosen the association, run enc0's rules and put the outer IPv4 header
 * in front, `skip` bytes of it, and hands over the mbuf and one reference each on the policy and the
 * association. The inner packet is wrapped for the engine and handed to the port.
 *
 *   not in the table    the association is not mirrored, or is being freed: the kernel's own
 *                       esp_output, which is then the only encryptor on it
 *   not ready           dropped; installing or leaving
 *   a bundle            dropped: the engine cannot run the next transform
 *   IPv6 inside         dropped, and counted; a later slice
 *   no next hop         dropped; resolving it has just asked for it
 *
 * The ESP frame leaves the port straight from the engine: it does not pass pf on the way out as the
 * software path's does; the inner packet was filtered on enc0 like any other.
 */
static int
npuipsec_xf_output(struct mbuf *m, struct secpolicy *sp, struct secasvar *sav, u_int idx, int skip,
    int protoff)
{
	struct npuep_nhop nh;
	struct npuipsec_sa *s = NULL;
	struct npuipsec_tx t;
	if_t ifp;
	uint32_t i;
	uint8_t outer_p;
	int port = -1, found = 0, ready = 0, err;

	bzero(&t, sizeof(t));
	mtx_lock(&npuipsec_mtx);
	for (i = 1; i < NPUIPSEC_SA_IDX_MAX; i++) {
		s = &npuipsec_sa_tab[i];
		if (s->used && s->dir == 0 && s->sav == sav) {
			found = s->taken;
			ready = s->ready;
			port = s->port;
			npuipsec_tx_of(s, &t);
			break;
		}
	}
	mtx_unlock(&npuipsec_mtx);
	if (!found) {
		npuipsec_tx_orig++;
		return (npuipsec_esp_orig->xf_output(m, sp, sav, idx, skip, protoff));
	}
	if (!ready || sp->tcount != 1 || skip < (int)sizeof(struct ip) ||
	    m->m_pkthdr.len < skip + (int)sizeof(struct ip)) {
		npuipsec_tx_drop++;
		err = ENETDOWN;
		goto out;
	}
	m_copydata(m, protoff, 1, (caddr_t)&outer_p);
	if (outer_p != IPPROTO_IPV4) {
		npuipsec_tx_drop++;
		err = EAFNOSUPPORT;
		goto out;
	}
	if (npuep_nhop_resolve(t.dst, port, &nh) != 0 || nh.port != port ||
	    (ifp = (if_t)npugiu_port_ifp(port)) == NULL) {
		npuipsec_tx_nonhop++;
		err = EHOSTUNREACH;
		goto out;
	}
	key_sa_recordxfer(sav, m);
	m_adj(m, skip);		/* the outer header the kernel built goes: the engine writes its own */
	err = npuipsec_send_inner(ifp, s, &t, &nh, m);
	m = NULL;
	if (err == 0)
		npuipsec_tx_taken++;
out:
	if (m != NULL)
		m_freem(m);
	key_freesav(&sav);
	key_freesp(&sp);
	return (err);
}

/*
 * The transmit side's other door: the kernel's own offload path. For a packet this appliance
 * generates and routes out of the association's port, ipsec_accel_output does not run the transform
 * at all - it tags the plaintext packet (PACKET_TAG_IPSEC_ACCEL_OUT, the association by its drv_spi)
 * and sends it to the port behind a link header for wherever the INNER destination routes. Measured
 * 2026-10-14: with the transform interposed and every association accepted, the appliance's own
 * pings took this door and left in the clear - xf_output never saw them. So the port's transmit asks
 * here first, without the GIU lock (resolving a next hop can send an ARP request back through this
 * transmit). Returns the frame to send - the one given - or NULL when it was consumed: wrapped and
 * sent from here, or dropped. A tagged frame whose association is not here must never leave in the
 * clear. The OCTEON TX's octep_ipsec_tx_prepare.
 */
struct mbuf *
npuipsec_tx_prepare(if_t ifp, struct mbuf *m)
{
	struct ipsec_accel_out_tag *tag;
	struct npuep_nhop nh;
	struct npuipsec_sa *s = NULL;
	struct npuipsec_tx t;
	uint32_t i;
	uint16_t dspi;
	int port, found = 0;

	tag = (struct ipsec_accel_out_tag *)m_tag_find(m, PACKET_TAG_IPSEC_ACCEL_OUT, NULL);
	if (tag == NULL)
		return (m);
	dspi = tag->drv_spi;
	if (dspi == IPSEC_ACCEL_DRV_SPI_BYPASS) {
		/* The policy said none or bypass: the kernel is telling us so, and the frame goes plain. */
		npuipsec_tx_bypass++;
		return (m);
	}
	bzero(&t, sizeof(t));
	port = npuipsec_port_index_of(ifp);
	mtx_lock(&npuipsec_mtx);
	for (i = 1; i < NPUIPSEC_SA_IDX_MAX; i++) {
		s = &npuipsec_sa_tab[i];
		if (s->used && s->ready && s->dir == 0 && s->drv_spi == dspi && s->port == port) {
			found = 1;
			npuipsec_tx_of(s, &t);
			break;
		}
	}
	mtx_unlock(&npuipsec_mtx);
	if (!found || port < 0) {
		npuipsec_tx_nosa++;
		m_freem(m);
		return (NULL);
	}
	if (m->m_pkthdr.len <= ETHER_HDR_LEN + (int)sizeof(struct ip)) {
		npuipsec_tx_drop++;
		m_freem(m);
		return (NULL);
	}
	if (npuep_nhop_resolve(t.dst, port, &nh) != 0 || nh.port != port) {
		npuipsec_tx_nonhop++;
		m_freem(m);
		return (NULL);
	}
	/* The link header for the inner destination goes; the envelope carries the tunnel's next hop. */
	m_adj(m, ETHER_HDR_LEN);
	/*
	 * Sent from here, each envelope through the port's transmit again - which asks this function
	 * once more and, finding no kernel tag on an envelope, passes it on. Nothing goes back to the
	 * caller: the frame it was given is consumed either way.
	 */
	if (npuipsec_send_inner(ifp, s, &t, &nh, m) == 0)
		npuipsec_tx_accel++;
	return (NULL);
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
	sbuf_printf(sb, "receive: done %ju nosa %ju nokey %ju bad %ju noenc %ju blocked %ju "
	    "queuefail %ju | last word 0x%08x\n",
	    (uintmax_t)npuipsec_rx_done, (uintmax_t)npuipsec_rx_nosa, (uintmax_t)npuipsec_rx_nokey,
	    (uintmax_t)npuipsec_rx_bad, (uintmax_t)npuipsec_rx_noenc, (uintmax_t)npuipsec_rx_blocked,
	    (uintmax_t)npuipsec_rx_queuefail, npuipsec_rx_last_word);
	sbuf_printf(sb, "transmit: accept_encrypt %s (accepted %ju fail %ju removed %ju) | forwarded %ju "
	    "generated %ju orig %ju bypass %ju | fragmented %ju needfrag %ju | nosa %ju drop %ju nonhop %ju "
	    "toobig %ju nobuf %ju\n",
	    npuipsec_accept_encrypt ? "on" : "off", (uintmax_t)npuipsec_accepted_enc,
	    (uintmax_t)npuipsec_accept_enc_fail, (uintmax_t)npuipsec_removed_enc,
	    (uintmax_t)npuipsec_tx_taken, (uintmax_t)npuipsec_tx_accel, (uintmax_t)npuipsec_tx_orig,
	    (uintmax_t)npuipsec_tx_bypass, (uintmax_t)npuipsec_tx_fragmented,
	    (uintmax_t)npuipsec_tx_needfrag, (uintmax_t)npuipsec_tx_nosa, (uintmax_t)npuipsec_tx_drop,
	    (uintmax_t)npuipsec_tx_nonhop, (uintmax_t)npuipsec_tx_toobig, (uintmax_t)npuipsec_tx_nobuf);
	for (i = 0; i < NPUIPSEC_SEEN_MAX; i++) {
		mtx_lock(&npuipsec_mtx);
		c = npuipsec_seen[i];
		mtx_unlock(&npuipsec_mtx);
		if (!c.used)
			continue;
		n++;
		sbuf_printf(sb, "%-6s spi 0x%08x %s %s alg %u", c.ifname, c.spi,
		    c.dir == 1 ? "decrypt" : "encrypt", c.family == AF_INET6 ? "inet6" : "inet", c.alg);
		if (c.dir == 1)
			sbuf_printf(sb, " win %u", c.win);
		sbuf_printf(sb, " %ds ago", (ticks - c.when) / hz);
		if (c.win_refused)
			sbuf_cat(sb, " | window refused: not a power of two or above 4096, left to the kernel");
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
	int on = 0;

	/*
	 * hw.npuep.ipsec_on: both accept switches below, as a loader tunable - the XGS 3300's
	 * hw.octep.ipsec_on on this family. A row in System > Settings > Tunables, which OPNsense
	 * writes into /boot/loader.conf; read here, when the driver attaches, because the kernel
	 * offers an association once, when strongSwan installs it during the boot, and a switch set
	 * after that finds every tunnel already in software. Until 2026-10-17 the switches were
	 * sysctls alone, so each boot turned the offload off again (issue 418).
	 */
	TUNABLE_INT_FETCH("hw.npuep.ipsec_on", &on);
	if (on != 0) {
		npuipsec_accept_decrypt = 1;
		npuipsec_accept_encrypt = 1;
	}

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
	    "marks the frame - off by default, on at attach with the tunable hw.npuep.ipsec_on=1");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "accepted", CTLFLAG_RD,
	    &npuipsec_accepted, 0, "decrypt associations installed on the engine and kept");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "accept_fail", CTLFLAG_RD,
	    &npuipsec_accept_fail, 0, "decrypt associations the engine refused, or no free index");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "win_refused", CTLFLAG_RD,
	    &npuipsec_win_refused, 0, "decrypt associations left to the kernel because their replay "
	    "window is not a power of two or is above the engine's 4096");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "removed", CTLFLAG_RD,
	    &npuipsec_removed, 0, "accepted associations the kernel later removed (SA_DEL)");
	/* The receive termination (npuipsec_rx): what became of each frame the engine decrypted. */
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_done", CTLFLAG_RD,
	    &npuipsec_rx_done, 0, "decrypted frames terminated and queued to IP as their inner packet");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_nosa", CTLFLAG_RD,
	    &npuipsec_rx_nosa, 0, "marked frames whose handle, revision, SPI or direction match no "
	    "accepted association of ours");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_nokey", CTLFLAG_RD,
	    &npuipsec_rx_nokey, 0, "the kernel no longer holds the association (a rekey in flight)");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_bad", CTLFLAG_RD,
	    &npuipsec_rx_bad, 0, "frames whose envelope did not parse: too short, not IPv4/ESP, a "
	    "trailer naming something other than IPv4 inside, an inner length that does not add up");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_noenc", CTLFLAG_RD,
	    &npuipsec_rx_noenc, 0, "dropped because enc0 does not exist (never delivered unfiltered)");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_blocked", CTLFLAG_RD,
	    &npuipsec_rx_blocked, 0, "inner packets the firewall's enc0 rules refused");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_queuefail", CTLFLAG_RD,
	    &npuipsec_rx_queuefail, 0, "inner packets netisr would not queue");
	SYSCTL_ADD_U32(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_last_word", CTLFLAG_RD,
	    &npuipsec_rx_last_word, 0, "the sa word of the last marked frame: handle (SA_ADD index + 1) "
	    "in the low 16 bits, the association's revision in the high 16");
	/* The transmit side (npuipsec_xf_output). */
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "accept_encrypt", CTLFLAG_RW,
	    &npuipsec_accept_encrypt, 0, "accept an offered AES-GCM ENCRYPT association for real: the "
	    "engine is given it and the kernel's ESP output step is taken from it, so every packet of it "
	    "is encrypted by the engine from a frame this driver lays out - off by default, on at attach "
	    "with the tunable hw.npuep.ipsec_on=1");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "accepted_encrypt", CTLFLAG_RD,
	    &npuipsec_accepted_enc, 0, "encrypt associations installed on the engine and taken");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "accept_encrypt_fail", CTLFLAG_RD,
	    &npuipsec_accept_enc_fail, 0, "encrypt associations declined: no next hop on this port, "
	    "the engine refused, the transform could not be taken, or the kernel let go");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "removed_encrypt", CTLFLAG_RD,
	    &npuipsec_removed_enc, 0, "accepted encrypt associations the kernel later removed");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_taken", CTLFLAG_RD,
	    &npuipsec_tx_taken, 0, "forwarded packets wrapped for the engine at the interposed output step");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_accel", CTLFLAG_RD,
	    &npuipsec_tx_accel, 0, "packets this host generated, tagged by the kernel's own offload path "
	    "and wrapped for the engine at the port's transmit");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_bypass", CTLFLAG_RD,
	    &npuipsec_tx_bypass, 0, "frames the kernel tagged as bypass: sent plain, as told");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_nosa", CTLFLAG_RD,
	    &npuipsec_tx_nosa, 0, "frames the kernel tagged for an association not accepted on this port: "
	    "dropped, never sent in the clear");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_orig", CTLFLAG_RD,
	    &npuipsec_tx_orig, 0, "packets passed to the kernel's own esp_output: the association is not mirrored");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_drop", CTLFLAG_RD,
	    &npuipsec_tx_drop, 0, "dropped: not ready, a bundle, a short packet, or IPv6 inside");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_nonhop", CTLFLAG_RD,
	    &npuipsec_tx_nonhop, 0, "dropped: no next hop for the far end on this port yet");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_toobig", CTLFLAG_RD,
	    &npuipsec_tx_toobig, 0, "dropped: a fragment's envelope would still not fit the port's MTU");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_fragmented", CTLFLAG_RD,
	    &npuipsec_tx_fragmented, 0, "inner packets fragmented before the envelope, each fragment its own ESP frame");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_natt", CTLFLAG_RD,
	    &npuipsec_tx_natt, 0, "envelopes written with the NAT-T UDP header (RFC 3948) for the engine");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_needfrag", CTLFLAG_RD,
	    &npuipsec_tx_needfrag, 0, "inner packets too big that may not be fragmented (DF): a forwarded one is "
	    "answered with ICMP fragmentation needed, one of this host's is dropped");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_nobuf", CTLFLAG_RD,
	    &npuipsec_tx_nobuf, 0, "dropped: no mbuf or tag for the envelope");
}
