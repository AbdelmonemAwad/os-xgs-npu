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
    /* The grid's columns fill the width exactly and its cells start one pixel in, so with no
       vertical scrollbar to take the difference a one-pixel horizontal bar appears under the
       last row. Wide screens never need to scroll this table sideways; narrow ones still can. */
    @media (min-width: 768px) {
        #grid-ports .tabulator-tableholder { overflow-x: hidden; }
    }
</style>

<script>
    $(document).ready(function () {
        /* OPNsense's own grid, the one Interfaces: Overview uses, so the page searches, sorts,
           chooses columns and exports like every other list in the GUI. The API escapes what it
           returns; nothing here escapes it again. */
        $('#grid-ports').UIBootgrid({
            search: '/api/xgs/ports/search',
            options: {
                selection: false,
                formatters: {
                    status: function (column, row) {
                        const up = row.status === 'up';
                        return '<i class="fa fa-plug ' + (up ? 'text-success' : 'text-danger') + '" title="' +
                            (up ? "{{ lang._('up') }}" : "{{ lang._('down') }}") + '" data-toggle="tooltip"></i>';
                    },
                    port: function (column, row) {
                        return '<b>' + row.port + '</b>' + (row.bay ? ' <small class="text-muted">' +
                            "{{ lang._('bay') }}" + ' ' + row.bay + '</small>' : '');
                    },
                    assigned: function (column, row) {
                        return row.assigned ? row.assigned + ' <small class="text-muted">(' + row.key + ')</small>' :
                            '<span class="text-muted">' + "{{ lang._('not assigned') }}" + '</span>';
                    },
                    speed: function (column, row) {
                        return row.status === 'up' ? (row.speed || "{{ lang._('up') }}") : '';
                    }
                }
            }
        });

        function card(cls, icon, key, value, sub) {
            return '<div class="xgs-card ' + cls + '"><i class="fa ' + icon + '"></i>' +
                '<div class="k">' + key + '</div><div class="v">' + value + '</div>' +
                '<div class="s">' + (sub || '&nbsp;') + '</div></div>';
        }

        ajaxGet('/api/xgs/ports/get', {}, function (data) {
            const slots = (data && data.module && data.module.slots) || {};
            /* the summary: link, assignment, the expansion module, the switch chip */
            const plist = (data && data.ports) || [];
            const up = plist.filter(function (p) { return String(p.status || '').indexOf('active') === 0; }).length;
            const assigned = plist.filter(function (p) { return p.assigned && (p.assigned.key || p.assigned.descr); }).length;
            const fitted = Object.keys(slots);
            const swText = (data && data.switch) || '';
            $('#xgs_cards').html(
                card(up > 0 ? 'ok' : 'warn', 'fa-plug', "{{ lang._('Front ports') }}", up + ' / ' + plist.length,
                    "{{ lang._('with link') }}") +
                card(assigned > 0 ? 'ok' : 'warn', 'fa-tags', "{{ lang._('Assigned') }}", assigned + ' / ' + plist.length,
                    "{{ lang._('in Interfaces: Assignments') }}") +
                card(fitted.length ? 'ok' : '', 'fa-th-large', "{{ lang._('Expansion module') }}",
                    fitted.length ? (slots[fitted[0]]['assembly.partno'] || '?') : "{{ lang._('none fitted') }}",
                    fitted.length ? "{{ lang._('bay') }}" + ' ' + fitted.join(', ') : '') +
                card(swText.indexOf('10,240') >= 0 ? 'ok' : '', 'fa-sitemap', "{{ lang._('Switch chip') }}",
                    swText.indexOf('10,240') >= 0 ? "{{ lang._('Jumbo frames') }}" : "{{ lang._('2,048 bytes') }}",
                    swText ? swText.split(' ').slice(0, 3).join(' ') : "{{ lang._('set at the end of each boot') }}"));
            const bays = Object.keys(slots).map(function (bay) {
                const s = slots[bay];
                return "{{ lang._('bay') }}" + ' ' + bay + ': ' + (s['assembly.partno'] || '?') +
                    (s['assembly.revision'] ? ' rev ' + s['assembly.revision'] : '');
            });
            $('#xgs_module').html(bays.length ? bays.join('<br/>') :
                '<span class="text-muted">' + "{{ lang._('none fitted') }}" + '</span>');
            const sw = (data && data.switch) || '';
            $('#xgs_switch').html(sw ? (sw.indexOf('10,240') >= 0 ?
                '<i class="fa fa-check text-success"></i> ' + "{{ lang._('jumbo, 10,240 bytes') }}" :
                "{{ lang._('2,048 bytes') }}") + ' <small class="text-muted">' + sw + '</small>' :
                '<span class="text-muted">' + "{{ lang._('set at the end of each boot') }}" + '</span>');
        });
    });
</script>

<div class="tab-content content-box">
    <div class="xgs-cards" id="xgs_cards"></div>
    <table id="grid-ports" class="table table-bordered table-condensed table-hover table-striped table-responsive">
        <thead>
            <tr>
                <th data-column-id="status" data-width="5em" data-formatter="status" data-type="string">{{ lang._('Status') }}</th>
                <th data-column-id="port" data-formatter="port" data-type="string">{{ lang._('Port') }}</th>
                <th data-column-id="assigned" data-formatter="assigned" data-type="string">{{ lang._('Assigned as') }}</th>
                <th data-column-id="address" data-type="string">{{ lang._('Address') }}</th>
                <th data-column-id="speed" data-formatter="speed" data-type="string" data-width="6em">{{ lang._('Speed') }}</th>
                <th data-column-id="media" data-type="string" data-visible="false">{{ lang._('Media') }}</th>
                <th data-column-id="mtu" data-type="string" data-width="6em">{{ lang._('MTU') }}</th>
                <th data-column-id="bridge" data-type="string" data-width="7em">{{ lang._('Bridge') }}</th>
                <th data-column-id="device" data-identifier="true" data-type="string" data-width="7em">{{ lang._('Device') }}</th>
            </tr>
        </thead>
        <tbody></tbody>
        <tfoot></tfoot>
    </table>
</div>

<div class="content-box" style="margin-top: 15px;">
    <table class="table table-striped opnsense_standard_table_form">
        <colgroup><col class="col-md-3"/><col class="col-md-9"/></colgroup>
        <tbody>
            <tr>
                <td><i class="fa fa-info-circle fa-fw text-muted"></i> <b>{{ lang._('Expansion module') }}</b></td>
                <td id="xgs_module">&hellip;</td>
            </tr>
            <tr>
                <td><i class="fa fa-info-circle fa-fw text-muted"></i> <b>{{ lang._('Switch chip') }}</b></td>
                <td id="xgs_switch">&hellip;</td>
            </tr>
        </tbody>
    </table>
</div>

{{ partial("OPNsense/XGS/credit") }}
