<?php

/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 */

namespace OPNsense\RRD\Types;

/**
 * The connections the XGS coprocessor forwards by itself, for Reporting: Health, in
 * /var/db/rrd/system-xgs-flows.rrd: listed under System as "xgs-flows", described by
 * scripts/health/definitions/system-xgs-flows.xml. A file of its own because its scale (up to two
 * million) has nothing to do with a load or a percentage. Its figure comes from the same
 * collector as the load (Stats/XGSCoprocessor.php), and only where a driver counts it.
 */
class XGSFlows extends Base
{
    protected int $ds_heartbeat = 120;
    protected int $ds_min = 0;
    protected int $ds_max = 100000000;
    protected static string $stdfilename = 'system-xgs-flows.rrd';

    public function __construct(string $filename)
    {
        parent::__construct($filename);
        $this->addDataset('flows', 'GAUGE');
    }

    public static function wantsStats()
    {
        return 'XGSCoprocessor';
    }

    public static function payloadSplitter(array $payload)
    {
        if (isset($payload['flows'])) {
            yield static::$basedir . static::$stdfilename => $payload;
        }
    }
}
