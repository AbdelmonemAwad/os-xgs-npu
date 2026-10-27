/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The status light on the front of the XGS 126 and the XGS 136, read and set from OPNsense (#735).
 *
 * The vendor's appliance table gives these models STATUS_LED=I2C: the light is two registers of the
 * board's controller at slave 0x40 on the host's auxiliary SMBus controller - the AMD FCH's second
 * PIIX4-style block, I/O 0x0b20 - 0xf2 the green, 0xf3 the red, each 0x01 lit, 0x51 blinking and
 * 0x00 dark; a blinking one can read back as 0x29. The board's own start leaves the green blinking,
 * and the vendor's software lights it at the end of its boot. Under OPNsense nothing did, so the
 * light blinked for as long as the appliance ran: read on the XGS 136 on 2026-10-27, 0xf2 0x29 and
 * 0xf3 0x00.
 *
 * The same controller holds other registers, a factory-reset flag among them, so this writes those
 * two registers and those three values only, whatever it is asked, and writes nothing to a bus
 * where the controller does not answer a read of 0xf2 first. The SMBus transaction is the one
 * tools/sensors/xgs3300-sensors.c makes on the XGS 3300's main controller, a write added.
 *
 *   xgs-statusled                       what the light shows
 *   xgs-statusled green|red             lit, the other colour dark
 *   xgs-statusled blink-green|blink-red
 *   xgs-statusled off
 */
#include <sys/types.h>
#include <sys/file.h>
#include <machine/cpufunc.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define LOCKFILE	"/var/run/xgs-statusled.lock"
#define BASE		0x0b20
#define SLAVE		0x40
#define REG_GREEN	0xf2
#define REG_RED		0xf3
#define LIT		0x01
#define BLINK		0x51
#define DARK		0x00

#define HST_STS		0
#define HST_CNT		2
#define HST_CMD		3
#define XMIT_SLVA	4
#define HST_DAT0	5

#define STS_BUSY	0x01
#define STS_INTR	0x02
#define STS_DEV		0x04
#define STS_BUS		0x08
#define STS_FAIL	0x10
#define CNT_START	0x40
#define CNT_KILL	0x02
#define PROTO_BYTE_DATA	0x08

/* One byte-data transaction, a read (*v filled) or a write (*v sent); 0 when it went through. */
static int
xfer(int write, unsigned reg, unsigned *v)
{
	unsigned sts;
	int i;

	if (inb(BASE + HST_STS) & STS_BUSY) {
		outb(BASE + HST_CNT, CNT_KILL);
		usleep(1000);
		outb(BASE + HST_STS, 0xff);
		if (inb(BASE + HST_STS) & STS_BUSY)
			return (-2);
	}
	outb(BASE + HST_STS, 0xff);
	outb(BASE + XMIT_SLVA, (SLAVE << 1) | (write ? 0 : 1));
	outb(BASE + HST_CMD, reg);
	if (write)
		outb(BASE + HST_DAT0, *v);
	outb(BASE + HST_CNT, PROTO_BYTE_DATA | CNT_START);
	for (i = 0; i < 1000; i++) {
		usleep(50);
		sts = inb(BASE + HST_STS);
		if ((sts & STS_BUSY) == 0 && (sts & (STS_INTR | STS_DEV | STS_BUS | STS_FAIL)))
			break;
	}
	if (i == 1000) {
		outb(BASE + HST_CNT, CNT_KILL);
		usleep(1000);
		outb(BASE + HST_STS, 0xff);
		return (-3);
	}
	if (sts & (STS_DEV | STS_BUS | STS_FAIL)) {
		outb(BASE + HST_STS, 0xff);
		return ((sts & STS_DEV) ? 1 : (sts & STS_BUS) ? 2 : 3);
	}
	if (!write)
		*v = inb(BASE + HST_DAT0);
	outb(BASE + HST_STS, 0xff);
	return (0);
}

/* The only writes there are: one of the two registers, one of the three values. */
static int
set(unsigned reg, unsigned value)
{
	if ((reg != REG_GREEN && reg != REG_RED) || (value != LIT && value != BLINK && value != DARK))
		return (-1);
	return (xfer(1, reg, &value));
}

static const char *
shows(unsigned v)
{
	return (v == DARK ? "dark" : v == LIT ? "lit" : (v == BLINK || v == 0x29) ? "blinking" : "unknown");
}

int
main(int argc, char **argv)
{
	unsigned g, r, first, second, value;
	int fd, i, lk;

	lk = open(LOCKFILE, O_RDWR | O_CREAT, 0600);
	if (lk >= 0) {
		for (i = 0; flock(lk, LOCK_EX | LOCK_NB) != 0; i++) {
			if (i >= 50) {
				fprintf(stderr, "%s: held by another caller\n", LOCKFILE);
				return (1);
			}
			usleep(100000);
		}
	}
	fd = open("/dev/io", O_RDWR);
	if (fd < 0) {
		perror("/dev/io");
		return (1);
	}
	/* The controller has to answer before anything is written to it. */
	if (xfer(0, REG_GREEN, &g) != 0 || xfer(0, REG_RED, &r) != 0) {
		fprintf(stderr, "no status light controller at 0x%02x on SMBus 0x%04x: nothing written\n",
		    SLAVE, BASE);
		return (1);
	}
	if (argc < 2) {
		printf("green %s (0x%02x), red %s (0x%02x)\n", shows(g), g, shows(r), r);
		return (0);
	}
	/* The colour going dark first, then the one lit: the order the vendor's own script writes. */
	if (strcmp(argv[1], "green") == 0 || strcmp(argv[1], "blink-green") == 0) {
		first = REG_RED;
		second = REG_GREEN;
	} else if (strcmp(argv[1], "red") == 0 || strcmp(argv[1], "blink-red") == 0) {
		first = REG_GREEN;
		second = REG_RED;
	} else if (strcmp(argv[1], "off") == 0) {
		first = REG_RED;
		second = REG_GREEN;
	} else {
		fprintf(stderr, "usage: xgs-statusled [green | red | blink-green | blink-red | off]\n");
		return (2);
	}
	value = strcmp(argv[1], "off") == 0 ? DARK : strncmp(argv[1], "blink-", 6) == 0 ? BLINK : LIT;
	if (set(first, DARK) != 0 || set(second, value) != 0) {
		fprintf(stderr, "the status light did not take it\n");
		return (1);
	}
	if (xfer(0, REG_GREEN, &g) != 0 || xfer(0, REG_RED, &r) != 0)
		return (1);
	printf("green %s (0x%02x), red %s (0x%02x)\n", shows(g), g, shows(r), r);
	return (0);
}
