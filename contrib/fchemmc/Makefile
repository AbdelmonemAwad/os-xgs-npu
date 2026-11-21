#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#

# The eMMC controller of the XGS 87 (AMD FCH, PCI 1022:7906). Built on the appliance by the kernel
# follower, against the sources of the kernel it is for, with that kernel's version line:
#
#	make SYSDIR=/usr/src-<series>-<sha>/sys \
#	    FCHEMMC_KERNEL="FreeBSD 15.1-RELEASE-p6 stable/26.7-n284176-c962f76493b6 SMP"
#
# The module probes only on a kernel whose version line is that one (fchemmc.c, fchemmc_kernel_matches),
# because the loader loads it before anything can check its stamp.
#
KMOD	= fchemmc
SRCS	= fchemmc.c fchemmc_kernel.h device_if.h bus_if.h pci_if.h mmcbr_if.h sdhci_if.h opt_mmccam.h
CLEANFILES += fchemmc_kernel.h

fchemmc_kernel.h:
	@case "${FCHEMMC_KERNEL}" in \
	"FreeBSD "*) ;; \
	*) echo "FCHEMMC_KERNEL= the kernel's version line, as uname -v prints it" >&2; exit 1 ;; \
	esac
	@case "${FCHEMMC_KERNEL}" in \
	*'"'*|*'\'*) echo "FCHEMMC_KERNEL holds a quote or a backslash" >&2; exit 1 ;; \
	esac
	printf '#define\tFCHEMMC_KERNEL\t"%s"\n' "${FCHEMMC_KERNEL}" > ${.TARGET}

.include <bsd.kmod.mk>
