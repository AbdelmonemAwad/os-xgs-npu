# The coprocessor's section of the menu

Written 2026-10-17, when the plugin's pages were gathered into one section and every setting it
has was put on its settings page.

Everything the plugin puts in the web interface is in one section of the main menu, **XGS
Coprocessor**, between Interfaces and Firewall. It replaced three entries in three places: the
settings under System: Settings (as "Hardware Offloading"), the status under Interfaces:
Diagnostics, and the updates under System: Firmware. An owner looking for one of them had to know
which errand OPNsense files it under; now they are where the hardware is.

| page | address | what it does |
|---|---|---|
| Overview | `/ui/xgs/status` | the coprocessor's state, read-only - the page that was under Interfaces: Diagnostics |
| Settings | `/ui/xgs/settings` | every setting the plugin has, below, and the port assignment's second run |
| Ports | `/ui/xgs/ports` | each front port by the name on the metal: what it is assigned as, its link, media, MTU and bridge; an expansion module's ports; what the switch chip's ports were set to at the last boot |
| IPsec | `/ui/xgs/ipsec` | whether the driver was started with IPsec in the engine, the associations the engine holds, and every counter of its IPsec subtree |
| Updates | `/ui/xgs/updates` | the updates page that was under System: Firmware |
| Log File | `/ui/diagnostics/log/core/xgs` | every tag the plugin logs under: `octep` (and `octep-follow`, `octep-stop`), `npuep`, `npuctl`, `xgs-assign`, `xgs-switch` |

The appliance plugins - link health, fan control, the front panel - are meant to put their pages in
the same section from their own packages; until they do, they stay where they are.

The privileges keep their old ids, so a user or group already granted one keeps it. The read-only
pages (Overview, Ports, IPsec) share one privilege; Settings and Updates have one each, because one
takes traffic out of the firewall's path and the other installs software and reboots.

## The settings

**Save and apply** writes each setting where it takes effect, and reports each step:

| setting | family | written to | takes effect |
|---|---|---|---|
| Hardware offloading | both | the coprocessor's gate, through `offload.sh` | at once, and again at every boot |
| Accelerate connections automatically | both | `dev.octep.0.dp.auto` / `dev.npuep.0.flow.auto` | at once, and again at every boot |
| IPsec in the coprocessor's engine | both | the loader tunable `hw.octep.ipsec_on` / `hw.npuep.ipsec_on` | at the next reboot |
| Tunnel flows forwarded by the coprocessor | XGS 3300 | the loader tunable `hw.octep.ipsec_flows`, 0 or 1 - never 2 | at the next reboot |
| Keep IPsec's software sessions off ccp | XGS 136 | `net.inet.ipsec.crypto_support` | at once, and at every boot (`07-npuep`) |
| Receive poll interval | XGS 136 | `dev.npuep.0.giu.poll_ms` and the tunable `hw.npuep.giu.poll_ms` | at once, and at every boot |
| Receive budget | XGS 136 | `dev.npuep.0.giu.rx_budget` and the tunable `hw.npuep.giu.rx_budget` | at once, and at every boot |
| Jumbo frames on the switch ports | both | read by `switch-jumbo.sh` | at the end of every boot |

A setting this appliance does not have is not shown. The tunables go through OPNsense's own Tunables
model, so they are listed under System: Settings: Tunables like any other, and `loader.conf` is
written by the function that page's apply runs. A tunable already holding the wanted value is not
written again, so a save that changes nothing writes nothing. When a setting needs a reboot the page
says so; nothing reboots on its own.

There is no restart button for the coprocessor. Nothing restarts it safely while the firewall runs:
it is reset at every boot of the appliance, and that is the way to restart it.

**Assign the ports again at the next boot** removes the mark the first-boot assignment leaves
([interface-assignment.md](interface-assignment.md)). The assignment still acts only on a box with
no front port assigned, so on a box in service the next boot marks it again and changes nothing.

### A box set up by hand keeps its settings

Before these settings were on the page, IPsec in the engine was turned on by adding the tunable by
hand. The page reads those rows when the plugin is installed (the model's 1.1.0 migration): each
field takes the value of the tunable it now writes, so the page opens showing what the box does, and
saving it unchanged changes nothing. Without that, the page would open at the defaults and the first
save would turn IPsec off in the engine at the next boot.

### Two things that were wrong, found while testing it

- **`loader.conf` missed the new rows.** The apply saved the tunable and then called
  `system_login_configure()`, which writes `loader.conf` from the legacy `$config` array that
  `config.inc` filled when the script started - the rows as they were before the save. The array is
  now reloaded after the save.
- **The Ports page showed OPNsense's names on the XGS 3300.** The driver names each port in the
  interface's description, and OPNsense replaces that description once the port is assigned. The
  bring-up now saves each name as the driver gives it (`/var/db/os-xgs-npu/labels`); until a boot has
  written that file, the page uses the order the bring-up creates the interfaces in.

## Tested

On 2026-10-17, on an XGS 3300 (with a 4x10G module) and an XGS 136, both on OPNsense 26.7.7, after
an install of the branch: the migration (`Migrated OPNsense\XGS\XGS from 1.0.0 to 1.1.0`, IPsec in
the engine read as on from each box's tunable); `configctl xgs apply` twice on each, the second
writing nothing; `loader.conf` gaining the new rows and nothing else; `configctl xgs ports` naming
all sixteen ports of the XGS 3300 (`F1` ... `A1`) and all fourteen of the XGS 136; `configctl xgs
ipsec` listing the four associations of each box's two tunnels. Not yet: the pages in a browser,
and a boot with the new bring-up and switch state file.
