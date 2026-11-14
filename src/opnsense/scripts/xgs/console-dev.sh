#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Which of the host's serial devices is the coprocessor's console: the UART at I/O port 0x3e8, found
# by that address among the kernel's devices - for everything here that talks to the console or reads
# it (npu-run.sh, npu-sysrq.sh, switch-jumbo.sh, console_log.py, npuctl/mgmt-link.sh).
#
#   console-dev.sh                 the console's device, /dev/cuauN
#   console-dev.sh -v              the same, and on a second line how it was found
#   console-dev.sh [-v] <address>  the same for the UART at another of the four legacy COM ports'
#                                  I/O addresses - 0x3f8, 0x2f8, 0x3e8 (the default), 0x2e8 - such as
#                                  the front panel's at 0x2f8, which panel/panel.sh writes to
#
# Each address has the unit FreeBSD gave it where this project has run as its fallback - /dev/cuau0,
# 1, 2 and 3 in that order: below, /dev/cuau2 stands for the fallback of the address asked for, and
# 0x3e8 for that address.
#
# Exit 0 for the UART at 0x3e8. Otherwise /dev/cuau2, as every script here took it before: exit 1
# when nothing could be said of this host's UARTs, and 2 when the kernel's devices were read and no
# UART is at 0x3e8 - /dev/cuau2 is then not the console's port, and npu-run.sh does not detach it and
# attach it again for an interrupt that does not come.
#
# WHY BY ADDRESS. The console is the host's legacy COM port at 0x3e8 on both families - the vendor's
# Linux names it ttyS2 by that address - but FreeBSD numbers its UARTs in the order it finds them, so
# the unit that port gets depends on the board: uart2 on the XGS 136 r1 and the XGS 3300, where this
# project has run; uart4 on an XGS 107, where the chipset's own two UARTs, memory-mapped at 0xfedc6000
# and 0xfedc8000, were numbered first (#748's support bundle); and on an XGS 136 r2 (BIOS Z136-008)
# uart2 is the chipset's UART at 0xfedc9000. #601's log from that board has uart2 detached and
# attached again 9 times, 11 to 25 s apart - as npu-run.sh does it to a UART that a byte written
# brings no interrupt from.
#
# FROM THE KERNEL, AT EVERY CALL. devinfo -rv: each device, and under it each kind of resource it holds
# with their ranges - a UART's line, then "I/O ports:" and "0x3e8-0x3ef". Without devinfo, this boot's
# messages (/var/run/dmesg.boot): "uart4: <16550 or compatible> port 0x3e8-0x3ef irq 5 on acpi0".
# Nothing is kept: a UART detached and attached again is found under the number it has then, and each
# boot is read afresh.
#
# THE KNOB. kenv xgs.console.by_address=0 - at run time, or for every boot as a line of
# /boot/loader.conf.local - gives /dev/cuau2 and exit 1 whatever the kernel says: the console, and
# npu-run.sh's attaching it again, as before this script.
#
# XGS_CONSOLE_DEVINFO names a file read in place of the kernel, for testing this away from the
# appliance: saved devinfo -rv output, or boot messages as a support bundle's dmesg.txt keeps them -
# of which the last boot counts.

: ${KENV:=/bin/kenv}
: ${DEVINFO:=/usr/sbin/devinfo}
: ${DMESG_BOOT:=/var/run/dmesg.boot}
VERBOSE=0
[ "${1:-}" = -v ] && { VERBOSE=1; shift; }
ADDR=$(printf '%s' "${1-0x3e8}" | /usr/bin/tr 'A-FX' 'a-fx')
case ${ADDR} in
0x3f8) FALLBACK=/dev/cuau0 ;;
0x2f8) FALLBACK=/dev/cuau1 ;;
0x3e8) FALLBACK=/dev/cuau2 ;;
0x2e8) FALLBACK=/dev/cuau3 ;;
*) echo "usage: console-dev.sh [-v] [0x3f8|0x2f8|0x3e8|0x2e8]" >&2; exit 64 ;;
esac
# The unit the fallback is: what holds it, when no UART is at ADDR, is said with the answer.
FBU=uart${FALLBACK##*/cuau}

# The console's device on one line and how it was found on the next, from a list of the kernel's
# devices on stdin - devinfo -rv's, or boot messages - named <what> in the second line. Its exit is
# this script's.
pick() { # <what>
	/usr/bin/awk -v what="$1" -v fallback="${FALLBACK}" -v addr="${ADDR}" -v fbu="${FBU}" '
	{
		t = $0
		sub(/\r$/, "", t)
		sub(/^[ \t]+/, "", t)
	}
	# Boot messages: each boot begins with this line, and only the last one counts.
	index(t, "---<<BOOT>>---") == 1 {
		unit = ""; range = ""; u2 = ""; dev = ""; kind = ""
		next
	}
	{
		sub(/^\[[0-9.]+\] /, "", t)	# a boot message'\''s time stamp (kern.msgbuf_show_timestamp)
	}
	# Boot messages: "uart4: <16550 or compatible> port 0x3e8-0x3ef irq 5 on acpi0", or on a bus
	# found by hints "... at port 0x3e8-0x3ef irq 3 on isa0".
	t ~ /^uart[0-9]+: / {
		name = substr(t, 1, index(t, ":") - 1)
		low = tolower(t)
		if (match(low, " port " addr "(-0x[0-9a-f]+)?")) {
			c = substr(low, RSTART + RLENGTH, 1)
			if (c == "" || c == " " || c == ",") {
				unit = name
				range = substr(low, RSTART + 6, RLENGTH - 6)
			}
		} else if (name == fbu && match(low, / iomem 0x[0-9a-f]+(-0x[0-9a-f]+)?/)) {
			u2 = "I/O memory " substr(low, RSTART + 7, RLENGTH - 7)
		} else if (name == fbu && match(low, / port 0x[0-9a-f]+(-0x[0-9a-f]+)?/)) {
			u2 = "I/O port " substr(low, RSTART + 6, RLENGTH - 6)
		}
		dev = ""
		next
	}
	# devinfo -rv: a range of the kind named above it, held by the device above that.
	t ~ /^(0x[0-9a-fA-F]+|[0-9]+)(-(0x[0-9a-fA-F]+|[0-9]+))?$/ {
		if (dev ~ /^uart[0-9]+$/) {
			low = tolower(t)
			if (kind == "I/O ports" && (low == addr || index(low, addr "-") == 1)) {
				unit = dev
				range = low
			} else if (dev == fbu && u2 == "" && kind == "I/O ports") {
				u2 = "I/O port " low
			} else if (dev == fbu && u2 == "" && kind == "I/O memory addresses") {
				u2 = "I/O memory " low
			}
		}
		next
	}
	# devinfo -rv: "I/O ports:", "I/O memory addresses:", "Interrupt request lines:" and the like.
	t ~ /:$/ {
		kind = substr(t, 1, length(t) - 1)
		next
	}
	# devinfo -rv: a device, its name first - a UART not attached is "unknown", and is not taken. The
	# whole tree begins with nexus0: one without a UART at the address says there is none.
	{
		split(t, w, " ")
		dev = w[1]
		kind = ""
		if (dev == "nexus0")
			tree = 1
	}
	END {
		if (unit != "") {
			print "/dev/cuau" substr(unit, 5)
			print unit " at I/O port " range " (" what ")"
			exit 0
		}
		print fallback
		if (tree || u2 != "") {
			print "no UART at I/O port " addr " (" what "), and " (u2 != "" ? fbu " at " u2 : "no " fbu) \
			    ": " fallback " is not the console'\''s port"
			exit 2
		}
		if (NR == 0)
			print "nothing read from " what ": " fallback ", as before"
		else
			print "no UART at I/O port " addr " in " what ", nor what " fbu " is: " fallback ", as before"
		exit 1
	}'
}

if [ -n "${XGS_CONSOLE_DEVINFO:-}" ]; then
	got=$(pick "${XGS_CONSOLE_DEVINFO}" < "${XGS_CONSOLE_DEVINFO}")
	rc=$?
elif [ "$(${KENV} -q xgs.console.by_address 2>/dev/null)" = 0 ]; then
	got="${FALLBACK}
kenv xgs.console.by_address=0: ${FALLBACK}, as before"
	rc=1
else
	got=$(${DEVINFO} -rv 2>/dev/null | pick "devinfo -rv")
	rc=$?
	if [ ${rc} = 1 ] && [ -r "${DMESG_BOOT}" ]; then
		got=$(pick "${DMESG_BOOT}" < "${DMESG_BOOT}")
		rc=$?
	fi
fi
# A list that could not be read, or an awk that did not run, says nothing either.
case ${rc} in
0|1|2) ;;
*) got= ;;
esac
if [ -z "${got}" ]; then
	got="${FALLBACK}
nothing could be read of this host's UARTs: ${FALLBACK}, as before"
	rc=1
fi
printf '%s\n' "${got}" | sed -n 1p
[ ${VERBOSE} = 1 ] && printf '%s\n' "${got}" | sed -n 2p
exit ${rc}
