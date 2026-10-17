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
        const c = d.counters || {};
        const n = name => Number(c[name] || 0);
        const on = String((d.tunable || {}).value || '0') === '1';
        const held = Math.max(0, n('sa_installed') - n('sa_removed'));
        const fmt = v => Number(v).toLocaleString();

        const rows = [
            [this.translations.engine, on
                ? `<i class="fa fa-check-circle text-success"></i>&nbsp;${this.translations.on}`
                : `<i class="fa fa-circle-o text-muted"></i>&nbsp;${this.translations.off}`],
            [this.translations.associations, fmt(held)],
            [this.translations.encrypted, fmt(n('tx_encrypt'))],
            [this.translations.decrypted, fmt(n('rx_done'))],
            [this.translations.refused, fmt(n('sa_refused'))]
        ];
        $('#xgsIpsecTable .error-message').remove();
        this.updateTable('xgsIpsecTable', rows.map(r => [`<a href="/ui/xgs/ipsec">${r[0]}</a>`, r[1]]));
    }
}
