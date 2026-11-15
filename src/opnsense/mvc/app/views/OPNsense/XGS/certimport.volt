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
        /* A certificate file read on the firewall (api/xgs/certimport/convert, scripts/xgs/certimport.inc)
           into PEM, then handed to System: Trust's own API: api/trust/cert/add for each certificate that
           is no authority - the one the private key belongs to with its key - and api/trust/ca/add for
           the authorities. Trust checks and stores each as any import; this page stores nothing. */
        let found = null;
        function plain(value) {
            return $('<textarea/>').html(value === null || value === undefined ? '' : String(value)).text();
        }
        function say(text, kind) {
            $('#ci_msg').removeClass('text-danger text-success').addClass(kind || '').text(text);
        }
        $('#ci_read').click(function () {
            const file = $('#ci_file')[0].files[0];
            found = null;
            $('#ci_list tbody').empty();
            $('#ci_import').prop('disabled', true);
            if (!file) {
                say("{{ lang._('Choose a file first.') }}", 'text-danger');
                return;
            }
            const reader = new FileReader();
            reader.onload = function () {
                const bytes = new Uint8Array(reader.result);
                let bin = '';
                for (let i = 0; i < bytes.length; i++) {
                    bin += String.fromCharCode(bytes[i]);
                }
                ajaxCall('/api/xgs/certimport/convert', {data: btoa(bin), password: $('#ci_password').val()}, function (data) {
                    if (!data || data.status !== 'ok') {
                        say(plain((data && data.message) || "{{ lang._('The file could not be read.') }}"), 'text-danger');
                        return;
                    }
                    found = data;
                    data.certs.forEach(function (c, i) {
                        const as = c.ca ? "{{ lang._('an authority') }}" :
                            (c.key ? "{{ lang._('a certificate, with its private key') }}" : "{{ lang._('a certificate') }}");
                        $('#ci_list tbody').append($('<tr/>')
                            .append($('<td/>').text(c.subject))
                            .append($('<td/>').text(c.issuer))
                            .append($('<td/>').text(c.until))
                            .append($('<td/>').text(as)));
                    });
                    say("{{ lang._('Read. Import adds them to System: Trust as listed.') }}", 'text-success');
                    $('#ci_import').prop('disabled', false);
                });
            };
            reader.readAsArrayBuffer(file);
        });
        $('#ci_import').click(function () {
            if (!found) {
                return;
            }
            const $btn = $(this).prop('disabled', true);
            const base = $('#ci_descr').val().trim();
            const results = [];
            let left = found.certs.length;
            found.certs.forEach(function (c) {
                const descr = base !== '' ? base + ' - ' + c.subject : c.subject;
                const url = c.ca ? '/api/trust/ca/add' : '/api/trust/cert/add';
                const body = c.ca ? {ca: {action: 'existing', descr: descr, crt_payload: c.pem}} :
                    {cert: {action: 'import', descr: descr, crt_payload: c.pem, prv_payload: c.key ? found.key : ''}};
                ajaxCall(url, body, function (data) {
                    const ok = data && data.result === 'saved';
                    let why = '';
                    if (!ok && data && data.validations) {
                        why = Object.values(data.validations).join(' ');
                    }
                    results.push((ok ? "{{ lang._('Added') }}" : "{{ lang._('Not added') }}") + ': ' + c.subject +
                        (why !== '' ? ' - ' + why : ''));
                    if (--left === 0) {
                        say(results.join('\n'), results.every(function (r) { return r.indexOf("{{ lang._('Added') }}") === 0; }) ? 'text-success' : 'text-danger');
                        /* the button stays off until a file is read again: a second press would add them twice */
                        found = null;
                    }
                });
            });
        });
    });
</script>

<div class="content-box">
    <table class="table table-striped table-condensed">
        <thead>
            <tr><th colspan="2"><div style="padding: 5px 0; font-size: 16px;"><b>{{ lang._('A certificate file into System: Trust') }}</b></div></th></tr>
        </thead>
        <tbody>
            <tr>
                <td style="width: 25%;">{{ lang._('File') }}</td>
                <td>
                    <input type="file" id="ci_file" accept=".der,.cer,.crt,.pem,.p7b,.p7c,.pfx,.p12"/>
                    <div class="text-muted"><small>{{ lang._('DER (.der, .cer, .crt), PKCS#7 (.p7b, .p7c) or PKCS#12 (.pfx, .p12), or PEM. System: Trust imports PEM text alone; this page reads the others on the firewall into PEM and hands each certificate to Trust, which checks and stores it as any import.') }}</small></div>
                </td>
            </tr>
            <tr>
                <td>{{ lang._('Password') }}</td>
                <td>
                    <input type="password" id="ci_password" maxlength="256" style="max-width: 360px;" autocomplete="new-password"/>
                    <div class="text-muted"><small>{{ lang._('The PKCS#12 file\'s, or an encrypted PEM key\'s. Used to open the file and not kept.') }}</small></div>
                </td>
            </tr>
            <tr>
                <td>{{ lang._('Description') }}</td>
                <td>
                    <input type="text" id="ci_descr" maxlength="200" style="max-width: 360px;" autocomplete="off"/>
                    <div class="text-muted"><small>{{ lang._('Put in front of each certificate\'s name in System: Trust. Empty: its name alone.') }}</small></div>
                </td>
            </tr>
            <tr>
                <td></td>
                <td>
                    <button class="btn btn-default" id="ci_read" type="button"><i class="fa fa-fw fa-file-import"></i> {{ lang._('Read the file') }}</button>
                    <button class="btn btn-primary" id="ci_import" type="button" disabled="disabled"><i class="fa fa-fw fa-certificate"></i> {{ lang._('Import into System: Trust') }}</button>
                </td>
            </tr>
        </tbody>
    </table>
    <table class="table table-condensed" id="ci_list">
        <thead>
            <tr>
                <th>{{ lang._('Name') }}</th>
                <th>{{ lang._('Issued by') }}</th>
                <th>{{ lang._('Valid until') }}</th>
                <th>{{ lang._('Imported as') }}</th>
            </tr>
        </thead>
        <tbody></tbody>
    </table>
    <div style="padding: 0 15px 15px;">
        <div id="ci_msg" style="white-space: pre-line; padding: 6px 10px;"></div>
    </div>
</div>

{{ partial("OPNsense/XGS/credit") }}
