#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Build octep on the appliance. Run from anywhere:  sh /root/npu/octep/build.sh
#
# Separate from the Makefile so it can be invoked over ssh without three layers of quoting between
# the shell that sends it, sh and make eating the redirections.
set -e
cd "$(dirname "$0")"

# Which kernel sources to build against. The default is where FreeBSD puts them; the tree that
# matches an OPNsense kernel is OPNsense's own - see contrib/npuep/fetch-sources.sh, which fetches it
# pinned to the running kernel's own commit and prints the SYSDIR to use.
: ${SYSDIR:=/usr/src/sys}

# Which kernel to STAMP the result with. The default is the running one, which is right whenever
# the running kernel is the one being built against. It is given explicitly when it is not - see
# install/kernel-follow.sh, which builds for the kernel the next boot will use.
: ${KVER:=$(uname -v)}

rm -f *.o *.ko
if make SYSDIR="${SYSDIR}" > /tmp/octep-build.log 2>&1; then
    echo "BUILD OK"
    ls -l octep.ko
    # Record the kernel this was built against, beside the module.
    #
    # A FreeBSD module declares its kernel dependency as a RANGE - from the __FreeBSD_version it was
    # compiled with up to the end of that branch - so within 15.x kldload will happily load a module
    # built against a different 15.x kernel rather than refusing it. The loud failure one might
    # expect on a kernel update does not arrive; what happens instead is that it loads and any
    # structure that moved is read at the wrong offset.
    #
    # So the mismatch has to be detected out of band, by writing down what we built against and
    # comparing strings later.
    #
    # KVER exists for the pre-reboot case. install/kernel-follow.sh builds against the kernel on
    # disk while a different one is still running, so stamping with `uname -v` there would record
    # the kernel this module is NOT for - and the stamp is the only thing standing between a
    # silent mismatched load and a firewall whose structures are read at the wrong offsets.
    #
    printf '%s\n' "${KVER:-$(uname -v)}" > octep.ko.kernel
    echo "   built against: $(cat octep.ko.kernel)"
else
    echo "BUILD FAILED"
    grep -E 'error:|warning:' /tmp/octep-build.log | head -30
    tail -5 /tmp/octep-build.log
    exit 1
fi
