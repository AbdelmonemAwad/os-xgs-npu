<?php

/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
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
 * A ping sent by the coprocessor over the management link (scripts/npuctl/mgmt-link.sh ping, svc-1-10).
 * The address or name and the count become part of a command line on the coprocessor, so they are
 * checked here, and again by mgmt-link.sh: letters, digits, dots, colons and hyphens, not starting
 * with a hyphen, 253 characters at most; a count of 1 to 10. It sends a few packets and changes
 * nothing, so it is under the reading pages' privilege.
 */
class PingController extends ApiControllerBase
{
    public function pingAction()
    {
        if (!$this->request->isPost()) {
            return ['status' => 'error', 'message' => gettext('this endpoint is POST only')];
        }
        $target = trim((string)$this->request->getPost('address'));
        $count = (string)$this->request->getPost('count');
        if (!preg_match('/^[A-Za-z0-9.:][A-Za-z0-9.:-]{0,252}$/', $target)) {
            return ['status' => 'error', 'message' => gettext('Not an address or a host name.')];
        }
        if (!preg_match('/^([1-9]|10)$/', $count)) {
            return ['status' => 'error', 'message' => gettext('A count is 1 to 10.')];
        }
        $output = (new Backend())->configdpRun('xgs coprocessor ping', [$target, $count]);
        return ['status' => 'ok', 'output' => trim((string)$output)];
    }
}
