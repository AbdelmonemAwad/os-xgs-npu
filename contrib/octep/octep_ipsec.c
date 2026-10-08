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
/*
 * How far ahead of the kernel's sequence counter the coprocessor is started when an outbound
 * association is installed while the kernel's cipher is still running on it; and the largest
 * counter such an association may have. The kernel goes on numbering packets until its cipher is
 * taken - a few thousand at most, in the milliseconds one command takes - and none of its numbers
 * may reach the coprocessor's first. A million out of four thousand million, once an association.
 * Any gap that is safe is wider than a peer's replay window, so a smaller one buys nothing. The
 * coprocessor's register is 32 bits and ESN is refused, so an association already half way through
 * its numbers is left to the kernel.
 */
#define	OCTEP_SA_SEQ_AHEAD	(1u << 20)
#define	OCTEP_SA_SEQ_SEED_MAX	(1u << 31)
/*
 * And when a flow has encrypted on the association: the engine's packet count is a statistic it
 * refreshes on its own schedule, not the sequence register, so the last value read can be behind
 * the last number used by however many frames went by in between. A million numbers out of four
 * thousand million, once, at the end of an association's life.
 */
#define	OCTEP_SA_SEQ_FLOW_SLACK	(1u << 20)
#define	OCTEP_SA_POLL_PER_PASS	2	/* associations whose counts are read per one-second poll */
/*
 * More than an association can add to its counts in one second: twice the line rate of the fastest
 * front port, in bytes and in the smallest frames. A reading that claims more is not this
 * association's traffic, whatever else it is.
 */
#define	OCTEP_SA_RATE_BYTES	2500000000ULL
#define	OCTEP_SA_RATE_PKTS	30000000ULL
/*
 * key.c's private mark on an association it has cloned for a changed address (key_updateaddresses).
 * From then on the replay state, the lifetime counters and the lock the old association points at
 * belong to the CLONE and are freed with it - which can be before this driver has been told to let
 * the old one go, because that telling is a task. So anything of the kernel's association that is
 * reached through a pointer is left alone once the association is marked so, or is DEAD. The test
 * is made at the last moment and is not a lock: a clone made and deleted in the instructions
 * between the test and the use would still be reached. The value is key.c's, 0x80000000.
 */
#define	OCTEP_SAV_F_CLONED	0x80000000u

static inline int
octep_ipsec_sav_let_go(const struct secasvar *sav)
{
	return (sav->state == SADB_SASTATE_DEAD || (sav->flags & OCTEP_SAV_F_CLONED) != 0);
}
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
	/* Nor while an association is being installed: its cipher is about to be taken. */
	busy = (octep_esp_orig != NULL || sc->ipsec_installing != 0);
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

/*
 * Would octep_ipsec_interpose succeed? Asked before anything is installed, so that the moment the
 * cipher is taken - which comes after the coprocessor has the association - cannot be the moment
 * it turns out it cannot be. Nothing but octep_ipsec_interpose changes what this reads, and that
 * runs on the kernel's one offload thread, as this does.
 */
static bool
octep_ipsec_can_interpose(const struct secasvar *sav)
{
	const struct xformsw *cur = sav->tdb_xform;

	if (cur == NULL)
		return (false);
	if (cur == &octep_esp_xformsw)
		return (true);
	if (octep_esp_orig == NULL)
		return (cur->xf_type == XF_ESP && cur->xf_output != NULL &&
		    cur->xf_cleanup != NULL);
	return (cur == octep_esp_orig);
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
	uint64_t seq, kiv, b0, p0, klow, last;
	sbintime_t t0, t1, t2;
	int dir, keylen, err, ok, inst, gone, took;

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
	rec.gen = ++sc->ipsec_sa_gen;
	rec.polled = 1;
	rec.reqid = sav->sah->saidx.reqid;
	*s = rec;		/* not ready: a packet that finds it now is dropped, not encrypted */
	mtx_unlock(&sc->mtx);
	explicit_bzero(&rec, sizeof(rec));

	/*
	 * What the engine's counters for this index read NOW, before the SA_ADD: the previous
	 * occupants' totals, which the engine shows again - and counts on from - once it has used
	 * the new association. After the SA_ADD is too late: they read 0 until then. One command,
	 * before the kernel's cipher is touched. If it is not answered the SA_ADD would not be
	 * either, and it would be written into the one command buffer the unanswered descriptor
	 * still points at - so the install stops here, with nothing of the kernel's disturbed, and
	 * the kernel keeps the association for itself as it does when SA_ADD fails. Answered with a
	 * refusal is another matter: then the index says nothing and its remembered reading stands.
	 */
	b0 = p0 = 0;
	if (octep_rpc_sa_stats(sc, s->idx, &b0, &p0) == ETIMEDOUT) {
		mtx_lock(&sc->mtx);
		explicit_bzero(s->key, sizeof(s->key));
		explicit_bzero(s->salt, sizeof(s->salt));
		s->sav = NULL;
		s->used = 0;
		mtx_unlock(&sc->mtx);
		sc->ipsec_sa_failed++;
		device_printf(sc->dev, "ipsec: %s association spi 0x%08x not installed: the "
		    "coprocessor is not answering\n", dir == 1 ? "inbound" : "outbound",
		    ntohl(sav->spi));
		return (EIO);
	}

	/*
	 * THE ORDER, which is not the one this was first written with.
	 *
	 * It used to be: the record in the table and not ready; the kernel's cipher taken away;
	 * every packet inside the kernel's cipher waited out; the kernel's counter read, which
	 * could then no longer move; SA_ADD; ready. Safe, and for the whole of it - the wait and
	 * the command - a packet of the association had no cipher and was dropped: up to eleven
	 * hundred of them at a rekey under load, because OPNsense has the kernel send on a new
	 * association the moment it exists and this is called a little later.
	 *
	 * Now the coprocessor is given the association FIRST, while the kernel's cipher is still
	 * running on it, and the cipher is taken SECOND, when there is something to take it:
	 *
	 *   - The kernel's IV counter is moved into the upper half of its space, under the
	 *     association's write lock - which is the lock esp_output takes its numbers under, so
	 *     no packet is half way through taking one. From there every IV the kernel uses is one
	 *     the coprocessor, whose IV is a 32-bit sequence number, cannot. An association the
	 *     kernel has cloned or let go is refused in the same hold: a clone made before this
	 *     line carries its own counter, in the lower half, under the same key.
	 *   - The coprocessor is started a million numbers past the kernel's sequence counter,
	 *     and past its old IV counter. Every lower-half IV the kernel ever used is below its
	 *     sequence counter - it takes one of each per packet, the sequence number first - so
	 *     the coprocessor's first IV is past all of them whatever the margin; the margin is
	 *     for the sequence numbers the kernel goes on using until its cipher is taken.
	 *   - SA_ADD. The kernel is still encrypting; nothing is dropped; if this fails nothing
	 *     was taken and there is nothing to give back.
	 *   - In ONE hold of the softc lock: ready, taken, and the transform swapped. From that
	 *     line new packets go to the coprocessor. The ones already inside the kernel's cipher
	 *     finish there, with the kernel's numbers.
	 *   - Those are waited out, and the kernel's counter - which now cannot move - is read
	 *     once more. If it reached the seed, numbers were used twice: counted and said.
	 *   - Settled. Only now may the poll or a flow raise the kernel's counter to where the
	 *     coprocessor is: before this the kernel was still counting from it.
	 *
	 * What it costs is the handful of packets that were inside the kernel's cipher at the swap:
	 * they leave a million numbers behind the coprocessor's first, and a peer whose window has
	 * moved on drops them. A handful, where there were hundreds.
	 *
	 * An association the kernel clones, or lets go, while this is under way is given up at the
	 * next place that is seen - before its lock is touched, inside the lock, after it, and once
	 * more inside it immediately before the cipher is taken. A clone carries the key on under
	 * the kernel's cipher and shares the counters this reads; nothing here may be built on
	 * them once they are the clone's.
	 *
	 * The gate is read under the lock the detach check shuts it under, and an install in hand
	 * is counted there, so the device does not agree to go between this line and the swap.
	 */
	ok = 1;
	inst = 0;
	t0 = sbinuptime();
	if (dir == 0) {
		mtx_lock(&sc->mtx);
		ok = (sc->ipsec_on != 0 && octep_ipsec_can_interpose(sav));
		if (ok) {
			sc->ipsec_installing++;
			inst = 1;
		}
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
	seq = 0;
	kiv = 0;
	if (dir == 0) {
		/*
		 * Let go is asked before the lock is taken as well as inside it: a clone that has
		 * since been deleted took the lock with it. Not a lock itself - the file's own
		 * standard for this flag, see octep_ipsec_sav_let_go.
		 */
		gone = octep_ipsec_sav_let_go(sav);
		klow = 0;
		if (!gone) {
			SECASVAR_WLOCK(sav);
			if (octep_ipsec_sav_let_go(sav))
				gone = 1;
			else {
				if ((sav->cntr >> 63) == 0)
					klow = sav->cntr;
				sav->cntr |= (uint64_t)1 << 63;
				kiv = sav->cntr;
			}
			SECASVAR_WUNLOCK(sav);
		}
		if (!gone && octep_ipsec_sav_let_go(sav))
			gone = 1;
		if (!gone && sav->replay != NULL) {
			SECREPLAY_LOCK(sav->replay);
			seq = sav->replay->count;
			SECREPLAY_UNLOCK(sav->replay);
		}
		if (klow > seq)
			seq = klow;
		if (gone || seq > OCTEP_SA_SEQ_SEED_MAX) {
			mtx_lock(&sc->mtx);
			explicit_bzero(s->key, sizeof(s->key));
			explicit_bzero(s->salt, sizeof(s->salt));
			s->sav = NULL;
			s->used = 0;
			sc->ipsec_installing--;
			mtx_unlock(&sc->mtx);
			if (gone)
				sc->ipsec_sa_let_go++;
			sc->ipsec_sa_refused++;
			return (EOPNOTSUPP);
		}
		seq += OCTEP_SA_SEQ_AHEAD;
	} else if (sav->replay != NULL) {
		/* The window's head: the kernel's highest number seen, as the vendor's host sends. */
		SECREPLAY_LOCK(sav->replay);
		seq = sav->replay->last;
		SECREPLAY_UNLOCK(sav->replay);
	}
	mtx_lock(&sc->mtx);
	s->seq = seq;
	mtx_unlock(&sc->mtx);

	err = octep_rpc_sa_install(sc, s);
	/*
	 * Once more, inside the association's lock, immediately before its cipher is taken: has
	 * the kernel cloned it or let it go while the command was out? Then the coprocessor has an
	 * association whose key is going on under the kernel's cipher, on a clone, and it is not
	 * given a packet: it is taken out again below, as when the transform cannot be taken.
	 */
	gone = 0;
	if (err == 0 && dir == 0) {
		gone = octep_ipsec_sav_let_go(sav);
		if (!gone) {
			SECASVAR_WLOCK(sav);
			gone = octep_ipsec_sav_let_go(sav);
			SECASVAR_WUNLOCK(sav);
		}
	}
	/*
	 * The key has been posted and is never needed again on the host - a rekey brings a new one -
	 * so it leaves the record now, whichever way the install went, and does not wait in a table a
	 * core dump would carry.
	 */
	took = 1;
	mtx_lock(&sc->mtx);
	explicit_bzero(s->key, sizeof(s->key));
	explicit_bzero(s->salt, sizeof(s->salt));
	if (err != 0) {
		/* Nothing was installed, so nothing rests: the index is free again at once. */
		s->sav = NULL;
		s->used = 0;
		if (inst)
			sc->ipsec_installing--;
	} else {
		/*
		 * Its flows are counted from what the index read before the install, or - when
		 * that said nothing, which is an index whose last occupant the engine never used -
		 * from the last thing the index was ever seen to read.
		 */
		if (b0 == 0 && p0 == 0) {
			b0 = sc->ipsec_idx_bytes[s->idx];
			p0 = sc->ipsec_idx_packets[s->idx];
		}
		s->base_bytes = s->bytes = b0;
		s->base_packets = s->packets = p0;
		s->stat_time = time_uptime;
		s->base_valid = 1;
		s->ready = 1;
		if (dir == 0 && gone) {
			took = 0;
		} else if (dir == 0) {
			/* Ready, taken and swapped in this one hold: no reader sees them apart. */
			s->taken = 1;
			took = octep_ipsec_interpose(sav);
		} else
			s->settled = 1;
	}
	mtx_unlock(&sc->mtx);
	if (err != 0) {
		/*
		 * The kernel's cipher was never taken, so there is nothing to give back: the
		 * kernel goes on as the association's only encryptor, from an IV counter in the
		 * upper half, which is as good an IV as any.
		 */
		sc->ipsec_sa_failed++;
		device_printf(sc->dev, "ipsec: %s association spi 0x%08x refused by the "
		    "coprocessor at index %u (rc 0x%04x, error %d)\n",
		    dir == 1 ? "inbound" : "outbound", ntohl(sav->spi), s->idx,
		    sc->rpc_last_rc, err);
		return (EIO);
	}
	if (dir == 0 && !took) {
		/*
		 * Asked beforehand and nothing changes the answer but this function; so this is
		 * not expected, and it is not left half done. The coprocessor has an association
		 * nobody will hand it a packet for: taken out again, both stages, and the index
		 * rested as a removed one is. The kernel never stopped being the encryptor.
		 */
		mtx_lock(&sc->mtx);
		s->ready = 0;
		s->taken = 0;
		mtx_unlock(&sc->mtx);
		(void)octep_rpc_sa_remove(sc, s->idx, 0);
		(void)octep_rpc_sa_remove(sc, s->idx, 1);
		mtx_lock(&sc->mtx);
		s->sav = NULL;
		s->used = 0;
		s->cooling = 1;
		s->cool_until = time_uptime + OCTEP_SA_COOLOFF;
		sc->ipsec_installing--;
		mtx_unlock(&sc->mtx);
		sc->ipsec_sa_refused++;
		if (gone)
			sc->ipsec_sa_let_go++;
		device_printf(sc->dev, "ipsec: outbound association spi 0x%08x was installed and "
		    "%s; removed again\n", ntohl(sav->spi), gone ? "the kernel cloned it or let it "
		    "go meanwhile" : "its transform could not be taken");
		return (EOPNOTSUPP);
	}
	if (dir == 0) {
		t1 = sbinuptime();
		NET_EPOCH_WAIT();
		/*
		 * Not read for an association let go since the swap: the counter is then a
		 * clone's, and may be gone with it. That one is not checked, and settles all the
		 * same - its record is removed when the kernel says so.
		 */
		last = 0;
		if (sav->replay != NULL && !octep_ipsec_sav_let_go(sav)) {
			SECREPLAY_LOCK(sav->replay);
			last = sav->replay->count;
			SECREPLAY_UNLOCK(sav->replay);
		}
		mtx_lock(&sc->mtx);
		s->settled = 1;
		sc->ipsec_installing--;
		t2 = sbinuptime();
		sc->ipsec_install_us = (uint64_t)sbttous(t2 - t0);
		sc->ipsec_settle_us = (uint64_t)sbttous(t2 - t1);
		mtx_unlock(&sc->mtx);
		/*
		 * The coprocessor's first number is the seed plus one, so a counter that stands
		 * exactly on the seed has repeated nothing; it is reported with the rest, because
		 * the margin is a million and a counter that close has used all of it.
		 */
		if (last >= seq) {
			sc->ipsec_seq_overlap++;
			device_printf(sc->dev, "ipsec: outbound association spi 0x%08x: the kernel's "
			    "sequence counter stood at %ju when its cipher had been taken, and the "
			    "coprocessor was started after %ju - any numbers above that were used "
			    "twice and the peer drops one of each\n", ntohl(sav->spi),
			    (uintmax_t)last, (uintmax_t)seq);
		}
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

/*
 * One reading of the engine's counters for an association's index, turned into what the
 * association's flows have added since the reading before. With sc->mtx held. Returns 1 when
 * pushed_* moved.
 *
 * Nothing about these counters is documented and one thing about them was learned the hard way -
 * see struct octep_sa - so a reading is believed only as far as it can be true. 0 and 0 is an index
 * the engine has not used since its SA_ADD: nothing to add. Otherwise the difference from the last
 * reading is this association's, unless it runs backwards or is more than the port could have
 * carried since the counts were last known to be settled; then the reading is some other history
 * of the index showing through, it becomes the new starting point, and the kernel is told nothing
 * - an association that under-reports a second of traffic is a nuisance, one that reports
 * gigabytes it never carried expires on the spot when a byte lifetime is set.
 */
static int
octep_ipsec_stats_account(struct octep_softc *sc, struct octep_sa *s, uint64_t b, uint64_t p)
{
	uint64_t db, dp, secs;

	mtx_assert(&sc->mtx, MA_OWNED);
	s->bytes = b;
	s->packets = p;
	secs = (uint64_t)(time_uptime - s->stat_time) + 2;
	s->stat_time = time_uptime;
	if (b == 0 && p == 0)
		return (0);
	if (b < s->base_bytes || p < s->base_packets ||
	    b - s->base_bytes > secs * OCTEP_SA_RATE_BYTES ||
	    p - s->base_packets > secs * OCTEP_SA_RATE_PKTS) {
		device_printf(sc->dev, "ipsec: the engine's counts at index %u read %ju bytes %ju "
		    "packets against %ju and %ju accounted for, %ju s on: not this association's, "
		    "taken as a new starting point\n", s->idx, (uintmax_t)b, (uintmax_t)p,
		    (uintmax_t)s->base_bytes, (uintmax_t)s->base_packets, (uintmax_t)secs - 2);
		s->base_bytes = b;
		s->base_packets = p;
		sc->ipsec_stat_rebase++;
		return (0);
	}
	db = b - s->base_bytes;
	dp = p - s->base_packets;
	s->base_bytes = b;
	s->base_packets = p;
	if (db == 0 && dp == 0)
		return (0);
	s->pushed_bytes += db;
	s->pushed_packets += dp;
	return (1);
}

static int
octep_ipsec_sa_deinstall(if_t ifp, u_int drv_spi, void *priv)
{
	struct octep_dp_if *dif = if_getsoftc(ifp);
	struct octep_softc *sc;
	struct octep_sa *s = priv;
	const struct octep_sa *o;
	struct secasvar *sav;
	uint64_t past, b, p;
	uint32_t i, repl, repl_rev, rgen;
	int e0, e1, flowed;

	if (s == NULL || dif == NULL || (sc = dif->sc) == NULL)
		return (0);
	/*
	 * Not ready first: from here a packet for it is dropped rather than handed to a coprocessor
	 * that is about to forget the association, and no new connection is attached to it.
	 *
	 * And its successor, if it has one: another association hanging from the same head in the
	 * kernel - which is what a rekey leaves behind, since the new pair is installed before the
	 * old one is deleted - on the same interface, in the same direction, and ready. The head
	 * and not just the tunnel's two ends: two children between the same gateways share the
	 * ends, and one child's connections must not be moved to the other's association. The
	 * head is named by value - the ends and the reqid - for the reason struct octep_sa gives.
	 * The newest, if there are several.
	 */
	mtx_lock(&sc->mtx);
	s->ready = 0;
	repl = repl_rev = rgen = 0;
	for (i = 1; i < OCTEP_SA_MAX; i++) {
		o = &sc->ipsec_sa[i];
		if (o == s || !o->used || !o->ready || !o->settled || o->dir != s->dir ||
		    o->dif != s->dif ||
		    o->src != s->src || o->dst != s->dst || o->reqid != s->reqid)
			continue;
		if (repl == 0 || (int32_t)(o->gen - rgen) > 0) {
			repl = o->idx + 1;
			repl_rev = o->rev;
			rgen = o->gen;
		}
	}
	flowed = s->flowed;
	mtx_unlock(&sc->mtx);
	/*
	 * Then the connections that name it, BEFORE it is deleted - moved to the successor, or
	 * taken out of the fast path when there is none. octep_dp_flows_sa_gone says why the order
	 * is not a nicety for the direction that arrives decrypted.
	 */
	if (octep_dp_flows_on_sa(sc, s->idx + 1, s->rev) != 0)
		flowed = 1;
	octep_dp_flows_sa_gone(sc, s->idx + 1, s->rev, repl, repl_rev);
	/*
	 * And how many numbers its flows used, read while the entry can still be read. The host
	 * counted the frames it handed over itself; the engine counted the ones its flow table
	 * encrypted, and those are the ones the host never saw. For either direction: the reading
	 * is also remembered for the index, and the next association to be given it is measured
	 * from there.
	 */
	if (flowed && octep_rpc_sa_stats(sc, s->idx, &b, &p) == 0) {
		mtx_lock(&sc->mtx);
		(void)octep_ipsec_stats_account(sc, s, b, p);
		mtx_unlock(&sc->mtx);
	}
	/*
	 * Two stages, in order: invalidate, then free. The second on a still-valid entry answers 0
	 * and frees nothing, so the order is not a nicety - see docs/families/octeon-tx-rpc.md.
	 */
	e0 = octep_rpc_sa_remove(sc, s->idx, 0);
	/*
	 * And between them, the kernel gets its counter back in a state it can use. Once the record
	 * is gone octep_ipsec_xf_output passes this association's packets to the kernel's cipher -
	 * and an association can outlive its mirror when its interface goes away. (The kernel
	 * cloning it for a changed address is the other way to outlive it, and is not served from
	 * here: see below.) The kernel's counter is only as far on as the poll last put it, so the
	 * kernel would resume with numbers the peer has already seen and every packet would be
	 * dropped as a replay. The coprocessor has stopped by now, and it used one number per frame
	 * it was handed and one per frame its flow table encrypted, so the kernel resumes after the
	 * last of them - with a wide margin when there were flows, because that second count is a
	 * statistic and can be behind. The association is still referenced here: the kernel holds
	 * it across this call.
	 */
	mtx_lock(&sc->mtx);
	sav = (s->dir == 0) ? s->sav : NULL;
	past = s->seq + s->handed + s->pushed_packets +
	    (flowed ? OCTEP_SA_SEQ_FLOW_SLACK : OCTEP_SA_SEQ_SLACK);
	mtx_unlock(&sc->mtx);
	/*
	 * Not when the kernel has cloned it: the replay state is then the clone's, and the clone may
	 * already be gone. The clone resumes from where the poll last put the counter - and the
	 * poll keeps that OCTEP_SA_SEQ_FLOW_SLACK numbers ahead of its own reckoning for as long as
	 * connections use the association, because the reckoning is a statistic a second or two old
	 * and a flow uses eighty thousand numbers in one. A path (an address change under a
	 * mirrored association) that has not been exercised.
	 */
	if (sav != NULL && (sav->flags & OCTEP_SAV_F_CLONED) == 0 && sav->replay != NULL) {
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
octep_ipsec_rx(struct octep_softc *sc, struct octep_dp_if *dif, struct mbuf *m, uint32_t sa_word,
    uint32_t ident)
{
	struct epoch_tracker et;
	struct octep_pf_tuple ct;
	struct secasvar *sav = NULL;
	struct octep_sa *s;
	uint8_t hd[68];
	uint32_t hn;
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
	if (s == NULL || !s->used || s->dir != 1 || s->spi != spi ||
	    s->rev != (uint16_t)(sa_word >> 16)) {
		/*
		 * The revision too, since the flow path now rests on what this frame says about
		 * itself: an index is reused, and the revision is what tells its occupants apart.
		 * The vendor's host drops on the same mismatch.
		 */
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
	/*
	 * The frame has passed everything a decrypted frame must pass - the association is this
	 * driver's and the kernel's, the trailer parsed, enc0's rules let it through - and that
	 * makes it the one piece of evidence the flow path can have for this direction: a frame of
	 * this inner tuple arrived on this port through that association. Its metadata also names
	 * the microflow the fast path keeps for the INNER flow, which is the one to program. So it
	 * is offered as a candidate, when the operator has asked for this direction in hardware.
	 * From the packet as it is now, after the filter, which may have replaced the mbuf.
	 */
	if (sc->ipsec_flows >= 2 && (ident & 0x80000000u) != 0 && (ident & 0x01ffffffu) != 0) {
		hn = (uint32_t)m->m_pkthdr.len < sizeof(hd) ? (uint32_t)m->m_pkthdr.len :
		    (uint32_t)sizeof(hd);
		m_copydata(m, 0, (int)hn, hd);
		if (octep_dp_tuple_from_ip(hd, hn, &ct))
			octep_flow_cand_put(sc, &ct, ident & 0x01ffffffu, (ident >> 25) & 0x3fu,
			    (int)(dif - sc->dp_if), dif->tag, (uint16_t)handle,
			    (uint16_t)(sa_word >> 16), octep_dp_tcp_closing(hd, hn));
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
 * What the kernel's security policy database says about a connection, and - when it says the
 * connection belongs in a tunnel this driver mirrors - which association its microflow must name.
 *
 * The fast path forwards what it is given, and the policy is applied in ip_forward, after the point
 * a punted frame is taken from. So a connection the policy wants encrypted, programmed as a plain
 * one, leaves in the clear in hardware (measured, issue 290), and one the policy wants decrypted,
 * programmed from a frame that arrived in the clear, is forwarded past the check that would have
 * refused it. Every connection the flow maker handles is therefore asked about here first, and the
 * answer is one of three:
 *
 *    0   no policy covers either direction: a plain connection
 *    1   a tunnel this driver mirrors: *fi says which direction leaves encrypted and by which
 *        association, and the caller holds each direction's frames to what they must be
 *   -1   covered, and it stays with the host: a discard policy, or a shape this cannot carry
 *   -2   a tunnel of the right shape whose association is not on the coprocessor - not mirrored,
 *        or not yet: the kernel prefers a new association from the moment it exists, which is
 *        before this driver has finished installing it. For a new connection that is a refusal
 *        like any other. For one already in hardware it is not a reason to take it out
 *
 * Both of the connection's ingress tuples are asked, each as an outbound selector and as an inbound
 * one. The first version asked the opener's only as outbound and the responder's only as inbound,
 * which is right for a connection opened from this side and finds nothing for one opened from the
 * far side; nothing came of that only because the decrypted direction never produced a candidate.
 *
 * The lookup is the kernel's own, the way ipsec4_forward makes it: addresses, protocol, and the
 * ports left as ANY, because that is what selects the policy for a forwarded packet. A selector
 * that names a port therefore does not match here, as it does not match there - and such a
 * connection is then asked about once more WITH its ports, and left to the host if that finds a
 * policy, because whatever it is, it is not a tunnel the forward path would have used.
 *
 * And the association is the kernel's own choice, key_allocsa_policy, the call the output path
 * makes: with two alive across a rekey the kernel's preference decides, not this driver's guess.
 * Called with nothing locked and a vnet set.
 */
static void
octep_ipsec_spidx(struct secpolicyindex *spidx, const struct octep_pf_tuple *t, u_int dir,
    int withports)
{
	int ports = withports && (t->proto == IPPROTO_TCP || t->proto == IPPROTO_UDP);

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

/* 0 nothing or a bypass, 1 an IPsec policy - returned referenced through spp when asked - 2 discard. */
static int
octep_ipsec_sp_look(const struct octep_pf_tuple *t, u_int dir, int withports,
    struct secpolicy **spp)
{
	struct secpolicyindex spidx;
	struct secpolicy *sp;
	int r = 0;

	if (!key_havesp(dir))
		return (0);
	octep_ipsec_spidx(&spidx, t, dir, withports);
	sp = key_allocsp(&spidx, dir);
	if (sp == NULL)
		return (0);
	if (sp->policy == IPSEC_POLICY_IPSEC)
		r = 1;
	else if (sp->policy == IPSEC_POLICY_DISCARD)
		r = 2;
	if (r == 1 && spp != NULL)
		*spp = sp;
	else
		key_freesp(&sp);
	return (r);
}

/* One ESP transform, tunnel mode, IPv4 outside: the only request this can carry. */
static int
octep_ipsec_sp_shape(const struct secpolicy *sp)
{
	const struct secasindex *x;

	if (sp->tcount != 1 || sp->req[0] == NULL)
		return (0);
	x = &sp->req[0]->saidx;
	return (x->proto == IPPROTO_ESP && x->mode == IPSEC_MODE_TUNNEL &&
	    x->src.sa.sa_family == AF_INET && x->dst.sa.sa_family == AF_INET);
}

/* The kernel's policy generation: it moves whenever a policy is added or removed. */
uint32_t
octep_ipsec_spgen(void)
{
	return (key_getspgen());
}

/*
 * quiet: asked by the sweep about a connection that exists, so the refusal counters stay still.
 * want_sa: 0 when all the caller needs is whether a policy covers the connection - ipsec.flows is
 * 0 - and then the association is not looked for, because looking is key_allocsa_policy, and that
 * call asks the key daemon for an association when there is none.
 */
int
octep_ipsec_flow_resolve(struct octep_softc *sc, const struct octep_pf_tuple *tup,
    struct octep_ipsec_flow *fi, int quiet, int want_sa)
{
	struct epoch_tracker et;
	struct secpolicy *spo[2] = { NULL, NULL }, *spi[2] = { NULL, NULL };
	const struct secasindex *ox, *ix;
	const struct octep_sa *s;
	struct secasvar *sav;
	uint32_t i;
	int out[2], in[2], d, e, error, ret, epoch;

	if (tup[0].af != AF_INET)
		return (0);
	if (!key_havesp(IPSEC_DIR_OUTBOUND) && !key_havesp(IPSEC_DIR_INBOUND))
		return (0);

	NET_EPOCH_ENTER(et);
	epoch = 1;
	for (d = 0; d < 2; d++) {
		out[d] = octep_ipsec_sp_look(&tup[d], IPSEC_DIR_OUTBOUND, 0, &spo[d]);
		in[d] = octep_ipsec_sp_look(&tup[d], IPSEC_DIR_INBOUND, 0, &spi[d]);
	}
	ret = -1;
	if (out[0] == 0 && out[1] == 0 && in[0] == 0 && in[1] == 0) {
		/* Not a tunnel the forward path would use. Anything at all, asked with the ports? */
		ret = 0;
		for (d = 0; d < 2 && ret == 0; d++)
			if (octep_ipsec_sp_look(&tup[d], IPSEC_DIR_OUTBOUND, 1, NULL) != 0 ||
			    octep_ipsec_sp_look(&tup[d], IPSEC_DIR_INBOUND, 1, NULL) != 0)
				ret = -1;
		goto done;
	}
	if (!want_sa)
		goto done;		/* covered, and that is all that was asked */
	/*
	 * A tunnel has exactly one shape here: one direction's frames match an outbound policy and
	 * no inbound one, and the other direction's match an inbound policy and no outbound one.
	 */
	if (out[0] == 1 && in[0] == 0 && out[1] == 0 && in[1] == 1)
		e = 0;
	else if (out[1] == 1 && in[1] == 0 && out[0] == 0 && in[0] == 1)
		e = 1;
	else {
		if (!quiet)
			sc->ipsec_flow_shape++;
		goto done;
	}
	if (!octep_ipsec_sp_shape(spo[e]) || !octep_ipsec_sp_shape(spi[1 - e])) {
		if (!quiet)
			sc->ipsec_flow_shape++;
		goto done;
	}
	/* And the two policies must be the two halves of ONE tunnel: the same ends, turned round. */
	ox = &spo[e]->req[0]->saidx;
	ix = &spi[1 - e]->req[0]->saidx;
	if (ox->src.sin.sin_addr.s_addr != ix->dst.sin.sin_addr.s_addr ||
	    ox->dst.sin.sin_addr.s_addr != ix->src.sin.sin_addr.s_addr) {
		if (!quiet)
			sc->ipsec_flow_shape++;
		goto done;
	}
	/* Which tunnel, whatever becomes of the association: the caller may hold a connection to it. */
	fi->enc_dir = e;
	fi->out_chosen = 0;
	fi->req_src = ox->src.sin.sin_addr.s_addr;
	fi->req_dst = ox->dst.sin.sin_addr.s_addr;
	fi->req_reqid = ox->reqid;
	fi->in_reqid = ix->reqid;
	error = 0;
	sav = key_allocsa_policy(spo[e], ox, &error);
	ret = -2;		/* a tunnel of the right shape; from here only the association can be missing */
	if (sav == NULL) {
		if (!quiet)
			sc->ipsec_flow_nosa++;
		goto done;
	}
	fi->out_chosen = 1;
	fi->sah_src = sav->sah->saidx.src.sin.sin_addr.s_addr;
	fi->sah_dst = sav->sah->saidx.dst.sin.sin_addr.s_addr;
	fi->sah_reqid = sav->sah->saidx.reqid;
	/*
	 * Out of the epoch before the driver's lock is asked for: a command can hold that lock for
	 * two seconds, and nothing from here on needs the epoch - the association and the two
	 * policies are held by reference.
	 */
	NET_EPOCH_EXIT(et);
	epoch = 0;
	mtx_lock(&sc->mtx);
	for (i = 1; i < OCTEP_SA_MAX; i++) {
		s = &sc->ipsec_sa[i];
		if (!s->used || !s->ready || s->dir != 0 || s->sav != (void *)sav)
			continue;
		fi->enc_dir = e;
		fi->out_sa = (uint16_t)(s->idx + 1);
		fi->out_rev = s->rev;
		fi->out_dif = s->dif;
		fi->out_src = s->src;
		fi->out_dst = s->dst;
		ret = 1;
		break;
	}
	mtx_unlock(&sc->mtx);
	key_freesav(&sav);
	if (ret != 1 && !quiet)
		sc->ipsec_flow_nosa++;
done:
	if (epoch)
		NET_EPOCH_EXIT(et);
	for (d = 0; d < 2; d++) {
		if (spo[d] != NULL)
			key_freesp(&spo[d]);
		if (spi[d] != NULL)
			key_freesp(&spi[d]);
	}
	return (ret);
}

/*
 * Is the outbound association a connection names in the tunnel the policy names now? With sc->mtx
 * held. By the kernel's association head when the kernel chose an association - a rekey installs
 * its new association under the same head, another child or another peer has another - and
 * otherwise by what the policy's request says: the two ends and, when it names one, the reqid.
 * The head by what identifies it, its ends and its reqid, and not by its address: struct octep_sa
 * says why.
 */
int
octep_ipsec_flow_same_tunnel(struct octep_softc *sc, const struct octep_ipsec_flow *fi,
    uint32_t handle)
{
	const struct octep_sa *s;

	mtx_assert(&sc->mtx, MA_OWNED);
	if (handle < 2 || handle > OCTEP_SA_MAX)
		return (0);
	s = &sc->ipsec_sa[handle - 1];
	if (!s->used || s->dir != 0)
		return (0);
	if (fi->out_chosen)
		return (s->src == fi->sah_src && s->dst == fi->sah_dst &&
		    s->reqid == fi->sah_reqid);
	return (s->src == fi->req_src && s->dst == fi->req_dst &&
	    (fi->req_reqid == 0 || s->reqid == fi->req_reqid));
}

/*
 * Does this handle still name an association of this revision and direction? With sc->mtx held.
 * need_ready 0 is the audit's question - does it exist - and an association that is being removed
 * still does, until the removal has moved or taken out the connections that name it. need_ready 1
 * is the question about an association something is about to be pointed AT.
 */
int
octep_ipsec_handle_live(struct octep_softc *sc, uint32_t handle, uint32_t rev, int dir,
    int need_ready)
{
	const struct octep_sa *s;

	mtx_assert(&sc->mtx, MA_OWNED);
	if (handle < 2 || handle > OCTEP_SA_MAX)
		return (0);
	s = &sc->ipsec_sa[handle - 1];
	return (s->used && s->dir == dir && s->rev == (uint16_t)rev &&
	    (!need_ready || (s->ready && s->base_valid && s->settled)));
}

/*
 * Are the associations a tunnelled connection is about to name still what octep_ipsec_flow_resolve
 * found? The outbound one by its handle and revision; and, when the connection's other direction
 * is in hand, the inbound one a frame of it was decrypted by - dsa, from that frame's metadata -
 * which must be a decrypt association of the SAME tunnel: the same interface, the same two ends
 * turned round. That last test is what stands in for the kernel's inbound policy check on a
 * direction the kernel will no longer see.
 *
 * Called with sc->mtx held, which is the lock an association's removal takes before anything else
 * - so a connection programmed in the same hold is programmed against associations that exist.
 */
int
octep_ipsec_flow_live(struct octep_softc *sc, const struct octep_ipsec_flow *fi, uint32_t dsa,
    uint32_t dsa_rev)
{
	const struct octep_sa *s;

	mtx_assert(&sc->mtx, MA_OWNED);
	if (fi->out_sa < 2 || fi->out_sa > OCTEP_SA_MAX)
		return (0);
	s = &sc->ipsec_sa[fi->out_sa - 1];
	if (!s->used || !s->ready || !s->base_valid || !s->settled || s->dir != 0 ||
	    s->rev != fi->out_rev || s->dif != fi->out_dif)
		return (0);
	if (dsa == 0)
		return (1);
	if (dsa < 2 || dsa > OCTEP_SA_MAX)
		return (0);
	s = &sc->ipsec_sa[dsa - 1];
	/* The same tunnel turned round, and - when the inbound policy names one - its reqid. */
	return (s->used && s->ready && s->base_valid && s->settled && s->dir == 1 &&
	    s->rev == (uint16_t)dsa_rev && s->dif == fi->out_dif && s->src == fi->out_dst &&
	    s->dst == fi->out_src && (fi->in_reqid == 0 || s->reqid == fi->in_reqid));
}

/* The same question about the inbound association, for a caller that holds nothing. */
int
octep_ipsec_flow_dec_ok(struct octep_softc *sc, const struct octep_ipsec_flow *fi, uint32_t dsa,
    uint32_t dsa_rev)
{
	int ok;

	if (dsa == 0)
		return (0);
	mtx_lock(&sc->mtx);
	ok = octep_ipsec_flow_live(sc, fi, dsa, dsa_rev);
	mtx_unlock(&sc->mtx);
	return (ok);
}

/*
 * A microflow has just been made to name this association - either direction, a new connection, a
 * direction attached to one, or a connection moved here from an association that is going. With
 * sc->mtx held, by whoever wrote the handle into the connection.
 *
 * From this moment the flow table may be using sequence numbers, and counting packets, that the
 * host does not see. Three things follow, and they follow HERE because the first version worked
 * them out afterwards from the engine's counts - which is to condition a margin on the statistic
 * it is the margin for. The record is marked for good. The poll is made to read the index at least
 * once more, whether or not the connection is still there when it comes round. And for the
 * direction that encrypts, the kernel's own sequence counter is put the margin ahead at once, not
 * a poll or two later: a flow uses eighty thousand numbers a second, and that counter is where the
 * kernel's cipher would resume if the association outlived its mirror.
 */
void
octep_ipsec_sa_flow_attached(struct octep_softc *sc, uint32_t handle)
{
	struct octep_sa *s;
	struct secasvar *sav;
	uint64_t cnt;

	mtx_assert(&sc->mtx, MA_OWNED);
	if (handle < 2 || handle > OCTEP_SA_MAX)
		return;
	s = &sc->ipsec_sa[handle - 1];
	if (!s->used)
		return;
	s->flowed = 1;
	s->polled = 0;
	/*
	 * The same conditions under which the poll touches the kernel's association - and not
	 * before it is settled: until then the kernel's cipher may still be numbering packets
	 * from this counter, and raising it would hand the kernel the coprocessor's numbers.
	 */
	if (!s->ready || !s->settled || s->dir != 0 || (sav = s->sav) == NULL ||
	    octep_ipsec_sav_let_go(sav) || sav->replay == NULL)
		return;
	cnt = s->seq + s->handed + s->pushed_packets + OCTEP_SA_SEQ_FLOW_SLACK;
	SECREPLAY_LOCK(sav->replay);
	if (sav->replay->count < cnt)
		sav->replay->count = cnt;
	SECREPLAY_UNLOCK(sav->replay);
}

/*
 * The engine's counts, handed to the kernel.
 *
 * While every packet of a tunnel crossed the host the host counted it, with key_sa_recordxfer, and
 * the association's bytes and packets were right without anybody asking the coprocessor. A packet
 * a microflow forwards is one the host never sees. The engine counts those - and only those:
 * twenty pings each way by the host path read 0 and 0 - so the two counts are disjoint and the
 * kernel's total is their sum: the host goes on recording what it handles, and this pushes what
 * the flow table handled, as a cumulative total since the association was installed, which is
 * what ipsec_accel_drv_sa_lifetime_update takes. That call is the only way such traffic reaches
 * the association's lifetime: nothing in this kernel calls if_sa_cnt.
 *
 * Two things ride on it. strongSwan's byte lifetimes and its idea of whether a tunnel is in use
 * read those counters. And the kernel asks for a rekey at 80 % of the 32-bit sequence space by
 * watching its own counter for the association, which stands still while the coprocessor numbers
 * the packets - so the counter is moved here too, to where the coprocessor has got to: one number
 * per frame the host handed over and one per packet a flow encrypted. Without that a long-lived
 * association would run the coprocessor into the end of the space, where it stops and drops.
 *
 * Called once a second from the link poll, which may sleep and has a vnet. A command waits for its
 * answer with the transmit path's lock held, so a pass reads two associations and no more, and only
 * ones a connection names - plus once more after the last connection has left. The kernel's
 * association is touched under sc->mtx with the record seen ready: removal takes that lock first
 * and the kernel holds the association until removal returns, so it is there.
 */
void
octep_ipsec_stats_poll(struct octep_softc *sc)
{
	struct octep_sa *s;
	struct secasvar *sav;
	uint64_t b, p, cnt;
	uint32_t i, gen, rev, flows;
	int n, tries, need;
	if_t ifp;

	/*
	 * First, and with no command: the kernel's sequence counter for every outbound association
	 * is moved to where the coprocessor has got to by this host's own reckoning - one number
	 * per frame handed over, one per packet the flow table is known to have encrypted. For an
	 * association no connection names, that is the whole of it, and it is what lets the kernel
	 * ask for a rekey before the 32-bit space runs out on a tunnel the host carries by itself.
	 */
	mtx_lock(&sc->mtx);
	for (i = 1; i < OCTEP_SA_MAX; i++) {
		s = &sc->ipsec_sa[i];
		if (!s->used || !s->ready || !s->settled || s->dir != 0 ||
		    (sav = s->sav) == NULL || octep_ipsec_sav_let_go(sav) || sav->replay == NULL)
			continue;
		/*
		 * Kept well ahead, always. While flows are using numbers the host does not see
		 * go, because the count of those is a statistic a second or two old. And when
		 * there are none, because this is written once a second and the host hands the
		 * coprocessor tens of thousands of packets in one: a clone of the association -
		 * the kernel's own cipher again, on the same key, numbering from this counter -
		 * would otherwise start inside what the coprocessor used since the last poll. It
		 * was only kept ahead for flows until a reader of the install sequence asked what
		 * a clone resumes from. The kernel does not number from this counter while the
		 * cipher is taken, so the margin costs nothing but the numbers.
		 */
		cnt = s->seq + s->handed + s->pushed_packets + OCTEP_SA_SEQ_FLOW_SLACK;
		SECREPLAY_LOCK(sav->replay);
		if (sav->replay->count < cnt)
			sav->replay->count = cnt;
		SECREPLAY_UNLOCK(sav->replay);
	}
	mtx_unlock(&sc->mtx);

	for (n = 0, tries = 0; tries < OCTEP_SA_MAX && n < OCTEP_SA_POLL_PER_PASS; tries++) {
		i = sc->ipsec_poll_next;
		sc->ipsec_poll_next = (i + 1 < OCTEP_SA_MAX) ? i + 1 : 1;
		if (i == 0 || i >= OCTEP_SA_MAX)
			continue;
		mtx_lock(&sc->mtx);
		s = &sc->ipsec_sa[i];
		if (!s->used || !s->ready) {
			mtx_unlock(&sc->mtx);
			continue;
		}
		gen = s->gen;
		rev = s->rev;
		/*
		 * Claimed here, under the lock, and from here on this pass only ever CLEARS it:
		 * an attach on another thread sets it to 0 at any moment - that is what makes this
		 * poll read the index once more - and a 1 written at the end of the pass, from a
		 * count of connections taken before the lock was dropped, would undo exactly that.
		 */
		need = !s->polled;
		s->polled = 1;
		mtx_unlock(&sc->mtx);
		flows = octep_dp_flows_on_sa(sc, i + 1, rev);
		if (flows == 0 && !need) {
			/* Nothing names it, and its counts were read after the last one left. */
			mtx_lock(&sc->mtx);
			if (s->used && s->gen == gen)
				s->stat_time = time_uptime;
			mtx_unlock(&sc->mtx);
			continue;
		}

		n++;
		sc->ipsec_stat_polls++;
		if (octep_rpc_sa_stats(sc, i, &b, &p) != 0) {
			/* Not answering: the claim is given back, and the next pass asks again. */
			mtx_lock(&sc->mtx);
			if (s->used && s->gen == gen)
				s->polled = 0;
			mtx_unlock(&sc->mtx);
			return;
		}

		mtx_lock(&sc->mtx);
		if (s->used && s->ready && s->gen == gen) {
			sav = s->sav;
			ifp = (s->dif >= 0 && s->dif < OCTEP_DP_IF_MAX) ?
			    sc->dp_if[s->dif].ifp : NULL;
			if (octep_ipsec_stats_account(sc, s, b, p) && sav != NULL && ifp != NULL &&
			    !octep_ipsec_sav_let_go(sav)) {
				ipsec_accel_drv_sa_lifetime_update(sav, ifp, s->drv_spi,
				    s->pushed_bytes, s->pushed_packets);
				sc->ipsec_stat_pushed++;
			}
			if (flows != 0)
				s->polled = 0;
		}
		mtx_unlock(&sc->mtx);
	}
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
 *   installing                  the kernel's own esp_output still: the record has not taken
 *                               the cipher, and the kernel is the only encryptor until it has
 *   leaving                     dropped; milliseconds
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
				/*
				 * A record that has not taken the cipher yet is still being
				 * installed, and the kernel's cipher is still this association's
				 * encryptor: the packet is the kernel's. That is reached only for
				 * an association whose transform was this module's before its
				 * record existed - a clone the kernel later offers.
				 */
				found = s->taken;
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
	int on = 0, flows = 0;

	TUNABLE_INT_FETCH("hw.octep.ipsec_on", &on);
	sc->ipsec_on = (on != 0) ? 1 : 0;
	TUNABLE_INT_FETCH("hw.octep.ipsec_flows", &flows);
	sc->ipsec_flows = (flows >= 0 && flows <= 2) ? (uint32_t)flows : 0;
	sc->ipsec_poll_next = 1;
	octep_ipsec_sc = sc;
}

/*
 * ipsec.flows: how much of a tunnelled connection the flow table carries. Raising it changes
 * nothing that exists - the next punted frame makes the next connection the new way. Lowering it
 * takes out, at once, what the new value does not allow.
 */
static int
octep_sysctl_ipsec_flows(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	uint32_t v = sc->ipsec_flows, old;
	int error;

	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (v > 2)
		return (EINVAL);
	/*
	 * Under the lock every place that writes a connection re-reads it under. And the walk runs
	 * whenever the value is below 2, not only when it has just gone down: writing the value it
	 * already has is how an operator asks for the table to be held to it again.
	 */
	mtx_lock(&sc->mtx);
	old = sc->ipsec_flows;
	sc->ipsec_flows = v;
	mtx_unlock(&sc->mtx);
	if (v < 2 || v < old)
		octep_dp_flows_ipsec_out(sc, v);
	return (0);
}

static int
octep_sysctl_ipsec_table(SYSCTL_HANDLER_ARGS)
{
	struct octep_softc *sc = arg1;
	struct octep_sa s;
	struct sbuf *sb;
	char name[IFNAMSIZ];
	int error, n;
	uint32_t i, flows;

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
		flows = octep_dp_flows_on_sa(sc, s.idx + 1, s.rev);
		sbuf_printf(sb, "%3u handle %u rev %u  %s  spi 0x%08x  0x%08x -> 0x%08x  lif 0x%x  "
		    "%s  drv_spi %u  win %u  after seq %ju%s\n", s.idx, s.idx + 1, s.rev,
		    s.dir == 1 ? "decrypt" : "encrypt", ntohl(s.spi), ntohl(s.src), ntohl(s.dst),
		    s.lif, name[0] != '\0' ? name : "no interface", s.drv_spi, s.win,
		    (uintmax_t)s.seq, s.ready ? "" : "  (not ready)");
		if (flows != 0 || s.pushed_packets != 0)
			sbuf_printf(sb, "      %u connection(s) in the flow table; by flows since "
			    "install %ju bytes %ju packets, told to the kernel\n", flows,
			    (uintmax_t)s.pushed_bytes, (uintmax_t)s.pushed_packets);
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
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "sa_let_go",
	    CTLFLAG_RD, &sc->ipsec_sa_let_go, 0,
	    "outbound installs given up because the kernel had cloned the association or let it "
	    "go while it was being installed");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "seq_overlap",
	    CTLFLAG_RD, &sc->ipsec_seq_overlap, 0,
	    "outbound installs at whose end the kernel's sequence counter had reached the number "
	    "the coprocessor was started from: some numbers were used twice and the peer dropped "
	    "one of each. The margin is a million; this should read 0");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "install_us",
	    CTLFLAG_RD, &sc->ipsec_install_us, 0,
	    "microseconds the last outbound association took from the gate, after the read of "
	    "its index's counters, to settled. The kernel's cipher carries the association for "
	    "all but the last part");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "settle_us",
	    CTLFLAG_RD, &sc->ipsec_settle_us, 0,
	    "of that, the microseconds from the swap of the cipher to settled: the wait for every "
	    "packet that was inside the kernel's cipher at the swap");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "out_drop",
	    CTLFLAG_RD, &sc->ipsec_out_drop, 0,
	    "packets dropped rather than encrypted by anyone: the association leaving, a policy "
	    "with a bundle, IPv6 inside, no memory. Not an association installing: the kernel's "
	    "cipher carries it until the coprocessor has it");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flows",
	    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0, octep_sysctl_ipsec_flows, "IU",
	    "how much of a connection an IPsec policy covers the coprocessor forwards by itself. "
	    "0, none: every packet of it crosses the host. 1, the direction that leaves encrypted: "
	    "its microflow names the outbound association, and the direction that arrives as ESP "
	    "is still terminated by the host. 2, both: the direction that arrives as ESP is "
	    "decrypted and then forwarded by a plain microflow - and the fast path does not ask "
	    "whether a frame that matches that microflow was decrypted. A frame that arrives IN THE "
	    "CLEAR on the tunnel's port, from the same link-layer neighbour, with the addresses and "
	    "ports of a connection being forwarded, is forwarded too, past the policy that says it "
	    "must have come through the tunnel. Choose 2 only where whatever delivers frames to "
	    "that port is trusted not to do that. Lowering the value takes out at once what it no "
	    "longer allows. Also the loader tunable hw.octep.ipsec_flows");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_made",
	    CTLFLAG_RD, &sc->ipsec_flow_made, 0,
	    "connections a policy covers that were put in the flow table with their association");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_policy",
	    CTLFLAG_RD, &sc->ipsec_flow_policy, 0,
	    "attempts to accelerate a connection a policy covers that left it with the host, for "
	    "any of the reasons counted beside this one or because ipsec.flows is 0 (issue 290)");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_nosa",
	    CTLFLAG_RD, &sc->ipsec_flow_nosa, 0,
	    "of those: the association the kernel would use is not on the coprocessor");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_shape",
	    CTLFLAG_RD, &sc->ipsec_flow_shape, 0,
	    "of those: not one ESP tunnel over IPv4 with matching ends - a bundle, transport mode, "
	    "tunnel to tunnel, a translated connection, or an association of another tunnel");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_clear",
	    CTLFLAG_RD, &sc->ipsec_flow_clear, 0,
	    "of those: a frame that arrived in the clear in the direction the policy wants "
	    "decrypted. It made no flow, and the kernel's own check deals with the frame");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_wait",
	    CTLFLAG_RD, &sc->ipsec_flow_wait, 0,
	    "polls on which a tunnelled connection waited for its other direction to be seen");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_repoint",
	    CTLFLAG_RD, &sc->ipsec_flow_repoint, 0,
	    "directions moved to the successor of an association that was being removed");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_gone",
	    CTLFLAG_RD, &sc->ipsec_flow_gone, 0,
	    "tunnelled connections taken out of the flow table because their association was "
	    "removed with no successor, or because ipsec.flows was lowered");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_reval",
	    CTLFLAG_RD, &sc->ipsec_flow_reval, 0,
	    "connections taken out of the flow table because the kernel's policy database changed "
	    "and no longer says about them what it said when they were made: a tunnel that came up "
	    "over a plain connection, went away from under a tunnelled one, or was replaced by "
	    "another tunnel for the same addresses. Found by the sweep after each change of the "
	    "database, or by the next frame of the connection, whichever comes first");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_backoff",
	    CTLFLAG_RD, &sc->ipsec_flow_backoff, 0,
	    "polls on which a connection was left with the host because the fast path had given it "
	    "back with only its encrypting direction in hardware: ipsec.flows 1, a download");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "flow_audit",
	    CTLFLAG_RD, &sc->ipsec_flow_audit, 0,
	    "tunnelled connections the once-a-second audit took out: one that named an association "
	    "that no longer exists, or had more in hardware than ipsec.flows allows. Zero unless a "
	    "removal was cut short by the far side not answering");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "stat_polls",
	    CTLFLAG_RD, &sc->ipsec_stat_polls, 0,
	    "times the engine was asked for an association's counts");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "stat_pushed",
	    CTLFLAG_RD, &sc->ipsec_stat_pushed, 0,
	    "times those counts had moved and were handed to the kernel's association");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(node), OID_AUTO, "stat_rebase",
	    CTLFLAG_RD, &sc->ipsec_stat_rebase, 0,
	    "readings of the engine's counts that could not have been the association's own - gone "
	    "backwards, or more than the port can carry in the time - and were taken as a new "
	    "starting point instead of being told to the kernel. The engine's counters belong to "
	    "an index and outlive its occupants; this is zero unless accounting for that failed");
}
