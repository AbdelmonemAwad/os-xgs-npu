/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2008 Alexander Motin <mav@FreeBSD.org>
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * fchemmc: the AMD FCH SD/eMMC controller, PCI 1022:7906, an SDHCI 2.00 host,
 * as the XGS 87 carries it.  Adapted from FreeBSD's sys/dev/sdhci/sdhci_pci.c,
 * whose structure it keeps: the kernel's own sdhci core does the work
 * (sdhci_init_slot(), sdhci_start_slot(), the sdhci_generic_* methods), and
 * this front end gives it the registers.
 *
 * Why a driver of its own: the kernel this is built for has sdhci, sdhci_pci,
 * mmc and mmcsd compiled in, so patched copies of them cannot be loaded.
 * sdhci_pci takes this controller by its class alone, at BUS_PROBE_GENERIC;
 * this driver matches 1022:7906 by its id, at BUS_PROBE_DEFAULT, and wins the
 * probe when the loader has loaded it.
 *
 * What it does differently, each as Linux does it for this controller
 * (drivers/mmc/host/sdhci-pci-core.c, drivers/mmc/core/mmc.c):
 *
 * - The CMD19/CMD14 bus test is never sent to the card.  On this controller
 *   the CMD19 write never completes, and the eMMC then answers no command
 *   until its power is removed.  mmc(4) still runs its bus test; this
 *   driver answers CMD19 itself, and answers CMD14 the way Linux checks a
 *   width: the card is switched to it with CMD6, EXT_CSD is read again over
 *   it and compared with the copy mmc(4) read over 1 bit
 *   (mmc_compare_ext_csds()), and the card is put back at 1 bit.  The
 *   pattern mmc_test_bus_width() expects is returned only when they match.
 * - On the chipsets before the one whose SMBus, 1022:790b, is revision 0x51
 *   or later, Transfer Mode is written 0 before a command without data
 *   (amd_probe(), SDHCI_QUIRK2_CLEAR_TRANSFERMODE_REG_BEFORE_CMD).
 * - A full reset first takes the controller to D3hot and back to D0
 *   (amd_sdhci_reset(), which asks for D3cold).
 * - 4 bits at most, no DDR52, HS200 or HS400, and 50 MHz at most: an SDHCI
 *   2.00 host, run as Linux runs this card (4 bits, 50 MHz, high speed).
 *
 * hw.fchemmc.disable=1, set in the loader, leaves the controller to the
 * built-in driver.  So does a kernel other than the one this was built for:
 * the module carries that kernel's version line and does not probe on any
 * other, since the loader loads it before anything can check it.
 */

#include <sys/cdefs.h>
#include "opt_mmccam.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/mmc/bridge.h>
#include <dev/mmc/mmcreg.h>
#include <dev/mmc/mmc_subr.h>

#include <dev/sdhci/sdhci.h>

#include "mmcbr_if.h"
#include "sdhci_if.h"

#include "fchemmc_kernel.h"

#ifdef MMCCAM
#error "fchemmc is a bridge for mmc(4), not for MMCCAM"
#endif

#define	FCHEMMC_DEVID		0x79061022	/* device << 16 | vendor */
#define	AMD_VENDOR		0x1022
#define	AMD_HUDSON2_SMBUS	0x780b
#define	AMD_KERNCZ_SMBUS	0x790b
#define	AMD_KERNCZ_SMBUS_REV_NL	0x51
#define	FCHEMMC_MAX_CLOCK	50000000

#define	PCI_SLOT_INFO			0x40	/* 8 bits */
#define	PCI_SLOT_INFO_FIRST_BAR(x)	((x) & 7)

/* EXT_CSD bytes Linux's mmc_compare_ext_csds() compares, not all in mmcreg.h. */
#define	FCHEMMC_EXT_CSD_ERASED_MEM_CONT	181
#define	FCHEMMC_EXT_CSD_STRUCTURE	194
#define	FCHEMMC_EXT_CSD_S_A_TIMEOUT	217
#define	FCHEMMC_EXT_CSD_SEC_TRIM_MULT	229
#define	FCHEMMC_EXT_CSD_SEC_ERASE_MULT	230
#define	FCHEMMC_EXT_CSD_TRIM_MULT	232

/* Bus test results, dev.fchemmc.N.bus_test. */
#define	FCHEMMC_TEST_NONE	0	/* not run yet */
#define	FCHEMMC_TEST_4BIT	4	/* EXT_CSD read back the same over 4 bits */
#define	FCHEMMC_TEST_FAILED	1	/* it did not: mmc(4) stays at 1 bit */

struct fchemmc_softc {
	device_t	dev;
	struct resource	*irq_res;	/* IRQ resource */
	void		*intrhand;	/* Interrupt handle */
	struct resource	*mem_res;	/* Memory resource */
	struct sdhci_slot slot;
	int		have_slot;
	int		clear_tm;	/* Transfer Mode cleared before a command without data */
	int		bus_test;	/* FCHEMMC_TEST_* */
	int		no_soft_reset;	/* PMCSR No_Soft_Reset, -1 until read */
	int		d3_cycles;	/* Full resets through D3hot and back */
	int		d0_failed;	/* Of those, D0 not read back */
	uint16_t	rca;		/* Of the card selected last */
	int		ext_csd_valid;
	uint8_t		ext_csd[MMC_EXTCSD_SIZE];	/* As read over 1 bit */
	void		(*snoop_done)(struct mmc_request *);
	void		*snoop_done_data;
};

static int fchemmc_disable = 0;
static int fchemmc_enable_msi = 1;

static SYSCTL_NODE(_hw, OID_AUTO, fchemmc, CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
    "AMD FCH SD/eMMC controller");
SYSCTL_INT(_hw_fchemmc, OID_AUTO, disable, CTLFLAG_RDTUN, &fchemmc_disable,
    0, "Leave the controller to the built-in sdhci_pci");
SYSCTL_INT(_hw_fchemmc, OID_AUTO, enable_msi, CTLFLAG_RDTUN,
    &fchemmc_enable_msi, 0, "Enable MSI interrupts");

/* The pattern mmc_test_bus_width() expects back from a working 4-bit bus. */
static const uint8_t fchemmc_p4ok[4] = { 0xA5, 0x00, 0x00, 0x00 };

/* The EXT_CSD bytes compared, as Linux's mmc_compare_ext_csds() does. */
static const uint16_t fchemmc_compared[] = {
	EXT_CSD_PART_SUPPORT, FCHEMMC_EXT_CSD_ERASED_MEM_CONT, EXT_CSD_REV,
	FCHEMMC_EXT_CSD_STRUCTURE, EXT_CSD_CARD_TYPE,
	FCHEMMC_EXT_CSD_S_A_TIMEOUT, EXT_CSD_HC_WP_GRP_SIZE,
	EXT_CSD_ERASE_TO_MULT, EXT_CSD_ERASE_GRP_SIZE,
	FCHEMMC_EXT_CSD_SEC_TRIM_MULT, FCHEMMC_EXT_CSD_SEC_ERASE_MULT,
	EXT_CSD_SEC_FEATURE_SUPPORT, FCHEMMC_EXT_CSD_TRIM_MULT,
	EXT_CSD_SEC_CNT, EXT_CSD_SEC_CNT + 1, EXT_CSD_SEC_CNT + 2,
	EXT_CSD_SEC_CNT + 3
};

/*
 * The kernel this module was built for, by its version line (FCHEMMC_KERNEL,
 * written into fchemmc_kernel.h by the build).
 */
static int
fchemmc_kernel_matches(void)
{
	size_t n;

	n = strlen(FCHEMMC_KERNEL);
	return (n != 0 && strncmp(version, FCHEMMC_KERNEL, n) == 0 &&
	    (version[n] == '\n' || version[n] == '\0'));
}

/*
 * Whether Transfer Mode has to be cleared before a command without data:
 * where a Hudson-2 SMBus is present, or a 1022:790b SMBus below revision
 * 0x51 (the generation Linux calls "CZ"), as Linux's amd_probe() reads it.
 */
static int
fchemmc_needs_clear_tm(void)
{
	device_t smbus;

	if (pci_find_device(AMD_VENDOR, AMD_HUDSON2_SMBUS) != NULL)
		return (1);
	smbus = pci_find_device(AMD_VENDOR, AMD_KERNCZ_SMBUS);
	return (smbus != NULL &&
	    pci_get_revid(smbus) < AMD_KERNCZ_SMBUS_REV_NL);
}

/*
 * What the slot offers mmc(4), once sdhci_init_slot() has read the
 * capabilities: 4 bits and never 8, no DDR52, HS200, HS400 or UHS modes and
 * no 1.8 V or 1.2 V signaling (an SDHCI 2.00 host has none of them), and a
 * clock of 50 MHz at most, at which Linux runs this card in high speed.
 */
static void
fchemmc_limit_slot(struct sdhci_slot *slot)
{

	slot->host.caps &= ~(MMC_CAP_8_BIT_DATA | MMC_CAP_MMC_DDR52 |
	    MMC_CAP_MMC_HS200 | MMC_CAP_MMC_HS400 | MMC_CAP_MMC_ENH_STROBE |
	    MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 | MMC_CAP_UHS_SDR50 |
	    MMC_CAP_UHS_SDR104 | MMC_CAP_UHS_DDR50 | MMC_CAP_SIGNALING_120 |
	    MMC_CAP_SIGNALING_180);
	slot->host.caps |= MMC_CAP_4_BIT_DATA;
	if (slot->host.f_max > FCHEMMC_MAX_CLOCK)
		slot->host.f_max = FCHEMMC_MAX_CLOCK;
}

static uint8_t
fchemmc_read_1(device_t dev, struct sdhci_slot *slot __unused, bus_size_t off)
{
	struct fchemmc_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res, 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return bus_read_1(sc->mem_res, off);
}

static void
fchemmc_write_1(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off, uint8_t val)
{
	struct fchemmc_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res, 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	bus_write_1(sc->mem_res, off, val);
}

static uint16_t
fchemmc_read_2(device_t dev, struct sdhci_slot *slot __unused, bus_size_t off)
{
	struct fchemmc_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res, 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return bus_read_2(sc->mem_res, off);
}

/*
 * The command register is written last, after Transfer Mode: for a command
 * without data (no SDHCI_CMD_DATA), on a chipset that needs it, Transfer Mode
 * is written 0 first, so that the mode of the last command with data is not
 * taken for this one.  Tuning commands carry data, so they never come here.
 */
static void
fchemmc_write_2(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off, uint16_t val)
{
	struct fchemmc_softc *sc = device_get_softc(dev);

	if (off == SDHCI_COMMAND_FLAGS && sc->clear_tm &&
	    (val & SDHCI_CMD_DATA) == 0) {
		bus_barrier(sc->mem_res, 0, 0xFF,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		bus_write_2(sc->mem_res, SDHCI_TRANSFER_MODE, 0);
	}
	bus_barrier(sc->mem_res, 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	bus_write_2(sc->mem_res, off, val);
}

static uint32_t
fchemmc_read_4(device_t dev, struct sdhci_slot *slot __unused, bus_size_t off)
{
	struct fchemmc_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res, 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return bus_read_4(sc->mem_res, off);
}

static void
fchemmc_write_4(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off, uint32_t val)
{
	struct fchemmc_softc *sc = device_get_softc(dev);

	bus_barrier(sc->mem_res, 0, 0xFF,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	bus_write_4(sc->mem_res, off, val);
}

static void
fchemmc_read_multi_4(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off, uint32_t *data, bus_size_t count)
{
	struct fchemmc_softc *sc = device_get_softc(dev);

	bus_read_multi_stream_4(sc->mem_res, off, data, count);
}

static void
fchemmc_write_multi_4(device_t dev, struct sdhci_slot *slot __unused,
    bus_size_t off, uint32_t *data, bus_size_t count)
{
	struct fchemmc_softc *sc = device_get_softc(dev);

	bus_write_multi_stream_4(sc->mem_res, off, data, count);
}

/*
 * A full reset goes through the PCI power states first, as Linux's
 * amd_sdhci_reset() does: 0x7906 needs a hard reset to clear all of its
 * internal state.  Linux asks for D3cold, which can cut power through ACPI
 * where the platform has it; this goes to D3hot only, by writing the power
 * management register directly rather than with pci_set_powerstate(): sdhci
 * calls this with the slot's mutex held, and pci_set_powerstate() can go
 * through ACPI, which sleeps.  Whether D3hot resets the controller's state
 * depends on its No_Soft_Reset bit, kept in dev.fchemmc.N.no_soft_reset.
 * D3hot to D0 may reset the configuration space, so it is saved before and
 * restored after, and only once D0 reads back: pci_restore_state() would
 * otherwise call pci_set_powerstate() itself.  If D0 does not read back,
 * that is logged and counted, and the generic reset runs alone.  Card
 * detect is reset with the rest and is given up to 100 ms to settle before
 * the generic reset.  sdhci asks for a full reset at attach, detach,
 * suspend and resume only; its command and data timeouts reset CMD and
 * DATA, which get the generic reset alone, so this does not recover a
 * stuck card at run time.
 */
static void
fchemmc_reset(device_t dev, struct sdhci_slot *slot, uint8_t mask)
{
	struct fchemmc_softc *sc = device_get_softc(dev);
	uint16_t status;
	int i, pmc;

	if ((mask & SDHCI_RESET_ALL) != 0 &&
	    pci_find_cap(dev, PCIY_PMG, &pmc) == 0) {
		status = pci_read_config(dev, pmc + PCIR_POWER_STATUS, 2);
		sc->no_soft_reset = (status & PCIM_PSTAT_NOSOFTRESET) != 0;
		status &= ~PCIM_PSTAT_DMASK;
		pci_save_state(dev);
		pci_disable_busmaster(dev);
		pci_write_config(dev, pmc + PCIR_POWER_STATUS,
		    status | PCIM_PSTAT_D3, 2);
		DELAY(10000);
		pci_write_config(dev, pmc + PCIR_POWER_STATUS,
		    status | PCIM_PSTAT_D0, 2);
		for (i = 0; i < 10; i++) {
			DELAY(10000);
			if ((pci_read_config(dev, pmc + PCIR_POWER_STATUS, 2) &
			    PCIM_PSTAT_DMASK) == PCIM_PSTAT_D0)
				break;
		}
		sc->d3_cycles++;
		if (i == 10) {
			sc->d0_failed++;
			device_printf(dev, "not back in D0 after D3hot; "
			    "configuration not restored\n");
		} else {
			pci_restore_state(dev);
			for (i = 0; i < 10; i++) {
				if (fchemmc_read_4(dev, slot,
				    SDHCI_PRESENT_STATE) & SDHCI_CARD_STABLE)
					break;
				DELAY(10000);
			}
		}
	}
	sdhci_generic_reset(dev, slot, mask);
}

/*
 * The EXT_CSD mmc(4) reads over 1 bit, kept when the read succeeds: what
 * the bus test below compares with.  Called where the request completes,
 * with the slot's mutex held, like the completion it stands in front of.
 */
static void
fchemmc_snoop_done(struct mmc_request *req)
{
	struct fchemmc_softc *sc = req->done_data;
	struct mmc_data *data = req->cmd->data;

	if (req->cmd->error == MMC_ERR_NONE) {
		memcpy(sc->ext_csd, data->data, MMC_EXTCSD_SIZE);
		sc->ext_csd_valid = 1;
	}
	req->done = sc->snoop_done;
	req->done_data = sc->snoop_done_data;
	req->done(req);
}

/*
 * mmc(4)'s CMD14, answered without sending it: the card is switched to
 * 4 bits with CMD6 (the host is at 4 bits already, set so for the test), its
 * EXT_CSD is read again and compared with the copy read over 1 bit, and the
 * card is switched back to 1 bit, as the bus test leaves it; mmc(4) puts the
 * host back at 1 bit and later switches both to the width found.  These go
 * to the card through mmc(4) itself, which calls this request method again
 * for each; it is safe to sleep here, since mmc(4) holds no lock across a
 * request.  4 bits is the widest this host offers, so CMD14 is only ever
 * asked for 4 bytes.
 */
static int
fchemmc_bus_test(struct fchemmc_softc *sc, device_t mmcdev,
    struct mmc_data *data)
{
	uint8_t *ext_csd;
	u_int gen, i, timeout;
	int err;

	memset(data->data, 0, data->len);
	if (sc->ext_csd_valid == 0 || sc->rca == 0 || data->len != 4 ||
	    sc->slot.host.ios.bus_width != bus_width_4) {
		sc->bus_test = FCHEMMC_TEST_FAILED;
		return (MMC_ERR_FAILED);
	}
	/* The switch time in us: GEN_CMD6_TIME, in 10 ms units, or 500 ms. */
	timeout = 500 * 1000;
	if (sc->ext_csd[EXT_CSD_REV] >= 6) {
		gen = sc->ext_csd[EXT_CSD_GEN_CMD6_TIME] * 10000U;
		if (gen > timeout)
			timeout = gen;
	}
	ext_csd = malloc(MMC_EXTCSD_SIZE, M_DEVBUF, M_WAITOK | M_ZERO);
	err = mmc_switch(mmcdev, mmcdev, sc->rca, EXT_CSD_CMD_SET_NORMAL,
	    EXT_CSD_BUS_WIDTH, EXT_CSD_BUS_WIDTH_4, timeout, true);
	if (err == MMC_ERR_NONE) {
		err = mmc_send_ext_csd(mmcdev, mmcdev, ext_csd);
		for (i = 0; err == MMC_ERR_NONE &&
		    i < nitems(fchemmc_compared); i++) {
			if (ext_csd[fchemmc_compared[i]] !=
			    sc->ext_csd[fchemmc_compared[i]])
				err = MMC_ERR_FAILED;
		}
	}
	/*
	 * Back to 1 bit after any attempt, whether 4 bits work or not: the
	 * card may have taken the switch even when its answer was lost.
	 */
	if (mmc_switch(mmcdev, mmcdev, sc->rca, EXT_CSD_CMD_SET_NORMAL,
	    EXT_CSD_BUS_WIDTH, EXT_CSD_BUS_WIDTH_1, timeout, true) !=
	    MMC_ERR_NONE)
		err = MMC_ERR_FAILED;
	free(ext_csd, M_DEVBUF);
	if (err != MMC_ERR_NONE) {
		sc->bus_test = FCHEMMC_TEST_FAILED;
		return (err);
	}
	memcpy(data->data, fchemmc_p4ok, sizeof(fchemmc_p4ok));
	sc->bus_test = FCHEMMC_TEST_4BIT;
	return (MMC_ERR_NONE);
}

/*
 * Every request mmc(4) sends goes through here.  CMD19 (MMC_BUSTEST_W) is
 * never sent to the card, and CMD14 (MMC_BUSTEST_R) is answered by
 * fchemmc_bus_test(); both complete here, at once.  The EXT_CSD read over
 * 1 bit is kept on its way back, and the card's address taken from CMD7;
 * CMD0 forgets both.  Everything else goes to the controller unchanged.
 */
static int
fchemmc_request(device_t brdev, device_t reqdev, struct mmc_request *req)
{
	struct fchemmc_softc *sc = device_get_softc(brdev);
	struct mmc_command *cmd = req->cmd;
	struct mmc_data *data = cmd->data;
	int err;

	switch (cmd->opcode) {
	case MMC_GO_IDLE_STATE:
		sc->ext_csd_valid = 0;
		sc->rca = 0;
		break;
	case MMC_SELECT_CARD:
		if ((cmd->arg >> 16) != 0)
			sc->rca = cmd->arg >> 16;
		break;
	case MMC_BUSTEST_W:
		if (data != NULL && (data->flags & MMC_DATA_WRITE) != 0) {
			cmd->error = MMC_ERR_NONE;
			req->done(req);
			return (0);
		}
		break;
	case MMC_BUSTEST_R:
		if (data != NULL && (data->flags & MMC_DATA_READ) != 0) {
			cmd->error = fchemmc_bus_test(sc, reqdev, data);
			req->done(req);
			return (0);
		}
		break;
	case MMC_SEND_EXT_CSD:
		if (data != NULL && (data->flags & MMC_DATA_READ) != 0 &&
		    data->len == MMC_EXTCSD_SIZE &&
		    sc->slot.host.ios.bus_width == bus_width_1) {
			sc->ext_csd_valid = 0;
			sc->snoop_done = req->done;
			sc->snoop_done_data = req->done_data;
			req->done = fchemmc_snoop_done;
			req->done_data = sc;
			err = sdhci_generic_request(brdev, reqdev, req);
			if (err != 0) {
				req->done = sc->snoop_done;
				req->done_data = sc->snoop_done_data;
			}
			return (err);
		}
		break;
	}
	return (sdhci_generic_request(brdev, reqdev, req));
}

static void fchemmc_intr(void *arg);

static int
fchemmc_probe(device_t dev)
{

	if (pci_get_devid(dev) != FCHEMMC_DEVID)
		return (ENXIO);
	if (fchemmc_disable != 0) {
		printf("fchemmc: hw.fchemmc.disable is set; "
		    "the eMMC controller is left to sdhci_pci\n");
		return (ENXIO);
	}
	if (fchemmc_kernel_matches() == 0) {
		printf("fchemmc: built for \"%s\", not for this kernel; "
		    "the eMMC controller is left to sdhci_pci\n",
		    FCHEMMC_KERNEL);
		return (ENXIO);
	}
	device_set_desc(dev, "AMD FCH SD/eMMC Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
fchemmc_attach(device_t dev)
{
	struct fchemmc_softc *sc = device_get_softc(dev);
	struct sdhci_slot *slot = &sc->slot;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *tree;
	int bar, err, rid, i;

	sc->dev = dev;
	device_printf(dev, "fchemmc, from os-xgs-npu (https://github.com/AbdelmonemAwad/os-xgs-npu): "
	    "Copyright (c) 2026 Abdelmonem Awad, BSD-2-Clause\n");
	sc->no_soft_reset = -1;
	sc->clear_tm = fchemmc_needs_clear_tm();
	/*
	 * No sdhci quirks of its own; hw.sdhci.quirk_set still applies, as in
	 * sdhci_pci (there is nothing for hw.sdhci.quirk_clear to take away).
	 */
	slot->quirks = sdhci_quirk_set;

	/* The controller's one slot, at the BAR its slot information names. */
	bar = PCI_SLOT_INFO_FIRST_BAR(pci_read_config(dev, PCI_SLOT_INFO, 1));
	if (bar > 5) {
		device_printf(dev, "Incorrect slot information (BAR %d).\n",
		    bar);
		return (EINVAL);
	}
	/* Allocate IRQ. */
	i = 1;
	rid = 0;
	if (fchemmc_enable_msi != 0 && pci_alloc_msi(dev, &i) == 0)
		rid = 1;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE | (rid != 0 ? 0 : RF_SHAREABLE));
	if (sc->irq_res == NULL) {
		device_printf(dev, "Can't allocate IRQ\n");
		pci_release_msi(dev);
		return (ENOMEM);
	}
	rid = PCIR_BAR(bar);
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Can't allocate memory for the slot\n");
		bus_release_resource(dev, SYS_RES_IRQ,
		    rman_get_rid(sc->irq_res), sc->irq_res);
		pci_release_msi(dev);
		return (ENOMEM);
	}
	if (sdhci_init_slot(dev, slot, 0) != 0) {
		device_printf(dev, "Can't set up the slot\n");
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->mem_res), sc->mem_res);
		bus_release_resource(dev, SYS_RES_IRQ,
		    rman_get_rid(sc->irq_res), sc->irq_res);
		pci_release_msi(dev);
		return (ENXIO);
	}
	sc->have_slot = 1;
	fchemmc_limit_slot(slot);
	device_printf(dev, "4-bit bus at most, %u MHz at most, no bus test%s\n",
	    slot->host.f_max / 1000000,
	    sc->clear_tm ? ", Transfer Mode cleared before commands" : "");

	ctx = device_get_sysctl_ctx(dev);
	tree = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO, "clear_transfer_mode", CTLFLAG_RD,
	    &sc->clear_tm, 0,
	    "Transfer Mode cleared before a command without data");
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO, "bus_test", CTLFLAG_RD,
	    &sc->bus_test, 0,
	    "Width EXT_CSD read back the same at (4), failed (1), not run (0)");
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO, "no_soft_reset", CTLFLAG_RD,
	    &sc->no_soft_reset, 0,
	    "PMCSR No_Soft_Reset bit: D3hot keeps state (1), resets it (0)");
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO, "d3_cycles", CTLFLAG_RD,
	    &sc->d3_cycles, 0, "Full resets through D3hot and back to D0");
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO, "d0_failed", CTLFLAG_RD,
	    &sc->d0_failed, 0, "Full resets after which D0 did not read back");

	/* Activate the interrupt */
	err = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, fchemmc_intr, sc, &sc->intrhand);
	if (err)
		device_printf(dev, "Can't setup IRQ\n");
	pci_enable_busmaster(dev);
	/* Process cards detection. */
	sdhci_start_slot(slot);

	return (0);
}

static int
fchemmc_detach(device_t dev)
{
	struct fchemmc_softc *sc = device_get_softc(dev);

	if (sc->intrhand != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->intrhand);
	bus_release_resource(dev, SYS_RES_IRQ,
	    rman_get_rid(sc->irq_res), sc->irq_res);
	pci_release_msi(dev);
	if (sc->have_slot) {
		sdhci_cleanup_slot(&sc->slot);
		sc->have_slot = 0;
	}
	bus_release_resource(dev, SYS_RES_MEMORY,
	    rman_get_rid(sc->mem_res), sc->mem_res);
	return (0);
}

static int
fchemmc_suspend(device_t dev)
{
	struct fchemmc_softc *sc = device_get_softc(dev);
	int err;

	err = bus_generic_suspend(dev);
	if (err)
		return (err);
	if (sc->have_slot)
		sdhci_generic_suspend(&sc->slot);
	return (0);
}

static int
fchemmc_resume(device_t dev)
{
	struct fchemmc_softc *sc = device_get_softc(dev);

	if (sc->have_slot)
		sdhci_generic_resume(&sc->slot);
	return (bus_generic_resume(dev));
}

static void
fchemmc_intr(void *arg)
{
	struct fchemmc_softc *sc = arg;

	if (sc->have_slot)
		sdhci_generic_intr(&sc->slot);
}

static device_method_t fchemmc_methods[] = {
	/* device_if */
	DEVMETHOD(device_probe,		fchemmc_probe),
	DEVMETHOD(device_attach,	fchemmc_attach),
	DEVMETHOD(device_detach,	fchemmc_detach),
	DEVMETHOD(device_suspend,	fchemmc_suspend),
	DEVMETHOD(device_resume,	fchemmc_resume),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	sdhci_generic_read_ivar),
	DEVMETHOD(bus_write_ivar,	sdhci_generic_write_ivar),

	/* mmcbr_if */
	DEVMETHOD(mmcbr_update_ios,	sdhci_generic_update_ios),
	DEVMETHOD(mmcbr_switch_vccq,	sdhci_generic_switch_vccq),
	DEVMETHOD(mmcbr_tune,		sdhci_generic_tune),
	DEVMETHOD(mmcbr_retune,		sdhci_generic_retune),
	DEVMETHOD(mmcbr_request,	fchemmc_request),
	DEVMETHOD(mmcbr_get_ro,		sdhci_generic_get_ro),
	DEVMETHOD(mmcbr_acquire_host,	sdhci_generic_acquire_host),
	DEVMETHOD(mmcbr_release_host,	sdhci_generic_release_host),

	/* SDHCI accessors */
	DEVMETHOD(sdhci_read_1,		fchemmc_read_1),
	DEVMETHOD(sdhci_read_2,		fchemmc_read_2),
	DEVMETHOD(sdhci_read_4,		fchemmc_read_4),
	DEVMETHOD(sdhci_read_multi_4,	fchemmc_read_multi_4),
	DEVMETHOD(sdhci_write_1,	fchemmc_write_1),
	DEVMETHOD(sdhci_write_2,	fchemmc_write_2),
	DEVMETHOD(sdhci_write_4,	fchemmc_write_4),
	DEVMETHOD(sdhci_write_multi_4,	fchemmc_write_multi_4),
	DEVMETHOD(sdhci_set_uhs_timing,	sdhci_generic_set_uhs_timing),
	DEVMETHOD(sdhci_reset,		fchemmc_reset),

	DEVMETHOD_END
};

static driver_t fchemmc_driver = {
	"fchemmc",
	fchemmc_methods,
	sizeof(struct fchemmc_softc),
};

DRIVER_MODULE(fchemmc, pci, fchemmc_driver, NULL, NULL);
MODULE_VERSION(fchemmc, 1);
SDHCI_DEPEND(fchemmc);
MMC_DECLARE_BRIDGE(fchemmc);
