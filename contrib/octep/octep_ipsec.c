/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * The kernel's IPsec offload contract on the front ports.
 *
 * FreeBSD 15 ships `struct if_ipsec_accel_methods` (sys/net/if_var.h) and the glue in
 * sys/netipsec/ipsec_offload.c, and the OPNsense 26.7 kernel this driver runs under is built with
 * `options IPSEC_OFFLOAD`: the glue is in ipsec.ko, and the kernel exports
 * if_setipsec_accel_methods(). So the association mirror this project spent a design on does not
 * need a userland reader of PF_KEY at all. strongSwan installs an association, the kernel offers it
 * to every interface that advertises IFCAP2_IPSEC_OFFLOAD, and this file answers:
 *
 *   if_sa_newkey     install it on the coprocessor (SA_ADD), if it is ours and the engine takes it
 *   if_sa_deinstall  take it out again (SA_DEL, two stages)
 *   if_sa_cnt        answer the kernel's lifetime bookkeeping from SA_GET_STATS
 *   if_hwassist      nothing: the stack finishes its checksums before the frame reaches us
 *   if_spdadd/del    nothing yet: the policy is consulted when a flow is made, not stored here
 *
 * and two things the contract leaves to the driver's own paths:
 *
 *   transmit    a frame carrying PACKET_TAG_IPSEC_ACCEL_OUT is the plaintext inner packet the kernel
 *               chose not to encrypt itself. Its metadata asks the coprocessor to (sa_is_out, the
 *               association's handle), and a tagged frame whose association this driver does not
 *               know is dropped - never sent as it is.
 *   receive     a punted frame whose kn_md.sa_index is not zero was decrypted by the coprocessor on
 *               the way in, IN PLACE: measured 2026-10-07, the host is handed the ESP frame with its
 *               outer header re-templated, SPI and sequence intact, payload and trailer in the clear
 *               and the ICV still attached. The kernel has no entry for that shape - esp_input runs
 *               the cipher over the plaintext and drops it - so this file terminates the ESP itself
 *               and delivers the inner packet the way ipsec4_common_input_cb would have: marked
 *               decrypted, tagged with where it came from, filtered on enc0, and queued to IP.
 *
 * The handle a microflow or a metadata block names is the SA_ADD index PLUS ONE
 * (docs/the-handle-is-the-index-plus-one.md). Everything here keeps that offset in one place:
 * octep_ipsec_handle().
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/endian.h>
#include <sys/epoch.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_private.h>
#include <net/if_types.h>
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

#include <netipsec/ipsec.h>
#include <netipsec/keydb.h>
#include <netipsec/key.h>
#include <netipsec/xform.h>
#include <netipsec/ipsec_offload.h>

#include "octep.h"

/*
 * The DLT_ENC header enc(4) puts in front of a tapped packet, and its two flags: the layout of
 * struct enchdr in sys/net/if_enc.h, written out here so the module does not depend on a header that
 * is not installed everywhere. A capture on enc0 then decodes a frame this driver delivered exactly as
 * it decodes one the kernel decrypted.
 */
struct octep_enchdr {
	uint32_t	af;
	uint32_t	spi;
	uint32_t	flags;
};
#define	OCTEP_ENC_M_CONF	0x0400	/* payload was encrypted */
#define	OCTEP_ENC_M_AUTH	0x0800	/* payload was authenticated */

/* AES-GCM-16: an 8-byte explicit IV after the ESP header, a 16-byte ICV at the end. */
#define	OCTEP_SA_IVLEN		8
#define	OCTEP_SA_ICVLEN		16
#define	OCTEP_ESP_HDRLEN	8

static int	octep_ipsec_sa_newkey(if_t ifp, void *savp, u_int drv_spi, void **privp);
static int	octep_ipsec_sa_deinstall(if_t ifp, u_int drv_spi, void *priv);
static int	octep_ipsec_sa_cnt(if_t ifp, void *savp, uint32_t drv_spi, void *priv,
		    struct seclifetime *lt);
static int	octep_ipsec_hwassist(if_t ifp, void *savp, u_int drv_spi, void *priv);
static int	octep_ipsec_spdadd(if_t ifp, void *sp, void *inp, void **priv);
static int	octep_ipsec_spddel(if_t ifp, void *sp, void *priv);

static const struct if_ipsec_accel_methods octep_ipsec_methods = {
	.if_spdadd = octep_ipsec_spdadd,
	.if_spddel = octep_ipsec_spddel,
	.if_sa_newkey = octep_ipsec_sa_newkey,
	.if_sa_deinstall = octep_ipsec_sa_deinstall,
	.if_sa_cnt = octep_ipsec_sa_cnt,
	.if_hwassist = octep_ipsec_hwassist,
};

/* The one place the offset lives. */
static inline uint32_t
octep_ipsec_handle(const struct octep_sa *s)
{
	return (s->idx + 1);
}

void
octep_ipsec_if_attach(struct octep_softc *sc, if_t ifp)
{
	/*
	 * The methods first, then the capability: ipsec_accel_sa_install_match() reads the capability
	 * and then dereferences the method table, and the kernel may offer an association the moment
	 * the bit is visible.
	 */
	if_setipsec_accel_methods(ifp, &octep_ipsec_methods);
	if_setcapabilities2bit(ifp, IFCAP2_BIT(IFCAP2_IPSEC_OFFLOAD), 0);
	if_setcapenable2bit(ifp, IFCAP2_BIT(IFCAP2_IPSEC_OFFLOAD), 0);
}

/*
 * enc0, looked up on first use and held by reference until detach. Lazy because enc(4) may attach
 * after this driver on a cold boot, and because an appliance without it must drop decrypted frames
 * rather than deliver them past the firewall - see octep_ipsec_rx.
 */
static if_t
octep_ipsec_enc(struct octep_softc *sc)
{
	if_t ifp;

	if (sc->ipsec_enc != NULL)
		return (sc->ipsec_enc);
	ifp = ifunit_ref("enc0");
	if (ifp == NULL)
		return (NULL);
	mtx_lock(&sc->mtx);
	if (sc->ipsec_enc == NULL) {
		sc->ipsec_enc = ifp;
		ifp = NULL;
	}
	mtx_unlock(&sc->mtx);
	if (ifp != NULL)
		if_rele(ifp);
	return (sc->ipsec_enc);
}

void
octep_ipsec_detach(struct octep_softc *sc)
{
	if_t ifp;

	mtx_lock(&sc->mtx);
	ifp = sc->ipsec_enc;
	sc->ipsec_enc = NULL;
	mtx_unlock(&sc->mtx);
	if (ifp != NULL)
		if_rele(ifp);
}

/*
 * Which of the two ends of the association this interface is.
 *
 * The kernel's SADB has no direction field; strongSwan installs the inbound association with our
 * address as its destination and the outbound one with our address as its source, and the kernel
 * offers each to every interface that advertises the capability. So the interface that OWNS the
 * destination takes the inbound association (dir 1, decrypt: the coprocessor keys its SPI hash by
 * that interface), the interface that owns the source takes the outbound one (dir 0, encrypt), and
 * every other interface declines. mlx5 installs both directions per association because its
 * hardware tables are per direction; the coprocessor's are not, and an encrypt entry for an inbound
 * SPI would only occupy a slot.
 */
static int
octep_ipsec_direction(if_t ifp, const struct secasindex *saidx)
{
	struct epoch_tracker et;
	struct ifaddr *ifa;
	int dir = -1;

	if (saidx->dst.sa.sa_family != AF_INET || saidx->src.sa.sa_family != AF_INET)
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

/*
 * A free coprocessor index. Index 0 is never used - the handle a frame names is index + 1, and the
 * fast path refuses a handle of 0 - and an index freed by SA_DEL rests OCTEP_SA_COOLOFF seconds
 * before it is given out again, as the vendor's host does, because the fast path's grace period for
 * a deleted entry answers the next SA_ADD on it with -EAGAIN until the workers have passed a
 * quiescent point. Called with sc->mtx held.
 */
static struct octep_sa *
octep_ipsec_alloc(struct octep_softc *sc)
{
	struct octep_sa *s;
	uint32_t i;

	mtx_assert(&sc->mtx, MA_OWNED);
	for (i = 1; i < OCTEP_SA_MAX; i++) {
		s = &sc->ipsec_sa[i];
		if (s->used)
			continue;
		if (s->cooling && time_uptime < s->cool_until)
			continue;
		s->cooling = 0;
		s->used = 1;
		s->idx = i;
		/* Per index, climbs on every install, never 0: the fast path compares it. */
		s->rev++;
		if (s->rev == 0)
			s->rev = 1;
		return (s);
	}
	return (NULL);
}

static int
octep_ipsec_sa_newkey(if_t ifp, void *savp, u_int drv_spi, void **privp)
{
	struct secasvar *sav = savp;
	struct octep_dp_if *dif = if_getsoftc(ifp);
	struct octep_softc *sc;
	struct octep_sa *s, rec;
	const struct secasindex *saidx;
	int dir, keylen, err;

	*privp = NULL;
	if (dif == NULL || (sc = dif->sc) == NULL)
		return (EOPNOTSUPP);
	if (sc->ipsec_on == 0) {
		sc->ipsec_sa_refused++;
		return (EOPNOTSUPP);
	}
	saidx = &sav->sah->saidx;

	/*
	 * What the coprocessor can take, as the vendor's own host checks it: tunnel-mode ESP over
	 * IPv4 with AES-GCM-16 and a key of 16, 24 or 32 bytes plus the 4-byte salt. Not transport
	 * mode, not AH, not IPv6 yet, not ESN yet. Everything else stays with the kernel, which is
	 * what EOPNOTSUPP means to ipsec_offload.c.
	 */
	if (saidx->proto != IPPROTO_ESP || saidx->mode != IPSEC_MODE_TUNNEL ||
	    sav->alg_enc != SADB_X_EALG_AESGCM16 || sav->key_enc == NULL ||
	    (sav->flags & SADB_X_SAFLAGS_ESN) != 0) {
		sc->ipsec_sa_refused++;
		return (EOPNOTSUPP);
	}
	keylen = _KEYLEN(sav->key_enc) - 4;	/* the salt rides at the end, RFC 4106 8.1 */
	if (keylen != 16 && keylen != 24 && keylen != 32) {
		sc->ipsec_sa_refused++;
		return (EOPNOTSUPP);
	}
	dir = octep_ipsec_direction(ifp, saidx);
	if (dir < 0) {
		sc->ipsec_sa_refused++;
		return (EOPNOTSUPP);
	}

	bzero(&rec, sizeof(rec));
	rec.dir = dir;
	rec.dif = (int)(dif - sc->dp_if);
	rec.drv_spi = (uint16_t)drv_spi;
	rec.spi = sav->spi;				/* network order, as on the wire */
	rec.src = saidx->src.sin.sin_addr.s_addr;	/* network order */
	rec.dst = saidx->dst.sin.sin_addr.s_addr;
	rec.keylen = keylen;
	memcpy(rec.key, sav->key_enc->key_data, keylen);
	memcpy(rec.salt, sav->key_enc->key_data + keylen, 4);
	/*
	 * The replay window the kernel keeps is in BYTES of bitmap (keydb.h: "window size, i.g. 4
	 * bytes" - that is 32 packets, strongSwan's default); the coprocessor wants packets, and a
	 * power of two. Only the decrypt side checks replay. The encrypt side's window is 0 and its
	 * anti-replay bit off, as the vendor sends it.
	 */
	rec.win = (dir == 1 && sav->replay != NULL) ? sav->replay->wsize * 8 : 0;
	if (rec.win != 0 && (rec.win & (rec.win - 1)) != 0) {
		sc->ipsec_sa_refused++;
		return (EOPNOTSUPP);
	}
	if (sav->natt != NULL) {
		rec.natt = 1;
		rec.nat_sport = sav->natt->sport;	/* network order, as the kernel keeps them */
		rec.nat_dport = sav->natt->dport;
	}
	rec.lif = dif->lif_iface << 12;

	mtx_lock(&sc->mtx);
	s = octep_ipsec_alloc(sc);
	if (s == NULL) {
		mtx_unlock(&sc->mtx);
		sc->ipsec_sa_full++;
		return (ENOSPC);
	}
	rec.idx = s->idx;
	rec.rev = s->rev;
	rec.used = 1;
	*s = rec;
	mtx_unlock(&sc->mtx);

	err = octep_rpc_sa_install(sc, s);
	/*
	 * The key has been posted and is never needed again on the host - a rekey brings a new one -
	 * so it leaves the record now, whichever way the install went, and does not wait in a table a
	 * core dump would carry.
	 */
	mtx_lock(&sc->mtx);
	explicit_bzero(s->key, sizeof(s->key));
	explicit_bzero(s->salt, sizeof(s->salt));
	if (err != 0) {
		/* Nothing was installed, so nothing rests: the index is free again at once. */
		s->used = 0;
	}
	mtx_unlock(&sc->mtx);
	if (err != 0) {
		sc->ipsec_sa_failed++;
		device_printf(sc->dev, "ipsec: %s association spi 0x%08x refused by the "
		    "coprocessor at index %u (rc 0x%04x, error %d)\n",
		    dir == 1 ? "inbound" : "outbound", ntohl(rec.spi), rec.idx,
		    sc->rpc_last_rc, err);
		return (EIO);
	}
	explicit_bzero(rec.key, sizeof(rec.key));
	explicit_bzero(rec.salt, sizeof(rec.salt));
	sc->ipsec_sa_installed++;
	*privp = s;
	device_printf(sc->dev, "ipsec: %s association spi 0x%08x on %s: coprocessor index %u, "
	    "handle %u, rev %u, drv_spi %u%s\n", dir == 1 ? "inbound" : "outbound",
	    ntohl(rec.spi), if_name(ifp), s->idx, octep_ipsec_handle(s), s->rev, drv_spi,
	    rec.natt ? ", NAT-T" : "");
	return (0);
}

static int
octep_ipsec_sa_deinstall(if_t ifp, u_int drv_spi, void *priv)
{
	struct octep_dp_if *dif = if_getsoftc(ifp);
	struct octep_softc *sc;
	struct octep_sa *s = priv;
	int e0, e1;

	if (s == NULL || dif == NULL || (sc = dif->sc) == NULL)
		return (0);
	/*
	 * Two stages, in order: invalidate, then free. The second on a still-valid entry answers 0
	 * and frees nothing, so the order is not a nicety - see docs/families/octeon-tx-rpc.md.
	 */
	e0 = octep_rpc_sa_remove(sc, s->idx, 0);
	e1 = octep_rpc_sa_remove(sc, s->idx, 1);
	mtx_lock(&sc->mtx);
	explicit_bzero(s->key, sizeof(s->key));
	explicit_bzero(s->salt, sizeof(s->salt));
	s->used = 0;
	s->cooling = 1;
	s->cool_until = time_uptime + OCTEP_SA_COOLOFF;
	mtx_unlock(&sc->mtx);
	sc->ipsec_sa_removed++;
	if (e0 != 0 || e1 != 0)
		device_printf(sc->dev, "ipsec: removing association at index %u: stage 0 %d, "
		    "stage 1 %d\n", s->idx, e0, e1);
	return (0);
}

/*
 * The kernel asks for the hardware's cumulative counts and adds the difference since it last asked
 * to the association's lifetime (ipsec_accel_sa_lifetime_op_impl, IF_SA_CNT_TOTAL_HW_VAL). So
 * swanctl's bytes and packets stay honest for traffic the host never saw.
 */
static int
octep_ipsec_sa_cnt(if_t ifp, void *savp, uint32_t drv_spi, void *priv, struct seclifetime *lt)
{
	struct octep_dp_if *dif = if_getsoftc(ifp);
	struct octep_softc *sc;
	struct octep_sa *s = priv;
	uint64_t bytes, packets;
	int err;

	if (s == NULL || dif == NULL || (sc = dif->sc) == NULL)
		return (ENXIO);
	err = octep_rpc_sa_stats(sc, s->idx, &bytes, &packets);
	if (err != 0)
		return (err);
	mtx_lock(&sc->mtx);
	s->bytes = bytes;
	s->packets = packets;
	mtx_unlock(&sc->mtx);
	lt->bytes = bytes;
	lt->allocations = (uint32_t)packets;
	return (0);
}

static int
octep_ipsec_hwassist(if_t ifp, void *savp, u_int drv_spi, void *priv)
{
	/* The coprocessor encrypts what it is given; the stack completes every checksum first. */
	return (0);
}

static int
octep_ipsec_spdadd(if_t ifp, void *sp, void *inp, void **priv)
{
	*priv = NULL;
	return (0);
}

static int
octep_ipsec_spddel(if_t ifp, void *sp, void *priv)
{
	return (0);
}

/*
 * The transmit side's question: does this frame carry the kernel's request to encrypt it, and with
 * which association? Returns the handle to put in the metadata, 0 for a frame to send as it is, and
 * sets *drop for a frame the kernel expected encrypted by an association this driver does not hold -
 * which must not leave in the clear. Called with sc->mtx held, from octep_dp_if_transmit.
 */
uint32_t
octep_ipsec_tx_handle(struct octep_softc *sc, const struct octep_dp_if *dif, struct mbuf *m,
    int *drop)
{
	struct ipsec_accel_out_tag *tag;
	struct octep_sa *s;
	uint32_t i;

	mtx_assert(&sc->mtx, MA_OWNED);
	*drop = 0;
	tag = (struct ipsec_accel_out_tag *)m_tag_find(m, PACKET_TAG_IPSEC_ACCEL_OUT, NULL);
	if (tag == NULL)
		return (0);
	if (tag->drv_spi == IPSEC_ACCEL_DRV_SPI_BYPASS) {
		/* The policy said none or bypass: the kernel is telling us so, and the frame goes plain. */
		sc->ipsec_tx_bypass++;
		return (0);
	}
	for (i = 1; i < OCTEP_SA_MAX; i++) {
		s = &sc->ipsec_sa[i];
		if (s->used && s->dir == 0 && s->drv_spi == tag->drv_spi &&
		    s->dif == (int)(dif - sc->dp_if)) {
			sc->ipsec_tx_encrypt++;
			return (octep_ipsec_handle(s));
		}
	}
	sc->ipsec_tx_nosa++;
	*drop = 1;
	return (0);
}

/*
 * A frame the coprocessor decrypted on the way in. Consumes the mbuf.
 *
 * What arrives, byte for byte (measured 2026-10-07 with tcpdump -XX on the front port, before and
 * after the association was mirrored):
 *
 *   Ethernet 14 | outer IPv4, id 0, checksum recomputed | ESP: SPI, sequence | 8 bytes where the IV
 *   was, now L2 residue | the inner packet, plaintext | pad, pad length, next header | ICV 16
 *
 * same length as the frame on the wire. So this strips the front down to the inner packet and the
 * tail up to the pad length the trailer states, and then does what the kernel's own input callback
 * does after esp_input_cb: records what was done (PACKET_TAG_IPSEC_IN_DONE, which also makes
 * ip_input skip the WAN's filter rules for it), marks it decrypted (which the inbound policy check
 * requires), counts it on the kernel's association, shows it to enc0's capture and firewall rules the
 * way enc_hhook does, and queues it to IP. The replay window is the coprocessor's, which checked it;
 * the kernel's own window for this association is not advanced.
 */
void
octep_ipsec_rx(struct octep_softc *sc, struct octep_dp_if *dif, struct mbuf *m, uint32_t sa_word)
{
	struct epoch_tracker et;
	struct secasvar *sav = NULL;
	struct octep_sa *s;
	struct m_tag *mtag;
	struct xform_history *xh;
	struct octep_enchdr eh;
	union sockaddr_union su;
	if_t enc, rcvif;
	const uint8_t *p;
	uint32_t handle, spi, ihl, front, back, len;
	uint8_t tail[2];

	handle = sa_word & 0xffffu;
	len = m->m_pkthdr.len;
	p = mtod(m, const uint8_t *);	/* the receive copy is one contiguous cluster */
	if (len < ETHER_HDR_LEN + 20 + OCTEP_ESP_HDRLEN + OCTEP_SA_IVLEN + 20 + 2 +
	    OCTEP_SA_ICVLEN || be16dec(p + 12) != ETHERTYPE_IP || (p[ETHER_HDR_LEN] >> 4) != 4)
		goto bad;
	ihl = (uint32_t)(p[ETHER_HDR_LEN] & 0x0f) * 4;
	if (ihl < 20 || p[ETHER_HDR_LEN + 9] != IPPROTO_ESP ||
	    len < ETHER_HDR_LEN + ihl + OCTEP_ESP_HDRLEN + OCTEP_SA_IVLEN + 20 + 2 +
	    OCTEP_SA_ICVLEN)
		goto bad;
	memcpy(&spi, p + ETHER_HDR_LEN + ihl, 4);	/* network order, compared as such */

	mtx_lock(&sc->mtx);
	s = (handle >= 1 && handle < OCTEP_SA_MAX + 1) ? &sc->ipsec_sa[handle - 1] : NULL;
	if (s == NULL || !s->used || s->dir != 1 || s->spi != spi) {
		mtx_unlock(&sc->mtx);
		sc->ipsec_rx_nosa++;
		goto drop;
	}
	bzero(&su, sizeof(su));
	su.sin.sin_len = sizeof(struct sockaddr_in);
	su.sin.sin_family = AF_INET;
	su.sin.sin_addr.s_addr = s->dst;
	mtx_unlock(&sc->mtx);

	NET_EPOCH_ENTER(et);
	sav = key_allocsa(&su, IPPROTO_ESP, spi);
	if (sav == NULL) {
		/* The kernel's association is gone - a rekey the coprocessor has not heard of yet. */
		sc->ipsec_rx_nokey++;
		goto drop_epoch;
	}

	front = ETHER_HDR_LEN + ihl + OCTEP_ESP_HDRLEN + OCTEP_SA_IVLEN;
	m_copydata(m, len - OCTEP_SA_ICVLEN - 2, 2, tail);	/* pad length, next header */
	back = OCTEP_SA_ICVLEN + 2 + tail[0];
	if (tail[1] == IPPROTO_IPV6) {
		sc->ipsec_rx_v6++;
		goto drop_epoch;
	}
	if (tail[1] != IPPROTO_IPV4 || len < front + back + 20) {
		sc->ipsec_rx_bad++;
		goto drop_epoch;
	}
	m_adj(m, (int)front);
	m_adj(m, -(int)back);
	p = mtod(m, const uint8_t *);
	if ((p[0] >> 4) != 4 || be16dec(p + 2) != m->m_pkthdr.len) {
		sc->ipsec_rx_bad++;
		goto drop_epoch;
	}

	mtag = m_tag_get(PACKET_TAG_IPSEC_IN_DONE, sizeof(*xh), M_NOWAIT);
	if (mtag == NULL) {
		sc->ipsec_rx_bad++;
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
	 * OPNsense's IPsec rules live. An appliance without enc0 gets the frame dropped, not
	 * delivered unfiltered.
	 */
	enc = octep_ipsec_enc(sc);
	if (enc == NULL) {
		sc->ipsec_rx_noenc++;
		goto drop_epoch;
	}
	if ((if_getflags(enc) & IFF_UP) != 0) {
		if (bpf_peers_present_if(enc)) {
			eh.af = AF_INET;
			eh.spi = sav->spi;
			eh.flags = OCTEP_ENC_M_CONF | OCTEP_ENC_M_AUTH;
			bpf_mtap2_if(enc, &eh, sizeof(eh), m);
		}
		if (PFIL_HOOKED_IN(V_inet_pfil_head)) {
			rcvif = m->m_pkthdr.rcvif;
			m->m_pkthdr.rcvif = enc;
			if (pfil_mbuf_in(V_inet_pfil_head, &m, enc, NULL) != PFIL_PASS) {
				/* Consumed by the filter, as enc_hhook reports it. */
				sc->ipsec_rx_blocked++;
				m = NULL;
				goto drop_epoch;
			}
			m->m_pkthdr.rcvif = rcvif;
		}
	}
	if (netisr_queue_src(NETISR_IP, (uintptr_t)sav->spi, m) != 0)
		sc->ipsec_rx_queuefail++;	/* netisr freed it */
	else
		sc->ipsec_rx_done++;
	m = NULL;
drop_epoch:
	if (sav != NULL)
		key_freesav(&sav);
	NET_EPOCH_EXIT(et);
drop:
	if (m != NULL)
		m_freem(m);
	return;
bad:
	sc->ipsec_rx_bad++;
	m_freem(m);
}

/*
 * Does the kernel's security policy cover this connection? The fast path forwards what it is given
 * and the policy is applied in ip_forward, after the point a punted frame is taken from - so a flow
 * the policy wants encrypted, programmed without its association, leaves in the clear in hardware
 * (measured, issue 290). Until the flow path knows how to attach the association, such a flow stays
 * with the host. Both of the connection's ingress tuples are asked: the opener's as an outbound
 * selector, the responder's as an inbound one, which is how ipsec4_forward would see each.
 */
static void
octep_ipsec_spidx(struct secpolicyindex *spidx, const struct octep_pf_tuple *t, u_int dir)
{
	int ports = (t->proto == IPPROTO_TCP || t->proto == IPPROTO_UDP);

	bzero(spidx, sizeof(*spidx));
	spidx->src.sin.sin_len = sizeof(struct sockaddr_in);
	spidx->src.sin.sin_family = AF_INET;
	spidx->src.sin.sin_addr.s_addr = t->sip;
	spidx->src.sin.sin_port = ports ? t->sport : IPSEC_PORT_ANY;
	spidx->dst.sin.sin_len = sizeof(struct sockaddr_in);
	spidx->dst.sin.sin_family = AF_INET;
	spidx->dst.sin.sin_addr.s_addr = t->dip;
	spidx->dst.sin.sin_port = ports ? t->dport : IPSEC_PORT_ANY;
	spidx->ul_proto = t->proto;
	spidx->dir = (uint8_t)dir;
	spidx->prefs = 32;
	spidx->prefd = 32;
}

int
octep_ipsec_policy_covers(struct octep_softc *sc, const struct octep_pf_tuple *orig,
    const struct octep_pf_tuple *reply)
{
	struct secpolicyindex spidx;
	struct secpolicy *sp;
	int covered = 0;

	if (orig->af != AF_INET)
		return (0);
	if (!key_havesp(IPSEC_DIR_OUTBOUND) && !key_havesp(IPSEC_DIR_INBOUND))
		return (0);
	octep_ipsec_spidx(&spidx, orig, IPSEC_DIR_OUTBOUND);
	sp = key_allocsp(&spidx, IPSEC_DIR_OUTBOUND);
	if (sp != NULL) {
		if (sp->policy == IPSEC_POLICY_IPSEC || sp->policy == IPSEC_POLICY_DISCARD)
			covered = 1;
		key_freesp(&sp);
	}
	if (!covered && reply != NULL && reply->af == AF_INET) {
		octep_ipsec_spidx(&spidx, reply, IPSEC_DIR_INBOUND);
		sp = key_allocsp(&spidx, IPSEC_DIR_INBOUND);
		if (sp != NULL) {
			if (sp->policy == IPSEC_POLICY_IPSEC || sp->policy == IPSEC_POLICY_DISCARD)
				covered = 1;
			key_freesp(&sp);
		}
	}
	if (covered)
		sc->ipsec_flow_policy++;
	return (covered);
}

static int
octep_sysctl_ipsec_table(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct octep_sa s;
	struct sbuf *sb;
	char name[IFNAMSIZ];
	int error, n;
	uint32_t i;

	sb = sbuf_new_for_sysctl(NULL, NULL, 1024, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_clear_flags(sb, SBUF_INCLUDENUL);
	n = 0;
	for (i = 1; i < OCTEP_SA_MAX; i++) {
		mtx_lock(&sc->mtx);
		s = sc->ipsec_sa[i];
		name[0] = '\0';
		if (s.used && s.dif >= 0 && s.dif < OCTEP_DP_IF_MAX &&
		    sc->dp_if[s.dif].ifp != NULL)
			strlcpy(name, if_name(sc->dp_if[s.dif].ifp), sizeof(name));
		mtx_unlock(&sc->mtx);
		if (!s.used) {
			if (s.cooling && time_uptime < s.cool_until)
				sbuf_printf(sb, "%3u  resting until %jd\n", i,
				    (intmax_t)s.cool_until);
			continue;
		}
		n++;
		sbuf_printf(sb, "%3u handle %u rev %u  %s  spi 0x%08x  0x%08x -> 0x%08x  lif 0x%x  "
		    "%s  drv_spi %u  win %u%s  %ju bytes %ju packets\n", s.idx, s.idx + 1, s.rev,
		    s.dir == 1 ? "decrypt" : "encrypt", ntohl(s.spi), ntohl(s.src), ntohl(s.dst),
		    s.lif, name[0] != '\0' ? name : "no interface", s.drv_spi, s.win,
		    s.natt ? "  NAT-T" : "", (uintmax_t)s.bytes, (uintmax_t)s.packets);
	}
	if (n == 0)
		sbuf_cat(sb, "no associations are mirrored\n");
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

void
octep_ipsec_add_sysctls(struct octep_softc *sc, struct sysctl_ctx_list *ctx,
    struct sysctl_oid_list *top)
{
	struct sysctl_oid *node;

	node = SYSCTL_ADD_NODE(ctx, top, OID_AUTO, "ipsec", CTLFLAG_RD, NULL,
	    "the kernel's IPsec offload contract, answered by the coprocessor's crypto engine");
	if (node == NULL)
		return;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "on",
	    CTLFLAG_RW, &sc->ipsec_on, 0,
	    "accept security associations the kernel offers to the front ports. Off, every offer is "
	    "declined and IPsec stays with the host. An association installed while this was off is "
	    "not offered again until it is re-established (configctl ipsec reload)");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "table",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, octep_sysctl_ipsec_table, "A",
	    "every association mirrored to the coprocessor: index, the handle a flow names (index "
	    "plus one), direction, SPI, outer addresses, interface, and the counts the engine "
	    "reported last");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_installed",
	    CTLFLAG_RD, &sc->ipsec_sa_installed, 0, "associations the coprocessor took");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_refused",
	    CTLFLAG_RD, &sc->ipsec_sa_refused, 0,
	    "offers declined: the gate off, another interface's association, or a shape the engine "
	    "does not take");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_failed",
	    CTLFLAG_RD, &sc->ipsec_sa_failed, 0, "SA_ADD the coprocessor answered with an error");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_full",
	    CTLFLAG_RD, &sc->ipsec_sa_full, 0, "offers declined for want of a free index");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_removed",
	    CTLFLAG_RD, &sc->ipsec_sa_removed, 0, "associations taken out again");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_done",
	    CTLFLAG_RD, &sc->ipsec_rx_done, 0,
	    "frames the coprocessor decrypted, terminated here and queued to IP");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_nosa",
	    CTLFLAG_RD, &sc->ipsec_rx_nosa, 0,
	    "decrypted frames naming a handle this driver does not hold, or an SPI that is not its");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_nokey",
	    CTLFLAG_RD, &sc->ipsec_rx_nokey, 0,
	    "decrypted frames whose association the kernel no longer has");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_bad",
	    CTLFLAG_RD, &sc->ipsec_rx_bad, 0, "decrypted frames that did not parse as ESP over IPv4");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_v6",
	    CTLFLAG_RD, &sc->ipsec_rx_v6, 0, "decrypted frames carrying IPv6 inside, not handled yet");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_noenc",
	    CTLFLAG_RD, &sc->ipsec_rx_noenc, 0, "decrypted frames dropped because enc0 does not exist");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_blocked",
	    CTLFLAG_RD, &sc->ipsec_rx_blocked, 0, "decrypted frames enc0's rules refused");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "rx_queuefail",
	    CTLFLAG_RD, &sc->ipsec_rx_queuefail, 0, "decrypted frames netisr would not queue");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_encrypt",
	    CTLFLAG_RD, &sc->ipsec_tx_encrypt, 0,
	    "frames the kernel asked to have encrypted, handed to the coprocessor with their handle");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_nosa",
	    CTLFLAG_RD, &sc->ipsec_tx_nosa, 0,
	    "frames the kernel asked to have encrypted by an association this driver does not hold: "
	    "dropped, never sent in the clear");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_bypass",
	    CTLFLAG_RD, &sc->ipsec_tx_bypass, 0,
	    "frames the kernel marked as needing no IPsec, sent as they were");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_policy",
	    CTLFLAG_RD, &sc->ipsec_flow_policy, 0,
	    "connections not accelerated because the kernel's IPsec policy covers them (issue 290)");
}
