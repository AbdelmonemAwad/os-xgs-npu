#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Read the coprocessor's platform database - its own board file, with whatever expansion module is
# fitted already folded in - through the coprocessor's console, and keep it for ports.py.
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
# The console is the coprocessor's root console: a login prompt that asks for no password. This logs
# in only if the prompt asks, reads, and logs out again if it logged in, so the console is left as it
# was found. Exits 1 and writes nothing when the console does not answer in three tries; ports.py then
# falls back to what it can derive without a name.

DEV=/dev/cuau2
OUT=${1:-/var/db/os-xgs-npu/npu-platform.txt}
TMP=/var/run/npu-console.$$

clean() { tr -d '\r' < "${TMP}" | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g'; }

pgrep -f "cat ${DEV}" > /dev/null 2>&1 && { echo "npu-platform: a reader already holds ${DEV}" >&2; exit 1; }
[ -c "${DEV}" ] || { echo "npu-platform: no ${DEV}" >&2; exit 1; }
stty -f "${DEV}.init" speed 115200 raw -echo > /dev/null 2>&1
stty -f "${DEV}" speed 115200 raw -echo > /dev/null 2>&1
mkdir -p "$(dirname "${OUT}")"

try=0
while [ ${try} -lt 3 ]; do
    try=$((try + 1))
    timeout 20 cat "${DEV}" > "${TMP}" 2>/dev/null &
    sleep 1
    printf '\r' > "${DEV}"; sleep 1
    LOGGED_IN=0
    if clean | grep -q 'login:'; then
        printf 'root\r' > "${DEV}"; sleep 2
        clean | grep -q 'assword' && { printf '\r' > "${DEV}"; sleep 2; }
        LOGGED_IN=1
    fi
    # The markers go out in two quoted halves: the console echoes the command line, and a marker
    # whole in it would match the echo instead of the output.
    printf 'echo "---NPU""BEGIN---"; xgs-platform; echo "---NPU""END---"\r' > "${DEV}"
    sleep 6
    [ ${LOGGED_IN} = 1 ] && printf 'exit\r' > "${DEV}"
    wait
    if clean | grep -q '^---NPUEND---'; then
        clean | awk '/^---NPUBEGIN---/ {on=1; next} /^---NPUEND---/ {on=0} on' | grep '^npu0\.' > "${OUT}.new"
        if [ -s "${OUT}.new" ]; then
            mv "${OUT}.new" "${OUT}"
            rm -f "${TMP}"
            echo "npu-platform: $(grep -c . "${OUT}") keys in ${OUT}"
            exit 0
        fi
    fi
    sleep 3
done
rm -f "${TMP}" "${OUT}.new"
echo "npu-platform: the console did not answer in three tries" >&2
exit 1
