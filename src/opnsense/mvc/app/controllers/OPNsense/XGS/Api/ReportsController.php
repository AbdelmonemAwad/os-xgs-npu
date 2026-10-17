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
 * The problem reports, through scripts/xgs/reporter.py. The script owns the queue, the cleaning and
 * the sending; this only passes requests to it and its answers back. A report is named by its
 * fingerprint, sixteen hex digits, and nothing else reaches the script's command line.
 */
class ReportsController extends ApiControllerBase
{
    private function run($action, $params = [])
    {
        $decoded = json_decode((new Backend())->configdpRun('xgs report ' . $action, $params), true);
        return is_array($decoded) ? $decoded : ['status' => 'failed', 'message' => gettext('No answer from the problem reporter.')];
    }

    private function ident($id)
    {
        return preg_match('/^[0-9a-f]{16}$/', (string)$id) ? (string)$id : null;
    }

    /**
     * The reporting section of the model alone. The page's privilege covers this controller and no
     * other, so somebody allowed to choose how reports are sent cannot reach the offload settings
     * through it: only xgs.reporting is read, and only xgs.reporting is written.
     */
    public function getAction()
    {
        return ['xgs' => ['reporting' => (new XGS())->reporting->getNodes()]];
    }

    public function setAction()
    {
        $result = ['result' => 'failed'];
        if (!$this->request->isPost()) {
            return $result;
        }
        $mdl = new XGS();
        $post = $this->request->getPost('xgs');
        if (is_array($post) && isset($post['reporting']) && is_array($post['reporting'])) {
            $mdl->reporting->setNodes($post['reporting']);
        }
        foreach ($mdl->performValidation() as $msg) {
            $result['validations']['xgs.' . $msg->getField()] = $msg->getMessage();
        }
        if (!empty($result['validations'])) {
            return $result;
        }
        $mdl->serializeToConfig();
        Config::getInstance()->save();
        return ['result' => 'saved'];
    }

    public function searchAction()
    {
        $list = $this->run('list');
        $rows = isset($list['rows']) && is_array($list['rows']) ? $list['rows'] : [];
        return $this->searchRecordsetBase($rows);
    }

    /**
     * The mode, the relay and the counts the page's cards show: reports waiting to be sent, reports
     * sent, and when the last problem not dismissed was seen.
     */
    public function stateAction()
    {
        $list = $this->run('list');
        $rows = isset($list['rows']) && is_array($list['rows']) ? $list['rows'] : [];
        $counts = ['waiting' => 0, 'sent' => 0, 'last' => ''];
        foreach ($rows as $row) {
            $status = $row['status'] ?? '';
            if ($status === 'new' || $status === 'failed') {
                $counts['waiting']++;
            } elseif ($status === 'sent') {
                $counts['sent']++;
            }
            if ($status !== 'dismissed' && (string)($row['last_seen'] ?? '') > $counts['last']) {
                $counts['last'] = (string)$row['last_seen'];
            }
        }
        unset($list['rows']);
        $list['counts'] = $counts;
        return $list;
    }

    public function showAction($id = null)
    {
        $id = $this->ident($id);
        return $id === null ? ['status' => 'failed'] : $this->run('show', [$id]);
    }

    public function sendAction($id = null)
    {
        $id = $this->ident($id);
        if (!$this->request->isPost() || $id === null) {
            return ['status' => 'failed'];
        }
        return $this->run('send', [$id]);
    }

    public function dismissAction($id = null)
    {
        $id = $this->ident($id);
        if (!$this->request->isPost() || $id === null) {
            return ['status' => 'failed'];
        }
        return $this->run('dismiss', [$id]);
    }

    public function collectAction()
    {
        return $this->request->isPost() ? $this->run('collect') : ['status' => 'failed'];
    }

    public function acknowledgeAction()
    {
        return $this->request->isPost() ? $this->run('acknowledge') : ['status' => 'failed'];
    }

    /**
     * A report written by the owner. The text goes to the script as base64, so that no character
     * of it is ever read by a shell.
     */
    public function manualAction()
    {
        if (!$this->request->isPost()) {
            return ['status' => 'failed'];
        }
        /* unfiltered: the text is never markup here, and the script cleans and limits it */
        $request = [
            'title' => trim((string)$this->request->getPost('title')),
            'description' => trim((string)$this->request->getPost('description')),
            'part' => (string)($this->request->getPost('part') ?? 'os-xgs-npu'),
        ];
        if ($request['title'] === '' || $request['description'] === '') {
            return ['status' => 'failed', 'message' => gettext('A title and a description are needed.')];
        }
        return $this->run('manual', [base64_encode(json_encode($request))]);
    }
}
