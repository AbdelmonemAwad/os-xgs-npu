/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * The XGS 3300's temperatures, fan speeds and flags, read from OPNsense.
 *
 * They are a register bank on the appliance's CPLD, behind an 8-bit SMBus face at slave 0x60 on
 * the host's own SMBus - the AMD FCH's main host controller, the port the firmware leaves selected.
 * FreeBSD's intsmb(4) attaches that controller, but a read-byte-data through it (smbmsg) returned
 * ENXIO on this FCH, and a receive-byte 0x02 from every address, which looked like an undriven bus
 * for a day. Driving the PIIX4-style register block directly, as this does over /dev/io, the same
 * controller acknowledges 0x60 alone and answers register by register. The transaction is the one
 * the vendor's own library makes (I2C_SMBUS_READ, BYTE_DATA), and the arithmetic is its too.
 *
 *   xgs3300-sensors                 the decoded readings (default base 0x0b00, slave 0x60)
 *   xgs3300-sensors json            the same, one line for a program
 *   xgs3300-sensors raw             the bank 0x00..0x3c as bytes
 *   xgs3300-sensors dump <base>     a controller's sixteen registers, read only
 *   xgs3300-sensors rbdata <base> <addr> <reg>
 *
 * Beside the temperatures and the fans, the bank holds four flags (2026-10-27): the reset flags 0
 * and 1 at 0x11 and 0x12 - the first is the reset button's -, the power supplies at 0x13 (bit 0 the
 * internal one, bit 1 the external) and fan 1's under-speed flag at 0x15, fan 0's being 0x14. Each
 * is its register's bit 0, as the vendor's sensor support reads them. Nothing here clears one.
 *
 * Before anything in it is believed, the bank has to answer with its block id, 0xb002, at 0x03 and
 * 0x04 - the check the vendor's own reader makes. The map was checked on CPLD 0x05000008, the
 * version on both XGS 3300s here; a board of an earlier revision may carry 0x04000002. The vendor's
 * sensor support reads the same registers on either, so another version is named in the output,
 * not refused.
 *
 * The base is where i2c-piix4 would put it on a KERNCZ FCH: (byte 1 of the PM block at 0xfed80300)
 * << 8, 0x0b00 here; the auxiliary controller at base | 0x20 has nothing on its bus (measured).
 * Every transaction is bounded by a timeout and a stuck one is killed before the tool gives up.
 */
#include <sys/types.h>
#include <sys/file.h>
#include <machine/cpufunc.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Held while the bus is in use, by every copy of this tool. */
#define LOCKFILE	"/var/run/xgs3300-sensors.lock"

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

#define CPLD_SLAVE	0x60
#define DEFAULT_BASE	0x0b00
#define BLOCK_ID	0xb002
#define CHECKED_CPLD	0x050008

static unsigned base = DEFAULT_BASE;

static int
rbdata(unsigned addr, unsigned reg, unsigned *out)
{
	unsigned sts;
	int i;

	if (inb(base + HST_STS) & STS_BUSY) {
		outb(base + HST_CNT, CNT_KILL);
		usleep(1000);
		outb(base + HST_STS, 0xff);
		if (inb(base + HST_STS) & STS_BUSY)
			return (-2);
	}
	outb(base + HST_STS, 0xff);
	outb(base + XMIT_SLVA, (addr << 1) | 1);
	outb(base + HST_CMD, reg);
	outb(base + HST_CNT, PROTO_BYTE_DATA | CNT_START);
	for (i = 0; i < 1000; i++) {
		usleep(50);
		sts = inb(base + HST_STS);
		if ((sts & STS_BUSY) == 0 && (sts & (STS_INTR | STS_DEV | STS_BUS | STS_FAIL)))
			break;
	}
	if (i == 1000) {
		outb(base + HST_CNT, CNT_KILL);
		usleep(1000);
		outb(base + HST_STS, 0xff);
		return (-3);
	}
	if (sts & (STS_DEV | STS_BUS | STS_FAIL)) {
		outb(base + HST_STS, 0xff);
		return (sts & STS_DEV) ? 1 : (sts & STS_BUS) ? 2 : 3;
	}
	*out = inb(base + HST_DAT0);
	outb(base + HST_STS, 0xff);
	return (0);
}

static const char *
why(int rc)
{
	return (rc == 0 ? "ok" : rc == 1 ? "no device" : rc == 2 ? "bus collision" :
	    rc == 3 ? "failed" : "controller stuck");
}

static int
reg(unsigned r, unsigned *v)
{
	int rc;

	rc = rbdata(CPLD_SLAVE, r, v);
	if (rc != 0)
		fprintf(stderr, "register 0x%02x: %s\n", r, why(rc));
	return (rc);
}

/* One reading of the bank, decoded. */
struct bank {
	unsigned	cpld;		/* the version's three bytes, 0x050008 */
	double		cpu, npu, inlet;
	unsigned	fan0, fan1, fan_min;	/* rpm */
	unsigned	fan0_under, fan1_under;
	unsigned	psu_internal, psu_external;	/* 1 good */
	unsigned	reset0, reset1;
};

/*
 * The vendor's arithmetic: a temperature is hi + lo / 8, a fan byte is in units of 300 rpm.
 * Returns 0, 1 when a register did not answer, or 3 when the device is not the bank.
 */
static int
readbank(struct bank *k)
{
	unsigned lo, hi, v0, v1, v2, b0, b1, r;

	if (reg(0x00, &v0) || reg(0x01, &v1) || reg(0x02, &v2) || reg(0x03, &b0) || reg(0x04, &b1))
		return (1);
	if ((b1 << 8 | b0) != BLOCK_ID) {
		fprintf(stderr, "block id 0x%02x%02x at slave 0x%02x, not 0x%04x: not the sensor bank\n",
		    b1, b0, CPLD_SLAVE, BLOCK_ID);
		return (3);
	}
	k->cpld = v0 << 16 | v1 << 8 | v2;
	if (reg(0x07, &lo) || reg(0x08, &hi))
		return (1);
	k->cpu = (double)hi + (double)lo * 0.125;
	if (reg(0x0a, &lo) || reg(0x09, &hi))
		return (1);
	k->npu = (double)hi + (double)lo * 0.125;
	if (reg(0x0c, &lo) || reg(0x0b, &hi))
		return (1);
	k->inlet = (double)hi + (double)lo * 0.125;
	if (reg(0x0d, &k->fan0) || reg(0x0e, &k->fan1) || reg(0x16, &k->fan_min))
		return (1);
	k->fan0 *= 300;
	k->fan1 *= 300;
	k->fan_min *= 300;
	if (reg(0x14, &r))
		return (1);
	k->fan0_under = r & 1;
	if (reg(0x15, &r))
		return (1);
	k->fan1_under = r & 1;
	if (reg(0x13, &r))
		return (1);
	k->psu_internal = r & 1;
	k->psu_external = (r >> 1) & 1;
	if (reg(0x11, &r))
		return (1);
	k->reset0 = r & 1;
	if (reg(0x12, &r))
		return (1);
	k->reset1 = r & 1;
	return (0);
}

static int
sensors(void)
{
	struct bank k;
	int rc;

	if ((rc = readbank(&k)) != 0)
		return (rc);
	printf("cpld        version %06x  block id %04x%s\n", k.cpld, BLOCK_ID,
	    k.cpld == CHECKED_CPLD ? "" : "  (the map was checked on 050008)");
	printf("cpu         %.3f C\n", k.cpu);
	printf("npu         %.3f C\n", k.npu);
	printf("inlet       %.3f C\n", k.inlet);
	printf("fan 0       %u rpm%s\n", k.fan0, k.fan0_under ? "  (under-speed flag set)" : "");
	printf("fan 1       %u rpm%s\n", k.fan1, k.fan1_under ? "  (under-speed flag set)" : "");
	printf("fan minimum %u rpm\n", k.fan_min);
	printf("power       internal %s, external %s\n", k.psu_internal ? "good" : "not good",
	    k.psu_external ? "good" : "not good or absent");
	printf("reset flags 0 %s, 1 %s\n", k.reset0 ? "set (the reset button)" : "clear",
	    k.reset1 ? "set" : "clear");
	return (0);
}

int
main(int argc, char **argv)
{
	unsigned a, r, v;
	int fd, i, lk, rc;

	/*
	 * One reader at a time. os-fanctl and os-xgs-npu's temperature feeder both run this, each on
	 * its own clock, and two transactions interleaved on one SMBus controller read garbage. The
	 * lock is held until exit; a reader that cannot get it within a few seconds gives up.
	 */
	lk = open(LOCKFILE, O_RDWR | O_CREAT, 0600);
	if (lk >= 0) {
		for (i = 0; flock(lk, LOCK_EX | LOCK_NB) != 0; i++) {
			if (i >= 50) {
				fprintf(stderr, "%s: held by another reader\n", LOCKFILE);
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
	if (argc < 2)
		return (sensors());
	if (strcmp(argv[1], "json") == 0) {
		/* One line for a program to read: the same readings, the same arithmetic. */
		struct bank k;

		if ((rc = readbank(&k)) != 0)
			return (rc);
		printf("{\"cpu_c\": %.3f, \"npu_c\": %.3f, \"inlet_c\": %.3f, \"fan0_rpm\": %u, "
		    "\"fan1_rpm\": %u, \"fan0_under\": %u, \"fan1_under\": %u, \"fan_min_rpm\": %u, "
		    "\"psu_internal_good\": %u, \"psu_external_good\": %u, \"reset_flag0\": %u, "
		    "\"reset_flag1\": %u, \"cpld\": \"%06x\", \"cpld_checked\": %u}\n",
		    k.cpu, k.npu, k.inlet, k.fan0, k.fan1, k.fan0_under, k.fan1_under, k.fan_min,
		    k.psu_internal, k.psu_external, k.reset0, k.reset1, k.cpld,
		    (unsigned)(k.cpld == CHECKED_CPLD));
		return (0);
	}
	if (strcmp(argv[1], "raw") == 0) {
		for (r = 0; r <= 0x3c; r++) {
			if (r % 16 == 0)
				printf("%s0x%02x:", r ? "\n" : "", r);
			rc = rbdata(CPLD_SLAVE, r, &v);
			if (rc == 0)
				printf(" %02x", v);
			else
				printf(" --");
		}
		printf("\n");
		return (0);
	}
	if (argc >= 3)
		base = strtoul(argv[2], NULL, 0);
	if (strcmp(argv[1], "dump") == 0) {
		printf("io 0x%04x:", base);
		for (i = 0; i < 16; i++)
			printf(" %02x", inb(base + i));
		printf("\n");
		return (0);
	}
	if (strcmp(argv[1], "rbdata") == 0 && argc >= 5) {
		a = strtoul(argv[3], NULL, 0);
		r = strtoul(argv[4], NULL, 0);
		rc = rbdata(a, r, &v);
		if (rc == 0)
			printf("0x%02x[0x%02x] = 0x%02x\n", a, r, v);
		else
			printf("0x%02x[0x%02x]: %s\n", a, r, why(rc));
		return (rc != 0);
	}
	fprintf(stderr, "usage: xgs3300-sensors [json | raw | dump <base> | rbdata <base> <addr> <reg>]\n");
	return (2);
}
