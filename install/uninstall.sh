#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
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
rm -f "${PREFIX}/etc/rc.syshook.d/early/30-xgs-assign"
# its mark: a later install asks again only on a box whose front ports are still unassigned
rm -f /conf/os-xgs-npu-assign
rm -f "${PREFIX}/etc/rc.syshook.d/upgrade/20-octep"
rm -f "${PREFIX}/etc/rc.syshook.d/stop/20-octep"
rm -f "${PREFIX}/etc/cron.d/npuctl"
rm -f "${PREFIX}/etc/cron.d/octep"
rm -f "${PREFIX}/etc/newsyslog.conf.d/octep"

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
rm -f "${PREFIX}/opnsense/scripts/octep/npu-platform.sh"
rm -f "${PREFIX}/opnsense/scripts/octep/ports.py"
rm -f /var/db/os-xgs-npu/npu-platform.txt /var/db/os-xgs-npu/ports.json
rm -f "${PREFIX}/opnsense/scripts/octep/kernel-follow.sh"
rm -f "${PREFIX}/opnsense/scripts/octep/fetch-sources.sh"
rmdir "${PREFIX}/opnsense/scripts/octep" 2>/dev/null || true

# The board reader both appliances' hooks ask, the status reader, and the gate writer.
rm -f "${PREFIX}/opnsense/scripts/xgs/board.sh"
rm -f "${PREFIX}/opnsense/scripts/xgs/status.py"
rm -f "${PREFIX}/opnsense/scripts/xgs/offload.sh"
rm -f "${PREFIX}/opnsense/scripts/xgs/update.py"
rm -f "${PREFIX}/opnsense/scripts/xgs/assign.php"
rm -f "${PREFIX}/opnsense/scripts/xgs/countdown.py"
rm -f /var/db/os-xgs-npu/labels /var/db/os-xgs-npu/switch-jumbo.state
rm -f "${PREFIX}/opnsense/scripts/xgs/apply.php" "${PREFIX}/opnsense/scripts/xgs/portmap.py" \
    "${PREFIX}/opnsense/scripts/xgs/ipsec.py" "${PREFIX}/opnsense/scripts/xgs/reassign.sh" \
    "${PREFIX}/opnsense/scripts/xgs/defaults.php" "${PREFIX}/opnsense/scripts/xgs/npu-run.sh" \
    "${PREFIX}/opnsense/scripts/xgs/temperatures.sh" "${PREFIX}/opnsense/scripts/xgs/transceivers.py" \
    "${PREFIX}/opnsense/scripts/xgs/bundle.py" "${PREFIX}/opnsense/scripts/xgs/flows.py" \
    "${PREFIX}/opnsense/scripts/xgs/flow_exclude.php" \
    "${PREFIX}/opnsense/scripts/xgs/lists_cut.py" \
    "${PREFIX}/etc/cron.d/xgs-lists" \
    "${PREFIX}/opnsense/scripts/xgs/ja4_cut.py" \
    "${PREFIX}/etc/cron.d/xgs-ja4" \
    "${PREFIX}/opnsense/scripts/xgs/anomaly.py" \
    "${PREFIX}/etc/cron.d/xgs-anomaly"
rm -f /var/db/os-xgs-npu/transceivers.json
rm -rf /var/db/os-xgs-npu/lists /var/db/os-xgs-npu/ja4 /var/db/os-xgs-npu/anomaly
# The hardware watchdog: disarmed before its script goes, or nothing would ever stop watchdogd.
[ -f "${PREFIX}/opnsense/scripts/xgs/watchdog.sh" ] && /bin/sh "${PREFIX}/opnsense/scripts/xgs/watchdog.sh" stop > /dev/null 2>&1
rm -f "${PREFIX}/opnsense/scripts/xgs/watchdog.sh" "${PREFIX}/etc/rc.syshook.d/start/08-xgs-watchdog"     "${PREFIX}/etc/rc.syshook.d/stop/05-xgs-watchdog"
rm -f "${PREFIX}/opnsense/scripts/xgs/switch-jumbo.sh" "${PREFIX}/etc/rc.syshook.d/start/09-xgs-switch-jumbo"
# The dashboard widgets. A dashboard that still lists one shows an empty panel until it is removed.
rm -f "${PREFIX}/opnsense/www/js/widgets/XGSCoprocessor.js" "${PREFIX}/opnsense/www/js/widgets/XGSPorts.js" \
    "${PREFIX}/opnsense/www/js/widgets/XGSIpsec.js" "${PREFIX}/opnsense/www/js/widgets/XGSFlows.js" \
    "${PREFIX}/opnsense/www/js/widgets/Metadata/XGS.xml"
# The GUI translations' merge, its hook and its trigger. The strings already merged stay in the
# catalogues until a catalogue is written again; nothing looks them up any more.
rm -rf "${PREFIX}/opnsense/scripts/xgs/i18n"
rm -f "${PREFIX}/opnsense/scripts/xgs/merge_ui_translations.py" "${PREFIX}/etc/rc.syshook.d/start/63-xgs-i18n" \
    "${PREFIX}/share/pkg/triggers/os-xgs-npu.ucl"
rm -f "${PREFIX}/share/os-xgs-npu/installed.json" "${PREFIX}/share/os-xgs-npu/manifest.sha256"
rm -rf /var/db/os-xgs-npu/update
# The problem reporter: the script, its hooks, its place in OPNsense's status, and its queue. Its
# cron entry goes when the crontab is next written without xgs.inc, below.
rm -f "${PREFIX}/opnsense/scripts/xgs/reporter.py" "${PREFIX}/etc/rc.syshook.d/start/65-xgs-report" \
    "${PREFIX}/etc/rc.syshook.d/stop/90-xgs-report" \
    "${PREFIX}/opnsense/mvc/app/library/OPNsense/System/Status/XGSReporterStatus.php"
rm -rf /var/db/os-xgs-npu/reports
rm -f "${PREFIX}/opnsense/scripts/xgs/reset_reason.py" "${PREFIX}/etc/rc.syshook.d/start/06-xgs-reset-reason" \
    /var/db/os-xgs-npu/reset-reason.json
rmdir "${PREFIX}/opnsense/scripts/xgs" 2>/dev/null || true

# The GUI integration. This was missing until the settings page was added, and it mattered more
# than it looked: with the scripts gone and these left behind, the menu still offers two pages, one
# of which writes to a driver that is no longer installed. The menu cache is dropped as well, or
# the entries stay in the sidebar until it expires.
rm -rf "${PREFIX}/opnsense/mvc/app/models/OPNsense/XGS"
rm -rf "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS"
rm -rf "${PREFIX}/opnsense/mvc/app/views/OPNsense/XGS"
rm -f "${PREFIX}/opnsense/service/conf/actions.d/actions_xgs.conf"
rm -f "${PREFIX}/opnsense/service/templates/OPNsense/Syslog/local/xgs.conf"
/usr/local/sbin/configctl template reload OPNsense/Syslog > /dev/null 2>&1 || true
/usr/local/sbin/configctl syslog restart > /dev/null 2>&1 || true
rm -f "${PREFIX}/etc/inc/plugins.inc.d/xgs.inc"
# The crontab written again without xgs.inc's entry, which would otherwise run a script that is gone.
/usr/local/bin/php -r 'require_once("config.inc"); require_once("util.inc"); require_once("system.inc"); require_once("plugins.inc"); system_cron_configure();' > /dev/null 2>&1 || true
rm -f /var/lib/php/tmp/opnsense_menu_cache.xml /tmp/opnsense_menu_cache.xml
# The crontab written again without the plugin's job, now that xgs.inc is gone.
/usr/local/bin/php -r 'require_once("config.inc"); require_once("util.inc"); require_once("system.inc"); require_once("plugins.inc"); system_cron_configure();' \
    > /dev/null 2>&1 || true
# The driver's build tree.
rm -rf "${PREFIX}/share/os-xgs-npu"

# The front panel script.
rm -f "${PREFIX}/opnsense/scripts/panel/panel.sh"
rmdir "${PREFIX}/opnsense/scripts/panel" 2>/dev/null || true

echo "removed, including the modules in /boot/modules and the build tree in ${PREFIX}/share/os-xgs-npu."
echo "Kernel sources fetched into /usr/src-<series>-<sha> are left where they are."
echo "On an ARMADA appliance, hidraw_load was left in /boot/loader.conf.local."
echo "Settings made in OPNsense's own pages (Thermal Sensors) are left; they are the owner's now."
echo "A loaded module stays loaded until the next reboot."
