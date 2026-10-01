# The rest of the appliance, and what driving each piece would need

This project's subject is the coprocessor and its front ports. An XGS 3300 is more than that: a front
panel with an LCD and twenty-odd indicators, a CPLD, a managed switch, four PHY types, a fail-to-wire
relay, thermal sensors on two different buses, and a USB-to-SPI bridge. None of it works under
OPNsense today.

Everything here is from the live inventory taken off this board while the vendor's firmware was
running it, which is held outside this repository, plus what this driver has measured since. Each row
says where the knowledge is, so nobody has to rediscover it.

**One structural fact decides most of this page.** The panel, the switch and the PHYs hang off the
**coprocessor**, not the host - and NetAgent's MDIO and GPIO opcodes, `0x41`, `0x42` and `0x43`, have
**no handler** on this board: they return an ACK with status 1. So there is currently no host-side
path to any of them. What is reachable from the host today is a much shorter list than the hardware
suggests.

## Reachable from the host now

| Piece | What it is | What it needs |
|---|---|---|
| **The LCD** | an EZIO-300 style serial panel at **2400 baud on `/dev/ttyS1`**, driven by a proprietary `lcdd` rather than LCDproc | a host UART, which FreeBSD sees as `cuau1`, and the panel's own protocol. The menu tree, the key codes and the fact that `lcdd` owns the port are all recorded. This is the one piece that needs nothing from the coprocessor |
| **Thermal sensors** | **read through the CPLD, not a Nuvoton part.** `xgs-1us-sensors -a` prints `CPLD VERSION 0x05000008` and `CPLD BLOCKID 0x0000b002` - byte for byte the CPLD's own registers `0x01` and `0x00` - and then `CPU Temperature 45`, `NPU Temperature 64`, `INLET Temperature 26`, `Fan 0 speed 6900` and the power-supply good flags, all in one invocation | the CPLD path above, and nothing else. `xgs-nct` is a register poke tool, not a sensor reader: with no arguments it answers "You must specify a device and register and may have an optional value to write." Scoping a Nuvoton hwmon driver would have been wasted work |
| **The MCP2210 bridge** | USB-to-SPI, present on this board | already measured: **all nine pins read as inputs**, matching no board's hold or release mask, so it does not hold this coprocessor. The plugin's reset path is for ARMADA boards |
| **The CPLD** | **nineteen** registers, not eleven: twelve in `0x00`-`0x2f` and seven more at `0x30`, `0x31`, `0x38`-`0x3c`. `0x00` is a block id reading `0x0000b002` and `0x01` a version reading `0x05000008` - the same two words `xgs-1us-sensors` prints as its banner, which is how they are identified. `0x02` and `0x03` hold `0xa5a5a5a5` and `0x5a5a5a5a`, a fixed pair that catches a wrong stride. `0x25` carries the SFP cage pin states | **driven, from the coprocessor** - see the section below. `npu0.cpld.location=spi:0:1:3` reads as SPI bus 0, chip select 1, mode 3, and the node is `/dev/spidev0.1` on the coprocessor exactly as that says. It is **not a host bus**, and the two sibling keys that settle the notation - `npu0.slotA.vpd=i2c1:1:0x50` and `npu0.device1.mdio=mdio22:0:2` - are on the coprocessor too |

## Behind the coprocessor, and currently unreachable

| Piece | What it is | What it needs |
|---|---|---|
| **The panel LEDs, ten ports** | driven by the **88E6193X switch itself**, from `MVL6193LEDcontrol=0xe3` per port plus a GPIO pair each - `gpio:0:51:52` for phy0, `0:53:54` for phy1, upward | two things, both on the far side: the switch's LED control over MDIO, and those GPIOs, which are on the coprocessor's `gpiochip432`, label `gpio_thunderx`, 80 pins. Platform GPIO 51 is sysfs 483 there. This is why the whole panel is dark under a foreign operating system |
| **PortF1 and PortF2's LEDs** | **no LED key at all** in the platform store, because the cages hang directly off BGX2 rather than behind the switch | the 88X5113's own LED registers. The vendor's `xgs-led all <left> <right>` lights them by hand - amber at low speed, blue at high - so the hardware and the path are fine; nothing drives them as an activity indication, and **a light on a cage is not an instrument** |
| **The 88E6193X switch** | the ten panel ports behind it, at `npu0.device1.mdio=mdio22:0:2`. **Its per-port registers were read**, for all eleven devices: every one answers `reg3=0x1930`, the switch identifying itself from silicon, with `reg0` ranging over `0x0f4d`, `0x0e0f`, `0x0249` and `0x0e49` | MDIO from the coprocessor, where `xgs-ssh.sh "xgs-mdio -a 0 2 <dev>.<reg>"` already works. The host-side failure has a cause and it is in the capture too: the host binary wants a `/dev/uio*` node the host does not have, so it fails with "Error opening mdio handle" |
| **The 88X5113 PHY** | F1 and F2 at **`mdio45:0:7`** - clause 45, bus 0, address 7 - with F1 on slice 0 and F2 on slice 2, from `npu0.phy10.MVL5113=mdio45:0:7:0` and `npu0.phy11.MVL5113=mdio45:0:7:2`. Its identity is measured: `1.2 = 0x002b` and `1.3 = 0x0b45` compose a PMA part number of `0x002b0b45`, and `1.0 = 0x2040`, `4.0 = 0x204c`. Address 9 answers `0xffff`, so F2 is not at an address of its own | the same MDIO path. There is a safe first write on the same part - the tool's own help names an LED scratch register at `1F.0xF434`, which reads back `0x4444` |
| **The AQR412C and MVL3610** | the other PHY types the platform knows, named in `xgs-mdio`'s own help | the same |
| **The fail-to-wire relay** | `LANBYPASS=MVL_FTW` on this model. `npu0.phy0.ftwbump=1` and `npu0.phy1.ftwbump=-1` put the copper bypass pair on **Port1 and Port2**. Its three registers are on the CPLD and **have been read**: `0x39` is 0, so `ftwstatus` (mask `0x30`), `ftwcmd` (mask `0xf00`) and `ftwarm` (bit 0) are all clear; `0x3a`, the watchdog, is 0; `0x3b`, the trigger, is 2. **The relay is disarmed** | `xgs-ftw`, which writes. Never run here, and it should not be run casually: an armed relay changes what the appliance does when software stops. Reading the three registers is safe and is how the state above was established |

## Not present, and worth knowing so nobody looks

- **Expansion slot A is empty.** `npu0.slotA.present=0`, and `xgs-eeprom -t 1:0x50` answers "No such
  device or address" for its VPD. The tool and the key store agree.

**And a correction, because this page had it wrong.** `npu0.bp0` was described here as a 10G bypass
pair belonging to that empty slot. It is not. It is the **internal 10G link between the coprocessor
and the switch**, and the key store says so in its own notation:

    npu0.bp0.init_speed=10G
    npu0.bp0.port0=0:8          device 0 is CN8365, so this is the coprocessor's port 8
    npu0.bp0.port1=1:0          device 1 is 88E6193X, so this is switch port 0
    npu0.bp0.port0.mac=<base+0x0c>      the thirteenth address of the unit's block
    npu0.bp0.port1.mac=<base+0x0d>      and the fourteenth

`npu0.device0=CN8365` and `npu0.device1=88E6193X` give the `dev:port` scheme, and every front port
reads `1:1` through `1:10` - all on device 1, the switch. A pair keyed `0:8` and `1:0` is therefore
one end on each chip, which is a link between them, not a relay in a slot. Switch port 0 reads
`reg0=0x0f4d` - linked, full duplex, above 1G - which an unpopulated slot's port would not.

**It also names two netdevs this project could not explain.** `pport_l0` and `pport_l0s0p0`
carry exactly those two addresses - the last two of the unit's fourteen-address block - and they
appear nowhere else in the capture. So the two odd entries in the vendor's interface list are
the two ends of the backplane link.

The copper fail-to-wire pair is separately keyed, on `npu0.phy0.ftwbump` and `npu0.phy1.ftwbump`, so
`bp` here means backplane and not bypass. `PORT_000` in `worker_port_cnt` being the switch uplink is
the same link seen from the fast path's side.
- **Power over Ethernet does not apply.** `xgs-poe` and `xgs-poe-116` exist in the image; this model
  has no PoE.
- **`STATUS_LED=NO`** on this model, so `statusled_CPLD`, `xgs-led-event` and `xgs-led-identify` are
  for other boards.

## The CPLD is driven now, and it was holding two ports dark

**This is the first piece on this page that has been driven rather than described.** It is reached
from the coprocessor, not from the host, and it needed no new code: the vendor's own `xgs-cpld` and
`xgs-sff` are already in the coprocessor's image.

The protocol is eight lines of `lib/xgs_spi_cpld.c` in the BSP and is worth writing down, because
anything that has to reach the CPLD without those tools needs it:

```
/dev/spidev0.1, SPI mode 3, 3 MHz
read   write one byte (reg | 0x80), then read four bytes, big-endian
write  one five-byte transfer: (reg & 0x7f) then four bytes, big-endian
```

So **every register is 32 bits wide**, which is what makes `0x25` able to hold four cages' worth of
pins. Registers `0x00` and `0x01` answer `0x0000b002` and `0x05000008` - the block id and version
this page already recorded from the sensor tool's banner - so a read can be checked before anything
is believed, and a wrong stride is caught immediately.

**What it was holding.** `npu0.phy8.pin.tx_disable=cpld:0x25.4` and `npu0.phy9.pin.tx_disable=cpld:0x25.10`
are the two 1G SFP cages' laser-enable pins. Both read **set**, so both lasers were off; the two 10G
cages' equivalents at bits 16 and 22 read clear, which is why those two always worked. Clearing the
two bits through the vendor's pin path brings one cage straight up at a gigabit:

```
xgs-sff -p 8 -d 0        F3
xgs-sff -p 9 -d 0        F4
xgs-cpld 0x25            0x0038669a -> 0x0038628a
```

The full decode of `0x25`, the correction it forces to what was written here about those modules, and
the switch-side result are in [octeon-tx.md](octeon-tx.md).

**How volatile it is, measured rather than assumed.** The bit survives a coprocessor restart: after
a host reboot, which restarts the coprocessor, register `0x25` still read the value it had been
given and a populated cage linked without being touched. It also survives a module being pulled and
a different one put in.

**What does not survive a module swap is the SERDES.** The same module moved from one cage to the
other did not link, and both cages' clause-45 registers read identically - `0xf002` `0x0058`,
`0xf003` `0x0004`, `0x2000` `0x1140` - so it was not a configuration difference. Re-running the
SERDES bring-up on the cage now holding the module brought it up at a gigabit within seconds, and
the panel LED followed. **So a cage has two gates in order: the CPLD bit once, and the SERDES again
after every insertion.**

## The sensors, and how far a FreeBSD host gets toward them

The first table calls the thermal sensors reachable from the host, on the strength of
`xgs-1us-sensors -a` printing them under the vendor's firmware. That is right about where they live
and wrong about how close this project is to them, so here is the whole picture.

**They are a register bank on the CPLD, reached over SMBus rather than SPI.** The BSP's
`lib/xgs_1us_sensors.c` opens an 8-bit-addressed device at **slave `0x60`**, probing I2C bus 0 and
then bus 1, and confirms it by reading the same block id the SPI path reads - `0xb002`. So it is one
chip with two ways in, and the map is small enough to write down:

| register | what |
|---|---|
| `0x07`, `0x08` | CPU temperature, low and high |
| `0x09`, `0x0a` | NPU temperature, high and low |
| `0x0b`, `0x0c` | inlet temperature, high and low |
| `0x0d`, `0x0e` | fan 0 and fan 1 speed |
| `0x14` | fan 0 under-speed flag; writing 1 clears it |
| `0x16` | minimum fan speed |

Temperature is `high + low * 0.125`, and the inlet reading is signed - above 128 it has 255
subtracted. The fan registers are read several times and reconciled, because a tachometer read in
flight is not a speed.

**A device does answer at `0x60` on the host's own SMBus.** FreeBSD attaches `intsmb0` to the AMD
FCH controller and `smbus0` above it, and with `smb.ko` loaded a bus probe finds something:

```
Probing for devices on /dev/smb0:
Device @0x60: r      ... and 0x62, 0x66, 0x68, 0x6a
```

A receive-byte returns a value. **A read-byte-data - the transaction the map above needs - returns
`ENXIO`.** Every address on that list answers a receive-byte with the same `0x02`, which is what a
bus with nothing really driving it looks like as often as it is five devices.

**What the vendor's host does differently is the part to chase.** Under SFOS the x86 side loads
`i2c_piix4`, which exposes **both** of the FCH's SMBus ports, and the BSP probes bus 0 and bus 1
precisely because the chip is not always on the first one. FreeBSD's `intsmb` attaches one
controller. So the next step is not a sensor driver; it is finding out whether the second port is
reachable here at all, and that is worth doing because it is the only peripheral on this page that
would need nothing from the coprocessor if it were.

### Two roads to the sensors that are now closed, so nobody walks them again

**The SPI face does not carry them.** It is the same chip by block id, but not the same register
space, and reading the sensor offsets over SPI says so plainly:

```
0x00  0x0000b002      0x01  0x05000008      the block id and version, as expected
0x07  0xffffffff      0x08  0x082471e0      0x0a  0x00000098      0x0c  0x000000a0
0x09  0xfeedbeef      0x0d  0xfeedbeef      0x0e  0xfeedbeef      0x16  0xfeedbeef
```

`0xfeedbeef` is this CPLD's answer for a register that is not there, and it lands on four of the
seven sensor offsets. The SPI side is 32 bits wide and has its own map; the sensor map is the 8-bit
one behind the SMBus face.

**And the coprocessor's own I2C buses do not reach it.** `/dev/i2c-0` and `/dev/i2c-1` exist there,
`i2cget`, `i2cset`, `i2cdetect` and `i2cdump` are all in `/sbin`, and slave `0x60` answers on
neither:

```
bus 0:  0x00=Error: Read failed   0x01=Error: Read failed
bus 1:  0x00=Error: Read failed   0x01=Error: Read failed
```

That is consistent with where the vendor's tool lives: `xgs-1us-sensors` is an **x86** binary on the
SFOS host and is not in the coprocessor's image at all. The sensors are the host's to read, over
the host's SMBus, and the only question left is which of the AMD FCH's two ports they are on.

## The order these are worth doing in

1. **The LCD.** It is the only piece that needs nothing from the coprocessor, its port is an ordinary
   host UART, and the protocol is a serial panel rather than a register map. It is also the most
   visible thing an appliance can do while a driver is still being written.
2. ~~**The CPLD, over the coprocessor's SPI.**~~ **Done** - see the section above. It carries the SFP
   cage pin states and, on the evidence in this page's first table, the temperatures and fan speeds
   as well, so it was one piece of work rather than the three this list used to have. What remains
   is a host-side path to it, which today means going through the coprocessor.
3. ~~**An MDIO path through the coprocessor**~~, which unlocks the switch, all four PHY types and the
   panel LEDs at once. **Done** - `contrib/mvsw` drives the switch over `/dev/mvmdio-uio`, every
   panel port is forwarding and the panel LEDs are lit. NetAgent still cannot carry it, so this is
   still a coprocessor-side path and not a host-side one.
4. **The LEDs on F1 and F2**, which are the only indicators left dark: they hang off the 88X5113
   rather than the switch, and the board file has no LED key for them at all.
5. **The fail-to-wire relay, last**, and only deliberately. It is the one piece here whose wrong
   setting changes the appliance's behaviour when nothing is running.

## What this page is not

It is not a claim that all of it works. Two rows have now been driven - the CPLD and, through it, the
SFP cages, plus the switch over MDIO - and each says so where it is claimed. **Everything else in the
two tables is still description**, and the repository's rule is that nothing is claimed that has not
been run. Note also what "driven" means here: from the coprocessor's own shell, not from OPNsense. The value here is that the
measurements exist, they were taken on this board under the firmware that does drive all of it, and
the next person does not have to start by finding out which bus a part is on.
