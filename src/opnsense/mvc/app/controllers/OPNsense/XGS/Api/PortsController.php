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
 * the front ports, the expansion module and the switch chip, as the script reports it. Read-only.
 */
class PortsController extends ApiControllerBase
{
    public function getAction()
    {
        $decoded = json_decode((new Backend())->configdRun('xgs ports'), true);
        if ($decoded === null) {
            return ['present' => false, 'message' => gettext('the backend returned nothing readable')];
        }
        return $decoded;
    }

    /**
     * Power over Ethernet on the ports that carry it, read from the controller now
     * (scripts/xgs/poe.py show). Switching a port is Api/PoeController's, under XGS: Settings.
     */
    public function poeAction()
    {
        $decoded = json_decode((new Backend())->configdRun('xgs poe show'), true);
        if ($decoded === null) {
            return ['present' => false, 'error' => gettext('the backend returned nothing readable')];
        }
        return $decoded;
    }

    /**
     * The switch's own counters for the panel ports behind it - errors and drops the host never
     * sees - as the plugin's cron last read them (scripts/xgs/portstats.py show).
     */
    public function countersAction()
    {
        $decoded = json_decode((new Backend())->configdRun('xgs portstats show'), true);
        if ($decoded === null) {
            return ['error' => gettext('the backend returned nothing readable')];
        }
        return $decoded;
    }

    /**
     * And read now: one command on the coprocessor, which on the XGS 3300 is its console, so a POST
     * rather than something a page's every refresh does.
     */
    public function countersReadAction()
    {
        if (!$this->request->isPost()) {
            return ['error' => gettext('this endpoint is POST only')];
        }
        $decoded = json_decode((new Backend())->configdRun('xgs portstats collect'), true);
        if ($decoded === null) {
            return ['error' => gettext('the backend returned nothing readable')];
        }
        return $decoded;
    }

    /**
     * The same ports as rows for the page's grid - OPNsense's own, searchable and sortable like
     * Interfaces: Overview. The media string is split into the speed and the rest here, once.
     */
    public function searchAction()
    {
        $decoded = json_decode((new Backend())->configdRun('xgs ports'), true) ?? [];
        $extra = [];
        foreach (($decoded['module']['extra'] ?? []) as $x) {
            $extra[$x['name'] ?? ''] = $x;
        }
        $rows = [];
        foreach (($decoded['ports'] ?? []) as $p) {
            $media = preg_replace('/^Ethernet\s+autoselect\s*/i', '', (string)($p['media'] ?? ''));
            $media = trim(preg_replace('/\s+/', ' ', str_replace(['(', ')', '<', '>'], ' ', $media)));
            $speed = '';
            if (preg_match('/^(\d+)(G|M)?base/i', $media, $m)) {
                $n = (int)$m[1];
                $speed = (strtoupper($m[2] ?? '') === 'G') ? $n . 'G' : ($n >= 1000 ? ($n / 1000) . 'G' : $n . 'M');
            }
            $a = $p['assigned'] ?? [];
            $x = $extra[$p['label'] ?? ''] ?? null;
            $rows[] = [
                'port' => (string)($p['label'] ?? ''),
                'device' => (string)($p['device'] ?? ''),
                'assigned' => (string)($a['descr'] ?? ''),
                'key' => (string)($a['key'] ?? ''),
                'address' => (string)($a['address'] ?? ''),
                'status' => strpos((string)($p['status'] ?? ''), 'active') === 0 ? 'up' : 'down',
                'speed' => $speed,
                'media' => $media === 'none' ? '' : $media,
                'mtu' => (string)($p['mtu'] ?? ''),
                'bridge' => (string)($p['bridge'] ?? ''),
                /* the bay alone: the module in it is named once, under the table */
                'bay' => $x !== null ? (string)($x['slot'] ?? '') : '',
            ];
        }
        return $this->searchRecordsetBase($rows);
    }
}
