#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
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
FAMILY=${BOARD#* }

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

# ---------------------------------------------------------------- the OPNsense integration
#
# All of this is discovered rather than registered: plugins_scan() globs plugins.inc.d, the menu
# and ACL loaders glob the model directories by vendor, and configd reads every actions_*.conf.
# So the package reaches the GUI without editing one OPNsense file, and an OPNsense update cannot
# undo it. Both boards get it - the status page reports an absent driver perfectly well, which is
# more useful than a missing menu entry.

echo "== OPNsense integration =="
install -d -m 0755 "${PREFIX}/etc/inc/plugins.inc.d"
install -m 0644 "${SRC}/src/etc/inc/plugins.inc.d/xgs.inc" "${PREFIX}/etc/inc/plugins.inc.d/xgs.inc"

install -d -m 0755 "${PREFIX}/opnsense/service/conf/actions.d"
install -m 0644 "${SRC}/src/opnsense/service/conf/actions.d/actions_xgs.conf" \
    "${PREFIX}/opnsense/service/conf/actions.d/actions_xgs.conf"

for d in models/OPNsense/XGS/Menu models/OPNsense/XGS/ACL \
         controllers/OPNsense/XGS/Api views/OPNsense/XGS; do
    install -d -m 0755 "${PREFIX}/opnsense/mvc/app/${d}"
done
install -m 0644 "${SRC}/src/opnsense/mvc/app/models/OPNsense/XGS/Menu/Menu.xml" \
    "${PREFIX}/opnsense/mvc/app/models/OPNsense/XGS/Menu/Menu.xml"
install -m 0644 "${SRC}/src/opnsense/mvc/app/models/OPNsense/XGS/ACL/ACL.xml" \
    "${PREFIX}/opnsense/mvc/app/models/OPNsense/XGS/ACL/ACL.xml"
install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/StatusController.php" \
    "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/StatusController.php"
install -m 0644 "${SRC}/src/opnsense/mvc/app/controllers/OPNsense/XGS/Api/StatusController.php" \
    "${PREFIX}/opnsense/mvc/app/controllers/OPNsense/XGS/Api/StatusController.php"
install -m 0644 "${SRC}/src/opnsense/mvc/app/views/OPNsense/XGS/status.volt" \
    "${PREFIX}/opnsense/mvc/app/views/OPNsense/XGS/status.volt"

# The menu is cached in a file with a time-to-live, so a new entry can be up to that long in
# appearing. Removing it costs one rebuild on the next page load and makes the install immediate.
rm -f /tmp/opnsense_menu_cache.xml

# configd reads actions.d once, at start. "configctl configd reload" re-reads templates and NOT
# the action list, so a new action stays invisible until the daemon is restarted - measured, it
# answers "Action not allowed or missing" until then. configd is the configuration daemon and not
# the datapath, so a restart costs nothing that is carrying traffic.
if [ -x /usr/sbin/service ] && [ -f "${PREFIX}/opnsense/service/conf/actions.d/actions_xgs.conf" ]; then
    /usr/sbin/service configd restart > /dev/null 2>&1 || true
fi

# ---------------------------------------------------------------- the ARMADA tools

if [ "${OCTEONTX}" = 0 ]; then
    echo "== tools =="
    install -d -m 0755 "${PREFIX}/opnsense/scripts/npuctl"
    install -m 0755 "${SRC}/src/opnsense/scripts/npuctl/mcp2210.py" "${PREFIX}/opnsense/scripts/npuctl/"
    install -m 0755 "${SRC}/src/opnsense/scripts/npuctl/npuhs.py" "${PREFIX}/opnsense/scripts/npuctl/"
    install -m 0755 "${SRC}/install/verify.sh" "${PREFIX}/opnsense/scripts/npuctl/verify.sh"
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
if [ "${OCTEONTX}" = 0 ]; then
    install -m 0755 "${SRC}/src/etc/rc.syshook.d/early/06-npuctl" "${PREFIX}/etc/rc.syshook.d/early/06-npuctl"
    install -m 0755 "${SRC}/src/etc/rc.syshook.d/early/07-npuep" "${PREFIX}/etc/rc.syshook.d/early/07-npuep"
else
    rm -f "${PREFIX}/etc/rc.syshook.d/early/06-npuctl" "${PREFIX}/etc/rc.syshook.d/early/07-npuep"
fi
# 08-octep looks for its own PCI id and exits on a machine without it, so it is safe on both.
install -m 0755 "${SRC}/src/etc/rc.syshook.d/early/08-octep" "${PREFIX}/etc/rc.syshook.d/early/08-octep"

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

# And the one that does not depend on which path asked for the reboot - see the file. rc.reboot,
# rc.halt and rc.shutdown all run `rc.syshook stop` before anything is torn down, so this is the
# layer that caught the update neither of the two above did.
install -d -m 0755 "${PREFIX}/etc/rc.syshook.d/stop"
install -m 0755 "${SRC}/src/etc/rc.syshook.d/stop/20-octep" "${PREFIX}/etc/rc.syshook.d/stop/20-octep"

# The OCTEON TX bring-up is a sequence rather than a kldload, so it lives in its own script and
# the hook calls it. That also makes it runnable by hand, which is how it gets changed.
install -d -m 0755 "${PREFIX}/opnsense/scripts/octep"
install -m 0755 "${SRC}/src/opnsense/scripts/octep/bringup.sh" "${PREFIX}/opnsense/scripts/octep/bringup.sh"

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
# hold exactly what this repository holds. The files are named rather than globbed: a checkout that
# has been built in place carries generated bus_if.h, device_if.h, pci_if.h and opt_*.h beside the
# real headers, and a `*.h` copied those along, newer than any .m file and so never regenerated.
# The tree is replaced, not added to, so a file the repository drops does not linger here.
OCTEPSRC="${PREFIX}/share/os-xgs-npu/octep"
echo "== driver sources =="
rm -rf "${OCTEPSRC}"
install -d -m 0755 "${OCTEPSRC}"
install -d -m 0750 /var/db/os-xgs-npu
for f in Makefile octep.h octep.c octep_mgmt.c octep_sdp.c octep_dp.c octep_nwa.c octep_rpc.c; do
    install -m 0644 "${SRC}/contrib/octep/${f}" "${OCTEPSRC}/${f}"
done
echo "   ${OCTEPSRC}"

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

if [ "${OCTEONTX}" = 0 ]; then
    # The appliance's own build directory comes FIRST, and the order is deliberate. A .ko is only
    # valid for the kernel it was compiled against, /root/npu/kmod is where it is compiled on the
    # machine it will run on, and a checkout can carry a stale one from anywhere. Searching the
    # checkout first meant a left-over file won silently and the next boot loaded the wrong build.
    KO=""
    for c in /root/npu/kmod/npuep.ko "${SRC}/contrib/npuep/npuep.ko"; do
        if [ -f "${c}" ]; then
            KO="${c}"
            break
        fi
    done
    if [ -n "${KO}" ]; then
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
