/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 */

/*
 * Dashboard widget: the coprocessor's front ports as a row of tiles in the order the Ports page
 * lists them - green with its speed where a port has a link, grey where it has none - each with
 * the name it is assigned under. A tile opens the Ports page.
 *
 * Read from /api/xgs/ports/search, the same rows the Ports page's grid shows.
 */

export default class XGSPorts extends BaseWidget {
    constructor() {
        super();
        this.tickTimeout = 15;
    }

    getMarkup() {
        return $(`
            <div class="xgs-port-tiles" id="xgsPortTiles"></div>
            <style>
                .xgs-port-tiles { display: flex; flex-wrap: wrap; gap: 6px; padding: 6px 4px; }
                .xgs-port-tile { flex: 0 0 74px; border: 1px solid rgba(128,128,128,.3); border-radius: 3px;
                                 border-top: 3px solid #9e9e9e; padding: 4px 6px; text-align: center;
                                 color: inherit; text-decoration: none; background-color: rgba(128,128,128,.04); }
                .xgs-port-tile.up { border-top-color: #5cb85c; }
                .xgs-port-tile:hover { text-decoration: none; background-color: rgba(128,128,128,.1); }
                .xgs-port-tile .p { font-weight: 600; font-size: 13px; }
                .xgs-port-tile .s { font-size: 11px; opacity: .75; }
                .xgs-port-tile .n { font-size: 10px; opacity: .6; white-space: nowrap; overflow: hidden;
                                    text-overflow: ellipsis; }
            </style>
        `);
    }

    async onWidgetTick() {
        const data = await this.ajaxCall('/api/xgs/ports/search', JSON.stringify({current: 1, rowCount: -1}), 'POST');
        const rows = (data && Array.isArray(data.rows)) ? data.rows : [];
        if (!this.dataChanged('xgs-ports', rows)) {
            return;
        }
        const $tiles = $('#xgsPortTiles').empty();
        if (!rows.length) {
            $tiles.append($('<a href="/ui/xgs/ports"/>').text(this.translations.noports));
            return;
        }
        rows.forEach(r => {
            const up = r.status === 'up';
            $tiles.append($('<a class="xgs-port-tile" href="/ui/xgs/ports"/>')
                .toggleClass('up', up)
                .attr('title', [r.port, r.assigned, r.media].filter(Boolean).join(' - '))
                .append($('<div class="p"/>').text(r.port))
                .append($('<div class="s"/>').text(up ? (r.speed || this.translations.up) : this.translations.down))
                .append($('<div class="n"/>').text(r.assigned || ' ')));
        });
    }
}
