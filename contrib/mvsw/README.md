# mvsw - the switch behind the panel ports

Ten of the XGS 3300's twelve panel ports hang off a Marvell 88E6193X, and the switch is reachable
only from the coprocessor. `mvsw` runs there and talks to it.

```
mvsw 0 2 id                          the switch identifier
mvsw 0 2 dump [first last]           every port's control, status, VLAN map and PHY
mvsw 0 2 read  <dev> <reg>           a switch register
mvsw 0 2 write <dev> <reg> <val>     a switch register
mvsw 0 2 phyr  <phy> <page> <reg>    an internal PHY register
mvsw 0 2 phyw  <phy> <page> <reg> <val>
mvsw 0 2 up    <port>                bring a copper port up
mvsw 0 2 down  <port>
```

`0 2` is the MDIO bus and the switch's SMI address, and they are not a guess: the board file says
`npu0.device1.mdio=mdio22:0:2`, in the format the BSP's own parser defines - clause 22, bus 0,
address 2.

## Why it exists

The switch was first reached with a shell loop driving the MDIO controller's registers through
busybox `devmem` over a 115200 console. That is fine for a read or two and useless for anything
more: roughly half the reads came back wrong, a register reading `0x007f` on one pass and `0x0000`
on the next, because nothing was polling the controller's busy bit properly between operations. A
read-modify-write built on one of those bad reads wrote `0 | 3` into a port's control register and
**took down a working gigabit link**.

`/dev/mvmdio-uio` removes the problem rather than papering over it. It is a character device from
the vendor's own `mvmdio_uio` module, and its read and write paths call the kernel's `mdiobus_read`
and `mdiobus_write`, so the controller handshake is the kernel's business. The interface is one
struct passed in the buffer of a read or a write:

```c
struct { int bus_id; int phy_id; int reg; unsigned short data; }
```

A **read** must have that buffer already carrying bus, phy and reg, because the driver copies the
request out of it before doing anything. That is why this cannot be `dd`, a shell, or the
coprocessor's Lua: each of them hands the kernel a buffer whose contents it does not control. It
has to be a program.

## Building it, and why it is freestanding

The coprocessor has a `cc` with no `cc1`, so nothing can be built there. It runs **glibc 2.27**,
while the cross toolchain to hand targets a far newer one - so a dynamically linked binary will not
load, and a statically linked one is seven hundred kilobytes. The only way onto the box is base64
through the same 115200 console.

With no libc at all it is sixty-odd kilobytes, uses five system calls and its own hex printer, and
runs on any aarch64 Linux:

```
make                     # aarch64-linux-gnu-gcc, freestanding, static
make mvsw.b64            # what goes over the console
```

One trap worth naming, because it costs an afternoon and looks like something else: **the entry
point must not be an ordinary C function.** Written as one it gets a prologue, the prologue moves
the stack pointer, and what the program then reads as `argc` is whatever the prologue left there -
so it runs, prints its usage and looks like an argument-parsing bug. `__attribute__((naked))` would
express it but aarch64 gcc ignores the attribute with a warning, so `_start` is a file-scope
assembly block instead.

## What it knows about the switch

Every number came from the vendor's own sources in the GPL drop, not from a datasheet:

| thing | where |
|---|---|
| clause 22, bus 0, SMI address 2 | the board file, parsed by the BSP's own `sscanf` format |
| multi-chip addressing: register 0 is a command, 1 its data | one SMI address for the whole chip |
| busy `0x8000`, clause at bit 12, op at bit 10, device at bit 5 | `umsd/include/driver/msdHwAccess.h` |
| Global2 is device `0x1C`; SMI PHY command `0x18`, data `0x19` | `Amethyst_msdDrvSwRegs.h` |
| port control `0x04`, state in bits [1:0], 3 forwarding | `Amethyst_gprtSetPortState` |
| a 1G port is the bridging state **and** PHY register 0 bit 11 | the vendor's own `umsd_port_up` |

**The page is the trap.** Reading a copper PHY returns flat zeros - not `0xffff`, which would mean
nobody home - until register 22 is set to page 0. The vendor's routine saves register 22, does its
paged write and puts the page back, and that is why. An hour went into concluding there was no PHY
there at all before the page was tried, so every PHY access here is paged.

## What it produced

```
port  control  status   vlanmap  phy0     state
  0    0x017f   0x0f4d   0x07fe   0x0000   forwarding, link
  1    0x007f   0x0e0f   0x0001   0x1140   forwarding, link
  2    0x007f   0x000f   0x0001   0x1140   forwarding, no link
  ...
  9    0x007c   0x0249   0x0001   0x0000   disabled, no link
 10    0x007c   0x0249   0x0001   0x0000   disabled, no link
```

Port 0 is the coprocessor's uplink. Ports 1 to 8 are the eight RJ45 panel ports, all forwarding
with their PHYs powered, and the one with a cable in it is linked at a gigabit. Ports 9 and 10 are
the two SFP cages, which are SERDES rather than copper and are left alone - the copper path above
does not apply to them.

The port-based VLAN map needs nothing: `0x07fe` on the uplink and `0x0001` on each panel port is
the reset default, and it already says each panel port may forward only to the coprocessor and the
coprocessor to all of them.
