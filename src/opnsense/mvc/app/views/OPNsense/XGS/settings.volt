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
    $(document).ready(function () {

        /* Settings that exist on one family only. Each is hidden on the other, and a section left
           with nothing visible is hidden with it, so the page shows what this appliance has. */
        const only = {
            'octeontx': ['ipsec_flows'],
            'armada': ['ipsec_soft_crypto', 'rx_poll_ms', 'rx_budget']
        };

        function show_family(family) {
            Object.keys(only).forEach(function (fam) {
                only[fam].forEach(function (name) {
                    $('tr[id="row_xgs.general.' + name + '"]').toggle(fam === family);
                });
            });
            $('#frm_general table').each(function () {
                const $rows = $(this).find('tbody > tr');
                if ($rows.length) {
                    $(this).closest('.table-responsive').toggle($rows.filter(function () {
                        return $(this).css('display') !== 'none';
                    }).length > 0);
                }
            });
        }

        /* What the last apply did, step by step. The button says "Save and apply" because the two
           are never worth separating here: a setting on its own changes nothing until it is
           written, and a page that let them drift would show offloading as on while the hardware
           had it off. */
        function report(data) {
            const $box = $('#apply-result');
            if (!data || typeof data.message !== 'string') {
                $box.attr('class', 'alert alert-danger').text(
                    "{{ lang._('The apply did not return anything that could be read.') }}");
                return;
            }
            const $list = $('<ul style="margin: 0;"/>');
            (data.steps || []).forEach(function (step) {
                $list.append($('<li/>').html(step));
            });
            $box.attr('class', data.applied ? 'alert alert-success' : 'alert alert-danger').empty().append($list);
            $('#reboot-needed').toggle(data.reboot === true);
        }

        /* What the coprocessor has, as opposed to what config.xml says.
           These two can differ and the difference is the whole reason this is here: the gate is
           re-applied at every boot by a script that runs before syslogd, so a failure there leaves
           no message anywhere. Reading the value is the only honest way to show it. */
        function show_live_state() {
            ajaxCall('/api/xgs/status/get', {}, function (data) {
                const $box = $('#live-state');
                const bit = data && data.offload ? data.offload.offload_bit_requested : null;
                show_family(data && data.board ? data.board.family : null);
                if (bit === true) {
                    $box.attr('class', 'text-success')
                        .html('<i class="fa fa-check-circle"></i> ' +
                              "{{ lang._('The coprocessor currently has offloading on.') }}");
                } else if (bit === false) {
                    $box.attr('class', 'text-muted')
                        .html('<i class="fa fa-circle-o"></i> ' +
                              "{{ lang._('The coprocessor currently has offloading off.') }}");
                } else if (data && data.present) {
                    $box.attr('class', 'text-muted')
                        .html('<i class="fa fa-question-circle"></i> ' +
                              "{{ lang._('The driver is loaded, but the gate has not been written since boot - save and apply writes it.') }}");
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

        $('#reassignAct').click(function () {
            stdDialogConfirm(
                "{{ lang._('Assign the ports again') }}",
                "{{ lang._('The next boot runs the first-boot assignment again: a 30-second countdown on the console, then the ports assigned by their names. It acts only if no front port is assigned by then - with any one assigned it changes nothing and marks itself done again.') }}",
                "{{ lang._('Yes') }}", "{{ lang._('Cancel') }}",
                function () {
                    ajaxCall('/api/xgs/settings/reassign', {}, function (data) {
                        const ok = data && data.status === 'ok';
                        $('#reassign-result').attr('class', ok ? 'text-success' : 'text-danger').text(ok ?
                            "{{ lang._('Done: the ports are assigned again at the next boot.') }}" :
                            "{{ lang._('The request was not accepted.') }}");
                    });
                });
        });
    });
</script>

<div class="content-box">
    <div class="col-md-12" style="padding: 15px 15px 0;">
        <div class="alert alert-info" role="alert">
            <i class="fa fa-fw fa-info-circle"></i>
            {{ lang._('Save and apply writes each setting where it takes effect: most at once, the IPsec ones at the next reboot. Every boot applies them all again, because the coprocessor remembers none of them.') }}
        </div>
        <div class="alert alert-warning" role="alert">
            <i class="fa fa-fw fa-exclamation-triangle"></i>
            {{ lang._('A frame the coprocessor forwards does not reach the firewall rules. Hardware offloading only opens the gate; nothing leaves the firewall path until Accelerate connections automatically is on as well.') }}
        </div>
        <div id="reboot-needed" class="alert alert-warning" role="alert" style="display: none;">
            <i class="fa fa-fw fa-power-off"></i>
            {{ lang._('Some of these settings take effect at the next reboot (Power: Reboot). The coprocessor itself has no restart of its own that is safe while the firewall runs: it is reset at every boot of the appliance, and that is the way to restart it.') }}
        </div>
    </div>
    {{ partial("layout_partials/base_form", ['fields': generalForm, 'id': 'frm_general']) }}
    <div class="col-md-12" style="padding: 10px 15px 20px;">
        <button class="btn btn-primary" id="saveAct" type="button"><b>{{ lang._('Save and apply') }}</b></button>
        <span id="live-state" style="margin: 0 15px;"></span>
        <div id="apply-result" style="margin-top: 15px;"></div>
    </div>
    <div class="col-md-12" style="padding: 0 15px 20px;">
        <h2>{{ lang._('Port assignment') }}</h2>
        <p class="text-muted">{{ lang._('The first boot assigns the ports by itself, from the names the drivers give them, and then never again. To have it run once more - after removing the front ports from Interfaces: Assignments, to start from its plan - ask for it here; it runs at the next boot, and only if no front port is assigned by then.') }}</p>
        <button class="btn btn-default" id="reassignAct" type="button">{{ lang._('Assign the ports again at the next boot') }}</button>
        <span id="reassign-result" style="margin: 0 15px;"></span>
    </div>
</div>
