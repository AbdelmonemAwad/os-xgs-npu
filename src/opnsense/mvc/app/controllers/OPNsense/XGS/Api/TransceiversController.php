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
 * The modules in the front cages and their diagnostics, as scripts/xgs/transceivers.py read them
 * on the coprocessor - every five minutes from the plugin's cron, or now when the page asks.
 */
class TransceiversController extends ApiControllerBase
{
    private function read($action)
    {
        $decoded = json_decode((new Backend())->configdRun('xgs transceivers ' . $action), true);
        return is_array($decoded) ? $decoded : ['at' => null, 'modules' => [],
            'error' => gettext('the backend returned nothing readable')];
    }

    public function getAction()
    {
        return $this->read('show');
    }

    /**
     * Read the cages again now. A POST, because it talks to the coprocessor - over its console on
     * the XGS 3300, for up to a minute.
     */
    public function refreshAction()
    {
        if (!$this->request->isPost()) {
            return ['status' => 'failed'];
        }
        return $this->read('collect');
    }

    public function searchAction()
    {
        $data = $this->read('show');
        $rows = [];
        foreach (($data['modules'] ?? []) as $m) {
            $dom = $m['dom'] ?? [];
            $rows[] = [
                'port' => (string)($m['port'] ?? ''),
                'present' => !empty($m['present']) ? '1' : '0',
                'vendor' => (string)($m['vendor'] ?? ''),
                'part' => (string)($m['part'] ?? ''),
                'serial' => (string)($m['serial'] ?? ''),
                'type' => implode(', ', $m['types'] ?? []),
                'rate' => isset($m['rate_mbd']) ? (string)$m['rate_mbd'] : '',
                'diagnostics' => !empty($m['diagnostics']) ? '1' : '0',
                'temp' => isset($dom['temp_c']) ? sprintf('%.1f', $dom['temp_c']) : '',
                'vcc' => isset($dom['vcc_v']) ? sprintf('%.3f', $dom['vcc_v']) : '',
                'bias' => isset($dom['bias_ma']) ? sprintf('%.2f', $dom['bias_ma']) : '',
                'tx' => isset($dom['tx_dbm']) ? sprintf('%.2f', $dom['tx_dbm']) : (isset($dom['tx_mw']) ? '-inf' : ''),
                'rx' => isset($dom['rx_dbm']) ? sprintf('%.2f', $dom['rx_dbm']) : (isset($dom['rx_mw']) ? '-inf' : ''),
            ];
        }
        return $this->searchRecordsetBase($rows);
    }
}
