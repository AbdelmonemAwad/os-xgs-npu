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
 * The connections the coprocessor forwards by itself, read from the driver's own table by
 * scripts/xgs/flows.py - the same rows on either family.
 */
class FlowsController extends ApiControllerBase
{
    public function getAction()
    {
        $decoded = json_decode((new Backend())->configdRun('xgs flows'), true);
        return is_array($decoded) ? $decoded : ['family' => '', 'auto' => null, 'counters' => [], 'flows' => []];
    }

    /** The figures without the table, for the dashboard widget (scripts/xgs/flows.py summary). */
    public function summaryAction()
    {
        $decoded = json_decode((new Backend())->configdRun('xgs flowsummary'), true);
        return is_array($decoded) ? $decoded : ['family' => '', 'auto' => null, 'summary' => []];
    }

    public function searchAction()
    {
        $data = $this->getAction();
        /* the port by the name printed on the panel, not by the kernel's device name */
        $labels = [];
        $ports = json_decode((new Backend())->configdRun('xgs ports'), true);
        foreach ((is_array($ports) ? ($ports['ports'] ?? []) : []) as $p) {
            $labels[(string)($p['device'] ?? '')] = (string)($p['label'] ?? '');
        }
        /*
         * The next hop each direction leaves by: the driver keeps one per flow (scripts/xgs/flows.py);
         * on the XGS 3300 with the neighbour's address or MAC, its VLAN and MTU, on the XGS 136 the
         * index alone.
         */
        $hop = function ($f, $d) use ($data) {
            $i = $f['nhop'][$d] ?? null;
            if ($i === null) {
                return '';
            }
            $nh = $data['nhops'][(string)$i] ?? null;
            if (!is_array($nh)) {
                return '#' . $i;
            }
            return '#' . $i . ' ' . ($nh['address'] !== '' ? $nh['address'] : $nh['mac']) .
                (!empty($nh['vlan']) ? ' VLAN ' . $nh['vlan'] : '') . ' MTU ' . $nh['mtu'];
        };
        $toPf = !empty($data['to_pf']);
        $rows = [];
        foreach (($data['flows'] ?? []) as $f) {
            $rows[] = [
                'nhop_orig' => $hop($f, 0),
                'nhop_reply' => $hop($f, 1),
                /* counted in pf: what the coprocessor counted is added to the connection's pf state */
                'pf' => !$toPf ? 'off' : (isset($f['bytes']) ? 'counted' : 'waiting'),
                'id' => (string)($f['id'] ?? ''),
                /* not shown: Return to host posts it with the index (OffloadController::releaseAction) */
                'rev' => (string)($f['rev'] ?? ''),
                'protocol' => (string)($f['protocol'] ?? ''),
                'source' => (string)($f['src'] ?? '') . ':' . (string)($f['sport'] ?? ''),
                'destination' => (string)($f['dst'] ?? '') . ':' . (string)($f['dport'] ?? ''),
                'port' => $labels[(string)($f['port'] ?? '')] ?? (string)($f['port'] ?? ''),
                'nat' => (string)($f['nat'] ?? ''),
                'bridged' => !empty($f['bridged']) ? '1' : '',
                'vlan' => !empty($f['vlan']) ? '1' : '',
                'ipsec' => implode(',', $f['ipsec'] ?? []),
                'state' => (string)($f['state'] ?? ''),
                'age' => (string)($f['age'] ?? ''),
                'punts' => (string)($f['punts'] ?? '0'),
                /* what the coprocessor counted, [opener's direction, the other] */
                'bytes' => isset($f['bytes']) ? (string)array_sum($f['bytes']) : '',
                'bytes_orig' => isset($f['bytes']) ? (string)$f['bytes'][0] : '',
                'bytes_reply' => isset($f['bytes']) ? (string)$f['bytes'][1] : '',
                'packets' => isset($f['packets']) ? (string)array_sum($f['packets']) : '',
            ];
        }
        return $this->searchRecordsetBase($rows);
    }

    /**
     * The connections that left hardware between two readings of the table, the last hundred, newest
     * first: when (between the two readings) and what pf says of each now - its state gone (closed,
     * killed or expired) or still there (the host carries it). The drivers keep counts of why a
     * connection was let go, not a record of each; scripts/xgs/flows.py keeps this one.
     */
    public function leftAction()
    {
        $data = $this->getAction();
        $rows = [];
        foreach (array_reverse($data['left']['gone'] ?? []) as $i => $f) {
            $rows[] = [
                'n' => (string)$i,
                'protocol' => (string)($f['protocol'] ?? ''),
                'source' => (string)($f['src'] ?? '') . ':' . (string)($f['sport'] ?? ''),
                'destination' => (string)($f['dst'] ?? '') . ':' . (string)($f['dport'] ?? ''),
                'after' => (int)($f['after'] ?? 0),
                'before' => (int)($f['before'] ?? 0),
                'pf' => (string)($f['pf'] ?? ''),
            ];
        }
        return $this->searchRecordsetBase($rows);
    }
}
