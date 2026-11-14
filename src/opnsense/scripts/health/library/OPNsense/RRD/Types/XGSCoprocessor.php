<?php

/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 */

namespace OPNsense\RRD\Types;

/**
 * The XGS coprocessor's load and memory in use (per cent) for Reporting: Health, in
 * /var/db/rrd/system-xgs-coprocessor.rrd: listed under System as "xgs-coprocessor", described by
 * scripts/health/definitions/system-xgs-coprocessor.xml. Only on a machine whose coprocessor has
 * been read (Stats/XGSCoprocessor.php).
 */
class XGSCoprocessor extends Base
{
    protected int $ds_heartbeat = 120;
    protected int $ds_min = 0;
    protected int $ds_max = 100000;
    protected static string $stdfilename = 'system-xgs-coprocessor.rrd';

    public function __construct(string $filename)
    {
        parent::__construct($filename);
        $this->addDatasets(['load', 'memory'], 'GAUGE');
    }

    public static function payloadSplitter(array $payload)
    {
        if (isset($payload['load']) || isset($payload['memory'])) {
            yield static::$basedir . static::$stdfilename => $payload;
        }
    }
}
