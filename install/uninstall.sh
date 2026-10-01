#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Remove everything this repository installs.
#
# It does not put the coprocessor back into reset. Leaving a running NPU running is the less
# surprising of the two options, and a power cycle restores the factory state anyway - the
# release is written to the bridge's volatile settings and never to its NVRAM.
set -e

PREFIX=${PREFIX:-/usr/local}

if [ "$(id -u)" != "0" ]; then
    echo "run this as root"
    exit 1
fi

if kldstat -q -n npuep; then
    echo "== npuep is loaded; unload it yourself when you are ready =="
    echo "   kldunload npuep"
    echo "   (pulse the NPU's reset first - it raises doorbells by writing the MSI message"
    echo "    itself, so freed vectors mean its writes land on whatever replaces them)"
fi

rm -f "${PREFIX}/etc/rc.syshook.d/early/06-npuctl"
rm -f "${PREFIX}/etc/rc.syshook.d/early/07-npuep"
rm -f "${PREFIX}/etc/rc.syshook.d/early/08-octep"

# And the module, so the next boot cannot load a driver whose plugin is gone. The source is
# untouched - it lives in the repository, not here.
rm -f /boot/modules/npuep.ko
rm -f /boot/modules/npuep.ko.kernel
rm -f /boot/modules/octep.ko
rm -f /boot/modules/octep.ko.kernel
rm -f "${PREFIX}/opnsense/scripts/npuctl/mcp2210.py"
rm -f "${PREFIX}/opnsense/scripts/npuctl/npuhs.py"
rm -f "${PREFIX}/opnsense/scripts/npuctl/bridge-pathcost.sh"
rm -f "${PREFIX}/opnsense/scripts/npuctl/verify.sh"
rmdir "${PREFIX}/opnsense/scripts/npuctl" 2>/dev/null || true
rm -f "${PREFIX}/opnsense/scripts/octep/bringup.sh"
rmdir "${PREFIX}/opnsense/scripts/octep" 2>/dev/null || true
rm -f "${PREFIX}/opnsense/scripts/panel/panel.sh"
rmdir "${PREFIX}/opnsense/scripts/panel" 2>/dev/null || true

# The devd rule goes, and devd is told, because a rule whose script has just been deleted would
# otherwise log a failure on every link event until the next reboot.
rm -f "${PREFIX}/etc/cron.d/npuctl"

# Any path cost this set is LEFT ALONE. It is per-member state inside a bridge somebody else
# configured, it is the right number for the link that is running, and clearing it would need a
# guess about what to put back.

# hidraw_load is left in loader.conf.local on purpose: it is harmless, it may have been there
# before this plugin, and removing a line somebody else added is worse than leaving one behind.
# A loaded module is LEFT LOADED. Unloading octep while its rings are live would leave the
# coprocessor writing into memory the kernel has taken back, and the datapath attaches once per
# coprocessor boot, so it could not be restarted afterwards anyway. The next reboot is what
# removes it, and after this the boot hook will not bring it back.
echo "removed, including /boot/modules/npuep.ko and /boot/modules/octep.ko."
echo "hidraw_load was left in /boot/loader.conf.local, and a loaded module was left loaded until the next reboot."
