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
    uname -v > octep.ko.kernel
    echo "   built against: $(cat octep.ko.kernel)"
else
    echo "BUILD FAILED"
    grep -E 'error:|warning:' /tmp/octep-build.log | head -30
    tail -5 /tmp/octep-build.log
    exit 1
fi
