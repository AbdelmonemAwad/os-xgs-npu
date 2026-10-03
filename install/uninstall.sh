#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Remove everything install.sh installs, on either appliance.
#
# It removes files and leaves running things running. A loaded module stays loaded until the next
# reboot, because unloading one is not a cleanup step on these appliances: unloading octep takes the
# twelve front ports, the WAN lease and the default route with it, and the datapath attaches once per
# coprocessor boot, so a reload cannot be answered without a host reboot.
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
if kldstat -q -n octep; then
    echo "== octep is loaded and stays loaded until the next reboot =="
    echo "   do not kldunload it on a machine whose front ports matter"
fi

# The boot, upgrade and cron hooks.
rm -f "${PREFIX}/etc/rc.syshook.d/early/06-npuctl"
rm -f "${PREFIX}/etc/rc.syshook.d/early/07-npuep"
rm -f "${PREFIX}/etc/rc.syshook.d/early/08-octep"
rm -f "${PREFIX}/etc/rc.syshook.d/upgrade/20-octep"
rm -f "${PREFIX}/etc/rc.syshook.d/stop/20-octep"
rm -f "${PREFIX}/etc/cron.d/npuctl"
rm -f "${PREFIX}/etc/cron.d/octep"

# The modules, their stamps, and the previous module the kernel follower keeps.
rm -f /boot/modules/npuep.ko /boot/modules/npuep.ko.kernel
rm -f /boot/modules/octep.ko /boot/modules/octep.ko.kernel
rm -f /boot/modules/octep.ko.prev /boot/modules/octep.ko.prev.kernel

# The scripts.
rm -f "${PREFIX}/opnsense/scripts/npuctl/mcp2210.py"
rm -f "${PREFIX}/opnsense/scripts/npuctl/npuhs.py"
rm -f "${PREFIX}/opnsense/scripts/npuctl/bridge-pathcost.sh"
rm -f "${PREFIX}/opnsense/scripts/npuctl/verify.sh"
rmdir "${PREFIX}/opnsense/scripts/npuctl" 2>/dev/null || true
rm -f "${PREFIX}/opnsense/scripts/octep/bringup.sh"
rm -f "${PREFIX}/opnsense/scripts/octep/kernel-follow.sh"
rm -f "${PREFIX}/opnsense/scripts/octep/fetch-sources.sh"
rmdir "${PREFIX}/opnsense/scripts/octep" 2>/dev/null || true

# The board reader both appliances' hooks ask.
rm -f "${PREFIX}/opnsense/scripts/xgs/board.sh"
rmdir "${PREFIX}/opnsense/scripts/xgs" 2>/dev/null || true

# The driver's build tree.
rm -rf "${PREFIX}/share/os-xgs-npu"

# The front panel script.
rm -f "${PREFIX}/opnsense/scripts/panel/panel.sh"
rmdir "${PREFIX}/opnsense/scripts/panel" 2>/dev/null || true

echo "removed, including the modules in /boot/modules and the build tree in ${PREFIX}/share/os-xgs-npu."
echo "Kernel sources fetched into /usr/src-<series>-<sha> are left where they are."
echo "On an ARMADA appliance, hidraw_load was left in /boot/loader.conf.local."
echo "A loaded module stays loaded until the next reboot."
