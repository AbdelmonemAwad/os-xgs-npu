#!/bin/sh
#
# Read the coprocessor's 182 fast-path counters and print them with their names.
#
# The far side answers rpc command 44 with an array of 64-bit values and no names at all, so a bare
# dump is a column of integers whose meaning has to be remembered. fpcntr-names.txt holds the enum
# the array is ordered by; with it, a diff reads like a sentence - TX_DROP_QUEUE_FULL rose by three
# million, TX_WIRE_ERR by seventy-six - and that sentence is usually the whole diagnosis.
#
# Usage:
#   octep-fpcnt.sh              every counter that is not zero, with its name
#   octep-fpcnt.sh -a           all 182, including the zeros
#   octep-fpcnt.sh -s FILE      write a snapshot for -d to compare against
#   octep-fpcnt.sh -d FILE      print only what moved since that snapshot
#
# NAMES points at fpcntr-names.txt; it defaults to the copy next to this script.
#
# num_entries MUST be set or the far side answers nothing. Never pass C to a counter read.
#
set -u

NAMES=${NAMES:-$(dirname "$0")/fpcntr-names.txt}
DEV=${DEV:-dev.octep.0}
mode=nonzero
file=

while [ $# -gt 0 ]; do
	case $1 in
	-a) mode=all ;;
	-s) mode=save; file=${2:?-s needs a file}; shift ;;
	-d) mode=diff; file=${2:?-d needs a file}; shift ;;
	*) echo "usage: $(basename "$0") [-a | -s FILE | -d FILE]" >&2; exit 2 ;;
	esac
	shift
done

[ -r "$NAMES" ] || { echo "cannot read $NAMES" >&2; exit 1; }

read_counters() {
	sysctl "$DEV".rpc.cmd=44 "$DEV".rpc.s_index=0 "$DEV".rpc.e_index=181 \
	       "$DEV".rpc.num_entries=182 "$DEV".rpc.req_flags=0 > /dev/null || return 1
	sysctl "$DEV".rpc.post=1 > /dev/null || return 1
	pa=$(sysctl -n "$DEV".rpc.buf | sed -n 's/.*buffer at pa 0x\([0-9a-f]*\).*/\1/p')
	[ -n "$pa" ] || { echo "the far side returned no buffer" >&2; return 1; }
	# Two 32-bit words per counter, low word first; the first two words are the reply header.
	dd if=/dev/mem bs=4096 skip=$((0x$pa / 4096)) count=1 2>/dev/null |
	hexdump -v -e '1/4 "%u" "\n"' |
	awk 'NR > 2 && NR <= 366 { if (NR % 2 == 1) lo = $1;
	    else print (NR - 4) / 2, lo + $1 * 4294967296 }'
}

now=$(read_counters) || exit 1

case $mode in
save)
	echo "$now" > "$file"
	echo "snapshot of 182 counters written to $file"
	;;
diff)
	[ -r "$file" ] || { echo "cannot read $file" >&2; exit 1; }
	echo "$now" | join "$file" - |
	awk -v names="$NAMES" '
		BEGIN { while ((getline line < names) > 0) if (line !~ /^#/) { split(line, f, " "); n[f[1]] = f[2] } }
		$2 != $3 { printf "%4d  %-48s %+14d   (%s -> %s)\n", $1, n[$1], $3 - $2, $2, $3 }'
	;;
*)
	echo "$now" |
	awk -v names="$NAMES" -v all="$mode" '
		BEGIN { while ((getline line < names) > 0) if (line !~ /^#/) { split(line, f, " "); n[f[1]] = f[2] } }
		all == "all" || $2 != 0 { printf "%4d  %-48s %20d\n", $1, n[$1], $2 }'
	;;
esac
