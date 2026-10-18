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
 * XGS: Updates - updates of this project from its GitHub repository.
 *
 * Every action is a thin call into update.py through configd, which does the work and answers in
 * JSON; this controller only refuses what must be POST and passes the answer through. configd
 * reads a dot in an action's section name as a level, so [update.status] is "xgs update status".
 * The update itself is started detached (it builds the driver and takes minutes), and the page
 * follows it through status and log.
 */
class UpdatesController extends ApiControllerBase
{
    private function run($action)
    {
        $decoded = json_decode((new Backend())->configdRun('xgs ' . $action), true);
        if ($decoded === null) {
            return ['status' => 'error', 'message' => gettext('the updates backend returned nothing readable')];
        }
        return $decoded;
    }

    public function statusAction()
    {
        return $this->run('update status');
    }

    public function logAction()
    {
        return $this->run('update log');
    }

    public function checkAction()
    {
        if (!$this->request->isPost()) {
            return ['status' => 'error', 'message' => gettext('this endpoint is POST only')];
        }
        return $this->run('update check');
    }

    public function applyAction()
    {
        if (!$this->request->isPost()) {
            return ['status' => 'error', 'message' => gettext('this endpoint is POST only')];
        }
        (new Backend())->configdRun('xgs update apply', true);
        return ['status' => 'ok'];
    }

    /**
     * The firewall's own reboot, as System: Power runs it - offered once an update is installed,
     * because the new driver is loaded only at the next boot. Never called on its own.
     */
    public function rebootAction()
    {
        if (!$this->request->isPost()) {
            return ['status' => 'error', 'message' => gettext('this endpoint is POST only')];
        }
        (new Backend())->configdRun('system reboot', true);
        return ['status' => 'ok'];
    }
}
