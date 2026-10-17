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
    /* As on the Ports page: the grid's columns fill the width exactly and its cells start one pixel
       in, so with no vertical scrollbar a one-pixel sideways bar appears. Wide screens never need to
       scroll it sideways; narrow ones still can. */
    @media (min-width: 768px) {
        #grid-reports .tabulator-tableholder { overflow-x: hidden; }
    }
</style>

<script>
    $(document).ready(function () {
        /* The text the API returns is escaped once by the API, and Volt escapes an apostrophe in a
           translated string; both are read back to text before .text() is given them. */
        function plain(value) {
            return $('<textarea/>').html(value === null || value === undefined ? '' : String(value)).text();
        }

        const kinds = {
            'driver': "{{ lang._('driver') }}",
            'log-error': "{{ lang._('logged error') }}",
            'php': "{{ lang._('page error') }}",
            'update-failed': "{{ lang._('update') }}",
            'fan-incident': "{{ lang._('fan incident') }}",
            'driver-absent': "{{ lang._('driver missing') }}",
            'driver-not-ready': "{{ lang._('driver not ready') }}",
            'module-mismatch': "{{ lang._('module and kernel') }}",
            'kernel-panic': "{{ lang._('kernel panic') }}",
            'unexpected-reset': "{{ lang._('restart without shutdown') }}",
            'manual': "{{ lang._('written by hand') }}"
        };

        $('#grid-reports').UIBootgrid({
            search: '/api/xgs/reports/search',
            options: {
                selection: false,
                formatters: {
                    status: function (column, row) {
                        if (row.status === 'sent') {
                            const $a = $('<a target="_blank" rel="noopener"/>').attr('href', plain(row.url))
                                .text('#' + row.issue);
                            return '<i class="fa fa-check text-success"></i> ' + $a[0].outerHTML +
                                (row.repeat ? ' <small class="text-muted">' + "{{ lang._('seen again') }}" + '</small>' : '');
                        }
                        if (row.status === 'failed') {
                            return '<span class="text-warning" title="' + $('<div/>').text(plain(row.error)).html() +
                                '" data-toggle="tooltip"><i class="fa fa-exclamation-triangle"></i> ' +
                                "{{ lang._('not sent yet') }}" + '</span>';
                        }
                        if (row.status === 'dismissed') {
                            return '<span class="text-muted">' + "{{ lang._('dismissed') }}" + '</span>';
                        }
                        return '<span class="text-info"><i class="fa fa-clock-o"></i> ' + "{{ lang._('waiting') }}" + '</span>';
                    },
                    kind: function (column, row) {
                        return $('<span/>').text(kinds[row.kind] || plain(row.kind)).html();
                    },
                    part: function (column, row) {
                        return '<small>' + row.part + '</small>';
                    },
                    commands: function (column, row) {
                        const button = function (cls, icon, title) {
                            return '<button type="button" class="btn btn-xs btn-default ' + cls + '" data-row-id="' +
                                row.id + '" title="' + title + '" data-toggle="tooltip"><i class="fa fa-fw ' + icon + '"></i></button> ';
                        };
                        return button('xgs-show', 'fa-eye', "{{ lang._('What is sent') }}") +
                            (row.status === 'sent' && !row.repeat ? '' : button('xgs-send', 'fa-paper-plane', "{{ lang._('Send now') }}")) +
                            (row.status === 'dismissed' ? '' : button('xgs-dismiss', 'fa-times', "{{ lang._('Dismiss') }}"));
                    }
                }
            }
        });

        function reload() {
            $('#grid-reports').bootgrid('reload');
            show_state();
        }

        function busy($button, on) {
            $button.prop('disabled', on).find('i').toggleClass('fa-spinner fa-pulse', on);
        }

        function card(cls, icon, key, value, sub) {
            return '<div class="xgs-card ' + cls + '"><i class="fa ' + icon + '"></i>' +
                '<div class="k">' + key + '</div><div class="v">' + value + '</div>' +
                '<div class="s">' + (sub || '&nbsp;') + '</div></div>';
        }

        /* the relay the reports go through, in the form's own row, and the summary as cards */
        function show_state() {
            ajaxGet('/api/xgs/reports/state', {}, function (data) {
                data = data || {};
                $('#xgs_relay').text(data.relay ? plain(data.relay) : '');
                const modes = {
                    'automatic': ["{{ lang._('Automatic') }}", 'ok'],
                    'manual': ["{{ lang._('Manual') }}", 'warn'],
                    'off': ["{{ lang._('Off') }}", '']
                };
                const mode = modes[data.mode] || modes.automatic;
                const counts = data.counts || {};
                $('#xgs_cards').html(
                    card(mode[1], 'fa-paper-plane', "{{ lang._('Sending') }}", mode[0], "{{ lang._('to the project on GitHub') }}") +
                    card(counts.waiting ? 'warn' : 'ok', 'fa-clock-o', "{{ lang._('Waiting') }}", String(counts.waiting || 0),
                        "{{ lang._('reports not sent yet') }}") +
                    card('', 'fa-github', "{{ lang._('Sent') }}", String(counts.sent || 0), "{{ lang._('issues opened or noted') }}") +
                    card(counts.last ? 'warn' : 'ok', 'fa-bug', "{{ lang._('Last problem') }}",
                        counts.last ? plain(counts.last).slice(0, 10) : "{{ lang._('None') }}", counts.last ? plain(counts.last).slice(11) : ''));
            });
        }

        mapDataToFormUI({'frm_reporting': '/api/xgs/reports/get'}).done(function () {
            $('.selectpicker').selectpicker('refresh');
            show_state();
        });

        /* Applying a choice is also the owner's answer to the banner, so the banner goes with it. */
        $('#saveAct').click(function () {
            const $button = $(this);
            const $progress = $('#frm_reporting_progress');
            $button.prop('disabled', true);
            $progress.addClass('fa fa-spinner fa-pulse');
            saveFormToEndpoint('/api/xgs/reports/set', 'frm_reporting', function () {
                ajaxCall('/api/xgs/reports/acknowledge', {}, function () {
                    $progress.removeClass('fa fa-spinner fa-pulse');
                    $button.prop('disabled', false);
                    if (typeof updateSystemStatus === 'function') {
                        updateSystemStatus();
                    }
                });
            }, false, function () {
                $progress.removeClass('fa fa-spinner fa-pulse');
                $button.prop('disabled', false);
            });
        });

        $(document).on('click', '.xgs-show', function () {
            const id = $(this).data('row-id');
            ajaxGet('/api/xgs/reports/show/' + id, {}, function (data) {
                const doc = data && data.payload ? data.payload : null;
                const $box = $('<div/>');
                if (doc === null) {
                    $box.text("{{ lang._('The report could not be read.') }}");
                } else {
                    $box.append($('<p class="text-muted"/>').text(
                        "{{ lang._('Sent to the relay as it stands here, and filed in the repository it names. The contact address goes to the relay only.') }}"));
                    $box.append($('<table class="table table-condensed"/>').append(
                        $('<tbody/>')
                            .append($('<tr/>').append($('<td style="width: 25%;"/>').text("{{ lang._('Repository') }}")).append($('<td/>').text(plain(doc.repo))))
                            .append($('<tr/>').append($('<td/>').text("{{ lang._('Title') }}")).append($('<td/>').text(plain(doc.title))))
                            .append($('<tr/>').append($('<td/>').text("{{ lang._('Appliance') }}")).append($('<td/>').text(plain(doc.appliance))))
                            .append($('<tr/>').append($('<td/>').text("{{ lang._('Contact address') }}")).append($('<td/>').text(plain(doc.contact || '-'))))
                            .append($('<tr/>').append($('<td/>').text("{{ lang._('Relay') }}")).append($('<td/>').text(plain(data.relay))))));
                    $box.append($('<pre style="max-height: 420px; overflow: auto; white-space: pre-wrap;"/>').text(plain(doc.body)));
                }
                BootstrapDialog.show({
                    title: "{{ lang._('What is sent') }}",
                    message: $box,
                    size: BootstrapDialog.SIZE_WIDE,
                    buttons: [{label: "{{ lang._('Close') }}", action: function (dialog) { dialog.close(); }}]
                });
            });
        });

        $(document).on('click', '.xgs-send', function () {
            const $button = $(this);
            busy($button, true);
            ajaxCall('/api/xgs/reports/send/' + $button.data('row-id'), {}, function (data) {
                busy($button, false);
                if (!data || data.status !== 'ok') {
                    BootstrapDialog.show({
                        type: BootstrapDialog.TYPE_WARNING,
                        title: "{{ lang._('Problem reports') }}",
                        message: $('<div/>').text("{{ lang._('The report was not sent:') }}" + ' ' + plain((data && data.message) || '')),
                        buttons: [{label: "{{ lang._('Close') }}", action: function (dialog) { dialog.close(); }}]
                    });
                }
                reload();
            });
        });

        $(document).on('click', '.xgs-dismiss', function () {
            const id = $(this).data('row-id');
            ajaxCall('/api/xgs/reports/dismiss/' + id, {}, function () {
                reload();
            });
        });

        $('#bundleAct').click(function () {
            const $button = $(this).prop('disabled', true);
            $button.find('i').removeClass('fa-download').addClass('fa-spinner fa-pulse');
            $('#bundle_note').text('');
            ajaxCall('/api/xgs/reports/bundle', {}, function (data) {
                $button.prop('disabled', false);
                $button.find('i').removeClass('fa-spinner fa-pulse').addClass('fa-download');
                if (!data || data.status !== 'ok' || !data.data) {
                    $('#bundle_note').text(plain((data && data.message) || "{{ lang._('The support bundle could not be made.') }}"));
                    return;
                }
                const raw = atob(data.data);
                const bytes = new Uint8Array(raw.length);
                for (let i = 0; i < raw.length; i++) {
                    bytes[i] = raw.charCodeAt(i);
                }
                const link = document.createElement('a');
                link.href = URL.createObjectURL(new Blob([bytes], {type: 'application/gzip'}));
                link.download = data.file;
                document.body.appendChild(link);
                link.click();
                setTimeout(function () { URL.revokeObjectURL(link.href); link.remove(); }, 1000);
                $('#bundle_note').text(data.file + ' - ' + Math.round(data.size / 1024) + ' KB');
            });
        });

        $('#collectAct').click(function () {
            const $button = $(this);
            busy($button, true);
            ajaxCall('/api/xgs/reports/collect', {}, function () {
                busy($button, false);
                reload();
            });
        });

        /* A report written by the owner: the same queue, the same cleaning, the same View and Send. */
        $('#manualAct').click(function () {
            const parts = ['os-xgs-npu', 'os-fanctl', 'os-frontpanel', 'os-linkhealth', 'os-netreport'];
            const $part = $('<select class="form-control"/>');
            parts.forEach(function (name) {
                $part.append($('<option/>').val(name).text(name));
            });
            const $title = $('<input type="text" class="form-control" maxlength="150"/>');
            const $description = $('<textarea class="form-control" rows="8" maxlength="4000"/>');
            const $form = $('<div/>')
                .append($('<label/>').text("{{ lang._('Which part') }}")).append($part)
                .append($('<label style="margin-top: 10px;"/>').text("{{ lang._('Title') }}")).append($title)
                .append($('<label style="margin-top: 10px;"/>').text("{{ lang._('What happened, and what you expected') }}")).append($description)
                .append($('<p class="text-muted" style="margin-top: 10px;"/>').text(
                    "{{ lang._('The appliance block - model, board, versions - is added to it, and addresses and serials are taken out of all of it, as with every report. It waits in the list below until it is sent.') }}"));
            BootstrapDialog.show({
                title: "{{ lang._('Report a problem') }}",
                message: $form,
                buttons: [{
                    label: "{{ lang._('Cancel') }}",
                    action: function (dialog) { dialog.close(); }
                }, {
                    label: "{{ lang._('Add to the list') }}",
                    cssClass: 'btn-primary',
                    action: function (dialog) {
                        ajaxCall('/api/xgs/reports/manual', {
                            'title': $title.val(), 'description': $description.val(), 'part': $part.val()
                        }, function (data) {
                            if (data && data.status === 'ok') {
                                dialog.close();
                                reload();
                            } else {
                                $form.find('.text-danger').remove();
                                $form.append($('<p class="text-danger"/>').text(plain((data && data.message) || '')));
                            }
                        });
                    }
                }]
            });
        });
    });
</script>

<div class="content-box">
    <div class="xgs-cards" id="xgs_cards"></div>
    {{ partial("layout_partials/base_form", ['fields': reportingForm, 'id': 'frm_reporting', 'apply_btn_id': 'saveAct']) }}
</div>

<div class="tab-content content-box" style="margin-top: 15px;">
    <table id="grid-reports" class="table table-condensed table-hover table-striped table-responsive">
        <thead>
            <tr>
                <th data-column-id="status" data-formatter="status" data-type="string" data-width="9em">{{ lang._('Status') }}</th>
                <th data-column-id="last_seen" data-type="string" data-width="12em" data-order="desc">{{ lang._('Last seen') }}</th>
                <th data-column-id="first_seen" data-type="string" data-visible="false">{{ lang._('First seen') }}</th>
                <th data-column-id="part" data-formatter="part" data-type="string" data-width="9em">{{ lang._('Part') }}</th>
                <th data-column-id="kind" data-formatter="kind" data-type="string" data-width="11em">{{ lang._('Kind') }}</th>
                <th data-column-id="title" data-type="string">{{ lang._('Problem') }}</th>
                <th data-column-id="count" data-type="numeric" data-width="5em">{{ lang._('Times') }}</th>
                <th data-column-id="id" data-identifier="true" data-type="string" data-visible="false">{{ lang._('Fingerprint') }}</th>
                <th data-column-id="commands" data-formatter="commands" data-sortable="false" data-width="8em">{{ lang._('Commands') }}</th>
            </tr>
        </thead>
        <tbody></tbody>
        <tfoot></tfoot>
    </table>
    <div style="padding: 10px 15px;">
        <button class="btn btn-default" id="manualAct" type="button"><i class="fa fa-fw fa-pencil"></i> {{ lang._('Report a problem') }}</button>
        <button class="btn btn-default" id="collectAct" type="button"><i class="fa fa-fw fa-search"></i> {{ lang._('Look now') }}</button>
    </div>
</div>

<div class="content-box" style="margin-top: 15px;">
    <table class="table table-striped opnsense_standard_table_form">
        <colgroup><col class="col-md-3"/><col class="col-md-9"/></colgroup>
        <tbody>
            <tr>
                <td><i class="fa fa-info-circle fa-fw text-muted"></i> <b>{{ lang._('Support bundle') }}</b></td>
                <td>
                    <button class="btn btn-default" id="bundleAct" type="button">
                        <i class="fa fa-fw fa-download"></i> {{ lang._('Download a support bundle') }}
                    </button>
                    <span id="bundle_note" class="text-muted" style="margin-inline-start: 8px;"></span>
                    <div class="help-block">
                        {{ lang._('One file with what somebody helping with this appliance needs: the coprocessor\'s state as these pages read it, the driver\'s counters, the boot messages, the plugin\'s lines from the system log and the network interfaces. Addresses, MACs, serial numbers, the host and domain names, e-mail addresses and keys are replaced before anything is written, and no configuration, password or certificate is in it. It is made now, kept nowhere on the appliance and sent nowhere: you decide whom to give it to.') }}
                    </div>
                </td>
            </tr>
        </tbody>
    </table>
</div>
