#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Read the ARMADA coprocessor's 176 fast-path counters and print them with their names.
#
# The far side answers rpc command 44 (LO_WORKER_SYS_CNT_READ) with an array of 64-bit values and
# no names, and the npuep driver's by-hand command sysctl shows at most 512 bytes of a reply, so
# the array is read in three windows of 64 entries. fpcntr-names-armada.txt holds the enum this
# family's own usfp_rh.ko carries, in order; its OCTEON TX cousin has a different order and its
# own file and tool (octep-fpcnt.sh).
#
#   npuep-fpcnt.sh          the counters that are not zero
#   npuep-fpcnt.sh -a       every counter
#   npuep-fpcnt.sh -d       the counters that moved since the last -d run, with the delta
#
# NAMES points at fpcntr-names-armada.txt; it defaults to the copy next to this script.
set -u
NAMES=${NAMES:-$(dirname "$0")/fpcntr-names-armada.txt}
DEV=${DEV:-dev.npuep.0}
LAST=${LAST:-/var/db/os-xgs-npu/npuep-fpcnt.last}
mode=${1:-}

[ -r "$NAMES" ] || { echo "cannot read $NAMES" >&2; exit 1; }
TOTAL=$(grep -c '^[0-9]' "$NAMES")

# One window: s_index, num_entries and e_index as the table-read request wants them, little endian,
# and the reply's bytes turned into one 64-bit value per line.
read_window() {
	s=$1; n=$2; e=$((s + n - 1))
	req=$(printf '%02x %02x %02x %02x  %02x %02x  00 00  %02x %02x %02x %02x' \
	    $((s & 255)) $(((s >> 8) & 255)) $(((s >> 16) & 255)) $(((s >> 24) & 255)) \
	    $((n & 255)) $(((n >> 8) & 255)) \
	    $((e & 255)) $(((e >> 8) & 255)) $(((e >> 16) & 255)) $(((e >> 24) & 255)))
	# sysctl(8) reports "Invalid argument" on this set even though the command went out and was
	# answered - the handler's report after the send does not fit the write-path's buffer - so
	# the exit status says nothing; the reply's own text is what is checked.
	sysctl "$DEV".rpc_channel.command="2c $req" > /dev/null 2>&1
	reply=$(sysctl -n "$DEV".rpc_channel.command 2>/dev/null)
	case "$reply" in
	"command 44, "*"answered, rc 0, "*) ;;
	*) echo "$reply" | cut -c1-120 >&2; return 1 ;;
	esac
	echo "$reply" |
	    sed -n 's/.* bytes back://p' | tr -s ' ' '\n' | grep -v '^$' |
	    awk -v base="$s" '
	    # FreeBSD awk has no strtonum; a hex byte is two digits looked up by hand.
	    function hx(s,   d, c, k) { d = 0; for (k = 1; k <= length(s); k++) { c = index("0123456789abcdef", substr(s, k, 1)); d = d * 16 + (c - 1) } return d }
	    {
		b[(NR - 1) % 8] = $1
		if (NR % 8 == 0) {
			v = 0
			for (i = 7; i >= 0; i--) v = v * 256 + hx(b[i])
			printf "%d %.0f\n", base + int((NR - 1) / 8), v
		}
	    }'
}

read_counters() {
	i=0
	while [ "$i" -lt "$TOTAL" ]; do
		n=$((TOTAL - i)); [ "$n" -gt 64 ] && n=64
		read_window "$i" "$n" || { echo "the far side did not answer window $i" >&2; return 1; }
		i=$((i + n))
	done
}

now=$(read_counters) || exit 1
[ -n "$now" ] || { echo "no counters came back" >&2; exit 1; }

case "$mode" in
-d)
	mkdir -p "$(dirname "$LAST")"
	if [ -r "$LAST" ]; then
		echo "$now" | awk -v names="$NAMES" -v last="$LAST" '
			BEGIN { while ((getline line < names) > 0) if (line !~ /^#/) { split(line, f, " "); n[f[1]] = f[2] }
			        while ((getline line < last) > 0) { split(line, f, " "); p[f[1]] = f[2] } }
			{ d = $2 - (($1 in p) ? p[$1] : 0); if (d != 0) printf "%3d %-56s %14.0f  %+.0f\n", $1, n[$1], $2, d }'
	else
		echo "no previous reading; this one is kept" >&2
	fi
	echo "$now" > "$LAST"
	;;
-a)
	echo "$now" | awk -v names="$NAMES" '
		BEGIN { while ((getline line < names) > 0) if (line !~ /^#/) { split(line, f, " "); n[f[1]] = f[2] } }
		{ printf "%3d %-56s %14.0f\n", $1, n[$1], $2 }'
	;;
*)
	echo "$now" | awk -v names="$NAMES" '
		BEGIN { while ((getline line < names) > 0) if (line !~ /^#/) { split(line, f, " "); n[f[1]] = f[2] } }
		$2 != 0 { printf "%3d %-56s %14.0f\n", $1, n[$1], $2 }'
	;;
esac
