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
    /* The summary as cards, one for each thing a glance is for; the colours are the theme's text
       and a faint tint of it, so the cards read the same in the light theme and the dark one. */
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

        function card(cls, icon, key, value, sub) {
            return '<div class="xgs-card ' + cls + '"><i class="fa ' + icon + '"></i>' +
                '<div class="k">' + key + '</div><div class="v">' + value + '</div>' +
                '<div class="s">' + (sub || '&nbsp;') + '</div></div>';
        }

        function rows($body, list) {
            $body.empty();
            list.forEach(function (r) {
                $body.append('<tr><td style="width: 30%;">' + r[0] + '</td><td>' + val(r[1]) + '</td></tr>');
            });
        }

        function render(st, ports, ipsec, settings) {
            if (!st || st.present !== true) {
                $('#xgs_cards').html(card('warn', 'fa-microchip', "{{ lang._('Coprocessor') }}",
                    "{{ lang._('not attached') }}",
                    st && st.message ? st.message : "{{ lang._('the coprocessor driver is not attached') }}"));
                $('#xgs_link, #xgs_limits, #xgs_rpc, #xgs_raw').empty();
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

            let cards = '';
            cards += card(linked ? 'ok' : 'warn', 'fa-microchip', "{{ lang._('Coprocessor') }}", family,
                linked ? "{{ lang._('attached, link up') }}" : "{{ lang._('not attached') }}");
            cards += card(offOn ? 'ok' : 'off', 'fa-bolt', "{{ lang._('Hardware offloading') }}",
                offOn ? "{{ lang._('On') }}" : "{{ lang._('Off') }}",
                g.flow_auto === '1' ? "{{ lang._('connections accelerated automatically') }}" : "{{ lang._('no automatic acceleration') }}");
            cards += card(ipsecOn ? 'ok' : 'off', 'fa-lock', "{{ lang._('IPsec engine') }}",
                ipsecOn ? sas + ' ' + "{{ lang._('associations') }}" : "{{ lang._('Off') }}",
                ipsecOn ? "{{ lang._('in the coprocessor') }}" : "{{ lang._('the host encrypts') }}");
            cards += card(up > 0 ? 'ok' : 'warn', 'fa-plug', "{{ lang._('Front ports') }}",
                up + ' / ' + plist.length, extra.length ? extra.length + ' ' + "{{ lang._('on the expansion module') }}" : "{{ lang._('with link') }}");
            cards += card(sw.indexOf('10,240') >= 0 ? 'ok' : 'off', 'fa-sitemap', "{{ lang._('Switch chip') }}",
                sw.indexOf('10,240') >= 0 ? "{{ lang._('Jumbo frames') }}" : (g.switch_jumbo === '0' ? "{{ lang._('2,048 bytes') }}" : '&mdash;'),
                sw ? sw.split(' ').slice(0, 3).join(' ') : "{{ lang._('set at the end of each boot') }}");
            $('#xgs_cards').html(cards);
            rows($('#xgs_link'), [
                ["{{ lang._('Board') }}", b.assembly ? 'AMDA0' + b.assembly + ' (' + family + ')' : null],
                ["{{ lang._('Driver') }}", dr.description],
                ["{{ lang._('Handshake') }}", hs.state],
                ["{{ lang._('Rings in use') }}", hs.rings_in_use],
                ["{{ lang._('Virtual functions') }}", hs.vfs]
            ]);
            /* What the board itself says it holds, and what this host allocated of it and uses now. */
            const lim = st.limits || {};
            function num(n) { return (n === null || n === undefined) ? null : Number(n).toLocaleString(); }
            function of(used, max) {
                return (max === null || max === undefined) ? null :
                    num(used === null || used === undefined ? 0 : used) + ' ' + "{{ lang._('of') }}" + ' ' + num(max);
            }
            rows($('#xgs_limits'), [
                ["{{ lang._('Platform') }}", lim.platform],
                ["{{ lang._('Connections the board holds') }}", num(lim.board_connections)],
                ["{{ lang._('Connections in hardware') }}", of(lim.connections_in_use, lim.connections_max)],
                ["{{ lang._('Next hops in use') }}", of(lim.next_hops_in_use, lim.next_hops_max)],
                ["{{ lang._('IPsec associations the board holds') }}", num(lim.board_associations)],
                ["{{ lang._('Transmit queues') }}", lim.transmit_queues],
                ["{{ lang._('Flow writes') }}", lim.flow_writes === 'posted on the high-priority ring' ?
                    "{{ lang._('posted on the high-priority ring') }}" :
                    (lim.flow_writes ? "{{ lang._('each one waited for') }}" : null)],
                ["{{ lang._('Connections made at most') }}", lim.made_at_most === null || lim.made_at_most === undefined ? null :
                    num(lim.made_at_most) + ' ' + (lim.made_per === 'second' ? "{{ lang._('a second') }}" : "{{ lang._('a sweep, one sweep a second') }}")],
                ["{{ lang._('IPv6 connections left with the host') }}", num(lim.ipv6_refused)]
            ].filter(function (r) { return r[1] !== null && r[1] !== undefined; }));
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
        <div class="xgs-cards" id="xgs_cards"></div>
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th colspan="2">
                        <div class="xgs-head">
                            <b>{{ lang._('Coprocessor link') }}</b>
                            <span class="pull-right">
                                <button class="btn btn-xs btn-default" id="refreshAct" type="button" title="{{ lang._('Refresh') }}"><i class="fa fa-fw fa-refresh"></i></button>
                            </span>
                        </div>
                    </th>
                </tr>
            </thead>
            <tbody id="xgs_link"></tbody>
            <thead>
                <tr><th colspan="2"><div class="xgs-head"><b>{{ lang._('This board\'s limits') }}</b></div></th></tr>
            </thead>
            <tbody id="xgs_limits"></tbody>
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

{{ partial("OPNsense/XGS/credit") }}
