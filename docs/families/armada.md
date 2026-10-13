# ARMADA - Marvell CN913x

    PCI id     11ab:7080
    driver     npuep  (contrib/npuep)
    platforms  xgsdt1, xgsdt2-116, xgsdt2-126136, xgsdt2-138
    hardware   Sophos XGS 136, assembly AMDA0201, 14 ports - ON THE BENCH
    state      all fourteen front ports carry traffic

This is the family the project started on and the only one where the datapath works. Everything in
the top-level README's "What works" section is this family, and the protocol documents describe it:

- [../facility-protocol.md](../npuep/facility-protocol.md) - the facility framework and the handshake
- [../giu.md](../npuep/giu.md) - the GIU datapath, which is what carries packets
- [../mvmgmt.md](../npuep/mvmgmt.md) - the management netdev
- [../netagent.md](../npuep/netagent.md) - the network agent and the switch
- [../rpc.md](../npuep/rpc.md) - the RPC facility
- [../npu-bring-up.md](../npuep/npu-bring-up.md) - what has to happen, in order

## What is specific to this family

The facility table sits at a **fixed offset** in the window and is guarded by a cookie -
`0xAFACAFAC` for the facility itself, `0xD0FAC10D` for the barmap - and its facility records are
`{bar, type, offset, size}`. Target-to-host doorbells are **MSI-X vectors**, which `npuep` allocates.

A fifth facility, **GIU**, exists here and is what carries the front ports. It does not exist on
OCTEON at all, and its absence there is the clearest single sign that these are different protocols
rather than one protocol with two PCI ids.

The reset polarity is a per-board table rather than a constant, so the module reads the assembly
number out of the bridge's EEPROM and looks it up. Values are carried for AMDA0200, AMDA0201,
AMDA0202-0205, AMDA0208 and AMDA0224. **Only AMDA0201 has been tested on real hardware.**

## What is held here, and what it allows

| material | detail |
|---|---|
| hardware | **XGS 136 on the bench**, assembly AMDA0201 |
| coprocessor rootfs | complete, including `NPU-BSP-slot1/rootfs/` |
| fast path | `usfp` for this family, 1.9 MB, with symbols |
| firmware | U-Boot for all four ARMADA platforms, from BSP 21.0.0.169 and 22.0.2.546 |
| source | the Sophos GPL release, 794 files, including the `pcie_ep_armada` patches |
| launcher | `armada_target_setup_usfp.sh`, selected by `DP_DT1/DT2_STARTUP_SCRIPT` |

This is the family with the most complete material and the only one whose ports all carry traffic.

## Building and running it

`npuep` is not packaged, for the same reason `octep` is not: an out-of-tree module has to be
compiled against the headers of the kernel that is actually running, and OPNsense ships no kernel
sources. So it is built on the appliance. `install/install.sh` lays the sources down in
`/usr/local/share/os-xgs-npu/npuep` and builds through `install/kernel-follow.sh` - the same
follower that rebuilds the OCTEON TX module after every kernel update, which since 2026-10-13
follows whichever of the two modules is installed - and `07-npuep` loads it at boot, after
reading the kernel stamp beside it and refusing a module built for another kernel (the previous
module is kept as `npuep.ko.prev` for the kernel it was built for). Before that date the follower
answered every tick on this appliance with "the driver is not installed on this machine", the
upgrade and stop hooks were gated on the OCTEON TX's PCI identity alone, and the boot hook read
no stamp: a kernel update here would have loaded the old module unchecked.

Measured on 2026-10-13, on the XGS 136, with the owner running the update from the GUI: OPNsense
26.7 to 26.7.7 brought the kernel from 15.1-RELEASE-p1 to p4. The reboot's stop hook ran the
follower; it read p4 from `/boot/kernel/kernel`, fetched that commit's sources, built the module
from the laid-down tree, kept the p1 module as `npuep.ko.prev` with its stamp, installed the p4
one and cleared the fall-back - 2 min 59 s in all - and the host came up on p4 with the module
loaded, all fourteen ports present and the WAN lease back, nothing done by hand.

    sh contrib/npuep/fetch-sources.sh
    sh install/install.sh

## The coprocessor from the host

The coprocessor's console is the host's second UART - `hint.uart.2.at="isa"`, `port="0x3E8"`,
`irq="3"` in `/boot/loader.conf.local`, so `/dev/cuau2` on OPNsense at 115200 - the same
arrangement as the OCTEON TX appliance's. A newline answers `marvell login:`; `root` with no
password gives a root shell on a Buildroot 2018.11 userland over Linux 4.14.207, with 3 GB of
memory and `usfp` running. An idle coprocessor prints nothing, so silence is not death. Read on
2026-10-14 on the XGS 136.

The helper that runs one command and returns its reply (as on the OCTEON TX box: a background
reader on the line, the command between two echoed markers) has one difference here: the marker
is sent in two quoted halves, `echo "---MARK$$""---"`, because this shell echoes the command line
back wrapped at 80 columns and bracketed-paste codes around it, and a marker that appears whole in
that echo is taken for the first marker. Keep each command short and plain; some pipes with
quoted field separators came back empty through the line and were not understood.

What the console gives on this appliance, all of it read-only:

- **The vendor's tools are on the coprocessor**, not on the host as they were under the
  original firmware on the OCTEON TX box: `xgs-platform` (the platform store: assembly
  AMDA0201-0003, revision 08, the base MAC and 16 addresses), `xgs-ports -b` (the real MAC of
  every label), `xgs-phy-temperature -l <label>`, `xgs-poe -m all`, `xgs-sff-decode -l <label>`,
  `xgs-eeprom -r`, `xgs-i2c-reg`, `xgs-mdio` (clause 22 and 45), `xgs-reg` (raw NPU registers),
  `xgs-led <phy|all> <left> <right>` and `xgs-led-identify -l <label> <seconds>` (the identify
  light this project's Link Health page has no path to from the host - here it has one, through
  the console; not wired), `xgs-sgpio`. Two are never run: `xgs-healthmond` and
  `xgs-1us-led-bus-test`.
- **There is no CPLD on the XGS 136.** `xgs-cpld -t` and every register read answer "Error
  opening cpld handle. No such device". The 1U sensor block - CPU, NPU and inlet temperatures
  and two fans behind a SMBus address, the one the OCTEON TX appliance reads from its host bus -
  is not on this board. Its temperatures are the host's own (`amdtemp`, the NCT6779D), the
  coprocessor's thermal zones (`ap-thermal-ic` and `cpu0..3`, and the two CP dies at
  `f2400078`/`f4400078`, 41 to 50 C idle), and the PHYs: `xgs-phy-temperature` reads 55 C on
  label 1 (an 88E6193X PHY) and 49 to 53 C on labels 9 to 12 (the AQR412C lanes).
- **The port map, from the vendor's own platform data** (`AMDA0201-0003R00.txt` in the GPL
  release, confirmed by the tools): labels 1 to 8 are the 88E6193X's ports 1 to 8; F1 and F2
  are its ports 9 and 10, SFP cages on `i2c1` (buses 2 and 1, 0x50 and 0x51) with GPIO lines
  for presence, TX fault, TX disable, RX LOS and rate select; labels 9 to 12 are the four
  lanes of the AQR412C on the CN9131's own ports, 9 and 10 rate-capped at 1G, 11 and 12 at
  2.5G and on the PoE module - one module, 62 W, status good, as `xgs-poe` reports.
- The fast path's syslog (tag `dpdk`) says `mrvl_mtu_set(): MTU too big, max MTU possible
  limited by current mbuf size` at start-up: the jumbo question for this family, unmeasured.
- `mvmgmt0`, the management link over PCIe, is up at the host and NO-CARRIER at the
  coprocessor and carries nothing (issue 374); the coprocessor runs `sshd`, so that link, once
  it works, is the fast channel. Until then the console is the only path.
