#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Unload the module, reset the coprocessor, load it again, and report. Run:
#   sh /usr/local/share/os-xgs-npu/source/contrib/npuep/reload.sh    the installed module
#   sh <a build directory>/reload.sh                                 the npuep.ko beside it
#
# UNLOAD FIRST, THEN THE RESET - and this header said the opposite until 2026-10-25 (#780), while
# the code below has always done it this way, which is the right one. A loaded driver reads its
# endpoint's BARs, and a read of an endpoint in reset never completes: the paragraph further down
# is what that costs. The reset right after the unload is not tidiness either. The detach path
# withdraws what it can - its goodbye, its rings' addresses, its connections out of the flow
# table - but the coprocessor may still be writing into host memory it was given, and only the
# reset makes that certain. It also puts the far side back to a known state for the next attach.
#
# WAIT FOR THE ENDPOINT, DO NOT COUNT SECONDS. This script used to sleep 25 seconds and load,
# on the reasoning that userspace cannot see whether the far side is back: the reset clears the
# endpoint BARs, FreeBSD keeps serving its cached addresses, and reads through /dev/mem come
# back as all ones however healthy the coprocessor is. That is true of MEMORY space and it is
# not true of CONFIG space, which is what this now polls - `pciconf -r` reads it live, and a
# device that is not answering reads 0xffff there rather than hanging.
#
# The distinction is worth a paragraph because getting it wrong stops the machine. A read of a
# BAR belonging to an endpoint whose link is down does not fail and does not time out: it never
# completes, and the host dies with no panic and no console output. Measured here, at the cost
# of a power cycle, after a pulse and a FORTY-FIVE second wait - longer than the twenty-five
# this script used, which is the point. The wait was never too short. It was blind.
set -e
CTL=/usr/local/opnsense/scripts/npuctl
D=$(dirname "$0")
: ${WAIT:=90}

# THE MODULE TO LOAD AGAIN, FOUND BEFORE ANYTHING IS UNLOADED. The one beside this script where there is
# one - the build directory it is run from while a driver is being worked on - and otherwise the
# installed one, chosen as the boot hook early/07-npuep chooses it: /boot/modules/npuep.ko, or its
# .prev where that one is built for another kernel and .prev for this one. Until 2026-11-06 it was
# "${D}/npuep.ko" whatever was there, and looked for only at the end: run from the plugin's source tree,
# as install/verify.sh tells an owner to, it unloaded the driver and reset the coprocessor, found no
# module there, and left the appliance without its front ports until a restart (the XGS 136,
# 2026-11-06; the review's A15). MODULE= names another.
if [ -z "${MODULE:-}" ]; then
    if [ -f "${D}/npuep.ko" ]; then
        MODULE=${D}/npuep.ko
    else
        MODULE=/boot/modules/npuep.ko
        NOW=$(/usr/bin/uname -v)
        # .prev only for an npuep.ko stamped for another kernel: one with no stamp is loaded, as the hook
        # loads it - until the release check's review of 0.1.0-alpha.15.1 an npuep.ko copied in by hand,
        # without its stamp, gave way here to the older .prev
        if [ -n "$(cat "${MODULE}.kernel" 2>/dev/null)" ] &&
            [ "$(cat "${MODULE}.kernel" 2>/dev/null)" != "${NOW}" ] && [ -f "${MODULE}.prev" ] &&
            [ "$(cat "${MODULE}.prev.kernel" 2>/dev/null)" = "${NOW}" ]; then
            MODULE=${MODULE}.prev
        fi
    fi
fi
if [ ! -f "${MODULE}" ]; then
    echo "   no ${MODULE}: nothing to load again, so nothing is unloaded and the coprocessor is not reset"
    exit 1
fi
# A module stamped for another kernel is refused before anything is unloaded, as 07-npuep refuses it
# at boot: one built against other structures reads them at the wrong offsets. A module with no stamp
# is loaded, as there. FORCE=1 loads a mismatched one anyway. Until 2026-11-07 only the choice of
# .prev followed the hook, and npuep.ko built for the next kernel was loaded into this one (the
# release check's review).
STAMP=$(cat "${MODULE}.kernel" 2>/dev/null || true)
if [ -n "${STAMP}" ] && [ "${STAMP}" != "$(/usr/bin/uname -v)" ] && [ "${FORCE:-0}" != "1" ]; then
    echo "   ${MODULE} is built for another kernel (${STAMP}): nothing is unloaded and the coprocessor is not reset."
    echo "   Rebuild it with install/kernel-follow.sh, or set FORCE=1 if you know the layouts did not change."
    exit 1
fi
echo "== the module to load again: ${MODULE} =="
: ${BOOT_TOKEN:=/var/run/xgs-npuep-boot}

# The endpoint, by vendor and device rather than by a hard-coded path, because the bus number
# is not ours to assume.
#
# Parse it with awk's DEFAULT field separator. An earlier version passed -F'[@ ]', which does
# not include the tab that pciconf actually puts between the selector and the class - so the
# field came out as "pci0:1:0:0:<tab>class=0x020000", pciconf refused to parse it, every probe
# fell back to the all-ones default, and the script waited the full ninety seconds and blamed
# the hardware for a bug in this line. A dead device and an unparseable selector are not the
# same thing and must not look the same, which is what the shape check below is for.
#
# And by the id the driver probes for, 11ab:7080, in either form pciconf has printed it. Until
# 2026-11-06 the newer form matched on the vendor alone, and the first Marvell device listed was taken,
# whatever it was (the review's A14).
SEL=$(pciconf -l 2>/dev/null | awk '/chip=0x708011ab|vendor=0x11ab[ \t]+device=0x7080/ {
    s = $1
    sub(/^[^@]*@/, "", s)   # drop the driver name and the @
    sub(/:$/, "", s)        # drop the trailing colon pciconf prints
    print s
    exit
}')
[ -n "${SEL}" ] || SEL=pci0:1:0:0

case "${SEL}" in
    pci[0-9]*:[0-9]*:[0-9]*:[0-9]*)
        ;;
    *)
        echo "cannot work out the endpoint's selector - got '${SEL}'." >&2
        echo "pciconf -l output has changed shape; fix the awk above rather than waiting." >&2
        exit 2
        ;;
esac

if kldstat -q -m pci/npuep; then
    echo "== unloading =="
    # by the file it came from: 07-npuep loads npuep.ko.prev when npuep.ko is for another kernel
    if kldstat -q -n npuep.ko.prev; then kldunload npuep.ko.prev || true; else kldunload npuep || true; fi
    # A driver that is still loaded - from a file of another name, or under a securelevel that keeps
    # it - must not see its coprocessor reset under it: that is what stopped the host (issue 780).
    if kldstat -q -m pci/npuep; then
        echo "   the driver is still loaded (kldstat -m pci/npuep): the coprocessor is not reset, nothing more is done" >&2
        exit 1
    fi
fi

echo "== resetting the NPU =="
python3 "${CTL}/mcp2210.py" pulse || true
# THE OFFLOAD GATE GOES WITH THE COPROCESSOR'S BOOT. It forgets the gate's word at each one, and the
# status page shows the word offload.sh last had it accept while that word's stamp is the token in
# BOOT_TOKEN. Until 2026-11-06 only the start hook 07-npuep-gate made it, once a host boot, and after
# a reload the page said the gate was on, from the word accepted before it, while the coprocessor had
# it off until the boot hook's settings wrote it again - which they did not when the rpc channel was
# late (the review's A6). So it is made again here, whatever the pulse answered, since one that did
# not settle may still have reset it: the word kept from before is nothing to the page, and the gate
# the settings write below is stamped with the new token.
{ od -An -N8 -tx8 /dev/urandom | tr -d ' \n'; echo; } 2>/dev/null > "${BOOT_TOKEN}" || true

echo "== waiting for ${SEL} to answer config space (up to ${WAIT}s) =="
i=0
answered=no
while [ "${i}" -lt "${WAIT}" ]; do
    v=$(pciconf -r "${SEL}" 0:1 2>/dev/null || echo ffffffff)
    case "${v}" in
        ""|*ffffffff*|*ffff)
            ;;
        *)
            echo "   answering after ${i}s: ${v}"
            answered=yes
            break
            ;;
    esac
    i=$((i + 1))
    sleep 1
done

if [ "${answered}" != yes ]; then
    # Refusing is the right answer. Loading now is what hangs the host, and a hung host is worse
    # than a driver that did not load: one of them you can still log into.
    echo "   the endpoint never answered. NOT loading - a read of its memory in this state"
    echo "   hangs the machine with no panic and no log. Power cycle and try again."
    exit 1
fi

# It answers config space, which means the link is up. That is not the same as its Linux being
# ready, and the module's own DEV_READY wait covers the rest - but it is now waiting on a device
# that can be read rather than on one that might stop the host.
echo "== loading =="
kldload "${MODULE}"
sleep 3
echo
dmesg | tail -20

# PUT THE INTERFACES BACK. A reload destroys every front port's ifnet and makes them again, and
# OPNsense is not told: a bridge over the ports is left with no members and no address, and an
# interface assigned to a port without its address - on the XGS 136 the LAN bridge and the WAN on
# Port1, so the box could only be reached over its console. Until 2026-10-20 that took three configctl
# calls by hand. Once the first port is back, the bridges are made again with their members, and
# every interface assigned to a front port, a bridge, a VLAN or a LAGG is configured again, as a
# boot does - the management link to the coprocessor among them, whose address went with its ifnet;
# then the firewall rules, which name those interfaces, and the coprocessor's own end of that link,
# which its reset forgot (mgmt-link.sh coprocessor, as the bootup hook does).
echo "== putting the interfaces back =="
i=0
while [ "${i}" -lt 60 ] && ! ifconfig npup1 > /dev/null 2>&1; do
    i=$((i + 1))
    sleep 1
done
if ! ifconfig npup1 > /dev/null 2>&1; then
    echo "   the ports did not come back in 60 s; nothing reconfigured - see dmesg above"
    exit 1
fi
# The settings a boot applies once the ports are there - the management link, the coprocessor's
# acceleration gate (its reset forgot it), the never-offloaded list, the flow maker and the
# counts - from the boot hook itself, so the two cannot drift apart. Before the interfaces, as at
# boot: the flow maker should not meet a configured port before the exclusions are in.
HOOK=/usr/local/etc/rc.syshook.d/early/07-npuep
if [ -x "${HOOK}" ]; then
    "${HOOK}" settings
    echo "   the boot hook's settings: flow maker $(sysctl -n dev.npuep.0.flow.auto 2>/dev/null)," \
        "counts to pf $(sysctl -n dev.npuep.0.flow.hw_to_pf 2>/dev/null)," \
        "to the ports $(sysctl -n dev.npuep.0.flow.lif_apply 2>/dev/null)"
fi
configctl interface bridge configure > /dev/null 2>&1 || true
# One line a key: the key, its interface and what the configuration gives it (empty for a bridge member).
/usr/local/bin/php -r '
    $c = simplexml_load_file("/conf/config.xml");
    foreach ($c->interfaces->children() as $key => $v) {
        if (preg_match("/^(npup|bridge|vlan|lagg|mvmgmt)/", (string)$v->if)) {
            echo $key, " ", $v->if, " ", (string)$v->ipaddr, "\n";
        }
    }' 2>/dev/null | while read -r key ifn want; do
    configctl interface reconfigure "${key}" < /dev/null > /dev/null 2>&1 || true
    case "${want}" in
        "") ;;
        dhcp) echo "   ${key} on ${ifn}: dhcp, asked again" ;;
        *) echo "   ${key} on ${ifn}: $(ifconfig "${ifn}" 2>/dev/null | awk '/inet / {print $2; exit}')" ;;
    esac
done
configctl filter reload > /dev/null 2>&1 || true
echo "   bridges: $(ifconfig -g bridge 2>/dev/null | tr '\n' ' ')"
if [ -x /usr/local/opnsense/scripts/npuctl/mgmt-link.sh ]; then
    # It tells the coprocessor its address again while the link does not answer: up to about five
    # minutes (#1010).
    echo "   the management link (up to five minutes): $(sh /usr/local/opnsense/scripts/npuctl/mgmt-link.sh coprocessor 2>/dev/null | tail -1)"
fi
# The coprocessor's reset also put every PoE port back on: the ones the owner switched off go off
# again (scripts/xgs/poe.py, as the start hook does at boot), and the cages' media are told again.
if [ -x /usr/local/opnsense/scripts/xgs/poe.py ]; then
    echo "   PoE: $(/usr/local/opnsense/scripts/xgs/poe.py apply --tries 6 2>/dev/null |
        /usr/local/bin/python3 -c 'import json, sys; d = json.load(sys.stdin); print(d.get("changed") or "nothing to switch")' 2>/dev/null)"
fi
if [ -x /usr/local/opnsense/scripts/xgs/transceivers.py ]; then
    /usr/local/opnsense/scripts/xgs/transceivers.py apply > /dev/null 2>&1 || true
fi
