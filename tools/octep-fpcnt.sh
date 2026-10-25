#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
#
# Read the coprocessor's fast-path counters and print them with their names.
#
# The far side answers rpc command 44 with an array of 64-bit values and no names at all, so a bare
# dump is a column of integers whose meaning has to be remembered. The names, and how many of the
# array's 255 entries carry one, belong to the SFOS whose software the coprocessor runs: the driver
# says which in rpc.sfos, and fpcntr-names-sfos<that>.txt holds its enum in order - 176 on SFOS 21,
# 182 on 22, 184 on 23. With it, a diff reads like a sentence - TX_DROP_QUEUE_FULL rose by three
# million, TX_WIRE_ERR by seventy-six - and that sentence is usually the whole diagnosis. The driver
# moves the command's number to the coprocessor's own (rpc.cmd_shift), so 44 is right on all three.
# When the version is not known, all 255 are printed by index, with no name.
#
# Usage:
#   octep-fpcnt.sh              every counter that is not zero, with its name
#   octep-fpcnt.sh -a           all of them, including the zeros
#   octep-fpcnt.sh -s FILE      write a snapshot for -d to compare against
#   octep-fpcnt.sh -d FILE      print only what moved since that snapshot
#   octep-fpcnt.sh -z           zero them on the coprocessor first, so a measurement starts there
#                               (rpc.counters_clear; discovery 13), then print what is not zero
#
# NAMES overrides the list; by default it is the one next to this script for the driver's sfos.
#
# num_entries MUST be set or the far side answers nothing. Never pass C to a counter read.
#
set -u

DEV=${DEV:-dev.octep.0}
mode=nonzero
file=

while [ $# -gt 0 ]; do
	case $1 in
	-a) mode=all ;;
	-s) mode=save; file=${2:?-s needs a file}; shift ;;
	-d) mode=diff; file=${2:?-d needs a file}; shift ;;
	# the workers take the clear off its ring on their own time: measured, zero within a second
	-z) sysctl "$DEV".rpc.counters_clear=1 > /dev/null || {
		echo "the coprocessor did not take the clear (rpc.counters_clear)" >&2; exit 1; }
	    sleep 2 ;;
	*) echo "usage: $(basename "$0") [-z] [-a | -s FILE | -d FILE]" >&2; exit 2 ;;
	esac
	shift
done

sfos=$(sysctl -n "$DEV".rpc.sfos 2>/dev/null || echo 0)
NAMES=${NAMES:-$(dirname "$0")/fpcntr-names-sfos${sfos}.txt}
if [ -r "$NAMES" ]; then
	TOTAL=$(grep -c '^[0-9]' "$NAMES")
else
	echo "the coprocessor's software is not one this tool has names for (rpc.sfos $sfos):" \
	    "every entry is printed by its index alone" >&2
	NAMES=/dev/null
	TOTAL=255
fi

read_counters() {
	sysctl "$DEV".rpc.cmd=44 "$DEV".rpc.s_index=0 "$DEV".rpc.e_index=$((TOTAL - 1)) \
	       "$DEV".rpc.num_entries="$TOTAL" "$DEV".rpc.req_flags=0 > /dev/null || return 1
	sysctl "$DEV".rpc.post=1 > /dev/null || return 1
	pa=$(sysctl -n "$DEV".rpc.buf | sed -n 's/.*buffer at pa 0x\([0-9a-f]*\).*/\1/p')
	[ -n "$pa" ] || { echo "the far side returned no buffer" >&2; return 1; }
	# Two 32-bit words per counter, low word first; the first two words are the reply header.
	dd if=/dev/mem bs=4096 skip=$((0x$pa / 4096)) count=1 2>/dev/null |
	hexdump -v -e '1/4 "%u" "\n"' |
	awk -v last=$((2 + 2 * TOTAL)) 'NR > 2 && NR <= last { if (NR % 2 == 1) lo = $1;
	    else print (NR - 4) / 2, lo + $1 * 4294967296 }'
}

now=$(read_counters) || exit 1

case $mode in
save)
	echo "$now" > "$file"
	echo "snapshot of $TOTAL counters written to $file"
	;;
diff)
	[ -r "$file" ] || { echo "cannot read $file" >&2; exit 1; }
	echo "$now" | join "$file" - |
	awk -v names="$NAMES" '
		BEGIN { while ((getline line < names) > 0) if (line !~ /^#/) { split(line, f, " "); n[f[1]] = f[2] } }
		$2 != $3 { printf "%4d  %-48s %+14d   (%s -> %s)\n", $1, (($1 in n) ? n[$1] : "-"), $3 - $2, $2, $3 }'
	;;
*)
	echo "$now" |
	awk -v names="$NAMES" -v all="$mode" '
		BEGIN { while ((getline line < names) > 0) if (line !~ /^#/) { split(line, f, " "); n[f[1]] = f[2] } }
		all == "all" || $2 != 0 { printf "%4d  %-48s %20d\n", $1, (($1 in n) ? n[$1] : "-"), $2 }'
	;;
esac
