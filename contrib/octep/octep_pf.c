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
octep_pf_state_exists(const struct octep_pf_tuple *t, int *order)
{
	struct pf_state_key_cmp key;
	int i;

	if (!octep_pf_present())
		return (0);

	for (i = 0; i < octep_pf_combs(t); i++) {
		octep_pf_key(&key, t, i);
		if (octep_pf_exists(&key, PF_IN)) {
			if (order != NULL)
				*order = i;
			return (1);
		}
	}

	return (0);
}

/*
 * Turn pf's two keys into struct usfp_nat_info, in the connection's orientation.
 *
 * pf keeps a wire key and a stack key, and for a translated connection exactly one of their two
 * ends differs: that end is the machine whose address is being rewritten, wire-side translated and
 * stack-side original. The other end is the same in both and needs no translation, so it goes into
 * both the orig and the nat halves - which is what the fast path expects, not a zero.
 *
 * Which of the two the fast path should rewrite is decided by comparing the differing end against
 * the FRAME in hand. If the translated end is where the frame is going, the frame is a reply on a
 * connection whose source was translated on its way out, and the connection is do_snat. If it is
 * where the frame came from, the connection is do_dnat. Reading it off the frame rather than
 * asserting it is deliberate: this is the field that was wrong for four days, and the direction of
 * the frame being looked at is exactly what makes it easy to get backwards.
 */
static void
octep_pf_nat(struct octep_pf_state *out, const struct octep_pf_tuple *t)
{
	int xl;		/* the end that is translated: 0 or 1 */
	int sm;		/* the end that is not */

	out->nat_valid = 0;
	out->nat_snat = 0;

	if (out->wire_addr[0] != out->stack_addr[0] || out->wire_port[0] != out->stack_port[0])
		xl = 0;
	else if (out->wire_addr[1] != out->stack_addr[1] || out->wire_port[1] != out->stack_port[1])
		xl = 1;
	else
		return;		/* the keys agree: nothing is translated */
	sm = xl ^ 1;

	/*
	 * The translated end is the connection's source in the fast path's terms: ipv4_nat_src is
	 * what it looks like on the wire and ipv4_orig_src is what it is behind the firewall. The
	 * untranslated end is the other party and is copied into both halves.
	 */
	out->orig_src = out->stack_addr[xl];
	out->orig_sport = out->stack_port[xl];
	out->nat_src = out->wire_addr[xl];
	out->nat_sport = out->wire_port[xl];

	out->orig_dst = out->stack_addr[sm];
	out->orig_dport = out->stack_port[sm];
	out->nat_dst = out->wire_addr[sm];
	out->nat_dport = out->wire_port[sm];

	/*
	 * The frame decides which flag. Its destination carrying the translated address means the
	 * frame is heading towards the end that gets rewritten - a reply on a source-translated
	 * connection.
	 */
	out->nat_snat = (t->dip == out->wire_addr[xl] && t->dport == out->wire_port[xl]);
	out->nat_valid = 1;
}

/*
 * The same question, answered from the state itself rather than from its existence.
 *
 * pf_find_state_all returns with PF_STATE_LOCK(s) HELD when more is NULL - it takes the hashrow
 * lock, finds the key, takes the state lock and drops the hashrow lock before returning - so every
 * path out of here unlocks. What is copied out is only what a flow needs, taken under the lock,
 * because the state may be freed the moment it is released.
 */
int
octep_pf_state_read(const struct octep_pf_tuple *t, struct octep_pf_state *out)
{
	struct pf_state_key_cmp key;
	struct pf_kstate *s;
	int order;

	if (!octep_pf_present())
		return (0);

	for (order = 0; order < octep_pf_combs(t); order++) {
		octep_pf_key(&key, t, order);
		s = octep_pf_find(&key, PF_IN, NULL);
		if (s == NULL)
			continue;

		out->order = order;
		out->direction = s->direction;
		out->timeout = s->timeout;
		out->state_flags = s->state_flags;
		out->src_state = s->src.state;
		out->dst_state = s->dst.state;
		strlcpy(out->ifname, s->kif != NULL ? s->kif->pfik_name : "",
		    sizeof(out->ifname));

		/*
		 * Both keys, copied under the state lock because they are pointers into memory the
		 * state owns. A state always has both; they are only ever NULL while it is being
		 * taken apart, and a lookup does not return one of those.
		 */
		bzero(out->wire_addr, sizeof(out->wire_addr));
		bzero(out->stack_addr, sizeof(out->stack_addr));
		bzero(out->wire_port, sizeof(out->wire_port));
		bzero(out->stack_port, sizeof(out->stack_port));
		if (s->key[PF_SK_WIRE] != NULL) {
			out->wire_addr[0] = s->key[PF_SK_WIRE]->addr[0].v4.s_addr;
			out->wire_addr[1] = s->key[PF_SK_WIRE]->addr[1].v4.s_addr;
			out->wire_port[0] = s->key[PF_SK_WIRE]->port[0];
			out->wire_port[1] = s->key[PF_SK_WIRE]->port[1];
		}
		if (s->key[PF_SK_STACK] != NULL) {
			out->stack_addr[0] = s->key[PF_SK_STACK]->addr[0].v4.s_addr;
			out->stack_addr[1] = s->key[PF_SK_STACK]->addr[1].v4.s_addr;
			out->stack_port[0] = s->key[PF_SK_STACK]->port[0];
			out->stack_port[1] = s->key[PF_SK_STACK]->port[1];
		}

		PF_STATE_UNLOCK(s);
		octep_pf_nat(out, t);
		return (1);
	}

	return (0);
}
