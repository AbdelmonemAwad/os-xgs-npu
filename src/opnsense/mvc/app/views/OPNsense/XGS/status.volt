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
    /* the heading of a section, as OPNsense's base_form draws one */
    .xgs-head { padding-top: 5px; padding-bottom: 5px; font-size: 16px; }
    @media (min-width: 768px) {
        #grid-counters .tabulator-tableholder { overflow-x: hidden; }
    }
</style>

<script>
    $(document).ready(function () {
        /* Laid out as OPNsense's own status pages are: tabs, a striped table of what the coprocessor
           and its driver report, the datapath counters in OPNsense's grid, and the technical details
           in a table of their own. The API escapes every string it returns, so a value is inserted
           as it comes; only what this page builds itself is escaped here. */
        function val(s) {
            return (s === null || s === undefined || s === '') ? '&mdash;' : String(s);
        }

        function badge(on, yes, no) {
            return '<span class="label label-' + (on ? 'success' : 'default') + '">' + (on ? yes : no) + '</span>';
        }

        function rows($body, list) {
            $body.empty();
            list.forEach(function (r) {
                $body.append('<tr><td style="width: 30%;">' + r[0] + '</td><td>' + val(r[1]) + '</td></tr>');
            });
        }

        function render(st, ports, ipsec, settings) {
            if (!st || st.present !== true) {
                rows($('#xgs_summary'), [["{{ lang._('Coprocessor') }}", '<span class="text-warning">' +
                    (st && st.message ? st.message : "{{ lang._('the coprocessor driver is not attached') }}") + '</span>']]);
                $('#xgs_link, #xgs_rpc, #xgs_raw').empty();
                return;
            }
            const b = st.board || {}, dr = st.driver || {}, hs = st.handshake || {},
                rpc = st.rpc || {}, nwa = st.netagent || {}, off = st.offload || {},
                g = (settings && settings.xgs && settings.xgs.general) || {};
            const family = b.family === 'octeontx' ? 'OCTEON TX' : (b.family === 'armada' ? 'ARMADA' : val(b.family));
            const linked = String(hs.state || '').indexOf('completed') === 0 || dr.ready === 1;
            const plist = (ports && ports.ports) || [];
            const up = plist.filter(function (p) { return String(p.status || '').indexOf('active') === 0; }).length;
            const extra = (ports && ports.module && ports.module.extra) || [];
            const sas = ((ipsec && ipsec.texts && (ipsec.texts.table || ipsec.texts.sas)) || '')
                .split('\n').filter(function (l) { return /\bspi\s+0x/.test(l); }).length;
            const ipsecOn = ipsec && ipsec.tunable && ipsec.tunable.value === '1';
            const offOn = off.offload_bit_requested === true;
            const sw = (ports && ports.switch) || '';

            rows($('#xgs_summary'), [
                ["{{ lang._('Coprocessor') }}", family + ' ' + badge(linked, "{{ lang._('attached, link up') }}", "{{ lang._('not attached') }}")],
                ["{{ lang._('Hardware offloading') }}", badge(offOn, "{{ lang._('On') }}", "{{ lang._('Off') }}") + ' <small class="text-muted">' +
                    (g.flow_auto === '1' ? "{{ lang._('connections accelerated automatically') }}" : "{{ lang._('no automatic acceleration') }}") + '</small>'],
                ["{{ lang._('IPsec engine') }}", ipsecOn ? badge(true, "{{ lang._('On') }}", '') + ' ' + sas + ' ' + "{{ lang._('associations') }}" :
                    badge(false, '', "{{ lang._('Off') }}") + ' <small class="text-muted">' + "{{ lang._('the host encrypts') }}" + '</small>'],
                ["{{ lang._('Front ports') }}", up + ' / ' + plist.length + ' <small class="text-muted">' + "{{ lang._('with link') }}" +
                    (extra.length ? ', ' + extra.length + ' ' + "{{ lang._('on the expansion module') }}" : '') + '</small>'],
                ["{{ lang._('Switch chip') }}", sw.indexOf('10,240') >= 0 ? "{{ lang._('Jumbo frames') }}" + ' <small class="text-muted">' + sw + '</small>' :
                    (sw || "{{ lang._('set at the end of each boot') }}")]
            ]);
            rows($('#xgs_link'), [
                ["{{ lang._('Board') }}", b.assembly ? 'AMDA0' + b.assembly + ' (' + family + ')' : null],
                ["{{ lang._('Driver') }}", dr.description],
                ["{{ lang._('Handshake') }}", hs.state],
                ["{{ lang._('Rings in use') }}", hs.rings_in_use],
                ["{{ lang._('Virtual functions') }}", hs.vfs]
            ]);
            rows($('#xgs_rpc'), [
                ["{{ lang._('Control facility ready') }}", badge(rpc.ready === 1, "{{ lang._('yes') }}", "{{ lang._('no') }}")],
                ["{{ lang._('Port control ready') }}", badge(nwa.ready === 1, "{{ lang._('yes') }}", "{{ lang._('no') }}")],
                ["{{ lang._('Port commands issued') }}", nwa.commands],
                ["{{ lang._('Port command timeouts') }}", nwa.timeouts],
                ["{{ lang._('Last command') }}", rpc.last]
            ]);
            rows($('#xgs_raw'), [
                ["{{ lang._('Rings') }}", hs.rings],
                ["{{ lang._('Host status') }}", dr.host_status],
                ["{{ lang._('Target status') }}", dr.target_status],
                ["{{ lang._('Writes permitted') }}", rpc.allow_write],
                ["fw_cfg", off.fw_cfg_requested],
                ["fw_rev", (off.fw_rev_posted !== undefined) ? off.fw_rev_posted + ' / ' + off.fw_rev_bumps : null],
                ["{{ lang._('Note') }}", off.note]
            ]);
        }

        function refresh() {
            const $button = $('#refreshAct').prop('disabled', true);
            $.when(
                $.ajax({url: '/api/xgs/status/get', dataType: 'json'}),
                $.ajax({url: '/api/xgs/ports/get', dataType: 'json'}),
                $.ajax({url: '/api/xgs/ipsec/get', dataType: 'json'}),
                $.ajax({url: '/api/xgs/settings/get', dataType: 'json'})
            ).always(function () {
                $button.prop('disabled', false);
            }).done(function (st, ports, ipsec, settings) {
                render(st[0], ports[0], ipsec[0], settings[0]);
            }).fail(function () {
                ajaxGet('/api/xgs/status/get', {}, function (st) { render(st, null, null, null); });
            });
        }

        $('#grid-counters').UIBootgrid({
            search: '/api/xgs/status/counters',
            options: {
                selection: false,
                formatters: {
                    mono: function (column, row) {
                        return '<code>' + row.name + '</code>';
                    },
                    number: function (column, row) {
                        return Number(row.value).toLocaleString();
                    }
                }
            }
        });

        $('#refreshAct').click(function () {
            refresh();
            $('#grid-counters').bootgrid('reload');
        });
        refresh();

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
    <li><a data-toggle="tab" href="#counters">{{ lang._('Datapath counters') }}</a></li>
    <li><a data-toggle="tab" href="#details">{{ lang._('Technical details') }}</a></li>
</ul>

<div class="tab-content content-box">
    <div id="status" class="tab-pane active table-responsive">
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th colspan="2">
                        <div class="xgs-head">
                            <b>{{ lang._('Summary') }}</b>
                            <span class="pull-right">
                                <button class="btn btn-xs btn-default" id="refreshAct" type="button" title="{{ lang._('Refresh') }}"><i class="fa fa-fw fa-refresh"></i></button>
                            </span>
                        </div>
                    </th>
                </tr>
            </thead>
            <tbody id="xgs_summary"></tbody>
            <thead>
                <tr><th colspan="2"><div class="xgs-head"><b>{{ lang._('Coprocessor link') }}</b></div></th></tr>
            </thead>
            <tbody id="xgs_link"></tbody>
            <thead>
                <tr><th colspan="2"><div class="xgs-head"><b>{{ lang._('Control facility') }}</b></div></th></tr>
            </thead>
            <tbody id="xgs_rpc"></tbody>
        </table>
    </div>

    <div id="counters" class="tab-pane">
        <table id="grid-counters" class="table table-condensed table-hover table-striped table-responsive">
            <thead>
                <tr>
                    <th data-column-id="name" data-formatter="mono" data-type="string" data-identifier="true" data-width="14em">{{ lang._('Counter') }}</th>
                    <th data-column-id="value" data-formatter="number" data-type="numeric" data-width="10em">{{ lang._('Value') }}</th>
                    <th data-column-id="description" data-type="string">{{ lang._('What it counts') }}</th>
                </tr>
            </thead>
            <tbody></tbody>
            <tfoot></tfoot>
        </table>
    </div>

    <div id="details" class="tab-pane table-responsive">
        <table class="table table-striped table-condensed">
            <tbody id="xgs_raw"></tbody>
        </table>
    </div>
</div>
