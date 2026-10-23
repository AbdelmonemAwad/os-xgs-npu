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

namespace OPNsense\System\Status;

use OPNsense\Core\Backend;
use OPNsense\System\AbstractStatus;
use OPNsense\System\SystemStatusCode;

/**
 * The problem reporter in OPNsense's own status, the way the firmware's crash reporter is there.
 *
 * Two things, in this order. Until the owner has answered - applied a choice on XGS: Problem
 * Reports, answered XGS: Overview's question, or closed this - a notice in the header says what is
 * and is not sent, and where to change it: since 2026-10-23 nothing leaves the appliance unless its
 * owner chooses, and sending was automatic by default until then. Its X closes it for good and the
 * notice itself opens the page. After that, a report waiting to be sent - in manual mode, or one
 * the relay did not take - is a notice in the header that links to the page.
 *
 * The first was a banner across the top of every page until 2026-10-19, when the owner asked for a
 * way to close it and a link to the page. OPNsense draws a banner with neither, and escapes any
 * markup in it; the notices in its status menu have both.
 *
 * It reads one small file the reporter writes (scripts/xgs/reporter.py, summary()), so it costs a
 * file read on each status poll and runs nothing.
 */
class XGSReporterStatus extends AbstractStatus
{
    public function __construct()
    {
        $this->internalPriority = 50;
        $this->internalTitle = gettext('Problem reports');
        $this->internalLocation = '/ui/xgs/reports';
    }

    public function collectStatus()
    {
        $file = '/var/db/os-xgs-npu/reports/summary.json';
        if (!is_dir('/usr/local/opnsense/scripts/xgs') || !is_readable('/usr/local/opnsense/scripts/xgs/reporter.py')) {
            return;
        }
        $summary = is_readable($file) ? json_decode((string)@file_get_contents($file), true) : null;
        $summary = is_array($summary) ? $summary : [];
        $seen = file_exists('/var/db/os-xgs-npu/reports/notice-seen');
        $mode = $summary['mode'] ?? 'manual';

        if (!$seen) {
            $this->internalStatus = SystemStatusCode::NOTICE;
            if ($mode === 'off') {
                $this->internalMessage = gettext('Problem reports are off on this appliance. XGS: Problem Reports is where to turn them on.');
            } elseif ($mode === 'automatic') {
                $this->internalMessage = gettext('This appliance reports its problems to the project on GitHub, without addresses or serials, so they can be fixed. XGS: Problem Reports shows what is sent and is where to make it manual or turn it off.');
            } else {
                $this->internalMessage = gettext('Nothing leaves this appliance unless you choose: its problem reports wait for you to send them, and it is not counted. XGS: Overview asks once; XGS: Problem Reports is where to change it.');
            }
            return;
        }

        $waiting = (int)($summary['waiting'] ?? 0);
        if ($waiting > 0 && $mode !== 'off') {
            /* answered on the page, by Send or Dismiss - so no X that would do nothing */
            $this->internalPersistent = true;
            $this->internalStatus = SystemStatusCode::WARNING;
            $this->internalTimestamp = (int)($summary['last_problem'] ?? 0) ?: null;
            $this->internalMessage = sprintf(
                $mode === 'manual' ?
                    gettext('%d problem report(s) are waiting for you to send them.') :
                    gettext('%d problem report(s) could not be sent yet; they are tried again.'),
                $waiting
            );
        }
    }

    public function dismissStatus()
    {
        /*
         * The X on the first notice: the owner has seen how reporting works, as when a choice is
         * applied on the page, and the mode stays what it is. A waiting report has no X.
         */
        if (!file_exists('/var/db/os-xgs-npu/reports/notice-seen')) {
            (new Backend())->configdRun('xgs report acknowledge');
        }
    }
}
