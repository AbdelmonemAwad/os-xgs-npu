#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Which Sophos XGS board this is, read positively from the board's own assembly record.
#
# Prints "<assembly> <family>" - "201 armada", "202 octeontx" - and exits 0 for a board this project
# knows, with a third word, "rnd", for a model this project has not been run on: research and
# development, untested, because there is no such appliance here to test on. Anything else prints
# "<assembly> untested" (or "none untested") and exits 1. Everything here that touches hardware asks
# this first, and does nothing on a 1. The family is always the second word; read it as a word.
#
# The assembly number is inside the serial of SMBIOS type 2, and nowhere else: the XGS 136's reads
# a prefix, then AMDA0201-0003, then the unit's serial; the XGS 3300's the same shape with
# AMDA0202-0004. The product string ("XGS") is the same across the range and the version is a board
# revision, so neither names the board. The vendor's own xgs-usb-spi-flash keys its per-board tables
# on this same number.
#
# WHY POSITIVELY. This used to be decided by elimination - "no OCTEON TX endpoint on PCI, so this is
# the ARMADA appliance" - and the ARMADA reset tool itself carried the XGS 136's board number as a
# constant. On the XGS 3300 that sent the 136's pin values to the 3300's MCP2210, which on that board
# is wired to the coprocessor's reset and boot flash, and the coprocessor dropped off the bus until the
# bridge was put back to its power-on state. The PCI id cannot stand in for this on the ARMADA side
# either: the 136's coprocessor comes out of power-on held in reset, so it is not on the bus at the
# moment the hook that releases it has to decide.
#
# ~~Only boards this project has been run on are named. The XGS 116 and 138 are ARMADA desktops like the
# 136 on paper, but nothing here has run on them, and what a wrong guess drives is a reset line.~~
# Every board of both families is named since 2026-10-21, and every model this project has not been run
# on is marked "rnd". What the guess drove is no longer a guess: the reset lines' values for every
# ARMADA board are the vendor's own, read out of xgs-usb-spi-flash's table (scripts/npuctl/mcp2210.py),
# and each model's ports are its board file's (scripts/xgs/models.py). Run here: the XGS 136
# (AMDA0201-0003) and the XGS 3300 (AMDA0202-0004). Everything else is from the vendor's files only.
#
# XGS_PLANAR_SERIAL overrides the SMBIOS read, for testing this away from the appliance.

SERIAL=${XGS_PLANAR_SERIAL:-$(/bin/kenv -q smbios.planar.serial 2>/dev/null)}
BOARD=$(echo "${SERIAL}" | /usr/bin/sed -n 's/.*AMDA0*\([0-9][0-9]*\)-.*/\1/p')

VARIANT=$(echo "${SERIAL}" | /usr/bin/sed -n 's/.*AMDA0*[0-9][0-9]*-\([0-9][0-9][0-9][0-9]\).*/\1/p')
case "${BOARD}-${VARIANT}" in
201-0003|202-0004)	RND="" ;;		# the XGS 136 and the XGS 3300: run here
*)			RND=" rnd" ;;		# research and development: untested, no appliance here
esac

case "${BOARD}" in
200)	echo "200 armada${RND}" ;;		# XGS 87, 87w, 107, 107w
201)	echo "201 armada${RND}" ;;		# XGS 126, 126w, 136, 136w
208)	echo "208 armada${RND}" ;;		# XGS 116, 116w
224)	echo "224 armada${RND}" ;;		# XGS 138
202)	echo "202 octeontx${RND}" ;;		# XGS 2100, 2300, 3100, 3300
*)	echo "${BOARD:-none} untested"
	exit 1
	;;
esac
exit 0
