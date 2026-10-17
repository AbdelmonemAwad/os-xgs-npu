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
    .xgs-bar { display: flex; flex-wrap: wrap; align-items: center; gap: 10px; padding: 12px 15px 4px; }
    .xgs-chip { display: inline-block; border: 1px solid #e3e3e3; border-radius: 12px; padding: 2px 10px;
                font-size: 12px; background: #fafafa; color: #555; }
    .xgs-chip b { color: #222; }
    .xgs-chip.ok { border-color: #c9e6c9; background: #f3faf3; }
    .xgs-sa td, .xgs-sa th { font-size: 13px; vertical-align: middle !important; }
    .xgs-sa .mono { font-family: monospace; font-size: 12px; }
    .xgs-groups { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 12px; padding: 0 15px 10px; }
    .xgs-group h4 { font-size: 13px; font-weight: 600; margin: 6px 0; }
    .xgs-group table { width: 100%; font-size: 12px; }
    .xgs-group td { padding: 2px 4px; border-top: 1px solid #f2f2f2; }
    .xgs-group td.n { font-family: monospace; color: #666; }
    .xgs-group td.v { text-align: end; font-weight: 600; }
    .xgs-group tr.zero td { color: #bbb; font-weight: normal; }
    .xgs-groups.hide-zero tr.zero { display: none; }
    .xgs-group tr.allzero td { color: #bbb; font-style: italic; }
    .xgs-groups:not(.hide-zero) tr.allzero { display: none; }
    .xgs-section { padding: 0 15px; }
    .xgs-section h3 { font-size: 14px; font-weight: 600; margin: 12px 0 8px; }
    .xgs-note { padding: 4px 15px 15px; font-size: 12px; color: #777; }
</style>

<script>
    'use strict';

    function val(s) {
        return (s === null || s === undefined || s === '') ? '&mdash;' : String(s);
    }

    function esc(s) {
        return $('<div/>').text(String(s)).html();
    }

    /* The API escaped the engine's text ("->" arrives as "-&gt;"); it is read as text again here. */
    function decode(s) {
        return $('<textarea/>').html(s || '').text();
    }

    /* 0xc0000201 -> 192.0.2.1 */
    function ip4(hex) {
        const n = parseInt(hex, 16);
        return isNaN(n) ? hex : [n >>> 24, (n >>> 16) & 255, (n >>> 8) & 255, n & 255].join('.');
    }

    function dir(d) {
        return '<span class="label label-' + (d === 'encrypt' ? 'info' : 'primary') + '">' +
            (d === 'encrypt' ? "{{ lang._('encrypt') }}" : "{{ lang._('decrypt') }}") + '</span>';
    }

    /* Two engines print two formats: the XGS 3300's table and the XGS 136's list. A line neither
       pattern reads is kept as it is, below the table. */
    function associations(text) {
        const rows = [], other = [];
        decode(text).split('\n').forEach(function (line) {
            let m = /^\s*\d+\s+handle\s+(\d+)\s+rev\s+\d+\s+(encrypt|decrypt)\s+spi\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+->\s+(0x[0-9a-f]+)\s+lif\s+\S+\s+(\S+)\s+drv_spi\s+\d+\s+win\s+(\d+)/i.exec(line);
            if (m) {
                rows.push({handle: m[1], dir: m[2], spi: m[3], from: ip4(m[4]), to: ip4(m[5]), port: m[6], win: m[7], note: ''});
                return;
            }
            m = /^(\S+)\s+spi\s+(0x[0-9a-f]+)\s+(encrypt|decrypt)\s+(inet6?)\s+alg\s+\d+\s+win\s+(\d+)\s+(\d+)s ago\s*\|?\s*(.*)$/i.exec(line.trim());
            if (m) {
                rows.push({handle: '', dir: m[3], spi: m[2], from: m[4], to: '', port: m[1], win: m[5], note: m[7]});
                return;
            }
            if (line.trim()) { other.push(line.trim()); }
        });
        return {rows: rows, other: other};
    }

    const GROUPS = [
        ["{{ lang._('Associations') }}", /^sa_|^seq_|^install/],
        ["{{ lang._('Receive') }}", /^rx_/],
        ["{{ lang._('Transmit') }}", /^tx_|^out_/],
        ["{{ lang._('Flows') }}", /^flow/],
        ["{{ lang._('Other') }}", /./]
    ];

    function draw(d) {
        const on = d.tunable && d.tunable.value === '1';
        let chips = '<span class="xgs-chip ' + (on ? 'ok' : '') + '">' + "{{ lang._('IPsec in the engine') }}" + ': <b>' +
            (on ? "{{ lang._('yes') }}" : "{{ lang._('no') }}") + '</b></span>';
        if (d.family === 'octeontx') {
            chips += '<span class="xgs-chip ' + (d.flows_tunable === '1' ? 'ok' : '') + '">' +
                "{{ lang._('Outbound tunnel flows forwarded by the coprocessor') }}" + ': <b>' +
                (d.flows_tunable === '1' ? "{{ lang._('yes') }}" : "{{ lang._('no') }}") + '</b></span>';
        } else if (d.family === 'armada') {
            chips += '<span class="xgs-chip ' + (d.crypto_support === '33554432' ? 'ok' : '') + '">' +
                "{{ lang._('Software sessions kept off ccp') }}" + ': <b>' +
                (d.crypto_support === '33554432' ? "{{ lang._('yes') }}" : "{{ lang._('no') }}") + '</b></span>';
        }
        $('#ipsec-chips').html(chips);

        const sa = associations((d.texts && (d.texts.table || d.texts.sas)) || '');
        $('#sa-body').html(sa.rows.map(function (r) {
            return '<tr><td>' + dir(r.dir) + '</td><td class="mono">' + esc(r.spi) + '</td>' +
                '<td class="mono">' + esc(r.from) + (r.to ? ' &rarr; ' + esc(r.to) : '') + '</td>' +
                '<td>' + esc(r.port) + '</td><td>' + esc(r.win) + '</td>' +
                '<td class="mono">' + esc(r.handle) + '</td><td><small class="text-muted">' + esc(r.note) + '</small></td></tr>';
        }).join('') || '<tr><td colspan="7">' + "{{ lang._('No association is in the engine.') }}" + '</td></tr>');
        $('#sa-other').text(sa.other.join('\n')).toggle(sa.other.length > 0);

        const counters = d.counters || {}, used = {};
        let html = '';
        GROUPS.forEach(function (g) {
            const names = Object.keys(counters).filter(function (n) { return !used[n] && g[1].test(n); }).sort();
            if (!names.length) { return; }
            names.forEach(function (n) { used[n] = true; });
            const allZero = names.every(function (n) { return Number(counters[n]) === 0; });
            html += '<div class="xgs-group"><h4>' + g[0] + '</h4><table><tbody>' + names.map(function (n) {
                const v = Number(counters[n]);
                return '<tr class="' + (v === 0 ? 'zero' : '') + '"><td class="n">' + esc(n) + '</td><td class="v">' +
                    v.toLocaleString() + '</td></tr>';
            }).join('') + (allZero ? '<tr class="allzero"><td colspan="2">' + names.length + ' ' +
                "{{ lang._('counters, all at zero') }}" + '</td></tr>' : '') + '</tbody></table></div>';
        });
        $('#counters').html(html || '<p class="text-muted">' + "{{ lang._('The driver is not loaded, or was started without IPsec.') }}" + '</p>');
    }

    function load() {
        ajaxCall('/api/xgs/ipsec/get', {}, function (data) { draw(data || {}); });
    }

    $(document).ready(function () {
        load();
        $('#refresh').click(load);
        $('#show-zero').change(function () { $('#counters').toggleClass('hide-zero', !this.checked); });
    });
</script>

<div class="content-box">
    <div class="xgs-bar">
        <span id="ipsec-chips"></span>
        <button class="btn btn-default btn-xs" id="refresh" type="button" title="{{ lang._('Refresh') }}"><i class="fa fa-refresh"></i></button>
    </div>
    <div class="xgs-section">
        <h3>{{ lang._('Associations in the engine') }}</h3>
        <div class="table-responsive">
            <table class="table table-condensed table-hover xgs-sa">
                <thead><tr>
                    <th>{{ lang._('Direction') }}</th><th>SPI</th><th>{{ lang._('Tunnel') }}</th><th>{{ lang._('Port') }}</th>
                    <th>{{ lang._('Replay window') }}</th><th>{{ lang._('Handle') }}</th><th></th>
                </tr></thead>
                <tbody id="sa-body"><tr><td colspan="7">{{ lang._('Reading...') }}</td></tr></tbody>
            </table>
        </div>
        <pre id="sa-other" style="display: none; font-size: 12px; white-space: pre-wrap;"></pre>
    </div>
    <div class="xgs-section">
        <h3>{{ lang._('Counters') }}
            <label style="font-weight: normal; font-size: 12px; margin-inline-start: 12px;">
                <input type="checkbox" id="show-zero"> {{ lang._('show the ones at zero') }}
            </label>
        </h3>
    </div>
    <div class="xgs-groups hide-zero" id="counters"></div>
    <div class="xgs-note">
        <i class="fa fa-info-circle"></i>
        {{ lang._('The tunnels themselves are configured under VPN: IPsec. Whether their associations go to the crypto engine of the coprocessor is set on the Settings page and takes effect at the next reboot; this page shows what the engine holds now.') }}
    </div>
</div>
