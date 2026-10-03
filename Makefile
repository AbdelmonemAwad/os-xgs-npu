#-
# SPDX-License-Identifier: BSD-2-Clause
#

PLUGIN_NAME=		npuctl
PLUGIN_VERSION=		0.1
PLUGIN_COMMENT=		Drive the Marvell coprocessor on Sophos XGS appliances
PLUGIN_MAINTAINER=	eg2@live.com

# No PLUGIN_DEPENDS. Everything the installed half needs is in the base system: python3 for the
# bridge tool and hidraw(4) for it to talk through. Nothing is installed on the owner's behalf.
#
# THE KERNEL MODULE IS NOT PACKAGED, and that is a decision rather than an omission.
#
# contrib/npuep is C against FreeBSD kernel headers. It cannot be built on a machine without
# kernel sources, it cannot be built at package time on a build host whose kernel differs from
# the target's, and - see docs/porting-notes.md - it cannot usefully be preloaded from
# loader.conf either, because the BAR restore this hardware needs only happens on the kldload
# path. So it ships as source, is built on the appliance, and is loaded by hand.
#
# What the package does install is the reset hook, which is the half that is safe unattended:
# it touches a USB bridge, exits 0 on hardware it does not recognise, and cannot affect the
# host if it fails. That was not true everywhere until it was checked: the XGS 3300 has an MCP2210
# too, wired to its coprocessor's boot flash, and the hook's only guard was whether the bridge
# existed. It now reads the board's assembly number first (src/opnsense/scripts/xgs/board.sh) and
# exits on anything but an ARMADA board this project has been run on, before it loads or opens
# anything; install/install.sh no longer puts it on any other.
#
# What it DOES do, as of the boot hook pair, is install a module into /boot/modules and load it at
# boot, because a firewall that comes up without its front ports because somebody forgot a kldload
# is a firewall that is down. npuep is still built by hand. octep is built by install/install.sh,
# through the same kernel follower that rebuilds it after every kernel update.

.include "../../Mk/plugins.mk"
