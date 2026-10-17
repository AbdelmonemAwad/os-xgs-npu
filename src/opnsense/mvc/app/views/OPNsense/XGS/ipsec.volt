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

        function show_state() {
            ajaxGet('/api/xgs/ipsec/get', {}, function (d) {
                d = d || {};
                $('#xgs_engine').html(yes(d.tunable && d.tunable.value === '1'));
                if (d.family === 'octeontx') {
                    $('#row_family').show();
                    $('#row_family_label').text("{{ lang._('Outbound tunnel flows forwarded by the coprocessor') }}");
                    $('#xgs_family').html(yes(d.flows_tunable === '1'));
                } else if (d.family === 'armada') {
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
        <table class="table table-striped table-condensed">
            <tbody>
                <tr>
                    <td style="width: 30%;">{{ lang._('IPsec in the engine') }}</td>
                    <td id="xgs_engine"></td>
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
