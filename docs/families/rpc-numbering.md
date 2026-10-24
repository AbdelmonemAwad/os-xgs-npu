<!-- reviewed: 2026-10-25 -->
# How each SFOS numbers the control channel's commands, and how the drivers tell

The coprocessor's control channel is one byte of command number in front of a payload, and the
number is an index into the far side's handler table. SFOS 23 EAP 1 changed the table: it put a new
command, `SA_IF_MTU_SET`, at 36 and moved every command from 36 on one higher. A coprocessor runs the
software the appliance's last SFOS installed on it, so two boards of one model can speak two
numberings - issue #736 was the first owner whose board spoke the new one. Since 2026-10-25 both
drivers find out which software they are talking to at the channel's first command and number their
commands to match; the table they share is `contrib/npuep/sfosnum.h`, the same file in
`contrib/octep`, and the checks fail when the two copies differ.

## Where the numbers were read

From three of SFOS's own x86 host modules, `usfp_firewall.ko`, one per version:

| version | the module | read from |
|---|---|---|
| SFOS 21 | kernel 4.14.302, 21.0.0 GA build 169 | the XGS 136's own disk, before OPNsense went on it - the same 234 functions as 21.0.0 GA-169's module, and the coprocessor's `usfp_rh.ko` the same build as that release's BSP |
| SFOS 22 | kernel 6.6.116, 22.0.2 MR-2 build 546 | the XGS 3300's capture of 2026-09-29 |
| SFOS 23 EAP 1 | kernel 6.6.116, 23.0.0 EAP 1 build 329 | the first XGS 3300, running it, 2026-10-24 |

The module wraps nearly every command in a small function - `lif_delete`, `platform_read`,
`sa_if_mtu_set` - that loads the command's number into the first argument register and calls
`rpc_communicate` or `rpc_communicate_low_prio`, so the disassembly names each number. The table
reads are the exception: one function, `table_read` (`usfp_table_read` on SFOS 21), takes a table id
and picks the command from a jump table, which has to be followed through the module's relocations.
A first reading on 2026-10-25 did not, and saw two of the eleven; the counts below are the jump
table's.

The coprocessor side agrees. The text dumps of SFOS 22.0.2 MR-2's and SFOS 23 EAP 1's `usfp_rh.ko`,
the far side's handler, differ in one function, `ipsec_fpop_sa_if_mtu_set`, present only in 23 - in
the ARMADA build and in the OCTEON TX build alike.

## The numbers

Commands 0 to 35 are the same in all three: the firewall revisions 0 and 1, the configuration word
2, the interfaces 3 and 4, the port binding 5, the next hops 6 and 7, the microflows 8 and 9, the
flows 10 to 17, the live users 18 and 19, and the associations 30 to 35 among them. From 36:

| what | the drivers' name | SFOS 21 | SFOS 22 | SFOS 23 |
|---|---|---|---|---|
| the platform block | `PLATFORM_READ` | 36 | 36 | **37** |
| an association's MTU | - | - | - | **36** |
| the interface table | `LO_LIF_READ` | 37 | 37 | 38 |
| the connection table | `LO_CONN_READ` | 38 | 38 | 39 |
| the next-hop table | `LO_NHOP_READ` | 39 | 39 | 40 |
| the microflow table | `LO_MFLOW_READ` | 40 | 40 | 41 |
| the live users | `LO_LUID_READ` | 41 | 41 | 42 |
| the associations | `LO_SA_READ` | 42 | 42 | 43 |
| debug counters | `LO_WORKER_DBG_CNT_READ` | 43 | 43 | 44 |
| fast-path counters | `LO_WORKER_SYS_CNT_READ` | 44 | 44 | 45 |
| port counters | `LO_WORKER_PORT_CNT_READ` | 45 | 45 | 46 |
| direct-forwarding counters | `LO_WORKER_DF_CNT_READ` | - | 46 | 47 |

SFOS 21's host never asks for the platform block - its module has no `platform_read` - but its
coprocessor answers it at 36: the XGS 136, whose coprocessor runs SFOS 21's software, has given its
block at 36 to every load of this driver. Every number in both drivers is SFOS 21's and 22's, and
goes out through `sfosnum_wire`, which moves it from 36 on by what the board was found to speak.

## The platform block

`struct platform_info`, read from the DWARF of two of the far side's own modules - the XGS 136's
`usfp_rh.ko` (SFOS 21) and the XGS 3300's (SFOS 22, the same as the vendor's header):

| offset | SFOS 21, 228 bytes | SFOS 22 and 23, 232 bytes |
|---|---|---|
| 0, 64, 128 | the platform name, the version, the assembly - 64 bytes each | the same |
| 192 to 195 | id, cores, interfaces, high rings | the same |
| 196 to 223 | the microflow timeouts and the table sizes, capabilities at 220 | the same |
| 224 | `num_pfs`, then three reserved bytes | `num_mflows`, four bytes |
| 228 to 231 | - | `num_pfs`, `num_vfs`, `num_workers`, one reserved |

The version field is a build's git hash on SFOS 21 - `9a31cacb7d1` on the XGS 136 - and the
placeholder `UNKNOWN_VERSION` on SFOS 22 and 23, as SFOS's own `platform.version` showed under both.

Two readings in this project were wrong about the tail until 2026-10-25. The ARMADA driver named
offset 224 `num_mflows` on the XGS 136, where SFOS 21 has `num_pfs` - stored and never used, so it
cost nothing. The OCTEON TX driver put `num_pfs` and `num_vfs` at 232 and 233, past the end of the
232 bytes, a miscount of the header; they are at 228 and 229. It also took `num_mflows` from any
block of 228 bytes or more, and used it to bound the microflow slots: on an SFOS 21 block that is
the number of PFs, and every slot from 1 on would have been refused. No XGS 3300 here runs SFOS 21's
software, so it was never seen; it now reads `num_mflows` from a block of 232 only.

## The fast-path counters

`LO_WORKER_SYS_CNT_READ` answers an array of 64-bit counters and no names; the names are an
enumeration in the far side's module, and it is the SFOS version's, not the family's:

| version | named counters | where the list came from | the list |
|---|---|---|---|
| SFOS 21 | 176 | the DWARF of the XGS 136's `usfp_rh.ko`; its OCTEON TX `usfp_rh.ko`, from the same root file system, has the same 176, and the OCTEON TX2 98XX `usfp` of 21.0.0 GA build 169 names the same 176 in the same order | `tools/fpcntr-names-sfos21.txt` |
| SFOS 22 | 182 | the DWARF of the XGS 3300's `usfp_rh.ko`; the ARMADA `usfp`'s name strings are the same 182 in the same order | `tools/fpcntr-names-sfos22.txt` |
| SFOS 23 | 184 | **derived**: SFOS 22's list with the two names SFOS 23's strings add, where they add them - no DWARF of SFOS 23 has been read | `tools/fpcntr-names-sfos23.txt` |

Until 2026-10-25 the two lists this project had were filed as the ARMADA's and the OCTEON TX's,
because the one was read from an ARMADA board running SFOS 21 and the other from an OCTEON TX board
running SFOS 22. The difference was the version all along.

How many counters there are is not something a read finds. The far side's array is longer than
any of the lists: on the XGS 136, SFOS 21, a one-entry read answers at every index up to 254 and is
refused at 255 (measured 2026-10-25), so the count is the version's, and the version comes from the
platform block. `tools/npuep-fpcnt.sh` and `tools/octep-fpcnt.sh` pick the list by the driver's
`sfos`, and with none to pick print all 255 by index alone.

## How the drivers tell

At the channel's first command, before any port is programmed:

1. **Where the platform block answers.** `PLATFORM_READ` is sent at 36, then 37 and on up to 40, as
   written and with no payload, until one answers a block at least 228 bytes long whose name starts
   `XGS_` - which nothing else answers. A number below the right one is another command given
   nothing to work on, and the far side's handlers refuse a short payload before they do anything;
   SFOS 23's 36, `SA_IF_MTU_SET`, received exactly that at every boot of the XGS 126 in #736, which
   carried traffic all along. The search stops at the first block.
2. **How long the block is**, 228 or 232, and **whether its version field names a build**.

| answered at | length | version field | the coprocessor runs |
|---|---|---|---|
| 36 | 228 | a git hash | SFOS 21's software |
| 36 | 232 | `UNKNOWN_VERSION` | SFOS 22's |
| 37 | 232 | `UNKNOWN_VERSION` | SFOS 23's |

The position is what separates 23 from the others, and the length, with the version field
agreeing, separates 21 from 22. What confirms the position is the interface table read back right
after it, entry for entry, at the number the shift gives - the line `dev.npuep.0.iftable` prints.
Anything else - a block at 38, a length or a field that does not fit a row - is said as software
the driver has not seen: the commands still go by the position the block was found at, and the
read-back still checks them, but nothing version-specific, such as the counters' names, is
assumed. No block at 36 to 40 sends no command from 36 on at all; the ports, the interfaces and the
flows are all below 36 and go as they are.

What each driver shows:

| | ARMADA, `dev.npuep.0.rpc_channel.` | OCTEON TX, `dev.octep.0.rpc.` |
|---|---|---|
| the version found: 21, 22, 23, or 0 | `sfos` | `sfos` |
| how much higher from 36 on: 0, 1, or -1 | `cmd_shift` | `cmd_shift` |
| the block's length and version field | `platform_len`, `platform_version` | `platform_len`, `platform_version` |
| a hand-sent command by the number written | `command_as_written=1` | `cmd_as_written=1` |

Run on the XGS 136 (SFOS 21's software) and the second XGS 3300 (SFOS 22's), the unknown-software
path included: [sfos-versions-2026-10-25.md](../measurements/sfos-versions-2026-10-25.md). No board
running OPNsense here speaks SFOS 23's numbering yet.

A command sent by hand - `rpc_channel.command`, `rpc.cmd` - is in the drivers' numbering and moved
like their own, so a tool asking for command 44 gets the fast-path counters on all three versions;
the report says the number it went as when that differs. The two instruments, `hw.npuep.rpc_platform_test`
and `hw.octep.rpc_platform_test`, start the search that many numbers past 36, which on SFOS 21's and
22's numbering makes the block unfindable - how the unknown-software path is run on a board that
speaks a known one.

## What else SFOS 23 EAP 1 changed, beside the numbering

Read from the text dumps of both versions; none of it is something these drivers do:

- Two fast-path counters, above.
- A BPF program on the host, `xgs_npu_filter`, `npu_addr_filter` inside it, loaded before the
  management link comes up on every family: it reads the calling process's user, which reads as a
  rule about who may reach the coprocessor's management address.
- The OCTEON TX2 and TX2 98XX fast paths are not in the EAP 1 copy that was read.
- A new CPLD image for `AMDA0226-0001`, a BayHub eMMC controller firmware for the 1UL-80 and 1US-80
  boards, and an outlet temperature in `xgs-1us-sensors` for the 1US-80 boards.
- The `AMDA0228` arm of `xgs-check-uboot-version.sh` mended ([README.md](README.md)).

The device trees, the U-Boot images and environments, the PHY firmware and the remaining CPLD images
are byte for byte SFOS 22's.
