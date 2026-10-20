/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The kernel's IPsec offload contract on the ARMADA front ports - the first slice: advertise the
 * capability and record what the kernel offers. See npuipsec.c.
 */
#ifndef _NPUIPSEC_H_
#define	_NPUIPSEC_H_

#include <sys/types.h>
#include <sys/mbuf.h>

struct sysctl_ctx_list;
struct sysctl_oid;
struct mbuf;

/* Advertise IFCAP2_IPSEC_OFFLOAD on one front port and install the method table. */
void	npuep_ipsec_if_attach(if_t ifp);
/* dev.npuep.0.ipsec: what the kernel has offered. */
void	npuep_ipsec_add_sysctls(struct sysctl_ctx_list *ctx, struct sysctl_oid *parent);
/*
 * The receive termination: a frame the engine decrypted in place on the way in, named by the sa word
 * its metadata carries (handle low, revision high). Consumes the mbuf. Called with no driver lock held.
 */
void	npuipsec_rx(if_t ifp, struct mbuf *m, uint32_t sa_word);
/*
 * The transmit side's other door, asked by the port's transmit before anything else and without the
 * GIU lock: a frame the kernel's own offload path tagged for an association becomes the ESP envelope
 * for the engine. Returns the frame to send, or NULL when it was consumed (dropped, never in the clear).
 */
struct mbuf *npuipsec_tx_prepare(if_t ifp, struct mbuf *m);
struct npuep_pf_tuple;
int	npuipsec_flow_covered(const struct npuep_pf_tuple *tup);
uint32_t npuipsec_spgen(void);
/* The tunnel a connection's encrypting direction leaves by, for a flow that carries the association. */
struct npuipsec_flow {
	int		enc_dir;	/* which of the connection's two tuples leaves encrypted */
	uint32_t	handle;		/* the outbound association on the engine: its SA_ADD index plus one */
	uint8_t		rev;		/* and its revision */
	int		port;		/* the front port the ESP frames leave by */
	uint32_t	far;		/* the tunnel's far end, network order: the encrypting direction's next hop */
};
int	npuipsec_flow_tunnel(const struct npuep_pf_tuple *tup, struct npuipsec_flow *fi);
int	npuipsec_flow_live(const struct npuipsec_flow *fi);

/*
 * The transmit side's request to the engine, carried on a frame the driver built itself (the ESP
 * envelope behind the tunnel's link header) from the output transform to the port's encap: the
 * fast path's own metadata asks for the encryption - md_valid, sa_is_out, the MTU, the handle -
 * written by npugiu_encap from this tag. The cookie is this module's; the type is the one use.
 */
#define	NPUIPSEC_MTAG_COOKIE	0x4e505549u	/* 'NPUI' */
#define	NPUIPSEC_MTAG_TX	1
struct npuipsec_tx_tag {
	struct m_tag	tag;
	uint32_t	handle;		/* the association's handle: its SA_ADD index plus one */
	uint16_t	mtu;		/* the path MTU the vendor's host writes beside it */
};

#endif /* _NPUIPSEC_H_ */
