/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 */

/*
 * The inspector's side on the XGS 136 (discovery 4), npuips.c: oxips0, where an inspector reads the
 * frames the fast path hands the coprocessor's inspector function, and the way back for the ones it
 * lets through.
 */
#ifndef _NPUIPS_H_
#define	_NPUIPS_H_

struct mbuf;

void		 npuips_attach(device_t dev);
void		 npuips_detach(void);
int		 npuips_listening(void);
struct mbuf	*npuips_rx(const uint8_t *src, int total);

#endif /* _NPUIPS_H_ */
