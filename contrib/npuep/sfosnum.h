/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * Which vendor version left its software on the coprocessor, and what that changes for the host.
 *
 * This file is in contrib/npuep and in contrib/octep, the same in both - each driver is built from
 * its own directory alone, on the appliance - and checks.yml fails when the two copies differ.
 * docs/families/rpc-numbering.md has where every number below was read.
 *
 * A coprocessor runs the software the appliance's last vendor install put on it: the vendor's software writes its own
 * coprocessor root file system to the standby partition and boots it (xgs-npu-rootfs-update.sh,
 * xgs-npu-rootfs-set-boot.sh), and nothing on OPNsense changes it. So two boards of one model can
 * speak two numberings, and which one is the owner's history, not the board's. Three versions are
 * known, and a driver tells them apart at the channel's first command, from reads that change
 * nothing on the far side:
 *
 *	                     PLATFORM_READ   its block   the block's version field
 *	vendor version 21    at 36           228 bytes   a git hash - "9a31cacb7d1" on the XGS 136
 *	vendor version 22    at 36           232 bytes   "UNKNOWN_VERSION"
 *	vendor version 23    at 37           232 bytes   "UNKNOWN_VERSION"
 *
 * Vendor version 23 EAP 1 put a new command, SA_IF_MTU_SET, at 36 and moved PLATFORM_READ and every command
 * after it one higher; 0 to 35 are the same in all three. Vendor version 22 added num_mflows to the platform
 * block, at 224, where vendor version 21 has num_pfs. So where the block answers separates 23 from the other
 * two, and its length separates 21 from 22, with the version field agreeing; the interface table
 * then read back entry for entry, at the command the shift gives, is what confirms the shift.
 *
 * Not a fourth signal: the fast-path counter array. Its count differs - 176 named counters on vendor version
 * 21, 182 on 22, 184 on 23 - but the far side's array is 255 long whatever the build (on the XGS 136,
 * vendor version 21, every index up to 254 answers and 255 is refused, measured 2026-10-25), so the count is
 * not something a read can find. The version found here is what picks the counters' names.
 *
 * Every command number in both drivers is vendor version 22; the drivers send it through sfosnum_wire.
 * Until 2026-10-25 this said "vendor version 21 and 22's", and that was wrong from 46 on: the two agree
 * only below 46. Vendor version 22 put LO_WORKER_DF_CNT_READ at 46, where
 * vendor version 21 has the first of its counter clears, so vendor version 21 numbers every command from 46 on one lower
 * and has no DF_CNT command at all (read from the XGS 136's own usfp_firewall.ko, usfp_cntrs_clr,
 * 2026-10-25). Nothing either driver sent went past 45 before the clears.
 */

#ifndef _SFOSNUM_H_
#define	_SFOSNUM_H_

#define	SFOSNUM_FIRST_MOVABLE	36	/* the first command vendor version 23 moved */
#define	SFOSNUM_FIRST_NOT_21	46	/* the first command whose vendor version 21 number is another */
/* the drivers' numbers from 46 on, vendor version 22: the direct-forwarding read and the four clears */
#define	SFOSNUM_DF_CNT_READ	46
#define	SFOSNUM_DBG_CNT_CLR	47
#define	SFOSNUM_SYS_CNT_CLR	48
#define	SFOSNUM_PORT_CNT_CLR	49
#define	SFOSNUM_DF_CNT_CLR	50
#define	SFOSNUM_PLATFORM_READ	36	/* in the drivers' numbering */
#define	SFOSNUM_SHIFT_MAX	4	/* PLATFORM_READ looked for at 36 to 40 */
#define	SFOSNUM_SHIFT_UNKNOWN	(-1)

/*
 * struct platform_info, by version. The three 64-byte names and the fields up to capabilities are
 * where they are in all three; what follows them is not.
 */
#define	SFOSNUM_PLAT_OFF_NAME		0
#define	SFOSNUM_PLAT_OFF_VERSION	64
#define	SFOSNUM_PLAT_OFF_ASSEMBLY	128
#define	SFOSNUM_PLAT_NAME_LEN		64
#define	SFOSNUM_PLAT_LEN_21		228	/* ... capabilities at 220, num_pfs at 224, 3 reserved */
#define	SFOSNUM_PLAT_LEN_22		232	/* ... num_mflows at 224, num_pfs 228, num_vfs 229,
						   num_workers 230, 1 reserved */
#define	SFOSNUM_PLAT_OFF_NUM_PFS_21	224
#define	SFOSNUM_PLAT_OFF_NUM_MFLOWS	224	/* from vendor version 22 */
#define	SFOSNUM_PLAT_OFF_NUM_PFS	228	/* from vendor version 22 */
#define	SFOSNUM_PLAT_OFF_NUM_VFS	229	/* from vendor version 22 */
#define	SFOSNUM_PLAT_OFF_NUM_WORKERS	230	/* from vendor version 22 */

/*
 * The number a command goes on the wire as: the drivers' own, moved by the shift found for this
 * coprocessor from SFOSNUM_FIRST_MOVABLE on, and from SFOSNUM_FIRST_NOT_21 on by the version too -
 * one lower on vendor version 21, and nothing for the DF_CNT commands it does not have. -1 when a command
 * cannot be placed - from 36 on before the shift is known, from 46 on before the version is - and
 * must not be sent: under a numbering not understood it would name another command, and among
 * those are writes.
 */
static inline int
sfosnum_wire(int cmd, int shift, int sfos)
{
	if (cmd < SFOSNUM_FIRST_MOVABLE)
		return (cmd);
	if (shift < 0 || cmd + shift > 255)
		return (-1);
	if (cmd >= SFOSNUM_FIRST_NOT_21) {
		if (sfos == 21)
			return ((cmd == SFOSNUM_DF_CNT_READ || cmd == SFOSNUM_DF_CNT_CLR) ? -1 :
			    cmd - 1);
		if (sfos != 22 && sfos != 23)
			return (-1);
	}
	return (cmd + shift);
}

/*
 * Whether a platform block's version field is a build's own name - vendor version 21 writes a git hash there -
 * or the placeholder later versions write.
 */
static inline int
sfosnum_version_named(const char *ver)
{
	return (ver[0] != '\0' && strncmp(ver, "UNKNOWN_VERSION", 15) != 0);
}

/*
 * The vendor version a coprocessor's software comes from - 21, 22 or 23 - from where its platform block
 * answered (shift), how long the block is, and whether its version field names a build; 0 when the
 * three do not describe a version known here. A 0 is said, not acted on: the commands still go by
 * the shift the block was found at, and the interface table's read-back still checks it; what a 0
 * changes is that nothing version-specific - the counters' names - is assumed.
 */
static inline int
sfosnum_version(int shift, int plat_len, int ver_named)
{
	if (shift == 0 && plat_len == SFOSNUM_PLAT_LEN_21 && ver_named)
		return (21);
	if (shift == 0 && plat_len == SFOSNUM_PLAT_LEN_22 && !ver_named)
		return (22);
	if (shift == 1 && plat_len == SFOSNUM_PLAT_LEN_22 && !ver_named)
		return (23);
	return (0);
}

/*
 * SA_ADD's request, struct usfp_fpop_req_sa_add. Vendor version 23 put a word at 8, after the index and the
 * interface - its host fills it from the kernel's xfrm_state at 0xa8, read as the interface id of a
 * route-based tunnel and 0 for any other - and moved everything after it 8 bytes on: 200 bytes where
 * vendor version 21 and 22 send 192. Read from the hosts' own fw_fp_add_egress_sa, which zero and send 0xc0
 * bytes on vendor version 21 and 0xc8 on 23, and their populate_add_req, which store the window at 0x70 and
 * 0x78, the SPI at 0x74 and 0x7c, the sequence at 0x78 and 0x80, the revision at 0xb8 and 0xc0
 * (2026-10-25). The far side refuses a request shorter than its own: until then vendor version 23's answered
 * every association with INVALID_PARAMETER, and none was offloaded. The other IPsec commands kept
 * their sizes. How far the fields from the key on move for a version, -1 for one not known, which
 * must not be sent an association: a request laid out for another version is refused, or is read
 * with every field in the wrong place.
 */
#define	SFOSNUM_SA_ADD_LEN	192	/* vendor version 21 and 22's */
#define	SFOSNUM_SA_ADD_IF_OFF	8	/* vendor version 23's word, then four bytes to align the rest */

static inline int
sfosnum_sa_add_shift(int sfos)
{
	switch (sfos) {
	case 21:
	case 22:
		return (0);
	case 23:
		return (8);
	}
	return (-1);
}

/* The named fast-path counters a version's LO_WORKER_SYS_CNT_READ array begins with. */
static inline int
sfosnum_fpcntr_named(int sfos)
{
	switch (sfos) {
	case 21:
		return (176);
	case 22:
		return (182);
	case 23:
		return (184);
	}
	return (0);
}

#endif /* !_SFOSNUM_H_ */
