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
    $(document).ready(function () {

        /* What the coprocessor said to the last apply. The button says "Save and apply" because
           the two are never worth separating here: the setting on its own changes nothing until
           it is written, and a page that let them drift would show offloading as on while the
           hardware had it off. */
        function report(data) {
            const $box = $('#apply-result');
            if (!data || typeof data.message !== 'string') {
                $box.attr('class', 'alert alert-danger').text(
                    "{{ lang._('The apply did not return anything that could be read.') }}");
                return;
            }
            $box.attr('class', data.applied ? 'alert alert-success' : 'alert alert-danger')
                .text(data.message);
        }

        /* What the coprocessor has, as opposed to what config.xml says.
           These two can differ and the difference is the whole reason this is here: the gate is
           re-applied at every boot by a script that runs before syslogd, so a failure there leaves
           no message anywhere. Reading the value is the only honest way to show it. */
        function show_live_state() {
            ajaxCall('/api/xgs/status/get', {}, function (data) {
                const $box = $('#live-state');
                const bit = data && data.offload ? data.offload.offload_bit_requested : null;
                if (bit === true) {
                    $box.attr('class', 'text-success')
                        .html('<i class="fa fa-check-circle"></i> ' +
                              "{{ lang._('The coprocessor currently has offloading on.') }}");
                } else if (bit === false) {
                    $box.attr('class', 'text-muted')
                        .html('<i class="fa fa-circle-o"></i> ' +
                              "{{ lang._('The coprocessor currently has offloading off.') }}");
                } else {
                    $box.attr('class', 'text-warning')
                        .html('<i class="fa fa-question-circle"></i> ' +
                              "{{ lang._('The coprocessor could not be read - the driver may not be loaded.') }}");
                }
            });
        }

        const data_get_map = {'frm_general': '/api/xgs/settings/get'};
        mapDataToFormUI(data_get_map).done(function () {
            $('.selectpicker').selectpicker('refresh');
            show_live_state();
        });

        $('#saveAct').click(function () {
            const $button = $(this);
            $button.prop('disabled', true);
            saveFormToEndpoint('/api/xgs/settings/set', 'frm_general', function () {
                $('#OPNsenseStdWaitDialog').modal('show');
                ajaxCall('/api/xgs/offload/apply', {}, function (data) {
                    $('#OPNsenseStdWaitDialog').modal('hide');
                    $button.prop('disabled', false);
                    report(data);
                    show_live_state();
                });
            }, false, function () {
                $button.prop('disabled', false);
            });
        });
    });
</script>

<div class="content-box">
    <div class="col-md-12" style="padding: 15px 15px 0;">
        <div class="alert alert-info" role="alert">
            <i class="fa fa-fw fa-info-circle"></i>
            {{ lang._('Offloading lets the coprocessor forward traffic it recognises without handing it to the firewall, which is the point of it and the reason it is off by default: a frame the coprocessor handles does not reach the firewall rules. The setting is written to the coprocessor when you save, and again at every boot - the coprocessor does not remember it.') }}
        </div>
        <div class="alert alert-warning" role="alert">
            <i class="fa fa-fw fa-exclamation-triangle"></i>
            {{ lang._('As of this version, turning this on accelerates nothing. The gate opens, the coprocessor accepts it, and every frame still reaches the firewall, because no traffic flow is ever activated on the coprocessor. It is here because it is the one setting this hardware has, it is measured, and it is safe to turn on and off; it is not here because it makes the appliance faster yet.') }}
        </div>
    </div>
    {{ partial("layout_partials/base_form", ['fields': generalForm, 'id': 'frm_general']) }}
    <div class="col-md-12" style="padding: 10px 15px 20px;">
        <button class="btn btn-primary" id="saveAct" type="button"><b>{{ lang._('Save and apply') }}</b></button>
        <span id="live-state" style="margin: 0 15px;"></span>
        <div id="apply-result" style="margin-top: 15px;"></div>
    </div>
</div>
