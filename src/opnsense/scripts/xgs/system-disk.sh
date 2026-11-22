#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
# Which disks the running system's root filesystem lies on.
#
#	sh system-disk.sh        the disks, one a line: mmcsd0, da0, nda0 ...
#	sh system-disk.sh -e     exit 0 where one of them is an eMMC (mmcsd), 1 where none is, and 2
#	                         where the root filesystem cannot be followed down to a disk
#
# WHY IT MATTERS. On the XGS 87 the eMMC is reached through fchemmc, a module of this plugin's own
# that the loader loads (contrib/fchemmc). Where the system disk is that eMMC, a kernel booted
# without a matching fchemmc has no root disk at all and the appliance does not boot, so the kernel
# follower treats fchemmc there as the boot needs it (install/kernel-follow.sh, "the eMMC as the
# system disk"). Everywhere else nothing changes, so the question has to be answered positively.
#
# HOW. The root filesystem's device is read from `mount -p`. OPNsense's installer mounts it by a
# label, not by the disk: /dev/gpt/rootfs for UFS (opnsense-ufs.sh labels partition 3 "rootfs" and
# writes that into fstab), and a pool for ZFS (zroot/ROOT/default), whose devices `zpool list -vHP`
# names - partitions or gpt/ labels again. Each name is then followed down GEOM's own tree,
# kern.geom.conftxt, where every provider is printed after the one it is built on and one level
# deeper: a label on a partition on a disk, an encrypted provider on a partition, a mirror on its
# disks. The disk is the provider at level 0 above it. A name that does not lie on a disk - a memory
# disk's, a network filesystem's - gives none. Every provider of that name counts: a system left on a
# USB disk carries the same gpt/rootfs label as one installed on the eMMC, and answering from the first
# would turn the eMMC's protection off.
#
# MOUNTS, CONFTXT and ZPOOLS name files that stand in for `mount -p`, kern.geom.conftxt and
# `zpool list -vHP <pool>`, for testing this away from an appliance.

set -u

mounts() {
	if [ -n "${MOUNTS:-}" ]; then
		/bin/cat "${MOUNTS}"
	else
		/sbin/mount -p
	fi
}
conftxt() {
	if [ -n "${CONFTXT:-}" ]; then
		/bin/cat "${CONFTXT}"
	else
		/sbin/sysctl -n kern.geom.conftxt
	fi
}
pool_devices() {
	if [ -n "${ZPOOLS:-}" ]; then
		/bin/cat "${ZPOOLS}"
	else
		/sbin/zpool list -vHP "$1"
	fi | /usr/bin/tr '\t ' '\n\n' | /usr/bin/sed -n 's|^/dev/||p'
}

# The root filesystem: its device and its type, from the line whose mount point is /.
ROOTLINE=$(mounts 2>/dev/null | /usr/bin/awk '$2 == "/" { line = $1 " " $3 } END { print line }')
ROOTDEV=${ROOTLINE%% *}
ROOTFS=${ROOTLINE##* }

if [ -z "${ROOTDEV}" ]; then
	DEVICES=
elif [ "${ROOTFS}" = zfs ]; then
	DEVICES=$(pool_devices "${ROOTDEV%%/*}" 2>/dev/null)
else
	DEVICES=${ROOTDEV#/dev/}
fi

# Every disk under those devices, each once.
TREE=$(conftxt 2>/dev/null)
DISKS=$(for d in ${DEVICES}; do
	printf '%s\n' "${TREE}" | /usr/bin/awk -v want="${d}" '
		NF >= 3 && $1 ~ /^[0-9]+$/ { above[$1] = $3; if ($3 == want) print above[0] }'
done | /usr/bin/awk 'NF && !seen[$0]++')

if [ "${1:-}" = "-e" ]; then
	[ -n "${DISKS}" ] || exit 2
	printf '%s\n' "${DISKS}" | /usr/bin/grep -Eq '^mmcsd[0-9]+$' && exit 0
	exit 1
fi
[ -n "${DISKS}" ] && printf '%s\n' "${DISKS}"
exit 0
