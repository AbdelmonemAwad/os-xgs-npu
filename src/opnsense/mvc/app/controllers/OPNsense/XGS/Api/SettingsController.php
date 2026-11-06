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

use OPNsense\Base\ApiMutableModelControllerBase;
use OPNsense\Core\Config;

/**
 * One container saved in one go - the general section - so this is the plain model controller,
 * held to that container.
 *
 * Saving is only half of it: the setting lands in config.xml and the coprocessor knows nothing
 * about it until somebody writes the gate. OffloadController does that, and the page calls it
 * straight after a successful save, so a save that is not followed by an apply cannot happen by
 * accident.
 *
 * Held to it because the model holds the problem reports' settings too - xgs.reporting: the mode,
 * the count and the contact address - and those belong to XGS: Problem Reports, a privilege of its
 * own (ACL.xml) whose controller, ReportsController, reads and writes them and nothing else. Until
 * 2026-11-06 this controller read and wrote the whole model, so somebody given XGS: Settings alone
 * could read the contact address through it, and choose what this appliance sends. The settings
 * page never showed those fields or sent them, so nothing on it changes.
 */
class SettingsController extends ApiMutableModelControllerBase
{
    protected static $internalModelName = 'xgs';
    protected static $internalModelClass = '\OPNsense\XGS\XGS';

    /**
     * What settings/get answers: xgs.general, and not xgs.reporting.
     */
    protected function getModelNodes()
    {
        return ['general' => $this->getModel()->general->getNodes()];
    }

    /**
     * The base class's own set, step for step, with only xgs.general handed to the model: anything
     * else posted, the reporting section among it, is left as it is.
     */
    public function setAction()
    {
        $result = ['result' => 'failed'];
        if ($this->request->isPost()) {
            Config::getInstance()->lock();
            $post = $this->request->getPost(static::$internalModelName);
            $general = is_array($post) && isset($post['general']) ? ['general' => $post['general']] : null;
            $this->getModel()->setNodes($general);
            $result = $this->validate();
            if (empty($result['result'])) {
                $this->setActionHook();
                return $this->save(false, true);
            }
        }
        return $result;
    }

    /**
     * Let the next boot assign the front ports again: remove the first-boot assignment's mark.
     * The assignment itself still only acts on a box whose front ports are all unassigned.
     */
    public function reassignAction()
    {
        if (!$this->request->isPost()) {
            return ['status' => 'failed'];
        }
        $out = trim((new \OPNsense\Core\Backend())->configdRun('xgs reassign'));
        return ['status' => $out === 'OK' ? 'ok' : 'failed'];
    }
}
