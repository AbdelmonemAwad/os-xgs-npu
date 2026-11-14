<?php

/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 */

namespace OPNsense\XGS\Migrations;

use OPNsense\Base\BaseModelMigration;
use OPNsense\Core\Config;
use OPNsense\XGS\XGS;

/**
 * Version 1.3.0: the recommended settings are the defaults of a new install (2026-11-14), and an
 * installed appliance keeps what it saved. Nothing saved is changed here.
 *
 * Two fields are new and on by default: failclose, "Drop when the inspection stops", and flood_cut,
 * the cut in hardware of what pf's overload tables catch. An appliance that had this model's
 * settings before has no value for them, and would read the new defaults - a frame of an inspected
 * connection dropped while nothing reads oxips0, and a flooding source cut on the coprocessor, where
 * until now neither happened. So such an appliance is given 0 for both, what it did; XGS: Overview
 * offers them among the recommended settings, and only its owner turns them on.
 *
 * A new install has no settings of this model yet (the node is not in config.xml when the
 * migrations run), and keeps the defaults. Of those, IPsec in the engine and its tunnel flows reach
 * the driver only as loader tunables, which apply.php wrote at the owner's first Apply: until then
 * the page showed them on and the driver ran without them. So a new install has their rows written
 * here, where no row sets them (XGS::ipsecTunablesWhereNone), and the driver has them from its next
 * load; run_migrations.php saves the configuration after the migrations.
 */
class M1_3_0 extends BaseModelMigration
{
    public function run($model)
    {
        if (!($model instanceof XGS)) {
            return;
        }
        $cfg = Config::getInstance()->object();
        if (isset($cfg->OPNsense->XGS->general)) {
            $model->general->failclose = '0';
            $model->general->flood_cut = '0';
            return;
        }
        $board = trim((string)shell_exec('/bin/sh /usr/local/opnsense/scripts/xgs/board.sh 2>/dev/null'));
        XGS::ipsecTunablesWhereNone($model, explode(' ', $board)[1] ?? '');
    }
}
