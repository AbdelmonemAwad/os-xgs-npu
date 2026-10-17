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
        /* The installed version, the last check, and whether an update waits for a boot. */
        function short(sha) {
            return sha ? String(sha).substring(0, 12) : '';
        }
        function when(iso) {
            return iso ? String(iso).replace('T', ' ').replace('Z', ' UTC') : '';
        }
        function link(sha) {
            return $('<a target="_blank" rel="noopener"/>')
                .attr('href', 'https://github.com/AbdelmonemAwad/os-xgs-npu/commit/' + sha)
                .text(short(sha));
        }

        function show_installed(inst) {
            const $box = $('#installed').empty();
            if (!inst || !inst.commit) {
                $box.text("{{ lang._('Not known: this installation was made before installs recorded their version.') }}");
                return;
            }
            $box.append(link(inst.commit))
                .append(document.createTextNode(' - ' + when(inst.date) + ' (' + (inst.source || '') + ')'));
        }

        function show_check(chk) {
            const $res = $('#check-result').empty().attr('class', '');
            /* What changed is a tab of its own, as the changelog is on System: Firmware; it says
               so when there is nothing to list rather than standing empty. */
            const $list = $('#changes').empty();
            $('#changes-count').text('');
            $list.append($('<tr/>').append($('<td colspan="3" class="text-muted"/>').text(
                "{{ lang._('Nothing to list: the installed version is the newest, or no check has run.') }}")));
            $('#updateAct').prop('disabled', true);
            if (!chk || !chk.status) {
                $res.attr('class', 'text-muted').text("{{ lang._('Not checked yet.') }}");
                return;
            }
            if (chk.status !== 'ok') {
                $res.attr('class', 'text-warning').text(chk.message || "{{ lang._('The check failed.') }}");
                return;
            }
            if (!chk.update_available) {
                $res.attr('class', 'text-success').text("{{ lang._('Up to date with the newest version whose checks passed.') }}");
                return;
            }
            if ((chk.changes || []).length) {
                $list.empty();
            }
            $res.attr('class', 'text-info')
                .append(document.createTextNode("{{ lang._('Available:') }} "))
                .append(link(chk.target.sha))
                .append(document.createTextNode(' - ' + when(chk.target.date) +
                    (chk.ahead_by ? ' - ' + chk.ahead_by + " {{ lang._('change(s)') }}" : '')));
            /* the installed commit is not on GitHub, so what changed since it cannot be listed */
            if (chk.installed_unknown) {
                $res.append($('<div class="text-muted"/>').text(
                    "{{ lang._('The installed version is not on GitHub - a build from a local or deleted branch - so it cannot be compared; this is the newest version whose checks passed.') }}"));
            }
            (chk.changes || []).forEach(function (c) {
                $list.append($('<tr/>')
                    .append($('<td style="white-space: nowrap;"/>').text(when(c.date)))
                    .append($('<td/>').append(link(c.sha)))
                    .append($('<td/>').text(c.subject)));
            });
            $('#changes-count').text((chk.changes || []).length ? (chk.changes || []).length : '');
            $('#updateAct').prop('disabled', false);
        }

        function show_apply(st) {
            const ap = st.apply || {};
            $('#reboot-row').toggle(!!st.pending_reboot);
            /* The installer's log is on its own tab, as the firmware's update log is; a running
               or failed update says so here and the log is one click away. */
            if (ap.status === 'running') {
                $('#apply-row').show();
                $('#apply-state').attr('class', 'text-info').text("{{ lang._('Updating - this builds the driver and takes a few minutes. The firewall keeps running.') }}");
                return true;
            }
            if (ap.status === 'failed') {
                $('#apply-row').show();
                $('#apply-state').attr('class', 'text-danger').html(ap.message || "{{ lang._('The update failed.') }}");
            } else if (ap.status === 'done') {
                $('#apply-row').show();
                $('#apply-state').attr('class', 'text-success').text(st.pending_reboot ?
                    "{{ lang._('Installed. The coprocessor driver takes effect at the next boot.') }}" :
                    "{{ lang._('Installed, and loaded by the boot since.') }}");
            }
            return false;
        }

        let polling = null;
        function refresh() {
            ajaxGet('/api/xgs/updates/status', {}, function (st) {
                if (!st || st.status !== 'ok') {
                    return;
                }
                show_installed(st.installed);
                show_check(st.check);
                const running = show_apply(st);
                /* Nothing to press while an update runs: the check would offer the same version. */
                $('#checkAct').prop('disabled', running);
                if (running) {
                    $('#updateAct').prop('disabled', true);
                }
                ajaxGet('/api/xgs/updates/log', {}, function (lg) {
                    $('#apply-log').html((lg && lg.log) ||
                        "{{ lang._('No update has run on this firewall from this page.') }}");
                });
                if (running && polling === null) {
                    polling = setInterval(refresh, 3000);
                } else if (!running && polling !== null) {
                    clearInterval(polling);
                    polling = null;
                }
            });
        }

        $('#checkAct').click(function () {
            const $b = $(this).prop('disabled', true);
            $('#check-result').attr('class', 'text-muted').text("{{ lang._('Asking GitHub...') }}");
            ajaxCall('/api/xgs/updates/check', {}, function () {
                $b.prop('disabled', false);
                refresh();
            });
        });

        $('#updateAct').click(function () {
            BootstrapDialog.confirm({
                title: "{{ lang._('Update the coprocessor driver') }}",
                message: "{{ lang._('This downloads the version shown, runs its installer and builds the driver for the running kernel. The driver that is running now stays until the next boot, so the firewall keeps forwarding meanwhile.') }}",
                type: BootstrapDialog.TYPE_INFO,
                btnCancelLabel: "{{ lang._('Cancel') }}",
                btnOKLabel: "{{ lang._('Update') }}",
                callback: function (ok) {
                    if (ok) {
                        $('#updateAct').prop('disabled', true);
                        ajaxCall('/api/xgs/updates/apply', {}, function () {
                            setTimeout(refresh, 1500);
                            if (polling === null) {
                                polling = setInterval(refresh, 3000);
                            }
                        });
                    }
                }
            });
        });

        $('#rebootAct').click(function () {
            BootstrapDialog.confirm({
                title: "{{ lang._('Reboot now') }}",
                message: "{{ lang._('The firewall reboots now and loads the new driver. Traffic stops until it is back - a few minutes.') }}",
                type: BootstrapDialog.TYPE_WARNING,
                btnCancelLabel: "{{ lang._('Later') }}",
                btnOKLabel: "{{ lang._('Reboot now') }}",
                callback: function (ok) {
                    if (ok) {
                        ajaxCall('/api/xgs/updates/reboot', {}, function () {
                            $('#reboot-cell').empty().append($('<span class="text-info"/>').text(
                                "{{ lang._('Rebooting. Reload this page once the firewall is back.') }}"));
                        });
                    }
                }
            });
        });

        refresh();
    });
</script>

{# Laid out as System: Firmware is: tabs, and on the first a striped table of what is installed
   and what is offered, with the buttons in its last row. #}
<ul class="nav nav-tabs" data-tabs="tabs" id="maintabs">
    <li class="active"><a data-toggle="tab" href="#status">{{ lang._('Status') }}</a></li>
    <li><a data-toggle="tab" href="#changelog">{{ lang._('What changed') }} <span id="changes-count" class="badge"></span></a></li>
    <li><a data-toggle="tab" href="#installer">{{ lang._('Installer log') }}</a></li>
</ul>

<div class="tab-content content-box">
    <div id="status" class="tab-pane active table-responsive">
        <table class="table table-striped table-condensed">
            <tbody>
                <tr>
                    <td style="width: 150px;">{{ lang._('Source') }}</td>
                    <td>{{ lang._('GitHub: the newest version whose checks passed. System: Firmware does not list this plugin, and nothing here reboots on its own.') }}</td>
                </tr>
                <tr>
                    <td>{{ lang._('Installed') }}</td>
                    <td id="installed"></td>
                </tr>
                <tr>
                    <td>{{ lang._('Latest') }}</td>
                    <td id="check-result"></td>
                </tr>
                <tr id="apply-row" style="display: none;">
                    <td>{{ lang._('Last update') }}</td>
                    <td id="apply-state"></td>
                </tr>
                <tr id="reboot-row" style="display: none;">
                    <td>{{ lang._('Reboot') }}</td>
                    <td id="reboot-cell">
                        <span class="text-warning">{{ lang._('An update is installed and waits for a reboot to load the new driver.') }}</span>
                        <button class="btn btn-warning btn-xs" id="rebootAct" type="button">{{ lang._('Reboot now') }}</button>
                    </td>
                </tr>
                <tr>
                    <td></td>
                    <td>
                        <button class="btn btn-primary" id="checkAct" type="button">
                            <i class="fa fa-fw fa-refresh"></i> {{ lang._('Check for updates') }}
                        </button>
                        <button class="btn btn-primary" id="updateAct" type="button" disabled="disabled">
                            <i class="fa fa-fw fa-download"></i> {{ lang._('Update') }}
                        </button>
                    </td>
                </tr>
            </tbody>
        </table>
    </div>

    <div id="changelog" class="tab-pane table-responsive">
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th style="width: 180px;">{{ lang._('Date') }}</th>
                    <th style="width: 130px;">{{ lang._('Commit') }}</th>
                    <th>{{ lang._('Change') }}</th>
                </tr>
            </thead>
            <tbody id="changes"></tbody>
        </table>
    </div>

    <div id="installer" class="tab-pane" style="padding: 15px;">
        <pre id="apply-log" style="max-height: 480px; overflow: auto; white-space: pre-wrap; margin: 0;"></pre>
    </div>
</div>
