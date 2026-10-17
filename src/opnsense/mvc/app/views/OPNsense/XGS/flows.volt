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
    .xgs-card .k { font-size: 11px; text-transform: uppercase; letter-spacing: .04em; opacity: 0.6; }
    .xgs-card .v { font-size: 19px; font-weight: 600; line-height: 1.3; margin: 2px 0; }
    .xgs-card .s { font-size: 12px; opacity: 0.7; }
    .xgs-card .fa { opacity: 0.35; float: inline-end; font-size: 18px; margin-top: 2px; }
    @media (min-width: 768px) {
        #grid-flows .tabulator-tableholder { overflow-x: hidden; }
    }
</style>

<script>
    $(document).ready(function () {
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

        $('#grid-flows').UIBootgrid({
            search: '/api/xgs/flows/search',
            options: {
                selection: false,
                formatters: {
                    connection: function (column, row) {
                        return '<code>' + row.source + '</code> &rarr; <code>' + row.destination + '</code>';
                    },
                    ipsec: function (column, row) {
                        if (!row.ipsec) {
                            return '';
                        }
                        return '<i class="fa fa-lock text-success"></i> ' + row.ipsec.split(',').map(function (d) {
                            return d === 'out' ? "{{ lang._('encrypted out') }}" : "{{ lang._('decrypted in') }}";
                        }).join(', ');
                    },
                    state: function (column, row) {
                        return row.state === 'active'
                            ? '<i class="fa fa-bolt text-success"></i> ' + "{{ lang._('in hardware') }}"
                            : '<span class="text-muted">' + row.state + '</span>';
                    }
                }
            }
        });

        function load() {
            ajaxGet('/api/xgs/flows/get', {}, function (data) {
                const flows = (data && Array.isArray(data.flows)) ? data.flows : [];
                const counters = (data && data.counters) || {};
                const sealed = flows.filter(function (f) { return (f.ipsec || []).length; }).length;
                const made = counters.made !== undefined ? counters.made : (counters.flows !== undefined ? counters.flows : '-');
                $('#xgs_cards').empty()
                    .append(card(flows.length ? 'ok' : '', 'fa-bolt', "{{ lang._('In hardware now') }}",
                        String(flows.length), "{{ lang._('connections the coprocessor forwards by itself') }}"))
                    .append(card(data && data.auto ? 'ok' : '', 'fa-magic', "{{ lang._('Flow maker') }}",
                        data && data.auto ? "{{ lang._('on') }}" : "{{ lang._('off') }}",
                        "{{ lang._('hands settled connections to the coprocessor') }}"))
                    .append(card(sealed ? 'ok' : '', 'fa-lock', "{{ lang._('Through IPsec') }}", String(sealed),
                        "{{ lang._('with their association in hardware') }}"))
                    .append(card('', 'fa-history', "{{ lang._('Taken back') }}",
                        String(counters.gone !== undefined ? counters.gone : (counters.flow_reclaimed || 0)),
                        "{{ lang._('since the driver loaded') }}"));
                $('#xgs_note').text(flows.length ? '' : plain("{{ lang._('No connection is forwarded by the coprocessor right now. With the flow maker on, a connection the firewall has let through is handed to it once it has settled.') }}"));
            });
        }

        $('#refreshAct').click(function () {
            load();
            $('#grid-flows').bootgrid('reload');
        });

        load();
    });
</script>

<div class="tab-content content-box">
    <div class="xgs-cards" id="xgs_cards"></div>
    <div style="padding: 0 15px 10px;" class="text-muted">
        <span id="xgs_note"></span>
        <button id="refreshAct" class="btn btn-xs btn-default pull-right" type="button" title="{{ lang._('Refresh') }}">
            <i class="fa fa-fw fa-refresh"></i> {{ lang._('Refresh') }}
        </button>
    </div>
    <table id="grid-flows" class="table table-bordered table-condensed table-hover table-striped table-responsive">
        <thead>
            <tr>
                <th data-column-id="id" data-identifier="true" data-type="numeric" data-width="5em">{{ lang._('Slot') }}</th>
                <th data-column-id="protocol" data-type="string" data-width="6em">{{ lang._('Protocol') }}</th>
                <th data-column-id="connection" data-formatter="connection" data-sortable="false" data-type="string">{{ lang._('Connection') }}</th>
                <th data-column-id="source" data-type="string" data-visible="false">{{ lang._('Source') }}</th>
                <th data-column-id="destination" data-type="string" data-visible="false">{{ lang._('Destination') }}</th>
                <th data-column-id="port" data-type="string" data-width="7em">{{ lang._('Arrives on') }}</th>
                <th data-column-id="nat" data-type="string" data-width="6em">{{ lang._('NAT') }}</th>
                <th data-column-id="ipsec" data-formatter="ipsec" data-type="string">{{ lang._('IPsec') }}</th>
                <th data-column-id="state" data-formatter="state" data-type="string" data-width="9em">{{ lang._('State') }}</th>
                <th data-column-id="punts" data-type="numeric" data-width="6em" data-visible="false">{{ lang._('Handed back') }}</th>
            </tr>
        </thead>
        <tbody></tbody>
        <tfoot></tfoot>
    </table>
</div>

{{ partial("OPNsense/XGS/credit") }}
