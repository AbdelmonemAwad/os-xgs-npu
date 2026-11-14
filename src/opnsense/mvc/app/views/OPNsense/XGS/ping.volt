{#
 # SPDX-License-Identifier: BSD-2-Clause
 #
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
    $(document).ready(function () {
        /* A ping sent by the coprocessor itself, over the management link (scripts/npuctl/mgmt-link.sh
           ping): what its own Linux reaches, which a ping from this firewall cannot tell. The answer is
           what the coprocessor's ping printed, shown as text. */
        function plain(value) {
            return $('<textarea/>').html(value === null || value === undefined ? '' : String(value)).text();
        }
        $('#pingAct').click(function () {
            const $btn = $(this).prop('disabled', true);
            $('#ping_progress').addClass('fa fa-spinner fa-pulse');
            $('#ping_out').text('');
            ajaxCall('/api/xgs/ping/ping', {address: $('#ping_address').val(), count: $('#ping_count').val()}, function (data) {
                $btn.prop('disabled', false);
                $('#ping_progress').removeClass('fa fa-spinner fa-pulse');
                if (!data || data.status !== 'ok') {
                    $('#ping_out').text(plain((data && data.message) || "{{ lang._('The coprocessor could not be asked.') }}"));
                    return;
                }
                $('#ping_out').text(plain(data.output) || "{{ lang._('The coprocessor printed nothing: the management link may be down.') }}");
            });
        });
        $('#ping_address').keypress(function (e) {
            if (e.which === 13) {
                $('#pingAct').click();
            }
        });
    });
</script>

<div class="content-box">
    <table class="table table-striped table-condensed">
        <thead>
            <tr><th colspan="2"><div style="padding: 5px 0; font-size: 16px;"><b>{{ lang._('Ping from the coprocessor') }}</b></div></th></tr>
        </thead>
        <tbody>
            <tr>
                <td style="width: 25%;">{{ lang._('Address or host name') }}</td>
                <td>
                    <input type="text" id="ping_address" maxlength="253" style="max-width: 360px;" autocomplete="off"/>
                    <div class="text-muted"><small>{{ lang._('Sent by the coprocessor\'s own Linux over the management link, so it tells what the coprocessor reaches from its own ports, not what this firewall reaches. A name is resolved by the coprocessor.') }}</small></div>
                </td>
            </tr>
            <tr>
                <td>{{ lang._('Count') }}</td>
                <td>
                    <select id="ping_count" class="selectpicker" data-width="auto">
                        <option value="1">1</option>
                        <option value="3">3</option>
                        <option value="4" selected="selected">4</option>
                        <option value="10">10</option>
                    </select>
                </td>
            </tr>
            <tr>
                <td></td>
                <td>
                    <button class="btn btn-primary" id="pingAct" type="button"><i class="fa fa-fw fa-play"></i> {{ lang._('Ping') }} <i id="ping_progress"></i></button>
                </td>
            </tr>
        </tbody>
    </table>
    <div style="padding: 0 15px 15px;">
        <pre id="ping_out" style="max-height: 420px; overflow: auto; white-space: pre-wrap; margin: 0;"></pre>
    </div>
</div>

{{ partial("OPNsense/XGS/credit") }}
