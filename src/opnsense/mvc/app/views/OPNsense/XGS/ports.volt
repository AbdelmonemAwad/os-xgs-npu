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

<script>
    'use strict';

    function esc(s) {
        if (s === null || s === undefined || s === '') { return '-'; }
        return $('<div/>').text(String(s)).html();
    }

    /* "active" and "no carrier" are what ifconfig prints; only the first is a link. */
    function link_label(status) {
        if (!status) { return '-'; }
        const up = status.indexOf('active') === 0;
        return '<span class="label label-' + (up ? 'success' : 'default') + '">' + esc(status) + '</span>';
    }

    function draw(data) {
        let rows = '';
        (data.ports || []).forEach(function (p) {
            const a = p.assigned;
            rows += '<tr><td><b>' + esc(p.label) + '</b></td><td><code>' + esc(p.device) + '</code></td>' +
                '<td>' + (a ? esc(a.descr) + ' <small class="text-muted">(' + esc(a.key) + ')</small>'
                           : '<span class="text-muted">' + "{{ lang._('not assigned') }}" + '</span>') + '</td>' +
                '<td>' + esc(a ? a.address : null) + '</td>' +
                '<td>' + link_label(p.status) + '</td><td>' + esc(p.media) + '</td>' +
                '<td>' + esc(p.mtu) + '</td><td>' + esc(p.bridge) + '</td></tr>';
        });
        $('#ports-body').html(rows ||
            '<tr><td colspan="8">' + "{{ lang._('The driver has made no front ports - it may not be loaded.') }}" + '</td></tr>');

        const m = data.module;
        if (m && m.extra && m.extra.length) {
            let mrows = '';
            m.extra.forEach(function (x) {
                mrows += '<tr><td><b>' + esc(x.name) + '</b></td><td>' + esc(x.slot) + '</td><td>' +
                    esc(x.module) + (x.module_rev ? ' <small class="text-muted">' + esc(x.module_rev) + '</small>' : '') +
                    '</td><td>' + esc(x.speed) + '</td><td>' + esc(x.lifport) + '</td><td>' + esc(x.tag) +
                    '</td><td>' + esc(x.source) + '</td></tr>';
            });
            $('#module-body').html(mrows);
            $('#module-box').show();
        } else {
            $('#module-none').show();
        }
        $('#switch-state').text(data.switch ||
            "{{ lang._('No reading yet - the switch chip is set at the end of each boot.') }}");
    }

    $(document).ready(function () {
        ajaxCall('/api/xgs/ports/get', {}, function (data) {
            draw(data || {});
        });
    });
</script>

<div class="content-box" style="padding: 15px;">
    <h2>{{ lang._('Front ports') }}</h2>
    <p class="text-muted">{{ lang._('Every port the driver made, by the name on the metal: what OPNsense assigned it as, its link, its MTU and the bridge it belongs to. Ports are assigned and addressed under Interfaces: Assignments, as any other.') }}</p>
    <div class="table-responsive">
        <table class="table table-striped table-condensed">
            <thead><tr>
                <th>{{ lang._('Port') }}</th><th>{{ lang._('Device') }}</th><th>{{ lang._('Assigned as') }}</th>
                <th>{{ lang._('Address') }}</th><th>{{ lang._('Link') }}</th><th>{{ lang._('Media') }}</th>
                <th>{{ lang._('MTU') }}</th><th>{{ lang._('Bridge') }}</th>
            </tr></thead>
            <tbody id="ports-body"><tr><td colspan="8">{{ lang._('Reading...') }}</td></tr></tbody>
        </table>
    </div>

    <h2>{{ lang._('Expansion module') }}</h2>
    <p id="module-none" class="text-muted" style="display: none;">{{ lang._('No expansion module is fitted, or the coprocessor reported none at this boot.') }}</p>
    <div id="module-box" class="table-responsive" style="display: none;">
        <table class="table table-striped table-condensed">
            <thead><tr>
                <th>{{ lang._('Port') }}</th><th>{{ lang._('Bay') }}</th><th>{{ lang._('Module') }}</th>
                <th>{{ lang._('Speed') }}</th><th>{{ lang._('Logical port') }}</th><th>{{ lang._('Tag') }}</th>
                <th>{{ lang._('Name from') }}</th>
            </tr></thead>
            <tbody id="module-body"></tbody>
        </table>
    </div>

    <h2>{{ lang._('Switch chip') }}</h2>
    <p class="text-muted">{{ lang._('Port1 to Port8 and the two cages beside them take their frame limit from the switch chip. Whether they carry jumbo frames is set on the Settings page and applied at the end of every boot; this is what the last boot did.') }}</p>
    <p><code id="switch-state">{{ lang._('Reading...') }}</code></p>
</div>
