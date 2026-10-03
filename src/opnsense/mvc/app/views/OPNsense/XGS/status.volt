{#
 # Copyright (c) 2026 the os-xgs-npu authors
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
        if (s === null || s === undefined) { return '-'; }
        return $('<div/>').text(String(s)).html();
    }

    function yesno(v) {
        if (v === null || v === undefined) { return '-'; }
        var on = (v === true || v === 1 || v === '1');
        return '<span class="label label-' + (on ? 'success' : 'default') + '">' +
               (on ? '{{ lang._('yes') }}' : '{{ lang._('no') }}') + '</span>';
    }

    function pairs(rows) {
        var html = '';
        $.each(rows, function (i, r) {
            html += '<tr><td style="width:40%"><strong>' + esc(r[0]) + '</strong></td>' +
                    '<td>' + (r[2] === 'raw' ? r[1] : esc(r[1])) + '</td></tr>';
        });
        return html;
    }

    function render(d) {
        if (!d || d.present !== true) {
            $('#xgs_absent_text').text(d && d.message ? d.message :
                '{{ lang._('the coprocessor driver is not attached') }}');
            $('#xgs_absent').show();
            $('#xgs_present').hide();
            return;
        }
        $('#xgs_absent').hide();
        $('#xgs_present').show();

        var b = d.board || {}, dr = d.driver || {}, hs = d.handshake || {},
            rpc = d.rpc || {}, nwa = d.netagent || {}, off = d.offload || {};

        $('#xgs_board').html(pairs([
            ['{{ lang._('Assembly') }}', b.assembly],
            ['{{ lang._('Family') }}', b.family],
            ['{{ lang._('Driver') }}', dr.description],
            ['{{ lang._('Attached') }}', yesno(dr.ready), 'raw']
        ]));

        $('#xgs_link').html(pairs([
            ['{{ lang._('Handshake') }}', hs.state],
            ['{{ lang._('Rings') }}', hs.rings],
            ['{{ lang._('Rings in use') }}', hs.rings_in_use],
            ['{{ lang._('Virtual functions') }}', hs.vfs],
            ['{{ lang._('Host status') }}', dr.host_status],
            ['{{ lang._('Target status') }}', dr.target_status]
        ]));

        $('#xgs_rpc').html(pairs([
            ['{{ lang._('Control facility ready') }}', yesno(rpc.ready), 'raw'],
            ['{{ lang._('Writes permitted') }}', yesno(rpc.allow_write), 'raw'],
            ['{{ lang._('Last command') }}', rpc.last],
            ['{{ lang._('Port control ready') }}', yesno(nwa.ready), 'raw'],
            ['{{ lang._('Port commands issued') }}', nwa.commands],
            ['{{ lang._('Port command timeouts') }}', nwa.timeouts],
            ['{{ lang._('IPsec offload requested') }}', yesno(off.offload_bit_requested), 'raw']
        ]));
        $('#xgs_offload_note').text(off.note || '');

        var ch = '';
        $.each(d.datapath || {}, function (k, v) {
            ch += '<tr><td style="width:30%"><strong>' + esc(k) + '</strong></td>' +
                  '<td style="width:20%">' + esc(v.value) + '</td>' +
                  '<td><small class="text-muted">' + esc(v.description) + '</small></td></tr>';
        });
        $('#xgs_counters').html(ch || '<tr><td colspan="3">-</td></tr>');

        var ph = '';
        $.each(d.ports || [], function (i, p) {
            var up = (p.status === 'active');
            ph += '<tr><td>' + esc(p.device) + '</td>' +
                  '<td>' + esc(p.description) + '</td>' +
                  '<td><span class="label label-' + (up ? 'success' : 'default') + '">' +
                  esc(p.status || '-') + '</span></td>' +
                  '<td><small>' + esc(p.media) + '</small></td></tr>';
        });
        $('#xgs_ports').html(ph || '<tr><td colspan="4">-</td></tr>');
    }

    function refresh() {
        $('#xgs_refresh').addClass('disabled');
        ajaxGet('/api/xgs/status/get', {}, function (data) {
            $('#xgs_refresh').removeClass('disabled');
            render(data);
        });
    }

    $(document).ready(function () {
        $('#xgs_refresh').click(refresh);
        refresh();
    });
</script>

<div class="content-box" style="padding-bottom: 1.5em;">
    <div class="col-md-12">
        <br/>
        <button class="btn btn-primary" id="xgs_refresh" type="button">
            <i class="fa fa-refresh"></i> {{ lang._('Refresh') }}
        </button>
        <br/><br/>
    </div>
</div>

<div id="xgs_absent" style="display:none;">
    <div class="content-box" style="padding: 1em;">
        <div class="alert alert-warning" role="alert" id="xgs_absent_text"></div>
    </div>
</div>

<div id="xgs_present" style="display:none;">
    <div class="content-box">
        <div class="content-box-main">
            <h2 style="padding-left:1em;">{{ lang._('Appliance') }}</h2>
            <table class="table table-striped"><tbody id="xgs_board"></tbody></table>
        </div>
    </div>
    <br/>

    <div class="content-box">
        <div class="content-box-main">
            <h2 style="padding-left:1em;">{{ lang._('Coprocessor link') }}</h2>
            <table class="table table-striped"><tbody id="xgs_link"></tbody></table>
        </div>
    </div>
    <br/>

    <div class="content-box">
        <div class="content-box-main">
            <h2 style="padding-left:1em;">{{ lang._('Control facility') }}</h2>
            <table class="table table-striped"><tbody id="xgs_rpc"></tbody></table>
            <div style="padding: 0 1em 1em 1em;">
                <small class="text-muted" id="xgs_offload_note"></small>
            </div>
        </div>
    </div>
    <br/>

    <div class="content-box">
        <div class="content-box-main">
            <h2 style="padding-left:1em;">{{ lang._('Front ports') }}</h2>
            <table class="table table-striped">
                <thead>
                    <tr>
                        <th>{{ lang._('Device') }}</th>
                        <th>{{ lang._('Description') }}</th>
                        <th>{{ lang._('Status') }}</th>
                        <th>{{ lang._('Media') }}</th>
                    </tr>
                </thead>
                <tbody id="xgs_ports"></tbody>
            </table>
        </div>
    </div>
    <br/>

    <div class="content-box">
        <div class="content-box-main">
            <h2 style="padding-left:1em;">{{ lang._('Datapath counters') }}</h2>
            <table class="table table-striped"><tbody id="xgs_counters"></tbody></table>
        </div>
    </div>
</div>
