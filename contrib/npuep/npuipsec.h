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

struct sysctl_ctx_list;
struct sysctl_oid;

/* Advertise IFCAP2_IPSEC_OFFLOAD on one front port and install the method table. */
void	npuep_ipsec_if_attach(if_t ifp);
/* dev.npuep.0.ipsec: what the kernel has offered. */
void	npuep_ipsec_add_sysctls(struct sysctl_ctx_list *ctx, struct sysctl_oid *parent);

#endif /* _NPUIPSEC_H_ */
