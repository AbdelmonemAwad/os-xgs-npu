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
 *   xgs3300-sensors raw             the bank 0x00..0x3c as bytes
 *   xgs3300-sensors dump <base>     a controller's sixteen registers, read only
 *   xgs3300-sensors rbdata <base> <addr> <reg>
 *
 * The base is where i2c-piix4 would put it on a KERNCZ FCH: (byte 1 of the PM block at 0xfed80300)
 * << 8, 0x0b00 here; the auxiliary controller at base | 0x20 has nothing on its bus (measured).
 * Every transaction is bounded by a timeout and a stuck one is killed before the tool gives up.
 */
#include <sys/types.h>
#include <machine/cpufunc.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/* The vendor's arithmetic: a temperature is hi + lo / 8, a fan byte is in units of 300 rpm. */
static int
sensors(void)
{
	unsigned lo, hi, f0, f1, under, minfan, v0, v1, v2, b0, b1;

	if (reg(0x00, &v0) || reg(0x01, &v1) || reg(0x02, &v2) || reg(0x03, &b0) || reg(0x04, &b1))
		return (1);
	printf("cpld        version %02x%02x%02x  block id %02x%02x\n", v0, v1, v2, b1, b0);
	if (reg(0x07, &lo) || reg(0x08, &hi))
		return (1);
	printf("cpu         %.3f C\n", (double)hi + (double)lo * 0.125);
	if (reg(0x0a, &lo) || reg(0x09, &hi))
		return (1);
	printf("npu         %.3f C\n", (double)hi + (double)lo * 0.125);
	if (reg(0x0c, &lo) || reg(0x0b, &hi))
		return (1);
	printf("inlet       %.3f C\n", (double)hi + (double)lo * 0.125);
	if (reg(0x0d, &f0) || reg(0x0e, &f1) || reg(0x14, &under) || reg(0x16, &minfan))
		return (1);
	printf("fan 0       %u rpm%s\n", f0 * 300, under ? "  (under-speed flag set)" : "");
	printf("fan 1       %u rpm\n", f1 * 300);
	printf("fan minimum %u rpm\n", minfan * 300);
	return (0);
}

int
main(int argc, char **argv)
{
	unsigned a, r, v;
	int fd, i, rc;

	fd = open("/dev/io", O_RDWR);
	if (fd < 0) {
		perror("/dev/io");
		return (1);
	}
	if (argc < 2)
		return (sensors());
	if (strcmp(argv[1], "json") == 0) {
		/* One line for a program to read: the same readings, the same arithmetic. */
		unsigned lo, hi, f0, f1, under, minfan;
		double cpu, npu, inlet;

		if (reg(0x07, &lo) || reg(0x08, &hi))
			return (1);
		cpu = (double)hi + (double)lo * 0.125;
		if (reg(0x0a, &lo) || reg(0x09, &hi))
			return (1);
		npu = (double)hi + (double)lo * 0.125;
		if (reg(0x0c, &lo) || reg(0x0b, &hi))
			return (1);
		inlet = (double)hi + (double)lo * 0.125;
		if (reg(0x0d, &f0) || reg(0x0e, &f1) || reg(0x14, &under) || reg(0x16, &minfan))
			return (1);
		printf("{\"cpu_c\": %.3f, \"npu_c\": %.3f, \"inlet_c\": %.3f, \"fan0_rpm\": %u, "
		    "\"fan1_rpm\": %u, \"fan0_under\": %u, \"fan_min_rpm\": %u}\n",
		    cpu, npu, inlet, f0 * 300, f1 * 300, under, minfan * 300);
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
	fprintf(stderr, "usage: xgs3300-sensors [raw | dump <base> | rbdata <base> <addr> <reg>]\n");
	return (2);
}
