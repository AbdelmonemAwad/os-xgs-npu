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

namespace OPNsense\XGS\Migrations;

use OPNsense\Base\BaseModelMigration;
use OPNsense\XGS\XGS;

/**
 * Version 1.2.0: nothing leaves an appliance unless its owner chooses (2026-10-23). Problem reports
 * wait until their Send is pressed and the appliance is not counted on npuforge.org - the defaults
 * now - and an appliance updated from an earlier release is turned the same way, because its
 * "automatic" and its count were the old defaults far more often than anybody's answer: the saved
 * settings cannot tell the two apart. Off stays off. The question is asked again, on XGS: Overview
 * and in the header's notice, so an owner who wants reports sent says so once more.
 */
class M1_2_0 extends BaseModelMigration
{
    public function run($model)
    {
        if (!($model instanceof XGS)) {
            return;
        }
        $r = $model->reporting;
        if ((string)$r->mode === 'automatic') {
            $r->mode = 'manual';
        }
        $r->count = '0';
        /* the flag the question and the notice go by (scripts/xgs/reporter.py acknowledge) */
        @unlink('/var/db/os-xgs-npu/reports/notice-seen');
    }
}
