/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The flow side of the ARMADA driver: what a punted frame says about its flow, what pf holds for
 * it, where its next hop is, and the table the driver keeps of the flows it has handed to the fast
 * path. The records are the OCTEON TX driver's, renamed: the two families speak one control
 * protocol byte for byte (docs/npuep/rpc-payloads.md), so the host's side of a flow is the same
 * thing on both.
 */
#ifndef _NPUFLOW_H_
#define	_NPUFLOW_H_

#include <sys/types.h>

/* A connection as the frame names it. Network order throughout, as pf keeps it. */
struct npuep_pf_tuple {
	uint32_t	sip;
	uint32_t	dip;
	uint16_t	sport;
	uint16_t	dport;
	uint8_t		af;		/* AF_INET, or AF_INET6 since 2026-10-18 */
	uint8_t		proto;
	/*
	 * An IPv6 connection's two addresses, network order, zero for IPv4. sip and dip then hold
	 * the four words of each folded together - what the hash and the quick comparisons use -
	 * and npuflow_tuple_same compares these in full, so a fold that happens to agree is never
	 * taken for the same connection. The OCTEON TX driver's tuple, field for field.
	 */
	uint32_t	sip6[4];
	uint32_t	dip6[4];
};

/* The fold that stands for an IPv6 address in the 32-bit fields. */
static __inline uint32_t
npuflow_fold6(const uint32_t w[4])
{

	return (w[0] ^ w[1] ^ w[2] ^ w[3]);
}

/* What pf holds for a tuple, copied out under its lock - see npupf.c. */
struct npuep_pf_state {
	int	order;			/* which of the four key arrangements matched */
	uint8_t	direction;
	uint8_t	timeout;
	uint8_t	src_state;
	uint8_t	dst_state;
	uint16_t state_flags;
	char	ifname[16];
	uint32_t wire_addr[2];
	uint32_t stack_addr[2];
	uint16_t wire_port[2];
	uint16_t stack_port[2];
	int	 nat_valid;		/* at least one end is translated */
	int	 nat_snat;		/* the opener's address is rewritten: do_snat */
	int	 nat_dnat;		/* the responder's address is rewritten: do_dnat */
	int	 original;		/* the frame looked up goes from the opener to the responder */
	int	 lookup_dir;		/* PF_IN or PF_OUT: which of pf's two lists held the state */
	int	 keys_shared;		/* pf kept one key for both sides: nothing is translated */
	uint32_t orig_src, orig_dst;	/* network order */
	uint32_t nat_src, nat_dst;
	uint16_t orig_sport, orig_dport;
	uint16_t nat_sport, nat_dport;
	/* An IPv6 state: the family, both keys in full, and the two ends in full. */
	uint8_t	 af;
	uint32_t wire_addr6[2][4];
	uint32_t stack_addr6[2][4];
	uint32_t orig_src6[4], orig_dst6[4];
};

/* A resolved way out: the neighbour, our address there, the front port and its MTU. */
struct npuep_nhop {
	uint8_t	 dmac[6];
	uint8_t	 smac[6];
	uint8_t	 iface;			/* the front port's LIF interface id */
	uint16_t tag;			/* the front port's tag, as the frame header carries it */
	uint16_t mtu;
	int	 port;			/* index into npuep_front_ports */
	int	 from_hint;		/* the port came from the flow, not the route */
};

/*
 * A candidate: a flow the fast path has made an entry for and punted a frame of, with the
 * identity that frame carried. The fast path names the entry; the host can only load it.
 */
struct npuep_flow_cand {
	struct npuep_pf_tuple	 tuple;
	uint32_t		 id;		/* the microflow's table index, from the metadata */
	uint32_t		 rev;
	uint16_t		 tag;		/* the port the frame arrived on */
	int			 port;		/* index into npuep_front_ports, or -1 */
	int			 when;		/* ticks at the last frame */
	uint32_t		 frames;	/* punted frames seen for it */
	uint8_t			 closing;	/* the last of them carried a TCP FIN or RST */
	/* What the sweep found out, for the page; nothing is sent in this slice. */
	uint8_t			 pf_seen;
	uint8_t			 pf_settled;
	uint8_t			 nh_ok;
	int			 nh_err;
	struct npuep_nhop	 nh;
	struct npuep_pf_state	 st;
};

/*
 * The candidates, one slot per hash of the tuple: a frame's own slot is found without a search, and
 * a newer flow that hashes to an occupied slot takes it. Thirty-two searched in order were the
 * table until 2026-10-18, which a thousand connections at once could not get through; then 1,024
 * slots, which a thousand connections - two thousand directions - filled twice over: measured the
 * same day, 176,283 of 184,866 candidates pushed another out, and 609 of 1,024 connections were in
 * hardware twenty seconds into the load. Sixteen times that now, the OCTEON TX driver's 16,384
 * (4,096 slots of four ways there); the index stays sixteen bits of the hash.
 */
#define	NPUFLOW_CAND_MAX	16384

/* npupf.c */
bool	npuep_pf_present(void);
void	npuep_pf_retry(void);
int	npuep_pf_state_exists(const struct npuep_pf_tuple *t, int dir, int *order);
int	npuep_pf_state_read(const struct npuep_pf_tuple *t, struct npuep_pf_state *out);
int	npuep_pf_settled(const struct npuep_pf_tuple *t);
int	npuep_pf_touch(const struct npuep_pf_tuple *t);
int	npuep_pf_mark_sloppy(const struct npuep_pf_tuple *t);

/* npunhop.c */
int	npuep_nhop_resolve(uint32_t dst, int hint_port, struct npuep_nhop *out);
int	npuep_nhop_resolve6(const uint32_t *dst6, int hint_port, struct npuep_nhop *out);

/* npugiu.c: the ports, for the next-hop code and the candidate table. */
int	npugiu_port_count(void);
void	*npugiu_port_ifp(int port);
int	npugiu_port_link(int port);
uint8_t	npugiu_port_iface(int port);
uint16_t npugiu_port_tag(int port);

/* npuflow.c */
int	npuflow_tuple_from_ip(const uint8_t *ip, uint32_t len, struct npuep_pf_tuple *t);
int	npuflow_tuple_from_ip6(const uint8_t *ip, uint32_t len, struct npuep_pf_tuple *t);
void	npuflow_board(uint32_t max_conn, uint32_t max_nhop);
void	npuflow_cand_push(int port, uint16_t tag, uint32_t id, uint32_t rev,
	    const struct npuep_pf_tuple *t, int closing);
int	npuflow_tcp_closing(const uint8_t *ip, uint32_t len, int v6);
int	npuflow_attach(device_t dev, struct sysctl_ctx_list *ctx, struct sysctl_oid *parent);
void	npuflow_detach(void);

#endif /* _NPUFLOW_H_ */
