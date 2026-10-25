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
    /* Summary cards, as on the Overview: one for each thing a glance is for, in the theme's
       colours (its text and a faint tint of it), so they read the same in both themes. */
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
    /* As on the Ports page: a grid whose columns fill the width shows a one-pixel sideways bar
       when no vertical one takes the difference; wide screens never need to scroll it sideways. */
    @media (min-width: 768px) {
        #grid-associations .tabulator-tableholder, #grid-counters .tabulator-tableholder { overflow-x: hidden; }
    }
</style>

<script>
    $(document).ready(function () {
        /* Laid out as OPNsense's own IPsec pages are: a status table, then the associations and the
           counters in OPNsense's grid, each searchable and sortable. The API escapes what it
           returns; nothing here escapes it again. */
        function yes(on) {
            return '<span class="label label-' + (on ? 'success' : 'default') + '">' +
                (on ? "{{ lang._('yes') }}" : "{{ lang._('no') }}") + '</span>';
        }

        function card(cls, icon, key, value, sub) {
            return '<div class="xgs-card ' + cls + '"><i class="fa ' + icon + '"></i>' +
                '<div class="k">' + key + '</div><div class="v">' + value + '</div>' +
                '<div class="s">' + (sub || '&nbsp;') + '</div></div>';
        }

        function show_state() {
            ajaxGet('/api/xgs/ipsec/get', {}, function (d) {
                d = d || {};
                /* the summary: the engine, what it holds, what it took and what it declined */
                const on = d.tunable && d.tunable.value === '1';
                /* one summary for either family, made by scripts/xgs/ipsec.py: until 2026-10-20 the
                   XGS 136 counted every association offered since boot as held, and 0 installed */
                const sm = d.summary || {};
                const enc = Number(sm.live_encrypt || 0), dec = Number(sm.live_decrypt || 0);
                const refused = Number(sm.declined || 0);
                $('#xgs_cards').html(
                    card(on ? 'ok' : '', 'fa-lock', "{{ lang._('IPsec engine') }}", on ? "{{ lang._('On') }}" : "{{ lang._('Off') }}",
                        on ? "{{ lang._('in the coprocessor') }}" : "{{ lang._('the host encrypts') }}") +
                    card(enc + dec ? 'ok' : '', 'fa-exchange', "{{ lang._('Associations') }}", String(enc + dec),
                        "{{ lang._('encrypt') }}" + ' ' + enc + ', ' + "{{ lang._('decrypt') }}" + ' ' + dec) +
                    card('', 'fa-download', "{{ lang._('Installed') }}",
                        sm.installed === null || sm.installed === undefined ? '-' : Number(sm.installed).toLocaleString(),
                        "{{ lang._('since this boot') }}") +
                    card('', 'fa-ban', "{{ lang._('Not taken') }}",
                        sm.declined === null || sm.declined === undefined ? '-' : refused.toLocaleString(),
                        sm.declined_note === 'other ports included' ?
                            "{{ lang._('offers left to the host, those made to other ports included') }}" :
                            "{{ lang._('offers left to the host') }}"));
                $('#xgs_engine').html(yes(d.tunable && d.tunable.value === '1'));
                /* both families since 2026-10-20: the XGS 136's driver carries tunnel flows too */
                $('#row_flows').show();
                /* what the driver does now; the setting takes effect at the next boot */
                const flowsNow = d.flows_now !== null && d.flows_now !== undefined ? d.flows_now === '1' :
                    d.flows_tunable === '1';
                $('#xgs_flows').html(yes(flowsNow) + ((d.flows_tunable === '1') !== flowsNow ?
                    ' <small class="text-muted">' + "{{ lang._('the boot started it with the other value') }}" + '</small>' : ''));
                if (d.family === 'armada') {
                    $('#row_family').show();
                    $('#row_family_label').text("{{ lang._('Software sessions kept off ccp') }}");
                    $('#xgs_family').html(yes(d.crypto_support === '33554432'));
                }
                $('#xgs_driver').html(d.present ? yes(true) :
                    '<span class="text-warning">' + "{{ lang._('The driver is not loaded, or was started without IPsec.') }}" + '</span>');
            });
        }

        $('#grid-associations').UIBootgrid({
            search: '/api/xgs/ipsec/associations',
            options: {
                selection: false,
                formatters: {
                    direction: function (column, row) {
                        if (!row.direction) {
                            return '';
                        }
                        return '<span class="label label-' + (row.direction === 'encrypt' ? 'info' : 'primary') + '">' +
                            (row.direction === 'encrypt' ? "{{ lang._('encrypt') }}" : "{{ lang._('decrypt') }}") + '</span>';
                    },
                    mono: function (column, row) {
                        return '<code>' + row[column.id] + '</code>';
                    },
                    counted: function (column, row) {
                        if (row.packets === null || row.packets === undefined) {
                            return '';
                        }
                        const b = Number(row.bytes || 0);
                        const size = b >= 1073741824 ? (b / 1073741824).toFixed(2) + ' GB' :
                            (b >= 1048576 ? (b / 1048576).toFixed(1) + ' MB' : (b / 1024).toFixed(0) + ' KB');
                        return '<bdi dir="ltr">' + size + '</bdi>, ' + Number(row.packets).toLocaleString() + ' ' +
                            "{{ lang._('packets') }}" + (row.hw_packets ? ' <small class="text-muted">(' +
                            "{{ lang._('coprocessor') }}" + ' ' + Number(row.hw_packets).toLocaleString() + ')</small>' : '');
                    },
                    engine: function (column, row) {
                        return row.engine === '1' ? "{{ lang._('in the engine') }}" :
                            '<span class="text-muted">' + "{{ lang._('with the host') }}" + '</span>';
                    }
                }
            }
        });

        $('#grid-counters').UIBootgrid({
            search: '/api/xgs/ipsec/counters',
            options: {
                selection: false,
                requestHandler: function (request) {
                    request['zeros'] = $('#show-zero').is(':checked') ? '1' : '0';
                    return request;
                },
                formatters: {
                    mono: function (column, row) {
                        return '<code>' + row[column.id] + '</code>';
                    },
                    number: function (column, row) {
                        return Number(row.value).toLocaleString();
                    }
                }
            }
        });

        $('#show-zero').change(function () {
            $('#grid-counters').bootgrid('reload');
        });

        $('#refreshAct').click(function () {
            show_state();
            $('#grid-associations').bootgrid('reload');
            $('#grid-counters').bootgrid('reload');
        });

        show_state();

        /* the selected tab in the address, as on OPNsense's own tabbed pages */
        if (window.location.hash !== '') {
            $('a[href="' + window.location.hash + '"]').tab('show');
        }
        $('.nav-tabs a').on('shown.bs.tab', function (e) {
            history.pushState(null, null, e.target.hash);
        });
    });
</script>

<ul class="nav nav-tabs" data-tabs="tabs" id="maintabs">
    <li class="active"><a data-toggle="tab" href="#status">{{ lang._('Status') }}</a></li>
    <li><a data-toggle="tab" href="#associations">{{ lang._('Associations in the engine') }}</a></li>
    <li><a data-toggle="tab" href="#counters">{{ lang._('Counters') }}</a></li>
</ul>

<div class="tab-content content-box">
    <div id="status" class="tab-pane active table-responsive">
        <div class="xgs-cards" id="xgs_cards"></div>
        <table class="table table-striped table-condensed">
            <tbody>
                <tr>
                    <td style="width: 30%;">{{ lang._('IPsec in the engine') }}</td>
                    <td id="xgs_engine"></td>
                </tr>
                <tr id="row_flows" style="display: none;">
                    <td>{{ lang._('Outbound tunnel flows forwarded by the coprocessor') }}</td>
                    <td id="xgs_flows"></td>
                </tr>
                <tr id="row_family" style="display: none;">
                    <td id="row_family_label"></td>
                    <td id="xgs_family"></td>
                </tr>
                <tr>
                    <td>{{ lang._('Driver') }}</td>
                    <td id="xgs_driver"></td>
                </tr>
                <tr>
                    <td></td>
                    <td>
                        <button class="btn btn-primary" id="refreshAct" type="button"><i class="fa fa-fw fa-refresh"></i> {{ lang._('Refresh') }}</button>
                    </td>
                </tr>
                <tr>
                    <td colspan="2" class="text-muted">
                        <small>{{ lang._('The tunnels themselves are configured under VPN: IPsec. Whether their associations go to the crypto engine of the coprocessor is set on the Settings page and takes effect at the next reboot; this page shows what the engine holds now.') }}</small>
                    </td>
                </tr>
            </tbody>
        </table>
    </div>

    <div id="associations" class="tab-pane">
        <table id="grid-associations" class="table table-condensed table-hover table-striped table-responsive">
            <thead>
                <tr>
                    <th data-column-id="direction" data-formatter="direction" data-type="string" data-width="8em">{{ lang._('Direction') }}</th>
                    <th data-column-id="spi" data-formatter="mono" data-type="string" data-identifier="true" data-width="10em">SPI</th>
                    <th data-column-id="tunnel" data-type="string">{{ lang._('Tunnel') }}</th>
                    <th data-column-id="port" data-type="string" data-width="7em">{{ lang._('Port') }}</th>
                    <th data-column-id="window" data-type="numeric" data-width="9em">{{ lang._('Replay window') }}</th>
                    <th data-column-id="engine" data-formatter="engine" data-type="string" data-width="9em">{{ lang._('Encrypted by') }}</th>
                    <th data-column-id="counted" data-formatter="counted" data-type="string" data-sortable="false">{{ lang._('Counted') }}</th>
                    <th data-column-id="handle" data-type="string" data-width="6em">{{ lang._('Handle') }}</th>
                    <th data-column-id="note" data-type="string" data-visible="false">{{ lang._('Note') }}</th>
                </tr>
            </thead>
            <tbody></tbody>
            <tfoot></tfoot>
        </table>
    </div>

    <div id="counters" class="tab-pane">
        <div style="padding: 10px 15px 0;">
            <label style="font-weight: normal;">
                <input type="checkbox" id="show-zero"> {{ lang._('show the ones at zero') }}
            </label>
        </div>
        <table id="grid-counters" class="table table-condensed table-hover table-striped table-responsive">
            <thead>
                <tr>
                    <th data-column-id="group" data-type="string" data-width="12em">{{ lang._('Group') }}</th>
                    <th data-column-id="name" data-formatter="mono" data-type="string" data-identifier="true">{{ lang._('Counter') }}</th>
                    <th data-column-id="value" data-formatter="number" data-type="numeric" data-width="10em">{{ lang._('Value') }}</th>
                </tr>
            </thead>
            <tbody></tbody>
            <tfoot></tfoot>
        </table>
    </div>
</div>

{{ partial("OPNsense/XGS/credit") }}
