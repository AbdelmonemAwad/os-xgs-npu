# The project

**os-xgs-npu** is one person's project, worked on in the open: two FreeBSD kernel modules, `npuep`
for the ARMADA coprocessor and `octep` for the OCTEON TX, and the OPNsense plugin around them. The
plugins beside it are each in a repository of their own, and each installs on its own.

<div class="grid cards" markdown>

-   :material-chip: **[os-xgs-npu](https://github.com/AbdelmonemAwad/os-xgs-npu)**

    ---

    The drivers and the plugin: the front ports, flow and IPsec offload, the XGS section of the
    menu, updates and problem reports.

-   :material-package-variant: **[xgs-image-builder](https://github.com/AbdelmonemAwad/xgs-image-builder)**

    ---

    An OPNsense image for a Sophos XGS with all of it inside: ready to download with each release,
    or built on GitHub from [a form](https://build.npuforge.org/).

-   :material-fan: **[os-fanctl](https://github.com/AbdelmonemAwad/os-fanctl)**

    ---

    Fan control from the temperatures the kernel already publishes, every fan handed back to the
    hardware if anything goes wrong.

-   :material-monitor-dashboard: **[os-frontpanel](https://github.com/AbdelmonemAwad/os-frontpanel)**

    ---

    The firewall's own information on the LCD in the front of the appliance, and the keys beside it
    given something to do.

-   :material-heart-pulse: **[os-linkhealth](https://github.com/AbdelmonemAwad/os-linkhealth)**

    ---

    Every physical port watched, and the cable or transceiver going bad named the way the chassis
    names it.

-   :material-email-newsletter: **[os-netreport](https://github.com/AbdelmonemAwad/os-netreport)**

    ---

    A readable summary of the network by mail on a schedule, in Arabic or English.

-   :material-palette: **[os-theme-xgs](https://github.com/AbdelmonemAwad/os-theme-xgs)**

    ---

    XGS Silicon, a theme for the web GUI in light, dark and one that follows the browser.

-   :material-translate: **[opnsense-arabic](https://github.com/AbdelmonemAwad/opnsense-arabic)** and **[opnsense-rtl](https://github.com/AbdelmonemAwad/opnsense-rtl)**

    ---

    A complete Arabic translation of the OPNsense web interface, and the page laid out right to
    left to read it in.

-   :material-timer-refresh: **[os-watchdog](https://github.com/AbdelmonemAwad/os-watchdog)**

    ---

    The host's hardware watchdog armed, so a host that stops is reset by its own hardware, and the
    chipset read for why it last reset. It stands aside where os-xgs-npu arms the watchdog.

-   :material-cog-sync: **[os-kernelfollow](https://github.com/AbdelmonemAwad/os-kernelfollow)**

    ---

    Every out-of-tree kernel module registered with it rebuilt for the kernel the next boot takes,
    before that boot.

-   :material-playlist-remove: **[os-blocklists](https://github.com/AbdelmonemAwad/os-blocklists)**

    ---

    Address, DNS, country and JA4 block lists in one page; on a Sophos XGS a connection it cuts is
    taken out of the coprocessor too.

-   :material-lan-disconnect: **[os-portguard](https://github.com/AbdelmonemAwad/os-portguard)**

    ---

    Devices blocked at the firewall by their MAC address, and the devices it has seen in the last
    day listed beside them.

-   :material-shield-key: **[os-fallbackcrypto](https://github.com/AbdelmonemAwad/os-fallbackcrypto)**

    ---

    Which of the kernel's crypto drivers encrypts the IPsec tunnels the host carries itself:
    AES-NI, OpenSSL's, or both.

</div>

## How it is run

- **Every change is a pull request**, and the checks - syntax, nothing private, the pages' links -
  run on each. The drivers are built on the appliance by its own compiler, with every warning
  fatal: no runner on GitHub has a coprocessor.
- **Every figure is measured and photographed**, on a page that says how it was taken and on which
  build of the module. A claim found wrong is struck through where it stands, not deleted.
- **Releases are small and frequent**: the [changelog](../../../CHANGELOG.md) is written from the commits,
  and an appliance updates from its own Updates page.
- **Problems are reported by the appliances themselves**, when their owners allow it, without
  addresses or serials - [what is sent](../../../docs/problem-reports.md).

The pages in this section are the repository's own: [contributing](../../../CONTRIBUTING.md),
[the design](../../../DESIGN.md), [releasing](../../../RELEASING.md), [security](../../../SECURITY.md) and the
[code of conduct](../../../CODE_OF_CONDUCT.md).
