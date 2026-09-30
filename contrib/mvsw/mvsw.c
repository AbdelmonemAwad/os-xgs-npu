/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * mvsw - talk to the Marvell switch behind an XGS appliance's panel ports, from the coprocessor.
 *
 * WHY THIS EXISTS. The switch is reachable from the coprocessor and only from there, and the first
 * way it was reached was a shell loop driving the SMI controller's registers through busybox
 * `devmem` over a 115200 console. That works for a read or two and is not good enough for anything
 * else: roughly half the reads came back wrong - a register reading 0x007f on one pass and 0x0000
 * on the next - because nothing was polling the controller's busy bit properly between operations.
 * A read-modify-write built on a bad read then wrote 0 | 3 into a port's control register and took
 * down a working gigabit link.
 *
 * `/dev/mvmdio-uio` removes the whole problem. It is a character device from the vendor's own
 * `mvmdio_uio` module, and its read and write paths call the kernel's `mdiobus_read` and
 * `mdiobus_write` - so the controller handshake is the kernel's business rather than a shell
 * loop's. The interface is one struct, passed in the buffer of a read (which fills in `data`) or of
 * a write:
 *
 *	struct { int bus_id; int phy_id; int reg; unsigned short data; }
 *
 * A read must have that buffer already carrying bus, phy and reg, because the driver copies the
 * request out of it before it does anything. That is why this cannot be `dd`, a shell, or the
 * coprocessor's Lua: all of them hand the kernel a buffer whose contents they do not control.
 *
 * WHY IT HAS NO LIBC. The appliance's coprocessor runs glibc 2.27 and has a `cc` with no `cc1`, so
 * nothing can be built there; the only cross toolchain available targets a far newer glibc, whose
 * dynamic binaries will not load there and whose static ones are seven hundred kilobytes - and the
 * only way onto the box is base64 through that same 115200 console. Freestanding, this is a few
 * kilobytes: five system calls and a hex printer, and it runs on any aarch64 Linux.
 *
 *	aarch64-linux-gnu-gcc -Os -nostdlib -static -o mvsw mvsw.c
 *
 * WHAT IT KNOWS ABOUT THE SWITCH, and every number came out of the vendor's own sources rather than
 * a datasheet or a guess:
 *
 *   - The board file names the addressing, in the format the BSP's own parser defines:
 *     `npu0.device1.mdio=mdio22:0:2` is clause 22, MDIO bus 0, SMI address 2.
 *   - One SMI address means MULTI-CHIP addressing: register 0 at that address is a command and
 *     register 1 is its data. From umsd's include/driver/msdHwAccess.h: busy 0x8000, clause at bit
 *     12, operation at bit 10 (2 read, 1 write), device at bit 5, register at bit 0.
 *   - The internal PHYs are reached the same way again, through Global2 (device 0x1C) registers
 *     0x18 and 0x19, the SMI PHY Command and Data.
 *   - A 1G copper port needs exactly two things, which is what the vendor's own `umsd_port_up`
 *     does: the port control register's bits [1:0] set to 3, and bit 11 of the port's internal PHY
 *     register 0 cleared. The PHY address is the port number.
 *
 * THE PAGE IS THE TRAP. Reading a copper PHY returns flat zeros - not 0xffff, which would mean
 * nobody home - until register 22 is set to page 0. The vendor's routine saves register 22, does
 * its paged write and puts the page back, and that is why. An hour went into concluding there was
 * no PHY there at all before the page was tried.
 */

typedef unsigned long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef long ssize_t_;

#define	AT_FDCWD	-100
#define	O_RDWR		2

#define	SYS_openat	56
#define	SYS_close	57
#define	SYS_read	63
#define	SYS_write	64
#define	SYS_exit_group	94

static long
syscall4(long n, long a, long b, long c, long d)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;

	__asm__ __volatile__("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
	return (x0);
}

static long	sys_read(int fd, void *p, u64 n)  { return (syscall4(SYS_read, fd, (long)p, n, 0)); }
static long	sys_write(int fd, const void *p, u64 n) { return (syscall4(SYS_write, fd, (long)p, n, 0)); }
static long	sys_close(int fd)		 { return (syscall4(SYS_close, fd, 0, 0, 0)); }
static long	sys_open(const char *p, int fl)	 { return (syscall4(SYS_openat, AT_FDCWD, (long)p, fl, 0)); }
static void	sys_exit(int c)			 { syscall4(SYS_exit_group, c, 0, 0, 0); for (;;) ; }

/* ------------------------------------------------------------------ the smallest possible output */

static void
out(const char *s)
{
	u64 n = 0;

	while (s[n] != '\0')
		n++;
	(void)sys_write(1, s, n);
}

static void
outhex(u16 v)
{
	static const char d[] = "0123456789abcdef";
	char b[7];

	b[0] = '0'; b[1] = 'x';
	b[2] = d[(v >> 12) & 0xf];
	b[3] = d[(v >> 8) & 0xf];
	b[4] = d[(v >> 4) & 0xf];
	b[5] = d[v & 0xf];
	b[6] = '\0';
	out(b);
}

static void
outdec(int v)
{
	char b[12];
	int i = 11;

	b[i] = '\0';
	if (v == 0)
		b[--i] = '0';
	while (v > 0) {
		b[--i] = (char)('0' + v % 10);
		v /= 10;
	}
	out(&b[i]);
}

/* Accepts decimal, or hex with a 0x prefix. Returns -1 on anything it does not understand. */
static int
parse(const char *s)
{
	int v = 0, base = 10;

	if (s == 0 || s[0] == '\0')
		return (-1);
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		s += 2;
	}
	while (*s != '\0') {
		int d;

		if (*s >= '0' && *s <= '9')
			d = *s - '0';
		else if (base == 16 && *s >= 'a' && *s <= 'f')
			d = *s - 'a' + 10;
		else if (base == 16 && *s >= 'A' && *s <= 'F')
			d = *s - 'A' + 10;
		else
			return (-1);
		v = v * base + d;
		s++;
	}
	return (v);
}

static int
streq(const char *a, const char *b)
{
	while (*a != '\0' && *a == *b) {
		a++;
		b++;
	}
	return (*a == *b);
}

/* ------------------------------------------------------------------------------- the device */

#define	MVMDIO_DEV	"/dev/mvmdio-uio"

/* The driver's own struct, copied from mvmdio_uio.c so the layout cannot drift. */
struct mii_data {
	int	bus_id;
	int	phy_id;
	int	reg;
	u16	data;
};

/* umsd include/driver/msdHwAccess.h */
#define	SMI_BUSY	0x8000
#define	SMI_MODE_BIT	12
#define	SMI_OP_BIT	10
#define	SMI_DEV_BIT	5
#define	SMI_CLAUSE22	1
#define	SMI_OP_WRITE	1
#define	SMI_OP_READ22	2

/* umsd dev/amethyst/include/driver/Amethyst_msdDrvSwRegs.h */
#define	GLOBAL2_DEV		0x1C
#define	REG_SMI_PHY_CMD		0x18
#define	REG_SMI_PHY_DATA	0x19
#define	REG_PORT_STATUS		0x00
#define	REG_SWITCH_ID		0x03
#define	REG_PORT_CONTROL	0x04
#define	REG_PORT_VLAN_MAP	0x06

#define	PHY_CONTROL_REG		0
#define	PHY_PAGE_REG		22
#define	PHY_POWER_DOWN		(1u << 11)

#define	PORT_STATE_MASK		0x3
#define	PORT_FORWARDING		3
#define	PORT_STATUS_LINK	(1u << 11)

static int fd = -1;
static int bus;			/* the MDIO bus the switch is on */
static int smi;			/* the switch's own SMI address */

static int
mdio_read(int phy, int reg, u16 *outv)
{
	struct mii_data m;

	m.bus_id = bus;
	m.phy_id = phy;
	m.reg = reg;
	m.data = 0;
	if (sys_read(fd, &m, sizeof(m)) != (long)sizeof(m)) {
		out("mdio read failed\n");
		return (-1);
	}
	*outv = m.data;
	return (0);
}

static int
mdio_write(int phy, int reg, u16 val)
{
	struct mii_data m;

	m.bus_id = bus;
	m.phy_id = phy;
	m.reg = reg;
	m.data = val;
	if (sys_write(fd, &m, sizeof(m)) != (long)sizeof(m)) {
		out("mdio write failed\n");
		return (-1);
	}
	return (0);
}

/*
 * Wait for the switch's own command register to go idle. The kernel serialises the MDIO controller;
 * this is a different busy bit, inside the switch, and it is the one the shell version never waited
 * on properly.
 */
static int
sw_wait(void)
{
	u16 v;
	int i;

	for (i = 0; i < 1000; i++) {
		if (mdio_read(smi, 0, &v) != 0)
			return (-1);
		if ((v & SMI_BUSY) == 0)
			return (0);
	}
	out("switch command register stayed busy\n");
	return (-1);
}

static int
sw_read(int dev, int reg, u16 *outv)
{
	u16 cmd;

	if (sw_wait() != 0)
		return (-1);
	cmd = (u16)(SMI_BUSY | (SMI_CLAUSE22 << SMI_MODE_BIT) | (SMI_OP_READ22 << SMI_OP_BIT) |
	    ((dev & 0x1f) << SMI_DEV_BIT) | (reg & 0x1f));
	if (mdio_write(smi, 0, cmd) != 0)
		return (-1);
	if (sw_wait() != 0)
		return (-1);
	return (mdio_read(smi, 1, outv));
}

static int
sw_write(int dev, int reg, u16 val)
{
	u16 cmd;

	if (sw_wait() != 0)
		return (-1);
	if (mdio_write(smi, 1, val) != 0)
		return (-1);
	cmd = (u16)(SMI_BUSY | (SMI_CLAUSE22 << SMI_MODE_BIT) | (SMI_OP_WRITE << SMI_OP_BIT) |
	    ((dev & 0x1f) << SMI_DEV_BIT) | (reg & 0x1f));
	if (mdio_write(smi, 0, cmd) != 0)
		return (-1);
	return (sw_wait());
}

/* The internal PHYs, through Global2's SMI PHY command and data. */
static int
phy_wait(void)
{
	u16 v;
	int i;

	for (i = 0; i < 1000; i++) {
		if (sw_read(GLOBAL2_DEV, REG_SMI_PHY_CMD, &v) != 0)
			return (-1);
		if ((v & SMI_BUSY) == 0)
			return (0);
	}
	out("SMI PHY command register stayed busy\n");
	return (-1);
}

static int
phy_read(int phy, int reg, u16 *outv)
{
	u16 cmd;

	if (phy_wait() != 0)
		return (-1);
	cmd = (u16)(SMI_BUSY | (SMI_CLAUSE22 << SMI_MODE_BIT) | (SMI_OP_READ22 << SMI_OP_BIT) |
	    ((phy & 0x1f) << SMI_DEV_BIT) | (reg & 0x1f));
	if (sw_write(GLOBAL2_DEV, REG_SMI_PHY_CMD, cmd) != 0)
		return (-1);
	if (phy_wait() != 0)
		return (-1);
	return (sw_read(GLOBAL2_DEV, REG_SMI_PHY_DATA, outv));
}

static int
phy_write(int phy, int reg, u16 val)
{
	u16 cmd;

	if (phy_wait() != 0)
		return (-1);
	if (sw_write(GLOBAL2_DEV, REG_SMI_PHY_DATA, val) != 0)
		return (-1);
	cmd = (u16)(SMI_BUSY | (SMI_CLAUSE22 << SMI_MODE_BIT) | (SMI_OP_WRITE << SMI_OP_BIT) |
	    ((phy & 0x1f) << SMI_DEV_BIT) | (reg & 0x1f));
	if (sw_write(GLOBAL2_DEV, REG_SMI_PHY_CMD, cmd) != 0)
		return (-1);
	return (phy_wait());
}

/*
 * A paged PHY access: save the page, do the work on the page asked for, put the page back. Register
 * 0 reads as flat zero from whatever page the switch happened to leave selected, which is the trap
 * this whole comment block exists for.
 */
static int
phy_read_paged(int phy, int page, int reg, u16 *outv)
{
	u16 saved;

	if (phy_read(phy, PHY_PAGE_REG, &saved) != 0)
		return (-1);
	if (phy_write(phy, PHY_PAGE_REG, (u16)page) != 0)
		return (-1);
	if (phy_read(phy, reg, outv) != 0)
		return (-1);
	return (phy_write(phy, PHY_PAGE_REG, saved));
}

static int
phy_write_paged(int phy, int page, int reg, u16 val)
{
	u16 saved;

	if (phy_read(phy, PHY_PAGE_REG, &saved) != 0)
		return (-1);
	if (phy_write(phy, PHY_PAGE_REG, (u16)page) != 0)
		return (-1);
	if (phy_write(phy, reg, val) != 0)
		return (-1);
	return (phy_write(phy, PHY_PAGE_REG, saved));
}

/*
 * Bring a copper port up or down: the vendor's umsd_port_up for a 1G port, which is the bridging
 * state and then the PHY's power. Read-modify-write on both, because only some of the bits are
 * ours - and now that the reads are reliable, read-modify-write is safe again.
 */
static int
port_up(int port, int up)
{
	u16 v;

	if (sw_read(port, REG_PORT_CONTROL, &v) != 0)
		return (-1);
	v = (u16)((v & ~PORT_STATE_MASK) | (up ? PORT_FORWARDING : 0));
	if (sw_write(port, REG_PORT_CONTROL, v) != 0)
		return (-1);

	if (phy_read_paged(port, 0, PHY_CONTROL_REG, &v) != 0)
		return (-1);
	v = (u16)(up ? (v & ~PHY_POWER_DOWN) : (v | PHY_POWER_DOWN));
	return (phy_write_paged(port, 0, PHY_CONTROL_REG, v));
}

/*
 * Clause 45, for the SERDES ports.
 *
 * The two SFP cages are switch ports 9 and 10, and they have no copper PHY - so the page-and-power
 * sequence above does not reach them. The vendor's umsd_port_up takes its other branch for these:
 * three read-modify-writes on clause-45 device 4, which is what wakes the SERDES.
 *
 * The command word is the same register as clause 22, built differently. From
 * Amethyst_msdGetSMIC45PhyReg_MultiChip: the register address goes into the DATA register and the
 * command's low field carries the clause-45 DEVICE number instead, with the mode bit clear -
 * MSD_SMI_CLAUSE45 is 0, so these words are 0x8000-based where the clause-22 ones are 0x9800.
 * An access is two commands: WRITE_ADDR to set the address, then READ_45 or WRITE.
 */
#define	SMI_OP_WRITE_ADDR	0
#define	SMI_OP_READ45		3

static int
c45_addr(int phy, int dev, u16 reg)
{
	u16 cmd;

	if (phy_wait() != 0)
		return (-1);
	if (sw_write(GLOBAL2_DEV, REG_SMI_PHY_DATA, reg) != 0)
		return (-1);
	cmd = (u16)(SMI_BUSY | (SMI_OP_WRITE_ADDR << SMI_OP_BIT) |
	    ((phy & 0x1f) << SMI_DEV_BIT) | (dev & 0x1f));
	if (sw_write(GLOBAL2_DEV, REG_SMI_PHY_CMD, cmd) != 0)
		return (-1);
	return (phy_wait());
}

static int
c45_read(int phy, int dev, u16 reg, u16 *outv)
{
	u16 cmd;

	if (c45_addr(phy, dev, reg) != 0)
		return (-1);
	cmd = (u16)(SMI_BUSY | (SMI_OP_READ45 << SMI_OP_BIT) |
	    ((phy & 0x1f) << SMI_DEV_BIT) | (dev & 0x1f));
	if (sw_write(GLOBAL2_DEV, REG_SMI_PHY_CMD, cmd) != 0)
		return (-1);
	if (phy_wait() != 0)
		return (-1);
	return (sw_read(GLOBAL2_DEV, REG_SMI_PHY_DATA, outv));
}

static int
c45_write(int phy, int dev, u16 reg, u16 val)
{
	u16 cmd;

	if (c45_addr(phy, dev, reg) != 0)
		return (-1);
	if (sw_write(GLOBAL2_DEV, REG_SMI_PHY_DATA, val) != 0)
		return (-1);
	cmd = (u16)(SMI_BUSY | (SMI_OP_WRITE << SMI_OP_BIT) |
	    ((phy & 0x1f) << SMI_DEV_BIT) | (dev & 0x1f));
	if (sw_write(GLOBAL2_DEV, REG_SMI_PHY_CMD, cmd) != 0)
		return (-1);
	return (phy_wait());
}

static int
c45_rmw(int phy, int dev, u16 reg, u16 data, u16 mask)
{
	u16 v;

	if (c45_read(phy, dev, reg, &v) != 0)
		return (-1);
	v = (u16)((v & ~mask) | (data & mask));
	return (c45_write(phy, dev, reg, v));
}

/*
 * Wake a SERDES port, in the vendor's own order and with its own registers: the SERDES block's
 * power, then the receive and transmit lanes, then the 1000BASE control register's power-down bit.
 * `value` is 0 for up throughout, which is why each step clears rather than sets.
 */
#define	SERDES_DEV		4
#define	SERDES_BLOCK_REG	0xf002
#define	SERDES_LANE_REG		0xf003
#define	PHY_1000BASE_CONTROL	0x2000

static int
serdes_up(int port, int up)
{
	u16 v = up ? 0 : 1;

	if (c45_rmw(port, SERDES_DEV, SERDES_BLOCK_REG,
	    (u16)(0x8000u | (v << 5)), (u16)(0x8000u | (1u << 5))) != 0)
		return (-1);
	v = up ? 0 : 3;
	if (c45_rmw(port, SERDES_DEV, SERDES_LANE_REG,
	    (u16)(v << 8), (u16)((1u << 8) | (1u << 9))) != 0)
		return (-1);
	v = up ? 0 : 1;
	return (c45_rmw(port, SERDES_DEV, PHY_1000BASE_CONTROL,
	    (u16)(v << 11), (u16)(1u << 11)));
}

static void
dump(int first, int last)
{
	u16 ctl, sts, vmap, phy0;
	int p;

	out("port  control  status   vlanmap  phy0     state\n");
	for (p = first; p <= last; p++) {
		if (sw_read(p, REG_PORT_CONTROL, &ctl) != 0 ||
		    sw_read(p, REG_PORT_STATUS, &sts) != 0 ||
		    sw_read(p, REG_PORT_VLAN_MAP, &vmap) != 0)
			continue;
		if (phy_read_paged(p, 0, PHY_CONTROL_REG, &phy0) != 0)
			phy0 = 0;
		out("  ");
		outdec(p);
		out(p < 10 ? "    " : "   ");
		outhex(ctl); out("   ");
		outhex(sts); out("   ");
		outhex(vmap); out("   ");
		outhex(phy0); out("   ");
		out((ctl & PORT_STATE_MASK) == PORT_FORWARDING ? "forwarding" : "disabled");
		out((sts & PORT_STATUS_LINK) ? ", link" : ", no link");
		out((phy0 & PHY_POWER_DOWN) ? ", phy off" : "");
		out("\n");
	}
}

static void
usage(void)
{

	out("usage: mvsw <bus> <smi> <command>\n"
	    "  id                             the switch identifier\n"
	    "  dump [first last]              every port's control, status, VLAN map and PHY\n"
	    "  read  <dev> <reg>              a switch register\n"
	    "  write <dev> <reg> <val>        a switch register\n"
	    "  phyr  <phy> <page> <reg>       an internal PHY register\n"
	    "  phyw  <phy> <page> <reg> <val> an internal PHY register\n"
	    "  up    <port>                   bring a copper port up\n"
	    "  down  <port>                   and down again\n"
	    "  c45r  <phy> <dev> <reg>        a clause-45 register, for the SERDES ports\n"
	    "  c45w  <phy> <dev> <reg> <val>\n"
	    "  sup   <port>                   wake an SFP cage's SERDES and forward\n"
	    "  sdown <port>                   and put it back to sleep\n"
	    "\n"
	    "On an XGS 3300 the switch is bus 0, SMI address 2 - the board file's\n"
	    "npu0.device1.mdio entry reads mdio22:0:2.\n");
}

static int
run(int argc, char **argv)
{
	u16 v;
	int rc = 0;

	if (argc < 4) {
		usage();
		return (2);
	}
	bus = parse(argv[1]);
	smi = parse(argv[2]);
	if (bus < 0 || smi < 0) {
		usage();
		return (2);
	}

	fd = (int)sys_open(MVMDIO_DEV, O_RDWR);
	if (fd < 0) {
		out("cannot open " MVMDIO_DEV "\n");
		return (1);
	}

	if (streq(argv[3], "id")) {
		if ((rc = sw_read(0, REG_SWITCH_ID, &v)) == 0) {
			out("switch id ");
			outhex(v);
			out(" - device ");
			outhex((u16)(v >> 4));
			out(" revision ");
			outdec(v & 0xf);
			out("\n");
		}
	} else if (streq(argv[3], "dump")) {
		dump(argc > 5 ? parse(argv[4]) : 0, argc > 5 ? parse(argv[5]) : 10);
	} else if (streq(argv[3], "read") && argc >= 6) {
		if ((rc = sw_read(parse(argv[4]), parse(argv[5]), &v)) == 0) {
			outhex(v);
			out("\n");
		}
	} else if (streq(argv[3], "write") && argc >= 7) {
		rc = sw_write(parse(argv[4]), parse(argv[5]), (u16)parse(argv[6]));
	} else if (streq(argv[3], "phyr") && argc >= 7) {
		if ((rc = phy_read_paged(parse(argv[4]), parse(argv[5]), parse(argv[6]), &v)) == 0) {
			outhex(v);
			out("\n");
		}
	} else if (streq(argv[3], "phyw") && argc >= 8) {
		rc = phy_write_paged(parse(argv[4]), parse(argv[5]), parse(argv[6]),
		    (u16)parse(argv[7]));
	} else if (streq(argv[3], "c45r") && argc >= 7) {
		if ((rc = c45_read(parse(argv[4]), parse(argv[5]), (u16)parse(argv[6]), &v)) == 0) {
			outhex(v);
			out("\n");
		}
	} else if (streq(argv[3], "c45w") && argc >= 8) {
		rc = c45_write(parse(argv[4]), parse(argv[5]), (u16)parse(argv[6]),
		    (u16)parse(argv[7]));
	} else if (streq(argv[3], "sup") && argc >= 5) {
		rc = serdes_up(parse(argv[4]), 1);
		if (rc == 0)
			rc = sw_write(parse(argv[4]), REG_PORT_CONTROL, 0x007f);
	} else if (streq(argv[3], "sdown") && argc >= 5) {
		rc = serdes_up(parse(argv[4]), 0);
	} else if (streq(argv[3], "up") && argc >= 5) {
		rc = port_up(parse(argv[4]), 1);
	} else if (streq(argv[3], "down") && argc >= 5) {
		rc = port_up(parse(argv[4]), 0);
	} else {
		usage();
		rc = 2;
	}

	(void)sys_close(fd);
	return (rc == 0 ? 0 : 1);
}

/*
 * The entry point, because there is no libc to provide one.
 *
 * On aarch64 the kernel leaves the stack pointer at argc, with argv immediately after it, so the
 * entry point has to read the stack pointer before anything has moved it. Written as an ordinary C
 * function it gets a prologue, the prologue moves the stack pointer, and what it then reads as argc
 * is whatever the prologue left there - which happens silently: the program runs, prints its usage,
 * and looks like an argument-parsing bug. `__attribute__((naked))` would say so, but aarch64 gcc
 * ignores it with a warning, so the entry point is written in assembly at file scope instead.
 */
void __attribute__((used, noreturn))
start_c(long *sp)
{

	sys_exit(run((int)sp[0], (char **)&sp[1]));
}

__asm__(
	".globl _start\n"
	"_start:\n"
	"	mov	x0, sp\n"
	"	b	start_c\n"
);
