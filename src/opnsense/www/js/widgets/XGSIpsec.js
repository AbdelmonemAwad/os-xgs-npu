/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 */

/*
 * Dashboard widget: the coprocessor's IPsec engine - whether it is switched on, how many
 * associations it holds now, and what it has encrypted, decrypted and turned down since the driver
 * loaded. Read from /api/xgs/ipsec/get, the same counters the IPsec page groups.
 */

export default class XGSIpsec extends BaseTableWidget {
    constructor() {
        super();
        this.tickTimeout = 15;
    }

    getMarkup() {
        const $container = $('<div></div>');
        $container.append(this.createTable('xgsIpsecTable', {headerPosition: 'left'}));
        return $container;
    }

    async onWidgetTick() {
        const d = await this.ajaxCall('/api/xgs/ipsec/get');
        if (!d || d.present === false) {
            $('#xgsIpsecTable').html(`<div class="error-message"><a href="/ui/xgs/ipsec">${this.translations.absent}</a></div>`);
            return;
        }
        if (!this.dataChanged('xgs-ipsec', d)) {
            return;
        }
        // one summary for either family (scripts/xgs/ipsec.py): the XGS 136's counters have other
        // names, and until 2026-10-20 this widget showed it 0 associations and 0 encrypted
        const sm = d.summary || {};
        const on = String((d.tunable || {}).value || '0') === '1';
        const held = Number(sm.live_encrypt || 0) + Number(sm.live_decrypt || 0);
        const fmt = v => (v === null || v === undefined) ? '-' : Number(v).toLocaleString();

        const rows = [
            [this.translations.engine, on
                ? `<i class="fa fa-check-circle text-success"></i>&nbsp;${this.translations.on}`
                : `<i class="fa fa-circle-o text-muted"></i>&nbsp;${this.translations.off}`],
            [this.translations.associations, fmt(held)],
            [this.translations.encrypted, fmt(sm.encrypted)],
            [this.translations.decrypted, fmt(sm.decrypted)],
            [this.translations.refused, fmt(sm.declined)]
        ];
        $('#xgsIpsecTable .error-message').remove();
        this.updateTable('xgsIpsecTable', rows.map(r => [`<a href="/ui/xgs/ipsec">${r[0]}</a>`, r[1]]));
    }
}
