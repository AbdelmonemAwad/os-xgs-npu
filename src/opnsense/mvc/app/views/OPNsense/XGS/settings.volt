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
            'armada': ['ipsec_soft_crypto', 'rx_poll_ms', 'rx_budget', 'rx_doorbells']
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

        /* The Intrusion Detection, when what the coprocessor carries no longer reaches it (status.py,
           intrusion_detection): said here and on XGS: Overview, because nothing else says it. */
        function ids_note(ids) {
            if (!ids || ids.uninspected !== true) {
                return '';
            }
            const where = $('<span/>').text((ids.interfaces || []).join(', ')).html();
            let text = '<p><b>' + "{{ lang._('The Intrusion Detection does not see what the coprocessor carries') }}" + '</b></p><p>' +
                (ids.mode === 'ids' ?
                    "{{ lang._('It listens on %s in IDS mode. A connection the coprocessor carries no longer crosses this firewall, so Suricata sees how each one starts and nothing after it: an alert that would come later does not come.') }}" :
                    "{{ lang._('It inspects %s in IPS mode. A connection the coprocessor carries no longer crosses this firewall, so Suricata sees how each one starts and nothing after it, and cannot drop what comes later.') }}"
                ).replace('%s', where) + '</p>';
            if (ids.mode !== 'ids') {
                text += '<p>' + "{{ lang._('Inspect, then hardware, on the Security tab of XGS: Settings, sends each new connection through it first, for its first megabyte; it needs the Netmap mode and Cut in hardware.') }}" + '</p>';
            }
            text += '<p>' + "{{ lang._('For Suricata to see every frame, list what it must see under Never offloaded, or turn Accelerate connections automatically off: this firewall then carries it, at its own speed.') }}" + '</p>';
            return '<div class="alert alert-warning" role="alert" style="margin: 15px 0 0;">' + text + '</div>';
        }

        /* Where each Captive Portal zone runs and why, as cp_users.py last decided (status.py,
           captive_portal): the reasons are codes, worded here. */
        function cp_zones(cp) {
            if (!cp) {
                return '<span class="text-muted">' + "{{ lang._('Not decided yet: the watcher has not run since the plugin was installed.') }}" + '</span>';
            }
            if (!cp.zones || cp.zones.length === 0) {
                return '<span class="text-muted">' + "{{ lang._('No Captive Portal zone is enabled.') }}" + '</span>';
            }
            const overlay = {
                'not_applied': "{{ lang._("the change to the portal's accounting file is not applied.") }}",
                'unknown_version': "{{ lang._("the portal's accounting file is a version this plugin was not written for, so it was left as it is.") }}",
                'no_file': "{{ lang._("the portal's accounting file was not found.") }}"
            };
            return cp.zones.map(function (z) {
                const hw = z.where === 'hardware';
                let why = '';
                if (z.reason === 'off') {
                    why = "{{ lang._('Captive Portal clients in hardware is off.') }}";
                } else if (z.reason === 'no_counts') {
                    why = "{{ lang._('Its idle timeout or RADIUS accounting needs what the coprocessor carries counted, and that cannot reach the portal:') }}" +
                        ' ' + (overlay[cp.overlay.code] || overlay.not_applied);
                } else if (z.reason === 'counted') {
                    why = "{{ lang._('Its idle timeout or RADIUS accounting is given what the coprocessor carries.') }}";
                }
                if (hw) {
                    why += (why ? ' ' : '') + "{{ lang._('Clients now: %s') }}".replace('%s', String(z.clients || 0));
                }
                return '<div><b>' + $('<span/>').text(z.name || '').html() + '</b> ' +
                    badge(hw, "{{ lang._('In hardware') }}", "{{ lang._('With the firewall') }}") +
                    (why ? ' <span class="text-muted">' + why + '</span>' : '') + '</div>';
            }).join('');
        }

        /* What the coprocessor and the driver have now, read back from them, in the form's own
           Status rows. The API escapes what it returns; it is inserted as it comes. */
        function show_live_state() {
            ajaxGet('/api/xgs/status/get', {}, function (data) {
                const bit = data && data.offload ? data.offload.offload_bit_requested : null;
                $('#xgs_ids').html(ids_note(data && data.intrusion_detection));
                $('#xgs_now_cp').html(cp_zones(data && data.captive_portal));
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

        /* the tab in the address, as the other pages keep it */
        if (window.location.hash !== '') {
            $('a[href="' + window.location.hash + '"]').tab('show');
        }
        $('.nav-tabs a').on('shown.bs.tab', function (e) {
            history.pushState(null, null, e.target.hash);
        });

        /* Each tab has the Apply its form is drawn with; any of them saves every tab. */
        $('button[id^="save_xgs-"]').click(function () {
            const $button = $(this);
            const $progress = $button.find('i');
            $button.prop('disabled', true);
            $progress.addClass('fa fa-spinner fa-pulse');
            saveFormToEndpoint('/api/xgs/settings/set', 'frm_general', function () {
                ajaxCall('/api/xgs/offload/apply', {}, function (data) {
                    $progress.removeClass('fa fa-spinner fa-pulse');
                    $button.prop('disabled', false);
                    report(data);
                    show_live_state();
                });
            }, false, function () {
                $progress.removeClass('fa fa-spinner fa-pulse');
                $button.prop('disabled', false);
                /* a setting refused on another tab: open that tab */
                const $pane = $('#frm_general .has-error').first().closest('.tab-pane');
                if ($pane.length && !$pane.hasClass('active')) {
                    $('a[href="#' + $pane.attr('id') + '"]').tab('show');
                }
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

<div id="xgs_ids"></div>

<ul class="nav nav-tabs" data-tabs="tabs" id="maintabs">
    {{ partial("layout_partials/base_tabs_header", ['formData': generalForm]) }}
</ul>

{# One form around the tabs, as OPNsense's own IPsec settings page has it: a save reads every tab's
   fields from it. #}
<form id="frm_general">
    <div class="content-box tab-content">
        {{ partial("layout_partials/base_tabs_content", ['formData': generalForm]) }}
    </div>
</form>
<div class="content-box" style="padding: 0 15px;">
    <div id="reboot-needed" class="alert alert-warning" role="alert" style="display: none; margin: 15px 0 0;">
        <i class="fa fa-fw fa-power-off"></i>
        {{ lang._('Some of these settings take effect at the next reboot (Power: Reboot). The coprocessor itself has no restart of its own that is safe while the firewall runs: it is reset at every boot of the appliance, and that is the way to restart it.') }}
    </div>
    <div id="apply-result" style="display: none; margin: 15px 0;"></div>
</div>

{{ partial("OPNsense/XGS/credit") }}
