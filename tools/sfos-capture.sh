#!/bin/sh
#
# Everything worth capturing from an XGS 3300 while it runs its own firmware, in one pass.
#
# WHY THIS EXISTS. Driving this appliance's coprocessor from another operating system has repeatedly
# turned on facts that were already on the box and had never been printed - a counter family that
# every previous capture truncated away, a tool that was never run, a mapping table nobody dumped.
# Switching the appliance back to its own firmware is expensive and disruptive, so this script is
# written to be run ONCE and to leave nothing worth a second trip.
#
# IT ONLY READS. Every command here reads. Nothing writes a register, a key, an EEPROM or a relay.
# In particular:
#   - `usfp_table_print.sh` is never given `C`, which CLEARS a counter array.
#   - `xgs-ftw`, `xgs-led*`, `xgs-eeprom -w`, `xgs-cpld <reg> <value>` and `fw_setenv` are not here.
#   - no module is loaded or unloaded, no interface is brought up or down.
# The one exception is the optional traffic generator in section 9, which adds an address and a
# neighbour entry to a front port and removes both afterwards. It is clearly marked and is skipped
# unless RUN_TRAFFIC=1.
#
# HOW TO RUN IT. From the appliance's advanced shell:
#
#     sh sfos-capture.sh 2>&1 | tee /tmp/sfos-capture.txt
#
# then get the file off the box - `xgs-scp-from.sh` or an ftpput, whichever the session has.
# It prints section banners so the result can be split up afterwards.
#
# WHAT RUNS WHERE. Commands for the coprocessor go through `xgs-ssh.sh`, which is how the vendor's
# own scripts reach it. A command that is missing prints its error and the script carries on: a
# partial capture is worth having, and a tool that is absent is itself a finding.

say() { echo; echo "##### $* #####"; }
run() { echo; echo "--- \$ $*"; eval "$@" 2>&1; }
npu() { echo; echo "--- NPU \$ $*"; xgs-ssh.sh "$*" 2>&1; }

echo "sfos-capture, $(date 2>/dev/null)"

# ----------------------------------------------------------------- 1. identity, so the rest is placeable
say "1. IDENTITY"
run "cat /conf/sophos_version 2>/dev/null; cat /etc/sophos/version-rootfs 2>/dev/null"
run "xgs-platform"
run "uname -a"
npu "uname -a; cat /proc/cmdline"
npu "cat /etc/sophos/version-rootfs; cat /etc/sophos/version-sdk"

# ----------------------------------------------------------------- 2. THE LAST HOP - the blocking question
# A driver written elsewhere gets frames to the wire, back, and into a host buffer - but each armed
# ring delivers one packet and then stops. Everything in this section is about what a WORKING
# host-bound path looks like, and the question it settles is now the rate rather than the silence.
# Section 2a is the single most valuable capture on this page.
say "2a. WHAT THE COPROCESSOR ACTUALLY SENDS THE HOST - the decisive capture"
# Twenty frames, every byte, off the interface every pport is slaved to. This shows the private
# header, the port tag and the metadata in the direction that has never been observed.
run "timeout 30 tcpdump -i oct0 -s 0 -c 20 -xx -nn 2>&1 | head -200"
run "timeout 20 tcpdump -i oct0 -s 0 -c 20 -w /tmp/oct0.pcap 2>&1; ls -la /tmp/oct0.pcap"
# and the same traffic one layer up, to see what pport strips
run "timeout 20 tcpdump -i Port1 -s 0 -c 10 -xx -nn 2>&1 | head -100"

say "2b. THE HOST'S OWN VIEW OF THE RINGS"
run "ls -la /sys/module/octeon_drv/parameters/ 2>/dev/null; for f in /sys/module/octeon_drv/parameters/*; do echo \"\$f = \$(cat \$f 2>/dev/null)\"; done"
run "ls -la /sys/module/slipf/parameters/ 2>/dev/null; for f in /sys/module/slipf/parameters/*; do echo \"\$f = \$(cat \$f 2>/dev/null)\"; done"
run "ls -la /sys/module/octnic/parameters/ 2>/dev/null; for f in /sys/module/octnic/parameters/*; do echo \"\$f = \$(cat \$f 2>/dev/null)\"; done"
run "ls -la /sys/module/mv_pport/parameters/ 2>/dev/null; for f in /sys/module/mv_pport/parameters/*; do echo \"\$f = \$(cat \$f 2>/dev/null)\"; done"
run "ls -la /sys/module/usfp_firewall/parameters/ 2>/dev/null; for f in /sys/module/usfp_firewall/parameters/*; do echo \"\$f = \$(cat \$f 2>/dev/null)\"; done"
# any register or queue dump the vendor driver exposes
run "find /proc /sys/kernel/debug -maxdepth 3 -iname '*octeon*' -o -maxdepth 3 -iname '*oct*' 2>/dev/null | head -30"
run "ls -laR /sys/kernel/debug/octeon* 2>/dev/null | head -60"
run "cat /proc/octeon_device* 2>/dev/null | head -80"

say "2c. INTERRUPTS, BEFORE AND AFTER - does the SDP path raise any"
run "cat /proc/interrupts"
run "cat /proc/softirqs 2>/dev/null | head -20"

say "2d. THE HOST NIC'S OWN COUNTERS"
run "ethtool -S oct0 2>&1 | head -80"
run "ethtool -i oct0; ethtool -g oct0 2>&1; ethtool -l oct0 2>&1"
run "ip -s -d link show oct0"
run "ip -s -d link show pport_l254; ip -s -d link show pport_l0; ip -s -d link show mux_dev0"

say "2e. THE FAST PATH AS IT WAS ACTUALLY STARTED"
npu "ps | head -40"
npu "for p in /proc/[0-9]*; do c=\$(tr '\\0' ' ' < \$p/cmdline 2>/dev/null); case \$c in *usfp*) echo \"\$p: \$c\";; esac; done"
npu "cat /sys/module/slipf/parameters/pci_port; echo; ls /sys/module/ | head -40"

# ----------------------------------------------------------------- 3. THE COUNTERS, IN FULL
# Every previous capture truncated these with `head`, so whole counter families have never been
# seen. `D` adds the vendor's own description of each counter and has never been used. `I` includes
# zero entries, which is what shows an array's real shape. NEVER pass `C`; it clears.
say "3. THE COUNTER TABLES, COMPLETE, WITH DESCRIPTIONS"
for t in worker_sys_cnt worker_port_cnt worker_dbg_cnt worker_dragonfly_cnt; do
    npu "usfp_table_print.sh $t D"
    npu "usfp_table_print.sh $t I"
done
npu "usfp_table_print.sh platform_info"

say "4. THE STATE TABLES, COMPLETE"
for t in lif nhop conn mflow luid sa; do
    npu "usfp_table_print.sh $t I"
done
# the fast path prints a pport_tag to iface_id table somewhere; find the trigger
npu "usfp_table_print.sh 2>&1 | head -40"

# ----------------------------------------------------------------- 5. THE MAPPINGS NOBODY DUMPED
say "5. PORT AND INTERFACE MAPPINGS"
run "xgs-ports -b"
run "xgs-ports -f"
run "xgs-ports -d"
run "xgs-ports -g"
run "ls -la /sys/kernel/usfp_firewall/control/ 2>/dev/null; for f in /sys/kernel/usfp_firewall/control/*; do echo \"\$f = \$(cat \$f 2>/dev/null)\"; done"
run "ls -laR /sys/kernel/nwa_ports_info/ 2>/dev/null; for f in /sys/kernel/nwa_ports_info/*; do echo \"\$f = \$(cat \$f 2>/dev/null)\"; done"
run "ls -laR /sys/kernel/nwa_pports/ 2>/dev/null; for f in /sys/kernel/nwa_pports/*; do echo \"\$f = \$(cat \$f 2>/dev/null)\"; done"
run "for d in /sys/class/net/*/; do n=\$(basename \$d); echo \"\$n: \$(cat \$d/address 2>/dev/null) ifindex=\$(cat \$d/ifindex 2>/dev/null)\"; done"

# ----------------------------------------------------------------- 6. THE PERIPHERAL CHANNEL
# The CPLD's bus is named as spi:0:1:3 in the key store, in the coprocessor's namespace. It carries
# the sensors, the SFP cage pins and the fail-to-wire relay. Everything needed to drive it is here.
say "6a. THE CPLD, SWEPT FURTHER THAN BEFORE"
run "for r in 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10 11 12 13 14 15 16 17 18 19 1a 1b 1c 1d 1e 1f; do printf '0x%s = ' \$r; xgs-cpld 0x\$r 2>&1; done"
run "for r in 20 21 22 23 24 25 26 27 28 29 2a 2b 2c 2d 2e 2f 30 31 32 33 34 35 36 37 38 39 3a 3b 3c 3d 3e 3f; do printf '0x%s = ' \$r; xgs-cpld 0x\$r 2>&1; done"
run "for r in 40 41 42 43 44 45 46 47 48 49 4a 4b 4c 4d 4e 4f 50 51 52 53 54 55 56 57 58 59 5a 5b 5c 5d 5e 5f; do printf '0x%s = ' \$r; xgs-cpld 0x\$r 2>&1; done"
run "for r in 60 61 62 63 64 65 66 67 68 69 6a 6b 6c 6d 6e 6f 70 71 72 73 74 75 76 77 78 79 7a 7b 7c 7d 7e 7f; do printf '0x%s = ' \$r; xgs-cpld 0x\$r 2>&1; done"
say "6b. IS THERE AN SPI DEVICE, AND ON WHICH SIDE"
run "ls -la /dev/spidev* 2>&1; ls -la /sys/bus/spi/devices/ 2>&1; ls -la /sys/class/spi_master/ 2>&1"
npu "ls -la /dev/spidev* 2>&1; ls -la /sys/bus/spi/devices/ 2>&1; ls -la /sys/class/spi_master/ 2>&1"
npu "cat /proc/iomem | head -60; echo ---; cat /proc/interrupts"
say "6c. SENSORS, AND WHERE THEY COME FROM"
run "xgs-1us-sensors -a"
run "xgs-dt-sensors 2>&1 | head -20; xgs-phy-temperature 2>&1 | head -20"
run "xgs-nct 2>&1 | head -5"
run "sensors 2>&1 | head -40; ls /sys/class/hwmon/ 2>&1; ls /sys/class/thermal/ 2>&1"
say "6d. THE SFP CAGES AND THE EVENT REGISTER"
run "for p in F1 F2 F3 F4; do echo \"-- Port\$p\"; xgs-sff-info Port\$p 2>&1 | head -12; done"
npu "xgs-sff-event 2>&1 | head -20; xgs-sff-mq 2>&1 | head -20"
run "for p in F1 F2 F3 F4; do echo \"-- Port\$p\"; ethtool -m Port\$p 2>&1 | head -20; done"

say "6e. THE SWITCH AND THE PHYS, EVERY PORT"
npu "for d in 0 1 2 3 4 5 6 7 8 9 10; do for r in 0 1 2 3 4 21 22 26 27; do printf \"dev%s.%s = \" \$d \$r; xgs-mdio -a 0 2 \$d.\$r 2>&1; done; done"
npu "for l in 0 2; do for reg in 1.0 1.1 1.2 1.3 1.8 1.9 1.a 3.0 3.1 4.0 7.0 7.1; do printf \"5113 slice%s %s = \" \$l \$reg; xgs-mdio 0 7 \$reg 2>&1; done; done"
npu "xgs-mdio 2>&1 | head -30"
npu "ls /sys/class/gpio/ 2>&1; cat /sys/class/gpio/gpiochip*/label 2>&1; cat /sys/class/gpio/gpiochip*/base 2>&1"

# ----------------------------------------------------------------- 7. STRUCTURE THAT WAS TRUNCATED BEFORE
say "7. FULL STRUCTURE, NOT TRUNCATED"
run "lspci -vvv -s 01:00.0"
run "lspci -vvv | head -400"
run "lsmod"
npu "lsmod"
run "dmesg | tail -400"
npu "dmesg | tail -400"
run "cat /proc/iomem"
run "cat /proc/meminfo | head -30; cat /proc/cmdline"
run "xgs-platform | sort"

# ----------------------------------------------------------------- 8. THE KEY STORE AND THE TOOLS
say "8. THE PLATFORM KEY STORE AND THE TOOL SURFACE"
run "ls -la /usr/bin/xgs-* /sbin/xgs-* 2>/dev/null"
npu "ls -la /usr/bin/xgs-* /sbin/xgs-* 2>/dev/null"
for t in xgs-cpld xgs-mdio xgs-sff-info xgs-ports xgs-eeprom xgs-reg xgs-1us-sensors xgs-ftw xgs-led; do
    run "$t -h 2>&1 | head -20"
done

# ----------------------------------------------------------------- 9. OPTIONAL: traffic, and it is the point of 2a
# This is the only part that changes anything, and it undoes itself. Set RUN_TRAFFIC=1 to run it.
# Run section 2a's tcpdump in another shell at the same time, or this will have nothing to show.
if [ "$RUN_TRAFFIC" = "1" ]; then
    say "9. A KNOWN LOAD, WITH COUNTERS EITHER SIDE"
    npu "usfp_table_print.sh worker_sys_cnt I"
    npu "usfp_table_print.sh worker_port_cnt I"
    run "ip addr add 10.0.0.1/30 dev PortF1"
    run "ip neigh add 10.0.0.2 lladdr \$(cat /sys/class/net/PortF2/address) dev PortF1"
    run "ping -q -c 3000 -i 0.002 -s 1400 10.0.0.2 2>&1 | tail -5"
    npu "usfp_table_print.sh worker_sys_cnt I"
    npu "usfp_table_print.sh worker_port_cnt I"
    run "ip neigh del 10.0.0.2 dev PortF1"
    run "ip addr del 10.0.0.1/30 dev PortF1"
    run "cat /proc/interrupts"
    run "ethtool -S oct0 2>&1 | head -80"
fi

echo
echo "##### done. Nothing was written. #####"
