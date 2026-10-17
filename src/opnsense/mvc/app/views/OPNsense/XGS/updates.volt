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
        $('#apply-log-toggle').click(function (e) {
            e.preventDefault();
            const open = !($('#apply-log').data('open') === true);
            $('#apply-log').data('open', open).toggle(open);
        });

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
            const $list = $('#changes').empty();
            $('#changes-box').hide();
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
            $res.attr('class', 'text-info')
                .append(document.createTextNode("{{ lang._('Available:') }} "))
                .append(link(chk.target.sha))
                .append(document.createTextNode(' - ' + when(chk.target.date) +
                    (chk.ahead_by ? ' - ' + chk.ahead_by + " {{ lang._('change(s)') }}" : '')));
            (chk.changes || []).forEach(function (c) {
                $list.append($('<tr/>')
                    .append($('<td style="white-space: nowrap;"/>').text(when(c.date)))
                    .append($('<td/>').append(link(c.sha)))
                    .append($('<td/>').text(c.subject)));
            });
            $('#changes-box').toggle((chk.changes || []).length > 0);
            $('#updateAct').prop('disabled', false);
        }

        function show_apply(st) {
            const ap = st.apply || {};
            $('#reboot-box').toggle(!!st.pending_reboot);
            /* The installer's log is what to read while it runs or when it failed; after a good
               update it is folded away behind a link. */
            $('#apply-log').toggle(ap.status !== 'done' || $('#apply-log').data('open') === true);
            $('#apply-log-toggle').toggle(ap.status === 'done');
            if (ap.status === 'running') {
                $('#apply-box').show();
                $('#apply-state').attr('class', 'text-info').text("{{ lang._('Updating - this builds the driver and takes a few minutes. The firewall keeps running.') }}");
                return true;
            }
            if (ap.status === 'failed') {
                $('#apply-box').show();
                $('#apply-state').attr('class', 'text-danger').html(ap.message || "{{ lang._('The update failed.') }}");
            } else if (ap.status === 'done') {
                $('#apply-box').show();
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
                    $('#apply-log').html((lg && lg.log) || '');
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
                            $('#reboot-box').html('<div class="alert alert-info">' +
                                "{{ lang._('Rebooting. Reload this page once the firewall is back.') }}" + '</div>');
                        });
                    }
                }
            });
        });

        refresh();
    });
</script>

<div class="content-box" style="padding: 15px;">
    <div class="alert alert-info" role="alert">
        <i class="fa fa-fw fa-info-circle"></i>
        {{ lang._('The coprocessor driver and its pages are not in the OPNsense package repository, so System: Firmware: Updates does not offer them. This page does: it offers the newest version on GitHub whose checks passed, shows what changed, and installs it with its own installer. The new driver is loaded at the next boot - this page never reboots on its own.') }}
    </div>

    <div id="reboot-box" style="display: none;">
        <div class="alert alert-warning" role="alert">
            <i class="fa fa-fw fa-refresh"></i>
            {{ lang._('An update is installed and waits for a reboot to load the new driver.') }}
            <button class="btn btn-warning btn-xs" id="rebootAct" type="button" style="margin-left: 10px;">
                {{ lang._('Reboot now') }}
            </button>
        </div>
    </div>

    <table class="table table-condensed">
        <tbody>
            <tr><td style="width: 22%;"><strong>{{ lang._('Installed') }}</strong></td><td id="installed"></td></tr>
            <tr><td><strong>{{ lang._('Latest') }}</strong></td><td id="check-result"></td></tr>
        </tbody>
    </table>

    <button class="btn btn-default" id="checkAct" type="button">
        <i class="fa fa-fw fa-search"></i> {{ lang._('Check for updates') }}
    </button>
    <button class="btn btn-primary" id="updateAct" type="button" disabled="disabled">
        <i class="fa fa-fw fa-download"></i> {{ lang._('Update') }}
    </button>

    <div id="changes-box" style="display: none; margin-top: 15px;">
        <h4>{{ lang._('What changed') }}</h4>
        <table class="table table-condensed table-striped">
            <tbody id="changes"></tbody>
        </table>
    </div>

    <div id="apply-box" style="display: none; margin-top: 15px;">
        <h4>{{ lang._('Update') }}</h4>
        <p id="apply-state"></p>
        <a href="#" id="apply-log-toggle" style="display: none;"><i class="fa fa-chevron-down"></i> {{ lang._('The installer log') }}</a>
        <pre id="apply-log" style="max-height: 320px; overflow: auto; white-space: pre-wrap; margin-top: 8px;"></pre>
    </div>
</div>
