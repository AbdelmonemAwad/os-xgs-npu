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
	/*
	 * A TCP state's two peers as pf tracks them, [0] pf's src - the opener - [1] the responder,
	 * and whether pf rewrites sequence numbers or timestamps in every packet: the OCTEON TX
	 * driver's fields of the same names (octep.h), for the window the fast path checks (issue
	 * #679).
	 */
	uint32_t seqlo[2], seqhi[2];
	uint16_t max_win[2];
	uint8_t	 wscale[2];
	int	 rewrites;
};

/* What all of pf's states for a connection say about its window: octep.h's struct octep_pf_tcp. */
struct npuep_pf_tcp {
	int	states;
	int	rewrites;
	int	rule_sloppy;
	int	sloppy;
};

/*
 * A connection entry's TCP block, struct usfp_tcp_info - the same sixty bytes at the same offsets
 * on this family as on the OCTEON TX, which octep.h describes at OCTEP_CONN_OFF_TCP. Offset 24 of
 * a FLOW_CREATE_FP request.
 */
#define	NPUFLOW_CONN_OFF_TCP		24
#define	NPUFLOW_CONN_TCP_LEN		60
#define	NPUFLOW_TCP_SEQ_LEN		48
#define	NPUFLOW_TCP_SEEN(d)		((d) * 16)
#define	NPUFLOW_TCP_OFF_RETRANS		44
#define	NPUFLOW_TCP_OFF_FIN		45
#define	  NPUFLOW_TCP_FIN_STATE(b)	(((b) >> 5) & 0x7)
#define	NPUFLOW_TCP_OFF_FLAGS		56
#define	  NPUFLOW_TCP_SEQ_CHK_ON	0x00000001u
#define	  NPUFLOW_TCP_LIBERAL_ORIG	0x00000002u
#define	  NPUFLOW_TCP_LIBERAL_REPLY	0x00000004u
#define	  NPUFLOW_TCP_SCALE_REPLY_SHIFT	16
#define	  NPUFLOW_TCP_SCALE_ORIG_SHIFT	24

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
 * One TCP segment as the host saw it go by, the latest punted frame of a candidate's tuple: octep.h's
 * struct octep_tcp_seg. A connection's window, when pf's numbers for it have stopped, is made from
 * its two directions' latest segments (npuflow_tcp_from_segs; issue #679).
 */
struct npuep_tcp_seg {
	uint32_t	seq;
	uint32_t	ack;
	uint32_t	len;
	uint16_t	win;
	uint8_t		flags;
	uint8_t		valid;		/* 1 for a TCP segment whose header was read whole */
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
	struct npuep_tcp_seg	 seg;		/* the last of them's TCP header, for the window */
	/*
	 * When the connection was last made in hardware; and, when the fast path gave it back soon
	 * after, until when it stays with the host and how long that wait was (npuflow_letgo,
	 * npuflow_automake). Kept here, by the tuple, because a remake gives the connection a new table
	 * entry; cleared with the rest when the candidate ages out - ten seconds with nothing punted.
	 */
	int			 mk_tick;
	int			 hold_until;
	int			 hold_wait;
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
int	npuep_pf_count(const struct npuep_pf_tuple *tup, const uint64_t *pk, const uint64_t *by);
int	npuep_pf_tcp_scan(const struct npuep_pf_tuple *tup, struct npuep_pf_tcp *out);
int	npuep_pf_tos_scan(const struct npuep_pf_tuple *tup, uint16_t dscp[2]);
int	npuep_pf_handback(const struct npuep_pf_tuple *tup, const uint8_t *seq, const uint8_t *scale);

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
	    const struct npuep_pf_tuple *t, int closing, const struct npuep_tcp_seg *seg);
int	npuflow_tcp_closing(const uint8_t *ip, uint32_t len, int v6);
void	npuflow_tcp_seg(const uint8_t *ip, uint32_t len, struct npuep_tcp_seg *s);
void	npuflow_cmsg_rx(const uint8_t *f, uint32_t flen);
int	npuflow_attach(device_t dev, struct sysctl_ctx_list *ctx, struct sysctl_oid *parent);
void	npuflow_detach(void);
uint64_t npuflow_ipsec_sa_gone(uint32_t handle, uint8_t rev);
void	npuflow_ips_passed(uint32_t ci, uint32_t len);
void	npuflow_ips_unwatched(uint32_t ci);

#endif /* _NPUFLOW_H_ */
