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
    /* A box of its own, so in Arabic it does not run into a Latin name before it (os-fanctl0:08). */
    .xgs-progress .xgs-clock { display: inline-block; unicode-bidi: isolate; font-variant-numeric: tabular-nums;
                               opacity: 0.7; margin-inline-start: 8px; }
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
        /* The milliseconds go: toISOString() of a check time wrote 18:39:01.000 on the card. */
        function when(iso) {
            return iso ? String(iso).replace('T', ' ').replace(/\.\d+Z$/, 'Z').replace('Z', ' UTC') : '';
        }
        /* A release by its number, a commit by its first twelve characters, each linked to its page. */
        function link(ref) {
            const tag = /^v[0-9]/.test(String(ref || ''));
            return $('<a target="_blank" rel="noopener"/>')
                .attr('href', 'https://github.com/AbdelmonemAwad/os-xgs-npu/' + (tag ? 'releases/tag/' : 'commit/') + ref)
                .text(tag ? ref : short(ref));
        }

        /* A change line as the release notes write it - Markdown, its pull request as
           [#486](https://github.com/...) - drawn as text with that link, rather than the brackets
           and the address printed out. Only links into GitHub are made links; all of it is text. */
        function md_text(line) {
            const $span = $('<span/>');
            const text = String(line || '');
            const pattern = /\[([^\]]+)\]\((https:\/\/github\.com\/[^)\s]+)\)/g;
            let at = 0;
            let m;
            while ((m = pattern.exec(text)) !== null) {
                $span.append(document.createTextNode(text.slice(at, m.index)));
                $span.append($('<a target="_blank" rel="noopener"/>').attr('href', m[2]).text(m[1]));
                at = m.index + m[0].length;
            }
            $span.append(document.createTextNode(text.slice(at)));
            return $span;
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
                            .append($('<td/>').append(md_text(line))));
                    });
                }
                if (chk.source === 'releases' && (chk.compare === 'behind' || chk.newer_than_target) &&
                        chk.target && chk.target.tag) {
                    /* What is installed came after the newest release - from main, before this page
                       followed releases, or a build from a branch dated after it (issue 677). It is
                       not taken back; the next release is offered. */
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
            /* a build from a branch outside this history - a test build, or one whose change was
               merged as a single commit - offered the release, and told why */
            if (chk.compare === 'diverged') {
                $res.append($('<div class="text-muted"/>').text(
                    "{{ lang._('The installed version was built from a branch that this history does not hold - a test build, or one whose change was later merged as a single commit - so it is not ahead and not behind; this is the version to install.') }}"));
            }
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
                    .append($('<td/>').append(md_text(c.subject))));
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
            // what the reboot is for: the new driver, or the boot steps alone when the driver is the one running
            $('#reboot-driver').toggle(!st.driver_unchanged);
            $('#reboot-steps').toggle(!!st.driver_unchanged);
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
                $('#apply-state').attr('class', 'text-danger').text(ap.message || "{{ lang._('The update failed.') }}");
            } else if (ap.status === 'done') {
                $('#apply-row').show();
                $('#apply-state').attr('class', 'text-success').text(st.pending_reboot ?
                    (st.driver_unchanged ?
                        "{{ lang._('Installed. The driver is the one running, but its boot steps changed: they take effect at the next boot.') }}" :
                        "{{ lang._('Installed. The coprocessor driver takes effect at the next boot.') }}") :
                    (st.driver_unchanged ?
                        "{{ lang._('Installed. The driver it built is the one running, so nothing waits for a reboot.') }}" :
                        "{{ lang._('Installed, and loaded by the boot since.') }}"));
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
            /* OPNsense's own picker, as its settings pages draw a select: a plain one sized by
               input-sm cut its text off under this theme's line height. */
            $('#channel').selectpicker('val', main ? 'main' : 'releases');
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
                        $('#channel').selectpicker('val', 'releases');
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
                    $('#apply-log').text((lg && lg.log) ||
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

        /*
         * The appliance plugins (scripts/xgs/plugins.py): what each has installed, its latest release
         * as the last check found it, and its last update. Update starts the plugin's own
         * self-update; the list is read again every few seconds while one runs.
         */
        let pluginPoll = null, pluginMisses = 0, pluginSkew = 0;
        /*
         * Seconds since the epoch, as plugins.py keeps them. Not when(): a second function of that
         * name took the place of the first, which formats GitHub's dates, and every date at the top
         * of the page read "Invalid Date" (0.1.0-alpha.3 to alpha.4).
         */
        function whenAt(at) {
            return at ? new Date(at * 1000).toLocaleString() : '';
        }

        /*
         * A plugin's update, step by step, as plugins.py's runner reads it from what the plugin's own
         * self-update leaves while it works - the same bar and steps as the coprocessor's update
         * above. A bar stays ten minutes after its update ends, so what happened can be read.
         */
        const PSTEPS = [
            ['queued', "{{ lang._('Queued') }}", 'fa-hourglass-o'],
            ['check', "{{ lang._('Ask GitHub') }}", 'fa-cloud'],
            ['download', "{{ lang._('Download') }}", 'fa-cloud-download'],
            ['unpack', "{{ lang._('Unpack') }}", 'fa-archive'],
            ['install', "{{ lang._('Install') }}", 'fa-puzzle-piece'],
            ['done', "{{ lang._('Updated') }}", 'fa-flag-checkered']
        ];
        const PSTEP_TEXT = {
            queued: "{{ lang._('Waiting for its turn') }}",
            check: "{{ lang._('Asking GitHub...') }}",
            download: "{{ lang._('Downloading the release from GitHub') }}",
            unpack: "{{ lang._('Unpacking it') }}",
            install: "{{ lang._('Running its installer') }}"
        };
        const SHOWN_AFTER = 600;
        function pluginBar(pct, failed, ok) {
            return $('<div class="progress"/>').append(
                $('<div class="progress-bar" role="progressbar" aria-valuemin="0" aria-valuemax="100"/>')
                    .addClass(failed ? 'progress-bar-danger' :
                        (ok ? 'progress-bar-success' : 'progress-bar-info progress-bar-striped active'))
                    .attr('aria-valuenow', pct).css('width', pct + '%').text(pct + '%'));
        }
        function pluginProgress(job, now) {
            const failed = job.status === 'failed', ok = job.status === 'done' || job.status === 'current';
            const at = PSTEPS.findIndex(function (s) { return s[0] === job.step; });
            const $box = $('<div class="xgs-progress"/>').append(pluginBar(ok ? 100 : (job.percent || 0), failed, ok));
            const $steps = $('<ol class="xgs-steps"/>').appendTo($box);
            PSTEPS.forEach(function (s, i) {
                const state = ok || i < at ? 'done' : (i === at ? (failed ? 'bad' : 'on') : '');
                const icon = state === 'done' ? 'fa-check' : (state === 'on' ? 'fa-spinner fa-spin' :
                    (state === 'bad' ? 'fa-times' : s[2]));
                $steps.append($('<li/>').addClass(state)
                    .append($('<i class="fa"/>').addClass(icon)).append(document.createTextNode(s[1])));
            });
            const took = job.started_at ? clock(job.started_at, job.finished_at || now) : '';
            const $now = $('<div class="xgs-now"/>').appendTo($box);
            if (job.status === 'done') {
                $now.addClass('text-success').text("{{ lang._('Updated to %1 in %2.') }}"
                    .replace('%1', job.tag || '').replace('%2', took));
            } else if (job.status === 'current') {
                $now.addClass('text-success').text("{{ lang._('Already the latest release.') }}");
            } else if (failed) {
                $now.addClass('text-danger').text("{{ lang._('Stopped at:') }} " +
                    (at >= 0 ? PSTEPS[at][1] : '') + (job.message ? ' - ' + job.message : ''));
            } else {
                let text = PSTEP_TEXT[job.step] || '';
                if (job.step === 'download' && job.detail) {
                    text += ' - ' + "{{ lang._('%1 KB so far') }}".replace('%1', Math.round(job.detail / 1024));
                }
                $now.addClass('text-info').text(text);
                if (job.started_at) {
                    $('<span class="xgs-clock"/>').attr('data-from', job.started_at).text(took).appendTo($now);
                }
                if (job.step === 'install' && job.detail) {
                    $('<div class="text-muted"/>').append($('<small/>').append($('<code/>').text(job.detail))).appendTo($box);
                }
            }
            return $box;
        }

        /* Several at once: which one is running, and the whole of it on one bar. */
        function pluginsQueue(d) {
            const q = d.queue || {}, names = q.names || [], now = d.now;
            const $q = $('#plugins-queue');
            const ended = !q.running && q.finished_at && now - q.finished_at < SHOWN_AFTER;
            if (names.length < 2 || !(q.running || ended)) {
                $q.hide();
                return;
            }
            const jobs = {};
            (d.plugins || []).forEach(function (p) { jobs[p.name] = p.job || {}; });
            let sum = 0, failed = false, current = '';
            names.forEach(function (n) {
                const j = jobs[n] || {};
                sum += (j.status === 'done' || j.status === 'current') ? 100 : (j.percent || 0);
                failed = failed || j.status === 'failed';
                if (j.status === 'running') {
                    current = n;
                }
            });
            const pct = Math.round(sum / names.length);
            const $now = $('<div class="xgs-now"/>');
            if (q.running) {
                $now.addClass('text-info').text("{{ lang._('Updating %1 of %2: %3') }}"
                    .replace('%1', Math.min(names.length, q.finished + 1)).replace('%2', names.length)
                    .replace('%3', current || names[Math.min(names.length - 1, q.finished)]));
                $('<span class="xgs-clock"/>').attr('data-from', q.started_at).text(clock(q.started_at, now)).appendTo($now);
            } else {
                $now.addClass(failed ? 'text-danger' : 'text-success').text("{{ lang._('Updated %1 of %2 in %3.') }}"
                    .replace('%1', names.filter(function (n) { return (jobs[n] || {}).status !== 'failed'; }).length)
                    .replace('%2', names.length).replace('%3', clock(q.started_at, q.finished_at)));
            }
            $q.empty().append($('<div class="xgs-progress"/>').append(pluginBar(pct, failed && !q.running, !q.running && !failed), $now)).show();
        }

        function pluginsShow(d) {
            const rows = (d && d.plugins) || [], now = d.now || Date.now() / 1000;
            const queueRunning = !!(d.queue && d.queue.running);
            const $b = $('#plugin-rows').empty();
            let offered = 0, busy = queueRunning;
            rows.forEach(function (p) {
                const job = p.job || {};
                busy = busy || p.running;
                if (p.take) {
                    offered++;
                }
                let latest = p.latest ? p.latest + (p.latest_sha ? ' (' + p.latest_sha + ')' : '') : '';
                let current = false;
                if (p.check_status === 'none') {
                    latest = "{{ lang._('no release published') }}";
                } else if (p.check_status === 'failed') {
                    latest = p.check_message;
                } else if (p.latest && !p.take && !p.not_installed) {
                    latest += ' - ' + "{{ lang._('installed') }}";
                    current = true;
                }
                /* A plugin of the family that is not here: Install, from its latest release (2026-10-25). */
                const $btn = p.not_installed ?
                    $('<button type="button" class="btn btn-xs btn-default xgs-plugin-install"/>')
                        .attr('data-name', p.name).prop('disabled', !p.installable || p.running || queueRunning)
                        .text("{{ lang._('Install') }}") :
                    $('<button type="button" class="btn btn-xs btn-primary xgs-plugin-update"/>')
                        .attr('data-name', p.name).prop('disabled', !p.take || p.running || queueRunning)
                        .text(p.running ? "{{ lang._('Updating...') }}" : "{{ lang._('Update') }}");
                const $latest = $('<td/>');
                if (current) {
                    $latest.addClass('text-success').append($('<i class="fa fa-check fa-fw"/>'), document.createTextNode(latest));
                } else {
                    $latest.text(latest || '-').toggleClass('text-warning', !!p.take);
                }
                $('<tr/>').append(
                    $('<td/>').append($('<b/>').text(p.name), '<br/>', $('<small class="text-muted"/>').text(p.what)),
                    p.not_installed ?
                        $('<td class="text-muted"/>').text("{{ lang._('not installed') }}") :
                        $('<td/>').append(p.installed_tag ? $('<b/>').text(p.installed_tag + ' ') : '',
                            $('<code/>').text(p.installed || '-'), '<br/>',
                            $('<small class="text-muted"/>').text(p.installed_date || '')),
                    $latest,
                    $('<td/>').append($('<span/>').text(p.last_message || '')
                            .toggleClass('text-success', p.last_status === 'updated')
                            .toggleClass('text-danger', p.last_status === 'failed'), '<br/>',
                        $('<small class="text-muted"/>').text(whenAt(p.last_at))),
                    $('<td/>').append($btn)).appendTo($b);
                const recent = job.finished_at && now - job.finished_at < SHOWN_AFTER;
                if (job.status === 'queued' || job.status === 'running' || recent) {
                    $('<tr class="xgs-plugin-progress"/>').append(
                        $('<td colspan="5" style="border-top: 0;"/>').append(pluginProgress(job, now))).appendTo($b);
                }
            });
            if (!rows.length) {
                $b.append($('<tr/>').append($('<td colspan="5" class="text-muted"/>').text(
                    "{{ lang._('None of the appliance plugins is installed.') }}")));
            }
            pluginsQueue(d);
            $('#plugins-count').text(offered ? String(offered) : '');
            $('#pluginsUpdateAllAct').prop('disabled', !offered || queueRunning)
                .find('.xgs-count').text(offered ? ' (' + offered + ')' : '');
            $('#plugins-checked').text(d && d.checked_at ? "{{ lang._('Last checked') }}" + ' ' + whenAt(d.checked_at) : '');
            return busy;
        }

        /*
         * Read the list again every second while an update runs. A plugin's installer restarts
         * configd and the web GUI, so a read can fail for a few seconds: the page says why, keeps
         * what it showed, and goes on asking
         * instead of stopping where it was (until 2026-10-23 it stopped, and the page had to be
         * reloaded to see that the update was done).
         */
        function pluginsLoad() {
            $.ajax({url: '/api/xgs/updates/plugins', dataType: 'json', timeout: 8000}).done(function (d) {
                /* An installer restarts configd as well, and meanwhile the page's API answers with
                   an error and no list: that is a miss too, not a list of nothing. */
                if (!d || !Array.isArray(d.plugins)) {
                    pluginMisses++;
                    if (pluginPoll !== null) {
                        $('#plugins-away').show();
                    }
                    return;
                }
                pluginMisses = 0;
                pluginSkew = d.now - Date.now() / 1000;
                $('#plugins-away').hide();
                const busy = pluginsShow(d);
                if (busy && pluginPoll === null) {
                    pluginPoll = setInterval(pluginsLoad, 1000);
                } else if (!busy && pluginPoll !== null) {
                    clearInterval(pluginPoll);
                    pluginPoll = null;
                }
            }).fail(function () {
                pluginMisses++;
                if (pluginPoll !== null) {
                    $('#plugins-away').show();
                }
            });
        }
        /* The clocks of what runs, every second between two reads - on the appliance's time - so
           they do not stand still while a read waits for the web GUI to come back. */
        setInterval(function () {
            $('#plugins .xgs-clock[data-from]').each(function () {
                $(this).text(clock(Number($(this).attr('data-from')), Date.now() / 1000 + pluginSkew));
            });
        }, 1000);
        function pluginsFollow() {
            setTimeout(pluginsLoad, 500);
            if (pluginPoll === null) {
                pluginPoll = setInterval(pluginsLoad, 1000);
            }
        }
        pluginsLoad();

        $('#pluginsCheckAct').click(function () {
            const $b = $(this).prop('disabled', true);
            $('#plugins-checked').text("{{ lang._('Asking GitHub...') }}");
            ajaxCall('/api/xgs/updates/pluginsCheck', {}, function () {
                $b.prop('disabled', false);
                pluginsLoad();
            });
        });

        $(document).on('click', '.xgs-plugin-update', function () {
            const name = String($(this).data('name'));
            BootstrapDialog.confirm({
                title: "{{ lang._('Update') }}" + ' ' + name,
                message: "{{ lang._('This installs the latest release of this plugin with the installer it carries. The web GUI restarts while it runs.') }}",
                type: BootstrapDialog.TYPE_INFO,
                btnCancelLabel: "{{ lang._('Cancel') }}",
                btnOKLabel: "{{ lang._('Update') }}",
                callback: function (ok) {
                    if (ok) {
                        ajaxCall('/api/xgs/updates/pluginsUpdate', {name: name}, pluginsFollow);
                    }
                }
            });
        });

        $(document).on('click', '.xgs-plugin-install', function () {
            const name = String($(this).data('name'));
            BootstrapDialog.confirm({
                title: "{{ lang._('Install') }}" + ' ' + name,
                message: "{{ lang._('This installs the latest release of this plugin with the installer it carries. The web GUI restarts while it runs.') }}",
                type: BootstrapDialog.TYPE_INFO,
                btnCancelLabel: "{{ lang._('Cancel') }}",
                btnOKLabel: "{{ lang._('Install') }}",
                callback: function (ok) {
                    if (ok) {
                        ajaxCall('/api/xgs/updates/pluginsInstall', {name: name}, pluginsFollow);
                    }
                }
            });
        });

        $('#pluginsUpdateAllAct').click(function () {
            BootstrapDialog.confirm({
                title: "{{ lang._('Update all') }}",
                message: "{{ lang._('This installs the latest release of every plugin the last check offered, one after the other, each with the installer it carries. The web GUI restarts while they run.') }}",
                type: BootstrapDialog.TYPE_INFO,
                btnCancelLabel: "{{ lang._('Cancel') }}",
                btnOKLabel: "{{ lang._('Update all') }}",
                callback: function (ok) {
                    if (ok) {
                        ajaxCall('/api/xgs/updates/pluginsUpdateAll', {}, pluginsFollow);
                    }
                }
            });
        });
    });
</script>

{# Laid out as System: Firmware is: tabs, and on the first a striped table of what is installed
   and what is offered, with the buttons in its last row. #}
<ul class="nav nav-tabs" data-tabs="tabs" id="maintabs">
    <li class="active"><a data-toggle="tab" href="#status">{{ lang._('Status') }}</a></li>
    <li><a data-toggle="tab" href="#changelog">{{ lang._('What changed') }} <span id="changes-count" class="badge"></span></a></li>
    <li><a data-toggle="tab" href="#installer">{{ lang._('Installer log') }}</a></li>
    <li><a data-toggle="tab" href="#plugins">{{ lang._('Plugins') }} <span id="plugins-count" class="badge"></span></a></li>
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
                        <select id="channel" class="selectpicker" data-width="auto">
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
                        <span class="text-warning" id="reboot-driver">{{ lang._('An update is installed and waits for a reboot to load the new driver.') }}</span>
                        <span class="text-warning" id="reboot-steps" style="display: none;">{{ lang._('An update is installed and waits for a reboot to run its new boot steps.') }}</span>
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

    <div id="plugins" class="tab-pane table-responsive">
        <div id="plugins-queue" style="display: none; padding: 10px 8px 0;"></div>
        <div id="plugins-away" class="alert alert-info" style="display: none; margin: 10px 8px 0;">
            <i class="fa fa-spinner fa-spin fa-fw"></i> {{ lang._('The web GUI is restarting; this page carries on when it is back.') }}
        </div>
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th>{{ lang._('Plugin') }}</th>
                    <th>{{ lang._('Installed') }}</th>
                    <th>{{ lang._('Latest release') }}</th>
                    <th>{{ lang._('Last update') }}</th>
                    <th style="width: 110px;"></th>
                </tr>
            </thead>
            <tbody id="plugin-rows"></tbody>
            <tfoot>
                <tr>
                    <td colspan="5">
                        <button class="btn btn-primary" id="pluginsCheckAct" type="button">
                            <i class="fa fa-fw fa-refresh"></i> {{ lang._('Check for updates') }}
                        </button>
                        <button class="btn btn-default" id="pluginsUpdateAllAct" type="button" disabled="disabled">
                            <i class="fa fa-fw fa-cloud-download"></i> {{ lang._('Update all') }}<span class="xgs-count"></span>
                        </button>
                        <span class="text-muted" id="plugins-checked" style="margin-inline-start: 10px;"></span>
                        <div class="text-muted" style="margin-top: 8px;"><small>{{ lang._('Each plugin follows the latest release of its own repository on GitHub and installs it with the installer that release carries. Nothing is installed on its own: a plugin is updated when Update is pressed here. Its installer restarts the web GUI; this page follows the update through it.') }}</small></div>
                    </td>
                </tr>
            </tfoot>
        </table>
    </div>
</div>

{{ partial("OPNsense/XGS/credit") }}
