/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 */

/*
 * Dashboard widget: the connections the coprocessor forwards by itself - how many are in hardware
 * against what the table holds, the next hops they use, and what came and went: taken back by the
 * host, revived in place, refused because a table was full. Read from /api/xgs/flows/summary, the
 * figures without the table, so asking every fifteen seconds costs the driver nothing.
 *
 * No throughput here on purpose: a forwarded connection never reaches the host, so the host's own
 * counters do not see it, and a figure from them would be the traffic the offload did NOT carry.
 */

export default class XGSFlows extends BaseTableWidget {
    constructor() {
        super();
        this.tickTimeout = 15;
    }

    getMarkup() {
        const $container = $('<div></div>');
        $container.append(this.createTable('xgsFlowsTable', {headerPosition: 'left'}));
        return $container;
    }

    async onWidgetTick() {
        const d = await this.ajaxCall('/api/xgs/flows/summary');
        if (!d || !d.family) {
            $('#xgsFlowsTable').html(`<div class="error-message"><a href="/ui/xgs/flows">${this.translations.absent}</a></div>`);
            return;
        }
        if (!this.dataChanged('xgs-flows', d)) {
            return;
        }
        const s = d.summary || {};
        const fmt = v => Number(v).toLocaleString();
        const rows = [];
        rows.push([this.translations.auto, d.auto
            ? `<i class="fa fa-check-circle text-success"></i>&nbsp;${this.translations.on}`
            : `<i class="fa fa-circle-o text-muted"></i>&nbsp;${this.translations.off}`]);
        if (s.in_hardware !== undefined) {
            let cell = `<bdi dir="ltr">${fmt(s.in_hardware)}</bdi>`;
            if (s.max) {
                const pct = Math.max(0, Math.min(100, s.in_hardware / s.max * 100));
                cell = `<bdi dir="ltr">${fmt(s.in_hardware)} / ${fmt(s.max)}</bdi>` +
                    `<div style="height: 5px; border-radius: 3px; margin-top: 4px; background-color: rgba(128, 128, 128, 0.2);">` +
                    `<div style="height: 5px; border-radius: 3px; min-width: 2px; width: ${pct.toFixed(1)}%; ` +
                    `background-color: var(--xgs-signal, #31708f);"></div></div>`;
            }
            rows.push([this.translations.inhardware, cell]);
        }
        [['next_hops', 'nexthops'], ['made', 'made'], ['taken_back', 'takenback'], ['revived', 'revived'],
         ['refused_full', 'full']].forEach(([key, label]) => {
            if (s[key] !== undefined) {
                rows.push([this.translations[label], fmt(s[key])]);
            }
        });
        $('#xgsFlowsTable .error-message').remove();
        this.updateTable('xgsFlowsTable', rows.map(r => [`<a href="/ui/xgs/flows">${r[0]}</a>`, r[1]]));
    }
}
