<?php

/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 */

namespace OPNsense\RRD\Stats;

/**
 * The XGS coprocessor for Reporting: Health, collected by OPNsense's own updaterrd.php every
 * minute: its one-minute load and its memory in use in per cent, as scripts/xgs/health.py last
 * read them (every five minutes; older than fifteen minutes is not given, so a coprocessor that
 * stopped answering leaves a gap), and the connections it forwards by itself, the driver's count.
 * OPNsense's Factory runs every class in this directory; nothing of OPNsense's own is changed.
 * Types/XGSCoprocessor.php and Types/XGSFlows.php write the figures to their files.
 */
class XGSCoprocessor extends Base
{
    public function run()
    {
        $data = [];
        $kept = @file_get_contents('/var/db/os-xgs-npu/health.json');
        $h = $kept !== false ? json_decode($kept, true) : null;
        if (is_array($h) && !empty($h['at']) && time() - (int)$h['at'] <= 900) {
            if (!empty($h['load']) && is_array($h['load'])) {
                $data['load'] = (float)$h['load'][0];
            }
            if (!empty($h['mem_total_kb']) && isset($h['mem_available_kb'])) {
                $data['memory'] = 100.0 * ((float)$h['mem_total_kb'] - (float)$h['mem_available_kb']) /
                    (float)$h['mem_total_kb'];
            }
        }
        foreach (['dev.octep.0.dp.flows', 'dev.npuep.0.flow.conns'] as $oid) {
            $out = $this->shellCmd('/sbin/sysctl -n %s', [$oid]);
            if (!empty($out) && ctype_digit(trim($out[0]))) {
                $data['flows'] = (int)trim($out[0]);
                break;
            }
        }
        return $data;
    }
}
