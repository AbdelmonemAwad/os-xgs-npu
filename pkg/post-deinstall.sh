#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Run by pkg(8) as root after the files are gone.
#
# hidraw_load is left in loader.conf.local: harmless, possibly not ours, and removing somebody
# else's line is worse than leaving one behind.
#
# The family's plugins back in their own sections, now that the XGS section's menu file is gone:
# each writes its menu file again (scripts/<plugin>/place-menu.sh).
for s in /usr/local/opnsense/scripts/*/place-menu.sh; do
	[ -f "${s}" ] && /bin/sh "${s}" > /dev/null 2>&1 || true
done
rm -f /var/lib/php/tmp/opnsense_menu_cache.xml /tmp/opnsense_menu_cache.xml
exit 0
