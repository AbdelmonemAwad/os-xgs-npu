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

/**
 * IPsec in the coprocessor's engine, as the script reports it. Read-only.
 */
class IpsecController extends ApiControllerBase
{
    public function getAction()
    {
        $decoded = json_decode((new Backend())->configdRun('xgs ipsec'), true);
        if ($decoded === null) {
            return ['present' => false, 'message' => gettext('the backend returned nothing readable')];
        }
        return $decoded;
    }

    /**
     * The associations as rows for the page's grid, as scripts/xgs/ipsec.py made them for either
     * family: on the XGS 3300 what its engine table holds, on the XGS 136 what the kernel still has
     * of the associations offered to a front port, each saying whether the engine took it.
     */
    public function associationsAction()
    {
        $d = $this->getAction();
        $rows = [];
        foreach (($d['rows'] ?? []) as $r) {
            $rows[] = [
                'direction' => (string)($r['direction'] ?? ''),
                'spi' => (string)($r['spi'] ?? ''),
                'tunnel' => (string)($r['tunnel'] ?? ''),
                'port' => (string)($r['port'] ?? ''),
                'window' => (string)($r['window'] ?? ''),
                'handle' => (string)($r['handle'] ?? ''),
                'engine' => !empty($r['engine']) ? '1' : '0',
                'bytes' => isset($r['bytes']) ? (int)$r['bytes'] : null,
                'packets' => isset($r['packets']) ? (int)$r['packets'] : null,
                'hw_packets' => isset($r['hw_packets']) ? (int)$r['hw_packets'] : null,
                'note' => (string)($r['note'] ?? ''),
            ];
        }
        return $this->searchRecordsetBase($rows);
    }

    /**
     * Every counter of the engine's IPsec subtree, with the group it belongs to; the ones at zero
     * only when the page asks for them (zeros=1), because most of them are, most of the time.
     */
    public function countersAction()
    {
        $d = $this->getAction();
        $zeros = (string)$this->request->get('zeros') === '1';
        $groups = [
            /* the XGS 3300's names and the XGS 136's: sa_installed there, accepted here */
            [gettext('Associations'), '/^sa_|^seq_|^install|^offered|^accept|^removed|^probe|^esn|^win_/'],
            [gettext('Receive'), '/^rx_/'],
            [gettext('Transmit'), '/^tx_|^out_/'],
            [gettext('Flows'), '/^flow/'],
            [gettext('Other'), '/./'],
        ];
        $rows = [];
        $taken = [];
        foreach ($groups as $group) {
            $names = [];
            foreach (($d['counters'] ?? []) as $name => $value) {
                if (!isset($taken[$name]) && preg_match($group[1], $name)) {
                    $taken[$name] = true;
                    $names[$name] = (int)$value;
                }
            }
            ksort($names);
            foreach ($names as $name => $value) {
                if ($zeros || $value !== 0) {
                    $rows[] = ['group' => $group[0], 'name' => $name, 'value' => $value];
                }
            }
        }
        return $this->searchRecordsetBase($rows);
    }
}
