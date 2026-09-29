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
| **The CPLD** | **nineteen** registers, not eleven: twelve in `0x00`-`0x2f` and seven more at `0x30`, `0x31`, `0x38`-`0x3c`. `0x00` is a block id reading `0x0000b002` and `0x01` a version reading `0x05000008` - the same two words `xgs-1us-sensors` prints as its banner, which is how they are identified. `0x02` and `0x03` hold `0xa5a5a5a5` and `0x5a5a5a5a`, a fixed pair that catches a wrong stride. `0x25` carries the SFP cage pin states | **the bus is named in the key store and it is SPI**: `npu0.cpld.location=spi:0:1:3`. Two sibling keys settle the notation - `npu0.slotA.vpd=i2c1:1:0x50` and `npu0.device1.mdio=mdio22:0:2` - so this reads as SPI bus 0, chip select 1. Both of those siblings are on the coprocessor, so this is very probably the coprocessor's SPI0 and **not a host bus at all** |

## Behind the coprocessor, and currently unreachable

| Piece | What it is | What it needs |
|---|---|---|
| **The panel LEDs, ten ports** | driven by the **88E6193X switch itself**, from `MVL6193LEDcontrol=0xe3` per port plus a GPIO pair each - `gpio:0:51:52` for phy0, `0:53:54` for phy1, upward | two things, both on the far side: the switch's LED control over MDIO, and those GPIOs, which are on the coprocessor's `gpiochip432`, label `gpio_thunderx`, 80 pins. Platform GPIO 51 is sysfs 483 there. This is why the whole panel is dark under a foreign operating system |
| **PortF1 and PortF2's LEDs** | **no LED key at all** in the platform store, because the cages hang directly off BGX2 rather than behind the switch | the 88X5113's own LED registers. The vendor's `xgs-led all <left> <right>` lights them by hand - amber at low speed, blue at high - so the hardware and the path are fine; nothing drives them as an activity indication, and **a light on a cage is not an instrument** |
| **The 88E6193X switch** | the ten panel ports behind it, at `npu0.device1.mdio=mdio22:0:2`. **Its per-port registers were read**, for all eleven devices: every one answers `reg3=0x1930`, the switch identifying itself from silicon, with `reg0` ranging over `0x0f4d`, `0x0e0f`, `0x0249` and `0x0e49` | MDIO from the coprocessor, where `xgs-ssh.sh "xgs-mdio -a 0 2 <dev>.<reg>"` already works. The host-side failure has a cause and it is in the capture too: the host binary wants a `/dev/uio*` node the host does not have, so it fails with "Error opening mdio handle" |
| **The 88X5113 PHY** | F1 and F2 at **`mdio45:0:7`** - clause 45, bus 0, address 7 - with F1 on slice 0 and F2 on slice 2, from `npu0.phy10.MVL5113=mdio45:0:7:0` and `npu0.phy11.MVL5113=mdio45:0:7:2`. Its identity is measured: `1.2 = 0x002b` and `1.3 = 0x0b45` compose a PMA part number of `0x002b0b45`, and `1.0 = 0x2040`, `4.0 = 0x204c`. Address 9 answers `0xffff`, so F2 is not at an address of its own | the same MDIO path. There is a safe first write on the same part - the tool's own help names an LED scratch register at `1F.0xF434`, which reads back `0x4444` |
| **The AQR412C and MVL3610** | the other PHY types the platform knows, named in `xgs-mdio`'s own help | the same |
| **The fail-to-wire relay** | `LANBYPASS=MVL_FTW` on this model. `npu0.phy0.ftwbump=1` and `npu0.phy1.ftwbump=-1` put the copper bypass pair on **Port1 and Port2** | `xgs-ftw`, which writes. Never run here, and it should not be run casually: an armed relay changes what the appliance does when software stops |

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

## The order these are worth doing in

1. **The LCD.** It is the only piece that needs nothing from the coprocessor, its port is an ordinary
   host UART, and the protocol is a serial panel rather than a register map. It is also the most
   visible thing an appliance can do while a driver is still being written.
2. **The CPLD, over the coprocessor's SPI.** The bus is named, nineteen registers are mapped, and
   two of them are a block id and a version that any read can be checked against before anything
   else is believed. It carries the SFP cage pin states and, on the evidence above, the temperatures
   and fan speeds as well - so this is one piece of work, not the three this list used to have.
4. **An MDIO path through the coprocessor**, which unlocks the switch, all four PHY types and the
   panel LEDs at once. NetAgent cannot carry it - its MDIO opcode has no handler - so this needs
   either something on the coprocessor or a facility this project has not read yet.
5. **The fail-to-wire relay, last**, and only deliberately. It is the one piece here whose wrong
   setting changes the appliance's behaviour when nothing is running.

## What this page is not

It is not a claim that any of it works. Nothing in the two tables has been driven from OPNsense, and
the repository's rule is that nothing is claimed that has not been run. The value here is that the
measurements exist, they were taken on this board under the firmware that does drive all of it, and
the next person does not have to start by finding out which bus a part is on.
