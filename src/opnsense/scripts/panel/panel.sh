#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Write to the XGS front panel, which is an ordinary host UART and needs nothing from the
# coprocessor.
#
#   panel.sh init                    the vendor's own startup sequence
#   panel.sh clear
#   panel.sh line1 "text"            and line2
#   panel.sh say "top" "bottom"      init, clear, both lines
#   panel.sh raw 0x28                one escaped instruction, for experimenting
#
# THE PORT. FreeBSD probes it as uart1 at 0x2f8 irq 4, which is byte for byte the /dev/ttyS1 the
# vendor's lcdd drives, so it is cuau1 here. 2400 baud, raw. One process owns a UART at a time and
# nothing on OPNsense claims this one.
#
# THE PROTOCOL, and every byte of it is read out of the vendor's own lcdd rather than guessed.
# That binary is stripped, but the command bytes are not: they sit in .data and are written one at
# a time, the escape and then the instruction, through two write() calls. Disassembled:
#
#   .data 0x8057030 = 0xfe     the escape, written before every instruction
#         0x8057014 = 0x28     function set, two lines
#         0x805701c = 0x01     clear
#         0x8057018 = 0xc0     set address to the start of line two
#         0x805702c = 0x06     entry mode, increment
#         0x8057020 = 0x18     shift display left      0x8057024 = 0x1c   shift right
#         0x8057028 = 0x40     set CGRAM address, for a custom glyph
#
#   and inline: movw $0x0dfe -> FE 0D, movw $0x0efe -> FE 0E, and a three-byte FE 58 FD.
#
# So it is the HD44780 instruction set behind an 0xFE escape - accepted by an EZIO bridge on
# the panel board, a GIFAR GMRU20X4 with its own crystal, rather than by the glass directly.
# Knowing that is what makes the
# difference between this and a guess, because the first attempt here sent clear and addressing
# with no FUNCTION SET in front of them and the panel showed a row of identical characters - a
# display that has not been told how many lines it has does not have a line two to address.
#
# WHAT IS READ AND WHAT IS CONFIRMED. The byte values above are read from the vendor's binary and
# are not in doubt. Whether this sequence paints the panel correctly has not been confirmed by
# anyone looking at it yet - see issue #165 - so treat a surprise here as this script's fault
# rather than the display's.
#
# IT NEVER OPENS THE PORT TWICE. Every sequence goes through one descriptor, because setting the
# speed on the .init device and then writing through a fresh open is how the first attempt sent
# its bytes at the wrong rate.

set -u

: ${PORT:=/dev/cuau1}
: ${SPEED:=2400}
: ${DELAY:=0.2}

usage() {
	echo "usage: ${0##*/} init|clear|line1 TEXT|line2 TEXT|say TOP BOTTOM|raw 0xNN" >&2
	exit 2
}

[ $# -ge 1 ] || usage
[ -c "${PORT}" ] || { echo "${PORT} is not there" >&2; exit 1; }

exec 3<> "${PORT}" || { echo "cannot open ${PORT}" >&2; exit 1; }
stty -f "${PORT}" "${SPEED}" raw -echo clocal cs8 -parenb -cstopb || exit 1

# One instruction: the escape, then the byte, each on its own, the way lcdd does it.
cmd() {
	printf '\376' >&3
	sleep "${DELAY}"
	printf "\\$(printf '%03o' "$1")" >&3
	sleep "${DELAY}"
}

text() { printf '%s' "$1" >&3; }

do_init() {
	cmd 0x28		# function set, two lines
	cmd 0x06		# entry mode, increment
	cmd 0x0c		# display on, cursor off
	cmd 0x01		# clear
	sleep 1			# a clear is the slow instruction on this controller
}

case "$1" in
init)	do_init ;;
clear)	cmd 0x01; sleep 1 ;;
line1)	[ $# -eq 2 ] || usage; cmd 0x80; text "$2" ;;
line2)	[ $# -eq 2 ] || usage; cmd 0xc0; text "$2" ;;
say)	[ $# -eq 3 ] || usage
	do_init
	cmd 0x80; text "$2"
	cmd 0xc0; text "$3" ;;
raw)	[ $# -eq 2 ] || usage; cmd "$2" ;;
*)	usage ;;
esac

exec 3>&-
exit 0
