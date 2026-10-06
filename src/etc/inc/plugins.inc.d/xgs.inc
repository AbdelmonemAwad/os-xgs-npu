<?php

/*
 * Copyright (c) 2026 the os-xgs-npu authors
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
     * One sysctl, and it is write-only and takes no operand: the driver owns the revision, posts the
     * command from its own task, and reports what happened in its counters. Nothing here opens the
     * write gate, stages a command or sequences three separate writes - see docs/ruleset-reload.md
     * for why that shape was rejected.
     *
     * Muted, and the result ignored on purpose. There may be no coprocessor: the module may not be
     * loaded, may have declined to attach, or - the common case for the first of a boot's three
     * reloads - may not have handshaken yet. All of those are normal, and a hook that logged them
     * would log once per reload forever.
     *
     * Contributes no rules, so $fw is returned untouched.
     */
    mwexecf('/sbin/sysctl -q %s=1', ['dev.octep.0.rpc.fw_rev_bump'], true);

    return $fw;
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
     * The driver and the boot hooks all log under the 'octep' tag to daemon.*, so one facility
     * covers the module, the bring-up, the kernel follower and the shutdown hook. Without this the
     * messages are only in the general log and nobody finds them.
     */
    $logfacilities = [];

    $logfacilities['xgs'] = ['facility' => ['octep']];

    return $logfacilities;
}
