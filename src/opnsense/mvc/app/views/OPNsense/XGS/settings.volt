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
            'octeontx': [],
            'armada': ['ipsec_soft_crypto', 'rx_poll_ms', 'rx_budget']
        };

        function show_family(family) {
            Object.keys(only).forEach(function (fam) {
                only[fam].forEach(function (name) {
                    $('tr[id="row_xgs.general.' + name + '"]').toggle(fam === family);
                });
            });
            $('#frm_general table').each(function () {
                const $rows = $(this).find('tbody > tr[id^="row_"]');
                if ($rows.length) {
                    $(this).closest('.table-responsive').toggle($rows.filter(function () {
                        return $(this).css('display') !== 'none';
                    }).length > 0);
                }
            });
        }

        function badge(on, yes, no) {
            return '<span class="label label-' + (on ? 'success' : 'default') + '">' + (on ? yes : no) + '</span>';
        }

        /* What the coprocessor and the driver have now, read back from them, in the form's own
           Status rows. The API escapes what it returns; it is inserted as it comes. */
        function show_live_state() {
            ajaxGet('/api/xgs/status/get', {}, function (data) {
                const bit = data && data.offload ? data.offload.offload_bit_requested : null;
                show_family(data && data.board ? data.board.family : null);
                $('#xgs_now_offload').html(bit === true || bit === false ?
                    badge(bit, "{{ lang._('On') }}", "{{ lang._('Off') }}") :
                    '<span class="text-warning">' + (data && data.present ?
                        "{{ lang._('The driver is loaded, but the gate has not been written since boot - Apply writes it.') }}" :
                        "{{ lang._('The coprocessor could not be read - the driver may not be loaded.') }}") + '</span>');
            });
            ajaxGet('/api/xgs/ipsec/get', {}, function (data) {
                const on = data && data.tunable && data.tunable.value === '1';
                $('#xgs_now_ipsec').html(badge(on, "{{ lang._('On') }}", "{{ lang._('Off') }}"));
            });
        }

        /* What the last Apply did, step by step, and whether a reboot is needed for the rest. */
        function report(data) {
            const $box = $('#apply-result');
            if (!data || typeof data.message !== 'string') {
                $box.attr('class', 'alert alert-danger').text(
                    "{{ lang._('The apply did not return anything that could be read.') }}").show();
                return;
            }
            const $list = $('<ul style="margin: 0;"/>');
            (data.steps || []).forEach(function (step) {
                $list.append($('<li/>').html(step));
            });
            $box.attr('class', data.applied ? 'alert alert-success' : 'alert alert-danger').empty().append($list).show();
            $('#reboot-needed').toggle(data.reboot === true);
        }

        const data_get_map = {'frm_general': '/api/xgs/settings/get'};
        mapDataToFormUI(data_get_map).done(function () {
            $('.selectpicker').selectpicker('refresh');
            show_live_state();
        });

        $('#xgs_reassign').html('<button class="btn btn-default btn-xs" id="reassignAct" type="button">' +
            "{{ lang._('Assign the ports again at the next boot') }}" + '</button> <span id="reassign-result"></span>');

        $('#saveAct').click(function () {
            const $button = $(this);
            $button.prop('disabled', true);
            $('#frm_general_progress').addClass('fa fa-spinner fa-pulse');
            saveFormToEndpoint('/api/xgs/settings/set', 'frm_general', function () {
                ajaxCall('/api/xgs/offload/apply', {}, function (data) {
                    $('#frm_general_progress').removeClass('fa fa-spinner fa-pulse');
                    $button.prop('disabled', false);
                    report(data);
                    show_live_state();
                });
            }, false, function () {
                $('#frm_general_progress').removeClass('fa fa-spinner fa-pulse');
                $button.prop('disabled', false);
            });
        });

        $(document).on('click', '#reassignAct', function () {
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
    {{ partial("layout_partials/base_form", ['fields': generalForm, 'id': 'frm_general', 'apply_btn_id': 'saveAct']) }}
</div>
<div class="content-box" style="padding: 0 15px;">
    <div id="reboot-needed" class="alert alert-warning" role="alert" style="display: none; margin: 15px 0 0;">
        <i class="fa fa-fw fa-power-off"></i>
        {{ lang._('Some of these settings take effect at the next reboot (Power: Reboot). The coprocessor itself has no restart of its own that is safe while the firewall runs: it is reset at every boot of the appliance, and that is the way to restart it.') }}
    </div>
    <div id="apply-result" style="display: none; margin: 15px 0;"></div>
</div>

{{ partial("OPNsense/XGS/credit") }}
