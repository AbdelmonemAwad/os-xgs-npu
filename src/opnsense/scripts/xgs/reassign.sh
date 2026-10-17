#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Let the next boot assign the front ports again, from the settings page: remove the first-boot
# assignment's mark (rc.syshook.d/early/30-xgs-assign). The assignment still acts only on a box whose
# front ports are all unassigned, so on a box in service the next boot marks it again and changes
# nothing.
rm -f /conf/os-xgs-npu-assign
/usr/bin/logger -t xgs-assign -p daemon.notice "the mark was removed from the settings page; the next boot asks again"
echo OK
