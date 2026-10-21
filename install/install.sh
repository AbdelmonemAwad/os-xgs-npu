#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Install from a copy of this repository, without pkg(8).
#
# ONE INSTALLER, TWO APPLIANCES, AND NEITHER MAY GET THE OTHER'S PIECES. The XGS 136 carries an
# ARMADA coprocessor whose reset line is driven over an MCP2210 USB-SPI bridge; the XGS 3300 carries
# an OCTEON TX. For a long time this file installed everything everywhere, on the reasoning that the
# other appliance's pieces would look for their hardware, not find it, and exit.
#
# That reasoning was checked on the XGS 3300 and it does not hold. The 3300 has an MCP2210 too -
# `usbconfig` lists it as ugen1.2 - and on that board it is the bridge the vendor's
# xgs-usb-spi-flash uses to WRITE THE COPROCESSOR'S BOOT FLASH. 06-npuctl's only guard was "is there
# a /dev/hidraw0", which that bridge satisfies, so the 136's reset sequence would have gone to the
# 3300's boot flash bridge at every boot - and on 2026-10-02 it did, through a version of this file
# that still installed everything everywhere, and the 3300's coprocessor dropped off the bus. So the
# appliance is decided here, once, from the board's own assembly number, with the PCI id as a second
# check; each one gets only its own pieces, a board nothing here has run on gets none, and the pieces
# an older version of this file put on the wrong machine are taken off it.
#
# It also lays down the OCTEON TX driver's sources where this plugin owns them and builds the module
# through the kernel follower - the same code path a kernel update takes, so there is one way a module
# gets built and stamped rather than two. Before that, the only build tree on the appliance was one
# assembled by hand under /root, and both the installer and the follower were reaching into it.
set -e

SRC=$(cd "$(dirname "$0")/.." && pwd)
PREFIX=${PREFIX:-/usr/local}

if [ "$(id -u)" != "0" ]; then
    echo "run this as root"
    exit 1
fi

# Which appliance, read positively from the board's assembly number - src/opnsense/scripts/xgs/board.sh
# says why, and why elimination was not good enough. A board this project has not been run on gets
# nothing at all, rather than whichever family it was guessed to belong to.
if ! BOARD=$(/bin/sh "${SRC}/src/opnsense/scripts/xgs/board.sh"); then
    echo "== board ${BOARD%% *}: not one this project has been run on - nothing installed =="
    exit 1
fi
FAMILY=$(echo "${BOARD}" | /usr/bin/awk '{print $2}')
# A model this project has not been run on installs as everything else does, and says so first.
case "${BOARD}" in
*" rnd")
    echo "== board ${BOARD%% *}: this model has not been run by this project - research and development,"
    echo "   untested, from the vendor's own board files and reset table; report what it does =="
    ;;
esac

# And the PCI side has to agree with it. Both spellings of the identity, because pciconf prints one or
# the other depending on the release, and matching only the packed form once made the boot hook skip
# on the very machine it was written for.
A300=0
if /usr/sbin/pciconf -l 2>/dev/null |
    grep -qE 'chip=0xa300177d|vendor=0x177d[[:space:]]+device=0xa300'; then
    A300=1
fi

OCTEONTX=0
if [ "${FAMILY}" = octeontx ]; then
    OCTEONTX=1
    # The board's own sensors - CPU, NPU and inlet temperatures, both fans - are a register bank
    # on the CPLD behind an SMBus face the host's main SMBus controller reaches; the reader is a
    # small C tool built here with the appliance's own cc (docs/findings/the-sensors-answer-on-the-host-bus.md).
    # os-fanctl and anything else that wants the readings asks it for `json`. The CPU's own die
    # sensor, amdtemp, is set further down through OPNsense's own setting, on both families.
    if [ -f "${SRC}/tools/sensors/xgs3300-sensors.c" ] && command -v cc > /dev/null 2>&1; then
        if cc -O -o "${PREFIX}/sbin/xgs3300-sensors" "${SRC}/tools/sensors/xgs3300-sensors.c" 2> /dev/null; then
            chmod 0755 "${PREFIX}/sbin/xgs3300-sensors"
            echo "   sensors: ${PREFIX}/sbin/xgs3300-sensors built ($("${PREFIX}/sbin/xgs3300-sensors" 2> /dev/null | tr '
' ';' | cut -c1-80))"
        else
            echo "   NOTE: the sensor tool did not build; the CPLD's readings are not available until it does"
        fi
    fi
    echo "== board ${BOARD%% *}, OCTEON TX: the ARMADA pieces are not installed here, and are removed if present =="
    if [ "${A300}" = 0 ]; then
        # Not a reason to stop: nothing below needs the coprocessor running, and 08-octep brings it up
        # at boot. But it is worth knowing before that boot, not after it.
        echo "   NOTE: the coprocessor is not on the PCI bus right now"
    fi
else
    if [ "${A300}" = 1 ]; then
        echo "== board ${BOARD%% *} reads as ARMADA, but an OCTEON TX endpoint is on the bus - nothing installed =="
        exit 1
    fi
    echo "== board ${BOARD%% *}, ARMADA: installing the ARMADA pieces =="
fi

# The board reader itself goes on every appliance, because the hooks ask it at every boot.
install -d -m 0755 "${PREFIX}/opnsense/scripts/xgs"
install -m 0755 "${SRC}/src/opnsense/scripts/xgs/board.sh" "${PREFIX}/opnsense/scripts/xgs/board.sh"
install -m 0755 "${SRC}/src/opnsense/scripts/xgs/status.py" "${PREFIX}/opnsense/scripts/xgs/status.py"
install -m 0755 "${SRC}/src/opnsense/scripts/xgs/offload.sh" "${PREFIX}/opnsense/scripts/xgs/offload.sh"
install -m 0755 "${SRC}/src/opnsense/scripts/xgs/update.py" "${PREFIX}/opnsense/scripts/xgs/update.py"
install -m 0755 "${SRC}/src/opnsense/scripts/xgs/assign.php" "${PREFIX}/opnsense/scripts/xgs/assign.php"
install -m 0755 "${SRC}/src/opnsense/scripts/xgs/countdown.py" "${PREFIX}/opnsense/scripts/xgs/countdown.py"
# The settings page's apply, the readers of the Ports and IPsec pages, the appliance's settings in
# OPNsense's own pages, and the runner other plugins use to reach the coprocessor (npu-run.sh).
for f in apply.php portmap.py ipsec.py reassign.sh defaults.php npu-run.sh temperatures.sh watchdog.sh transceivers.py bundle.py flows.py flow_exclude.php lists_cut.py ja4_cut.py anomaly.py poe.py plugins.py health.py portstats.py models.py; do
    install -m 0755 "${SRC}/src/opnsense/scripts/xgs/${f}" "${PREFIX}/opnsense/scripts/xgs/${f}"
done
# Jumbo frames through the coprocessor's switch chip at every boot, on both families: the script,
# its start hook, and the switch tool it puts on the coprocessor (an aarch64 binary for the
# coprocessor, not for this host - contrib/mvsw).
install -m 0755 "${SRC}/src/opnsense/scripts/xgs/switch-jumbo.sh" "${PREFIX}/opnsense/scripts/xgs/switch-jumbo.sh"
install -d -m 0755 "${PREFIX}/etc/rc.syshook.d/start" "${PREFIX}/share/os-xgs-npu"
install -m 0755 "${SRC}/src/etc/rc.syshook.d/start/09-xgs-switch-jumbo" "${PREFIX}/etc/rc.syshook.d/start/09-xgs-switch-jumbo"
# The host's hardware watchdog: armed at the end of every boot when the setting says so, and
# disarmed first thing at shutdown (scripts/xgs/watchdog.sh).
install -d -m 0755 "${PREFIX}/etc/rc.syshook.d/stop"
install -m 0755 "${SRC}/src/etc/rc.syshook.d/start/08-xgs-watchdog" "${PREFIX}/etc/rc.syshook.d/start/08-xgs-watchdog"
install -m 0755 "${SRC}/src/etc/rc.syshook.d/stop/05-xgs-watchdog" "${PREFIX}/etc/rc.syshook.d/stop/05-xgs-watchdog"
install -m 0644 "${SRC}/contrib/mvsw/mvsw" "${PREFIX}/share/os-xgs-npu/mvsw"
# Block lists whose additions cut the connections already open (scripts/xgs/lists_cut.py): once a
# minute, on both families, from /usr/local/etc/cron.d like the kernel follower - OPNsense
# regenerates /etc/crontab from its own cron jobs and would drop a line put there.
install -d -m 0755 "${PREFIX}/etc/cron.d"
install -m 0644 "${SRC}/src/etc/cron.d/xgs-lists" "${PREFIX}/etc/cron.d/xgs-lists"
# TLS fingerprints whose connections are cut (scripts/xgs/ja4_cut.py): started every minute, it reads
# the Intrusion Detection's log every two seconds while a fingerprint is listed, and ends at once
# when none is.
install -m 0644 "${SRC}/src/etc/cron.d/xgs-ja4" "${PREFIX}/etc/cron.d/xgs-ja4"
# Unusual traffic, reported and never blocked (scripts/xgs/anomaly.py): every minute, ending at once
# while the setting is off.
install -m 0644 "${SRC}/src/etc/cron.d/xgs-anomaly" "${PREFIX}/etc/cron.d/xgs-anomaly"
# This appliance counted once a day among the ones running the project (scripts/xgs/count.py): a
# random identifier, its model and its release; off on XGS: Problem Reports.
install -m 0755 "${SRC}/src/opnsense/scripts/xgs/count.py" "${PREFIX}/opnsense/scripts/xgs/count.py"
install -m 0644 "${SRC}/src/etc/cron.d/xgs-count" "${PREFIX}/etc/cron.d/xgs-count"

# ---------------------------------------------------------------- the OPNsense integration
#
# All of this is discovered rather than registered: plugins_scan() globs plugins.inc.d, the menu
# and ACL loaders glob the model directories by vendor, and configd reads every actions_*.conf.
# So the package reaches the GUI without editing one OPNsense file, and an OPNsense update cannot
# undo it. Both boards get it - the status page reports an absent driver perfectly well, which is
# more useful than a missing menu entry.

echo "== OPNsense integration =="
# What this install is, for XGS: Updates: the commit, read from a git checkout,
# from the stamp update.py leaves in a tree it downloaded, or from the one xgs-image-builder leaves
# in an image - whichever this tree has. Without one the page says the version is not known and
# offers the newest version anyway.
REV=; RDATE=; RSRC=unknown; REL=
if REV=$(git -C "${SRC}" rev-parse HEAD 2>/dev/null); then
    RDATE=$(git -C "${SRC}" log -1 --format=%cI 2>/dev/null); RSRC=git
    REL=$(git -C "${SRC}" describe --tags --exact-match HEAD 2>/dev/null)
elif [ -s "${SRC}/.update-commit" ]; then
    REV=$(awk '{print $1}' "${SRC}/.update-commit"); RDATE=$(awk '{print $2}' "${SRC}/.update-commit"); RSRC=update
    REL=$(awk '{print $3}' "${SRC}/.update-commit")
elif [ -s "${SRC}/.image-build" ]; then
    REV=$(awk '{print $1}' "${SRC}/.image-build"); RDATE=$(awk '{print $2}' "${SRC}/.image-build"); RSRC=image
    REL=$(awk '{print $3}' "${SRC}/.image-build")
else
    REV=
fi
case ${REV} in *[!0-9a-f]*) REV=; RSRC=unknown ;; esac
# The release, where this tree is one: v0.1.0-alpha.1 and its like, nothing else.
case ${REL} in v[0-9]*.[0-9]*.[0-9]*) case ${REL} in *[!0-9A-Za-z.-]*) REL= ;; esac ;; *) REL= ;; esac
install -d -m 0755 "${PREFIX}/share/os-xgs-npu"
printf '{"commit": "%s", "date": "%s", "source": "%s", "release": "%s", "installed_at": %s}\n' \
    "${REV}" "${RDATE}" "${RSRC}" "${REL}" "$(date +%s)" > "${PREFIX}/share/os-xgs-npu/installed.json"
echo "   this install: ${REL:-${REV:-an unknown commit}} (${RSRC})"
# Every release's changes, as CHANGELOG.md has them, for the Releases tab of XGS: About.
install -m 0644 "${SRC}/CHANGELOG.md" "${PREFIX}/share/os-xgs-npu/CHANGELOG.md"
install -d -m 0755 "${PREFIX}/etc/inc/plugins.inc.d"
install -m 0644 "${SRC}/src/etc/inc/plugins.inc.d/xgs.inc" "${PREFIX}/etc/inc/plugins.inc.d/xgs.inc"

install -d -m 0755 "${PREFIX}/opnsense/service/conf/actions.d"
install -m 0644 "${SRC}/src/opnsense/service/conf/actions.d/actions_xgs.conf" \
    "${PREFIX}/opnsense/service/conf/actions.d/actions_xgs.conf"

for d in models/OPNsense/XGS/Menu models/OPNsense/XGS/ACL \
         controllers/OPNsense/XGS/Api controllers/OPNsense/XGS/forms views/OPNsense/XGS; do
    install -d -m 0755 "${PREFIX}/opnsense/mvc/app/${d}"
done
# Every page's view at once, the partials with them. One line per view left credit.volt - the
# credit line every page includes since #457 - out of every install, and each page of the menu
# failed to render with "View 'OPNsense/XGS/credit' was not found" on every box set up by this
# installer or updated by it; only boxes that had it copied by hand showed the pages.
install -m 0644 "${SRC}"/src/opnsense/mvc/app/views/OPNsense/XGS/*.volt "${PREFIX}/opnsense/mvc/app/views/OPNsense/XGS/"
install -m 0644 "${SRC}/src/opnsense/mvc/app/models/OPNsense/XGS/Menu/Menu.xml" \
    "${PREFIX}/opnsense/mvc/app/models/OPNsense/XGS/Menu/Menu.xml"
install -m 0644 "${SRC}/src/opnsense/mvc/app/models/OPNsense/XGS/ACL/ACL.xml" \
    "${PREFIX}/opnsense/mvc/app/models/OPNsense/XGS/ACL/ACL.xml"
# The model's XML and its class sit beside each other in the vendor directory, not in Menu or ACL:
# BaseModel finds the XML by the class's own namespace, so XGS.php and XGS.xml are one pair and
# moving either of them breaks the mount that bringup.sh reads the setting back out of.
install -m 0644 "${SRC}/src/opnsense/mvc/app/models/OPNsense/XGS/XGS.xml" \
    "${PREFIX}/opnsense/mvc/app/models/OPNsense/XGS/XGS.xml"
install -m 0644 "${SRC}/src/opnsense/mvc/app/models/OPNsense/XGS/XGS.php" \
    "${PREFIX}/opnsense/mvc/app/models/OPNsense/XGS/XGS.php"
install -d -m 0755 "${PREFIX}/opnsense/mvc/app/models/OPNsense/XGS/Migrations"
install -m 0644 "${SRC}/src/opnsense/mvc/app/models/OPNsense/XGS/Migrations/M1_1_0.php" \
    "${PREFIX}/opnsense/mvc/app/models/OPNsense/XGS/Migrations/M1_1_0.php"
# The model's migrations, as a package's install runs them: 1.1.0 reads into the settings page the
# tunables a box had set by hand, so the page opens showing what the box does. Only this model's.
if [ -x "${PREFIX}/opnsense/mvc/script/run_migrations.php" ]; then
    "${PREFIX}/opnsense/mvc/script/run_migrations.php" OPNsense/XGS || true
fi
install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/StatusController.php" \
    "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/StatusController.php"
install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/SettingsController.php" \
    "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/SettingsController.php"
install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/UpdatesController.php" \
    "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/UpdatesController.php"
install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/Api/UpdatesController.php" \
    "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/Api/UpdatesController.php"
install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/forms/general.xml" \
    "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/forms/general.xml"
install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/Api/StatusController.php" \
    "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/Api/StatusController.php"
install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/Api/SettingsController.php" \
    "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/Api/SettingsController.php"
install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/Api/OffloadController.php" \
    "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/Api/OffloadController.php"
# A PoE port switched off, on, or off and on again, from the Ports page (under XGS: Settings).
install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/Api/PoeController.php" \
    "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/Api/PoeController.php"
# XGS: About - a page controller and its view, no API: the project, its author, its links and
# addresses, the disclaimer and the licences.
install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/AboutController.php" \
    "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/AboutController.php"
# The Ports, Transceivers and IPsec pages: a page controller, an API controller and a view each.
for p in Ports Transceivers Ipsec Flows; do
    install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/${p}Controller.php" \
        "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/${p}Controller.php"
    install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/Api/${p}Controller.php" \
        "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/Api/${p}Controller.php"
done
# The dashboard widgets: the coprocessor, its ports, its IPsec engine and the connections it
# forwards, with one metadata file naming all four for the Lobby's "add widget" list.
install -d -m 0755 "${PREFIX}/opnsense/www/js/widgets/Metadata"
for w in XGSCoprocessor XGSPorts XGSIpsec XGSFlows; do
    install -m 0644 "${SRC}/src/opnsense/www/js/widgets/${w}.js" "${PREFIX}/opnsense/www/js/widgets/${w}.js"
done
install -m 0644 "${SRC}/src/opnsense/www/js/widgets/Metadata/XGS.xml" "${PREFIX}/opnsense/www/js/widgets/Metadata/XGS.xml"

# The menu is cached in a file with a time-to-live, so a new entry can be up to that long in
# appearing. Removing it costs one rebuild on the next page load and makes the install immediate.
#
# NOT /tmp. The cache goes in MenuSystem's tempDir, which config.php sets to /var/lib/php/tmp, and
# this line named /tmp for as long as it has existed - so it has never removed anything and the
# menu entry has always waited out the TTL instead. Both paths are given rather than the one, so
# that a future OPNsense moving tempDir degrades to the old behaviour instead of erroring.
rm -f /var/lib/php/tmp/opnsense_menu_cache.xml /tmp/opnsense_menu_cache.xml
# The privileges are cached beside it the same way, so a page added or renamed in ACL.xml is
# refused, or shown under its old name, until that cache expires - measured on 2026-10-17.
rm -f /var/lib/php/tmp/opnsense_acl_cache.json

# The Log File page reads /var/log/xgs, which syslog-ng writes only once it has the filter for it:
# a template under OPNsense/Syslog/local, the way every package that keeps its own log ships one.
# xgs_syslog() alone names the log to the GUI and does not create it - measured on 2026-10-17,
# the page was empty and the lines were in the system log.
install -d -m 0755 "${PREFIX}/opnsense/service/templates/OPNsense/Syslog/local"
install -m 0644 "${SRC}/src/opnsense/service/templates/OPNsense/Syslog/local/xgs.conf" \
    "${PREFIX}/opnsense/service/templates/OPNsense/Syslog/local/xgs.conf"

# configd reads actions.d once, at start. "configctl configd reload" re-reads templates and NOT
# the action list, so a new action stays invisible until the daemon is restarted - measured, it
# answers "Action not allowed or missing" until then. configd is the configuration daemon and not
# the datapath, so a restart costs nothing that is carrying traffic.
if [ -x /usr/sbin/service ] && [ -f "${PREFIX}/opnsense/service/conf/actions.d/actions_xgs.conf" ]; then
    /usr/sbin/service configd restart > /dev/null 2>&1 || true
fi
# syslog-ng's configuration written again from the templates, the log filter above among them.
/usr/local/sbin/configctl template reload OPNsense/Syslog > /dev/null 2>&1 || true
/usr/local/sbin/configctl syslog restart > /dev/null 2>&1 || true

# The coprocessor pages in the GUI's languages: the strings in i18n/ui, merged into each installed
# language's catalogue now, again at every boot (63-xgs-i18n), and whenever a package writes a
# catalogue (the pkg trigger) - docs/octep/coprocessor-section.md. Running php keeps the catalogue
# it loaded, so the web GUI is restarted when something was added.
echo "== GUI translations =="
install -d -m 0755 "${PREFIX}/opnsense/scripts/xgs/i18n/ui" "${PREFIX}/etc/rc.syshook.d/start" "${PREFIX}/share/pkg/triggers"
install -m 0644 "${SRC}"/src/opnsense/scripts/xgs/i18n/ui/*.json "${PREFIX}/opnsense/scripts/xgs/i18n/ui/"
install -m 0755 "${SRC}/src/opnsense/scripts/xgs/merge_ui_translations.py" "${PREFIX}/opnsense/scripts/xgs/merge_ui_translations.py"
install -m 0755 "${SRC}/src/etc/rc.syshook.d/start/63-xgs-i18n" "${PREFIX}/etc/rc.syshook.d/start/63-xgs-i18n"
install -m 0644 "${SRC}/src/share/pkg/triggers/os-xgs-npu.ucl" "${PREFIX}/share/pkg/triggers/os-xgs-npu.ucl"
ADDED=$(/usr/local/bin/python3 "${PREFIX}/opnsense/scripts/xgs/merge_ui_translations.py" 2> /dev/null || echo 0)
echo "   strings added to the GUI's catalogues: ${ADDED:-0}"
if [ "${ADDED:-0}" != 0 ]; then
    /usr/local/sbin/configctl webgui restart > /dev/null 2>&1 || true
fi

# Problem reports (docs/problem-reports.md): the reporter, its page under XGS, its place
# in OPNsense's own status (the banner until the owner has chosen, then a notice when a report
# waits), a look at boot and one every five minutes from the plugin's cron entry, and the stop hook
# that writes down an orderly shutdown so the next boot can tell one from a restart without it.
echo "== problem reports =="
install -m 0755 "${SRC}/src/opnsense/scripts/xgs/reporter.py" "${PREFIX}/opnsense/scripts/xgs/reporter.py"
install -d -m 0755 "${PREFIX}/etc/rc.syshook.d/start" "${PREFIX}/etc/rc.syshook.d/stop" \
    "${PREFIX}/opnsense/mvc/app/library/OPNsense/System/Status"
install -m 0755 "${SRC}/src/etc/rc.syshook.d/start/65-xgs-report" "${PREFIX}/etc/rc.syshook.d/start/65-xgs-report"
install -m 0755 "${SRC}/src/etc/rc.syshook.d/stop/90-xgs-report" "${PREFIX}/etc/rc.syshook.d/stop/90-xgs-report"
# What the host's chipset recorded as the cause of the last reset, read early in every boot and kept
# for the report of a restart without a shutdown (scripts/xgs/reset_reason.py, #454).
install -m 0755 "${SRC}/src/opnsense/scripts/xgs/reset_reason.py" "${PREFIX}/opnsense/scripts/xgs/reset_reason.py"
install -m 0755 "${SRC}/src/etc/rc.syshook.d/start/06-xgs-reset-reason" "${PREFIX}/etc/rc.syshook.d/start/06-xgs-reset-reason"
# What the modules in the cages are, told to the driver at every boot, so their ports report the
# module's medium (scripts/xgs/transceivers.py apply, then collect; #533).
install -m 0755 "${SRC}/src/etc/rc.syshook.d/start/64-xgs-transceivers" "${PREFIX}/etc/rc.syshook.d/start/64-xgs-transceivers"
# The PoE ports the owner switched off, switched off again at every boot (scripts/xgs/poe.py).
install -m 0755 "${SRC}/src/etc/rc.syshook.d/start/66-xgs-poe" "${PREFIX}/etc/rc.syshook.d/start/66-xgs-poe"
install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/ReportsController.php" \
    "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/ReportsController.php"
install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/Api/ReportsController.php" \
    "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/Api/ReportsController.php"
install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/forms/reporting.xml" \
    "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/forms/reporting.xml"
install -m 0644 "${SRC}/src/opnsense/mvc/app/library/OPNsense/System/Status/XGSReporterStatus.php" \
    "${PREFIX}/opnsense/mvc/app/library/OPNsense/System/Status/XGSReporterStatus.php"
install -d -m 0700 /var/db/os-xgs-npu/reports
# The cron entry is in the crontab only once OPNsense writes it again from every plugin's jobs.
if /usr/local/bin/php -r 'require_once("config.inc"); require_once("util.inc"); require_once("system.inc"); require_once("plugins.inc"); system_cron_configure();' > /dev/null 2>&1; then
    echo "   crontab written again: problems are looked for every five minutes"
fi
MODE=$(/usr/local/bin/php -r 'require_once("config.inc"); echo $config["OPNsense"]["XGS"]["reporting"]["mode"] ?? "automatic";' 2> /dev/null)
echo "   sending: ${MODE:-automatic} - XGS: Problem Reports shows what is sent, and changes it"

# The appliance's own settings in OPNsense's own pages, once each and never over the owner's choice:
# today the CPU's temperature sensor (System: Settings: Miscellaneous, Thermal Sensors). Earlier
# versions of this file loaded amdtemp from loader.conf.local on the XGS 3300 only, which the page
# did not show; that line is taken out once OPNsense's own setting carries the module - on the XGS
# 3300 only, where this file wrote it. A loader.conf.local made by hand is the owner's.
echo "== appliance settings =="
/usr/local/bin/php "${PREFIX}/opnsense/scripts/xgs/defaults.php" | sed 's/^/   /' || true
if [ "${OCTEONTX}" = 1 ] && [ "$(/usr/local/bin/php -r 'require_once("config.inc"); echo $config["system"]["thermal_hardware"] ?? "";' 2> /dev/null)" = amdtemp ] &&
    grep -qx 'amdtemp_load="YES"' /boot/loader.conf.local 2> /dev/null; then
    sed -i '' '/^amdtemp_load="YES"$/d' /boot/loader.conf.local
    echo "   amdtemp_load taken out of loader.conf.local - OPNsense's setting loads it now"
fi
# The plugin's cron entry (xgs_cron: the coprocessor's temperatures, every minute) is in the crontab
# only once OPNsense writes the crontab again from every plugin's jobs.
if /usr/local/bin/php -r 'require_once("config.inc"); require_once("util.inc"); require_once("system.inc"); require_once("plugins.inc"); system_cron_configure();' > /dev/null 2>&1; then
    echo "   crontab written again: the coprocessor's temperatures are fed every minute"
fi

# ---------------------------------------------------------------- the ARMADA tools

if [ "${OCTEONTX}" = 0 ]; then
    echo "== tools =="
    install -d -m 0755 "${PREFIX}/opnsense/scripts/npuctl"
    install -m 0755 "${SRC}/src/opnsense/scripts/npuctl/mcp2210.py" "${PREFIX}/opnsense/scripts/npuctl/"
    install -m 0755 "${SRC}/src/opnsense/scripts/npuctl/npuhs.py" "${PREFIX}/opnsense/scripts/npuctl/"
    install -m 0755 "${SRC}/install/verify.sh" "${PREFIX}/opnsense/scripts/npuctl/verify.sh"
    # The management link to the coprocessor (issue 374): the scripts, and the assignment of
    # mvmgmt0 as an interface of this firewall with its fixed link-local address. The rule that
    # admits the coprocessor comes from the plugin's own firewall hook at every reload; the
    # coprocessor's end is told its address at every boot by the bootup hook. Idempotent.
    install -m 0755 "${SRC}/src/opnsense/scripts/npuctl/mgmt-link.sh" "${PREFIX}/opnsense/scripts/npuctl/mgmt-link.sh"
    install -m 0755 "${SRC}/src/opnsense/scripts/npuctl/mgmt-link.php" "${PREFIX}/opnsense/scripts/npuctl/mgmt-link.php"
    if /sbin/ifconfig mvmgmt0 > /dev/null 2>&1; then
        echo "== the management link to the coprocessor =="
        sh "${PREFIX}/opnsense/scripts/npuctl/mgmt-link.sh" assign 2>&1 | sed 's/^/   /'
    fi
    install -m 0755 "${SRC}/src/opnsense/scripts/npuctl/bridge-pathcost.sh" \
        "${PREFIX}/opnsense/scripts/npuctl/bridge-pathcost.sh"
else
    rm -f "${PREFIX}/opnsense/scripts/npuctl/mcp2210.py" \
          "${PREFIX}/opnsense/scripts/npuctl/npuhs.py" \
          "${PREFIX}/opnsense/scripts/npuctl/verify.sh" \
          "${PREFIX}/opnsense/scripts/npuctl/bridge-pathcost.sh"
    rmdir "${PREFIX}/opnsense/scripts/npuctl" 2>/dev/null || true
fi

# ---------------------------------------------------------------- the boot hooks

echo "== boot hooks =="
install -d -m 0755 "${PREFIX}/etc/rc.syshook.d/early"
install -d -m 0755 "${PREFIX}/etc/rc.syshook.d/start"
if [ "${OCTEONTX}" = 0 ]; then
    install -m 0755 "${SRC}/src/etc/rc.syshook.d/early/06-npuctl" "${PREFIX}/etc/rc.syshook.d/early/06-npuctl"
    install -m 0755 "${SRC}/src/etc/rc.syshook.d/early/07-npuep" "${PREFIX}/etc/rc.syshook.d/early/07-npuep"
    # The gate written again after syslogd, when the early hook's write was refused (issue 406).
    install -m 0755 "${SRC}/src/etc/rc.syshook.d/start/07-npuep-gate" "${PREFIX}/etc/rc.syshook.d/start/07-npuep-gate"
else
    rm -f "${PREFIX}/etc/rc.syshook.d/early/06-npuctl" "${PREFIX}/etc/rc.syshook.d/early/07-npuep" \
        "${PREFIX}/etc/rc.syshook.d/start/07-npuep-gate"
fi
# 08-octep looks for its own PCI id and exits on a machine without it, so it is safe on both.
install -m 0755 "${SRC}/src/etc/rc.syshook.d/early/08-octep" "${PREFIX}/etc/rc.syshook.d/early/08-octep"
# The front ports assigned by their names at the first boot that has them, with a countdown any key
# stops. On both boards; on a box whose front ports are set up already it marks itself done and
# changes nothing.
install -m 0755 "${SRC}/src/etc/rc.syshook.d/early/30-xgs-assign" "${PREFIX}/etc/rc.syshook.d/early/30-xgs-assign"

# The upgrade hook, and the cron entry beside it.
#
# `upgrade` is the level OPNsense actually calls - scripts/firmware/upgrade.sh runs it between
# installing the packages and applying the pending kernel. The `update` level, which looks like the
# obvious home for this, is called by nothing on this appliance.
#
# The cron entry is the layer that needs nobody's cooperation: every five minutes it compares the
# kernel the next boot will use with the stamp beside the installed module, which is two file reads
# when there is nothing to do. Both are inert on a machine with no octep module installed.
install -d -m 0755 "${PREFIX}/etc/rc.syshook.d/upgrade"
install -m 0755 "${SRC}/src/etc/rc.syshook.d/upgrade/20-octep" "${PREFIX}/etc/rc.syshook.d/upgrade/20-octep"
install -d -m 0755 "${PREFIX}/etc/cron.d"
install -m 0644 "${SRC}/src/etc/cron.d/octep" "${PREFIX}/etc/cron.d/octep"

# The bring-up log's rotation. The early hook and bringup.sh write /var/log/octep-bringup.log
# themselves, before syslogd exists, and nothing else ever truncates it; newsyslog(8) reads this
# directory through the include in /etc/newsyslog.conf, so the entry needs no edit of a system file.
install -d -m 0755 "${PREFIX}/etc/newsyslog.conf.d"
install -m 0644 "${SRC}/src/etc/newsyslog.conf.d/octep" "${PREFIX}/etc/newsyslog.conf.d/octep"

# And the one that does not depend on which path asked for the reboot - see the file. rc.reboot,
# rc.halt and rc.shutdown all run `rc.syshook stop` before anything is torn down, so this is the
# layer that caught the update neither of the two above did.
install -d -m 0755 "${PREFIX}/etc/rc.syshook.d/stop"
install -m 0755 "${SRC}/src/etc/rc.syshook.d/stop/20-octep" "${PREFIX}/etc/rc.syshook.d/stop/20-octep"

# The OCTEON TX bring-up is a sequence rather than a kldload, so it lives in its own script and
# the hook calls it. That also makes it runnable by hand, which is how it gets changed.
install -d -m 0755 "${PREFIX}/opnsense/scripts/octep"
install -m 0755 "${SRC}/src/opnsense/scripts/octep/bringup.sh" "${PREFIX}/opnsense/scripts/octep/bringup.sh"
# What bringup.sh asks about an expansion module: the coprocessor's own platform database, read
# through its console, and the ports beyond the panel's twelve joined to it by address.
install -m 0755 "${SRC}/src/opnsense/scripts/octep/npu-platform.sh" "${PREFIX}/opnsense/scripts/octep/npu-platform.sh"
install -m 0755 "${SRC}/src/opnsense/scripts/octep/ports.py" "${PREFIX}/opnsense/scripts/octep/ports.py"

# The kernel follower, and the sources fetcher it drives.
#
# These two are what stop an OPNsense kernel update taking the front ports away. The module is
# stamped with the kernel it was built against and refused when that no longer matches - correct,
# and on this appliance expensive, because the WAN is one of those front ports and a refused module
# means nothing can reach the network that would rebuild it. Worse, OPNsense drops the interface
# assignments whose devices are absent straight out of config.xml, so one such boot costs the WAN's
# own DHCP assignment as well as its link.
#
# So the follower runs from three places, in the stretch after a kernel is installed and before the
# host reboots onto it, where the old WAN is still alive and the new kernel is already present naming
# itself: rc.syshook.d/stop/20-octep, which every orderly reboot passes through whatever asked for
# it; rc.syshook.d/upgrade/20-octep, for the firmware upgrade path that calls that level; and cron,
# which needs nobody's cooperation at all.
install -m 0755 "${SRC}/install/kernel-follow.sh" "${PREFIX}/opnsense/scripts/octep/kernel-follow.sh"
install -m 0755 "${SRC}/contrib/npuep/fetch-sources.sh" "${PREFIX}/opnsense/scripts/octep/fetch-sources.sh"

# ---------------------------------------------------------------- the driver's sources

# The driver's sources, owned by this plugin, and nothing else.
#
# The module is C against the running kernel's headers and OPNsense ships no kernel sources, so it is
# built on the appliance, and the kernel follower rebuilds it after every kernel update. This tree is
# what it builds FROM - it copies these files into an empty directory and builds there - so it must
# hold exactly what this repository holds. Every real .c and .h is copied, and the generated ones are
# NOT: a checkout that has been built in place carries bus_if.h, device_if.h, pci_if.h and opt_*.h
# beside the real headers, and copying those along leaves them newer than any .m file, so never
# regenerated. The files used to be named one by one instead, and that list is how npuep's four
# newer sources - npuflow.c, npupf.c, npunhop.c, npuipsec.c and their headers - never reached an
# installed tree: every ARMADA install made by this script failed to build its module (issue 408,
# found by booting an image from xgs-image-builder as an XGS 136). So the list is no longer kept by
# hand, and every source the Makefile builds is checked for after the copy.
# The tree is replaced, not added to, so a file the repository drops does not linger here.
echo "== driver sources =="
install -d -m 0750 /var/db/os-xgs-npu
copy_driver() { # <repository directory> <installed directory>
    rm -rf "$2"
    install -d -m 0755 "$2"
    install -m 0644 "$1/Makefile" "$2/Makefile"
    for f in "$1"/*.c "$1"/*.h; do
        [ -f "${f}" ] || continue
        case $(basename "${f}") in
        bus_if.h|device_if.h|pci_if.h|opt_*.h) continue ;;
        esac
        install -m 0644 "${f}" "$2/$(basename "${f}")"
    done
    missing=
    for f in $(sed -n '/^SRCS/,/^[[:space:]]*$/p' "$1/Makefile" | tr -d '\\' | tr ' \t' '\n\n' | grep '\.c$'); do
        [ -f "$2/${f}" ] || missing="${missing} ${f}"
    done
    if [ -n "${missing}" ]; then
        echo "   ${2}: the Makefile builds${missing}, which this copy does not have"
        return 1
    fi
    echo "   $2"
}
# The OCTEON TX driver's sources, for the follower to build octep.ko from on the appliance.
copy_driver "${SRC}/contrib/octep" "${PREFIX}/share/os-xgs-npu/octep"
# The ARMADA driver's sources the same way, for the same follower: it builds npuep.ko from here on
# the appliance that has one. Laid down on both families, because the follower decides which module
# it is following from what is installed, and the sources cost nothing where they are not used.
copy_driver "${SRC}/contrib/npuep" "${PREFIX}/share/os-xgs-npu/npuep"

# A carried copy of the kernel sources, if this checkout has one beside it.
#
# The appliance's WAN is one of the coprocessor's own front ports, so it exists only once the module
# is built: a fresh install has no internet, and the build needs the sources. Dropping the codeload
# tarball for the image's kernel into kernel-sources/ in this checkout - under any name with the
# commit in it - closes that circle, and fetch-sources.sh uses it in preference to the network from
# then on, including after an update.
SRCPOOL="${PREFIX}/share/os-xgs-npu/kernel-sources"
install -d -m 0755 "${SRCPOOL}"
for a in "${SRC}"/kernel-sources/*.tar.gz "${SRC}"/kernel-sources/*.tgz; do
    [ -f "${a}" ] || continue
    if [ ! -f "${SRCPOOL}/$(basename "${a}")" ]; then
        install -m 0644 "${a}" "${SRCPOOL}/"
        echo "   carried kernel sources: $(basename "${a}")"
    fi
done
have=$(ls "${SRCPOOL}" 2>/dev/null | wc -l | tr -d ' ')
if [ "${have}" = 0 ]; then
    echo "   no carried kernel sources - a build for a kernel this appliance has not seen will"
    echo "   need the internet. See kernel-sources/README.md."
fi

# ---------------------------------------------------------------- the front panel script

# The front panel is a host UART and belongs to neither coprocessor, so it gets its own directory.
install -d -m 0755 "${PREFIX}/opnsense/scripts/panel"
install -m 0755 "${SRC}/src/opnsense/scripts/panel/panel.sh" "${PREFIX}/opnsense/scripts/panel/panel.sh"

# ---------------------------------------------------------------- the ARMADA path-cost timer

# devd fires this when a front port's link comes up, which is the only moment RSTP will accept a
# path cost for it. See src/opnsense/scripts/npuctl/bridge-pathcost.sh for why that matters.
#
# A timer, not a link-up hook, and the reason is in bridge-pathcost.sh: devd executes only the
# single highest-priority statement that matches an event, and OPNsense already claims
# IFNET/LINK_UP at priority 101 to run its own linkup handling. Below that a rule never fires;
# above it, ours would fire and silence OPNsense's. Neither is acceptable, so this runs on cron.
#
# /usr/local/etc/cron.d, not /etc/crontab - OPNsense regenerates the latter from its own
# configuration and would drop the line.
if [ "${OCTEONTX}" = 0 ]; then
    echo "== path cost timer =="
    install -m 0644 "${SRC}/src/etc/cron.d/npuctl" "${PREFIX}/etc/cron.d/npuctl"
    echo "   installed ${PREFIX}/etc/cron.d/npuctl (once a minute)"
    # And once now, so an install does not have to wait for the first tick.
    if [ -x "${PREFIX}/opnsense/scripts/npuctl/bridge-pathcost.sh" ]; then
        "${PREFIX}/opnsense/scripts/npuctl/bridge-pathcost.sh" || true
    fi
else
    rm -f "${PREFIX}/etc/cron.d/npuctl"
fi

# ---------------------------------------------------------------- the modules

# It is not packaged - it is C against this kernel's headers, so it is built on the appliance - but
# a built module that is never installed is a module somebody has to remember to load by hand, and
# forgetting is what makes a firewall come up without its ports.
echo "== kernel module =="

# The kernel's sources fetched once, when no module of this project is installed at all: a first
# install. The follower is asked with NOFETCH=1 first, so an installer does not start a 350 MB
# download nobody asked for - right for an update, whose module was built with the sources already
# here. But on 2026-10-20 an XGS 126 installed from a checkout got no driver: "no sources for
# abbbe67a45e7 here, and NONET=1 forbids fetching them" (#628), and its front ports never came up
# (#625). Nothing would have fetched them later either: the cron entry follows a module that is
# installed, and there was none. NOFETCH=1 given to this installer keeps it from fetching.
build_fetching() {
    if [ "${NOFETCH:-0}" = 1 ] || [ -f "/boot/modules/$1.ko" ]; then
        return 1
    fi
    echo "   no $1.ko is installed and the kernel's sources are not here: fetching them once - about"
    echo "   350 MB from GitHub - and building (NOFETCH=1 given to this installer skips this)"
    if DRIVER="$1" FORCE=1 sh "${PREFIX}/opnsense/scripts/octep/kernel-follow.sh" >> /var/db/os-xgs-npu/install-build.log 2>&1; then
        echo "   fetched, built and installed through the kernel follower:"
        grep -E '^(built|installed|kept)' /var/db/os-xgs-npu/install-build.log | sed 's/^/     /'
        return 0
    fi
    echo "   the sources could not be fetched, or the build failed. Why:"
    tail -4 /var/db/os-xgs-npu/install-build.log | sed 's/^/     /'
    return 1
}

if [ "${OCTEONTX}" = 0 ]; then
    # Through the kernel follower first, as the OCTEON TX module is below: built from the sources
    # laid down above, against the kernel the next boot will use, stamped, the previous one kept.
    # Until 2026-10-13 this branch only copied a module built somewhere else, and preferred the
    # appliance's /root/npu/kmod over the checkout - which took a build from 2026-09-26 over the
    # one made minutes before from a fresh checkout, both stamped for the same kernel, and said
    # nothing (issue 372). A build made here, now, needs no such preference.
    KO=""
    if DRIVER=npuep FORCE=1 NOFETCH=1 sh "${PREFIX}/opnsense/scripts/octep/kernel-follow.sh" > /var/db/os-xgs-npu/install-build.log 2>&1; then
        echo "   built and installed through the kernel follower:"
        grep -E '^(built|installed|kept)' /var/db/os-xgs-npu/install-build.log | sed 's/^/     /'
        KO=followed
    elif [ -f /boot/modules/npuep.ko ] && [ "$(cat /boot/modules/npuep.ko.kernel 2>/dev/null)" = "$(uname -v)" ]; then
        # The build failed and a module for this kernel is installed: it stays, as the follower
        # said. Until 2026-10-18 the fall-back below ran here as well and put a stale build from
        # /root/npu/kmod - four days and a lock fix older, stamped for the same kernel - over the
        # working one; the next boot panicked on that lock at every start.
        echo "   THE BUILD FAILED - the module in /boot/modules is untouched and still the last good one."
        tail -4 /var/db/os-xgs-npu/install-build.log | sed 's/^/     /'
        KO=kept
    else
        echo "   the follower built nothing, and no module for this kernel is installed. Why:"
        tail -4 /var/db/os-xgs-npu/install-build.log | sed 's/^/     /'
        if build_fetching npuep; then
            KO=followed
        else
            # A module built somewhere else is taken only when its stamp names the running kernel,
            # and only here, where nothing is installed for it: an appliance with no kernel sources yet.
            for c in /root/npu/kmod/npuep.ko "${SRC}/contrib/npuep/npuep.ko"; do
                [ -f "${c}" ] || continue
                if [ "$(cat "${c}.kernel" 2>/dev/null)" = "$(uname -v)" ]; then
                    KO="${c}"
                    break
                fi
                echo "   skipped ${c}: its stamp '$(cat "${c}.kernel" 2>/dev/null)' is not the running kernel"
            done
        fi
    fi
    if [ "${KO}" = followed ] || [ "${KO}" = kept ]; then
        :
    elif [ -n "${KO}" ]; then
        install -d -m 0755 /boot/modules
        install -m 0555 "${KO}" /boot/modules/npuep.ko
        echo "   installed ${KO} as /boot/modules/npuep.ko"
        # Carry the build's kernel stamp with it, and say so now rather than at the next boot.
        if [ -f "${KO}.kernel" ]; then
            install -m 0444 "${KO}.kernel" /boot/modules/npuep.ko.kernel
            if [ "$(cat "${KO}.kernel")" != "$(uname -v)" ]; then
                echo "   WARNING: built against a different kernel than the one running."
                echo "            built  : $(cat "${KO}.kernel")"
                echo "            running: $(uname -v)"
                echo "            Rebuild before rebooting: sh contrib/npuep/build.sh"
            fi
        else
            rm -f /boot/modules/npuep.ko.kernel
            echo "   no kernel stamp beside it - verify.sh cannot tell whether it matches"
        fi
    else
        echo "   none built yet - see contrib/npuep/build.sh; the boot hook will skip until there is one"
    fi
else
    rm -f /boot/modules/npuep.ko /boot/modules/npuep.ko.kernel
fi

# The OCTEON TX module goes through the kernel follower and nowhere else.
#
# FORCE=1 because an install has to build even when nothing is installed yet, or when the stamp
# already matches; NOFETCH=1 because an installer should not start a 350 MB download nobody asked for.
# The follower builds for the kernel the NEXT boot will use - a staged kernel if there is one - which
# is the right target for an install too: building for the running kernel in the window between a
# kernel update and its reboot would undo what the follower had just done.
#
# There is deliberately no fallback to a module found in a checkout. An earlier version of this file
# fell through to contrib/octep/octep.ko when its own build failed, right after printing that the
# installed module was untouched - and a module built in a checkout with a bare `make` carries no
# stamp, so the next boot would have loaded it with no kernel check at all.
if [ "${OCTEONTX}" = 1 ]; then
    if FORCE=1 NOFETCH=1 sh "${PREFIX}/opnsense/scripts/octep/kernel-follow.sh" > /var/db/os-xgs-npu/install-build.log 2>&1; then
        echo "   built and installed through the kernel follower:"
        grep -E '^(built|installed|kept)' /var/db/os-xgs-npu/install-build.log | sed 's/^/     /'
    else
        echo "   no module built - the module in /boot/modules is untouched. Why:"
        tail -4 /var/db/os-xgs-npu/install-build.log | sed 's/^/     /'
        build_fetching octep || true
    fi
fi

# ---------------------------------------------------------------- the ARMADA loader line

# hidraw(4) is what gives a device node for the USB-SPI bridge. hidbus attaches on its own but
# leaves no node until a child driver claims it, and without that node nothing can reach the
# reset line. Loading it from loader.conf rather than from the hook keeps it available even if
# the hook is later removed.
#
# Not on an OCTEON TX appliance: there the only thing that node would reach is the bridge to the
# coprocessor's boot flash, and nothing this plugin installs there has any business opening it.
if [ "${OCTEONTX}" = 0 ]; then
    echo "== loader =="
    # The value is written literally. It used to be printf '%cYES%c' 34 34, which in sh(1) prints the
    # first CHARACTER of the argument "34" - so every install wrote hidraw_load=3YES3, the loader read
    # that as not-YES, and hidraw was only ever loaded by 06-npuctl itself. A line in that form is
    # replaced.
    if grep -q '^hidraw_load="YES"$' /boot/loader.conf.local 2>/dev/null; then
        echo "   hidraw_load already present"
    else
        if [ -f /boot/loader.conf.local ]; then
            sed -i '' '/^hidraw_load=/d' /boot/loader.conf.local
        fi
        echo 'hidraw_load="YES"' >> /boot/loader.conf.local
        echo "   added hidraw_load to /boot/loader.conf.local"
    fi
    kldstat -q -m hidraw || kldload hidraw 2>/dev/null || true
elif grep -q '^hidraw_load=' /boot/loader.conf.local 2>/dev/null; then
    # An older version of this file put the line on every appliance.
    sed -i '' '/^hidraw_load=/d' /boot/loader.conf.local
    echo "== loader: removed the ARMADA hidraw_load line from /boot/loader.conf.local =="
fi

echo
echo "== installed =="
if [ "${OCTEONTX}" = 0 ]; then
    echo "   Both hooks run at the next boot: 06 releases the NPU from reset, 07 loads the driver"
    echo "   and waits for the front ports so that OPNsense's interface configuration can see them."
    echo
    echo "   To release the NPU now, without rebooting:"
    echo "     ${PREFIX}/etc/rc.syshook.d/early/06-npuctl"
    echo
    echo "   To see what the bridge reports, changing nothing:"
    echo "     python3 ${PREFIX}/opnsense/scripts/npuctl/mcp2210.py status"
    echo
    echo "   After reloading the module by hand, the front ports' ifnets are new, so a bridge over"
    echo "   them has to be rebuilt. The path costs follow within a minute on their own:"
    echo "     configctl interface bridge configure"
    echo "     ${PREFIX}/opnsense/scripts/npuctl/bridge-pathcost.sh   # or just wait"
else
    echo "   08-octep brings the front ports up at the next boot, before OPNsense configures them."
    echo "   The module follows the kernel on its own: from the upgrade hook, and every five minutes."
    echo
    echo "   To see what the follower would do, changing nothing:"
    echo "     DRY=1 sh ${PREFIX}/opnsense/scripts/octep/kernel-follow.sh"
fi

# What this installer wrote, by digest, so that a problem report can say whether the installation it
# came from has been changed by hand since (scripts/xgs/reporter.py, modified_files). Names only
# ever leave the machine, never contents.
{
    find "${PREFIX}/opnsense/scripts/xgs" "${PREFIX}/opnsense/scripts/octep" "${PREFIX}/opnsense/scripts/npuctl" \
        "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS" "${PREFIX}/opnsense/mvc/app/models/OPNsense/XGS" \
        "${PREFIX}/opnsense/mvc/app/views/OPNsense/XGS" -type f ! -name '*.pyc' 2> /dev/null
    for f in "${PREFIX}/etc/inc/plugins.inc.d/xgs.inc" \
        "${PREFIX}/opnsense/service/conf/actions.d/actions_xgs.conf" \
        "${PREFIX}/opnsense/mvc/app/library/OPNsense/System/Status/XGSReporterStatus.php" \
        "${PREFIX}"/etc/rc.syshook.d/*/*-xgs-* "${PREFIX}"/etc/rc.syshook.d/*/*-octep* \
        "${PREFIX}"/etc/rc.syshook.d/*/*-npuep* "${PREFIX}"/etc/rc.syshook.d/*/*-npuctl*; do
        [ -f "${f}" ] && echo "${f}"
    done
} | sort -u | xargs sha256 -r > "${PREFIX}/share/os-xgs-npu/manifest.sha256" 2> /dev/null || true

# ---------------------------------------------------------------- one thing that is not ours
#
# Check that this box can resolve a name, and say so plainly if it cannot.
#
# This is not the driver's business, and it is here because it cost most of a day: a fresh
# OPNsense leaves unbound recursing to the root servers, an upstream that does not permit that
# makes every query SERVFAIL, and the symptom arrives disguised. A machine with more than one
# network keeps every adapter's nameservers and resolves through whichever works, so the port the
# box is administered from looks healthy while a network whose only resolver is this firewall has
# no internet at all - and the firewall still answers a ping by address, because an address needs
# no name. Everything then points at the datapath, which is the one thing that is working.
#
# A check that costs five seconds at install time is worth more than the warning in the README.
if command -v host > /dev/null 2>&1; then
    if ! host -W 5 opnsense.org 127.0.0.1 > /dev/null 2>&1; then
        echo
        echo "== this box cannot resolve a name =="
        echo "   host opnsense.org 127.0.0.1 failed. Nothing above depends on it, and nothing"
        echo "   above caused it, but a network whose only resolver is this firewall will have no"
        echo "   internet until it is fixed - while still answering ping by address."
        echo
        echo "   Give the system nameservers and let the resolver forward to them rather than"
        echo "   recurse: System - Settings - General - DNS servers, then Services - Unbound DNS -"
        echo "   Query Forwarding - Use System Nameservers."
    fi
fi

# Counted now, not up to a day later (scripts/xgs/count.py): the daily cron's first run after an
# install or an update is the next morning, so on 2026-10-20 an XGS 3300 updated at 16:41 UTC was not
# on npuforge.org's counter an hour later. Detached and quiet; count.py reads the owner's setting
# ("Count this appliance" on XGS: Problem Reports) and sends nothing when it is off.
/usr/sbin/daemon -f /usr/local/bin/python3 "${PREFIX}/opnsense/scripts/xgs/count.py" > /dev/null 2>&1 || true
