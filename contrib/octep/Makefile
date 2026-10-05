#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Built on the appliance, against the kernel sources that match the running kernel. install.sh does
# it through install/kernel-follow.sh, in an emptied copy of these files. By hand, in a checkout:
#
#	sh contrib/npuep/fetch-sources.sh
#	SYSDIR=/usr/src-26.7-<sha>/sys sh contrib/octep/build.sh
#
# build.sh rather than a bare make. It cleans with SYSDIR first and stamps the module with the kernel
# it was built for; a bare `make` leaves an unstamped octep.ko, and a bare `make clean` stops before
# cleaning anything, because the default SYSDIR below does not exist on an OPNsense appliance.
#
# The default below is where FreeBSD would put sources, but OPNsense ships none and the tree that
# matches an OPNsense kernel is OPNsense's own - see contrib/npuep/fetch-sources.sh, which pins it to
# the running kernel's own commit and explains why building against stock FreeBSD headers is luck
# rather than correctness. The fetch script is shared: there is nothing driver-specific in it.
#
# Not packaged, for the same reasons as npuep.
#
SYSDIR?=	/usr/src/sys

KMOD	=	octep
SRCS	=	octep.c octep_mgmt.c octep_sdp.c octep_dp.c octep_nwa.c octep_rpc.c octep_pf.c octep_nhop.c device_if.h bus_if.h pci_if.h opt_inet.h

.include <bsd.kmod.mk>
