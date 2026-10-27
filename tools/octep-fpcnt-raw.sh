#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
#
# Print the far side's fast-path counters as "index value", one per line, nothing else.
#
# This is the reader tools/octep-fairness.sh was written against, and the published fairness sweep in
# docs/measurements/xgs3300.md came out of it. It lived only on the appliance, under the experiment
# directory, until that directory was about to be removed and a review noticed that a tool in this
# repository could not run without it. octep-fpcnt.sh is the one to read by eye - it names every
# counter and diffs snapshots; this one is the one to parse.
#
# How many: the named counters of the vendor version the coprocessor runs (rpc.sfos) - 176 on
# vendor version 21, 182 on 22, 184 on 23, as many as fpcntr-names-sfos<that>.txt lists - and all 255 of the
# array when the version is not known. The driver moves command 44 to the coprocessor's own number.
#
# num_entries MUST be set or the far side answers nothing. Never pass C to a counter read.
sfos=$(sysctl -n dev.octep.0.rpc.sfos 2>/dev/null || echo 0)
f=$(dirname "$0")/fpcntr-names-sfos${sfos}.txt
if [ -r "$f" ]; then N=$(grep -c '^[0-9]' "$f"); else N=255; fi
sysctl dev.octep.0.rpc.cmd=44 dev.octep.0.rpc.s_index=0 dev.octep.0.rpc.e_index=$((N - 1)) \
       dev.octep.0.rpc.num_entries=$N dev.octep.0.rpc.req_flags=0 > /dev/null
sysctl dev.octep.0.rpc.post=1 > /dev/null
PA=$(sysctl -n dev.octep.0.rpc.buf | sed -n 's/.*buffer at pa 0x\([0-9a-f]*\).*/\1/p')
[ -n "$PA" ] || { echo "no buffer" >&2; exit 1; }
dd if=/dev/mem bs=4096 skip=$((0x$PA / 4096)) count=1 2>/dev/null |
hexdump -v -e '1/4 "%u" "\n"' |
awk -v last=$((2 + 2 * N)) 'NR>2 && NR<=last { if (NR % 2 == 1) lo = $1; else print (NR-4)/2, lo + $1*4294967296 }'
