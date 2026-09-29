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
| **Thermal sensors** | a Nuvoton super-I/O on the host, reached by the vendor's `xgs-nct` | a hwmon driver for that part, or direct super-I/O access. The vendor's own `xgs-nct -g <pin>` fails for every GPIO pin because those pins are not on this chip - see below |
| **The MCP2210 bridge** | USB-to-SPI, present on this board | already measured: **all nine pins read as inputs**, matching no board's hold or release mask, so it does not hold this coprocessor. The plugin's reset path is for ARMADA boards |
| **The CPLD** | eleven registers implemented in `0x00`-`0x2f`; every other offset answers `0xfeedbeef`. Register `0x25` decoded and agreed with a live 10G link | **the host-side access path is not established.** The vendor reads it with `xgs-cpld`, and how that tool reaches the part - LPC, i2c, or a platform driver - has not been read out of it yet. That is the first thing to settle |

## Behind the coprocessor, and currently unreachable

| Piece | What it is | What it needs |
|---|---|---|
| **The panel LEDs, ten ports** | driven by the **88E6193X switch itself**, from `MVL6193LEDcontrol=0xe3` per port plus a GPIO pair each - `gpio:0:51:52` for phy0, `0:53:54` for phy1, upward | two things, both on the far side: the switch's LED control over MDIO, and those GPIOs, which are on the coprocessor's `gpiochip432`, label `gpio_thunderx`, 80 pins. Platform GPIO 51 is sysfs 483 there. This is why the whole panel is dark under a foreign operating system |
| **PortF1 and PortF2's LEDs** | **no LED key at all** in the platform store, because the cages hang directly off BGX2 rather than behind the switch | the 88X5113's own LED registers. The vendor's `xgs-led all <left> <right>` lights them by hand - amber at low speed, blue at high - so the hardware and the path are fine; nothing drives them as an activity indication, and **a light on a cage is not an instrument** |
| **The 88E6193X switch** | the ten panel ports behind it. Reachable with `xgs-mdio -a`, which announces "Amethyst switch access, use clause 45 syntax" | MDIO from the coprocessor. Its per-port registers have never been read, on this board, by anyone - the tool stopped working part-way through the capture and the cause was never established |
| **The 88X5113 PHY** | F1 and F2, and **both lanes share one MDIO address** | the same MDIO path |
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
2. **The CPLD's access path.** Settle how `xgs-cpld` reaches the part. Eleven registers and one
   already decoded make it cheap to verify, and it carries the SFP cage pin states - module present,
   transmitter fault, loss of signal - which are genuinely useful to a driver.
3. **The Nuvoton sensors**, because a firewall that cannot read its own temperature is one that
   cannot be trusted to run unattended.
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
