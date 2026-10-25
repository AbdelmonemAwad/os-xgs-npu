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
 * The coprocessor's state, as the script reports it.
 *
 * Read-only on purpose. The datapath this page describes carries the firewall's traffic, and a
 * page that could reconfigure it would be a poor trade for a table of numbers; the sysctls that
 * do change things are deliberately not reachable from here.
 */
class StatusController extends ApiControllerBase
{
    public function getAction()
    {
        $response = (new Backend())->configdRun('xgs status');
        $decoded = json_decode($response, true);
        if ($decoded === null) {
            return ['present' => false, 'message' => gettext('the status backend returned nothing readable')];
        }
        return $decoded;
    }

    /**
     * The coprocessor itself, for the Overview's Coprocessor tab - its software and versions, its
     * chips, its flash and eMMC - as scripts/xgs/coprocessor.py reads them: what was read last on a
     * GET; on a POST read again now, with `read` "collect", or with "slots" also opening each eMMC
     * partition nothing has mounted read-only for a moment. Everything it runs reads; nothing on the
     * coprocessor is written.
     */
    public function coprocessorAction()
    {
        $what = 'show';
        if ($this->request->isPost()) {
            $asked = (string)$this->request->getPost('read');
            $what = in_array($asked, ['collect', 'slots'], true) ? $asked : 'collect';
        }
        $decoded = json_decode((new Backend())->configdRun('xgs coprocessor ' . $what), true);
        return is_array($decoded) ? $decoded : ['error' => 'unread'];
    }

    /**
     * The datapath counters as rows for the Overview's grid: the name, the value, and what it
     * counts, as the status script describes it.
     */
    public function countersAction()
    {
        $rows = [];
        foreach (($this->getAction()['datapath'] ?? []) as $name => $counter) {
            $rows[] = [
                'name' => (string)$name,
                'value' => (int)($counter['value'] ?? 0),
                'description' => (string)($counter['description'] ?? ''),
            ];
        }
        return $this->searchRecordsetBase($rows);
    }
}
