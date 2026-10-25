---
date: 2026-10-25
slug: five-new-plugins
title: "Five new plugins of the family"
summary: "os-watchdog, os-kernelfollow, os-blocklists, os-portguard and os-fallbackcrypto, each released today at 1.0.0 in a repository of its own. On an XGS appliance they sit in the XGS menu and are updated from XGS: Updates; elsewhere each sits in its own section of the menu."
---

# Five new plugins of the family

Five plugins were released today, each at **1.0.0**, each in a repository of its own and each
installable on its own: a hardware watchdog, a kernel follower, block lists, a port guard and a
choice of crypto driver for IPsec.

<!-- more -->

- **[os-watchdog](https://github.com/AbdelmonemAwad/os-watchdog)**, Hardware Watchdog - arms the
  host's hardware watchdog, so a host that stops is reset by its own hardware, and reads from the
  chipset why the host last reset. Where os-xgs-npu already arms the watchdog, it stands aside.
- **[os-kernelfollow](https://github.com/AbdelmonemAwad/os-kernelfollow)**, Kernel Follower -
  rebuilds the out-of-tree kernel modules registered with it for the kernel the next boot takes,
  and holds an orderly reboot until they are built, ten minutes at most.
- **[os-blocklists](https://github.com/AbdelmonemAwad/os-blocklists)**, Block Lists - address, DNS,
  country and JA4 block lists in one page; the DNS lists stay OPNsense's own, in Unbound, shown
  beside the others. On a Sophos XGS a connection it cuts is taken out of the coprocessor too.
- **[os-portguard](https://github.com/AbdelmonemAwad/os-portguard)**, Port Guard - blocks devices by
  their MAC address at the firewall, and lists the devices it has seen in the last day. Two devices
  behind the same switch still reach each other: their frames never come to the firewall.
- **[os-fallbackcrypto](https://github.com/AbdelmonemAwad/os-fallbackcrypto)**, Fallback Crypto -
  chooses which of the kernel's crypto drivers encrypts the IPsec tunnels the host carries itself:
  AES-NI, OpenSSL's, or both. The tunnels a coprocessor has taken are not touched; whether OpenSSL's
  driver is the faster one on these hosts is not measured yet.

## Where they sit

**On an XGS appliance**, with os-xgs-npu installed, all five sit in the **XGS** section of the
menu - XGS: Hardware Watchdog, Kernel Follower, Block Lists, Port Guard, Fallback Crypto - and are
updated from **XGS: Updates**, its *Plugins* tab, where an os-xgs-npu that lists them is installed.

**Elsewhere** each sits in its own section: Hardware Watchdog and Kernel Follower under **System**,
Block Lists and Port Guard under **Firewall**, Fallback Crypto under **VPN: IPsec** - and each is
updated by its own self-update script.

None of them is in OPNsense's plugin repository: each installs from a checkout with
`sh install/install.sh`, or from the package its release carries.

## What was tested

Before the release each was installed on an XGS 3300 running OPNsense 26.7.7 with os-xgs-npu, its
page opened in the browser, and its place in the menu checked both ways. What has not been tested
yet - an install on a plain OPNsense without os-xgs-npu, the packages, an update taken through the
Plugins tab, and for several the work itself: a list applied, a device blocked, a tunnel on
OpenSSL's driver, a real kernel update followed - is in each one's README, under *What is tested*.
[The project](../../website/pages/project/index.md) lists them with the rest of the family.
