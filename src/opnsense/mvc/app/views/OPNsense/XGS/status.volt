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
    .xgs-cards { display: flex; flex-wrap: wrap; gap: 12px; padding: 15px; }
    .xgs-card { flex: 1 1 200px; min-width: 190px; background: #fff; border: 1px solid #e5e5e5;
                border-inline-start: 4px solid #9e9e9e; border-radius: 3px; padding: 10px 14px; }
    .xgs-card.ok { border-inline-start-color: #5cb85c; }
    .xgs-card.warn { border-inline-start-color: #f0ad4e; }
    .xgs-card.off { border-inline-start-color: #9e9e9e; }
    .xgs-card .k { font-size: 11px; text-transform: uppercase; letter-spacing: .04em; color: #888; }
    .xgs-card .v { font-size: 19px; font-weight: 600; line-height: 1.3; margin: 2px 0; }
    .xgs-card .s { font-size: 12px; color: #777; }
    .xgs-card .fa { color: #bbb; float: inline-end; font-size: 18px; margin-top: 2px; }
    .xgs-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(420px, 1fr)); gap: 15px; padding: 0 15px 15px; }
    .xgs-grid h3, .xgs-section h3 { font-size: 14px; font-weight: 600; margin: 4px 0 8px; }
    .xgs-kv { width: 100%; font-size: 13px; }
    .xgs-kv td { padding: 4px 6px; border-top: 1px solid #f0f0f0; vertical-align: top; }
    .xgs-kv td:first-child { color: #666; width: 42%; }
    .xgs-tiles { display: grid; grid-template-columns: repeat(auto-fill, minmax(170px, 1fr)); gap: 8px; }
    .xgs-tile { border: 1px solid #eee; border-radius: 3px; padding: 6px 10px; background: #fafafa; }
    .xgs-tile .n { font-family: monospace; font-size: 11px; color: #888; }
    .xgs-tile .c { font-size: 16px; font-weight: 600; }
    .xgs-section { padding: 0 15px 15px; }
    .xgs-raw { font-size: 12px; }
    .xgs-raw td:first-child { white-space: nowrap; color: #666; width: 22%; }
</style>

<script>
    'use strict';

    /* The API escapes every string it returns, so a value is inserted as it comes; only what this
       page builds itself is escaped here. */
    function val(s) {
        return (s === null || s === undefined || s === '') ? '&mdash;' : String(s);
    }

    function card(cls, icon, key, value, sub) {
        return '<div class="xgs-card ' + cls + '"><i class="fa ' + icon + '"></i>' +
            '<div class="k">' + key + '</div><div class="v">' + value + '</div>' +
            '<div class="s">' + (sub || '&nbsp;') + '</div></div>';
    }

    function kv(rows) {
        return '<table class="xgs-kv"><tbody>' + rows.map(function (r) {
            return '<tr><td>' + r[0] + '</td><td>' + val(r[1]) + '</td></tr>';
        }).join('') + '</tbody></table>';
    }

    function badge(on, yes, no) {
        return '<span class="label label-' + (on ? 'success' : 'default') + '">' + (on ? yes : no) + '</span>';
    }

    function render(st, ports, ipsec, settings) {
        if (!st || st.present !== true) {
            $('#xgs_absent_text').html(st && st.message ? st.message :
                "{{ lang._('the coprocessor driver is not attached') }}");
            $('#xgs_absent').show();
            $('#xgs_present').hide();
            return;
        }
        $('#xgs_absent').hide();
        $('#xgs_present').show();

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
        const sw = (ports && ports.switch) || '';
        cards += card(sw.indexOf('10,240') >= 0 ? 'ok' : 'off', 'fa-sitemap', "{{ lang._('Switch chip') }}",
            sw.indexOf('10,240') >= 0 ? "{{ lang._('Jumbo frames') }}" : (g.switch_jumbo === '0' ? "{{ lang._('2,048 bytes') }}" : '&mdash;'),
            sw ? sw.split(' ').slice(0, 3).join(' ') : "{{ lang._('set at the end of each boot') }}");
        $('#xgs_cards').html(cards);

        $('#xgs_link').html(kv([
            ["{{ lang._('Board') }}", b.assembly ? 'AMDA0' + b.assembly + ' (' + family + ')' : null],
            ["{{ lang._('Driver') }}", dr.description],
            ["{{ lang._('Handshake') }}", hs.state],
            ["{{ lang._('Rings in use') }}", hs.rings_in_use],
            ["{{ lang._('Virtual functions') }}", hs.vfs]
        ]));
        $('#xgs_rpc').html(kv([
            ["{{ lang._('Control facility ready') }}", badge(rpc.ready === 1, "{{ lang._('yes') }}", "{{ lang._('no') }}")],
            ["{{ lang._('Port control ready') }}", badge(nwa.ready === 1, "{{ lang._('yes') }}", "{{ lang._('no') }}")],
            ["{{ lang._('Port commands issued') }}", nwa.commands],
            ["{{ lang._('Port command timeouts') }}", nwa.timeouts],
            ["{{ lang._('Last command') }}", rpc.last]
        ]));

        let tiles = '';
        $.each(st.datapath || {}, function (k, v) {
            tiles += '<div class="xgs-tile" title="' + val(v.description) + '"><div class="c">' +
                Number(v.value).toLocaleString() + '</div><div class="n">' + k + '</div></div>';
        });
        $('#xgs_counters').html(tiles || '&mdash;');

        $('#xgs_raw').html(kv([
            ["{{ lang._('Rings') }}", hs.rings],
            ["{{ lang._('Host status') }}", dr.host_status],
            ["{{ lang._('Target status') }}", dr.target_status],
            ["{{ lang._('Writes permitted') }}", rpc.allow_write],
            ["fw_cfg", off.fw_cfg_requested],
            ["fw_rev", (off.fw_rev_posted !== undefined) ? off.fw_rev_posted + ' / ' + off.fw_rev_bumps : null],
            ["{{ lang._('Note') }}", off.note]
        ]));
    }

    function refresh() {
        $('#xgs_refresh').addClass('disabled');
        $.when(
            $.ajax({url: '/api/xgs/status/get', dataType: 'json'}),
            $.ajax({url: '/api/xgs/ports/get', dataType: 'json'}),
            $.ajax({url: '/api/xgs/ipsec/get', dataType: 'json'}),
            $.ajax({url: '/api/xgs/settings/get', dataType: 'json'})
        ).always(function () {
            $('#xgs_refresh').removeClass('disabled');
        }).done(function (st, ports, ipsec, settings) {
            render(st[0], ports[0], ipsec[0], settings[0]);
        }).fail(function () {
            ajaxGet('/api/xgs/status/get', {}, function (st) { render(st, null, null, null); });
        });
    }

    $(document).ready(function () {
        $('#xgs_refresh').click(refresh);
        refresh();
    });
</script>

<div id="xgs_absent" style="display:none;">
    <div class="content-box" style="padding: 1em;">
        <div class="alert alert-warning" role="alert" id="xgs_absent_text"></div>
    </div>
</div>

<div id="xgs_present" style="display:none;">
    <div class="content-box">
        <div class="xgs-cards" id="xgs_cards"></div>
        <div class="xgs-grid">
            <div>
                <h3>{{ lang._('Coprocessor link') }}</h3>
                <div id="xgs_link"></div>
            </div>
            <div>
                <h3>{{ lang._('Control facility') }}</h3>
                <div id="xgs_rpc"></div>
            </div>
        </div>
        <div class="xgs-section">
            <h3>{{ lang._('Datapath counters') }}
                <button class="btn btn-default btn-xs" id="xgs_refresh" type="button" title="{{ lang._('Refresh') }}" style="margin-inline-start: 6px;"><i class="fa fa-refresh"></i></button>
            </h3>
            <div class="xgs-tiles" id="xgs_counters"></div>
        </div>
        <div class="xgs-section">
            <a data-toggle="collapse" href="#xgs_raw_box"><i class="fa fa-chevron-down"></i> {{ lang._('Technical details') }}</a>
            <div class="collapse" id="xgs_raw_box"><div class="xgs-raw" id="xgs_raw" style="margin-top: 8px;"></div></div>
        </div>
    </div>
</div>
