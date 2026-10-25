#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Unload the module, reset the coprocessor, load it again, and report. Run: sh /root/npu/kmod/reload.sh
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

# The endpoint, by vendor and device rather than by a hard-coded path, because the bus number
# is not ours to assume.
#
# Parse it with awk's DEFAULT field separator. An earlier version passed -F'[@ ]', which does
# not include the tab that pciconf actually puts between the selector and the class - so the
# field came out as "pci0:1:0:0:<tab>class=0x020000", pciconf refused to parse it, every probe
# fell back to the all-ones default, and the script waited the full ninety seconds and blamed
# the hardware for a bug in this line. A dead device and an unparseable selector are not the
# same thing and must not look the same, which is what the shape check below is for.
SEL=$(pciconf -l 2>/dev/null | awk '/chip=0x708011ab|vendor=0x11ab/ {
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

if kldstat -q -n npuep; then
    echo "== unloading =="
    kldunload npuep || true
fi

echo "== resetting the NPU =="
python3 "${CTL}/mcp2210.py" pulse || true

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
kldload "${D}/npuep.ko"
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
    echo "   the management link: $(sh /usr/local/opnsense/scripts/npuctl/mgmt-link.sh coprocessor 2>/dev/null | tail -1)"
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
