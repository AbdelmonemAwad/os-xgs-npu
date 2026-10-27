#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
#
# Read the coprocessor's per-packet-decision debug counters and print them with their names.
#
# These are the second of the four counter arrays - rpc command 43, LO_WORKER_DBG_CNT_READ - and
# they are the ones that answer "why was this frame not accelerated". FPCNTR_FROM_WIRE_TO_KN_FORCED
# says a frame was pushed to the host without a flow lookup; the NA_ ("not accelerated") name that
# moved by the same amount in this array says which test refused it. The pair is the diagnosis.
#
# Usage:
#   octep-pdcnt.sh              every counter that is not zero, with its name
#   octep-pdcnt.sh -a           all of them, including the zeros
#   octep-pdcnt.sh -s FILE      write a snapshot for -d to compare against
#   octep-pdcnt.sh -d FILE      print only what moved since that snapshot
#
# Ninety-nine names are known, in enum order: forty-nine from the DWARF of vendor version 22's
# coprocessor software, the rest from the vendor's own counter table (2026-10-07). The array is
# longer than that and the tail prints unnamed. On vendor versions 21 and 23 the names have not been
# checked against the software, and the tool says so. num_entries MUST be set or the far side answers
# nothing. The clear for this array is the driver's command 47 (DBG_CNT_CLR, sent as the number the
# coprocessor's version uses); the driver refuses it without rpc.allow_write - leave it that way.
#
set -u

NAMES=${NAMES:-$(dirname "$0")/pd-debug-cnt-names.txt}
DEV=${DEV:-dev.octep.0}
N=${N:-128}
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
sfos=$(sysctl -n "$DEV".rpc.sfos 2>/dev/null || echo 0)
[ "$sfos" = 22 ] || echo "the names are vendor version 22's; on this coprocessor (rpc.sfos $sfos) they are not checked" >&2

read_counters() {
	sysctl "$DEV".rpc.cmd=43 "$DEV".rpc.s_index=0 "$DEV".rpc.e_index=$((N - 1)) \
	       "$DEV".rpc.num_entries="$N" "$DEV".rpc.req_flags=0 > /dev/null || return 1
	sysctl "$DEV".rpc.post=1 > /dev/null || return 1
	pa=$(sysctl -n "$DEV".rpc.buf | sed -n 's/.*buffer at pa 0x\([0-9a-f]*\).*/\1/p')
	[ -n "$pa" ] || { echo "the far side returned no buffer" >&2; return 1; }
	# Two 32-bit words per counter, low word first; the first two words are the reply header.
	dd if=/dev/mem bs=4096 skip=$((0x$pa / 4096)) count=1 2>/dev/null |
	hexdump -v -e '1/4 "%u" "\n"' |
	awk -v n="$N" 'NR > 2 && NR <= 2 + n * 2 { if (NR % 2 == 1) lo = $1;
	    else print (NR - 4) / 2, lo + $1 * 4294967296 }'
}

now=$(read_counters) || exit 1

case $mode in
save)
	echo "$now" > "$file"
	echo "snapshot written to $file"
	;;
diff)
	[ -r "$file" ] || { echo "cannot read $file" >&2; exit 1; }
	echo "$now" | join "$file" - |
	awk -v names="$NAMES" '
		BEGIN { while ((getline line < names) > 0) if (line !~ /^#/) { split(line, f, " "); n[f[1]] = f[2] } }
		$2 != $3 { printf "%4d  %-44s %+14d   (%s -> %s)\n", $1, ($1 in n ? n[$1] : "-"), $3 - $2, $2, $3 }'
	;;
*)
	echo "$now" |
	awk -v names="$NAMES" -v all="$mode" '
		BEGIN { while ((getline line < names) > 0) if (line !~ /^#/) { split(line, f, " "); n[f[1]] = f[2] } }
		all == "all" || $2 != 0 { printf "%4d  %-44s %20d\n", $1, ($1 in n ? n[$1] : "-"), $2 }'
	;;
esac
