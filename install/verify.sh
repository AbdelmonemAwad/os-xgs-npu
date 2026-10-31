#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Check, on the appliance, that the whole chain still works.
#
# Run this after every OPNsense or FreeBSD update, and after anything that touches the boot path.
# It is the answer to the one question CI cannot ask: a GitHub runner has no Sophos appliance, no
# USB-SPI bridge and no coprocessor, so a green tick upstream says nothing about whether this
# machine still has all its front ports.
#
# It changes NOTHING. Every check reads. It is safe on a running firewall and safe to run twice.
#
# THE FAILURE THIS EXISTS FOR: the kernel module is built against one kernel's headers. An update
# that moves FreeBSD forward leaves a module that will not load, and the appliance then boots to a
# firewall with no front ports and no obvious reason why - the hook exits 0 by design, because a
# firewall that boots late is better than one that does not boot. So the silence is deliberate,
# and this is what breaks it.
#
#   sh /usr/local/opnsense/scripts/npuctl/verify.sh
#   sh install/verify.sh            (from a checkout)
#
# Exit 0 if everything a firewall needs is true, 1 otherwise.

pass=0
fail=0
warn=0

ok()   { pass=$((pass + 1)); printf '  ok    %s\n' "$1"; }
bad()  { fail=$((fail + 1)); printf '  FAIL  %s\n' "$1"; }
note() { warn=$((warn + 1)); printf '  note  %s\n' "$1"; }

echo "== the appliance =="
printf '  %s\n' "$(uname -sr)"
printf '  %s\n' "$(opnsense-version 2>/dev/null || echo 'opnsense-version unavailable')"
printf '  board: %s / %s\n' "$(kenv smbios.planar.maker 2>/dev/null)" "$(kenv smbios.planar.product 2>/dev/null)"
# The model by its assembly number and the board's revision, never its serial number: this output
# is meant to be pasted where anyone can read it. Two boards of one model can differ (issue 736).
set -- $(python3 /usr/local/opnsense/scripts/xgs/models.py 2>/dev/null)
if [ $# -ge 3 ]; then
    printf '  model: %s, assembly %s, board revision %s\n' "$(echo "$3" | tr _ ' ')" "$1" \
        "$(kenv -q smbios.planar.version 2>/dev/null || echo unknown)"
fi
set --

# Which coprocessor this machine has. The ARMADA one (11ab:7080) is npuep's, and the checks of
# npuep, its hooks, its endpoint and its npup ports below apply only where it is - as the OCTEON
# TX section further down applies only where that one is. Until 2026-10-29 the ARMADA checks ran
# everywhere, and an XGS 3300 with all its front ports up was told it had none that worked.
armada=0
/usr/sbin/pciconf -l 2>/dev/null |
    grep -qE 'chip=0x708011ab|vendor=0x11ab[[:space:]]+device=0x7080' && armada=1

if [ "$armada" = 1 ]; then
    echo
    echo "== what is installed =="
    for f in /usr/local/etc/rc.syshook.d/early/06-npuctl /usr/local/etc/rc.syshook.d/early/07-npuep; do
        if [ -x "$f" ]; then ok "$(basename $f) present and executable"; else bad "$f missing or not executable"; fi
    done
    if [ -f /boot/modules/npuep.ko ]; then ok "the module is installed in /boot/modules"; else bad "/boot/modules/npuep.ko missing - the boot hook will skip and there will be no front ports"; fi

    echo
    echo "== does the module match this kernel =="
    # The check that matters after an update, and it loads nothing.
    #
    # It used to run `kldload -n /boot/modules/npuep.ko`, on the belief - written into the comment
    # that used to be here - that kldload reports a version mismatch rather than loading. It does
    # not. There is no dry run; -n means "do not load if it is already loaded", and this branch only
    # runs when it is not. So the check LOADED the module. On a firewall, from a script whose own
    # header promises that it changes nothing. And when that load succeeded, every check after it
    # passed - so a boot that had failed was reported as a healthy chain, which is the one
    # circumstance this file exists to catch.
    #
    # It cannot be done by trying the load anyway. A module's kernel dependency is a RANGE, from the
    # __FreeBSD_version it was built against to the end of that branch, so a module built for 15.1
    # loads cleanly into any later 15.x kernel rather than being refused. The mismatch has to be
    # caught out of band, by comparing the stamp contrib/npuep/build.sh writes beside the module.
    if kldstat -q -n npuep; then
        ok "npuep is loaded"
    elif [ ! -f /boot/modules/npuep.ko ]; then
        bad "npuep is NOT loaded and there is no module in /boot/modules to load"
    else
        bad "npuep is NOT loaded"
        built=$(cat /boot/modules/npuep.ko.kernel 2>/dev/null)
        now=$(uname -v)
        if [ -z "${built}" ]; then
            note "no build stamp beside the module, so this cannot tell whether it matches"
            note "to see why it did not load: kldload /boot/modules/npuep.ko"
        elif [ "${built}" != "${now}" ]; then
            bad "it was built against a different kernel - rebuild: sh contrib/npuep/build.sh"
            printf '        built  : %s\n' "${built}"
            printf '        running: %s\n' "${now}"
        else
            note "it matches this kernel, so it failed to load for some other reason"
            note "to see it: kldload /boot/modules/npuep.ko"
        fi
    fi
else
    echo
    echo "== ARMADA side =="
    note "no ARMADA endpoint on this machine, so npuep does not apply here"
fi

echo
echo "== OCTEON TX side: XGS 2100, 2300, 3100, 3300 =="
# Same check, same reason, different module. A machine has one coprocessor or the other, so one of
# these two sections finding nothing is the normal case rather than a fault.
if ! /usr/sbin/pciconf -l 2>/dev/null |
    grep -qE 'chip=0xa300177d|vendor=0x177d[[:space:]]+device=0xa300'; then
    note "no OCTEON TX endpoint on this machine, so octep does not apply here"
elif [ ! -f /boot/modules/octep.ko ]; then
    bad "/boot/modules/octep.ko missing - 08-octep will skip and there will be no front ports"
else
    ok "octep.ko is installed in /boot/modules"
    obuilt=$(cat /boot/modules/octep.ko.kernel 2>/dev/null)
    onow=$(uname -v)
    if [ -z "${obuilt}" ]; then
        note "no build stamp beside it, so this cannot tell whether it matches"
    elif [ "${obuilt}" != "${onow}" ]; then
        bad "octep was built against a different kernel - rebuild: sh contrib/octep/build.sh"
        printf '        built  : %s
' "${obuilt}"
        printf '        running: %s
' "${onow}"
        note "the bring-up refuses to load it in this state rather than loading it wrongly"
    else
        ok "octep matches this kernel"
    fi
    if kldstat -q -n octep; then
        ok "octep is loaded"
        n=$(ifconfig -l 2>/dev/null | tr ' ' '
' | grep -c '^oxp')
        if [ "${n}" -gt 0 ]; then ok "${n} front-port interfaces"; else bad "no oxp interfaces - see the octep messages in the log"; fi
    else
        note "octep is not loaded; run /usr/local/opnsense/scripts/octep/bringup.sh to see why"
    fi
fi

if [ "$armada" = 1 ]; then
    echo
    echo "== the coprocessor =="
    if [ "$(sysctl -n dev.npuep.0.%desc 2>/dev/null | wc -l)" -gt 0 ] 2>/dev/null || sysctl -n dev.npuep.0.rpc_channel.open >/dev/null 2>&1; then
        ok "the endpoint attached"
    else
        bad "no dev.npuep.0 - the endpoint did not attach"
    fi

    # All-ones is what a PCIe read returns when nothing answers. It is the signature of a coprocessor
    # that has stopped, and it is worth naming rather than letting every other check fail obscurely.
    db=$(sysctl -n dev.npuep.0.doorbells 2>/dev/null | sed -n '3p')
    case "$db" in
        *ffffffff*) bad "the endpoint reads all ones - the coprocessor has stopped. Run contrib/npuep/reload.sh; if that does not recover it, power cycle" ;;
        '')         note "could not read the doorbell block" ;;
        *)          ok "the endpoint is answering" ;;
    esac

    # Evidence from the boot, which ages out.
    #
    # The next few facts are printed once each, at attach. dmesg's ring buffer is finite, so on a box
    # that has been up for days they will have scrolled away - and a check that reads "it never
    # happened" off a wrapped buffer reports a healthy machine as broken. So look in dmesg, then in
    # what syslog kept, and if the buffer has clearly wrapped say that instead of failing. Defined
    # before the front ports' check, whose no-interfaces branch asks it too.
    saw() {
        dmesg 2>/dev/null | grep -q -- "$1" && return 0
        grep -qs -- "$1" /var/log/system/latest.log && return 0
        return 1
    }
    # The driver's first attach line. If even that is gone, nothing from the boot is left to read.
    aged_out() {
        dmesg 2>/dev/null | grep -q "barmap version" && return 1
        return 0
    }

    echo
    echo "== the front ports =="
    # How many this model has, as the driver took it from its own table of the model: one receive
    # counter per front port (dev.npuep.0.giu.rx_port), live; else the boot line that names the model.
    # It was fourteen, the XGS 136's, for every model - and so an XGS 107w with all nine of its ports
    # was told it had none that worked (issue 890).
    want=$(sysctl -N dev.npuep.0.giu.rx_port 2>/dev/null | grep -c '^dev\.npuep\.0\.giu\.rx_port\.')
    if [ "${want:-0}" -eq 0 ]; then
        want=$({ dmesg 2>/dev/null; grep -hs 'front ports$' /var/log/system/latest.log; } |
            sed -n 's/.*npuep: .*: \([0-9][0-9]*\) front ports$/\1/p' | tail -1)
    fi
    n=$(ifconfig -l 2>/dev/null | tr ' ' '\n' | grep -c '^npup')
    if [ -z "$want" ]; then
        if [ "$n" -gt 0 ]; then
            note "$n npup interfaces - the driver did not say how many front ports this model has"
        else
            bad "no npup interfaces at all"
        fi
    elif [ "$n" -eq "$want" ]; then
        ok "$n interfaces npup1..npup$n, every front port this model has"
    elif [ "$n" -gt 0 ]; then
        bad "only $n npup interfaces - this model has $want"
    else
        bad "no npup interfaces at all"
    fi
    if [ "$n" -eq 0 ]; then
        # The most common cause, and the one nobody guesses.
        if saw "answers HOST_MGMT_READY once per coprocessor boot"; then
            bad "the datapath refused a second bring-up - the coprocessor has to restart. sh contrib/npuep/reload.sh pulses its reset line and waits for it, which is verified to bring every front port back"
        fi
    fi

    # What the driver said on its control channel, addresses masked: printed under a table that is not
    # confirmed, so that what an owner pastes from here is the whole story on a board this project has
    # never seen (issue 736).
    rpc_said() {
        { dmesg 2>/dev/null; grep -hs 'npuep0: rpc: ' /var/log/system/latest.log; } |
            grep -o 'npuep0: rpc: .*' |
            sed -E 's/([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}/xx:xx:xx:xx:xx:xx/g' |
            awk '!seen[$0]++' | tail -n 8 | sed 's/^/          /'
    }

    # The driver keeps the reason itself, live, in one line without addresses (dev.npuep.0.iftable);
    # a build older than that leaves only the boot messages to go by.
    why=$(sysctl -n dev.npuep.0.iftable 2>/dev/null)
    case "$why" in
        confirmed*)
            ok "the coprocessor's interface table was programmed and read back" ;;
        "not yet"*|"the channel is open"*|"not reached yet"*)
            note "the interface table is still being written - run this again in a minute"
            printf '        why: %s\n' "$why" ;;
        ?*)
            bad "the interface table is not confirmed - received frames will be dropped before they are counted"
            printf '        why: %s\n' "$why"
            echo   "        the driver said:"
            rpc_said ;;
        *)
            if saw "read back and confirmed"; then
                ok "the coprocessor's interface table was programmed and read back"
            elif aged_out; then
                note "cannot tell whether the interface table was confirmed - the boot messages have aged out"
            else
                bad "the interface table was never confirmed - received frames will be dropped before they are counted"
                echo   "        the driver said:"
                rpc_said
            fi ;;
    esac

    # The network agent's own count, "nwa: <up> of <ports> ports up", the last one it printed.
    nwa=$({ dmesg 2>/dev/null; grep -hs 'nwa: ' /var/log/system/latest.log; } |
        grep -o 'nwa: [0-9][0-9]* of [0-9][0-9]* ports up' | tail -1)
    up=$(echo "$nwa" | sed -n 's/^nwa: \([0-9]*\) of .*/\1/p')
    of=$(echo "$nwa" | sed -n 's/.* of \([0-9]*\) ports up$/\1/p')
    if [ -n "$up" ] && [ "$up" = "$of" ]; then
        ok "all $of ports were commanded up"
    elif [ -n "$up" ]; then
        note "the network agent reported $up of $of ports up"
    elif aged_out; then
        note "cannot tell whether every port was commanded up - the boot messages have aged out"
    else
        note "the network agent did not report its ports up"
    fi
fi

echo
echo "== thermal, because this board has nothing else watching =="
t=$(sysctl -n dev.cpu.0.temperature 2>/dev/null)
if [ -n "$t" ]; then
    ok "CPU temperature readable: $t"
else
    bad "no CPU temperature - load amdtemp (this is an AMD board; coretemp is the wrong driver)"
fi
grep -q '^amdtemp_load=' /boot/loader.conf.local 2>/dev/null \
    && ok "amdtemp survives a reboot" \
    || note "amdtemp is not in loader.conf.local - the reading will be gone after a reboot"

echo
echo "== interfaces sharing a subnet =="
# Two interfaces in one subnet share one route, and the one applied last loses it - cable or no
# cable - so one of them answers nothing from that subnet (issue 911; scripts/xgs/subnets.py).
SUBNETS=/usr/local/opnsense/scripts/xgs/subnets.py
if [ -x "${SUBNETS}" ]; then
    shared=$(/usr/local/bin/python3 "${SUBNETS}" --text 2>/dev/null)
    if [ -z "${shared}" ]; then
        ok "no two interfaces share a subnet"
    else
        note "two interfaces share a subnet - give each its own, or take the address off the one not used:"
        printf '%s\n' "${shared}" | sed 's/^/          /'
    fi
fi

echo
echo "== summary =="
printf '  %d passed, %d failed, %d worth a look\n' "$pass" "$fail" "$warn"
if [ "$fail" -gt 0 ]; then
    echo "  This appliance does not currently have working front ports."
    exit 1
fi
echo "  The chain is intact."
exit 0
