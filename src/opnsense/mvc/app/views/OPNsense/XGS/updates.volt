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

<style>
    /* Summary cards, as on the Overview: one for each thing a glance is for, in the theme's
       colours (its text and a faint tint of it), so they read the same in both themes. */
    .xgs-cards { display: flex; flex-wrap: wrap; gap: 12px; padding: 15px; }
    .xgs-card { flex: 1 1 200px; min-width: 190px; border: 1px solid rgba(128, 128, 128, 0.25);
                border-inline-start: 4px solid #9e9e9e; border-radius: 3px; padding: 10px 14px;
                background-color: rgba(128, 128, 128, 0.04); }
    .xgs-card.ok { border-inline-start-color: #5cb85c; }
    .xgs-card.warn { border-inline-start-color: #f0ad4e; }
    .xgs-card.bad { border-inline-start-color: #d9534f; }
    .xgs-card .k { font-size: 11px; text-transform: uppercase; letter-spacing: .04em; opacity: 0.6; }
    .xgs-card .v { font-size: 19px; font-weight: 600; line-height: 1.3; margin: 2px 0; }
    .xgs-card .s { font-size: 12px; opacity: 0.7; }
    .xgs-card .fa { opacity: 0.35; float: inline-end; font-size: 18px; margin-top: 2px; }

    /* An update's progress: the theme's own Bootstrap bar, and under it the six steps, each
       marked done, in hand or still to come - in the theme's colours, as the cards are. */
    .xgs-progress { max-width: 760px; }
    .xgs-progress .progress { height: 22px; margin: 2px 0 10px; }
    .xgs-progress .progress-bar { line-height: 22px; font-weight: 600; min-width: 3em; transition: width .6s ease; }
    /* In Arabic the bar fills from the right, as the text runs (opnsense-rtl sets html[dir=rtl]). */
    html[dir=rtl] .xgs-progress .progress-bar { float: right; }
    .xgs-steps { display: flex; flex-wrap: wrap; gap: 6px; list-style: none; margin: 0 0 8px; padding: 0; }
    .xgs-steps li { display: inline-flex; align-items: center; gap: 6px; font-size: 12px; padding: 3px 10px 3px 8px;
                    border: 1px solid rgba(128, 128, 128, 0.3); border-radius: 12px; opacity: 0.55; }
    .xgs-steps li .fa { width: 12px; text-align: center; }
    .xgs-steps li.done { opacity: 0.9; border-color: #5cb85c; }
    .xgs-steps li.done .fa { color: #5cb85c; }
    .xgs-steps li.on { opacity: 1; border-color: #5bc0de; font-weight: 600; }
    .xgs-steps li.on .fa { color: #5bc0de; }
    .xgs-steps li.bad { opacity: 1; border-color: #d9534f; font-weight: 600; }
    .xgs-steps li.bad .fa { color: #d9534f; }
    .xgs-progress .xgs-now { font-size: 13px; }
    .xgs-progress .xgs-clock { font-variant-numeric: tabular-nums; opacity: 0.7; margin-inline-start: 6px; }
</style>

<script>
    $(document).ready(function () {
        function card(cls, icon, key, value, sub) {
            return '<div class="xgs-card ' + cls + '"><i class="fa ' + icon + '"></i>' +
                '<div class="k">' + key + '</div><div class="v">' + value + '</div>' +
                '<div class="s">' + (sub || '&nbsp;') + '</div></div>';
        }

        /* The installed version, the last check, and whether an update waits for a boot. */
        function short(sha) {
            return sha ? String(sha).substring(0, 12) : '';
        }
        function when(iso) {
            return iso ? String(iso).replace('T', ' ').replace('Z', ' UTC') : '';
        }
        /* A release by its number, a commit by its first twelve characters, each linked to its page. */
        function link(ref) {
            const tag = /^v[0-9]/.test(String(ref || ''));
            return $('<a target="_blank" rel="noopener"/>')
                .attr('href', 'https://github.com/AbdelmonemAwad/os-xgs-npu/' + (tag ? 'releases/tag/' : 'commit/') + ref)
                .text(tag ? ref : short(ref));
        }

        function show_installed(inst) {
            const $box = $('#installed').empty();
            if (!inst || !inst.commit) {
                $box.text("{{ lang._('Not known: this installation was made before installs recorded their version.') }}");
                return;
            }
            if (inst.release) {
                $box.append(link(inst.release)).append(document.createTextNode(' - '));
            }
            $box.append(link(inst.commit))
                .append(document.createTextNode(' - ' + when(inst.date) + ' (' + (inst.source || '') + ')'));
            if (!inst.release) {
                $box.append($('<div class="text-muted"/>').text(
                    "{{ lang._('A commit, not a release: installed before this page followed releases, or by hand.') }}"));
            }
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
                /* Nothing to update to - so the tab shows what the newest release itself carries,
                   which is in what is installed, rather than standing empty. */
                const notes = chk.notes || {};
                if ((notes.lines || []).length) {
                    $list.empty().append($('<tr/>').append($('<td colspan="3" class="text-muted"/>').text(
                        "{{ lang._('Nothing newer to install. What the newest release carries - all of it in what is installed:') }}")));
                    notes.lines.forEach(function (line) {
                        $list.append($('<tr/>')
                            .append($('<td style="white-space: nowrap;"/>').text(when(notes.date)))
                            .append($('<td/>').append(link(notes.release)))
                            .append($('<td/>').text(line)));
                    });
                }
                if (chk.source === 'releases' && chk.compare === 'behind' && chk.target && chk.target.tag) {
                    /* What is installed came after the newest release - from main, before this page
                       followed releases. It is not taken back; the next release is offered. */
                    $res.attr('class', 'text-success')
                        .append(document.createTextNode("{{ lang._('Newer than the newest release,') }} "))
                        .append(link(chk.target.tag))
                        .append(document.createTextNode(' - ' + (chk.behind_by || 0) + ' ' +
                            "{{ lang._('change(s) after it. Nothing is taken back; the next release will be offered.') }}"));
                    return;
                }
                $res.attr('class', 'text-success').text(chk.source === 'releases' ?
                    "{{ lang._('Up to date with the newest release.') }}" :
                    "{{ lang._('Up to date with the newest version whose checks passed.') }}");
                return;
            }
            if ((chk.changes || []).length) {
                $list.empty();
            }
            $res.attr('class', 'text-info')
                .append(document.createTextNode("{{ lang._('Available:') }} "))
                .append(link(chk.target.tag || chk.target.sha))
                .append(document.createTextNode(' - ' + when(chk.target.date) +
                    (chk.target.prerelease || /-alpha/.test(chk.target.tag || '') ? ' - ' + "{{ lang._('alpha') }}" : '') +
                    (chk.releases_since > 1 ? ' - ' + chk.releases_since + " {{ lang._('releases since the installed one') }}" :
                        (chk.ahead_by && !chk.target.tag ? ' - ' + chk.ahead_by + " {{ lang._('change(s)') }}" : ''))));
            /* the installed commit is not on GitHub, so what changed since it cannot be listed */
            if (chk.installed_unknown) {
                $res.append($('<div class="text-muted"/>').text(
                    chk.source === 'releases' ?
                    "{{ lang._('The installed version is not on GitHub - a build from a local or deleted branch - so it cannot be compared; this is the newest release.') }}" :
                    "{{ lang._('The installed version is not on GitHub - a build from a local or deleted branch - so it cannot be compared; this is the newest version whose checks passed.') }}"));
            }
            (chk.changes || []).forEach(function (c) {
                $list.append($('<tr/>')
                    .append($('<td style="white-space: nowrap;"/>').text(when(c.date)))
                    .append($('<td/>').append(link(c.release || c.sha)))
                    .append($('<td/>').text(c.subject)));
            });
            $('#changes-count').text((chk.changes || []).length ? (chk.changes || []).length : '');
            $('#updateAct').prop('disabled', false);
        }

        /* The progress of an update, as update.py reads it from what the update leaves behind:
           its step, how far along it is, and - while the driver builds - how many of its source
           files are compiled. The clock runs between polls so it never sits still. */
        const STEPS = [
            ['download', "{{ lang._('Download') }}", 'fa-cloud-download'],
            ['unpack', "{{ lang._('Unpack') }}", 'fa-archive'],
            ['install', "{{ lang._('Install') }}", 'fa-puzzle-piece'],
            ['sources', "{{ lang._('Kernel sources') }}", 'fa-folder-open'],
            ['build', "{{ lang._('Build the driver') }}", 'fa-cogs'],
            ['finish', "{{ lang._('Finish') }}", 'fa-flag-checkered']
        ];
        const STEP_TEXT = {
            download: "{{ lang._('Downloading the release from GitHub') }}",
            unpack: "{{ lang._('Unpacking it') }}",
            install: "{{ lang._('Installing the plugin, its pages and its boot hooks') }}",
            sources: "{{ lang._('Making the kernel sources ready for the build') }}",
            build: "{{ lang._('Building the driver for the running kernel') }}",
            finish: "{{ lang._('Finishing') }}"
        };
        let ticker = null;
        function clock(from, to) {
            const s = Math.max(0, Math.floor(((to || Date.now() / 1000) - from)));
            return Math.floor(s / 60) + ':' + ('0' + (s % 60)).slice(-2);
        }
        function show_progress(st) {
            const p = st.progress, ap = st.apply || {};
            if (ticker !== null) {
                clearInterval(ticker);
                ticker = null;
            }
            /* After the boot that loaded it, the last update needs no bar: the row above says so. */
            if (!p || (ap.status === 'done' && !st.pending_reboot)) {
                $('#progress-row').hide();
                return;
            }
            $('#progress-row').show();
            const failed = ap.status === 'failed', done = ap.status === 'done';
            const at = STEPS.findIndex(function (s) { return s[0] === p.step; });
            $('#progress-bar')
                .attr('class', 'progress-bar ' + (failed ? 'progress-bar-danger' :
                    (done ? 'progress-bar-success' : 'progress-bar-info progress-bar-striped active')))
                .attr('aria-valuenow', p.percent).css('width', p.percent + '%').text(p.percent + '%');
            const $steps = $('#progress-steps').empty();
            STEPS.forEach(function (s, i) {
                const state = done || i < at ? 'done' : (i === at ? (failed ? 'bad' : 'on') : '');
                const icon = state === 'done' ? 'fa-check' : (state === 'on' ? 'fa-spinner fa-spin' :
                    (state === 'bad' ? 'fa-times' : s[2]));
                $steps.append($('<li/>').addClass(state)
                    .append($('<i class="fa"/>').addClass(icon)).append(document.createTextNode(s[1])));
            });
            const $now = $('#progress-now').empty();
            if (done) {
                $now.attr('class', 'xgs-now text-success').text("{{ lang._('Installed in') }} " +
                    clock(p.started_at, p.finished_at) + '.');
                return;
            }
            if (failed) {
                $now.attr('class', 'xgs-now text-danger').text("{{ lang._('Stopped at:') }} " +
                    (at >= 0 ? STEPS[at][1] : p.step) + ". {{ lang._('The driver that is running stays as it was; the installer log says why.') }}");
                return;
            }
            let text = STEP_TEXT[p.step] || p.step;
            if (p.detail) {
                const n = String(p.detail).split('/');
                text += ' - ' + "{{ lang._('%1 of %2 source files compiled') }}".replace('%1', n[0]).replace('%2', n[1]);
            }
            $now.attr('class', 'xgs-now text-info').text(text);
            const $clock = $('<span class="xgs-clock"/>').appendTo($now);
            function tick() {
                $clock.text(p.started_at ? clock(p.started_at) : '');
            }
            tick();
            ticker = setInterval(tick, 1000);
        }

        function show_apply(st) {
            const ap = st.apply || {};
            $('#reboot-row').toggle(!!st.pending_reboot);
            show_progress(st);
            /* The installer's log is on its own tab, as the firmware's update log is; a running
               or failed update says so here and the log is one click away. */
            if (ap.status === 'running') {
                $('#apply-row').show();
                $('#apply-state').attr('class', 'text-info').text("{{ lang._('Updating. The firewall keeps running meanwhile, and the new driver loads at the next boot.') }}");
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

        /* the summary: what is installed, what is offered, what the last update did */
        function show_cards(st) {
            const inst = st.installed || {}, chk = st.check || {}, ap = st.apply || {};
            let latest = card('', 'fa-cloud', "{{ lang._('Latest') }}", "{{ lang._('Not checked') }}", '');
            if (chk.status === 'ok' && chk.update_available) {
                latest = card('warn', 'fa-cloud-download', "{{ lang._('Latest') }}", "{{ lang._('Update available') }}",
                    (chk.target && chk.target.tag) ? chk.target.tag :
                        short(chk.target && chk.target.sha) + (chk.ahead_by ? ' - ' + chk.ahead_by + " {{ lang._('change(s)') }}" : ''));
            } else if (chk.status === 'ok') {
                latest = card('ok', 'fa-check', "{{ lang._('Latest') }}", "{{ lang._('Up to date') }}", when(chk.checked_at ? new Date(chk.checked_at * 1000).toISOString() : ''));
            } else if (chk.status) {
                latest = card('bad', 'fa-exclamation-triangle', "{{ lang._('Latest') }}", "{{ lang._('The check failed') }}", '');
            }
            const last = ap.status === 'failed' ? card('bad', 'fa-times', "{{ lang._('Last update') }}", "{{ lang._('Failed') }}", '') :
                ap.status === 'running' ? card('warn', 'fa-spinner', "{{ lang._('Last update') }}", "{{ lang._('Running') }}", '') :
                ap.status === 'done' ? card(st.pending_reboot ? 'warn' : 'ok', 'fa-check', "{{ lang._('Last update') }}",
                    st.pending_reboot ? "{{ lang._('Waits for a reboot') }}" : "{{ lang._('Installed') }}", short(ap.sha)) :
                card('', 'fa-history', "{{ lang._('Last update') }}", "{{ lang._('None from this page') }}", '');
            $('#xgs_cards').html(
                card(inst.commit ? 'ok' : '', 'fa-cube', "{{ lang._('Installed') }}",
                    inst.release ? inst.release : (inst.commit ? short(inst.commit) : "{{ lang._('Not known') }}"),
                    when(inst.date)) + latest + last);
        }

        /* The channel: releases, the one for an appliance in service, or main - every change merged
           whose checks passed, for an owner who tries what is merged before it is released. */
        function show_channel(ch) {
            const main = ch === 'main';
            $('#channel').val(main ? 'main' : 'releases');
            $('#channel-note').attr('class', main ? 'text-warning' : 'text-muted').text(main ?
                "{{ lang._('This appliance follows main: each change merged, before it is part of a release. For a test appliance, not one in service.') }}" :
                "{{ lang._('This appliance follows releases.') }}");
        }

        $('#channel').change(function () {
            const want = $(this).val();
            const go = function () {
                ajaxCall('/api/xgs/updates/channel', {channel: want}, function () {
                    refresh();
                });
            };
            if (want !== 'main') {
                go();
                return;
            }
            BootstrapDialog.confirm({
                title: "{{ lang._('Follow main') }}",
                message: "{{ lang._('Updates will offer each change as soon as it is merged and its checks pass, before it is part of a release. A change that is merged has passed its checks, but it may not have run on an appliance yet. Use it on an appliance you can reach, not on one in service.') }}",
                type: BootstrapDialog.TYPE_WARNING,
                btnCancelLabel: "{{ lang._('Cancel') }}",
                btnOKLabel: "{{ lang._('Follow main') }}",
                callback: function (ok) {
                    if (ok) {
                        go();
                    } else {
                        $('#channel').val('releases');
                    }
                }
            });
        });

        let polling = null;
        function refresh() {
            ajaxGet('/api/xgs/updates/status', {}, function (st) {
                if (!st || st.status !== 'ok') {
                    return;
                }
                show_installed(st.installed);
                show_channel(st.channel);
                show_cards(st);
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
                    polling = setInterval(refresh, 2000);
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
                                polling = setInterval(refresh, 2000);
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
        <div class="xgs-cards" id="xgs_cards"></div>
        <table class="table table-striped table-condensed">
            <tbody>
                <tr>
                    <td style="width: 150px;">{{ lang._('Source') }}</td>
                    <td>{{ lang._('GitHub: the newest release. A release is made when a fix is ready, or when improvements have been gathered - not for every change. System: Firmware does not list this plugin, and nothing here reboots on its own.') }}</td>
                </tr>
                <tr>
                    <td>{{ lang._('Channel') }}</td>
                    <td>
                        <select id="channel" class="form-control input-sm" style="display: inline-block; width: auto;">
                            <option value="releases">{{ lang._('Releases') }}</option>
                            <option value="main">{{ lang._('Test: every change merged, its checks passed') }}</option>
                        </select>
                        <div class="text-muted" id="channel-note" style="margin-top: 5px;"></div>
                    </td>
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
                <tr id="progress-row" style="display: none;">
                    <td>{{ lang._('Progress') }}</td>
                    <td>
                        <div class="xgs-progress">
                            <div class="progress">
                                <div id="progress-bar" class="progress-bar progress-bar-info progress-bar-striped active"
                                     role="progressbar" aria-valuemin="0" aria-valuemax="100" aria-valuenow="0"
                                     style="width: 0%;">0%</div>
                            </div>
                            <ol class="xgs-steps" id="progress-steps"></ol>
                            <div class="xgs-now" id="progress-now"></div>
                        </div>
                    </td>
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
                    <th style="width: 130px;">{{ lang._('Release') }}</th>
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

{{ partial("OPNsense/XGS/credit") }}
