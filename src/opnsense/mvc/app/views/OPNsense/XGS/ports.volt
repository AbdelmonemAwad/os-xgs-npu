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
    .xgs-ports td, .xgs-ports th { vertical-align: middle !important; font-size: 13px; }
    .xgs-ports .port { font-weight: 600; }
    .xgs-ports .sub { display: block; font-size: 11px; color: #999; font-weight: normal; }
    .xgs-ports .dev { font-family: monospace; font-size: 11px; color: #999; }
    .xgs-ports .media { font-size: 12px; color: #777; }
    .xgs-ports tr.down td { color: #888; }
    .xgs-ports .lk { min-width: 46px; display: inline-block; text-align: center; }
    .xgs-note { padding: 4px 15px 15px; font-size: 12px; color: #777; }
</style>

<script>
    'use strict';

    /* The API escapes every string it returns: inserted as it comes, never escaped twice. */
    function val(s) {
        return (s === null || s === undefined || s === '') ? '&mdash;' : String(s);
    }

    function decode(s) {
        return $('<textarea/>').html(s || '').text();
    }

    /* "Ethernet autoselect (10Gbase-SR <full-duplex>)" -> speed "10G", media "10Gbase-SR full-duplex" */
    function media(raw) {
        const m = decode(raw).replace(/^Ethernet\s+autoselect\s*/i, '').replace(/[()<>]/g, ' ').replace(/\s+/g, ' ').trim();
        const sp = /^(\d+)(G|M)?base/i.exec(m);
        let speed = '';
        if (sp) {
            const n = parseInt(sp[1], 10);
            speed = sp[2] && sp[2].toUpperCase() === 'G' ? n + 'G' : (n >= 1000 ? (n / 1000) + 'G' : n + 'M');
        }
        return {speed: speed, text: m === 'none' ? '' : m};
    }

    function draw(data) {
        const ports = data.ports || [];
        const extra = {};
        ((data.module && data.module.extra) || []).forEach(function (x) { extra[x.name] = x; });
        let rows = '', up = 0;
        ports.forEach(function (p) {
            const isUp = String(p.status || '').indexOf('active') === 0;
            const md = media(p.media);
            const a = p.assigned;
            const x = extra[p.label];
            if (isUp) { up++; }
            const sub = x ? "{{ lang._('expansion bay') }}" + ' ' + val(x.slot) + (x.module ? ' &middot; ' + x.module : '') : '';
            rows += '<tr class="' + (isUp ? '' : 'down') + '">' +
                '<td><span class="port">' + val(p.label) + '</span>' + (sub ? '<span class="sub">' + sub + '</span>' : '') + '</td>' +
                '<td>' + (a ? val(a.descr) + ' <small class="text-muted">' + val(a.key) + '</small>' :
                    '<span class="text-muted">' + "{{ lang._('not assigned') }}" + '</span>') + '</td>' +
                '<td>' + val(a ? a.address : null) + '</td>' +
                '<td><span class="label lk label-' + (isUp ? 'success' : 'default') + '">' +
                    (isUp ? (md.speed || "{{ lang._('up') }}") : "{{ lang._('down') }}") + '</span></td>' +
                '<td class="media">' + val(md.text) + '</td>' +
                '<td>' + val(p.mtu) + '</td>' +
                '<td>' + val(p.bridge) + '</td>' +
                '<td class="dev">' + val(p.device) + '</td></tr>';
        });
        $('#ports-body').html(rows ||
            '<tr><td colspan="8">' + "{{ lang._('The driver has made no front ports - it may not be loaded.') }}" + '</td></tr>');

        let chips = '<span class="xgs-chip"><b>' + up + '</b> / ' + ports.length + ' ' + "{{ lang._('with link') }}" + '</span>';
        const slots = (data.module && data.module.slots) || {};
        Object.keys(slots).forEach(function (bay) {
            const s = slots[bay];
            chips += '<span class="xgs-chip">' + "{{ lang._('Expansion module') }}" + ' ' + bay + ': <b>' +
                val(s['assembly.partno']) + '</b>' + (s['assembly.revision'] ? ' rev ' + s['assembly.revision'] : '') + '</span>';
        });
        const sw = data.switch || '';
        chips += '<span class="xgs-chip" title="' + val(sw) + '">' + "{{ lang._('Switch chip') }}" + ': <b>' +
            (sw.indexOf('10,240') >= 0 ? "{{ lang._('jumbo, 10,240 bytes') }}" : (sw ? "{{ lang._('2,048 bytes') }}" : '&mdash;')) + '</b></span>';
        $('#ports-chips').html(chips);
    }

    function load() {
        ajaxCall('/api/xgs/ports/get', {}, function (data) { draw(data || {}); });
    }

    $(document).ready(function () {
        load();
        $('#ports-refresh').click(load);
    });
</script>

<div class="content-box">
    <div class="xgs-bar">
        <span id="ports-chips"></span>
        <button class="btn btn-default btn-xs" id="ports-refresh" type="button" title="{{ lang._('Refresh') }}"><i class="fa fa-refresh"></i></button>
    </div>
    <div class="table-responsive" style="padding: 0 15px;">
        <table class="table table-condensed table-hover xgs-ports">
            <thead><tr>
                <th>{{ lang._('Port') }}</th><th>{{ lang._('Assigned as') }}</th><th>{{ lang._('Address') }}</th>
                <th>{{ lang._('Link') }}</th><th>{{ lang._('Media') }}</th><th>{{ lang._('MTU') }}</th>
                <th>{{ lang._('Bridge') }}</th><th>{{ lang._('Device') }}</th>
            </tr></thead>
            <tbody id="ports-body"><tr><td colspan="8">{{ lang._('Reading...') }}</td></tr></tbody>
        </table>
    </div>
    <div class="xgs-note">
        <i class="fa fa-info-circle"></i>
        {{ lang._('Every port the driver made, by the name on the metal. Ports are assigned and addressed under Interfaces: Assignments, as any other. Port1 to Port8 and the two cages beside them take their frame limit from the switch chip, set at the end of every boot from the Settings page.') }}
    </div>
</div>
