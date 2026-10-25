<!-- reviewed: 2026-10-19 -->
# Updating an appliance, step by step

On 2026-10-19 all three appliances on the bench were updated to
[v0.1.0-alpha.2](https://github.com/AbdelmonemAwad/os-xgs-npu/releases/tag/v0.1.0-alpha.2) from
their own **XGS: Updates** page, in the browser, and photographed along the way. The XGS 136 and
the first XGS 3300 were offered the release as usual. The second XGS 3300 at first was not, for a
reason this page shows. Each update, from **Update** to done, took 16 or 17 seconds; the driver is
built on the appliance itself.

The page does for this plugin what OPNsense's own **System: Firmware** does for OPNsense, which does not
list the plugin. Nothing on it reboots by itself.

## 1. What is installed, and what is offered

**Status** shows three cards: what is installed, the newest release, and the last update. Below them
are the channel this appliance follows (**Releases**, or **Test** for every change merged) and the
two buttons.

![The XGS 136: installed a commit from main, v0.1.0-alpha.2 offered](../images/screens/update-2026-10-19/01-xgs136-update-available.png)

![The first XGS 3300, the same release offered](../images/screens/update-2026-10-19/07-xgs3300-old-update-available.png)

## 2. What changed

**What changed** lists every change the offered release brings, each with its pull request.

![What changed, as it was: the pull requests printed as Markdown](../images/screens/update-2026-10-19/02-xgs136-what-changed.png)

The pull requests were printed as their Markdown, brackets and address included; they are drawn as
links now:

![What changed, the pull requests as links](../images/screens/update-2026-10-19/18-xgs3300-new-what-changed-linked.png)

## 3. Update, and what it says first

**Update** asks once, and says what is about to happen: the version shown is downloaded, its
installer runs, and the driver is built for the running kernel. The driver that is running now
stays until the next boot, so the firewall goes on forwarding.

![The confirmation](../images/screens/update-2026-10-19/03-xgs136-update-asks.png)

## 4. The progress bar

Six steps, each lit as it starts and ticked as it ends:

- **Download** - the release, by the commit the check found;
- **Unpack**;
- **Install** - the plugin, its pages and its boot hooks;
- **Kernel sources** - the sources of the kernel this appliance runs;
- **Build the driver** - counted in source files, 9 of 9;
- **Finish**.

On the first XGS 3300, about once a second:

![2 %: downloading the release](../images/screens/update-2026-10-19/08-xgs3300-old-progress-1-download.png)

![18 %: installing the plugin, its pages and its boot hooks](../images/screens/update-2026-10-19/09-xgs3300-old-progress-2-install.png)

![24 %: still installing](../images/screens/update-2026-10-19/10-xgs3300-old-progress-3-install.png)

![73 %: building the driver, 6 of 9 source files compiled](../images/screens/update-2026-10-19/11-xgs3300-old-progress-4-build.png)

![90 %: 9 of 9 compiled](../images/screens/update-2026-10-19/12-xgs3300-old-progress-5-build-done.png)

And on the XGS 136 and the second XGS 3300:

![The XGS 136 at 33 %, installing](../images/screens/update-2026-10-19/04-xgs136-progress-install.png)

![The second XGS 3300 starting](../images/screens/update-2026-10-19/19-xgs3300-new-progress-1.png)

![The second XGS 3300 installing](../images/screens/update-2026-10-19/20-xgs3300-new-progress-2.png)

![The second XGS 3300 at the kernel sources](../images/screens/update-2026-10-19/21-xgs3300-new-progress-3.png)

![The second XGS 3300 building its driver, 90 %](../images/screens/update-2026-10-19/22-xgs3300-new-progress-4-build.png)

![The second XGS 3300 finishing](../images/screens/update-2026-10-19/23-xgs3300-new-progress-5.png)

## 5. Done - and a reboot when the driver or a boot step changed

When it is done the bar is full and every step ticked. A reboot loads the driver just built, and the
page offers it with **Reboot now**.

![The first XGS 3300: installed, waiting for a reboot to load its new driver](../images/screens/update-2026-10-19/13-xgs3300-old-installed-waits-for-reboot.png)

The XGS 136 was asked for a reboot too, although the driver its update built was byte for byte the one
it was running (npuep 9619e07c, built by hand earlier the same day from the same source). A reboot
would have loaded the same driver again:

![The XGS 136 asked for a reboot it did not need](../images/screens/update-2026-10-19/05-xgs136-installed-waits-for-reboot.png)

Now the update compares the driver it built with the one the boot loaded, and says when nothing waits:

![The second XGS 3300: installed, the driver it built is the one running](../images/screens/update-2026-10-19/24-xgs3300-new-installed-no-reboot-needed.png)

Since 0.1.0-alpha.8.1 the boot steps count too. The hooks this project installs under
`/usr/local/etc/rc.syshook.d/early` and `start` run once a boot, so a change in one takes effect at the
next boot, as a new driver does: the update compares them with what the boot ran, and asks for a
reboot when one changed even with the driver unchanged - the page then says the driver is the one
running but its boot steps changed. 0.1.0-alpha.8 asked the lab's three boards for no reboot, their
driver being the release's already, while its boot steps had changed; updating from 0.1.0-alpha.8
asks for that reboot once.

## 6. The reboot

**Reboot now** asks once, and says what it costs: traffic stops until the appliance is back, a few
minutes.

![The reboot asks](../images/screens/update-2026-10-19/14-xgs3300-old-reboot-asks.png)

![Rebooting: the page says to reload it once the firewall is back](../images/screens/update-2026-10-19/15-xgs3300-old-rebooting.png)

The first XGS 3300 was back at 18:36:09 UTC with the new driver (octep 3315c08f) loaded and its
links up. The reboot also ended the browser's session, so the page after it asked for a login.

## 7. The installer's log

**Installer log** keeps everything the installer printed: what was downloaded, each part installed,
and the driver's build.

![The installer log](../images/screens/update-2026-10-19/06-xgs136-installer-log.png)

## A build from a branch

The second XGS 3300 had last been installed from a test branch. That build is not in the history of
the releases: the branch's change was merged as a single commit, which is a different commit from
the test build. Compared with the release, the build is neither ahead nor behind; GitHub calls this
*diverged*. The page treated it as up to date and turned **Update** off:

![The second XGS 3300 shown as up to date, the release ahead of it](../images/screens/update-2026-10-19/16-xgs3300-new-diverged-shown-as-up-to-date.png)

A build like that is now offered the release, with the reason beside it:

![The release offered, and why](../images/screens/update-2026-10-19/17-xgs3300-new-diverged-offered.png)

But not when the build is dated after the release: then the release would take it back. On
2026-10-22 the XGS 136, running a build of the branch that became v0.1.0-alpha.5.4, was given
v0.1.0-alpha.5.3 that way ([#677](https://github.com/AbdelmonemAwad/os-xgs-npu/issues/677)). Since
2026-10-24 such a build is shown as newer than the newest release, as one from main is, and the next
release is offered. And **Update** checks again before it downloads anything: on that same morning
it had installed what an earlier check found, after the check made since had failed. Now a check
that fails stops the update with the check's own reason, and nothing is downloaded.

## The appliance plugins

The **Plugins** tab updates the appliance plugins - Arabic, right-to-left, fan control, the front
panel, link health and the network report, those installed - each with its own updater, by hand.
**Check for updates** asks for each one's latest release; a plugin with one ahead of it gets its
**Update**, and **Update all** takes them all, one after the other. Each update has its own bar and
steps - queued, ask GitHub, download, unpack, install, updated - with its clock and its installer's
last line, and **Update all** a bar for the whole; a plugin's installer restarts the web GUI, and the
page says so and carries on by itself. When an update ends its row shows the release now installed,
by name, and offers it no more:

![The Plugins tab during Update all: fan control updated to v1.1.1 in six seconds, the network report on its second step](../images/screens/en/07b-xgs-updates-plugins.png)

**With the project's update, since 0.1.0-alpha.8.2.** After installing itself, an update of the
project brings the plugins along: every installed one to its latest release, and every one of the
family the appliance never had and that suits it installed - the front panel only on the OCTEON TX
family's rack models, which have one. A plugin its owner took away is not brought back: the
appliance keeps a list of the plugins it has had. The installer's log says what became of each,
and the reboot is offered only once they are done. A file `/var/db/os-xgs-npu/plugins-auto` holding
`off` stops it; the Plugins tab works as before.
