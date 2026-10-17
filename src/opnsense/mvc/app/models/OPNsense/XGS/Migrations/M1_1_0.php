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
use OPNsense\Core\Tunables;
use OPNsense\XGS\XGS;

/**
 * Version 1.1.0 put on the settings page what used to be set by hand under System: Settings:
 * Tunables - IPsec in the engine, its flows, the 136's receive poll - and the flow maker. A box that
 * had them set by hand keeps them: each field is read from the tunable it now writes, so the page
 * opens showing what the box does, and saving it unchanged changes nothing. Without this the page
 * would open at the defaults, and the first save would turn IPsec off in the engine at the next
 * boot of a box that relied on it.
 */
class M1_1_0 extends BaseModelMigration
{
    public function run($model)
    {
        if (!($model instanceof XGS)) {
            return;
        }
        $rows = [];
        foreach ((new Tunables())->item->iterateItems() as $item) {
            $rows[(string)$item->tunable] = trim((string)$item->value);
        }
        $g = $model->general;
        foreach (['hw.octep.ipsec_on', 'hw.npuep.ipsec_on'] as $name) {
            if (isset($rows[$name])) {
                $g->ipsec_offload = $rows[$name] === '1' ? '1' : '0';
            }
        }
        if (isset($rows['hw.octep.ipsec_flows'])) {
            // 1 only: 2 is the clear-text mode that is never set from here
            $g->ipsec_flows = $rows['hw.octep.ipsec_flows'] === '1' ? '1' : '0';
        }
        foreach (['dev.octep.0.dp.auto', 'dev.npuep.0.flow.auto'] as $name) {
            if (isset($rows[$name])) {
                $g->flow_auto = $rows[$name] === '1' ? '1' : '0';
            }
        }
        foreach (['hw.npuep.giu.poll_ms' => 'rx_poll_ms', 'hw.npuep.giu.rx_budget' => 'rx_budget'] as $name => $field) {
            if (isset($rows[$name]) && ctype_digit($rows[$name])) {
                $g->$field = $rows[$name];
            }
        }
    }
}
