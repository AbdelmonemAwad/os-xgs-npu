/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * The kernel's IPsec offload contract on the front ports.
 *
 * FreeBSD 15 ships `struct if_ipsec_accel_methods` (sys/net/if_var.h) and the glue in
 * sys/netipsec/ipsec_offload.c, and the OPNsense 26.7 kernel this driver runs under is built with
 * `options IPSEC_OFFLOAD`: the glue is in ipsec.ko, and the kernel exports
 * if_setipsec_accel_methods(). So strongSwan installs an association, the kernel offers it to every
 * interface that advertises IFCAP2_IPSEC_OFFLOAD, and this file answers:
 *
 *   if_sa_newkey     install it on the coprocessor (SA_ADD), if it is ours and the engine takes it
 *   if_sa_deinstall  take it out again (SA_DEL, two stages)
 *   if_sa_cnt        the engine's counts from SA_GET_STATS - which this kernel does not ask for
 *   if_hwassist      nothing: the stack finishes its checksums before the frame reaches us
 *   if_spdadd/del    nothing yet: the policy is consulted when a flow is made, not stored here
 *
 * ONE ENCRYPTOR PER ASSOCIATION, and that is the rule the outbound side is built on. An ESP
 * sequence number may be used once; the coprocessor keeps its own counter for an association it
 * encrypts on, the kernel keeps another, and a packet from each carries the same number and one of
 * them is dropped by the peer as a replay. The kernel's offload path, ipsec_accel_output, does not
 * cover this by itself: it is reached from ip_output with the output interface in hand, so a packet
 * the appliance FORWARDS - ipsec4_forward passes ifp NULL - and a packet that leaves by another
 * interface are still encrypted in software. The first design filtered forwarded packets with a
 * pfil hook and passed the awkward ones back to the kernel, and every one it passed was a packet
 * with the kernel's number on an association the coprocessor was numbering: measured 2026-10-07,
 * an upload through the tunnel stopped dead, 14 packets passed and 14 dropped as replays by the
 * peer.
 *
 * So a mirrored outbound association has its transform's output step taken over
 * (octep_ipsec_interpose): the kernel does everything it always did - policy, the association
 * choice, enc0's capture and rules, the outer header - and where it would call esp_output it calls
 * octep_ipsec_xf_output, which hands the inner packet to the coprocessor. Forwarded, generated
 * here, a fragment, too big: there is no other way to that association's cipher any more. The one
 * path that does not come through there is the kernel's own offload path, which tags a plaintext
 * frame (PACKET_TAG_IPSEC_ACCEL_OUT) and sends it to this interface's transmit - and the transmit
 * sends that to the coprocessor too (octep_ipsec_tx_prepare).
 *
 * Inbound, a punted frame whose kn_md.sa_index is not zero was decrypted by the coprocessor on the
 * way in, IN PLACE: the host is handed the ESP frame with its outer header re-templated, SPI and
 * sequence intact, payload and trailer in the clear and the ICV still attached. The kernel has no
 * entry for that shape - esp_input runs the cipher over the plaintext and drops it - so
 * octep_ipsec_rx terminates the ESP itself and delivers the inner packet the way
 * ipsec4_common_input_cb would have: marked decrypted, tagged with where it came from, filtered on
 * enc0, and queued to IP. Outbound is the mirror image: the coprocessor's host path encrypts in
 * place and wants the whole ESP frame laid out for it (octep_ipsec_envelope); a bare inner packet
 * is refused as CRYPTO_DROP_PROTO_ERR.
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
#include <sys/rmlock.h>
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
#include <netinet/ip_icmp.h>
#include <machine/in_cksum.h>

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
/* What the envelope puts in front of the inner packet, after the link header. */
#define	OCTEP_ESP_FRONT		((uint32_t)sizeof(struct ip) + OCTEP_ESP_HDRLEN + OCTEP_SA_IVLEN)

/*
 * One device, and the transform's output step is called with an association and nothing that leads
 * back to a softc, so the softc is found here. octep_esp_orig is the kernel's own ESP transform as
 * the first interposed association pointed at it; octep_esp_xformsw is a copy of it with one member
 * changed. Neither is ever put back, because not every association pointing here can be found again:
 * key_updateaddresses clones an association, pointer and all, without telling the driver. So once
 * octep_esp_orig is set this module must stay loaded, and octep_ipsec_detach_check makes the device
 * refuse to detach - which is what refuses kldunload.
 */
#define	OCTEP_SA_SEQ_SLACK	16	/* numbers left unused when the kernel's counter is moved up */
static struct octep_softc	*octep_ipsec_sc;
static const struct xformsw	*octep_esp_orig;
static struct xformsw		 octep_esp_xformsw;

static int	octep_ipsec_xf_output(struct mbuf *m, struct secpolicy *sp, struct secasvar *sav,
		    u_int idx, int skip, int protoff);

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
 * rather than deliver them past the firewall - see octep_ipsec_rx. ifunit_ref walks V_ifnet, so the
 * caller has to have a vnet set; octep_ipsec_rx does.
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

	/*
	 * Called from octep_dp_stop, which is device detach AND the dp.stop sysctl. So this lets go
	 * of enc0, which is looked up again on demand, and nothing else: the softc pointer the
	 * output step uses stays, or a dp.stop followed by dp.start would leave every mirrored
	 * association's packets falling through to the kernel's cipher while the coprocessor went
	 * on numbering the ones that arrive tagged. With the datapath stopped the transmit refuses
	 * them, which is a drop and not a second encryptor.
	 */
	mtx_lock(&sc->mtx);
	ifp = sc->ipsec_enc;
	sc->ipsec_enc = NULL;
	mtx_unlock(&sc->mtx);
	if (ifp != NULL)
		if_rele(ifp);
}

/*
 * May the device detach? Not once octep_esp_xformsw has been handed to any association: that
 * association's tdb_xform - and any clone the kernel made of it - points into this module until it
 * is freed, the kernel calls xf_cleanup through it when that happens, and the driver cannot
 * enumerate them to put the kernel's table back. So EBUSY, for good; the host reboots to unload.
 * Otherwise the gate is shut under the same lock the interposition takes it under, so nothing is
 * interposed after the answer was no, and the softc pointer goes.
 */
int
octep_ipsec_detach_check(struct octep_softc *sc)
{
	int busy;

	mtx_lock(&sc->mtx);
	busy = (octep_esp_orig != NULL);
	if (!busy) {
		sc->ipsec_on = 0;
		if (octep_ipsec_sc == sc)
			octep_ipsec_sc = NULL;
	}
	mtx_unlock(&sc->mtx);
	return (busy ? EBUSY : 0);
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

/*
 * Take the output step of one outbound association's transform.
 *
 * sav->tdb_xform is the table the kernel calls an association's cipher through: xf_input, xf_output,
 * xf_cleanup. This points it at a copy of the kernel's ESP table whose xf_output is
 * octep_ipsec_xf_output and whose other members are the kernel's own, so decryption and the
 * association's cleanup are untouched and the one thing that changes is who is asked to encrypt.
 * Called from if_sa_newkey, which the kernel runs on its single offload taskqueue with a reference
 * on the association, so two of these never race.
 */
static bool
octep_ipsec_interpose(struct secasvar *sav)
{
	const struct xformsw *cur = sav->tdb_xform;

	if (cur == NULL)
		return (false);
	if (cur == &octep_esp_xformsw)
		return (true);
	if (octep_esp_orig == NULL) {
		if (cur->xf_type != XF_ESP || cur->xf_output == NULL || cur->xf_cleanup == NULL)
			return (false);
		octep_esp_xformsw = *cur;
		memset(&octep_esp_xformsw.chain, 0, sizeof(octep_esp_xformsw.chain));
		octep_esp_xformsw.xf_cntr = 0;
		octep_esp_xformsw.xf_output = octep_ipsec_xf_output;
		octep_esp_orig = cur;
	} else if (cur != octep_esp_orig)
		return (false);
	sav->tdb_xform = &octep_esp_xformsw;
	return (true);
}

static int
octep_ipsec_sa_newkey(if_t ifp, void *savp, u_int drv_spi, void **privp)
{
	struct secasvar *sav = savp;
	struct octep_dp_if *dif = if_getsoftc(ifp);
	struct octep_softc *sc;
	struct octep_sa *s, rec;
	struct octep_nhop nh;
	const struct secasindex *saidx;
	uint64_t seq, kiv;
	int dir, keylen, err, ok;

	*privp = NULL;
	if (dif == NULL || (sc = dif->sc) == NULL)
		return (EOPNOTSUPP);
	/* The gate; and a coprocessor whose RPC facility is not up cannot be given anything. */
	if (sc->ipsec_on == 0 || sc->rpc_ready == 0) {
		sc->ipsec_sa_refused++;
		return (EOPNOTSUPP);
	}
	saidx = &sav->sah->saidx;

	/*
	 * What the coprocessor can take, as the vendor's own host checks it: tunnel-mode ESP over
	 * IPv4 with AES-GCM-16 and a key of 16, 24 or 32 bytes plus the 4-byte salt. Not transport
	 * mode, not AH, not IPv6 yet, not ESN yet - and not UDP-encapsulated: the receive
	 * termination and the envelope below both speak plain ESP, and an association behind NAT
	 * that was accepted here would lose every packet both ways. Everything else stays with the
	 * kernel, which is what EOPNOTSUPP means to ipsec_offload.c.
	 */
	if (saidx->proto != IPPROTO_ESP || saidx->mode != IPSEC_MODE_TUNNEL ||
	    sav->alg_enc != SADB_X_EALG_AESGCM16 || sav->key_enc == NULL ||
	    (sav->flags & SADB_X_SAFLAGS_ESN) != 0 || sav->natt != NULL) {
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
	/*
	 * Owning the source address is not enough for the outbound one: the ESP frame leaves by the
	 * route to the far end, and if that is another interface - a second uplink, an asymmetric
	 * route - every packet of a mirrored association would be dropped for want of a next hop
	 * here, for as long as the association lived. Then it is not this interface's to take. A
	 * neighbour that is merely not resolved yet is fine; the lookup has just asked for it.
	 */
	if (dir == 0) {
		err = octep_nhop_resolve(sc, saidx->dst.sin.sin_addr.s_addr, -1, &nh);
		if ((err != 0 && err != EWOULDBLOCK) ||
		    (err == 0 && nh.ifname_unit != (int)(dif - sc->dp_if))) {
			sc->ipsec_sa_refused++;
			return (EOPNOTSUPP);
		}
	}

	bzero(&rec, sizeof(rec));
	rec.dir = dir;
	rec.dif = (int)(dif - sc->dp_if);
	rec.drv_spi = (uint16_t)drv_spi;
	rec.spi = sav->spi;				/* network order, as on the wire */
	rec.src = saidx->src.sin.sin_addr.s_addr;	/* network order */
	rec.dst = saidx->dst.sin.sin_addr.s_addr;
	rec.keylen = keylen;
	rec.sav = sav;
	rec.lif = dif->lif_iface << 12;
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
	memcpy(rec.key, sav->key_enc->key_data, keylen);
	memcpy(rec.salt, sav->key_enc->key_data + keylen, 4);

	mtx_lock(&sc->mtx);
	s = octep_ipsec_alloc(sc);
	if (s == NULL) {
		mtx_unlock(&sc->mtx);
		explicit_bzero(&rec, sizeof(rec));
		sc->ipsec_sa_full++;
		return (ENOSPC);
	}
	rec.idx = s->idx;
	rec.rev = s->rev;
	rec.used = 1;
	*s = rec;		/* not ready: a packet that finds it now is dropped, not encrypted */
	mtx_unlock(&sc->mtx);
	explicit_bzero(&rec, sizeof(rec));

	/*
	 * The order is the point. The record is in the table and not ready; THEN the kernel's cipher
	 * is taken away, so from that line no packet of this association is encrypted by anyone;
	 * THEN every packet that was already inside the kernel's esp_output when the pointer changed
	 * is waited out - they run inside the network epoch, and one of them would otherwise take
	 * the number the coprocessor is about to start from; THEN the kernel's counter is read, which
	 * can no longer move; and the coprocessor starts from it. An association is offered the
	 * moment it is installed, so the counter is normally zero - but one re-offered after traffic,
	 * or installed while a flood is running, has already shown the peer some numbers, and the
	 * coprocessor must not show them again. For a decrypt association the same field is the
	 * window's head, and the kernel's highest number seen is what the vendor's host sends there.
	 *
	 * The gate is read again under the lock the detach check shuts it under, so an association
	 * is never interposed after the device has agreed to go.
	 */
	ok = 1;
	if (dir == 0) {
		mtx_lock(&sc->mtx);
		ok = (sc->ipsec_on != 0 && octep_ipsec_interpose(sav));
		mtx_unlock(&sc->mtx);
	}
	if (!ok) {
		mtx_lock(&sc->mtx);
		explicit_bzero(s->key, sizeof(s->key));
		explicit_bzero(s->salt, sizeof(s->salt));
		s->sav = NULL;
		s->used = 0;
		mtx_unlock(&sc->mtx);
		sc->ipsec_sa_refused++;
		return (EOPNOTSUPP);
	}
	if (dir == 0)
		NET_EPOCH_WAIT();
	seq = 0;
	if (sav->replay != NULL) {
		SECREPLAY_LOCK(sav->replay);
		seq = (dir == 0) ? sav->replay->count : sav->replay->last;
		SECREPLAY_UNLOCK(sav->replay);
	}
	mtx_lock(&sc->mtx);
	s->seq = seq;
	mtx_unlock(&sc->mtx);

	/*
	 * And the IV, which is the other number that must never repeat under one key. Read off the
	 * wire: the coprocessor sends the ESP sequence number, zero-extended, as the eight-byte GCM
	 * IV. The kernel sends its own counter, sav->cntr (xform_esp.c: "a simple per-SA counter"),
	 * which starts at zero, counts only what the kernel itself encrypted, and so stands at about
	 * the number just read. Going in that is harmless - the kernel used the IVs below it and the
	 * coprocessor starts above. Coming back it is not: if the kernel's cipher ever runs on this
	 * key again, it resumes from an IV the coprocessor has long since used, with the same salt,
	 * and a repeated GCM nonce gives away more than a dropped packet. That happens whenever an
	 * association outlives its mirror: the interface goes, or key_updateaddresses clones the
	 * association for a changed address and the clone - not in this table - falls through to the
	 * kernel's cipher. Moving the counter when the association is taken out would be too late
	 * for the clone, which copies cntr by value when it is made.
	 *
	 * So the kernel's IV counter is moved now, once, into the half of its 64-bit space that a
	 * sequence number cannot reach, while the kernel's cipher is stopped and before any clone can
	 * exist. If the install below fails the kernel resumes from there, which is as good an IV as
	 * any.
	 */
	kiv = 0;
	if (dir == 0) {
		SECASVAR_WLOCK(sav);
		sav->cntr |= (uint64_t)1 << 63;
		kiv = sav->cntr;
		SECASVAR_WUNLOCK(sav);
	}

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
		s->sav = NULL;
		s->used = 0;
	} else
		s->ready = 1;
	mtx_unlock(&sc->mtx);
	if (err != 0) {
		/*
		 * And the kernel has its cipher back: the coprocessor never encrypted on this
		 * association, so the kernel's counter is still the only one.
		 */
		if (dir == 0)
			sav->tdb_xform = octep_esp_orig;
		sc->ipsec_sa_failed++;
		device_printf(sc->dev, "ipsec: %s association spi 0x%08x refused by the "
		    "coprocessor at index %u (rc 0x%04x, error %d)\n",
		    dir == 1 ? "inbound" : "outbound", ntohl(sav->spi), s->idx,
		    sc->rpc_last_rc, err);
		return (EIO);
	}
	sc->ipsec_sa_installed++;
	*privp = s;
	if (dir == 0)
		device_printf(sc->dev, "ipsec: outbound association spi 0x%08x on %s: coprocessor "
		    "index %u, handle %u, rev %u, drv_spi %u, after sequence %ju, kernel IV counter "
		    "moved to 0x%016jx\n", ntohl(sav->spi), if_name(ifp), s->idx,
		    octep_ipsec_handle(s), s->rev, drv_spi, (uintmax_t)seq, (uintmax_t)kiv);
	else
		device_printf(sc->dev, "ipsec: inbound association spi 0x%08x on %s: coprocessor "
		    "index %u, handle %u, rev %u, drv_spi %u, after sequence %ju\n",
		    ntohl(sav->spi), if_name(ifp), s->idx, octep_ipsec_handle(s), s->rev, drv_spi,
		    (uintmax_t)seq);
	return (0);
}

static int
octep_ipsec_sa_deinstall(if_t ifp, u_int drv_spi, void *priv)
{
	struct octep_dp_if *dif = if_getsoftc(ifp);
	struct octep_softc *sc;
	struct octep_sa *s = priv;
	struct secasvar *sav;
	uint64_t past;
	int e0, e1;

	if (s == NULL || dif == NULL || (sc = dif->sc) == NULL)
		return (0);
	/*
	 * Not ready first: from here a packet for it is dropped rather than handed to a coprocessor
	 * that is about to forget the association.
	 */
	mtx_lock(&sc->mtx);
	s->ready = 0;
	mtx_unlock(&sc->mtx);
	/*
	 * Two stages, in order: invalidate, then free. The second on a still-valid entry answers 0
	 * and frees nothing, so the order is not a nicety - see docs/families/octeon-tx-rpc.md.
	 */
	e0 = octep_rpc_sa_remove(sc, s->idx, 0);
	/*
	 * And between them, the kernel gets its counter back in a state it can use. Once the record
	 * is gone octep_ipsec_xf_output passes this association's packets to the kernel's cipher -
	 * and an association can outlive its mirror: the interface goes away, or the kernel clones
	 * it for a changed address and frees this one while the clone, which shares the counter,
	 * carries on. The kernel's counter has stood still since the coprocessor took over, so the
	 * kernel would resume with numbers the peer has already seen and every packet would be
	 * dropped as a replay. The coprocessor has stopped by now, and it used one number per frame
	 * it was handed, so the kernel resumes after the last of them. The association is still
	 * referenced here: the kernel holds it across this call.
	 */
	mtx_lock(&sc->mtx);
	sav = (s->dir == 0) ? s->sav : NULL;
	past = s->seq + s->handed + OCTEP_SA_SEQ_SLACK;
	mtx_unlock(&sc->mtx);
	if (sav != NULL && sav->replay != NULL) {
		SECREPLAY_LOCK(sav->replay);
		if (sav->replay->count < past)
			sav->replay->count = past;
		SECREPLAY_UNLOCK(sav->replay);
	}
	e1 = octep_rpc_sa_remove(sc, s->idx, 1);
	mtx_lock(&sc->mtx);
	explicit_bzero(s->key, sizeof(s->key));
	explicit_bzero(s->salt, sizeof(s->salt));
	s->sav = NULL;
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
 * The engine's cumulative counts for one association. The contract has this method and
 * ipsec_accel_sa_lifetime_op_impl would call it for IF_SA_CNT_TOTAL_HW_VAL - but nothing in this
 * kernel asks for that value: key.c's one caller asks for the software total, and on the appliance
 * this was never entered across half a million packets. The association's bytes and packets are
 * right today because every packet still crosses the host, which counts it with
 * key_sa_recordxfer. When a flow carries the association in hardware the host will not see those
 * packets, and the driver will have to push the counts with ipsec_accel_drv_sa_lifetime_update.
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
 * The size of the ESP frame the coprocessor is handed for an inner packet of `inner` bytes, link
 * header included, and the padding that goes with it. RFC 4303 wants the payload, the pad, the pad
 * length and the next-header byte to end on a four-byte boundary, and the kernel's own esp_output
 * pads an AES-GCM association the same way.
 */
uint32_t
octep_ipsec_wire_len(uint32_t inner, uint32_t *padlen)
{
	*padlen = (4 - ((inner + 2) & 3)) & 3;
	return (ETHER_HDR_LEN + OCTEP_ESP_FRONT + inner + *padlen + 2 + OCTEP_SA_ICVLEN);
}

/* The largest inner packet whose envelope still fits an IP MTU of `mtu`; 0 when nothing fits. */
static uint32_t
octep_ipsec_inner_max(uint32_t mtu)
{
	uint32_t room;

	if (mtu < OCTEP_ESP_FRONT + OCTEP_SA_ICVLEN + 2 + (uint32_t)sizeof(struct ip) + 8)
		return (0);
	room = (mtu - OCTEP_ESP_FRONT - OCTEP_SA_ICVLEN) & ~3u;
	return (room - 2);
}

/*
 * Lay the ESP frame out at f, for the coprocessor to encrypt in place: the tunnel's next hop, an
 * outer IPv4 header, the ESP header with the SPI and a zero sequence, eight bytes where the IV
 * goes, the inner packet, the RFC 4303 trailer - pad bytes 1, 2, 3, the pad length, the next
 * header - and sixteen bytes where the ICV goes. The coprocessor writes the sequence, the IV and
 * the ICV, and replaces the outer header with the association's own template (TTL 63, id 0, DF
 * clear), so what is written into those here only has to be well formed. The vendor's host says
 * as much of the sequence on this path: "will be overridden".
 */
void
octep_ipsec_envelope(uint8_t *f, const struct octep_esp_tx *esp, struct mbuf *m, uint32_t inner,
    uint32_t padlen)
{
	struct ip oip;
	uint8_t *p;
	uint32_t i;

	memcpy(f, esp->dmac, ETHER_ADDR_LEN);
	memcpy(f + ETHER_ADDR_LEN, esp->smac, ETHER_ADDR_LEN);
	be16enc(f + 2 * ETHER_ADDR_LEN, ETHERTYPE_IP);

	bzero(&oip, sizeof(oip));
	oip.ip_v = IPVERSION;
	oip.ip_hl = sizeof(oip) >> 2;
	oip.ip_len = htons((uint16_t)(OCTEP_ESP_FRONT + inner + padlen + 2 + OCTEP_SA_ICVLEN));
	oip.ip_ttl = 64;
	oip.ip_p = IPPROTO_ESP;
	oip.ip_src.s_addr = esp->src;
	oip.ip_dst.s_addr = esp->dst;
	oip.ip_sum = in_cksum_hdr(&oip);
	p = f + ETHER_HDR_LEN;
	memcpy(p, &oip, sizeof(oip));
	p += sizeof(oip);
	memcpy(p, &esp->spi, 4);
	bzero(p + 4, 4 + OCTEP_SA_IVLEN);
	p += OCTEP_ESP_HDRLEN + OCTEP_SA_IVLEN;
	m_copydata(m, 0, (int)inner, (caddr_t)p);
	p += inner;
	for (i = 0; i < padlen; i++)
		*p++ = (uint8_t)(i + 1);
	*p++ = (uint8_t)padlen;
	*p++ = esp->nexthdr;
	bzero(p, OCTEP_SA_ICVLEN);
}

/*
 * The transmit side's question: does this frame carry the kernel's request to encrypt it, and with
 * which association? This is the kernel's own offload path - ipsec_accel_output in ip_output, for a
 * packet this appliance generates and routes out of the interface the association is on - and the
 * frame is the plaintext packet behind a link header for wherever the INNER destination routes.
 * The tunnel's far end is what the ESP frame has to reach, so the next hop is resolved for that
 * instead and the caller discards the frame's own link header before sending.
 *
 * Returns 0 for an ordinary frame, 1 with *esp filled for one to encrypt, and -1 for a frame the
 * kernel expected encrypted and this driver cannot: no such association here, or no next hop yet.
 * That one must not leave in the clear. Called without sc->mtx, because resolving a next hop can
 * send an ARP request back through this interface's transmit.
 */
int
octep_ipsec_tx_prepare(struct octep_softc *sc, struct octep_dp_if *dif, struct mbuf *m,
    struct octep_esp_tx *esp)
{
	struct ipsec_accel_out_tag *tag;
	struct octep_nhop nh;
	struct octep_sa *s;
	uint32_t i;
	uint16_t etype;
	int difidx, found;

	tag = (struct ipsec_accel_out_tag *)m_tag_find(m, PACKET_TAG_IPSEC_ACCEL_OUT, NULL);
	if (tag == NULL)
		return (0);
	if (tag->drv_spi == IPSEC_ACCEL_DRV_SPI_BYPASS) {
		/* The policy said none or bypass: the kernel is telling us so, and the frame goes plain. */
		sc->ipsec_tx_bypass++;
		return (0);
	}
	difidx = (int)(dif - sc->dp_if);
	found = 0;
	bzero(esp, sizeof(*esp));
	mtx_lock(&sc->mtx);
	for (i = 1; i < OCTEP_SA_MAX; i++) {
		s = &sc->ipsec_sa[i];
		if (s->used && s->ready && s->dir == 0 && s->drv_spi == tag->drv_spi &&
		    s->dif == difidx) {
			esp->handle = octep_ipsec_handle(s);
			esp->spi = s->spi;
			esp->src = s->src;
			esp->dst = s->dst;
			found = 1;
			break;
		}
	}
	mtx_unlock(&sc->mtx);
	if (!found) {
		sc->ipsec_tx_nosa++;
		return (-1);
	}
	if (m->m_pkthdr.len < ETHER_HDR_LEN + (int)sizeof(struct ip)) {
		sc->ipsec_out_drop++;
		return (-1);
	}
	m_copydata(m, 2 * ETHER_ADDR_LEN, sizeof(etype), (caddr_t)&etype);
	if (etype != htons(ETHERTYPE_IP)) {
		sc->ipsec_out_drop++;
		return (-1);
	}
	if (octep_nhop_resolve(sc, esp->dst, -1, &nh) != 0 || nh.ifname_unit != difidx) {
		sc->ipsec_out_nonhop++;
		return (-1);
	}
	memcpy(esp->dmac, nh.dmac, ETHER_ADDR_LEN);
	memcpy(esp->smac, nh.smac, ETHER_ADDR_LEN);
	esp->mtu = nh.mtu;
	esp->nexthdr = IPPROTO_IPV4;
	return (1);
}

/*
 * Hand one inner IPv4 packet to the coprocessor for esp's association, out of dif. The mbuf is the
 * inner packet and nothing else. Both ways in come here - the output step and the kernel's tagged
 * frames - so there is one answer to a packet too big for the tunnel:
 *
 *   DF set      ICMP "fragmentation needed" with the size that fits, as a tunnel should, when the
 *               packet came in on an interface; EMSGSIZE to the caller either way. The sender
 *               shrinks its segments and nothing is ever fragmented
 *   DF clear    fragmented BEFORE the envelope, each fragment its own ESP packet, because the
 *               coprocessor emits one frame per packet and cannot fragment what it has encrypted
 *
 * Consumes the mbuf. Returns 0, or why the packet was not sent.
 */
int
octep_ipsec_send_inner(struct octep_softc *sc, struct octep_dp_if *dif, struct mbuf *m,
    const struct octep_esp_tx *esp)
{
	struct mbuf *n, *next;
	struct ip *ip;
	uint32_t inner, inner_max;
	int err;

	inner = (uint32_t)m->m_pkthdr.len;
	inner_max = octep_ipsec_inner_max(esp->mtu);
	if (inner <= inner_max) {
		sc->ipsec_out_taken++;
		return (octep_dp_tx(dif, m, esp));
	}

	/* Too big. The header has to be in one piece for what follows. */
	m = m_pullup(m, MIN(m->m_pkthdr.len, 68));
	if (m == NULL) {
		sc->ipsec_out_drop++;
		return (ENOBUFS);
	}
	ip = mtod(m, struct ip *);
	if ((ip->ip_off & htons(IP_DF)) != 0 || inner_max < sizeof(struct ip) + 8) {
		sc->ipsec_out_needfrag++;
		/*
		 * A forwarded packet has the interface it arrived on, and the answer goes back out
		 * of it. One this host generated has none, and its sender is told by the error.
		 */
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
		sc->ipsec_out_drop++;
		return (err);
	}
	sc->ipsec_out_fragmented++;
	for (n = m; n != NULL; n = next) {
		next = n->m_nextpkt;
		n->m_nextpkt = NULL;
		/* ip_fragment leaves the checksum to an interface that offloads it; this one does not. */
		if (n->m_len < (int)sizeof(struct ip) &&
		    (n = m_pullup(n, sizeof(struct ip))) == NULL)
			continue;
		ip = mtod(n, struct ip *);
		ip->ip_sum = 0;
		ip->ip_sum = in_cksum(n, ip->ip_hl << 2);
		sc->ipsec_out_taken++;
		(void)octep_dp_tx(dif, n, esp);
	}
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

	/*
	 * This runs in the ring's interrupt thread or the receive watchdog's callout, and neither has
	 * a vnet set - and this kernel is built with VIMAGE, so every V_ variable key_allocsa, the
	 * filter and netisr read below resolves through curthread's vnet. Without this line the first
	 * decrypted frame of the first test took the appliance down with a page fault in key_allocsa
	 * (2026-10-07 15:51, textdump kept). The flow trigger learned the same lesson earlier, in
	 * octep_dp.c, with CURVNET_SET(vnet0) around its taskqueue work.
	 */
	CURVNET_SET(dif->ifp->if_vnet);
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
	CURVNET_RESTORE();
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

/*
 * Where the kernel's own cipher stood, for a mirrored outbound association.
 *
 * ipsec4_perform_request has done everything up to the cipher: found the policy and the
 * association, run enc0's capture and rules on the inner packet, fixed its header and - tunnel mode -
 * put the outer IPv4 header in front, `skip` bytes of it. It hands over the mbuf and one reference
 * each on the policy and the association, and esp_output would now insert the ESP header, encrypt,
 * and send the result with ip_output. This sends the inner packet to the coprocessor instead, which
 * does all of that with its own sequence number, and gives the two references back.
 *
 * Nothing is passed on to the kernel's cipher once the association is mirrored, whatever the
 * packet is - that is the rule in this file's first comment. So:
 *
 *   not in the table            the association is not mirrored (or is being freed): the kernel's
 *                               own esp_output, which is then the only encryptor on it
 *   installing, or leaving      dropped; milliseconds
 *   a policy with a bundle      dropped; the coprocessor cannot run the next transform
 *   IPv6 inside                 dropped, and counted; not built yet
 *   no next hop yet             dropped; resolving it has just sent the ARP request
 *   too big                     answered or fragmented before the envelope - see
 *                               octep_ipsec_send_inner, which the kernel's tagged frames share
 *
 * The ESP frame leaves the port straight from the coprocessor. It does not pass pf on the way out
 * as the software path's does, and it does not make the state there that the software path's ESP
 * made; the inner packet was filtered on enc0 like any other.
 */
static int
octep_ipsec_xf_output(struct mbuf *m, struct secpolicy *sp, struct secasvar *sav, u_int idx,
    int skip, int protoff)
{
	struct octep_softc *sc = octep_ipsec_sc;
	struct octep_esp_tx esp;
	struct octep_nhop nh;
	struct octep_dp_if *dif;
	struct octep_sa *s;
	uint32_t i;
	uint8_t outer_p;
	int difidx, err, found, ready;

	found = ready = 0;
	difidx = -1;
	bzero(&esp, sizeof(esp));
	if (sc != NULL) {
		mtx_lock(&sc->mtx);
		for (i = 1; i < OCTEP_SA_MAX; i++) {
			s = &sc->ipsec_sa[i];
			if (s->used && s->dir == 0 && s->sav == sav) {
				found = 1;
				ready = s->ready;
				difidx = s->dif;
				esp.handle = octep_ipsec_handle(s);
				esp.spi = s->spi;
				esp.src = s->src;
				esp.dst = s->dst;
				break;
			}
		}
		mtx_unlock(&sc->mtx);
	}
	if (!found) {
		if (sc != NULL)
			sc->ipsec_out_orig++;
		return (octep_esp_orig->xf_output(m, sp, sav, idx, skip, protoff));
	}

	if (!ready || sp->tcount != 1 || skip < (int)sizeof(struct ip) ||
	    m->m_pkthdr.len < skip + (int)sizeof(struct ip)) {
		sc->ipsec_out_drop++;
		err = ENETDOWN;
		goto out;
	}
	m_copydata(m, protoff, 1, (caddr_t)&outer_p);
	if (outer_p != IPPROTO_IPV4) {
		sc->ipsec_out_drop++;
		err = EAFNOSUPPORT;
		goto out;
	}
	if (difidx < 0 || difidx >= (int)sc->dp_nif ||
	    octep_nhop_resolve(sc, esp.dst, -1, &nh) != 0 || nh.ifname_unit != difidx ||
	    (dif = &sc->dp_if[difidx])->ifp == NULL) {
		sc->ipsec_out_nonhop++;
		err = EHOSTUNREACH;
		goto out;
	}
	memcpy(esp.dmac, nh.dmac, ETHER_ADDR_LEN);
	memcpy(esp.smac, nh.smac, ETHER_ADDR_LEN);
	esp.mtu = nh.mtu;
	esp.nexthdr = IPPROTO_IPV4;
	key_sa_recordxfer(sav, m);
	/* The outer header the kernel built goes: the coprocessor writes its own from the association. */
	m_adj(m, skip);
	err = octep_ipsec_send_inner(sc, dif, m, &esp);
	m = NULL;
out:
	if (m != NULL)
		m_freem(m);
	key_freesav(&sav);
	key_freesp(&sp);
	return (err);
}

/*
 * The softc the output step finds its way back to, and the gate's starting value.
 *
 * hw.octep.ipsec_on is a loader tunable - a line in /boot/loader.conf.local, or a row in OPNsense's
 * System > Settings > Tunables - because the gate has to be open BEFORE strongSwan installs its
 * associations: the kernel offers an association once, when it is installed, and one that was
 * declined then is not offered again until it is re-established. A sysctl set after boot is too
 * late for every tunnel that came up with it, which is how the first reboot after this was written
 * came back with the tunnel in software.
 */
void
octep_ipsec_attach(struct octep_softc *sc)
{
	int on = 0;

	TUNABLE_INT_FETCH("hw.octep.ipsec_on", &on);
	sc->ipsec_on = (on != 0) ? 1 : 0;
	octep_ipsec_sc = sc;
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
		    "%s  drv_spi %u  win %u  after seq %ju%s\n", s.idx, s.idx + 1, s.rev,
		    s.dir == 1 ? "decrypt" : "encrypt", ntohl(s.spi), ntohl(s.src), ntohl(s.dst),
		    s.lif, name[0] != '\0' ? name : "no interface", s.drv_spi, s.win,
		    (uintmax_t)s.seq, s.ready ? "" : "  (not ready)");
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
	    "not offered again until it is re-established, so the value that matters is the one at "
	    "boot: the loader tunable hw.octep.ipsec_on. Turning it off does not remove an "
	    "association that is already mirrored");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "table",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0, octep_sysctl_ipsec_table, "A",
	    "every association mirrored to the coprocessor: index, the handle a flow names (index "
	    "plus one), direction, SPI, outer addresses, interface, window, and the sequence number "
	    "it was installed after");
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
	    "ESP frames laid out and handed to the coprocessor to encrypt, from either path");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_nosa",
	    CTLFLAG_RD, &sc->ipsec_tx_nosa, 0,
	    "frames the kernel tagged for an association this driver does not hold on this "
	    "interface: dropped, never sent in the clear");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_bypass",
	    CTLFLAG_RD, &sc->ipsec_tx_bypass, 0,
	    "frames the kernel marked as needing no IPsec, sent as they were");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "tx_toobig",
	    CTLFLAG_RD, &sc->ipsec_tx_toobig, 0,
	    "envelopes the port's MTU or the transmit buffer could not take; the backstop behind "
	    "out_needfrag and out_fragmented, and it should stay at zero");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "out_taken",
	    CTLFLAG_RD, &sc->ipsec_out_taken, 0,
	    "packets taken where the kernel's cipher stood and handed to the coprocessor");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "out_orig",
	    CTLFLAG_RD, &sc->ipsec_out_orig, 0,
	    "packets passed on to the kernel's own cipher there, because their association is not "
	    "mirrored. On a mirrored association this never happens");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "out_needfrag",
	    CTLFLAG_RD, &sc->ipsec_out_needfrag, 0,
	    "packets too big for the tunnel with DF set: answered with ICMP fragmentation-needed "
	    "and dropped, so the sender shrinks its segments");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "out_fragmented",
	    CTLFLAG_RD, &sc->ipsec_out_fragmented, 0,
	    "packets too big for the tunnel without DF: fragmented before the envelope");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "out_nonhop",
	    CTLFLAG_RD, &sc->ipsec_out_nonhop, 0,
	    "packets dropped because the tunnel's far end had no next hop on the association's "
	    "interface yet; the lookup has asked for it");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "out_drop",
	    CTLFLAG_RD, &sc->ipsec_out_drop, 0,
	    "packets dropped rather than encrypted by anyone: the association installing or "
	    "leaving, a policy with a bundle, IPv6 inside, no memory");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_policy",
	    CTLFLAG_RD, &sc->ipsec_flow_policy, 0,
	    "connections not accelerated because the kernel's IPsec policy covers them (issue 290)");
}
