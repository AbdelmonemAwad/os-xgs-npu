/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 */

/*
 * Dashboard widget: the coprocessor at a glance - whether it answers, whether offloading is on,
 * how many of its front ports have a link, and its temperatures.
 *
 * Everything comes from what the pages already read: /api/xgs/status/get for the driver and the
 * offload gate, /api/xgs/ports/search for the ports (the same rows the Ports page and the XGS Ports
 * widget count, expansion module included), and OPNsense's own sensor list for the temperatures the drivers
 * publish (dev.octep.0.temperature.* on the XGS 3300, dev.npuep.0.temperature on the XGS 136).
 * Nothing is derived here that the page does not show; a line that has nothing to say is left out.
 */

export default class XGSCoprocessor extends BaseTableWidget {
    constructor() {
        super();
        this.tickTimeout = 15;
    }

    getMarkup() {
        const $container = $('<div></div>');
        $container.append(this.createTable('xgsCoprocessorTable', {headerPosition: 'left'}));
        return $container;
    }

    icon(good, text) {
        const cls = good === null ? 'fa-circle-o text-muted' : (good ? 'fa-check-circle text-success' : 'fa-times-circle text-danger');
        return `<i class="fa ${cls}"></i>&nbsp;${text}`;
    }

    async onWidgetTick() {
        const status = await this.ajaxCall('/api/xgs/status/get');
        if (!status || status.present === false) {
            $('#xgsCoprocessorTable').html(`<div class="error-message"><a href="/ui/xgs/status">${this.translations.absent}</a></div>`);
            return;
        }

        let ports = [];
        try {
            const found = await this.ajaxCall('/api/xgs/ports/search', JSON.stringify({current: 1, rowCount: -1}), 'POST');
            ports = (found && Array.isArray(found.rows)) ? found.rows : [];
        } catch (e) {
            ports = [];
        }

        let temperatures = [];
        try {
            temperatures = await this.ajaxCall('/api/diagnostics/system/system_temperature');
        } catch (e) {
            temperatures = [];
        }

        if (!this.dataChanged('xgs-coprocessor', [status, ports, temperatures])) {
            return;
        }

        const driver = status.driver || {};
        const offload = status.offload || {};
        const up = ports.filter(p => p.status === 'up').length;

        const rows = [];
        rows.push([this.translations.coprocessor,
            this.icon(Number(driver.ready) === 1, Number(driver.ready) === 1 ? this.translations.answering : this.translations.silent)]);
        rows.push([this.translations.offloading,
            this.icon(offload.offload_bit_requested === true ? true : null,
                offload.offload_bit_requested === true ? this.translations.on : this.translations.off)]);
        if (ports.length) {
            rows.push([this.translations.ports, `${up} / ${ports.length} ${this.translations.linked}`]);
        }

        /* the coprocessor's own die first, then the air it breathes */
        const names = [
            ['dev.octep.0.temperature.npu', this.translations.npu],
            ['dev.npuep.0.temperature', this.translations.npu],
            ['dev.octep.0.temperature.inlet', this.translations.inlet]
        ];
        const read = Array.isArray(temperatures) ? temperatures : [];
        names.forEach(([device, label]) => {
            const t = read.find(item => item.device === device);
            const value = t ? parseFloat(t.temperature) : NaN;
            if (!isNaN(value)) {
                rows.push([label, `${value.toFixed(1)} &deg;C`]);
            }
        });

        $('#xgsCoprocessorTable .error-message').remove();
        this.updateTable('xgsCoprocessorTable', rows.map(r => [`<a href="/ui/xgs/status">${r[0]}</a>`, r[1]]));
    }
}
