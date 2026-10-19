{#
 # Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions are met:
 #
 # 1. Redistributions of source code must retain the above copyright notice,
 #    this list of conditions and the following disclaimer.
 #
 # 2. Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #
 # THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 # INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 # AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 # AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 # OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 # SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 # INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 # CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 # ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 # POSSIBILITY OF SUCH DAMAGE.
 #}

<style>
    /* Summary cards, as on the other coprocessor pages. */
    .xgs-cards { display: flex; flex-wrap: wrap; gap: 12px; padding: 15px; }
    .xgs-card { flex: 1 1 200px; min-width: 190px; border: 1px solid rgba(128, 128, 128, 0.25);
                border-inline-start: 4px solid #9e9e9e; border-radius: 3px; padding: 10px 14px;
                background-color: rgba(128, 128, 128, 0.04); }
    .xgs-card.ok { border-inline-start-color: #5cb85c; }
    .xgs-card.warn { border-inline-start-color: #f0ad4e; }
    .xgs-card.bad { border-inline-start-color: #d9534f; }
    .xgs-card .k { font-size: 11px; text-transform: uppercase; letter-spacing: .04em; opacity: 0.6; }
    .xgs-card .v { font-size: 19px; font-weight: 600; line-height: 1.3; margin: 2px 0; }
    .xgs-card .s { font-size: 12px; opacity: 0.7; }
    .xgs-card .fa { opacity: 0.35; float: inline-end; font-size: 18px; margin-top: 2px; }
    .xgs-read { padding: 0 15px 10px; }
    @media (min-width: 768px) {
        #grid-transceivers .tabulator-tableholder { overflow-x: hidden; }
    }
</style>

<script>
    $(document).ready(function () {
        /* A receive power below this is worth a look on any of these modules: the weakest the
           10GBASE-SR standard has a receiver accept is -11.1 dBm, and a module that reads no light
           at all shows -inf. Shown as a colour, never acted on. */
        const RX_LOW_DBM = -11.1;

        function plain(value) {
            return $('<textarea/>').html(value === null || value === undefined ? '' : String(value)).text();
        }

        function card(cls, icon, key, value, sub) {
            return $('<div class="xgs-card"/>').addClass(cls)
                .append($('<i class="fa"/>').addClass(icon))
                .append($('<div class="k"/>').text(plain(key)))
                .append($('<div class="v"/>').text(plain(value)))
                .append($('<div class="s"/>').text(plain(sub || ' ')));
        }

        function rxClass(row) {
            if (row.rx === '') {
                return '';
            }
            const v = row.rx === '-inf' ? -Infinity : parseFloat(row.rx);
            return v < RX_LOW_DBM ? 'text-danger' : 'text-success';
        }

        $('#grid-transceivers').UIBootgrid({
            search: '/api/xgs/transceivers/search',
            options: {
                selection: false,
                formatters: {
                    port: function (column, row) {
                        return '<b>' + row.port + '</b>';
                    },
                    module: function (column, row) {
                        if (row.present !== '1') {
                            return '<span class="text-muted">' + "{{ lang._('empty') }}" + '</span>';
                        }
                        return [row.vendor, row.part].filter(Boolean).join(' ') +
                            (row.type ? ' <small class="text-muted">' + row.type + '</small>' : '');
                    },
                    medium: function (column, row) {
                        if (row.medium === '') {
                            return '';
                        }
                        const by = {
                            'part number': "{{ lang._('from its part number') }}",
                            'connector': "{{ lang._('from its connector') }}"
                        }[row.medium_by];
                        return row.medium + (by ? ' <small class="text-muted">' + by + '</small>' : '');
                    },
                    temp: function (column, row) {
                        return row.temp !== '' ? row.temp + ' &deg;C' : '';
                    },
                    power: function (column, row) {
                        const value = row[column.id];
                        if (value === '') {
                            return row.present === '1' && row.diagnostics !== '1' && column.id === 'rx'
                                ? '<span class="text-muted">' + "{{ lang._('no diagnostics') }}" + '</span>' : '';
                        }
                        const text = value === '-inf' ? "{{ lang._('no light') }}" : value + ' dBm';
                        return column.id === 'rx' ? '<span class="' + rxClass(row) + '">' + text + '</span>' : text;
                    }
                }
            }
        });

        function draw(data) {
            const modules = (data && Array.isArray(data.modules)) ? data.modules : [];
            const present = modules.filter(function (m) { return m.present; });
            const reading = present.filter(function (m) { return m.dom; });
            let hottest = null;
            let weakest = null;
            reading.forEach(function (m) {
                if (typeof m.dom.temp_c === 'number' && (hottest === null || m.dom.temp_c > hottest.dom.temp_c)) {
                    hottest = m;
                }
                const rx = (m.dom.rx_dbm === null || m.dom.rx_dbm === undefined) ? -Infinity : m.dom.rx_dbm;
                if (weakest === null || rx < weakest.rx) {
                    weakest = {port: m.port, rx: rx};
                }
            });
            $('#xgs_cards').empty()
                .append(card(present.length ? 'ok' : '', 'fa-plug', "{{ lang._('Modules') }}",
                    present.length + ' / ' + modules.length, "{{ lang._('cages with a module') }}"))
                .append(card(reading.length ? 'ok' : '', 'fa-heartbeat', "{{ lang._('Diagnostics') }}",
                    String(reading.length), "{{ lang._('modules that report them') }}"))
                .append(card(hottest === null ? '' : (hottest.dom.temp_c >= 70 ? 'warn' : 'ok'), 'fa-thermometer-half',
                    "{{ lang._('Warmest module') }}", hottest === null ? '-' : hottest.dom.temp_c.toFixed(1) + ' °C',
                    hottest === null ? '' : hottest.port))
                .append(card(weakest === null ? '' : (weakest.rx < RX_LOW_DBM ? 'warn' : 'ok'), 'fa-signal',
                    "{{ lang._('Weakest light received') }}",
                    weakest === null ? '-' : (weakest.rx === -Infinity ? "{{ lang._('no light') }}" : weakest.rx.toFixed(2) + ' dBm'),
                    weakest === null ? '' : weakest.port));
            const when = data && data.at ? new Date(data.at * 1000).toLocaleString() : '';
            $('#xgs_read').text(plain(when ? "{{ lang._('Read on the coprocessor at') }}" + ' ' + when : "{{ lang._('Not read yet.') }}") +
                (data && data.error ? ' - ' + plain(data.error) : ''));
        }

        function load() {
            ajaxGet('/api/xgs/transceivers/get', {}, draw);
        }

        $('#refreshAct').click(function () {
            const $icon = $(this).find('i').addClass('fa-spin');
            $(this).prop('disabled', true);
            ajaxCall('/api/xgs/transceivers/refresh', {}, function (data) {
                $icon.removeClass('fa-spin');
                $('#refreshAct').prop('disabled', false);
                draw(data);
                $('#grid-transceivers').bootgrid('reload');
            });
        });

        load();
    });
</script>

<div class="tab-content content-box">
    <div class="xgs-cards" id="xgs_cards"></div>
    <div class="xgs-read text-muted">
        <span id="xgs_read">&hellip;</span>
        <button id="refreshAct" class="btn btn-xs btn-default pull-right" type="button" title="{{ lang._('Read again now') }}">
            <i class="fa fa-fw fa-refresh"></i> {{ lang._('Read again now') }}
        </button>
    </div>
    <table id="grid-transceivers" class="table table-bordered table-condensed table-hover table-striped table-responsive">
        <thead>
            <tr>
                <th data-column-id="port" data-identifier="true" data-formatter="port" data-type="string" data-width="6em">{{ lang._('Port') }}</th>
                <th data-column-id="module" data-formatter="module" data-type="string">{{ lang._('Module') }}</th>
                <th data-column-id="medium" data-formatter="medium" data-type="string" data-width="12em">{{ lang._('Medium') }}</th>
                <th data-column-id="serial" data-type="string" data-visible="false">{{ lang._('Serial number') }}</th>
                <th data-column-id="rate" data-type="string" data-visible="false">{{ lang._('Signalling rate (MBd)') }}</th>
                <th data-column-id="temp" data-formatter="temp" data-type="string" data-width="7em">{{ lang._('Temperature') }}</th>
                <th data-column-id="vcc" data-type="string" data-width="6em">{{ lang._('Voltage (V)') }}</th>
                <th data-column-id="bias" data-type="string" data-width="7em">{{ lang._('Bias (mA)') }}</th>
                <th data-column-id="tx" data-formatter="power" data-type="string" data-width="8em">{{ lang._('Transmit') }}</th>
                <th data-column-id="rx" data-formatter="power" data-type="string" data-width="9em">{{ lang._('Receive') }}</th>
            </tr>
        </thead>
        <tbody></tbody>
        <tfoot></tfoot>
    </table>
    <div class="help-block" style="padding: 0 15px 15px;">
        {{ lang._('Read on the coprocessor every five minutes, from the modules\' own memory: their identity, and where a module reports them its temperature, supply voltage, laser bias and the light it sends and receives. A received light below -11.1 dBm - the weakest a 10GBASE-SR receiver is meant to accept - is shown in red.') }}
        {{ lang._('The medium is what the port reports to the system, on the Interfaces pages and in ifconfig, and it is taken from here: from the module\'s own codes, or, where a copper module claims a fibre standard - some 10GBASE-T modules do - from its part number.') }}
    </div>
</div>

{{ partial("OPNsense/XGS/credit") }}
