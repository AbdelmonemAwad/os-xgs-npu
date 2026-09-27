#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Built on the appliance, against the kernel sources that match the running kernel:
#
#	sh contrib/npuep/fetch-sources.sh
#	make -C contrib/octep SYSDIR=/usr/src-26.7-<sha>/sys
#
# The default below is where FreeBSD would put sources, but OPNsense ships none and the tree that
# matches an OPNsense kernel is OPNsense's own - see contrib/npuep/fetch-sources.sh, which pins it to
# the running kernel's own commit and explains why building against stock FreeBSD headers is luck
# rather than correctness. The fetch script is shared: there is nothing driver-specific in it.
#
# Not built by the installer and not packaged, for the same reasons as npuep.
#
SYSDIR?=	/usr/src/sys

KMOD	=	octep
SRCS	=	octep.c octep_mgmt.c octep_sdp.c octep_dp.c device_if.h bus_if.h pci_if.h opt_inet.h

.include <bsd.kmod.mk>
