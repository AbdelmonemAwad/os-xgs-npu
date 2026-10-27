<!-- reviewed: 2026-10-13 -->
# The sensors answer on the host's own bus, and the bus had been there all along

Measured on an XGS 3300, 2026-10-13, OPNsense 26.7.5, for
[issue 164](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/164). The appliance's temperatures,
fan speeds and flags are a register bank on its CPLD, behind an 8-bit SMBus face at slave `0x60`.
Where that face was wired had been looked for since 2026-10-02: not the coprocessor's two I2C
buses, not the CPLD's SPI face, and on the host every face that FreeBSD could drive came back empty
- the main controller's four multiplexed ports on 2026-10-12, and this morning the FCH's auxiliary
controller, found at `0x0b20` by i2c-piix4's own arithmetic, running transactions onto a bus with
nothing on it.

## What was in the way

**FreeBSD's `intsmb(4)` and `smbmsg(8)` were the instrument, and the instrument was wrong for this
FCH.** Every data protocol through them answered `ENXIO`, and a receive-byte answered `0x02` from
every address - which reads as an undriven bus and was recorded as one. The same controller - the
main one, at `0x0b00`, the port the firmware leaves selected - driven directly through its
PIIX4-style register block over `/dev/io` acknowledges `0x60` alone (`0x61`, `0x66`, `0x08`,
`0x20`, `0x40`: no device) and answers read-byte-data register by register. That is the exact
transaction the vendor's own library makes through `/dev/i2c-0` (`I2C_SMBUS_READ`, `BYTE_DATA`),
and nothing else: no GPIO gate is set for this device - the library's gate is the SFP EEPROMs', not
the sensors'.

## The bank

Two passes a few seconds apart, read-byte-data at `0x60`, registers `0x00` to `0x3c`:

```
05 00 08 02 b0 55 aa 00 36 45 05 1c 00 16 16 00  00 00 00 01 00 00 02 02 02 ... 02
05 00 08 02 b0 55 aa 00 32 45 05 1c 00 17 17 00  00 00 00 01 00 00 02 02 02 ... 02
```

| register | what | read |
|---|---|---|
| `0x00`-`0x02` | the CPLD version's bytes, `0x05000008` on the SPI face | `05 00 08` |
| `0x03`-`0x04` | the block id, `0xb002` | `02 b0` |
| `0x05`-`0x06` | a fixed pair, as the SPI face has at `0x02`/`0x03` | `55 aa` |
| `0x07` lo, `0x08` hi | CPU temperature, hi + lo / 8 | 54.0 C, 50.0 C; 43.0 C a minute earlier |
| `0x09` hi, `0x0a` lo | NPU temperature | 69.625 C |
| `0x0b` hi, `0x0c` lo | inlet temperature | 28.0 C |
| `0x0d`, `0x0e` | fan 0, fan 1, in units of 300 rpm | 6,600 and 6,900 rpm |
| `0x11` | reset flag 0, bit 0 - the reset button's | clear |
| `0x12` | reset flag 1, bit 0 | clear |
| `0x13` | power supplies good, bits 0 and 1 - internal and external, as the vendor shows them | `01`: bit 0 set, bit 1 clear |
| `0x14` | fan 0 under-speed flag | clear |
| `0x15` | fan 1 under-speed flag | clear |
| `0x16` | the minimum fan speed | 600 rpm |
| `0x17` and up | not there: `0x02` from every one, this face's `0xfeedbeef` | |

The rows for `0x11`, `0x12`, `0x13` and `0x15` were added on 2026-10-27: vendor version 22's sensor
support reads those four registers for those four purposes. The bank above already held their values.

The arithmetic is the vendor's (`hi + lo * 0.125`, `byte * 300`), and the numbers are the ones its
own tool printed under its firmware on 2026-10-02 - 45, 64, 26 and 6900 - a day's drift apart.

## What it gives

A reader that runs on OPNsense: [`tools/sensors/xgs3300-sensors.c`](../../tools/sensors/xgs3300-sensors.c),
built with `cc` on the appliance, prints the decoded readings, the raw bank, or one register. The
host's own die sensor, `amdtemp`, reads beside it. What this is not: a kernel path. That would be
`intsmb(4)` made to pass byte-data on this FCH, which is a FreeBSD driver question and not this
board's, and until then the port select (`PMIO 0x02`) is left as found and the tool and the driver
share the controller without a lock - one reading at a time, by hand or from a cron entry, is the
honest use.

## Lessons

- **A bus is not empty until it has been driven by two instruments.** One driver's `ENXIO` was taken
  for the hardware's answer for a day; the controller's own registers said otherwise in an hour.
- **Read the vendor's library before deriving a protocol.** The transaction, the slave, the
  arithmetic and the absence of a gate were all in `xgs_1us_sensors.c` and `xgs_eeprom.c`.
