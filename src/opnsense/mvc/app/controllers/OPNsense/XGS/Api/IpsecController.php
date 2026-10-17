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
     * The engine's associations as rows for the page's grid. The two engines print two formats -
     * the XGS 3300's table and the XGS 136's list - and both are read here, once; a line neither
     * reads is kept as a row of its own rather than dropped.
     */
    public function associationsAction()
    {
        $d = $this->getAction();
        $text = (string)($d['texts']['table'] ?? ($d['texts']['sas'] ?? ''));
        $rows = [];
        foreach (explode("
", $text) as $line) {
            if (preg_match('/^\s*\d+\s+handle\s+(\d+)\s+rev\s+\d+\s+(encrypt|decrypt)\s+spi\s+(0x[0-9a-f]+)\s+' .
                '(0x[0-9a-f]+)\s+->\s+(0x[0-9a-f]+)\s+lif\s+\S+\s+(\S+)\s+drv_spi\s+\d+\s+win\s+(\d+)/i', $line, $m)) {
                $rows[] = ['direction' => strtolower($m[2]), 'spi' => $m[3],
                           'tunnel' => long2ip((int)hexdec($m[4])) . ' -> ' . long2ip((int)hexdec($m[5])),
                           'port' => $m[6], 'window' => $m[7], 'handle' => $m[1], 'note' => ''];
            } elseif (preg_match('/^(\S+)\s+spi\s+(0x[0-9a-f]+)\s+(encrypt|decrypt)\s+(inet6?)\s+alg\s+\d+\s+' .
                'win\s+(\d+)\s+(\d+)s ago\s*\|?\s*(.*)$/i', trim($line), $m)) {
                $rows[] = ['direction' => strtolower($m[3]), 'spi' => $m[2], 'tunnel' => $m[4],
                           'port' => $m[1], 'window' => $m[5], 'handle' => '', 'note' => $m[7]];
            } elseif (trim($line) !== '') {
                $rows[] = ['direction' => '', 'spi' => '', 'tunnel' => '', 'port' => '', 'window' => '',
                           'handle' => '', 'note' => trim($line)];
            }
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
            [gettext('Associations'), '/^sa_|^seq_|^install/'],
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
