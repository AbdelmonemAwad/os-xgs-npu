#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Run by pkg(8) as root after the files are in place.
set -e

# hidraw is for the ARMADA reset bridge, and only on a board this project has been run on - see
# src/opnsense/scripts/xgs/board.sh. On the XGS 3300 the MCP2210 that node would reach is wired to the
# coprocessor's reset and boot flash.
case "$(/bin/sh /usr/local/opnsense/scripts/xgs/board.sh 2>/dev/null)" in
*" armada")
	;;
*)
	exit 0
	;;
esac

# Written literally: the old printf '%cYES%c' 34 34 printed the first character of "34" and left
# hidraw_load=3YES3, which the loader reads as not-YES. A line in that form is replaced.
if ! grep -q '^hidraw_load="YES"$' /boot/loader.conf.local 2>/dev/null; then
    if [ -f /boot/loader.conf.local ]; then
        sed -i '' '/^hidraw_load=/d' /boot/loader.conf.local
    fi
    echo 'hidraw_load="YES"' >> /boot/loader.conf.local
fi
kldstat -q -m hidraw || kldload hidraw 2>/dev/null || true

# Deliberately does NOT release the NPU here. An install should not change what the hardware is
# doing until the next boot, when the hook runs in a context where its output is logged and the
# machine is in a known state.
exit 0
