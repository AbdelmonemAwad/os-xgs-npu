#!/bin/sh
#
# Print the far side's 182 fast-path counters as "index value", one per line, nothing else.
#
# This is the reader tools/octep-fairness.sh was written against, and the published fairness sweep in
# docs/measurements/xgs3300.md came out of it. It lived only on the appliance, under the experiment
# directory, until that directory was about to be removed and a review noticed that a tool in this
# repository could not run without it. octep-fpcnt.sh is the one to read by eye - it names every
# counter and diffs snapshots; this one is the one to parse.
#
# num_entries MUST be set or the far side answers nothing. Never pass C to a counter read.
sysctl dev.octep.0.rpc.cmd=44 dev.octep.0.rpc.s_index=0 dev.octep.0.rpc.e_index=181 \
       dev.octep.0.rpc.num_entries=182 dev.octep.0.rpc.req_flags=0 > /dev/null
sysctl dev.octep.0.rpc.post=1 > /dev/null
PA=$(sysctl -n dev.octep.0.rpc.buf | sed -n 's/.*buffer at pa 0x\([0-9a-f]*\).*/\1/p')
[ -n "$PA" ] || { echo "no buffer" >&2; exit 1; }
dd if=/dev/mem bs=4096 skip=$((0x$PA / 4096)) count=1 2>/dev/null |
hexdump -v -e '1/4 "%u" "\n"' |
awk 'NR>2 { if (NR % 2 == 1) lo = $1; else print (NR-4)/2, lo + $1*4294967296 }'
