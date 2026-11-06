#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Read the coprocessor's platform database - its own board file, with whatever expansion module is
# fitted already folded in - from the coprocessor, and keep it for ports.py.
#
#   npu-platform.sh [output file]       default /var/db/os-xgs-npu/npu-platform.txt
#
# WHY THE CONSOLE. The coprocessor builds this database at every boot from the board file and the
# fitted module's VPD (the vendor's xgs-platform-init): every port's name as printed on the metal,
# its logical-interface number and its speed, and each slot's assembly number. Nothing the host
# reaches over PCIe carries it - NetAgent knows a port's tag and its address and not its name - and
# the console is the one place it can be read without running anything new on the coprocessor:
# `xgs-platform` is the vendor's own read tool, and nothing here writes.
#
# ASKED THROUGH npu-run.sh, AS EVERY OTHER READER. It takes the management link's ssh when that answers
# and the coprocessor's root console otherwise - logged in only if the prompt asks, and out again - under
# the console's one lock, with the console's logger (console_log.py) standing aside and what the
# coprocessor printed kept in its log, and it keeps the watch's URGENT mark (feeder.py). Until 2026-11-06
# this typed on the console itself, without that lock - which switch-jumbo.sh, mgmt-link.sh's tells and
# every npu-run.sh session hold - and with the logger left reading the port: its check for another
# reader looked for 'cat /dev/cuau2', which the logger is not, so the answer could go to the logger or
# its keystrokes into another caller's session. Waited for up to 30 s behind another session, three
# tries; exits 1 and writes nothing when none answers, and ports.py then falls back to what it can
# derive without a name.

OUT=${1:-/var/db/os-xgs-npu/npu-platform.txt}
RUN=/usr/local/opnsense/scripts/xgs/npu-run.sh

mkdir -p "$(dirname "${OUT}")"
try=0
while [ ${try} -lt 3 ]; do
    try=$((try + 1))
    # npu-run.sh's own reason, when it gives one, goes to stderr - the bring-up's log
    if got=$(NPU_RUN_WAIT=30 /bin/sh "${RUN}" 6 xgs-platform); then
        printf '%s\n' "${got}" | grep '^npu0\.' > "${OUT}.new"
        if [ -s "${OUT}.new" ]; then
            mv "${OUT}.new" "${OUT}"
            echo "npu-platform: $(grep -c . "${OUT}") keys in ${OUT}"
            exit 0
        fi
    fi
    sleep 3
done
rm -f "${OUT}.new"
echo "npu-platform: the coprocessor did not answer in three tries" >&2
exit 1
