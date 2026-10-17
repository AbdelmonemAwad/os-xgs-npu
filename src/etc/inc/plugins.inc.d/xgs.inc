<?php

/*
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The plugin hooks, which is how this package reaches OPNsense without editing any of its files.
 *
 * plugins_scan() picks up every file in this directory and calls the functions named after it, so
 * everything here is discovered rather than registered. That matters for an out-of-tree package:
 * nothing it does can be undone by an OPNsense update, and nothing an update does has to be
 * re-applied here.
 *
 * Only hooks with something real to do are present. There is an 'ipsec' configure hook available -
 * plugins_configure('ipsec') is fired whenever OPNsense reconfigures IPsec - and it is deliberately
 * not taken yet, because the driver does not advertise IPSEC_OFFLOAD and a hook that did nothing
 * would read like a feature.
 */

function xgs_firewall($fw)
{
    /*
     * Discard every offloaded flow, because the ruleset those flows were authorised under is being
     * replaced.
     *
     * This hook is the only one that covers every reload. plugins_firewall($fw) is called from
     * inside filter_configure_sync(), which is where all ten reload paths converge - the GUI, the
     * API, three calls during rc.bootup, a WAN lease renewal through rc.newwanip, a routing change
     * - so there is nothing to register and nothing an OPNsense update can undo. Hooking the
     * individual callers instead would miss nine of them, and a missed reload is a hole rather than
     * a slow path: the coprocessor would keep forwarding under a ruleset that no longer exists.
     *
     * It fires BEFORE pfctl -f, which is the right side to be on. The table is discarded while the
     * old ruleset is still live, so between here and the load the host filters every frame with the
     * old rules - correct, just slower. A post-load hook would leave the new ruleset live with the
     * hardware still holding flows the old one permitted, and that window is the hole itself.
     *
     * One sysctl per family, write-only and taking no operand: the driver owns the revision, posts
     * the command, and reports what happened in its counters. Nothing here opens the write gate,
     * stages a command or sequences three separate writes - see docs/octep/ruleset-reload.md for why
     * that shape was rejected. The OCTEON TX appliance has octep and the ARMADA one has npuep; a box
     * has at most one, and the write to the absent driver is a no-op.
     *
     * Muted, and the result ignored on purpose. There may be no coprocessor: the module may not be
     * loaded, may have declined to attach, or - the common case for the first of a boot's three
     * reloads - may not have handshaken yet. All of those are normal, and a hook that logged them
     * would log once per reload forever.
     *
     * Contributes no rules, so $fw is returned untouched.
     */
    mwexecf('/sbin/sysctl -q %s=1', ['dev.octep.0.rpc.fw_rev_bump'], true);
    mwexecf('/sbin/sysctl -q %s=1', ['dev.npuep.0.flow.fw_rev_bump'], true);

    /*
     * The management link to the ARMADA coprocessor (issue 374). mvmgmt0 is a point-to-point
     * link over PCIe with a fixed link-local pair, 169.254.254.1 at this end and .2 at the
     * coprocessor, assigned as an OPNsense interface by the installer. Without a rule the
     * default deny drops everything the coprocessor sends in - measured: its echo requests
     * arrived and were logged as blocked while the host's own went through. Two rules, on
     * whichever interface key carries mvmgmt0: ICMP, and TCP 22 for the coprocessor's sshd,
     * both from the coprocessor's address to this host alone. Nothing is forwarded, nothing
     * else enters, and a machine with no such assignment gets no rule at all.
     */
    foreach (config_read_array('interfaces') as $key => $intf) {
        if (empty($intf['if']) || $intf['if'] != 'mvmgmt0' || empty($intf['enable'])) {
            continue;
        }
        $baserule = [
            'interface' => $key,
            'direction' => 'in',
            'ipprotocol' => 'inet',
            'from' => '169.254.254.2',
            'to' => '(self)',
            'quick' => true,
            'log' => !isset(config_read_array('syslog')['nologdefaultpass']),
            'descr' => 'allow the coprocessor on the management link',
        ];
        $fw->registerFilterRule(1, ['protocol' => 'icmp'], $baserule);
        $fw->registerFilterRule(1, ['protocol' => 'tcp', 'to_port' => 22], $baserule);
        break;
    }

    return $fw;
}

function xgs_configure()
{
    /*
     * At boot, once OPNsense has configured its interfaces: give the coprocessor its end of the
     * management link. The coprocessor's root filesystem is read-only and forgets every
     * address at its reset, and nothing on its side knows the pair, so this host tells it over
     * the console - the one path that exists before the link carries anything. The hook that
     * loads the driver has already brought mvmgmt0 up (07-npuep); this is the other half.
     */
    return [
        'bootup' => ['xgs_mgmt_link_bootup'],
    ];
}

function xgs_mgmt_link_bootup()
{
    /* Only where the link exists; the OCTEON TX appliance has no mvmgmt0 and the script says so. */
    mwexecf('/usr/local/opnsense/scripts/npuctl/mgmt-link.sh coprocessor', [], true);
}

function xgs_devices()
{
    /*
     * The front ports, as a device class.
     *
     * No 'function' and no 'names' key, which is what keeps this safe: interfaces.inc skips an
     * entry without them when it creates or reloads devices, and list_assign_options.php only adds
     * entries that carry 'names'. So nothing here invents an interface or changes the assignment
     * list - these ports are real hardware that the driver brings up, not something OPNsense makes.
     *
     * What it does do is set spoofmac false. A MAC written to a front port goes to the coprocessor
     * as a NetAgent attribute and becomes that port's hardware filter, and a filter that does not
     * match the traffic silently drops every frame - measured, and it looks exactly like a dead
     * cable. Declining the capability is better than leaving a switch in the GUI that breaks a
     * port.
     */
    return [[
        'pattern' => '^oxp[0-9]',
        'configurable' => true,
        'spoofmac' => false,
        'volatile' => false,
        'type' => 'xgs',
        'descr' => gettext('XGS front port'),
    ]];
}

function xgs_syslog()
{
    /*
     * Every tag the plugin logs under, so the Log File page of the coprocessor's menu section shows
     * them all: 'octep' (the XGS 3300's bring-up, and through syslog-ng's unanchored match also
     * octep-follow and octep-stop), 'npuep' and 'npuctl' (the XGS 136's driver hook, gate and
     * reset), 'xgs-assign' (the first-boot port assignment) and 'xgs-switch' (the switch chip's
     * jumbo mode). Without this the messages are only in the general log and nobody finds them.
     */
    $logfacilities = [];

    $logfacilities['xgs'] = ['facility' => ['octep', 'npuep', 'npuctl', 'xgs-assign', 'xgs-switch']];

    return $logfacilities;
}

function xgs_cron()
{
    /*
     * The coprocessor's temperatures, fed to the driver every minute so they reach OPNsense's own
     * temperature widget: scripts/xgs/temperatures.sh. It exits at once on a box without the
     * driver's temperature nodes, and the driver refuses a reading five minutes old.
     */
    $jobs = [];

    $jobs[]['autocron'] = ['/bin/sh /usr/local/opnsense/scripts/xgs/temperatures.sh', '*'];

    return $jobs;
}
