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

namespace OPNsense\XGS;

/**
 * XGS: About - the project, its author, its links and addresses, the disclaimer and the licences,
 * in tabs. Static text, and one thing read from this firewall: the release installed, from the
 * record install.sh and the Updates page keep (/usr/local/share/os-xgs-npu/installed.json).
 */
class AboutController extends \OPNsense\Base\IndexController
{
    public function indexAction()
    {
        $installed = [];
        $raw = @file_get_contents('/usr/local/share/os-xgs-npu/installed.json');
        if ($raw !== false) {
            $installed = json_decode($raw, true) ?: [];
        }
        $this->view->release = (string)($installed['release'] ?? '');
        $this->view->commit = substr((string)($installed['commit'] ?? ''), 0, 12);
        $this->view->installedDate = (string)($installed['date'] ?? '');
        $this->view->pick('OPNsense/XGS/about');
    }
}
