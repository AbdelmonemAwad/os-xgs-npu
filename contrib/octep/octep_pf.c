/*-
 * SPDX-License-Identifier: BSD-2-Clause
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
 * docs/pf-state-to-flow.md, which worked through three bad answers before noticing the question
 * was the wrong shape: the driver does not need to be told, it needs to ask.
 *
 * WHY THE SYMBOLS ARE LOOKED UP AND NOT LINKED. Two ways were tried and the first one failed on
 * hardware.
 *
 * MODULE_DEPEND(octep, pf, ...) would link them, and it would make this driver refuse to load
 * without the firewall - taking twelve network interfaces with pf on a box that then has no WAN
 * and no route to it.
 *
 * So the symbols were declared __weak_symbol instead, on the strength of
 * kern/link_elf_obj.c:1792, where an unresolved weak symbol resolves to zero and the load
 * succeeds. The module loaded. The symbols were zero - with pf.ko loaded and its three state
 * lookups exported as global text. The reason is in kern_linker.c:922,
 * `if (deps) { for (i = 0; i < file->ndeps; i++) ... }`: a module's undefined symbols are resolved
 * against the kernel and against that module's own DECLARED dependencies, and nothing else. No
 * MODULE_DEPEND means pf is not in octep's dependency list means the symbol is never looked for
 * there. A weak reference without a dependency can only ever be zero.
 *
 * What is left is to ask the linker directly: walk every loaded file and look the name up in each.
 * No dependency is declared, pf can come and go, and a kernel without it simply has no offload.
 * linker_file_foreach() takes kld_sx exclusively, so this is done once from a sleepable context and
 * cached - never from the receive path.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/linker.h>
/* octep.h describes DMA rings, so it needs the bus types even where this file does not. */
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <machine/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <netinet/in.h>
#include <netinet/tcp_fsm.h>

#include <net/pfvar.h>

#include "octep.h"

/*
 * pf's own lookups, by pointer.
 *
 * The signatures are net/pfvar.h's, copied rather than included as declarations, because what is
 * being called is an address the linker handed over - there is no compile-time binding to get
 * wrong, and no compile-time check either. If pf ever changes these, this calls the old shape
 * through a new function, so the names are looked up together and used together: a version of pf
 * that renames one will fail to resolve it and the whole facility turns itself off.
 */
typedef struct pf_kstate *octep_pf_find_t(const struct pf_state_key_cmp *, u_int, int *);
typedef bool octep_pf_exists_t(const struct pf_state_key_cmp *, u_int);

static octep_pf_find_t	*octep_pf_find;
static octep_pf_exists_t *octep_pf_exists;
static int		 octep_pf_tried;

struct octep_pf_hunt {
	caddr_t	find;
	caddr_t	exists;
};

static int
octep_pf_in_file(linker_file_t lf, void *arg)
{
	struct octep_pf_hunt *h = arg;

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
 * driver, so octep_pf_retry() exists to clear the flag rather than leaving a boot order to decide
 * whether the firewall is visible for the life of the module.
 */
bool
octep_pf_present(void)
{
	struct octep_pf_hunt h;

	if (octep_pf_find != NULL && octep_pf_exists != NULL)
		return (true);
	if (octep_pf_tried)
		return (false);

	h.find = NULL;
	h.exists = NULL;
	(void)linker_file_foreach(octep_pf_in_file, &h);

	/* Both or neither. Half of this facility is not worth having. */
	if (h.find != NULL && h.exists != NULL) {
		octep_pf_find = (octep_pf_find_t *)h.find;
		octep_pf_exists = (octep_pf_exists_t *)h.exists;
		return (true);
	}

	octep_pf_tried = 1;
	return (false);
}

void
octep_pf_retry(void)
{

	octep_pf_tried = 0;
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
octep_pf_key(struct pf_state_key_cmp *key, const struct octep_pf_tuple *t, int comb)
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
	key->addr[s].v4.s_addr = t->sip;
	key->addr[d].v4.s_addr = t->dip;
	key->port[s] = sport;
	key->port[d] = dport;
}

/*
 * How many of the four arrangements are worth trying for this protocol.
 */
static int
octep_pf_combs(const struct octep_pf_tuple *t)
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
octep_pf_state_exists(const struct octep_pf_tuple *t, int dir, int *order)
{
	struct pf_state_key_cmp key;
	int i;

	if (!octep_pf_present())
		return (0);
	/*
	 * dir is which of pf's two lists to ask - PF_IN the wire keys, PF_OUT the stack keys - and
	 * the caller passes the one the state was found in. The hand instruments pass PF_IN as
	 * they always did; a state found through its stack key, which is how the translated
	 * state of an outbound connection is found, would be invisible to that list.
	 */
	if (dir != PF_IN && dir != PF_OUT)
		dir = PF_IN;

	for (i = 0; i < octep_pf_combs(t); i++) {
		octep_pf_key(&key, t, i);
		if (octep_pf_exists(&key, (u_int)dir)) {
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
octep_pf_orient(struct octep_pf_state *out, const struct octep_pf_tuple *t)
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
	out->nat_valid = (out->nat_snat || out->nat_dnat);

	out->original = ((t->sip == out->orig_src && t->sport == out->orig_sport) ||
	    (t->sip == out->nat_src && t->sport == out->nat_sport));
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
octep_pf_copy(struct octep_pf_state *out, struct pf_kstate *s)
{
	const struct pf_state_key *w = s->key[PF_SK_WIRE];
	const struct pf_state_key *k = s->key[PF_SK_STACK];

	out->direction = s->direction;
	out->timeout = s->timeout;
	out->state_flags = s->state_flags;
	out->src_state = s->src.state;
	out->dst_state = s->dst.state;
	strlcpy(out->ifname, s->kif != NULL ? s->kif->pfik_name : "", sizeof(out->ifname));
	bzero(out->wire_addr, sizeof(out->wire_addr));
	bzero(out->stack_addr, sizeof(out->stack_addr));
	bzero(out->wire_port, sizeof(out->wire_port));
	bzero(out->stack_port, sizeof(out->stack_port));
	if (w != NULL) {
		out->wire_addr[0] = w->addr[0].v4.s_addr;
		out->wire_addr[1] = w->addr[1].v4.s_addr;
		out->wire_port[0] = w->port[0];
		out->wire_port[1] = w->port[1];
	}
	if (k != NULL) {
		out->stack_addr[0] = k->addr[0].v4.s_addr;
		out->stack_addr[1] = k->addr[1].v4.s_addr;
		out->stack_port[0] = k->port[0];
		out->stack_port[1] = k->port[1];
	}
	/*
	 * One key for both sides is how pf records an untranslated connection. Equal contents
	 * under two pointers is treated the same way, so a key pf chose to copy rather than share
	 * cannot be read as a reversed one.
	 */
	out->keys_shared = (w == k ||
	    (out->wire_addr[0] == out->stack_addr[0] && out->wire_addr[1] == out->stack_addr[1] &&
	    out->wire_port[0] == out->stack_port[0] && out->wire_port[1] == out->stack_port[1]));
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
octep_pf_state_read(const struct octep_pf_tuple *t, struct octep_pf_state *out)
{
	struct octep_pf_state plain;
	struct pf_state_key_cmp key;
	struct pf_kstate *s;
	const u_int dirs[2] = { PF_IN, PF_OUT };
	int d, order, have_plain = 0;

	if (!octep_pf_present())
		return (0);
	for (d = 0; d < 2; d++) {
		for (order = 0; order < octep_pf_combs(t); order++) {
			octep_pf_key(&key, t, order);
			s = octep_pf_find(&key, dirs[d], NULL);
			if (s == NULL)
				continue;
			octep_pf_copy(out, s);
			out->order = order;
			out->lookup_dir = (int)dirs[d];
			octep_pf_orient(out, t);
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
 * as it lasts. So a state that still reads MULTIPLE:SINGLE when it is OCTEP_PF_UDP_ONEWAY_MS old
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
octep_pf_settled(const struct octep_pf_tuple *t)
{
	struct pf_state_key_cmp key;
	struct pf_kstate *s;
	const u_int dirs[2] = { PF_IN, PF_OUT };
	uint64_t now;
	int d, order, n = 0, bad = 0;

	if (!octep_pf_present())
		return (0);
	if (t->proto != IPPROTO_TCP && t->proto != IPPROTO_UDP)
		return (1);
	now = pf_get_uptime();
	for (d = 0; d < 2; d++) {
		for (order = 0; order < octep_pf_combs(t); order++) {
			octep_pf_key(&key, t, order);
			s = octep_pf_find(&key, dirs[d], NULL);
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
			    now - s->creation < OCTEP_PF_UDP_ONEWAY_MS))) {
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
octep_pf_touch(const struct octep_pf_tuple *t)
{
	struct pf_state_key_cmp key;
	struct pf_kstate *s;
	const u_int dirs[2] = { PF_IN, PF_OUT };
	int d, order, n = 0;

	if (!octep_pf_present())
		return (0);
	for (d = 0; d < 2; d++) {
		for (order = 0; order < octep_pf_combs(t); order++) {
			octep_pf_key(&key, t, order);
			s = octep_pf_find(&key, dirs[d], NULL);
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
 * are marked, because a frame crosses both. The flag is left set for the life of the state: clearing
 * it when the flow is discarded would re-arm the very check that the returning frames fail.
 *
 * Returns how many states were marked, which the caller prints so a flow whose pf state could not
 * be found says so.
 */
int
octep_pf_mark_sloppy(const struct octep_pf_tuple *t)
{
	struct pf_state_key_cmp key;
	struct pf_kstate *s;
	const u_int dirs[2] = { PF_IN, PF_OUT };
	int d, order, n = 0;

	if (!octep_pf_present())
		return (0);
	for (d = 0; d < 2; d++) {
		for (order = 0; order < octep_pf_combs(t); order++) {
			octep_pf_key(&key, t, order);
			s = octep_pf_find(&key, dirs[d], NULL);
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
