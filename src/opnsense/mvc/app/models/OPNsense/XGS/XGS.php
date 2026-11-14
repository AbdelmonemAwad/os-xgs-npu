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

use OPNsense\Base\BaseModel;

/**
 * No performValidation() override, and that is a decision rather than an omission.
 *
 * A model's job here would be to refuse a coherent-looking setting that cannot work, and there is
 * nothing of that kind to refuse: the field is a switch with two positions, both of which are
 * meaningful on this hardware. What a model must NOT do is ask the coprocessor whether it is ready
 * - the answer would be taken at the moment of a save rather than at the moment of a write, and a
 * save that failed because the driver had not finished bringing twelve ports up would be a
 * mystery, not a diagnosis. The script that applies the setting reports what the hardware said;
 * the page shows it.
 */
class XGS extends BaseModel
{
    /**
     * The recommended settings (2026-11-14): what a new install has, as XGS.xml's defaults, each with
     * whether it waits for a reboot. An appliance installed before keeps what it saved; XGS: Overview
     * lists those of these that differ here, and Apply recommended (Api/SettingsController.php) sets
     * exactly those, on the owner's press. tools/test-recommended.py holds this list to the model's
     * defaults. Each one's condition is said on the page: failclose acts only while the Intrusion
     * Detection runs in IPS mode, inspect_then_hardware does nothing while it is off, offloading
     * keeps the ports it reads in IPS mode with the firewall (the netmap gate).
     */
    public const RECOMMENDED = [
        'offload' => ['value' => '1', 'reboot' => false],
        'flow_auto' => ['value' => '1', 'reboot' => false],
        'cut_in_hardware' => ['value' => '1', 'reboot' => false],
        'flood_cut' => ['value' => '1', 'reboot' => false],
        'cp_hardware' => ['value' => '1', 'reboot' => false],
        'anomaly_watch' => ['value' => '1', 'reboot' => false],
        'inspect_then_hardware' => ['value' => '1', 'reboot' => true],
        'failclose' => ['value' => '1', 'reboot' => false],
        'ipsec_offload' => ['value' => '1', 'reboot' => true],
        'ipsec_flows' => ['value' => '1', 'reboot' => true],
    ];

    /**
     * The recommended settings that differ from $general (field => saved value, as strings), in the
     * order above: [['field', 'now', 'recommended', 'reboot'], ...]. A field with no saved value is
     * taken as its default, which is the recommended one, and does not differ.
     */
    public static function differing(array $general): array
    {
        $out = [];
        foreach (self::RECOMMENDED as $field => $want) {
            $now = array_key_exists($field, $general) ? (string)$general[$field] : $want['value'];
            if ($now !== $want['value']) {
                $out[] = ['field' => $field, 'now' => $now, 'recommended' => $want['value'],
                    'reboot' => $want['reboot']];
            }
        }
        return $out;
    }

    /**
     * The driver's IPsec loader tunables where no row sets them (2026-11-14): the family's ipsec_on
     * and ipsec_flows rows of System: Settings: Tunables, from $model's ipsec_offload and ipsec_flows
     * - on a new install their defaults, on. The driver reads them when it loads (tunables-kenv.sh
     * puts the rows in its environment first), and until now only scripts/xgs/apply.php wrote them, at
     * the owner's first Apply: a new install showed IPsec in the engine on and ran without it. A row
     * already there - the owner's, or apply.php's - is left as it is. Called by the migration to 1.3.0
     * on a new install, and by scripts/xgs/defaults.php at the first boot after a factory reset; it
     * serializes the Tunables model, and the caller saves the configuration. Returns name => value for
     * each row written, [] when none was.
     */
    public static function ipsecTunablesWhereNone(XGS $model, string $family): array
    {
        $prefix = ['octeontx' => 'hw.octep.', 'armada' => 'hw.npuep.'][$family] ?? null;
        if ($prefix === null) {
            return [];
        }
        $want = [
            $prefix . 'ipsec_on' => (string)$model->general->ipsec_offload === '1' ? '1' : '0',
            $prefix . 'ipsec_flows' => (string)$model->general->ipsec_flows === '1' ? '1' : '0',
        ];
        $tunables = new \OPNsense\Core\Tunables();
        foreach ($tunables->item->iterateItems() as $item) {
            unset($want[(string)$item->tunable]);
        }
        foreach ($want as $name => $value) {
            $row = $tunables->item->Add();
            $row->tunable = $name;
            $row->value = $value;
            $row->descr = 'XGS coprocessor - set on its settings page';
        }
        if (count($want)) {
            $tunables->serializeToConfig();
        }
        return $want;
    }
}
