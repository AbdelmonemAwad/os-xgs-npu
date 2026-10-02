#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Build octep in a checkout, on the appliance. Run from anywhere:  sh contrib/octep/build.sh
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

# A real clean, with SYSDIR, before every build. `rm -f *.o *.ko` was all this did before, and that
# leaves the machine, x86 and i386 include links and the generated *_if.h headers behind: kmod.mk
# creates those links only when they are missing and never re-points one, so a build against a new
# kernel compiled the new sys/ headers against the OLD kernel's machine headers and was stamped for
# the new one. `make clean` without SYSDIR does not help either - the Makefile then defaults to
# /usr/src/sys, which an OPNsense appliance does not have, and bsd.sysdir.mk stops before cleaning.
# install/kernel-follow.sh avoids the whole question by building in an emptied copy; this script
# builds in place, for a checkout, so it has to clean for real and say so when it cannot.
if ! make SYSDIR="${SYSDIR}" clean > /tmp/octep-build.log 2>&1; then
    echo "CLEAN FAILED - refusing to build on top of a previous build"
    tail -5 /tmp/octep-build.log
    exit 1
fi
rm -f *.o *.ko octep.ko.kernel
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
