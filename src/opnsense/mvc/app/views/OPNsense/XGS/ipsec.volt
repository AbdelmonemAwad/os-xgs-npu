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

    function yes_no(on) {
        return '<span class="label label-' + (on ? 'success' : 'default') + '">' +
            (on ? "{{ lang._('yes') }}" : "{{ lang._('no') }}") + '</span>';
    }

    function draw(d) {
        let rows = '<tr><td>' + "{{ lang._('The driver was started with IPsec in the engine') }}" + '</td><td>' +
            yes_no(d.tunable && d.tunable.value === '1') +
            ' <small class="text-muted"><code>' + esc(d.tunable ? d.tunable.name : null) + '</code></small></td></tr>';
        if (d.family === 'octeontx') {
            rows += '<tr><td>' + "{{ lang._('Tunnel flows forwarded by the coprocessor') }}" + '</td><td>' +
                yes_no(d.flows_tunable === '1') + ' <small class="text-muted"><code>hw.octep.ipsec_flows</code></small></td></tr>';
        } else if (d.family === 'armada') {
            rows += '<tr><td>' + "{{ lang._('Software sessions kept off ccp') }}" + '</td><td>' +
                yes_no(d.crypto_support === '33554432') + ' <small class="text-muted">ccp ' +
                (d.ccp_loaded ? "{{ lang._('loaded') }}" : "{{ lang._('not loaded') }}") + '</small></td></tr>';
        }
        $('#summary-body').html(rows);

        const texts = d.texts || {};
        $('#sas').text(texts.table || texts.sas || "{{ lang._('No association is in the engine.') }}");

        let crow = '';
        Object.keys(d.counters || {}).sort().forEach(function (n) {
            crow += '<tr><td><code>' + esc(n) + '</code></td><td class="text-right">' + esc(d.counters[n]) + '</td></tr>';
        });
        $('#counters-body').html(crow ||
            '<tr><td colspan="2">' + "{{ lang._('The driver is not loaded, or was started without IPsec.') }}" + '</td></tr>');
    }

    $(document).ready(function () {
        function load() {
            ajaxCall('/api/xgs/ipsec/get', {}, function (data) { draw(data || {}); });
        }
        load();
        $('#refresh').click(load);
    });
</script>

<div class="content-box" style="padding: 15px;">
    <p class="text-muted">{{ lang._('The tunnels themselves are configured under VPN: IPsec. Whether their associations go to the crypto engine of the coprocessor is set on the Settings page and takes effect at the next reboot; this page shows what the engine holds now.') }}</p>
    <table class="table table-condensed" style="max-width: 760px;"><tbody id="summary-body"></tbody></table>

    <h2>{{ lang._('Associations in the engine') }}
        <button class="btn btn-default btn-xs" id="refresh" type="button" title="{{ lang._('Refresh') }}"><i class="fa fa-refresh"></i></button>
    </h2>
    <pre id="sas" style="white-space: pre-wrap;">{{ lang._('Reading...') }}</pre>

    <h2>{{ lang._('Counters') }}</h2>
    <div class="table-responsive" style="max-width: 760px;">
        <table class="table table-striped table-condensed">
            <tbody id="counters-body"><tr><td colspan="2">{{ lang._('Reading...') }}</td></tr></tbody>
        </table>
    </div>
</div>
