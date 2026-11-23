#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""contrib/fchemmc, the driver of the XGS 87's eMMC controller (AMD FCH 1022:7906, #1205), away from
any appliance, and how os-xgs-npu builds, loads and removes it.

The whole of fchemmc.c is compiled with stand-ins for the kernel around it - its includes taken out,
every warning fatal - and run: the probe takes 1022:7906 alone, at BUS_PROBE_DEFAULT, and nothing on a
kernel it was not built for or with hw.fchemmc.disable set; CMD19 never reaches the controller; CMD14 is
answered from EXT_CSD read again over 4 bits and compared with the copy read over 1 bit, and the card is
always switched back to 1 bit; that copy is taken from mmc(4)'s own 1-bit read and from no other;
Transfer Mode is written 0 before a command without data, only where the chipset's SMBus asks for it;
a full reset goes through D3hot and back to D0 before the generic one, the configuration is restored only
once D0 reads back, and no other reset does; and the
slot offers 4 bits and 50 MHz at most, with no DDR52, HS200 or HS400. Then the source itself: the
copyright notices kept from sdhci_pci.c, the tunable, the module's declarations, and the bytes compared
against the ones Linux compares.

And the wiring: install.sh lays the sources down only where a 1022:7906 controller is and takes
everything of it away anywhere else; uninstall.sh takes it all away; and the kernel follower, run with
stand-ins for make, the fetcher, logger and sysctl, builds fchemmc.ko after the module it follows and
against the same sources, stamps it with the kernel's version line, gives make that line, writes the
loader line only beside a module, says a missing source or a failed build once, and does nothing at all
where the sources are not laid down.

What is not had here: a build against FreeBSD's headers (that is done on an appliance with the kernel's
sources) and any run on an XGS 87.

    python3 tools/test-fchemmc.py
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRIVER = os.path.join(ROOT, 'contrib', 'fchemmc', 'fchemmc.c')
MAKEFILE = os.path.join(ROOT, 'contrib', 'fchemmc', 'Makefile')
INSTALL = os.path.join(ROOT, 'install', 'install.sh')
UNINSTALL = os.path.join(ROOT, 'install', 'uninstall.sh')
FOLLOW = os.path.join(ROOT, 'install', 'kernel-follow.sh')
CC = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
SH = '/bin/sh' if os.path.exists('/bin/sh') else None

KERNEL = 'FreeBSD 15.1-RELEASE-p6 stable/26.7-n284176-c962f76493b6 SMP'
NEWER = 'FreeBSD 15.1-RELEASE-p7 stable/26.7-n284300-2b4c6d8e0f1a SMP'

# Linux's mmc_compare_ext_csds(), by EXT_CSD byte.
LINUX_COMPARED = {160, 181, 192, 194, 196, 217, 221, 223, 224, 229, 230, 231, 232, 212, 213, 214, 215}
# The names of mmcreg.h the comparison uses, with FreeBSD 15.1's values.
MMCREG = {'EXT_CSD_PART_SUPPORT': 160, 'EXT_CSD_REV': 192, 'EXT_CSD_CARD_TYPE': 196, 'EXT_CSD_SEC_CNT': 212,
          'EXT_CSD_HC_WP_GRP_SIZE': 221, 'EXT_CSD_ERASE_TO_MULT': 223, 'EXT_CSD_ERASE_GRP_SIZE': 224,
          'EXT_CSD_SEC_FEATURE_SUPPORT': 231}

with open(DRIVER, encoding='utf-8') as f:
    SOURCE = f.read()

# The kernel around fchemmc.c, as far as it reaches. Values are FreeBSD 15.1's (sys/dev/mmc/bridge.h,
# mmcreg.h, sys/dev/sdhci/sdhci.h, sys/dev/pci/pcireg.h); every stand-in that does something writes it
# to EV, the account each test reads.
STANDINS = r'''
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int u_int;
typedef unsigned char u_char;
typedef unsigned long bus_size_t;
struct fdev { void *softc; uint32_t devid; uint8_t revid; int has_pm; };
typedef struct fdev *device_t;
struct resource { int rid; };
struct sysctl_ctx_list { int unused; };
struct sysctl_oid_list { int unused; };
typedef struct { const char *name; void *func; } device_method_t;
typedef struct { const char *name; device_method_t *methods; size_t size; } driver_t;

static char EV[16384];
static void ev(const char *fmt, ...)
{
	va_list ap;
	size_t n = strlen(EV);

	va_start(ap, fmt);
	vsnprintf(EV + n, sizeof(EV) - n, fmt, ap);
	va_end(ap);
	n = strlen(EV);
	if (n + 1 < sizeof(EV)) { EV[n] = ';'; EV[n + 1] = '\0'; }
}

#define	__unused		__attribute__((unused))
#define	nitems(x)		(sizeof(x) / sizeof((x)[0]))
#define	ENOENT			2
#define	ENXIO			6
#define	ENOMEM			12
#define	EINVAL			22
#define	BUS_PROBE_DEFAULT	(-20)
#define	BUS_PROBE_GENERIC	(-100)
#define	M_DEVBUF		0
#define	M_WAITOK		1
#define	M_ZERO			2
#define	malloc(s, t, f)		calloc(1, (s))
#define	free(p, t)		(free)(p)
#define	OID_AUTO		(-1)
#define	CTLFLAG_RD		1
#define	CTLFLAG_RDTUN		2
#define	CTLFLAG_MPSAFE		4
#define	SYSCTL_NODE(p, n, name, ...)	int __attribute__((unused)) sysctl_node_##name
#define	SYSCTL_INT(p, n, name, ...)	extern int sysctl_int_##name
#define	SYSCTL_ADD_INT(ctx, tree, ...)	((void)(ctx), (void)(tree))
#define	SYSCTL_CHILDREN(x)		((struct sysctl_oid_list *)(x))
#define	DEVMETHOD(name, f)	{ #name, (void *)(f) }
#define	DEVMETHOD_END		{ NULL, NULL }
#define	DRIVER_MODULE(n, b, d, x, y)	extern int driver_module_##n##_##b
#define	MODULE_VERSION(n, v)	extern int module_version_##n
#define	SDHCI_DEPEND(n)		extern int sdhci_depend_##n
#define	MMC_DECLARE_BRIDGE(n)	extern int mmc_bridge_##n
#define	BUS_SPACE_BARRIER_READ	1
#define	BUS_SPACE_BARRIER_WRITE	2
#define	SYS_RES_IRQ		1
#define	SYS_RES_MEMORY		3
#define	RF_ACTIVE		2
#define	RF_SHAREABLE		4
#define	INTR_TYPE_MISC		16
#define	INTR_MPSAFE		512
#define	PCIR_BAR(x)		(0x10 + (x) * 4)
#define	PCIY_PMG		0x01
#define	PCIR_POWER_STATUS	0x4
#define	PCIM_PSTAT_DMASK	0x0003
#define	PCIM_PSTAT_D0		0x0000
#define	PCIM_PSTAT_D3		0x0003
#define	PCIM_PSTAT_NOSOFTRESET	0x0008

/* mmc(4) */
#define	MMC_CAP_4_BIT_DATA	(1 <<  0)
#define	MMC_CAP_8_BIT_DATA	(1 <<  1)
#define	MMC_CAP_HSPEED		(1 <<  2)
#define	MMC_CAP_UHS_SDR12	(1 <<  6)
#define	MMC_CAP_UHS_SDR25	(1 <<  7)
#define	MMC_CAP_UHS_SDR50	(1 <<  8)
#define	MMC_CAP_UHS_SDR104	(1 <<  9)
#define	MMC_CAP_UHS_DDR50	(1 << 10)
#define	MMC_CAP_MMC_DDR52	((1 << 11) | (1 << 12))
#define	MMC_CAP_MMC_HS200	((1 << 13) | (1 << 14))
#define	MMC_CAP_MMC_HS400	((1 << 15) | (1 << 16))
#define	MMC_CAP_MMC_ENH_STROBE	(1 << 17)
#define	MMC_CAP_SIGNALING_120	(1 << 18)
#define	MMC_CAP_SIGNALING_180	(1 << 19)
#define	MMC_CAP_SIGNALING_330	(1 << 20)
enum mmc_bus_width { bus_width_unknown = 0, bus_width_1 = 1, bus_width_4 = 2, bus_width_8 = 3 };
#define	MMC_ERR_NONE		0
#define	MMC_ERR_TIMEOUT		1
#define	MMC_ERR_FAILED		4
#define	MMC_EXTCSD_SIZE		512
#define	MMC_DATA_WRITE		(1UL << 0)
#define	MMC_DATA_READ		(1UL << 1)
#define	MMC_GO_IDLE_STATE	0
#define	MMC_SWITCH_FUNC		6
#define	MMC_SELECT_CARD		7
#define	MMC_SEND_EXT_CSD	8
#define	MMC_SEND_STATUS		13
#define	MMC_BUSTEST_R		14
#define	MMC_BUSTEST_W		19
#define	EXT_CSD_PART_SUPPORT	160
#define	EXT_CSD_BUS_WIDTH	183
#define	EXT_CSD_REV		192
#define	EXT_CSD_CARD_TYPE	196
#define	EXT_CSD_SEC_CNT		212
#define	EXT_CSD_HC_WP_GRP_SIZE	221
#define	EXT_CSD_ERASE_TO_MULT	223
#define	EXT_CSD_ERASE_GRP_SIZE	224
#define	EXT_CSD_SEC_FEATURE_SUPPORT 231
#define	EXT_CSD_GEN_CMD6_TIME	248
#define	EXT_CSD_CMD_SET_NORMAL	1
#define	EXT_CSD_BUS_WIDTH_1	0
#define	EXT_CSD_BUS_WIDTH_4	1
#define	EXT_CSD_BUS_WIDTH_8	2
struct mmc_ios { enum mmc_bus_width bus_width; };
struct mmc_host { uint32_t f_max; uint32_t caps; struct mmc_ios ios; };
struct mmc_data { size_t len; void *data; uint32_t flags; };
struct mmc_command { uint32_t opcode; uint32_t arg; uint32_t flags; int error; struct mmc_data *data; };
struct mmc_request { struct mmc_command *cmd; void (*done)(struct mmc_request *); void *done_data;
	uint32_t flags; };

/* sdhci */
#define	SDHCI_TRANSFER_MODE	0x0C
#define	SDHCI_COMMAND_FLAGS	0x0E
#define	SDHCI_CMD_DATA		0x20
#define	SDHCI_PRESENT_STATE	0x24
#define	SDHCI_CARD_STABLE	0x00020000
#define	SDHCI_RESET_ALL		0x01
#define	SDHCI_RESET_CMD		0x02
#define	SDHCI_RESET_DATA	0x04
struct sdhci_slot { u_int quirks; struct mmc_host host; };
u_int sdhci_quirk_set = 0;

/* The kernel's version line, as a test sets it. */
char version[256];
#define	FCHEMMC_KERNEL	"@KERNEL@"

/* The bus: a controller and the SMBus pci_find_device() finds. */
static struct fdev SMBUS;
static uint16_t SMBUS_ID;		/* 0: none */
static int STABLE_AFTER;		/* polls of Present State before card detect is stable */
static int UNSTABLE;			/* never stable */
static int POLLS;
static int PM_STATE;
static int PM_STUCK;			/* the power state stays D3 */
static int NO_SOFT_RESET;

static void *device_get_softc(device_t dev) { return dev->softc; }
static void device_set_desc(device_t dev, const char *d) { (void)dev; ev("desc %s", d); }
#define	device_printf(dev, ...)	((void)(dev), ev("printf"), printf(__VA_ARGS__))
static void *device_get_sysctl_ctx(device_t dev) { (void)dev; return NULL; }
static void *device_get_sysctl_tree(device_t dev) { (void)dev; return NULL; }
static uint32_t pci_get_devid(device_t dev) { return dev->devid; }
static uint8_t pci_get_revid(device_t dev) { return dev->revid; }
static device_t pci_find_device(uint16_t vendor, uint16_t device)
{
	return (vendor == 0x1022 && SMBUS_ID != 0 && device == SMBUS_ID) ? &SMBUS : NULL;
}
static int pci_find_cap(device_t dev, int cap, int *reg)
{
	if (!dev->has_pm || cap != PCIY_PMG)
		return (ENOENT);
	*reg = 0x50;
	return (0);
}
static uint32_t pci_read_config(device_t dev, int reg, int width)
{
	(void)dev; (void)width;
	if (reg == 0x40)
		return 0x00;			/* PCI_SLOT_INFO: one slot, BAR 0 */
	if (reg == 0x50 + PCIR_POWER_STATUS)
		return 0x0100 | (NO_SOFT_RESET ? 0x0008 : 0) | PM_STATE;	/* PME_En, No_Soft_Reset, state */
	return 0;
}
static void pci_write_config(device_t dev, int reg, uint32_t val, int width)
{
	(void)dev; (void)width;
	if (reg == 0x50 + PCIR_POWER_STATUS) {
		if (!PM_STUCK || (val & 3) == 3)
			PM_STATE = val & 3;
		ev("pm %u%s", val & 3, (val & ~3u) == (0x0100u | (NO_SOFT_RESET ? 0x0008u : 0)) ?
		    "" : " (other bits changed)");
	} else
		ev("cfg %x=%x", reg, val);
}
static void pci_save_state(device_t dev) { (void)dev; ev("save"); }
static void pci_restore_state(device_t dev) { (void)dev; ev("restore D%d", PM_STATE); }
static int pci_disable_busmaster(device_t dev) { (void)dev; ev("busmaster off"); return 0; }
static int pci_enable_busmaster(device_t dev) { (void)dev; ev("busmaster on"); return 0; }
static int pci_alloc_msi(device_t dev, int *n) { (void)dev; (void)n; return 0; }
static int pci_release_msi(device_t dev) { (void)dev; return 0; }
static struct resource RES;
static struct resource *bus_alloc_resource_any(device_t dev, int type, int *rid, u_int flags)
{
	(void)dev; (void)type; (void)flags;
	RES.rid = *rid;
	return &RES;
}
static int bus_release_resource(device_t dev, int type, int rid, struct resource *r)
{ (void)dev; (void)type; (void)rid; (void)r; return 0; }
static int rman_get_rid(struct resource *r) { return r->rid; }
static int bus_setup_intr(device_t dev, struct resource *r, int flags, void *filter, void (*f)(void *),
    void *arg, void **cookie)
{ (void)dev; (void)r; (void)flags; (void)filter; (void)f; (void)arg; *cookie = &RES; return 0; }
static int bus_teardown_intr(device_t dev, struct resource *r, void *cookie)
{ (void)dev; (void)r; (void)cookie; return 0; }
static int bus_generic_suspend(device_t dev) { (void)dev; return 0; }
static int bus_generic_resume(device_t dev) { (void)dev; return 0; }
static void bus_barrier(struct resource *r, int o, int l, int f) { (void)r; (void)o; (void)l; (void)f; }
static uint8_t bus_read_1(struct resource *r, bus_size_t off) { (void)r; (void)off; return 0; }
static uint16_t bus_read_2(struct resource *r, bus_size_t off) { (void)r; (void)off; return 0; }
static uint32_t bus_read_4(struct resource *r, bus_size_t off)
{
	(void)r;
	if (off == SDHCI_PRESENT_STATE) {
		POLLS++;
		return (!UNSTABLE && POLLS > STABLE_AFTER) ? SDHCI_CARD_STABLE : 0;
	}
	return 0;
}
static void bus_write_1(struct resource *r, bus_size_t off, uint8_t v) { (void)r; ev("W1 %02lx=%02x", off, v); }
static void bus_write_2(struct resource *r, bus_size_t off, uint16_t v) { (void)r; ev("W2 %02lx=%04x", off, v); }
static void bus_write_4(struct resource *r, bus_size_t off, uint32_t v) { (void)r; ev("W4 %02lx=%08x", off, v); }
static void bus_read_multi_stream_4(struct resource *r, bus_size_t o, uint32_t *d, bus_size_t c)
{ (void)r; (void)o; (void)d; (void)c; }
static void bus_write_multi_stream_4(struct resource *r, bus_size_t o, uint32_t *d, bus_size_t c)
{ (void)r; (void)o; (void)d; (void)c; }
static long DELAYED;
static void DELAY(int us) { DELAYED += us; }

/* sdhci's core. */
static void sdhci_generic_reset(device_t dev, struct sdhci_slot *slot, uint8_t mask)
{ (void)dev; (void)slot; ev("generic reset %02x", mask); }
static int sdhci_init_slot(device_t dev, struct sdhci_slot *slot, int num)
{
	(void)dev; (void)num;
	/* What sdhci_init_slot() would offer from an 8-bit, high speed, 200 MHz controller and more. */
	slot->host.caps = MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA | MMC_CAP_HSPEED | MMC_CAP_MMC_DDR52 |
	    MMC_CAP_MMC_HS200 | MMC_CAP_MMC_HS400 | MMC_CAP_UHS_SDR50 | MMC_CAP_SIGNALING_180 |
	    MMC_CAP_SIGNALING_330;
	slot->host.f_max = 200000000;
	ev("init slot");
	return 0;
}
static void sdhci_start_slot(struct sdhci_slot *slot)
{ ev("start slot caps %x f_max %u", slot->host.caps, slot->host.f_max); }
static int sdhci_cleanup_slot(struct sdhci_slot *slot) { (void)slot; return 0; }
static int sdhci_generic_suspend(struct sdhci_slot *slot) { (void)slot; return 0; }
static int sdhci_generic_resume(struct sdhci_slot *slot) { (void)slot; return 0; }
static void sdhci_generic_intr(struct sdhci_slot *slot) { (void)slot; }
static void sdhci_generic_read_ivar(void) {}
static void sdhci_generic_write_ivar(void) {}
static void sdhci_generic_update_ios(void) {}
static void sdhci_generic_switch_vccq(void) {}
static void sdhci_generic_tune(void) {}
static void sdhci_generic_retune(void) {}
static void sdhci_generic_get_ro(void) {}
static void sdhci_generic_acquire_host(void) {}
static void sdhci_generic_release_host(void) {}
static void sdhci_generic_set_uhs_timing(void) {}

/* The card, behind the controller and behind mmc(4). */
static uint8_t CARD_EXT_CSD[MMC_EXTCSD_SIZE];
static int CARD_WIDTH;			/* EXT_CSD_BUS_WIDTH_* the card is switched to */
static int CARD_CHANGED_AT_4;		/* a byte at which EXT_CSD reads changed over 4 bits, or -1 */
static int CARD_REFUSES_SWITCH;
static int CARD_SWITCH_ANSWER_LOST;
static int CARD_READ_FAILS;
static int CONTROLLER_ERR;		/* what sdhci_generic_request() answers */
static int REQUEST_ERROR;		/* the error a request sent to the controller completes with */

static int sdhci_generic_request(device_t brdev, device_t reqdev, struct mmc_request *req)
{
	(void)brdev; (void)reqdev;
	ev("controller CMD%u", req->cmd->opcode);
	if (CONTROLLER_ERR)
		return (CONTROLLER_ERR);
	if (req->cmd->opcode == MMC_SEND_EXT_CSD && req->cmd->data != NULL)
		memcpy(req->cmd->data->data, CARD_EXT_CSD, MMC_EXTCSD_SIZE);
	req->cmd->error = REQUEST_ERROR;
	req->done(req);
	return (0);
}
static int mmc_switch(device_t busdev, device_t dev, uint16_t rca, uint8_t set, uint8_t index,
    uint8_t value, u_int timeout, bool status)
{
	(void)busdev; (void)dev;
	ev("switch %u=%u rca %u set %u timeout %u%s", index, value, rca, set, timeout, status ? " status" : "");
	if (CARD_REFUSES_SWITCH)
		return (MMC_ERR_FAILED);
	if (index == EXT_CSD_BUS_WIDTH)
		CARD_WIDTH = value;
	if (CARD_SWITCH_ANSWER_LOST && value == EXT_CSD_BUS_WIDTH_4)
		return (MMC_ERR_TIMEOUT);	/* taken, but its status poll timed out */
	return (MMC_ERR_NONE);
}
static int mmc_send_ext_csd(device_t busdev, device_t dev, uint8_t *buf)
{
	(void)busdev; (void)dev;
	ev("read EXT_CSD at card width %d", CARD_WIDTH);
	memset(buf, 0, MMC_EXTCSD_SIZE);
	if (CARD_READ_FAILS)
		return (MMC_ERR_TIMEOUT);
	memcpy(buf, CARD_EXT_CSD, MMC_EXTCSD_SIZE);
	if (CARD_WIDTH == EXT_CSD_BUS_WIDTH_4 && CARD_CHANGED_AT_4 >= 0)
		buf[CARD_CHANGED_AT_4] ^= 0x40;
	return (MMC_ERR_NONE);
}
'''

HARNESS = r'''
/* ------------------------------------------------------------------ the tests */
static struct fdev CTRL;
static struct fchemmc_softc SC;
static int FAILURES;
static int DONE_CALLS;
static void *DONE_DATA_SEEN;
static void done(struct mmc_request *req) { DONE_CALLS++; DONE_DATA_SEEN = req->done_data; }

#define	CHECK(cond, ...) do { if (!(cond)) { FAILURES++; printf("FAIL %s:%d: ", __func__, __LINE__); \
	printf(__VA_ARGS__); printf("\n  events: %s\n", EV); } } while (0)

static void fresh(void)
{
	EV[0] = '\0';
	memset(&SC, 0, sizeof(SC));
	memset(&CTRL, 0, sizeof(CTRL));
	CTRL.softc = &SC;
	CTRL.devid = 0x79061022;
	CTRL.has_pm = 1;
	SC.dev = &CTRL;
	SMBUS_ID = 0;
	SMBUS.revid = 0;
	STABLE_AFTER = 0; UNSTABLE = 0; POLLS = 0; PM_STATE = 0; PM_STUCK = 0; NO_SOFT_RESET = 0; DELAYED = 0;
	for (int i = 0; i < MMC_EXTCSD_SIZE; i++)
		CARD_EXT_CSD[i] = (uint8_t)(i * 7 + 3);
	CARD_EXT_CSD[EXT_CSD_REV] = 8;
	CARD_EXT_CSD[EXT_CSD_GEN_CMD6_TIME] = 0x0a;
	CARD_WIDTH = EXT_CSD_BUS_WIDTH_1;
	CARD_CHANGED_AT_4 = -1;
	CARD_REFUSES_SWITCH = 0; CARD_SWITCH_ANSWER_LOST = 0; CARD_READ_FAILS = 0; CONTROLLER_ERR = 0; REQUEST_ERROR = MMC_ERR_NONE;
	DONE_CALLS = 0; DONE_DATA_SEEN = NULL;
	fchemmc_disable = 0;
	strcpy(version, FCHEMMC_KERNEL "\n    root@builder:/usr/obj/usr/src/amd64.amd64/sys/SMP\n");
}

static int submit(uint32_t opcode, uint32_t arg, struct mmc_data *data)
{
	struct mmc_command cmd = { opcode, arg, 0, -1, data };
	struct mmc_request req = { &cmd, done, (void *)0x5ca1ab1e, 0 };
	int calls = DONE_CALLS, err;

	err = fchemmc_request(&CTRL, NULL, &req);
	CHECK(err != 0 || DONE_CALLS == calls + 1, "CMD%u completed %d times", opcode, DONE_CALLS - calls);
	CHECK(err != 0 || DONE_DATA_SEEN == (void *)0x5ca1ab1e, "CMD%u completed with another done_data", opcode);
	CHECK(req.done == done && req.done_data == (void *)0x5ca1ab1e, "CMD%u: the request's done not given back", opcode);
	return err != 0 ? -1000 - err : cmd.error;
}

/* mmc(4)'s discovery up to the bus test: CMD0, CMD7, the 1-bit EXT_CSD read, then the host at 4 bits. */
static void discover(void)
{
	static uint8_t raw[MMC_EXTCSD_SIZE];
	struct mmc_data d = { MMC_EXTCSD_SIZE, raw, MMC_DATA_READ };

	SC.slot.host.ios.bus_width = bus_width_1;
	submit(MMC_GO_IDLE_STATE, 0, NULL);
	submit(MMC_SELECT_CARD, 0x0001 << 16, NULL);
	submit(MMC_SEND_EXT_CSD, 0, &d);
	SC.slot.host.ios.bus_width = bus_width_4;
}

static int bus_test(uint8_t *buf)
{
	static const uint8_t p4[4] = { 0x5A, 0, 0, 0 };
	struct mmc_data w = { 4, (void *)p4, MMC_DATA_WRITE };
	struct mmc_data r = { 4, buf, MMC_DATA_READ };

	memset(buf, 0xee, 4);
	submit(MMC_BUSTEST_W, 0, &w);
	return submit(MMC_BUSTEST_R, 0, &r);
}

static void test_probe(void)
{
	static const uint32_t others[] = { 0x79071022, 0x79051022, 0x0f148086, 0x7906abcd, 0x22948086, 0 };

	fresh();
	CHECK(fchemmc_probe(&CTRL) == BUS_PROBE_DEFAULT, "1022:7906 not taken at BUS_PROBE_DEFAULT");
	CHECK(strstr(EV, "desc AMD FCH SD/eMMC Controller") != NULL, "no description");
	for (int i = 0; others[i] != 0; i++) {
		fresh();
		CTRL.devid = others[i];
		CHECK(fchemmc_probe(&CTRL) == ENXIO, "%08x taken", others[i]);
	}
	fresh();
	fchemmc_disable = 1;
	CHECK(fchemmc_probe(&CTRL) == ENXIO, "taken with hw.fchemmc.disable=1");
	fresh();
	strcpy(version, "FreeBSD 15.1-RELEASE-p7 stable/26.7-n284300-2b4c6d8e0f1a SMP\n    root@b:/x\n");
	CHECK(fchemmc_probe(&CTRL) == ENXIO, "taken on another kernel");
	fresh();
	strcpy(version, FCHEMMC_KERNEL "-DEBUG\n");
	CHECK(fchemmc_probe(&CTRL) == ENXIO, "taken on a kernel whose version line only begins with its own");
	fresh();
	strcpy(version, FCHEMMC_KERNEL);
	CHECK(fchemmc_probe(&CTRL) == BUS_PROBE_DEFAULT, "a version line alone, with no newline, not taken");
	/* The shape of OPNsense 26.7's own version string: the line and one newline. */
	fresh();
	strcpy(version, FCHEMMC_KERNEL "\n");
	CHECK(fchemmc_probe(&CTRL) == BUS_PROBE_DEFAULT, "a version line and one newline not taken");
}

static void test_bus_test_matches(void)
{
	uint8_t buf[4];

	fresh();
	discover();
	CHECK(SC.ext_csd_valid && memcmp(SC.ext_csd, CARD_EXT_CSD, MMC_EXTCSD_SIZE) == 0, "1-bit copy not kept");
	CHECK(SC.rca == 1, "rca %u", SC.rca);
	EV[0] = '\0';
	CHECK(bus_test(buf) == MMC_ERR_NONE, "4 bits refused");
	CHECK(memcmp(buf, "\xA5\0\0\0", 4) == 0, "not the pattern mmc_test_bus_width() expects: %02x", buf[0]);
	CHECK(strstr(EV, "controller CMD19") == NULL && strstr(EV, "controller CMD14") == NULL,
	    "the bus test went to the controller");
	CHECK(strcmp(EV, "switch 183=1 rca 1 set 1 timeout 500000 status;read EXT_CSD at card width 1;"
	    "switch 183=0 rca 1 set 1 timeout 500000 status;") == 0, "steps: %s", EV);
	CHECK(CARD_WIDTH == EXT_CSD_BUS_WIDTH_1, "card left at width %d", CARD_WIDTH);
	CHECK(SC.bus_test == 4, "bus_test %d", SC.bus_test);
}

static void test_bus_test_long_switch_time(void)
{
	uint8_t buf[4];

	fresh();
	CARD_EXT_CSD[EXT_CSD_GEN_CMD6_TIME] = 100;	/* 1 s */
	discover();
	EV[0] = '\0';
	bus_test(buf);
	CHECK(strstr(EV, "timeout 1000000") != NULL, "GEN_CMD6_TIME not used: %s", EV);
	fresh();
	CARD_EXT_CSD[EXT_CSD_REV] = 5;
	CARD_EXT_CSD[EXT_CSD_GEN_CMD6_TIME] = 100;
	discover();
	EV[0] = '\0';
	bus_test(buf);
	CHECK(strstr(EV, "timeout 500000") != NULL, "GEN_CMD6_TIME read on a card before revision 6: %s", EV);
}

static void test_bus_test_each_compared_byte(void)
{
	uint8_t buf[4];

	for (u_int i = 0; i < nitems(fchemmc_compared); i++) {
		fresh();
		discover();
		CARD_CHANGED_AT_4 = fchemmc_compared[i];
		EV[0] = '\0';
		CHECK(bus_test(buf) != MMC_ERR_NONE, "byte %u changed over 4 bits and 4 bits kept", fchemmc_compared[i]);
		CHECK(memcmp(buf, "\0\0\0\0", 4) == 0, "a pattern returned after a mismatch");
		CHECK(strstr(EV, "switch 183=0") != NULL && CARD_WIDTH == EXT_CSD_BUS_WIDTH_1,
		    "card not switched back after a mismatch: %s", EV);
		CHECK(SC.bus_test == 1, "bus_test %d", SC.bus_test);
	}
	/* A byte Linux does not compare - BUS_WIDTH itself changes with the switch. */
	fresh();
	discover();
	CARD_CHANGED_AT_4 = EXT_CSD_BUS_WIDTH;
	CHECK(bus_test(buf) == MMC_ERR_NONE, "a byte outside the compared ones failed the test");
}

static void test_bus_test_failures(void)
{
	uint8_t buf[4];

	fresh();
	discover();
	CARD_REFUSES_SWITCH = 1;
	EV[0] = '\0';
	CHECK(bus_test(buf) != MMC_ERR_NONE, "4 bits kept when the card refused the switch");
	CHECK(strstr(EV, "read EXT_CSD") == NULL, "EXT_CSD read after a refused switch");
	CHECK(strstr(EV, "switch 183=0") != NULL, "no switch back after a refused switch: %s", EV);

	/* The card takes the switch to 4 bits, but its answer is lost. */
	fresh();
	discover();
	CARD_SWITCH_ANSWER_LOST = 1;
	EV[0] = '\0';
	CHECK(bus_test(buf) != MMC_ERR_NONE, "4 bits kept when the switch timed out");
	CHECK(strstr(EV, "read EXT_CSD") == NULL, "EXT_CSD read after a failed switch");
	CHECK(strstr(EV, "switch 183=0") != NULL && CARD_WIDTH == EXT_CSD_BUS_WIDTH_1,
	    "card left at 4 bits after a lost answer: %s", EV);
	CHECK(SC.bus_test == 1, "bus_test %d", SC.bus_test);

	fresh();
	discover();
	CARD_READ_FAILS = 1;
	EV[0] = '\0';
	CHECK(bus_test(buf) != MMC_ERR_NONE, "4 bits kept when the read failed");
	CHECK(strstr(EV, "switch 183=0") != NULL && CARD_WIDTH == EXT_CSD_BUS_WIDTH_1, "not back at 1 bit: %s", EV);

	/* No 1-bit copy: the read over 1 bit failed. */
	fresh();
	REQUEST_ERROR = MMC_ERR_TIMEOUT;
	discover();
	REQUEST_ERROR = MMC_ERR_NONE;
	CHECK(!SC.ext_csd_valid, "a failed read kept as the 1-bit copy");
	EV[0] = '\0';
	CHECK(bus_test(buf) != MMC_ERR_NONE, "4 bits kept with nothing to compare with");
	CHECK(strstr(EV, "switch") == NULL && strstr(EV, "controller") == NULL, "commands sent: %s", EV);

	/* CMD0 forgets the copy and the address. */
	fresh();
	discover();
	SC.slot.host.ios.bus_width = bus_width_1;
	submit(MMC_GO_IDLE_STATE, 0, NULL);
	SC.slot.host.ios.bus_width = bus_width_4;
	EV[0] = '\0';
	CHECK(bus_test(buf) != MMC_ERR_NONE && strstr(EV, "switch") == NULL, "a copy from before CMD0 used: %s", EV);

	/* The host not at 4 bits. */
	fresh();
	discover();
	SC.slot.host.ios.bus_width = bus_width_8;
	EV[0] = '\0';
	CHECK(bus_test(buf) != MMC_ERR_NONE && strstr(EV, "switch") == NULL, "tested at 8 bits: %s", EV);
}

static void test_snoop_only_the_1bit_read(void)
{
	static uint8_t raw[MMC_EXTCSD_SIZE];
	struct mmc_data d = { MMC_EXTCSD_SIZE, raw, MMC_DATA_READ };
	struct mmc_data small = { 8, raw, MMC_DATA_READ };

	fresh();
	SC.slot.host.ios.bus_width = bus_width_4;
	submit(MMC_SEND_EXT_CSD, 0, &d);
	CHECK(!SC.ext_csd_valid, "a 4-bit read kept as the 1-bit copy");
	fresh();
	SC.slot.host.ios.bus_width = bus_width_1;
	submit(MMC_SEND_EXT_CSD, 0x1aa, NULL);	/* an SD card's CMD8, no data */
	submit(MMC_SEND_EXT_CSD, 0, &small);
	CHECK(!SC.ext_csd_valid, "a CMD8 without 512 bytes kept");
	CHECK(strstr(EV, "controller CMD8;controller CMD8;") != NULL, "a CMD8 not passed on: %s", EV);
	/* The controller busy: the request is given back untouched. */
	fresh();
	CONTROLLER_ERR = 16;
	CHECK(submit(MMC_SEND_EXT_CSD, 0, &d) == -1016, "EBUSY not passed back");
}

static void test_everything_else_passes(void)
{
	static const uint32_t ops[] = { 0, 1, 2, 3, 6, 7, 9, 13, 16, 17, 18, 23, 24, 25, 12, 55 };
	char want[64];

	for (u_int i = 0; i < nitems(ops); i++) {
		fresh();
		submit(ops[i], 0x10000, NULL);
		snprintf(want, sizeof(want), "controller CMD%u;", ops[i]);
		CHECK(strcmp(EV, want) == 0, "CMD%u: %s", ops[i], EV);
	}
	/* CMD19 and CMD14 without the bus test's data go on as they are. */
	fresh();
	submit(MMC_BUSTEST_W, 0, NULL);
	submit(MMC_BUSTEST_R, 0, NULL);
	CHECK(strcmp(EV, "controller CMD19;controller CMD14;") == 0, "%s", EV);
}

static void test_transfer_mode(void)
{
	fresh();
	SC.clear_tm = 1;
	fchemmc_write_2(&CTRL, &SC.slot, SDHCI_COMMAND_FLAGS, (6 << 8) | 0x1b);
	CHECK(strcmp(EV, "W2 0c=0000;W2 0e=061b;") == 0, "no data, quirk: %s", EV);
	fresh();
	SC.clear_tm = 1;
	fchemmc_write_2(&CTRL, &SC.slot, SDHCI_COMMAND_FLAGS, (8 << 8) | 0x3a);
	CHECK(strcmp(EV, "W2 0e=083a;") == 0, "with data, quirk: %s", EV);
	fresh();
	SC.clear_tm = 1;
	fchemmc_write_2(&CTRL, &SC.slot, SDHCI_TRANSFER_MODE, 0x13);
	fchemmc_write_2(&CTRL, &SC.slot, 0x04, 0x7200);
	CHECK(strcmp(EV, "W2 0c=0013;W2 04=7200;") == 0, "another register: %s", EV);
	fresh();
	SC.clear_tm = 0;
	fchemmc_write_2(&CTRL, &SC.slot, SDHCI_COMMAND_FLAGS, (6 << 8) | 0x1b);
	CHECK(strcmp(EV, "W2 0e=061b;") == 0, "no data, no quirk: %s", EV);
}

static void test_chipset_generation(void)
{
	static const struct { uint16_t id; uint8_t rev; int clear; } cases[] = {
		{ 0x790b, 0x4a, 1 },	/* the XGS 87's: "CZ" */
		{ 0x790b, 0x50, 1 },
		{ 0x790b, 0x51, 0 },	/* "NL" and later */
		{ 0x790b, 0x61, 0 },
		{ 0x780b, 0x14, 1 },	/* Hudson-2 */
		{ 0, 0, 0 },		/* no known SMBus */
	};

	for (u_int i = 0; i < nitems(cases); i++) {
		fresh();
		SMBUS_ID = cases[i].id;
		SMBUS.revid = cases[i].rev;
		CHECK(fchemmc_needs_clear_tm() == cases[i].clear, "SMBus %04x rev %02x", cases[i].id, cases[i].rev);
	}
}

static void test_reset(void)
{
	fresh();
	STABLE_AFTER = 3;
	fchemmc_reset(&CTRL, &SC.slot, SDHCI_RESET_ALL);
	CHECK(strcmp(EV, "save;busmaster off;pm 3;pm 0;restore D0;generic reset 01;") == 0, "full: %s", EV);
	CHECK(DELAYED == 10000 + 10000 + 3 * 10000, "waited %ld us", DELAYED);
	CHECK(SC.d3_cycles == 1 && SC.d0_failed == 0 && SC.no_soft_reset == 0,
	    "cycles %d, failed %d, no_soft_reset %d", SC.d3_cycles, SC.d0_failed, SC.no_soft_reset);
	/* No_Soft_Reset set: kept for the owner to read, the cycle the same. */
	fresh();
	NO_SOFT_RESET = 1;
	fchemmc_reset(&CTRL, &SC.slot, SDHCI_RESET_ALL);
	CHECK(SC.no_soft_reset == 1, "no_soft_reset %d", SC.no_soft_reset);
	CHECK(strcmp(EV, "save;busmaster off;pm 3;pm 0;restore D0;generic reset 01;") == 0, "No_Soft_Reset: %s", EV);
	/* D0 never reads back: nothing restored, so nothing can sleep under the slot's mutex. */
	fresh();
	PM_STUCK = 1;
	fchemmc_reset(&CTRL, &SC.slot, SDHCI_RESET_ALL);
	CHECK(strcmp(EV, "save;busmaster off;pm 3;pm 0;printf;generic reset 01;") == 0, "stuck in D3: %s", EV);
	CHECK(SC.d3_cycles == 1 && SC.d0_failed == 1, "cycles %d, failed %d", SC.d3_cycles, SC.d0_failed);
	CHECK(POLLS == 0 && DELAYED == 10000 + 10 * 10000, "polled %d, waited %ld us", POLLS, DELAYED);
	fresh();
	UNSTABLE = 1;
	fchemmc_reset(&CTRL, &SC.slot, SDHCI_RESET_ALL);
	CHECK(POLLS == 10 && DELAYED == 20000 + 100000, "card detect waited for %d polls, %ld us", POLLS, DELAYED);
	CHECK(strstr(EV, "generic reset 01") != NULL, "no generic reset after an unstable card detect");
	fresh();
	fchemmc_reset(&CTRL, &SC.slot, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
	CHECK(strcmp(EV, "generic reset 06;") == 0, "partial: %s", EV);
	fresh();
	CTRL.has_pm = 0;
	fchemmc_reset(&CTRL, &SC.slot, SDHCI_RESET_ALL);
	CHECK(strcmp(EV, "generic reset 01;") == 0, "no power management: %s", EV);
}

static void test_attach_limits_the_slot(void)
{
	fresh();
	SMBUS_ID = 0x790b;
	SMBUS.revid = 0x4a;
	memset(&SC, 0, sizeof(SC));
	CHECK(fchemmc_attach(&CTRL) == 0, "attach failed");
	CHECK(SC.clear_tm == 1, "Transfer Mode quirk not set for SMBus 0x4a");
	CHECK(strstr(EV, "init slot") != NULL && strstr(EV, "start slot") > strstr(EV, "init slot"), "%s", EV);
	{
		uint32_t caps = SC.slot.host.caps;
		char want[96];

		CHECK((caps & MMC_CAP_4_BIT_DATA) && !(caps & MMC_CAP_8_BIT_DATA), "width caps %x", caps);
		CHECK(!(caps & (MMC_CAP_MMC_DDR52 | MMC_CAP_MMC_HS200 | MMC_CAP_MMC_HS400 | MMC_CAP_UHS_SDR50 |
		    MMC_CAP_SIGNALING_180)), "modes beyond high speed left: %x", caps);
		CHECK(caps & MMC_CAP_HSPEED, "high speed taken away");
		CHECK(SC.slot.host.f_max == 50000000, "f_max %u", SC.slot.host.f_max);
		snprintf(want, sizeof(want), "start slot caps %x f_max 50000000", caps);
		CHECK(strstr(EV, want) != NULL, "started before the limits: %s", EV);
	}
	fresh();
	SMBUS_ID = 0x790b;
	SMBUS.revid = 0x51;
	memset(&SC, 0, sizeof(SC));
	fchemmc_attach(&CTRL);
	CHECK(SC.clear_tm == 0, "Transfer Mode quirk set for SMBus 0x51");
}

int main(void)
{
	(void)fchemmc_driver;
	test_probe();
	test_bus_test_matches();
	test_bus_test_long_switch_time();
	test_bus_test_each_compared_byte();
	test_bus_test_failures();
	test_snoop_only_the_1bit_read();
	test_everything_else_passes();
	test_transfer_mode();
	test_chipset_generation();
	test_reset();
	test_attach_limits_the_slot();
	printf("%d failures\n", FAILURES);
	return FAILURES != 0;
}
'''


def harness_source():
    """fchemmc.c without its includes, between the stand-ins and the tests."""
    body = '\n'.join(line for line in SOURCE.splitlines() if not line.startswith('#include'))
    return STANDINS.replace('@KERNEL@', KERNEL) + '\n#line 1 "fchemmc.c"\n' + body + HARNESS


class Behaviour(unittest.TestCase):
    """fchemmc.c compiled whole with the stand-ins above, every warning fatal, and run."""

    def test_the_driver_does_what_it_says(self):
        if CC is None:
            self.skipTest('no C compiler here')
        with tempfile.TemporaryDirectory() as tmp:
            c = os.path.join(tmp, 't.c')
            exe = os.path.join(tmp, 't')
            with open(c, 'w', encoding='utf-8', newline='\n') as f:
                f.write(harness_source())
            done = subprocess.run([CC, '-std=gnu11', '-Wall', '-Wextra', '-Wno-unused-parameter',
                                   '-Wno-unused-function', '-Werror', '-o', exe, c],
                                  capture_output=True, text=True)
            self.assertEqual(done.returncode, 0, done.stderr)
            ran = subprocess.run([exe], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('0 failures', ran.stdout)


def function(name):
    m = re.search(r'^(?:static )?[\w ]+\*?\n%s\(.*?^}\n' % re.escape(name), SOURCE, re.S | re.M)
    return m.group(0) if m else ''


class Source(unittest.TestCase):

    def test_it_keeps_the_notices_of_what_it_is_adapted_from(self):
        head = SOURCE[:1600]
        self.assertIn('SPDX-License-Identifier: BSD-2-Clause', head)
        self.assertIn('Copyright (c) 2008 Alexander Motin <mav@FreeBSD.org>', head)
        self.assertIn('Redistributions of source code must retain the above copyright', head)

    def test_it_matches_by_id_only_never_by_class(self):
        probe = function('fchemmc_probe')
        self.assertIn('pci_get_devid(dev) != FCHEMMC_DEVID', probe)
        self.assertIn('return (BUS_PROBE_DEFAULT);', probe)
        self.assertRegex(SOURCE, r'#define\s+FCHEMMC_DEVID\s+0x79061022\b')
        code = re.sub(r'/\*.*?\*/', '', SOURCE, flags=re.S)
        for name in ('BUS_PROBE_GENERIC', 'pci_get_class', 'pci_get_subclass'):
            self.assertFalse(name in code, name + ' in the code')

    def test_the_bus_test_never_reaches_the_controller(self):
        request = function('fchemmc_request')
        for case in ('MMC_BUSTEST_W', 'MMC_BUSTEST_R'):
            arm = re.search(r'case %s:(.*?)break;' % case, request, re.S).group(1)
            self.assertIn('req->done(req);', arm)
            self.assertIn('return (0);', arm)
            self.assertNotIn('sdhci_generic_request', arm)
        self.assertIn('fchemmc_bus_test(sc, reqdev, data)', request)

    def test_the_comparison_covers_what_linux_compares(self):
        table = re.search(r'fchemmc_compared\[\] = \{(.*?)\};', SOURCE, re.S).group(1)
        values = dict(MMCREG)
        for m in re.finditer(r'#define\s+(FCHEMMC_EXT_CSD_\w+)\s+(\d+)', SOURCE):
            values[m.group(1)] = int(m.group(2))
        got = set()
        for item in (t.strip() for t in table.split(',') if t.strip()):
            name, _, plus = item.partition('+')
            got.add(values[name.strip()] + (int(plus) if plus else 0))
        self.assertEqual(got, LINUX_COMPARED)

    def test_its_methods(self):
        table = re.search(r'fchemmc_methods\[\] = \{(.*?)DEVMETHOD_END', SOURCE, re.S).group(1)
        methods = dict(re.findall(r'DEVMETHOD\((\w+),\s*(\w+)\)', table))
        self.assertEqual(methods['device_probe'], 'fchemmc_probe')
        self.assertEqual(methods['mmcbr_request'], 'fchemmc_request')
        self.assertEqual(methods['sdhci_write_2'], 'fchemmc_write_2')
        self.assertEqual(methods['sdhci_reset'], 'fchemmc_reset')
        # The kernel's own sdhci core does the rest, as for sdhci_pci.
        for method in ('mmcbr_update_ios', 'mmcbr_get_ro', 'mmcbr_acquire_host', 'mmcbr_release_host',
                       'bus_read_ivar', 'bus_write_ivar'):
            self.assertTrue(methods[method].startswith('sdhci_generic_'), method)

    def test_it_is_a_module_of_its_own_on_the_kernels_sdhci_and_mmc(self):
        for line in ('DRIVER_MODULE(fchemmc, pci, fchemmc_driver, NULL, NULL);', 'MODULE_VERSION(fchemmc, 1);',
                     'SDHCI_DEPEND(fchemmc);', 'MMC_DECLARE_BRIDGE(fchemmc);'):
            self.assertIn(line, SOURCE)
        self.assertIn('"fchemmc",', SOURCE)
        # Every function its own, and static: nothing collides with the kernel's sdhci_pci.
        names = re.findall(r'^([a-z_]\w*)\(', SOURCE, re.M)
        self.assertGreater(len(names), 10)
        for name in names:
            self.assertTrue(name.startswith('fchemmc_'), name)
        self.assertEqual(re.findall(r'^static [\w ]+\n([a-z_]\w*)\(', SOURCE, re.M), names)
        self.assertIn('sdhci_init_slot(dev, slot, 0)', SOURCE)
        self.assertIn('sdhci_start_slot(slot)', SOURCE)

    def test_the_tunable(self):
        self.assertRegex(SOURCE, r'SYSCTL_INT\(_hw_fchemmc, OID_AUTO, disable, CTLFLAG_RDTUN, &fchemmc_disable,')
        self.assertIn('if (fchemmc_disable != 0)', function('fchemmc_probe'))

    def test_the_makefile(self):
        with open(MAKEFILE, encoding='utf-8') as f:
            make = f.read()
        self.assertRegex(make, r'(?m)^KMOD\s*=\s*fchemmc$')
        srcs = re.search(r'(?m)^SRCS\s*=\s*(.*)$', make).group(1).split()
        for need in ('fchemmc.c', 'fchemmc_kernel.h', 'mmcbr_if.h', 'sdhci_if.h', 'opt_mmccam.h'):
            self.assertIn(need, srcs)
        self.assertIn('#include "fchemmc_kernel.h"', SOURCE)
        self.assertIn('FCHEMMC_KERNEL', make)
        self.assertIn('.include <bsd.kmod.mk>', make)


def read(path):
    with open(path, encoding='utf-8') as f:
        return f.read()


class Wiring(unittest.TestCase):
    """install.sh and uninstall.sh, read."""

    def test_install_lays_it_down_only_where_the_controller_is(self):
        text = read(INSTALL)
        m = re.search(r"grep -qE '(chip=0x79061022[^']*)'; then\n(.*?)\nelse\n(.*?)\nfi\n", text, re.S)
        self.assertIsNotNone(m, 'no pciconf test around the fchemmc sources')
        self.assertEqual(m.group(1), 'chip=0x79061022|vendor=0x1022[[:space:]]+device=0x7906')
        self.assertIn('copy_driver "${SRC}/contrib/fchemmc" "${PREFIX}/share/os-xgs-npu/fchemmc"', m.group(2))
        for gone in ('rm -rf "${PREFIX}/share/os-xgs-npu/fchemmc"', '/boot/modules/fchemmc.ko ',
                     '/boot/modules/fchemmc.ko.kernel', '/boot/loader.conf.d/fchemmc.conf'):
            self.assertIn(gone, m.group(3))
        self.assertEqual(text.count('copy_driver "${SRC}/contrib/fchemmc"'), 1)
        # Before the follower is asked to build anything.
        self.assertLess(text.index('copy_driver "${SRC}/contrib/fchemmc"'),
                        text.index('kernel-follow.sh"; } > /var/db/os-xgs-npu/install-build.log'))

    def test_uninstall_takes_it_all_away(self):
        text = read(UNINSTALL)
        for gone in ('/boot/modules/fchemmc.ko', '/boot/modules/fchemmc.ko.kernel', '/boot/loader.conf.d/fchemmc.conf',
                     '/var/db/os-xgs-npu/build-fchemmc', '/var/db/os-xgs-npu/fchemmc-follow.said'):
            self.assertIn(gone, text)
        self.assertIn('rm -rf "${PREFIX}/share/os-xgs-npu"', text)

    def test_the_follower_runs_it_after_the_module_it_follows(self):
        text = read(FOLLOW)
        # and, on the eMMC system disk, what the next boot then takes (tools/test-emmc-root.py)
        self.assertIn('follow_fchemmc ""\n\temmc_after\n\tprune_old_sources\n\texit 0', text)
        self.assertTrue(text.rstrip().endswith('unpin_old_kernel\nfollow_fchemmc "${SYSDIR}"\nemmc_after\nexit 0'))


@unittest.skipIf(SH is None, 'no /bin/sh here')
class Follower(unittest.TestCase):
    """kernel-follow.sh with an installed npuep.ko, the fchemmc sources laid down, and stand-ins."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        for d in ('modules', 'src', 'fsrc', 'sys/sys', 'sys/amd64/include', 'loader.conf.d'):
            os.makedirs(self.path(d))
        open(self.path('sys/sys/param.h'), 'w').close()
        # A kernel built without reproducible builds carries a second line after its version line;
        # the follower takes the first line alone, which is what the module compares.
        self.write('kernel', '\0\0' + KERNEL + '\n    root@builder:/usr/obj/usr/src/amd64.amd64/sys/SMP\n\0')
        for name in ('Makefile', 'npuep.c'):
            open(self.path('src/' + name), 'w').close()
        for name in ('Makefile', 'fchemmc.c'):
            open(self.path('fsrc/' + name), 'w').close()

    def tearDown(self):
        self.tmp.cleanup()

    def path(self, name):
        return os.path.join(self.dir, name)

    def write(self, name, text):
        with open(self.path(name), 'w', newline='\n') as f:
            f.write(text)

    def read(self, name):
        try:
            with open(self.path(name)) as f:
                return f.read()
        except FileNotFoundError:
            return None

    def stub(self, name, body):
        self.write(name, '#!/bin/sh\n%s\n' % body)
        os.chmod(self.path(name), 0o755)
        return self.path(name)

    def npuep(self, stamp):
        self.write('modules/npuep.ko', 'ELF npuep\n')
        self.write('modules/npuep.ko.kernel', stamp + '\n')

    def follow(self, sources=True, fails=False, **more):
        make = self.stub('make', 'ln -s "${1#SYSDIR=}/amd64/include" machine\n'
                                 'printf "%%s\\n" "$*" >> "%s"\n'
                                 'case "$*" in *FCHEMMC_KERNEL=*) %s ;; '
                                 '*) : > npuep.o; echo "ELF npuep" > npuep.ko ;; esac'
                         % (self.path('make.args'),
                            'echo "error: no" >&2; exit 1' if fails else
                            'echo "ELF fchemmc for ${2#FCHEMMC_KERNEL=}" > fchemmc.ko'))
        with open(FOLLOW, encoding='utf-8') as f:
            text = f.read()
        for command, stand_in in (('/usr/bin/logger', self.stub('logger', 'printf "%%s\\n" "$*" >> "%s"'
                                                                 % self.path('log'))),
                                  ('/sbin/sysctl', self.stub('sysctl', 'exit 1')),
                                  ('/usr/bin/make', make),
                                  ('/var/db/os-xgs-npu/build.log', self.path('build.log'))):
            text = text.replace(command, stand_in)
        self.write('kernel-follow.sh', text)
        fetch = ('echo "SYSDIR=%s"' % self.path('sys')) if sources else 'echo "no sources here"; exit 1'
        env = dict(os.environ)
        for name in ('OCTEP_FOLLOW_LOCKED', 'PINONLY', 'FORCE', 'DRY'):
            env.pop(name, None)
        env.update(OCTEP_FOLLOW_LOCKED='1', NOFETCH='1', DRIVER='npuep',
                   MODDIR=self.path('modules'), KERNEL=self.path('kernel'), KERNEL_OLD=self.path('kernel.old'),
                   NEXTBOOT=self.stub('nextboot', 'exit 0'), PENDINGDIR=self.path('pending'),
                   PENDING_KERNEL=self.path('kernel.pending'), PINNED=self.path('pinned'), ARMED=self.path('armed'),
                   SAID=self.path('said'), LOCK=self.path('lock'), RESULT=self.path('result'),
                   BUILDROOT=self.path('build'), SRCTREE=self.path('src'), FETCH=self.stub('fetch.sh', fetch),
                   UNAME=self.stub('uname', 'echo "%s"' % KERNEL),
                   FCHEMMC_SRCTREE=self.path('fsrc'), FCHEMMC_BUILDROOT=self.path('fbuild'),
                   FCHEMMC_BUILDLOG=self.path('fbuild.log'), FCHEMMC_SAID=self.path('fsaid'),
                   FCHEMMC_LOADER=self.path('loader.conf.d/fchemmc.conf'))
        env.update(more)
        return subprocess.run([SH, self.path('kernel-follow.sh')], env=env, capture_output=True, text=True,
                              timeout=60)

    def assert_built_for(self, version):
        self.assertEqual(self.read('modules/fchemmc.ko'), 'ELF fchemmc for %s\n' % version)
        self.assertEqual(self.read('modules/fchemmc.ko.kernel'), version + '\n')
        self.assertEqual(self.read('loader.conf.d/fchemmc.conf'), 'fchemmc_load="YES"\n')

    def test_an_install_builds_it_after_the_module_against_the_same_sources(self):
        self.npuep('none yet')
        done = self.follow(FORCE='1')
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assert_built_for(KERNEL)
        calls = self.read('make.args').splitlines()
        self.assertEqual(calls, ['SYSDIR=%s' % self.path('sys'),
                                 'SYSDIR=%s FCHEMMC_KERNEL=%s' % (self.path('sys'), KERNEL)])
        self.assertIn('fchemmc: installed', self.read('log'))

    def test_a_follower_tick_builds_it_when_only_it_is_missing(self):
        self.npuep(KERNEL)
        done = self.follow()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assert_built_for(KERNEL)
        self.assertEqual(self.read('make.args').splitlines(),
                         ['SYSDIR=%s FCHEMMC_KERNEL=%s' % (self.path('sys'), KERNEL)])
        # And the tick after it does nothing.
        os.unlink(self.path('make.args'))
        self.follow()
        self.assertIsNone(self.read('make.args'))
        self.assert_built_for(KERNEL)

    def test_a_new_kernel_rebuilds_it_with_the_module(self):
        self.npuep(KERNEL)
        self.follow()
        self.write('kernel', '\0\0' + NEWER + '\n\0')
        done = self.follow()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assert_built_for(NEWER)
        self.assertEqual(self.read('modules/npuep.ko.kernel'), NEWER + '\n')

    def test_no_sources_is_said_once_and_leaves_no_loader_line(self):
        self.npuep(KERNEL)
        self.follow(sources=False)
        self.assertIsNone(self.read('modules/fchemmc.ko'))
        self.assertIsNone(self.read('loader.conf.d/fchemmc.conf'))
        self.assertEqual(self.read('log').count('fchemmc: no kernel sources'), 1)
        self.follow(sources=False)
        self.assertEqual(self.read('log').count('fchemmc: no kernel sources'), 1)

    def test_a_failed_build_leaves_the_module_there_and_is_not_tried_every_tick(self):
        self.npuep(KERNEL)
        self.follow()
        self.write('kernel', '\0\0' + NEWER + '\n\0')
        self.npuep(NEWER)
        self.follow(fails=True)
        self.assertEqual(self.read('modules/fchemmc.ko'), 'ELF fchemmc for %s\n' % KERNEL)
        self.assertEqual(self.read('modules/fchemmc.ko.kernel'), KERNEL + '\n')
        self.assertEqual(self.read('log').count('fchemmc: the build failed'), 1)
        os.unlink(self.path('make.args'))
        self.follow(fails=True)
        self.assertIsNone(self.read('make.args'), 'a failed build tried again at the next tick')
        self.follow(fails=False, FORCE='1')
        self.assert_built_for(NEWER)

    def test_without_its_sources_it_does_nothing(self):
        shutil.rmtree(self.path('fsrc'))
        self.npuep('none yet')
        done = self.follow(FORCE='1')
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.read('make.args').splitlines(), ['SYSDIR=%s' % self.path('sys')])
        self.assertIsNone(self.read('modules/fchemmc.ko'))
        self.assertIsNone(self.read('loader.conf.d/fchemmc.conf'))
        self.assertNotIn('fchemmc', self.read('log') or '')

    def test_the_loader_line_goes_with_the_module(self):
        self.npuep(KERNEL)
        self.follow()
        os.unlink(self.path('modules/fchemmc.ko'))
        os.unlink(self.path('modules/fchemmc.ko.kernel'))
        self.follow(sources=False)
        self.assertIsNone(self.read('loader.conf.d/fchemmc.conf'))


if __name__ == '__main__':
    unittest.main()
