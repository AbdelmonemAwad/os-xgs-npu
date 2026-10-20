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
    /* The grid's columns fill the width exactly and its cells start one pixel in, so with no
       vertical scrollbar to take the difference a one-pixel horizontal bar appears under the
       last row. Wide screens never need to scroll this table sideways; narrow ones still can. */
    @media (min-width: 768px) {
        #grid-ports .tabulator-tableholder { overflow-x: hidden; }
    }
</style>

<script>
    $(document).ready(function () {
        /* OPNsense's own grid, the one Interfaces: Overview uses, so the page searches, sorts,
           chooses columns and exports like every other list in the GUI. The API escapes what it
           returns; nothing here escapes it again. */
        $('#grid-ports').UIBootgrid({
            search: '/api/xgs/ports/search',
            options: {
                selection: false,
                formatters: {
                    status: function (column, row) {
                        const up = row.status === 'up';
                        return '<i class="fa fa-plug ' + (up ? 'text-success' : 'text-danger') + '" title="' +
                            (up ? "{{ lang._('up') }}" : "{{ lang._('down') }}") + '" data-toggle="tooltip"></i>';
                    },
                    port: function (column, row) {
                        return '<b>' + row.port + '</b>' + (row.bay ? ' <small class="text-muted">' +
                            "{{ lang._('bay') }}" + ' ' + row.bay + '</small>' : '');
                    },
                    assigned: function (column, row) {
                        return row.assigned ? row.assigned + ' <small class="text-muted">(' + row.key + ')</small>' :
                            '<span class="text-muted">' + "{{ lang._('not assigned') }}" + '</span>';
                    },
                    speed: function (column, row) {
                        return row.status === 'up' ? (row.speed || "{{ lang._('up') }}") : '';
                    }
                }
            }
        });

        function card(cls, icon, key, value, sub) {
            return '<div class="xgs-card ' + cls + '"><i class="fa ' + icon + '"></i>' +
                '<div class="k">' + key + '</div><div class="v">' + value + '</div>' +
                '<div class="s">' + (sub || '&nbsp;') + '</div></div>';
        }

        ajaxGet('/api/xgs/ports/get', {}, function (data) {
            const slots = (data && data.module && data.module.slots) || {};
            /* the summary: link, assignment, the expansion module, the switch chip */
            const plist = (data && data.ports) || [];
            const up = plist.filter(function (p) { return String(p.status || '').indexOf('active') === 0; }).length;
            const assigned = plist.filter(function (p) { return p.assigned && (p.assigned.key || p.assigned.descr); }).length;
            const fitted = Object.keys(slots);
            const swText = (data && data.switch) || '';
            $('#xgs_cards').html(
                card(up > 0 ? 'ok' : 'warn', 'fa-plug', "{{ lang._('Front ports') }}", up + ' / ' + plist.length,
                    "{{ lang._('with link') }}") +
                card(assigned > 0 ? 'ok' : 'warn', 'fa-tags', "{{ lang._('Assigned') }}", assigned + ' / ' + plist.length,
                    "{{ lang._('in Interfaces: Assignments') }}") +
                card(fitted.length ? 'ok' : '', 'fa-th-large', "{{ lang._('Expansion module') }}",
                    fitted.length ? (slots[fitted[0]]['assembly.partno'] || '?') : "{{ lang._('none fitted') }}",
                    fitted.length ? "{{ lang._('bay') }}" + ' ' + fitted.join(', ') : '') +
                card(swText.indexOf('10,240') >= 0 ? 'ok' : '', 'fa-sitemap', "{{ lang._('Switch chip') }}",
                    swText.indexOf('10,240') >= 0 ? "{{ lang._('Jumbo frames') }}" : "{{ lang._('2,048 bytes') }}",
                    swText ? swText.split(' ').slice(0, 3).join(' ') : "{{ lang._('set at the end of each boot') }}"));
            const bays = Object.keys(slots).map(function (bay) {
                const s = slots[bay];
                return "{{ lang._('bay') }}" + ' ' + bay + ': ' + (s['assembly.partno'] || '?') +
                    (s['assembly.revision'] ? ' rev ' + s['assembly.revision'] : '');
            });
            $('#xgs_module').html(bays.length ? bays.join('<br/>') :
                '<span class="text-muted">' + "{{ lang._('none fitted') }}" + '</span>');
            const sw = (data && data.switch) || '';
            $('#xgs_switch').html(sw ? (sw.indexOf('10,240') >= 0 ?
                '<i class="fa fa-check text-success"></i> ' + "{{ lang._('jumbo, 10,240 bytes') }}" :
                "{{ lang._('2,048 bytes') }}") + ' <small class="text-muted">' + sw + '</small>' :
                '<span class="text-muted">' + "{{ lang._('set at the end of each boot') }}" + '</span>');
        });
        /*
         * Power over Ethernet, where the appliance has it (labels 11 and 12 on the XGS 136): read
         * from the controller each time the page opens, through the coprocessor. Switching a port
         * is granted with XGS: Settings; a user who may only read is told so by the answer.
         */
        function poeCell(text, cls) {
            return $('<td/>').text(text === undefined || text === null ? '' : String(text)).addClass(cls || '');
        }
        function poeLoad() {
            ajaxGet('/api/xgs/ports/poe', {}, function (data) {
                if (!data || !data.present) {
                    $('#xgs_poe').toggle(!!(data && data.error));
                    $('#xgs_poe_error').text((data && data.error) || '').toggle(!!(data && data.error));
                    return;
                }
                $('#xgs_poe').show();
                $('#xgs_poe_error').text(data.error || '').toggle(!!data.error);
                $('#xgs_poe_module').text((data.modules || []).map(function (m) {
                    return "{{ lang._('module') }}" + ' ' + m.id + ': ' + m.status + ', ' +
                        "{{ lang._('budget') }}" + ' ' + m.budget_w + ' W';
                }).join(' · '));
                const $rows = $('#xgs_poe_rows').empty();
                (data.ports || []).forEach(function (p) {
                    const powered = String(p.enabled).toLowerCase() === 'yes';
                    let state, cls;
                    if (p.kept_off) {
                        state = p.switched_off ? "{{ lang._('switched off') }}" : "{{ lang._('switched off in the settings, still on') }}";
                        cls = p.switched_off ? 'text-muted' : 'text-warning';
                    } else if (p.switched_off) {
                        state = "{{ lang._('off, not by this page') }}";
                        cls = 'text-warning';
                    } else {
                        state = powered ? "{{ lang._('on, delivering power') }}" : "{{ lang._('on, no device powered') }}";
                        cls = powered ? 'text-success' : '';
                    }
                    const drawn = [p.power_w ? p.power_w + ' W' : '', p.voltage_v ? p.voltage_v + ' V' : '',
                        p.current_ma ? p.current_ma + ' mA' : ''].filter(Boolean).join(', ');
                    const $act = $('<td/>');
                    $('<button type="button" class="btn btn-xs btn-default xgs-poe"/>')
                        .attr('data-label', p.label).attr('data-do', p.kept_off ? 'on' : 'off')
                        .text(p.kept_off ? "{{ lang._('Switch on') }}" : "{{ lang._('Switch off') }}").appendTo($act);
                    $act.append(' ');
                    $('<button type="button" class="btn btn-xs btn-default xgs-poe"/>')
                        .attr('data-label', p.label).attr('data-do', 'cycle').prop('disabled', !!p.kept_off)
                        .text("{{ lang._('Power cycle') }}").appendTo($act);
                    $('<tr/>').append(poeCell(p.label).css('font-weight', 'bold'), poeCell(state, cls),
                        poeCell(p.detection), poeCell(p['class']), poeCell(drawn || '—'),
                        poeCell(p.limit_w), $act).appendTo($rows);
                });
            });
        }
        poeLoad();

        /*
         * The switch's own counters for the ports behind it (scripts/xgs/portstats.py): what a port
         * took in with a bad checksum, too short or too long, or dropped - frames that never reach
         * the host. Read by the plugin's cron every five minutes; "Read now" asks the coprocessor.
         */
        const errNames = {
            in_fcs_err: "{{ lang._('bad checksum') }}", in_rx_err: "{{ lang._('receive error') }}",
            in_undersize: "{{ lang._('too short') }}", in_fragments: "{{ lang._('fragments') }}",
            in_oversize: "{{ lang._('too long') }}", in_jabber: "{{ lang._('jabber') }}",
            in_bad_octets: "{{ lang._('bytes in bad frames') }}", in_discards: "{{ lang._('dropped') }}",
            out_fcs_err: "{{ lang._('sent with a bad checksum') }}", collisions: "{{ lang._('collisions') }}",
            late: "{{ lang._('late collisions') }}", excessive: "{{ lang._('excessive collisions') }}"
        };
        const inErr = ['in_fcs_err', 'in_rx_err', 'in_undersize', 'in_fragments', 'in_oversize', 'in_jabber'];
        const outErr = ['out_fcs_err', 'collisions', 'late', 'excessive'];
        function num(v) {
            return (v === undefined || v === null) ? '?' : Number(v).toLocaleString();
        }
        function bytes(v) {
            if (v === undefined || v === null) {
                return '?';
            }
            const u = ['B', 'KB', 'MB', 'GB', 'TB'];
            let i = 0;
            while (v >= 1000 && i < u.length - 1) {
                v /= 1000;
                i++;
            }
            return (i ? v.toFixed(1) : String(v)) + ' ' + u[i];
        }
        function errCell(c, names, moved) {
            const parts = names.filter(function (n) { return c[n]; }).map(function (n) {
                return errNames[n] + ' ' + num(c[n]);
            });
            const fresh = names.reduce(function (a, n) { return a + ((moved || {})[n] || 0); }, 0);
            const $td = $('<td/>');
            if (!parts.length) {
                return $td.append($('<span class="text-success"/>').text('0'));
            }
            $td.append($('<span/>').addClass(fresh ? 'text-danger' : 'text-warning').text(parts.join(', ')));
            if (fresh) {
                $td.append(' ', $('<small class="text-danger"/>').text('+' + num(fresh) + ' ' + "{{ lang._('since the reading before') }}"));
            }
            return $td;
        }
        function countersShow(data) {
            const $rows = $('#xgs_counters_rows').empty();
            const ports = (data && data.ports) || {};
            const order = Object.keys(ports).sort(function (a, b) {
                const ka = (a.charAt(0) === 'F' ? 100 : 0) + parseInt(a.replace(/\D/g, ''), 10);
                const kb = (b.charAt(0) === 'F' ? 100 : 0) + parseInt(b.replace(/\D/g, ''), 10);
                return ka - kb;
            });
            order.forEach(function (name) {
                const c = ports[name], moved = ((data.since || {})[name]) || {};
                const inFrames = (c.in_unicast || 0) + (c.in_broadcast || 0) + (c.in_multicast || 0);
                const outFrames = (c.out_unicast || 0) + (c.out_broadcast || 0) + (c.out_multicast || 0);
                $('<tr/>').append(
                    $('<td/>').css('font-weight', 'bold').text(name),
                    $('<td/>').text(num(inFrames) + ' · ' + bytes(c.in_good_octets)),
                    $('<td/>').text(num(outFrames) + ' · ' + bytes(c.out_octets)),
                    errCell(c, inErr, moved),
                    errCell(c, ['in_discards'], moved),
                    errCell(c, outErr, moved)
                ).appendTo($rows);
            });
            const when = data && (data.read_at || data.at);
            $('#xgs_counters_when').text(when && order.length ? "{{ lang._('read') }}" + ' ' + new Date(when * 1000).toLocaleString() : '');
            /* Nothing to show is said in words, the reason on hover: the script's own message is for
               the log, not for the page. */
            $('#xgs_counters_error').text(order.length ? '' : "{{ lang._('Not available on this appliance.') }}")
                .attr('title', (data && data.error) || '').toggle(!order.length);
            $('#xgs_counters thead tr').eq(1).toggle(order.length > 0);
            $('#xgs_counters').show();
        }
        function countersRead(done) {
            const $b = $('#xgs_counters_read').prop('disabled', true);
            ajaxCall('/api/xgs/ports/countersRead', {}, function (data) {
                $b.prop('disabled', false);
                countersShow(data || {});
                if (done) {
                    done();
                }
            });
        }
        /* What the cron kept, and when nothing was kept yet - a boot or an update ago - a reading now. */
        ajaxGet('/api/xgs/ports/counters', {}, function (data) {
            if (data && data.ports && Object.keys(data.ports).length) {
                countersShow(data);
                return;
            }
            countersRead();
        });
        $('#xgs_counters_read').click(function () {
            countersRead();
        });

        $(document).on('click', '.xgs-poe', function () {
            const label = String($(this).data('label')), act = String($(this).data('do'));
            const run = function () {
                $('.xgs-poe').prop('disabled', true);
                ajaxCall('/api/xgs/poe/set', {label: label, do: act}, function (data) {
                    if (!data || data.error || data.result === 'failed') {
                        BootstrapDialog.show({
                            type: BootstrapDialog.TYPE_WARNING,
                            title: "{{ lang._('Power over Ethernet') }}",
                            message: $('<div/>').text((data && data.error) ||
                                "{{ lang._('No answer: this account may lack the XGS: Settings privilege.') }}"),
                            buttons: [{label: "{{ lang._('Close') }}", action: function (dialog) { dialog.close(); }}]
                        });
                    }
                    poeLoad();
                });
            };
            if (act === 'on') {
                run();
                return;
            }
            stdDialogConfirm("{{ lang._('Power over Ethernet') }}",
                act === 'off' ?
                    "{{ lang._('Switch this port off? A device it powers goes dark, and the port stays off, also after a reboot, until it is switched on here.') }}" :
                    "{{ lang._('Switch this port off and, three seconds later, on again? A device it powers restarts.') }}",
                act === 'off' ? "{{ lang._('Switch off') }}" : "{{ lang._('Power cycle') }}",
                "{{ lang._('Cancel') }}", run, 'warning');
        });
    });
</script>

<div class="tab-content content-box">
    <div class="xgs-cards" id="xgs_cards"></div>
    <table id="grid-ports" class="table table-bordered table-condensed table-hover table-striped table-responsive">
        <thead>
            <tr>
                <th data-column-id="status" data-width="5em" data-formatter="status" data-type="string">{{ lang._('Status') }}</th>
                <th data-column-id="port" data-formatter="port" data-type="string">{{ lang._('Port') }}</th>
                <th data-column-id="assigned" data-formatter="assigned" data-type="string">{{ lang._('Assigned as') }}</th>
                <th data-column-id="address" data-type="string">{{ lang._('Address') }}</th>
                <th data-column-id="speed" data-formatter="speed" data-type="string" data-width="6em">{{ lang._('Speed') }}</th>
                <th data-column-id="media" data-type="string" data-visible="false">{{ lang._('Media') }}</th>
                <th data-column-id="mtu" data-type="string" data-width="6em">{{ lang._('MTU') }}</th>
                <th data-column-id="bridge" data-type="string" data-width="7em">{{ lang._('Bridge') }}</th>
                <th data-column-id="device" data-identifier="true" data-type="string" data-width="7em">{{ lang._('Device') }}</th>
            </tr>
        </thead>
        <tbody></tbody>
        <tfoot></tfoot>
    </table>
</div>

<div class="content-box" id="xgs_poe" style="margin-top: 15px; display: none;">
    <table class="table table-striped table-condensed">
        <thead>
            <tr>
                <th colspan="7">
                    <i class="fa fa-bolt fa-fw text-muted"></i> {{ lang._('Power over Ethernet') }}
                    <small class="text-muted" id="xgs_poe_module"></small>
                </th>
            </tr>
            <tr>
                <th>{{ lang._('Port') }}</th>
                <th>{{ lang._('Power') }}</th>
                <th>{{ lang._('Detection') }}</th>
                <th>{{ lang._('Class') }}</th>
                <th>{{ lang._('Drawn now') }}</th>
                <th>{{ lang._('Limit') }}</th>
                <th></th>
            </tr>
        </thead>
        <tbody id="xgs_poe_rows"></tbody>
        <tfoot>
            <tr>
                <td colspan="7">
                    <div class="text-warning" id="xgs_poe_error" style="display: none;"></div>
                    <small class="text-muted">{{ lang._('Read from the PoE controller when the page opens. A port switched off stays off after a reboot; a power cycle is not kept. The controller on this module takes no priority or power limit.') }}</small>
                </td>
            </tr>
        </tfoot>
    </table>
</div>

<div class="content-box" id="xgs_counters" style="margin-top: 15px; display: none;">
    <table class="table table-striped table-condensed">
        <thead>
            <tr>
                <th colspan="6">
                    <i class="fa fa-exclamation-triangle fa-fw text-muted"></i> {{ lang._('Errors and drops on the switch') }}
                    <small class="text-muted" id="xgs_counters_when"></small>
                    <button type="button" class="btn btn-xs btn-default pull-right" id="xgs_counters_read">{{ lang._('Read now') }}</button>
                </th>
            </tr>
            <tr>
                <th>{{ lang._('Port') }}</th>
                <th>{{ lang._('Received, frames and bytes') }}</th>
                <th>{{ lang._('Sent, frames and bytes') }}</th>
                <th>{{ lang._('Received in error') }}</th>
                <th>{{ lang._('Dropped on arrival') }}</th>
                <th>{{ lang._('Errors sending') }}</th>
            </tr>
        </thead>
        <tbody id="xgs_counters_rows"></tbody>
        <tfoot>
            <tr>
                <td colspan="6">
                    <div class="text-muted" id="xgs_counters_error" style="display: none; margin-bottom: 4px;"></div>
                    <small class="text-muted">{{ lang._('The switch chip counts these itself for the ports behind it: Port1 to Port8 and two of the cages. A frame it took in with a bad checksum, too short or too long, or dropped, never reaches the host or its interface counters. Counted since the coprocessor last started; read every five minutes.') }}</small>
                </td>
            </tr>
        </tfoot>
    </table>
</div>

<div class="content-box" style="margin-top: 15px;">
    <table class="table table-striped opnsense_standard_table_form">
        <colgroup><col class="col-md-3"/><col class="col-md-9"/></colgroup>
        <tbody>
            <tr>
                <td><i class="fa fa-info-circle fa-fw text-muted"></i> <b>{{ lang._('Expansion module') }}</b></td>
                <td id="xgs_module">&hellip;</td>
            </tr>
            <tr>
                <td><i class="fa fa-info-circle fa-fw text-muted"></i> <b>{{ lang._('Switch chip') }}</b></td>
                <td id="xgs_switch">&hellip;</td>
            </tr>
        </tbody>
    </table>
</div>

{{ partial("OPNsense/XGS/credit") }}
