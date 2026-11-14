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
     * The XGS 3300's fail-to-wire pair, read now through the coprocessor (scripts/xgs/ftw-status.sh
     * -k): armed or not, normal or bypass, the relay's watchdog and what last triggered it. Read only,
     * and on a click: over the coprocessor's console it is one session of up to twenty seconds.
     */
    public function ftwAction()
    {
        $out = (string)(new Backend())->configdRun('xgs ftwstatus');
        $kv = [];
        foreach (explode("\n", $out) as $line) {
            $at = strpos($line, '=');
            if ($at !== false) {
                $kv[trim(substr($line, 0, $at))] = trim(substr($line, $at + 1));
            }
        }
        return $kv ?: ['relay' => '', 'reason' => trim($out)];
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
            /*
             * ifconfig's line is the medium asked for, then what the link runs at in brackets when
             * the two differ: "Ethernet autoselect (1000baseT <full-duplex>)", "Ethernet 100baseTX
             * (100baseTX <half-duplex>)", or "Ethernet 100baseTX <full-duplex>" alone when they are
             * the same. A switch socket's speed and duplex can be chosen since 2026-11-14, so the
             * speed is read from the running part, and the port says when it was fixed by hand.
             */
            $line = trim((string)($p['media'] ?? ''));
            $asked = $line;
            $running = $line;
            if (preg_match('/^(.*?)\s*\((.*)\)\s*$/', $line, $mm)) {
                $asked = $mm[1];
                $running = $mm[2];
            }
            $fixed = $line !== '' && stripos($asked, 'autoselect') === false && stripos($asked, 'none') === false;
            $duplex = stripos($running, 'half-duplex') !== false ? 'half' :
                (stripos($running, 'full-duplex') !== false ? 'full' : '');
            $media = preg_replace('/^Ethernet\s+/i', '', $running);
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
                'duplex' => $duplex,
                'fixed' => $fixed,
                'media' => $media === 'none' ? '' : $media,
                'mtu' => (string)($p['mtu'] ?? ''),
                'mac' => (string)($p['mac'] ?? ''),
                'bridge' => (string)($p['bridge'] ?? ''),
                /* each VLAN on the port: its OPNsense name, or its device, and tag; set in Interfaces: Devices: VLAN */
                'vlans' => implode(', ', array_map(function ($v) {
                    $name = (string)($v['assigned']['descr'] ?? '') !== '' ? (string)$v['assigned']['descr'] : (string)($v['device'] ?? '');
                    return $name . ' (' . (int)($v['tag'] ?? 0) . ')' . (!empty($v['bridge']) ? ' ' . (string)$v['bridge'] : '');
                }, $p['vlans'] ?? [])),
                /* the bay alone: the module in it is named once, under the table */
                'bay' => $x !== null ? (string)($x['slot'] ?? '') : '',
            ];
        }
        return $this->searchRecordsetBase($rows);
    }
}
