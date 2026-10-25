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

namespace OPNsense\XGS\Api;

use OPNsense\Base\ApiControllerBase;
use OPNsense\Core\Backend;

/**
 * Write the acceleration gate to the coprocessor, and say what it answered.
 *
 * Separate from SettingsController because the two do different things and fail differently. A
 * save either validates or it does not, and nothing outside config.xml has been touched either
 * way. An apply reaches hardware: the driver may not be loaded, the rpc facility may not be up,
 * the request may be refused. None of that is a reason to reject the setting - it is a reason to
 * tell the operator what happened, which is why this returns the script's own report rather than
 * a status word.
 */
class OffloadController extends ApiControllerBase
{
    public function applyAction()
    {
        if (!$this->request->isPost()) {
            return ['status' => 'failed', 'message' => gettext('this endpoint is POST only')];
        }

        $response = (new Backend())->configdRun('xgs apply');
        $decoded = json_decode($response, true);
        if ($decoded === null) {
            return [
                'status' => 'failed',
                'message' => gettext('the offload backend returned nothing readable'),
            ];
        }

        return $decoded;
    }

    /**
     * Hand one connection back to the host (scripts/xgs/flows.py release). Here and not on the Flows
     * page's own controller: that page's privilege reads and changes nothing, and taking a
     * connection out of the coprocessor is the owner's offload decision, granted with the settings.
     */
    /**
     * Zero the coprocessor's fast-path counters (counters_clear, discovery 13), then report the ones
     * not zero - the Datapath counters tab's Clear. Here and not on the read-only status API: it
     * changes something on the coprocessor, if only its counts.
     */
    public function fpclearAction()
    {
        if (!$this->request->isPost()) {
            return ['status' => 'failed'];
        }
        $decoded = json_decode((new Backend())->configdRun('xgs fpcounters clear'), true);
        return is_array($decoded) ? $decoded : ['error' => 'unread', 'counters' => []];
    }

    public function releaseAction($id = null)
    {
        if (!$this->request->isPost()) {
            return ['released' => false, 'message' => gettext('this endpoint is POST only')];
        }
        $id = (string)$id;
        if (!ctype_digit($id)) {
            return ['released' => false, 'message' => gettext('not a connection number')];
        }
        $decoded = json_decode((new Backend())->configdpRun('xgs flowrelease', [$id]), true);
        return is_array($decoded) ? $decoded :
            ['released' => false, 'message' => gettext('the flow backend returned nothing readable')];
    }
}
