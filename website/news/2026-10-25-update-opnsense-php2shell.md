---
date: 2026-10-25
slug: update-opnsense-php2shell
title: "Update OPNsense now - PHP2Shell, test appliances included"
summary: "Two critical PHP flaws can let code run on a firewall without a login, and OPNsense's web interface runs on PHP. The fix, PHP 8.5.12, comes to OPNsense 26.7.7 as a package update, so the version does not change. Update every appliance, a test one too, and update right after installing a ready-made image. Since 2026-10-30 OPNsense 26.7.8 carries the fix, and the version reads 26.7.8 after it."
---

# Update OPNsense now - PHP2Shell, test appliances included

![Update OPNsense now - PHP2Shell: update to PHP 8.5.12 or later, test appliances included](../../docs/images/readme/notice-light.svg#only-light)
![Update OPNsense now - PHP2Shell: update to PHP 8.5.12 or later, test appliances included](../../docs/images/readme/notice-dark.svg#only-dark)

On 2026-10-21 PHP published an emergency release fixing two critical flaws, **CVE-2026-109618** and
**CVE-2026-109619** - "PHP2Shell". [php.net](https://www.php.net/archive/2026.php) says both can allow
remote code execution without a login, and that updating is the only complete fix. OPNsense's web
interface runs on PHP.

<!-- more -->

OPNsense delivered the fix the same evening, **PHP 8.5.12 for 26.7.7**, as an out-of-band package
update ([its announcement](https://forum.opnsense.org/index.php?topic=53191.0)). Because it is a package
update, **the version still reads 26.7.7 afterwards** - the number does not say whether the fix is in.
On 2026-10-25 one of this project's own test appliances still ran PHP 8.5.11 under "OPNsense 26.7.7".

> **Since 2026-10-30:** OPNsense 26.7.8 carries the fix as part of its own update - base, kernel and
> packages, with a reboot - so the version reads **26.7.8** after it, or 26.7.8_1 with the hotfix
> that followed. `php -v` reads 8.5.12 or later either way.

## What to do, on every appliance - a test one too

1. **System: Firmware: Status** - *Check for updates*, then *Update*.
2. **Check it**: in a shell (the console's option 8, or SSH), `php -v` reads **8.5.12** or later.
3. **If you run the captive portal**, restart it after the update, as OPNsense advises.

A test appliance is no exception: on OPNsense, PHP answers the requests of the web interface - and of
the captive portal where it runs - and a test appliance's web interface is usually the one left open
to a whole lab network.

## The ready-made images

The [ready-made images](https://github.com/AbdelmonemAwad/xgs-image-builder/releases) start from
OPNsense 26.7's own image, as published by OPNsense, and carry the PHP that image has - older than
the fix. **Update right after installing**, before the web interface can be reached from any network
you do not trust; [the step-by-step guide](../../docs/guide/step-by-step.md) now says so at the start of
its quickest way and in its last step.
