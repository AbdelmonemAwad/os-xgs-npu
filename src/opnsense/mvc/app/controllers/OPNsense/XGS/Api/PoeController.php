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
 * THIS SOFTWARE IS PROVIDED ``AS IS' AND ANY EXPRESS OR IMPLIED WARRANTIES,
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

namespace OPNsense\XGS\Api;

use OPNsense\Base\ApiControllerBase;
use OPNsense\Core\Backend;
use OPNsense\Core\Config;
use OPNsense\XGS\XGS;

/**
 * A PoE port's power switched off, on, or off and on again, and its limit set to 15.4 W (af) or 30 W
 * (at), through scripts/xgs/poe.py. Under XGS: Settings in the ACL and not with the Ports page's
 * reading, because it changes what the appliance does: a camera or an access point on the port loses
 * its power. Off and on are kept in the configuration (general.poe_off), the limit too
 * (general.poe_af), and both are applied again at every boot; a cycle is kept nowhere.
 */
class PoeController extends ApiControllerBase
{
    public function setAction()
    {
        if (!$this->request->isPost()) {
            return ['result' => 'failed', 'error' => gettext('this endpoint is POST only')];
        }
        $label = (string)$this->request->getPost('label');
        $do = (string)$this->request->getPost('do');
        if (!preg_match('/^[0-9]{1,2}$/', $label) || !in_array($do, ['on', 'off', 'cycle', 'af', 'at'], true)) {
            return ['result' => 'failed', 'error' => gettext('a port label, and on, off, cycle, af or at')];
        }
        $backend = new Backend();
        if ($do === 'cycle') {
            $decoded = json_decode($backend->configdpRun('xgs poe cycle', [$label]), true);
            return $decoded ?? ['result' => 'failed', 'error' => gettext('the backend returned nothing readable')];
        }
        /* only a port the controller lists is kept in the configuration */
        $now = json_decode($backend->configdRun('xgs poe show'), true) ?? [];
        $labels = array_map(function ($p) {
            return (string)($p['label'] ?? '');
        }, $now['ports'] ?? []);
        if (!in_array($label, $labels, true)) {
            return ['result' => 'failed', 'error' => sprintf(gettext('port %s carries no PoE'), $label)];
        }
        $mdl = new XGS();
        /* on and off change the ports switched off, af and at the ports held to 15.4 W */
        $field = in_array($do, ['af', 'at'], true) ? 'poe_af' : 'poe_off';
        $kept = array_values(array_diff(array_filter(explode(',', (string)$mdl->general->$field), 'strlen'), [$label]));
        if ($do === 'off' || $do === 'af') {
            $kept[] = $label;
        }
        sort($kept, SORT_NUMERIC);
        $mdl->general->$field = implode(',', $kept);
        foreach ($mdl->performValidation() as $msg) {
            return ['result' => 'failed', 'error' => $msg->getMessage()];
        }
        $mdl->serializeToConfig();
        Config::getInstance()->save();
        $decoded = json_decode($backend->configdRun('xgs poe apply'), true);
        return $decoded ?? ['result' => 'failed', 'error' => gettext('the backend returned nothing readable')];
    }
}
