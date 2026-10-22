/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The ARMADA driver's copy of the OCTEON TX driver's pf side, renamed and otherwise the same
 * file: nothing in it is about either coprocessor. It asks pf, through the two symbols below, what
 * it holds for a tuple, whether that is settled, and restamps or marks it.
 *
 * Ask pf whether it is tracking a connection.
 *
 * This is the one thing the driver needs from the firewall and does not already have. The
 * coprocessor punts a frame, usfp_kn_md carries that frame's microflow slot and revision - so the
 * slot arrives for free - and the only missing fact is whether pf has decided to let the
 * connection through. Everything needed to program the flow is then in hand.
 *
 * WHY THIS IS NOT A HOOK. pf calls a function pointer on every state insert and delete, at
 * pf.c:1876 and pf.c:2893, which looks exactly right and is not available: those pointers belong
 * to pfsync, which claims them without checking and sets every one of them to NULL on unload, so a
 * module that takes them breaks high availability silently and a module that chains is undone the
 * moment pfsync goes. And pfsync is loaded on a stock OPNsense with no HA configured at all. See
 * docs/octep/pf-state-to-flow.md (the OCTEON TX page; the question is the same here), which worked through three bad answers before noticing the question
 * was the wrong shape: the driver does not need to be told, it needs to ask.
 *
 * WHY THE SYMBOLS ARE LOOKED UP AND NOT LINKED. Two ways were tried and the first one failed on
 * hardware.
 *
 * MODULE_DEPEND(npuep, pf, ...) would link them, and it would make this driver refuse to load
 * without the firewall - taking twelve network interfaces with pf on a box that then has no WAN
 * and no route to it.
 *
 * So the symbols were declared __weak_symbol instead, on the strength of
 * kern/link_elf_obj.c:1792, where an unresolved weak symbol resolves to zero and the load
 * succeeds. The module loaded. The symbols were zero - with pf.ko loaded and its three state
 * lookups exported as global text. The reason is in kern_linker.c:922,
 * `if (deps) { for (i = 0; i < file->ndeps; i++) ... }`: a module's undefined symbols are resolved
 * against the kernel and against that module's own DECLARED dependencies, and nothing else. No
 * MODULE_DEPEND means pf is not in npuep's dependency list means the symbol is never looked for
 * there. A weak reference without a dependency can only ever be zero.
 *
 * What is left is to ask the linker directly: walk every loaded file and look the name up in each.
 * No dependency is declared, pf can come and go, and a kernel without it simply has no offload.
 * linker_file_foreach() takes kld_sx exclusively, so this is done once from a sleepable context and
 * cached - never from the receive path.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/linker.h>
/* npuep.h describes DMA rings, so it needs the bus types even where this file does not. */
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <machine/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <netinet/in.h>
#include <netinet/tcp_fsm.h>

#include <net/pfvar.h>

#include "npuep.h"
#include "npuflow.h"

/* How long a UDP state may read MULTIPLE:SINGLE before it is taken as it is (milliseconds). */
#define	NPUEP_PF_UDP_ONEWAY_MS	3000

/*
 * pf's own lookups, by pointer.
 *
 * The signatures are net/pfvar.h's, copied rather than included as declarations, because what is
 * being called is an address the linker handed over - there is no compile-time binding to get
 * wrong, and no compile-time check either. If pf ever changes these, this calls the old shape
 * through a new function, so the names are looked up together and used together: a version of pf
 * that renames one will fail to resolve it and the whole facility turns itself off.
 */
typedef struct pf_kstate *npuep_pf_find_t(const struct pf_state_key_cmp *, u_int, int *);
typedef bool npuep_pf_exists_t(const struct pf_state_key_cmp *, u_int);

static npuep_pf_find_t	*npuep_pf_find;
static npuep_pf_exists_t *npuep_pf_exists;
static int		 npuep_pf_tried;

struct npuep_pf_hunt {
	caddr_t	find;
	caddr_t	exists;
};

static int
npuep_pf_in_file(linker_file_t lf, void *arg)
{
	struct npuep_pf_hunt *h = arg;

	/*
	 * deps 0, deliberately. Each file is asked about itself; asking it about its dependencies
	 * too would walk the kernel from every module and find the same answer many times over.
	 * Returning 0 keeps the walk going: there is no reason to believe the first file that has
	 * one name has the other, so both are collected and checked together afterwards.
	 */
	if (h->find == NULL)
		h->find = linker_file_lookup_symbol(lf, "pf_find_state_all", 0);
	if (h->exists == NULL)
		h->exists = linker_file_lookup_symbol(lf, "pf_find_state_all_exists", 0);

	return (0);
}

/*
 * Resolve once. Safe to call repeatedly and from any sleepable context; not safe from the receive
 * path, because linker_file_foreach() takes kld_sx exclusively.
 *
 * A failed attempt is remembered as an attempt, not as an answer: pf may be loaded after this
 * driver, so npuep_pf_retry() exists to clear the flag rather than leaving a boot order to decide
 * whether the firewall is visible for the life of the module.
 */
bool
npuep_pf_present(void)
{
	struct npuep_pf_hunt h;

	if (npuep_pf_find != NULL && npuep_pf_exists != NULL)
		return (true);
	if (npuep_pf_tried)
		return (false);

	h.find = NULL;
	h.exists = NULL;
	(void)linker_file_foreach(npuep_pf_in_file, &h);

	/* Both or neither. Half of this facility is not worth having. */
	if (h.find != NULL && h.exists != NULL) {
		npuep_pf_find = (npuep_pf_find_t *)h.find;
		npuep_pf_exists = (npuep_pf_exists_t *)h.exists;
		return (true);
	}

	npuep_pf_tried = 1;
	return (false);
}

void
npuep_pf_retry(void)
{

	npuep_pf_tried = 0;
}

/*
 * Fill a comparison key.
 *
 * pf compares the key with bcmp over the whole struct pf_state_key_cmp, so every byte of it has to
 * be initialised - including pad, and including the unused twelve bytes of an IPv4 address inside
 * struct pf_addr. A key built field by field without the bzero matches nothing and looks exactly
 * like a connection pf is not tracking.
 */
static void
npuep_pf_key(struct pf_state_key_cmp *key, const struct npuep_pf_tuple *t, int comb)
{
	int s = (comb & 1) ? 1 : 0;
	int d = (comb & 1) ? 0 : 1;
	uint16_t sport = t->sport;
	uint16_t dport = t->dport;

	/*
	 * Bit 1 exchanges the two ports WITHOUT exchanging the addresses, which is only ever
	 * needed for ICMP. pf keys an echo off (virtual_id, virtual_type) and chooses which goes
	 * in nsport by the direction it decided, so the four arrangements of two addresses and two
	 * ports are four distinct keys and only one of them is the state. For TCP and UDP the port
	 * belongs to its address and bit 1 produces a key that cannot match anything, which is why
	 * it is only tried where it means something.
	 */
	if (comb & 2) {
		sport = t->dport;
		dport = t->sport;
	}

	bzero(key, sizeof(*key));
	key->af = t->af;
	key->proto = t->proto;
	if (t->af == AF_INET6) {
		memcpy(&key->addr[s].v6, t->sip6, sizeof(t->sip6));
		memcpy(&key->addr[d].v6, t->dip6, sizeof(t->dip6));
	} else {
		key->addr[s].v4.s_addr = t->sip;
		key->addr[d].v4.s_addr = t->dip;
	}
	key->port[s] = sport;
	key->port[d] = dport;
}

/*
 * How many of the four arrangements are worth trying for this protocol.
 */
static int
npuep_pf_combs(const struct npuep_pf_tuple *t)
{

	return (t->proto == IPPROTO_ICMP ? 4 : 2);
}

/*
 * Does pf have a state for this tuple, and in which index order?
 *
 * The order is asked rather than asserted. pf stores a key with pd->sidx and pd->didx, which
 * follow the direction the state was created in, so which of (src,dst) and (dst,src) matches a
 * frame arriving from the wire is a property of how the connection started - not something to
 * settle by reading pf_state_key_setup and hoping. Both orders are tried and the one that matched
 * is reported, so the convention is discovered on the appliance and written down with evidence.
 *
 * Returns 1 when a state exists. *order is 0 when the tuple matched as read off the wire and 1
 * when it matched reversed; it is untouched when nothing matched.
 */
int
npuep_pf_state_exists(const struct npuep_pf_tuple *t, int dir, int *order)
{
	struct pf_state_key_cmp key;
	int i;

	if (!npuep_pf_present())
		return (0);
	/*
	 * dir is which of pf's two lists to ask - PF_IN the wire keys, PF_OUT the stack keys - and
	 * the caller passes the one the state was found in. The hand instruments pass PF_IN as
	 * they always did; a state found through its stack key, which is how the translated
	 * state of an outbound connection is found, would be invisible to that list.
	 */
	if (dir != PF_IN && dir != PF_OUT)
		dir = PF_IN;

	for (i = 0; i < npuep_pf_combs(t); i++) {
		npuep_pf_key(&key, t, i);
		if (npuep_pf_exists(&key, (u_int)dir)) {
			if (order != NULL)
				*order = i;
			return (1);
		}
	}

	return (0);
}

/*
 * Name the two ends of a state, then turn pf's two keys into struct usfp_nat_info.
 *
 * pf keeps a wire key and a stack key for every state, and which index of a key holds which end
 * depends on the direction the state was created in: pf_setup_pdesc sets sidx to 0 for PF_IN and
 * to 1 for PF_OUT, and pf_state_key_setup stores the packet's source at sidx. So a state created by
 * an inbound SYN has its opener at index 0, and one created by an outbound SYN has it at index 1.
 * And for a translated INBOUND state the stack key is built reversed - nsaddr at didx, ndaddr at
 * sidx - because it is the key the egress lookup, with its own sidx of 1, has to match. When
 * nothing is translated pf keeps one key for both sides and the question does not arise.
 *
 * So the ends are named first - opener and responder, each in its wire form and its stack form -
 * and only then compared. The vendor's block is in those terms: ipv4_orig_src and ipv4_nat_src are
 * the opener as the host sees it and as the wire sees it, orig_dest and nat_dest the same for the
 * responder, and do_snat and do_dnat say which of the two differs. An end that is not translated
 * has the same address in both forms, which is what the fast path expects there, not a zero.
 *
 * The frame in hand decides nothing about the block. It decides one thing: whether the microflow
 * being programmed is the connection's original direction or its reply, which is whether the
 * frame's source is the opener in either of its forms.
 *
 * This replaces a derivation that compared the keys index by index and read the flag off the frame.
 * That was right for the one case it was measured on - an outbound, source-translated connection
 * seen from its reply - and wrong for the same connection seen from its own direction, which it
 * called do_dnat, and for every translated inbound state, whose reversed stack key it would have
 * read as a connection rewritten at both ends.
 */
static void
npuep_pf_orient(struct npuep_pf_state *out, const struct npuep_pf_tuple *t)
{
	int ow, os, rw, rs;	/* opener and responder: index in the wire key, in the stack key */

	if (out->direction == PF_IN) {
		ow = 0;
		os = out->keys_shared ? 0 : 1;
	} else {
		ow = 1;
		os = 1;
	}
	rw = ow ^ 1;
	rs = os ^ 1;

	out->orig_src = out->stack_addr[os];
	out->orig_sport = out->stack_port[os];
	out->nat_src = out->wire_addr[ow];
	out->nat_sport = out->wire_port[ow];
	out->orig_dst = out->stack_addr[rs];
	out->orig_dport = out->stack_port[rs];
	out->nat_dst = out->wire_addr[rw];
	out->nat_dport = out->wire_port[rw];

	out->nat_snat = (out->orig_src != out->nat_src || out->orig_sport != out->nat_sport);
	out->nat_dnat = (out->orig_dst != out->nat_dst || out->orig_dport != out->nat_dport);
	if (out->af == AF_INET6) {
		/* The ends in full, and a translation is any difference between the two forms. */
		memcpy(out->orig_src6, out->stack_addr6[os], 16);
		memcpy(out->orig_dst6, out->stack_addr6[rs], 16);
		out->nat_snat = out->nat_snat ||
		    memcmp(out->stack_addr6[os], out->wire_addr6[ow], 16) != 0;
		out->nat_dnat = out->nat_dnat ||
		    memcmp(out->stack_addr6[rs], out->wire_addr6[rw], 16) != 0;
	}
	out->nat_valid = (out->nat_snat || out->nat_dnat);

	if (out->af == AF_INET6)
		out->original = (memcmp(t->sip6, out->stack_addr6[os], 16) == 0 &&
		    t->sport == out->orig_sport) ||
		    (memcmp(t->sip6, out->wire_addr6[ow], 16) == 0 && t->sport == out->nat_sport);
	else
		out->original = ((t->sip == out->orig_src && t->sport == out->orig_sport) ||
		    (t->sip == out->nat_src && t->sport == out->nat_sport));
}

/*
 * Does pf change, in every packet of this state, something the fast path would forward unchanged?
 *
 * Two things, both in pf's own peers. A sequence number modulator: modulate state gives each side
 * one, and so does synproxy state, whose handshake pf answers itself with numbers of its own -
 * pf then adds seqdiff to every sequence number and takes it from every acknowledgement. And a
 * timestamp modulator: scrub with reassemble tcp gives each side a random pfss_ts_mod, added to
 * every timestamp value and taken from every echo. A frame the coprocessor forwards carries
 * neither change, and the two ends then see numbers that do not follow - so a state with either is
 * never offloaded. Read under the state's lock.
 */
static int
npuep_pf_rewrites(const struct pf_kstate *s)
{

	if (s->src.seqdiff != 0 || s->dst.seqdiff != 0)
		return (1);
	if ((s->src.scrub != NULL && (s->src.scrub->pfss_flags & PFSS_TIMESTAMP) != 0) ||
	    (s->dst.scrub != NULL && (s->dst.scrub->pfss_flags & PFSS_TIMESTAMP) != 0))
		return (1);
	return (0);
}

/*
 * Copy what a flow needs out of a state, under its lock, and release it.
 *
 * pf_find_state_all returns with PF_STATE_LOCK(s) HELD when more is NULL - it takes the hashrow
 * lock, finds the key, takes the state lock and drops the hashrow lock before returning. The keys
 * are pointers into memory the state owns, so they are read here and nowhere else. A state always
 * has both; they are only ever NULL while it is being taken apart, and a lookup does not return
 * one of those.
 */
static void
npuep_pf_copy(struct npuep_pf_state *out, struct pf_kstate *s)
{
	const struct pf_state_key *w = s->key[PF_SK_WIRE];
	const struct pf_state_key *k = s->key[PF_SK_STACK];

	out->direction = s->direction;
	out->timeout = s->timeout;
	out->state_flags = s->state_flags;
	out->src_state = s->src.state;
	out->dst_state = s->dst.state;
	out->seqlo[0] = s->src.seqlo;
	out->seqlo[1] = s->dst.seqlo;
	out->seqhi[0] = s->src.seqhi;
	out->seqhi[1] = s->dst.seqhi;
	out->max_win[0] = s->src.max_win;
	out->max_win[1] = s->dst.max_win;
	out->wscale[0] = s->src.wscale;
	out->wscale[1] = s->dst.wscale;
	out->rewrites = npuep_pf_rewrites(s);
	strlcpy(out->ifname, s->kif != NULL ? s->kif->pfik_name : "", sizeof(out->ifname));
	bzero(out->wire_addr, sizeof(out->wire_addr));
	bzero(out->stack_addr, sizeof(out->stack_addr));
	bzero(out->wire_port, sizeof(out->wire_port));
	bzero(out->stack_port, sizeof(out->stack_port));
	bzero(out->wire_addr6, sizeof(out->wire_addr6));
	bzero(out->stack_addr6, sizeof(out->stack_addr6));
	out->af = (w != NULL) ? w->af : (k != NULL ? k->af : AF_INET);
	if (w != NULL) {
		if (out->af == AF_INET6) {
			memcpy(out->wire_addr6[0], &w->addr[0].v6, 16);
			memcpy(out->wire_addr6[1], &w->addr[1].v6, 16);
			out->wire_addr[0] = npuflow_fold6(out->wire_addr6[0]);
			out->wire_addr[1] = npuflow_fold6(out->wire_addr6[1]);
		} else {
			out->wire_addr[0] = w->addr[0].v4.s_addr;
			out->wire_addr[1] = w->addr[1].v4.s_addr;
		}
		out->wire_port[0] = w->port[0];
		out->wire_port[1] = w->port[1];
	}
	if (k != NULL) {
		if (out->af == AF_INET6) {
			memcpy(out->stack_addr6[0], &k->addr[0].v6, 16);
			memcpy(out->stack_addr6[1], &k->addr[1].v6, 16);
			out->stack_addr[0] = npuflow_fold6(out->stack_addr6[0]);
			out->stack_addr[1] = npuflow_fold6(out->stack_addr6[1]);
		} else {
			out->stack_addr[0] = k->addr[0].v4.s_addr;
			out->stack_addr[1] = k->addr[1].v4.s_addr;
		}
		out->stack_port[0] = k->port[0];
		out->stack_port[1] = k->port[1];
	}
	/*
	 * One key for both sides is how pf records an untranslated connection. Equal contents
	 * under two pointers is treated the same way, so a key pf chose to copy rather than share
	 * cannot be read as a reversed one. For IPv6 the full addresses are compared, not folds.
	 */
	out->keys_shared = (w == k ||
	    (out->wire_addr[0] == out->stack_addr[0] && out->wire_addr[1] == out->stack_addr[1] &&
	    out->wire_port[0] == out->stack_port[0] && out->wire_port[1] == out->stack_port[1] &&
	    (out->af != AF_INET6 ||
	    memcmp(out->wire_addr6, out->stack_addr6, sizeof(out->wire_addr6)) == 0)));
	PF_STATE_UNLOCK(s);
}

/*
 * Read the state pf holds for this tuple, preferring the one that carries the translation.
 *
 * A frame punted from a LAN port on its way out is held by TWO states. The rule on its own
 * interface made one, found through pf's wire list with the frame's source first; its two keys
 * agree, because nothing is rewritten on the way in. The rule on the WAN made the other when the
 * connection was opened; it is found only through the STACK list, and only with the addresses
 * reversed, and it is the one whose keys differ. Stopping at the first match - the wire list, the
 * frame's own arrangement - programmed the outbound half of every translated connection without
 * its NAT block, so its frames left the WAN carrying a private source address and were lost. That
 * is the mechanism behind the throughput collapse of issue #268: every acknowledgement of a
 * connection whose both directions were accelerated went missing, and its server stopped within
 * one window. The reply direction, found through the WAN state's wire key, was right all along,
 * which is why the one forwarded frame that was read byte by byte was correct.
 *
 * So every arrangement is tried in both of pf's lists, a state with a translation wins, and an
 * untranslated one is used only when no other exists.
 */
int
npuep_pf_state_read(const struct npuep_pf_tuple *t, struct npuep_pf_state *out)
{
	struct npuep_pf_state plain;
	struct pf_state_key_cmp key;
	struct pf_kstate *s;
	const u_int dirs[2] = { PF_IN, PF_OUT };
	int d, order, have_plain = 0;

	if (!npuep_pf_present())
		return (0);
	for (d = 0; d < 2; d++) {
		for (order = 0; order < npuep_pf_combs(t); order++) {
			npuep_pf_key(&key, t, order);
			s = npuep_pf_find(&key, dirs[d], NULL);
			if (s == NULL)
				continue;
			npuep_pf_copy(out, s);
			out->order = order;
			out->lookup_dir = (int)dirs[d];
			npuep_pf_orient(out, t);
			if (out->nat_valid)
				return (1);
			if (!have_plain) {
				plain = *out;
				have_plain = 1;
			}
		}
	}
	if (have_plain) {
		*out = plain;
		return (1);
	}
	return (0);
}

/*
 * Is every state pf keeps for this connection on the long timer of its kind - a TCP connection's
 * handshake seen through, a UDP one's both peers seen more than once?
 *
 * A state pf stops seeing keeps the timer of the last packet it tracked. One made by a SYN and a
 * SYN-ACK and never shown the final ACK is on tcp.opening, thirty seconds, and nothing the driver
 * does afterwards changes that: marking it sloppy does not, and restamping it buys thirty more
 * seconds of the wrong timer. So a connection is not taken from pf before both of its peers read
 * ESTABLISHED - which is also "not closing", since the states after it are all larger. Every
 * state of the tuple is asked, because a forwarded connection has one per side and the one the
 * driver happens to read first is, for an untranslated connection, the one that leads.
 *
 * The timer class is asked as well as the peers. After an ordinary handshake they agree; after a
 * synproxy one pf sets both peers ESTABLISHED and leaves the state on tcp.first until the next
 * packet it tracks, and "on its long timer" is the thing that is wanted.
 *
 * UDP has the same shape and it was missed at first. A UDP state is on udp.multiple, sixty
 * seconds, only when both peers read MULTIPLE, and that takes the opener, the responder and the
 * opener again; made after one datagram each way it is MULTIPLE:SINGLE on udp.single, thirty, for
 * good.
 *
 * And waiting for that for ever was a mistake of its own, which a reader found. A flow whose
 * opener sends once and whose responder then streams never has its second peer seen twice, so it
 * was never made at all - where pf by itself forwards it on the thirty-second timer for as long
 * as it lasts. So a state that still reads MULTIPLE:SINGLE when it is NPUEP_PF_UDP_ONEWAY_MS old
 * is taken as it is. The opener is always the state's source: both states of a forwarded
 * connection are made by its first datagram.
 *
 * A state that is being removed is not settled, whatever its peers read: the purge thread marks
 * it before it takes it off the list this lookup goes through.
 *
 * Nothing else is asked. Of the other protocols only an ICMP echo is ever made a connection of,
 * and its state has no longer timer to wait for.
 *
 * Returns 1 when at least one state was found and all of them are settled.
 */
int
npuep_pf_settled(const struct npuep_pf_tuple *t)
{
	struct pf_state_key_cmp key;
	struct pf_kstate *s;
	const u_int dirs[2] = { PF_IN, PF_OUT };
	uint64_t now;
	int d, order, n = 0, bad = 0;

	if (!npuep_pf_present())
		return (0);
	if (t->proto != IPPROTO_TCP && t->proto != IPPROTO_UDP)
		return (1);
	now = pf_get_uptime();
	for (d = 0; d < 2; d++) {
		for (order = 0; order < npuep_pf_combs(t); order++) {
			npuep_pf_key(&key, t, order);
			s = npuep_pf_find(&key, dirs[d], NULL);
			if (s == NULL)
				continue;
			n++;
			if (s->timeout >= PFTM_MAX) {
				bad++;
			} else if (t->proto == IPPROTO_TCP) {
				if (s->src.state != TCPS_ESTABLISHED ||
				    s->dst.state != TCPS_ESTABLISHED ||
				    s->timeout != PFTM_TCP_ESTABLISHED)
					bad++;
			} else if (s->src.state != PFUDPS_MULTIPLE ||
			    (s->dst.state != PFUDPS_MULTIPLE &&
			    (s->dst.state != PFUDPS_SINGLE ||
			    now - s->creation < NPUEP_PF_UDP_ONEWAY_MS))) {
				bad++;
			}
			PF_STATE_UNLOCK(s);
		}
	}
	return (n != 0 && bad == 0);
}

/*
 * Restamp every state pf keeps for this tuple, as a packet of the connection would have.
 *
 * pf_state_expires() is the stamp plus the timeout of the state's class, and the purge thread
 * removes a state the moment that is past - whether the connection behind it has ended or has
 * only stopped coming this way. For a connection the coprocessor forwards it is always the second.
 * The stamp is milliseconds of uptime and is written under the state's own lock, which
 * pf_find_state_all returns holding: exactly what pf's trackers do for a packet. The class is left
 * alone, so a state stays on the timer pf itself last chose for it.
 *
 * The caller decides that the connection is alive. Called unconditionally this would keep a dead
 * connection's state, and the driver's table entry with it, for ever.
 */
int
npuep_pf_touch(const struct npuep_pf_tuple *t)
{
	struct pf_state_key_cmp key;
	struct pf_kstate *s;
	const u_int dirs[2] = { PF_IN, PF_OUT };
	int d, order, n = 0;

	if (!npuep_pf_present())
		return (0);
	for (d = 0; d < 2; d++) {
		for (order = 0; order < npuep_pf_combs(t); order++) {
			npuep_pf_key(&key, t, order);
			s = npuep_pf_find(&key, dirs[d], NULL);
			if (s == NULL)
				continue;
			if (s->timeout < PFTM_MAX) {
				s->expire = pf_get_uptime();
				n++;
			}
			PF_STATE_UNLOCK(s);
		}
	}
	return (n);
}

/*
 * Stop pf judging this connection's sequence numbers, on every state it holds for it.
 *
 * While a connection is forwarded by the coprocessor, pf sees none of it: its idea of where each
 * side's sequence numbers are stays where the last punted frame left it, while the real window
 * moves on by whatever was forwarded in hardware. The first frame of that connection to reach pf
 * again - after the flow is discarded, or because the fast path handed one back - is then judged
 * against a window pf never saw advance, found outside it, and dropped as a bad state. Nothing on
 * the coprocessor counts that drop, because it does not happen there.
 *
 * pf has a mode for exactly this: a sloppy state, which tracks the TCP handshake, FIN and RST but
 * not the window. A rule gets it with `keep state (sloppy)`; this sets the same flag on the states
 * already there, under the lock pf_find_state_all returns holding. Both of the connection's states
 * are marked, because a frame crosses both. Clearing the flag when the flow is discarded would re-arm
 * the very check that the returning frames fail - unless pf is given the numbers the fast path
 * moved on to, which a take-back does since issue #679 (npuep_pf_handback). Without them the flag stays
 * for the life of the state, as it did for every connection before.
 *
 * Returns how many states were marked, which the caller prints so a flow whose pf state could not
 * be found says so.
 */
int
npuep_pf_mark_sloppy(const struct npuep_pf_tuple *t)
{
	struct pf_state_key_cmp key;
	struct pf_kstate *s;
	const u_int dirs[2] = { PF_IN, PF_OUT };
	int d, order, n = 0;

	if (!npuep_pf_present())
		return (0);
	for (d = 0; d < 2; d++) {
		for (order = 0; order < npuep_pf_combs(t); order++) {
			npuep_pf_key(&key, t, order);
			s = npuep_pf_find(&key, dirs[d], NULL);
			if (s == NULL)
				continue;
			if ((s->state_flags & PFSTATE_SLOPPY) == 0) {
				s->state_flags |= PFSTATE_SLOPPY;
				n++;
			}
			PF_STATE_UNLOCK(s);
		}
	}
	return (n);
}

/*
 * pf's rule counters take at most 32 bits at a time, and a constant above 2^31 cannot be the
 * immediate of the add that counter.h builds them on - so the chunk is 2^30.
 */
#define	NPUEP_PF_ADD_CHUNK	0x40000000u
static void
npuep_pf_add64(struct pf_counter_u64 *c, uint64_t n)
{
	while (n > NPUEP_PF_ADD_CHUNK) {
		pf_counter_u64_add_protected(c, NPUEP_PF_ADD_CHUNK);
		n -= NPUEP_PF_ADD_CHUNK;
	}
	if (n != 0)
		pf_counter_u64_add_protected(c, (uint32_t)n);
}

/*
 * Add what the coprocessor counted for a connection to pf's own count of it, as the packets would
 * have added it had they come through pf: on each state pf keeps for the connection, its packets
 * and bytes for each direction; and on the rule that passed it, the same, by whether the packet
 * entered or left that state's interface - pf_counters_inc's arithmetic, for the state and its
 * rule. pk and by are [the opener's direction, the other]; a state pf made for the opener counts
 * the opener's packets in its [0], which is how pf orients every state the flow maker takes. The
 * OCTEON TX driver's octep_pf_count, the same function.
 *
 * The two tuples find the same states more than once - each state is reachable by either key, in
 * either order - so a state is counted once, by its id. Not added to: the interface's pf counters,
 * the tables a rule names, match and NAT rules, source nodes. Returns how many states were counted.
 */
int
npuep_pf_count(const struct npuep_pf_tuple *tup, const uint64_t *pk, const uint64_t *by)
{
	struct pf_state_key_cmp key;
	struct pf_kstate *s;
	const u_int dirs[2] = { PF_IN, PF_OUT };
	uint64_t seen[4];
	int t, d, order, i, w, rd, dup, n = 0;

	if (!npuep_pf_present())
		return (0);
	for (t = 0; t < 2; t++) {
		for (d = 0; d < 2; d++) {
			for (order = 0; order < npuep_pf_combs(&tup[t]); order++) {
				npuep_pf_key(&key, &tup[t], order);
				s = npuep_pf_find(&key, dirs[d], NULL);
				if (s == NULL)
					continue;
				dup = 0;
				for (i = 0; i < n; i++)
					if (seen[i] == s->id)
						dup = 1;
				if (dup || n == (int)nitems(seen)) {
					PF_STATE_UNLOCK(s);
					continue;
				}
				seen[n++] = s->id;
				for (w = 0; w < 2; w++) {
					s->packets[w] += pk[w];
					s->bytes[w] += by[w];
				}
				if (s->rule != NULL) {
					pf_counter_u64_critical_enter();
					for (w = 0; w < 2; w++) {
						/* 1 for a packet leaving: the opener's leaves an outbound state */
						rd = (s->direction == PF_OUT) ^ w;
						npuep_pf_add64(&s->rule->packets[rd], pk[w]);
						npuep_pf_add64(&s->rule->bytes[rd], by[w]);
					}
					pf_counter_u64_critical_exit();
				}
				PF_STATE_UNLOCK(s);
			}
		}
	}
	return (n);
}

/*
 * Every state pf keeps for a connection, asked what it says about the window: whether it rewrites
 * what the fast path cannot (npuep_pf_rewrites), whether a rule made it sloppy, and whether it is
 * sloppy for another reason - which, for a state of a connection that was in hardware before,
 * means its numbers stopped where the hardware took over and say nothing about the window now.
 * Both of the connection's tuples are asked, in both of pf's lists, and a state is counted once
 * by its id, as npuep_pf_count does. Returns how many states were found.
 */
int
npuep_pf_tcp_scan(const struct npuep_pf_tuple *tup, struct npuep_pf_tcp *out)
{
	struct pf_state_key_cmp key;
	struct pf_kstate *s;
	const u_int dirs[2] = { PF_IN, PF_OUT };
	uint64_t seen[8];
	int t, d, order, i, dup, n = 0;

	bzero(out, sizeof(*out));
	if (!npuep_pf_present())
		return (0);
	for (t = 0; t < 2; t++) {
		for (d = 0; d < 2; d++) {
			for (order = 0; order < npuep_pf_combs(&tup[t]); order++) {
				npuep_pf_key(&key, &tup[t], order);
				s = npuep_pf_find(&key, dirs[d], NULL);
				if (s == NULL)
					continue;
				dup = 0;
				for (i = 0; i < n; i++)
					if (seen[i] == s->id)
						dup = 1;
				if (dup || n == (int)nitems(seen)) {
					PF_STATE_UNLOCK(s);
					continue;
				}
				seen[n++] = s->id;
				if (npuep_pf_rewrites(s))
					out->rewrites = 1;
				if ((s->state_flags & PFSTATE_SLOPPY) != 0) {
					if (s->rule != NULL &&
					    (s->rule->rule_flag & PFRULE_STATESLOPPY) != 0)
						out->rule_sloppy = 1;
					else
						out->sloppy = 1;
				}
				PF_STATE_UNLOCK(s);
			}
		}
	}
	out->states = n;
	return (n);
}

/*
 * Which end of the connection a key's index holds: 1 the opener, 0 the responder, -1 neither. The
 * opener is tup[0]'s source, as it arrives, and tup[1]'s destination, as the responder's frames
 * address it - its two forms when the connection is translated - and the responder the reverse.
 */
static int
npuep_pf_end(const struct pf_state_key *k, int i, const struct npuep_pf_tuple *tup)
{
	uint16_t port = k->port[i];

	if (k->af == AF_INET6) {
		const void *a = &k->addr[i].v6;

		if ((memcmp(a, tup[0].sip6, 16) == 0 && port == tup[0].sport) ||
		    (memcmp(a, tup[1].dip6, 16) == 0 && port == tup[1].dport))
			return (1);
		if ((memcmp(a, tup[1].sip6, 16) == 0 && port == tup[1].sport) ||
		    (memcmp(a, tup[0].dip6, 16) == 0 && port == tup[0].dport))
			return (0);
		return (-1);
	}
	if ((k->addr[i].v4.s_addr == tup[0].sip && port == tup[0].sport) ||
	    (k->addr[i].v4.s_addr == tup[1].dip && port == tup[1].dport))
		return (1);
	if ((k->addr[i].v4.s_addr == tup[1].sip && port == tup[1].sport) ||
	    (k->addr[i].v4.s_addr == tup[0].dip && port == tup[0].dport))
		return (0);
	return (-1);
}

/*
 * How far past the right edge the fast path last saw pf lets a sender go once the connection is
 * handed back, and how far apart the fast path's own numbers may be and still be a window. See
 * npuep_pf_handback.
 */
#define	NPUEP_PF_HANDBACK_ROOM	(4u << 20)
#define	NPUEP_PF_HANDBACK_ROOM_MAX (1u << 28)
#define	NPUEP_PF_HANDBACK_SPAN	(1u << 30)

/*
 * Give pf the fast path's window numbers for a connection taken back from it, on every state pf
 * keeps for it, and clear the sloppy mark this driver set when it was made (issue #679).
 *
 * seq is what CONN_RECLAIM_FP answered with - struct usfp_tcp_seq, seen[0] the opener's direction
 * - and scale the two window scales the connection was sent with. For each peer: seqlo, the
 * highest sequence sent, is the fast path's td_end; seqhi, the highest the peer may send, is its
 * td_maxend; max_win is its td_maxwin with the scale taken off, rounded up, and only ever raised.
 * Then pf judges the connection's frames itself again, from where the fast path left them. The
 * sequence numbers are the fast path's alone, never compared with pf's: pf's stopped when the
 * connection went to hardware, and after four gigabytes the comparison of two sequence numbers
 * says nothing about which is the later - the first version kept pf's seqhi when it looked ahead.
 *
 * Not exactly from there, and the difference is the frames between. The fast path stops forwarding
 * the connection a moment before it answers the reclaim, and the frames of that moment reach pf
 * while it is still sloppy and moves nothing. A sender that kept sending is then past td_maxend by
 * what crossed in the gap, and pf would drop its next segment and every one after it - and the
 * other side's acknowledgements of those bytes too, being further ahead of seqlo than pf allows.
 * So seqhi is given room: twice the other side's largest window or NPUEP_PF_HANDBACK_ROOM, which
 * is a gap of a few milliseconds at line rate. Once one of the sender's segments has passed, seqlo
 * has caught up and the acknowledgements pass too; the room is used up as the window moves on.
 *
 * Nothing is written when the answer does not look like a window: a direction whose td_end and
 * td_maxend are both zero, or a gigabyte or more apart, or whose td_maxwin is a gigabyte or more -
 * the state stays sloppy, as it was. pf's own numbers cannot be the test: they stopped when the
 * connection went to hardware, and a transfer since has moved td_end by any amount modulo 2^32 -
 * the first version compared them and refused a download of four gigabytes as implausible.
 * Nor is anything written to a state that rewrites what the fast path cannot, or one whose ends
 * are not the tuples'. A rule's own keep state (sloppy) is left as the rule made it. Returns how
 * many states took the numbers.
 */
int
npuep_pf_handback(const struct npuep_pf_tuple *tup, const uint8_t *seq, const uint8_t *scale)
{
	struct pf_state_key_cmp key;
	struct pf_kstate *s;
	struct pf_state_peer *p[2];
	const u_int dirs[2] = { PF_IN, PF_OUT };
	uint32_t end[2], maxend[2], maxwin[2], room, w;
	uint64_t seen[8];
	int t, d, order, i, k, o, dup, ok, n = 0, ns = 0;

	if (!npuep_pf_present())
		return (0);
	for (k = 0; k < 2; k++) {
		end[k] = le32dec(seq + NPUFLOW_TCP_SEEN(k));
		maxend[k] = le32dec(seq + NPUFLOW_TCP_SEEN(k) + 4);
		maxwin[k] = le32dec(seq + NPUFLOW_TCP_SEEN(k) + 8);
	}
	for (t = 0; t < 2; t++) {
		for (d = 0; d < 2; d++) {
			for (order = 0; order < npuep_pf_combs(&tup[t]); order++) {
				npuep_pf_key(&key, &tup[t], order);
				s = npuep_pf_find(&key, dirs[d], NULL);
				if (s == NULL)
					continue;
				dup = 0;
				for (i = 0; i < ns; i++)
					if (seen[i] == s->id)
						dup = 1;
				if (dup || ns == (int)nitems(seen)) {
					PF_STATE_UNLOCK(s);
					continue;
				}
				seen[ns++] = s->id;
				/* pf's src is the end that sent the packet that made the state */
				o = (s->key[PF_SK_WIRE] == NULL) ? -1 :
				    npuep_pf_end(s->key[PF_SK_WIRE], s->direction == PF_IN ? 0 : 1, tup);
				if (o < 0 || npuep_pf_rewrites(s)) {
					PF_STATE_UNLOCK(s);
					continue;
				}
				p[0] = o ? &s->src : &s->dst;
				p[1] = o ? &s->dst : &s->src;
				ok = 1;
				for (k = 0; k < 2; k++)
					if ((end[k] == 0 && maxend[k] == 0) ||
					    maxend[k] - end[k] + NPUEP_PF_HANDBACK_SPAN >=
					    2 * NPUEP_PF_HANDBACK_SPAN ||
					    maxwin[k] >= NPUEP_PF_HANDBACK_SPAN)
						ok = 0;
				if (!ok) {
					PF_STATE_UNLOCK(s);
					continue;
				}
				for (k = 0; k < 2; k++) {
					room = maxwin[k ^ 1] < NPUEP_PF_HANDBACK_ROOM_MAX / 2 ?
					    2 * maxwin[k ^ 1] : NPUEP_PF_HANDBACK_ROOM_MAX;
					if (room < NPUEP_PF_HANDBACK_ROOM)
						room = NPUEP_PF_HANDBACK_ROOM;
					p[k]->seqlo = end[k];
					p[k]->seqhi = maxend[k] + room;
					w = (scale[k] < 15) ?
					    (uint32_t)(((uint64_t)maxwin[k] + (1u << scale[k]) - 1) >>
					    scale[k]) : 0;
					if (w > 65535)
						w = 65535;
					if (w > p[k]->max_win)
						p[k]->max_win = (uint16_t)w;
				}
				if ((s->state_flags & PFSTATE_SLOPPY) != 0 && (s->rule == NULL ||
				    (s->rule->rule_flag & PFRULE_STATESLOPPY) == 0))
					s->state_flags &= ~PFSTATE_SLOPPY;
				n++;
				PF_STATE_UNLOCK(s);
			}
		}
	}
	return (n);
}
