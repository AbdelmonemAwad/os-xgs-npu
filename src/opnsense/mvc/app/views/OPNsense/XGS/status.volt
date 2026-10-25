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
    /* the heading of a section, as OPNsense's base_form draws one */
    .xgs-head { padding-top: 5px; padding-bottom: 5px; font-size: 16px; }
    /* The summary as cards, one for each thing a glance is for; the colours are the theme's text
       and a faint tint of it, so the cards read the same in the light theme and the dark one. */
    /* A grid rather than PAGE-DESIGN's flex row: six cards, so the sixth keeps the others' width
       on a row of its own instead of stretching across it. */
    .xgs-cards { display: grid; grid-template-columns: repeat(auto-fill, minmax(200px, 1fr)); gap: 12px; padding: 15px; }
    .xgs-card { flex: 1 1 200px; min-width: 190px; border: 1px solid rgba(128, 128, 128, 0.25);
                border-inline-start: 4px solid #9e9e9e; border-radius: 3px; padding: 10px 14px;
                background-color: rgba(128, 128, 128, 0.04); }
    .xgs-card.ok { border-inline-start-color: #5cb85c; }
    .xgs-card.warn { border-inline-start-color: #f0ad4e; }
    .xgs-card .k { font-size: 11px; text-transform: uppercase; letter-spacing: .04em; opacity: 0.6; }
    .xgs-card .v { font-size: 19px; font-weight: 600; line-height: 1.3; margin: 2px 0; }
    .xgs-card .s { font-size: 12px; opacity: 0.7; }
    .xgs-card .fa { opacity: 0.35; float: inline-end; font-size: 18px; margin-top: 2px; }
    .xgs-card.bad { border-inline-start-color: #d9534f; }
    /* The chassis, drawn: every front port in the order the panel has them, each with its link as a
       light. Left to right in every language, as the metal is. The colours are the theme's where it
       gives them (XGS Silicon does), and the plugin's own judgement colours where it does not. */
    .xgs-panel { display: flex; flex-wrap: wrap; align-items: flex-end; gap: 18px; padding: 15px 15px 0; direction: ltr; }
    .xgs-panel-group { display: flex; flex-direction: column; align-items: center; gap: 4px; }
    .xgs-panel-ports { display: flex; gap: 5px; }
    .xgs-port { display: inline-flex; flex-direction: column; align-items: center; justify-content: space-between;
                width: 30px; height: 34px; padding: 4px 0; box-sizing: border-box; border-radius: 4px;
                border: 1px solid rgba(128, 128, 128, 0.45); background-color: rgba(128, 128, 128, 0.10); }
    .xgs-port:hover, .xgs-port:focus { border-color: var(--xgs-copper, #c03e14); text-decoration: none; }
    .xgs-port .jack { width: 16px; height: 10px; border-radius: 2px; background-color: rgba(0, 0, 0, 0.55); }
    .xgs-port.cage .jack { width: 20px; height: 7px; border-radius: 1px; }
    .xgs-port .led { width: 6px; height: 6px; border-radius: 3px; background-color: rgba(128, 128, 128, 0.45); }
    .xgs-port.up .led { background-color: var(--xgs-up, #5cb85c); box-shadow: 0 0 4px var(--xgs-up, #5cb85c); }
    .xgs-panel-label { font-size: 11px; opacity: 0.65; font-family: monospace; }
    /* how much of what the board holds is in use, under the figure */
    .xgs-bar { height: 6px; border-radius: 3px; background-color: rgba(128, 128, 128, 0.20); margin-top: 5px; max-width: 360px; }
    .xgs-bar > span { display: block; height: 6px; border-radius: 3px; min-width: 2px;
                      background-color: var(--xgs-signal, #31708f); }
    @media (min-width: 768px) {
        #grid-counters .tabulator-tableholder { overflow-x: hidden; }
    }
</style>

<script>
    $(document).ready(function () {
        /* Laid out as OPNsense's own status pages are: tabs, a striped table of what the coprocessor
           and its driver report, the datapath counters in OPNsense's grid, and the technical details
           in a table of their own. The API escapes every string it returns, so a value is inserted
           as it comes; only what this page builds itself is escaped here. */
        function val(s) {
            return (s === null || s === undefined || s === '') ? '&mdash;' : String(s);
        }

        function badge(on, yes, no) {
            return '<span class="label label-' + (on ? 'success' : 'default') + '">' + (on ? yes : no) + '</span>';
        }

        /* A value of figures only ("8 / 16") is set left to right on its own: in an Arabic page the
           bidirectional algorithm would otherwise draw it as "16 / 8". */
        function figures(v) {
            return /^[0-9\s.,\/:%+\-]+$/.test(String(v)) ? '<bdi dir="ltr">' + v + '</bdi>' : v;
        }

        function card(cls, icon, key, value, sub) {
            return '<div class="xgs-card ' + cls + '"><i class="fa ' + icon + '"></i>' +
                '<div class="k">' + key + '</div><div class="v">' + figures(value) + '</div>' +
                '<div class="s">' + (sub || '&nbsp;') + '</div></div>';
        }

        /* A row is [label, value] or [label, value, share]: a share from 0 to 1 draws a bar of how
           much of what the board holds is in use. */
        function rows($body, list) {
            $body.empty();
            list.forEach(function (r) {
                let bar = '';
                if (typeof r[2] === 'number' && isFinite(r[2])) {
                    bar = '<div class="xgs-bar"><span style="width: ' +
                        Math.max(0, Math.min(100, r[2] * 100)).toFixed(1) + '%;"></span></div>';
                }
                $body.append('<tr><td style="width: 30%;">' + r[0] + '</td><td>' + val(r[1]) + bar + '</td></tr>');
            });
        }

        function share(used, max) {
            return (max === null || max === undefined || Number(max) <= 0) ? undefined :
                Number(used || 0) / Number(max);
        }

        /* The front panel: the ports in the order the driver made them, which is the panel's own -
           the two cages, Port1 to Port8, the other two cages and the expansion module on the XGS
           3300; Port1 to Port12 and the two cages on the XGS 136 - in a group for each kind. */
        function panel(ports) {
            const list = (ports && ports.ports) || [];
            const groups = [];
            list.forEach(function (p) {
                const name = String(p.label || p.device || '');
                const kind = /^Port\d/.test(name) ? 'copper' : (/^F\d/.test(name) ? 'cage' : 'module');
                if (!groups.length || groups[groups.length - 1].kind !== kind) {
                    groups.push({kind: kind, ports: []});
                }
                groups[groups.length - 1].ports.push(p);
            });
            if (!groups.length) {
                return '';
            }
            let html = '<div class="xgs-panel" role="img" aria-label="{{ lang._('The front panel') }}">';
            groups.forEach(function (g) {
                html += '<div class="xgs-panel-group"><div class="xgs-panel-ports">';
                g.ports.forEach(function (p) {
                    const up = String(p.status || '').indexOf('active') === 0;
                    const title = [p.label, p.assigned, up ? p.media : "{{ lang._('no link') }}", p.mac]
                        .filter(function (s) { return s; }).join(' - ');
                    html += '<a class="xgs-port' + (g.kind === 'copper' ? '' : ' cage') + (up ? ' up' : '') +
                        '" href="/ui/xgs/ports" title="' + title + '"><span class="jack"></span><span class="led"></span></a>';
                });
                const first = g.ports[0].label, last = g.ports[g.ports.length - 1].label;
                html += '</div><div class="xgs-panel-label">' + (first === last ? first : first + ' - ' + last) + '</div></div>';
            });
            return html + '</div>';
        }

        /* The last restart, as the host's chipset recorded it at this boot (reset_reason.py): the
           cause that matters most among the bits it set, judged as the cards are. */
        function restart(r) {
            if (!r) {
                return card('off', 'fa-power-off', "{{ lang._('Last restart') }}", "{{ lang._('not recorded') }}",
                    "{{ lang._('read from the chipset at each boot') }}");
            }
            const bits = r.bits || [];
            const has = function (list) { return list.some(function (b) { return bits.indexOf(b) >= 0; }); };
            let cls = 'ok', what = "{{ lang._('power on') }}";
            if (has([27, 31, 30])) { cls = 'bad'; what = "{{ lang._('an uncorrected hardware error') }}"; }
            else if (has([23])) { cls = 'bad'; what = "{{ lang._('the processor shut down') }}"; }
            else if (has([0, 9])) { cls = 'bad'; what = "{{ lang._('overheating') }}"; }
            else if (has([25])) { cls = 'warn'; what = "{{ lang._('the hardware watchdog') }}"; }
            else if (has([24, 29])) { cls = 'warn'; what = "{{ lang._('a failed boot') }}"; }
            else if (has([16, 22, 26])) { cls = 'warn'; what = "{{ lang._('the reset pin') }}"; }
            else if (has([1, 2, 4])) { cls = 'warn'; what = "{{ lang._('the power button') }}"; }
            else if (has([17, 18, 19, 20])) { what = "{{ lang._('a reboot') }}"; }
            else if (has([21])) { what = "{{ lang._('a sleep state') }}"; }
            const when = r.boot ? new Date(r.boot * 1000).toLocaleString() : '';
            return card(cls, 'fa-power-off', "{{ lang._('Last restart') }}", what,
                when ? "{{ lang._('since') }}" + ' ' + '<bdi dir="ltr">' + when + '</bdi>' : '');
        }

        /*
         * The coprocessor's own Linux as scripts/xgs/health.py last read it, every five minutes: up
         * for how long, its load on its cores, its memory. A coprocessor that restarted on its own
         * is up for less time than the host.
         */
        function health(h) {
            if (!h || (h.uptime_s === undefined && !h.error)) {
                return '<span class="text-muted">' + "{{ lang._('not read yet') }}" + '</span>';
            }
            if (h.uptime_s === undefined) {
                return $('<span class="text-warning"/>').text(h.error).prop('outerHTML');
            }
            const d = Math.floor(h.uptime_s / 86400), hh = Math.floor(h.uptime_s % 86400 / 3600),
                mm = Math.floor(h.uptime_s % 3600 / 60);
            const up = (d ? d + ' ' + "{{ lang._('d') }}" + ' ' : '') + hh + ' ' + "{{ lang._('h') }}" + ' ' + mm + ' ' + "{{ lang._('min') }}";
            const parts = ["{{ lang._('up') }}" + ' ' + up];
            if (h.load) {
                parts.push("{{ lang._('load') }}" + ' ' + h.load.join(' / ') +
                    (h.cpus ? ' ' + "{{ lang._('on') }}" + ' ' + h.cpus + ' ' + "{{ lang._('cores') }}" : ''));
            }
            if (h.mem_total_kb) {
                parts.push("{{ lang._('memory') }}" + ' ' + Math.round((h.mem_available_kb || 0) / 1024).toLocaleString() + ' ' +
                    "{{ lang._('of') }}" + ' ' + Math.round(h.mem_total_kb / 1024).toLocaleString() + ' MB ' + "{{ lang._('available') }}");
            }
            return parts.join(' &middot; ') + ' <small class="text-muted">' + "{{ lang._('read') }}" + ' ' +
                new Date(h.at * 1000).toLocaleTimeString() + '</small>';
        }

        /*
         * The heartbeat with the fast path, as the driver keeps it (dev.octep.0.heartbeat): asked every
         * five seconds, late after 15 without an answer, taken as stopped at 30 - on the XGS 136 an
         * answer is the beat taken off the host's queue. "Not asked" when heartbeat.on is 0.
         */
        function heartbeat(h) {
            if (!h || h.on === 0 || h.state === 'off') {
                return '<span class="text-muted">' + "{{ lang._('not asked') }}" + '</span>';
            }
            const secs = Math.round((h.since_answer_ms || 0) / 1000);
            let text;
            if (h.state === 'alive') {
                text = '<span class="text-success">' + "{{ lang._('answering') }}" + '</span>' +
                    (h.rtt_us !== null && h.rtt_us !== undefined ? ' &middot; <bdi dir="ltr">' + h.rtt_us + ' µs</bdi>' : '');
            } else if (h.state === 'waiting') {
                text = '<span class="text-muted">' + "{{ lang._('asked, no answer yet') }}" + '</span>';
            } else if (h.state === 'late') {
                text = '<span class="text-warning">' + "{{ lang._('no answer for') }}" + ' ' + secs + ' ' + "{{ lang._('s') }}" + '</span>';
            } else {
                text = '<span class="text-danger">' + "{{ lang._('taken as stopped') }}" + ' &middot; ' +
                    "{{ lang._('no answer for') }}" + ' ' + secs + ' ' + "{{ lang._('s') }}" + '</span>';
            }
            if (h.deaths) {
                text += ' <small class="text-muted">' + "{{ lang._('times taken as stopped since boot') }}" + ': ' + h.deaths + '</small>';
            }
            return text;
        }

        /* The Intrusion Detection, when what the coprocessor carries no longer reaches it (status.py,
           intrusion_detection): said here and on XGS: Settings, because nothing else says it. */
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
            return '<div class="alert alert-warning" role="alert" style="margin: 15px 15px 0;">' + text + '</div>';
        }

        function render(st, ports, ipsec, settings) {
            $('#xgs_ids').html(ids_note(st && st.intrusion_detection));
            if (!st || st.present !== true) {
                $('#xgs_cards').html(card('warn', 'fa-microchip', "{{ lang._('Coprocessor') }}",
                    "{{ lang._('not attached') }}",
                    st && st.message ? st.message : "{{ lang._('the coprocessor driver is not attached') }}"));
                $('#xgs_link, #xgs_limits, #xgs_rpc, #xgs_raw').empty();
                return;
            }
            const b = st.board || {}, dr = st.driver || {}, hs = st.handshake || {},
                rpc = st.rpc || {}, nwa = st.netagent || {}, off = st.offload || {},
                g = (settings && settings.xgs && settings.xgs.general) || {};
            const family = b.family === 'octeontx' ? 'OCTEON TX' : (b.family === 'armada' ? 'ARMADA' : val(b.family));
            const linked = String(hs.state || '').indexOf('completed') === 0 || dr.ready === 1;
            const plist = (ports && ports.ports) || [];
            const up = plist.filter(function (p) { return String(p.status || '').indexOf('active') === 0; }).length;
            const extra = (ports && ports.module && ports.module.extra) || [];
            /* what the engine holds now, as scripts/xgs/ipsec.py counts it for either family */
            const sas = ipsec && ipsec.summary ?
                Number(ipsec.summary.live_encrypt || 0) + Number(ipsec.summary.live_decrypt || 0) : 0;
            const ipsecOn = ipsec && ipsec.tunable && ipsec.tunable.value === '1';
            const offOn = off.offload_bit_requested === true;
            const sw = (ports && ports.switch) || '';

            let cards = '';
            const stopped = st.heartbeat && st.heartbeat.state === 'dead';
            cards += card(linked && !stopped ? 'ok' : 'warn', 'fa-microchip', "{{ lang._('Coprocessor') }}", family,
                stopped ? "{{ lang._('its fast path is not answering') }}" :
                (linked ? "{{ lang._('attached, link up') }}" : "{{ lang._('not attached') }}"));
            cards += card(offOn ? 'ok' : 'off', 'fa-bolt', "{{ lang._('Hardware offloading') }}",
                offOn ? "{{ lang._('On') }}" : "{{ lang._('Off') }}",
                g.flow_auto === '1' ? "{{ lang._('connections accelerated automatically') }}" : "{{ lang._('no automatic acceleration') }}");
            cards += card(ipsecOn ? 'ok' : 'off', 'fa-lock', "{{ lang._('IPsec engine') }}",
                ipsecOn ? sas + ' ' + "{{ lang._('associations') }}" : "{{ lang._('Off') }}",
                ipsecOn ? "{{ lang._('in the coprocessor') }}" : "{{ lang._('the host encrypts') }}");
            cards += card(up > 0 ? 'ok' : 'warn', 'fa-plug', "{{ lang._('Front ports') }}",
                up + ' / ' + plist.length, extra.length ? extra.length + ' ' + "{{ lang._('on the expansion module') }}" : "{{ lang._('with link') }}");
            /* on the XGS 136 the network agent sets the switch's limit from each port's MTU (issue 706) */
            cards += card(sw.indexOf('10,240') >= 0 || sw.indexOf('network agent') >= 0 ? 'ok' : 'off', 'fa-sitemap', "{{ lang._('Switch chip') }}",
                sw.indexOf('network agent') >= 0 ? "{{ lang._('Follows the MTU') }}" :
                sw.indexOf('10,240') >= 0 ? "{{ lang._('Jumbo frames') }}" : (g.switch_jumbo === '0' ? "{{ lang._('2,048 bytes') }}" : '&mdash;'),
                sw ? sw.split(' ').slice(0, 3).join(' ') : "{{ lang._('set at the end of each boot') }}");
            if (b.rnd) {
                cards += card('warn', 'fa-flask', "{{ lang._('This model') }}", b.model ? b.model : ('AMDA0' + b.assembly),
                    "{{ lang._('untested: research and development, no appliance to test on') }}");
            }
            cards += restart(st.reset);
            $('#xgs_cards').html(cards);
            $('#xgs_panel').html(panel(ports));
            rows($('#xgs_link'), [
                ["{{ lang._('Board') }}", b.assembly ? (b.model ? b.model + ', ' : '') + 'AMDA0' + b.assembly + ' (' + family + ')' : null],
                ["{{ lang._('Driver') }}", dr.description],
                ["{{ lang._('Handshake') }}", hs.state],
                ["{{ lang._('Rings in use') }}", hs.rings_in_use],
                ["{{ lang._('Virtual functions') }}", hs.vfs],
                ["{{ lang._('Its own Linux') }}", health(st.health)],
                ["{{ lang._('Fast path') }}", heartbeat(st.heartbeat)]
            ]);
            /* What the board itself says it holds, and what this host allocated of it and uses now. */
            const lim = st.limits || {};
            function num(n) { return (n === null || n === undefined) ? null : Number(n).toLocaleString(); }
            function of(used, max) {
                return (max === null || max === undefined) ? null :
                    num(used === null || used === undefined ? 0 : used) + ' ' + "{{ lang._('of') }}" + ' ' + num(max);
            }
            rows($('#xgs_limits'), [
                ["{{ lang._('Platform') }}", lim.platform],
                ["{{ lang._('Connections the board holds') }}", num(lim.board_connections)],
                ["{{ lang._('Connections in hardware') }}", of(lim.connections_in_use, lim.connections_max),
                    share(lim.connections_in_use, lim.connections_max)],
                ["{{ lang._('Next hops in use') }}", of(lim.next_hops_in_use, lim.next_hops_max),
                    share(lim.next_hops_in_use, lim.next_hops_max)],
                ["{{ lang._('IPsec associations the board holds') }}", num(lim.board_associations)],
                ["{{ lang._('Transmit queues') }}", lim.transmit_queues],
                ["{{ lang._('Flow writes') }}", lim.flow_writes === 'posted on the high-priority ring' ?
                    "{{ lang._('posted on the high-priority ring') }}" :
                    (lim.flow_writes ? "{{ lang._('each one waited for') }}" : null)],
                ["{{ lang._('Connections made at most') }}", lim.made_at_most === null || lim.made_at_most === undefined ? null :
                    num(lim.made_at_most) + ' ' + (lim.made_per === 'second' ? "{{ lang._('a second') }}" : "{{ lang._('a sweep, one sweep a second') }}")],
                ["{{ lang._('IPv6 connections left with the host') }}", num(lim.ipv6_refused)]
            ].filter(function (r) { return r[1] !== null && r[1] !== undefined; }));
            rows($('#xgs_rpc'), [
                ["{{ lang._('Control facility ready') }}", badge(rpc.ready === 1, "{{ lang._('yes') }}", "{{ lang._('no') }}")],
                ["{{ lang._('Port control ready') }}", badge(nwa.ready === 1, "{{ lang._('yes') }}", "{{ lang._('no') }}")],
                ["{{ lang._('Port commands issued') }}", nwa.commands],
                ["{{ lang._('Port command timeouts') }}", nwa.timeouts],
                ["{{ lang._('Last command') }}", rpc.last]
            ]);
            rows($('#xgs_raw'), [
                ["{{ lang._('Rings') }}", hs.rings],
                ["{{ lang._('Host status') }}", dr.host_status],
                ["{{ lang._('Target status') }}", dr.target_status],
                ["{{ lang._('Writes permitted') }}", rpc.allow_write],
                ["fw_cfg", off.fw_cfg_requested],
                ["fw_rev", (off.fw_rev_posted !== undefined) ? off.fw_rev_posted + ' / ' + off.fw_rev_bumps : null],
                ["{{ lang._('Last restart, as the chipset recorded it') }}", st.reset ?
                    '<code>' + st.reset.value + '</code> ' + (st.reset.reasons || []).join('; ') : null],
                ["{{ lang._('Note') }}", off.note]
            ]);
        }

        /* The one question about what this appliance sends (docs/problem-reports.md). Nothing leaves
           it unless its owner chooses, so it is asked here once - until it is answered here, on XGS:
           Problem Reports, or by closing the header's notice - with the model's tested status beside
           it: a model the project has never run is seen only through its reports. Not asked of
           somebody whose privileges do not reach the reports page; its answer would not be saved. */
        let asked = false;
        function ask(b) {
            if (asked) {
                return;
            }
            asked = true;
            ajaxGet('/api/xgs/reports/state', {}, function (rs) {
                if (!rs || rs.status !== 'ok' || rs.notice_seen) {
                    return;
                }
                const name = b.model ? b.model : (b.assembly ? 'AMDA0' + b.assembly : "{{ lang._('This model') }}");
                const tested = (b.rnd ?
                    "{{ lang._('%s has never been run by the project: what breaks on it reaches the project only through its reports.') }}" :
                    "{{ lang._('%s is a model the project is built and measured on.') }}").replace('%s', name);
                $('#xgs_ask').html('<div class="alert alert-info" role="alert" style="margin: 15px 15px 0;">' +
                    '<p><b>' + "{{ lang._('Nothing leaves this appliance unless you choose') }}" + '</b></p>' +
                    '<p>' + "{{ lang._('Its problem reports - what goes wrong here, with every address, serial and name taken out - wait on XGS: Problem Reports until you send them, and npuforge.org does not count it. Both can be turned on here, once, or there at any time.') }}" + '</p>' +
                    '<p>' + tested + '</p>' +
                    '<p><b>' + "{{ lang._('Help it get better: one report from this appliance can fix a problem for everyone who runs the same model.') }}" + '</b></p>' +
                    '<button type="button" class="btn btn-primary btn-sm" id="xgs_ask_send">' +
                    "{{ lang._('Send reports and count this appliance') }}" + '</button> ' +
                    '<button type="button" class="btn btn-default btn-sm" id="xgs_ask_keep">' +
                    "{{ lang._('Keep everything here') }}" + '</button> ' +
                    '<a href="/ui/xgs/reports" style="margin-inline-start: 8px;">' +
                    "{{ lang._('Choose each one on XGS: Problem Reports') }}" + '</a></div>');
            });
        }

        function answered() {
            ajaxCall('/api/xgs/reports/acknowledge', {}, function () {
                $('#xgs_ask').empty();
                if (typeof updateSystemStatus === 'function') {
                    updateSystemStatus();
                }
            });
        }

        $(document).on('click', '#xgs_ask_send', function () {
            $('#xgs_ask button').prop('disabled', true);
            ajaxCall('/api/xgs/reports/set', {'xgs': {'reporting': {'mode': 'automatic', 'count': '1'}}}, function (data) {
                if (data && data.result === 'saved') {
                    answered();
                } else {
                    $('#xgs_ask button').prop('disabled', false);
                }
            });
        });

        $(document).on('click', '#xgs_ask_keep', function () {
            $('#xgs_ask button').prop('disabled', true);
            answered();
        });

        function refresh() {
            const $button = $('#refreshAct').prop('disabled', true);
            $.when(
                $.ajax({url: '/api/xgs/status/get', dataType: 'json'}),
                $.ajax({url: '/api/xgs/ports/get', dataType: 'json'}),
                $.ajax({url: '/api/xgs/ipsec/get', dataType: 'json'}),
                $.ajax({url: '/api/xgs/settings/get', dataType: 'json'})
            ).always(function () {
                $button.prop('disabled', false);
            }).done(function (st, ports, ipsec, settings) {
                render(st[0], ports[0], ipsec[0], settings[0]);
                ask((st[0] && st[0].board) || {});
            }).fail(function () {
                ajaxGet('/api/xgs/status/get', {}, function (st) {
                    render(st, null, null, null);
                    ask((st && st.board) || {});
                });
            });
        }

        $('#grid-counters').UIBootgrid({
            search: '/api/xgs/status/counters',
            options: {
                selection: false,
                formatters: {
                    mono: function (column, row) {
                        return '<code>' + row.name + '</code>';
                    },
                    number: function (column, row) {
                        return Number(row.value).toLocaleString();
                    }
                }
            }
        });

        $('#refreshAct').click(function () {
            refresh();
            $('#grid-counters').bootgrid('reload');
        });
        refresh();

        /* The Coprocessor tab: what scripts/xgs/coprocessor.py read on the coprocessor - its software,
           its chips, its flash and eMMC - kept from the last read and read again on a button. The
           script answers in codes; the words are this page's, in the page's language. */
        const COPRO = {
            roles: {
                root_running: "{{ lang._('The system the coprocessor runs, read-only') }}",
                root_other: "{{ lang._('The other copy of its system: the one an update writes, holding the build before or the same') }}",
                persistent: "{{ lang._('Its persistent partition, read-write: what is kept across its restarts') }}",
                maker: "{{ lang._('A Linux of the board maker with no vendor build: by the look of it, the factory or recovery system') }}",
                unknown: "{{ lang._('Not known') }}"
            },
            parts: {
                host: "{{ lang._('This host') }}",
                coprocessor: "{{ lang._('The coprocessor') }}",
                'switch': "{{ lang._('The switch') }}",
                phy: "{{ lang._('The PHY') }}",
                cpld: "{{ lang._('The CPLD') }}"
            },
            sources: {
                host: "{{ lang._('read on this host') }}",
                board_file: "{{ lang._('the vendor board file, and what the coprocessor reports') }}",
                photographed: "{{ lang._('photographed on this model and matched to the vendor board file') }}",
                family: "{{ lang._('photographed on another model of the family, not checked on this one') }}"
            },
            unread: "{{ lang._('not read: the coprocessor started with the host, before its console was kept') }}"
        };

        function size_kb(kb) {
            if (kb === null || kb === undefined || kb === '') {
                return null;
            }
            kb = Number(kb);
            if (kb < 1024) {
                return kb.toFixed(0) + ' KB';
            }
            return kb >= 1048576 ? (kb / 1048576).toFixed(2) + ' GB' : (kb / 1024).toFixed(kb >= 102400 ? 0 : 1) + ' MB';
        }

        /* The tab's summary, as the Status tab's: a card for each thing a glance is for - its build,
           its chip, the copy of its system it runs, its eMMC, its persistent partition, its boot
           firmware - with the tables under them for the rest. */
        function copro_cards(d) {
            const sw = d.software, fw = sw.firmware || {};
            const chip = (d.chips || []).filter(function (c) { return c.part === 'coprocessor'; })[0] || {};
            const disk = (d.emmc || [])[0] || {};
            const parts = disk.partitions || [];
            const running = parts.filter(function (p) { return p.role === 'root_running'; })[0] || {};
            const other = parts.filter(function (p) { return p.role === 'root_other'; })[0] || {};
            const keep = parts.filter(function (p) { return p.role === 'persistent'; })[0] || {};
            let cards = card(sw.sfversion ? 'ok' : 'warn', 'fa-tag', "{{ lang._('Vendor build') }}", val(sw.sfversion),
                sw.generation ? "{{ lang._('Generation the driver found') }}" + ': ' + sw.generation : '');
            cards += card('', 'fa-microchip', "{{ lang._('The coprocessor') }}", val(chip.name),
                chip.cpus ? chip.cpus + ' ' + "{{ lang._('cores') }}" + ', ' + size_kb(chip.memkb) : '');
            cards += card(running.slot ? 'ok' : '', 'fa-code-fork', "{{ lang._('Running copy') }}",
                running.slot ? "{{ lang._('Copy') }}" + ' ' + running.slot : val(running.partition),
                other.partition ? (other.inside && other.inside.sfversion ?
                    "{{ lang._('The other copy holds') }}" + ' ' + other.inside.sfversion :
                    "{{ lang._('The other copy holds') }}" + ': ' + "{{ lang._('not looked inside yet') }}") : '');
            cards += card('', 'fa-hdd-o', 'eMMC', val(size_kb(disk.kb)),
                [disk.maker, disk.name, disk.date].filter(function (x) { return x; }).join(' &middot; '));
            cards += card('', 'fa-archive', "{{ lang._('Persistent partition') }}",
                keep.partition ? val(size_kb(keep.used_kb)) + ' / ' + val(size_kb(keep.kb)) : '&mdash;', "{{ lang._('In use') }}");
            const boot = fw.uboot ? 'U-Boot ' + fw.uboot.split('-')[0] : ((fw.atf || fw.atf_flash) ?
                'ATF ' + String(fw.atf || fw.atf_flash).split('(')[0] : '&mdash;');
            cards += card(fw.uboot || fw.atf || fw.atf_flash ? '' : 'warn', 'fa-cogs', "{{ lang._('Boot firmware') }}", boot,
                fw.uboot && (fw.atf || fw.atf_flash) ? 'ATF ' + String(fw.atf || fw.atf_flash).split('(')[0] : (fw.uboot ? '' : "{{ lang._('U-Boot not read') }}"));
            $('#copro_cards').html(cards);
        }

        function copro(d) {
            if (!d || d.error || !d.software) {
                $('#copro_cards').html(card('warn', 'fa-microchip', "{{ lang._('The coprocessor') }}", '&mdash;',
                    d && d.error ? "{{ lang._('It could not be read - over its management link nor its console.') }}" :
                    "{{ lang._('Not read yet: press Read now.') }}"));
                rows($('#copro_sw'), [["{{ lang._('The coprocessor') }}", d && d.error ?
                    "{{ lang._('It could not be read - over its management link nor its console.') }}" :
                    "{{ lang._('Not read yet: press Read now.') }}"]]);
                $('#copro_chips, #copro_flash, #copro_emmc_dev, #copro_emmc').empty();
                $('#copro_when').text('');
                return;
            }
            copro_cards(d);
            const sw = d.software, fw = sw.firmware || {};
            rows($('#copro_sw'), [
                ["{{ lang._('The vendor build on the coprocessor') }}", sw.sfversion],
                ["{{ lang._('Generation the driver found') }}", sw.generation ? sw.generation +
                    (Number(sw.cmd_shift) ? ' &mdash; ' + "{{ lang._('commands from 36 one higher') }}" : '') : null],
                ["{{ lang._('Linux on the coprocessor') }}", sw.kernel],
                ["{{ lang._('Its system') }}", sw.os],
                ['U-Boot', fw.uboot || COPRO.unread],
                ["{{ lang._('ARM Trusted Firmware') }}", fw.atf || fw.atf_flash || COPRO.unread],
                ["{{ lang._('Its memory controller PHY firmware') }}", fw.ddr_phy],
                ['BDK', fw.bdk]
            ].filter(function (r) { return r[1] !== undefined && r[1] !== null && r[1] !== ''; }));

            $('#copro_chips').empty();
            (d.chips || []).forEach(function (c) {
                let what = '<b>' + val(c.name) + '</b>';
                if (c.detail) {
                    what += ' &mdash; ' + c.detail;
                }
                if (c.cpus) {
                    what += ' &mdash; ' + c.cpus + ' ' + "{{ lang._('cores') }}" + ', ' + size_kb(c.memkb) + ' ' + "{{ lang._('of memory to its Linux') }}";
                }
                if (c.where) {
                    what += ' &mdash; <bdi dir="ltr">' + c.where + '</bdi>';
                }
                what += '<br><small style="opacity: 0.7;">' + (COPRO.sources[c.source] || val(c.source)) + '</small>';
                $('#copro_chips').append('<tr><td style="width: 30%;">' + (COPRO.parts[c.part] || val(c.part)) +
                    '</td><td>' + what + '</td></tr>');
            });

            rows($('#copro_flash'), (d.flash || []).map(function (m) {
                return [m.device, m.name + ' &mdash; ' + size_kb(m.bytes / 1024)];
            }));

            const devs = d.emmc || [];
            rows($('#copro_emmc_dev'), devs.map(function (e) {
                return [e.device, [size_kb(e.kb), e.maker, e.name, e.date].filter(function (x) { return x; }).join(' &mdash; ') +
                    (e.boot_areas && e.boot_areas.length ? '<br><small style="opacity: 0.7;">' + "{{ lang._('beside it, its boot areas') }}" + ': ' +
                    e.boot_areas.map(function (b) { return b.device + ' ' + size_kb(b.kb); }).join(', ') + '</small>' : '')];
            }));
            $('#copro_emmc').empty();
            devs.forEach(function (e) {
                (e.partitions || []).forEach(function (p) {
                    let role = COPRO.roles[p.role] || val(p.role);
                    if (p.slot) {
                        role = "{{ lang._('Copy') }}" + ' ' + p.slot + ' &mdash; ' + role;
                    }
                    let holds = [];
                    if (p.role === 'root_running' && d.software.sfversion) {
                        holds.push("{{ lang._('Vendor build') }}" + ' ' + d.software.sfversion);
                    }
                    if (p.holds) {
                        holds.push(p.holds.join(', '));
                    }
                    if (p.inside) {
                        if (p.inside.sfversion) {
                            holds.push("{{ lang._('Vendor build') }}" + ' ' + p.inside.sfversion);
                        }
                        if (p.inside.os) {
                            holds.push(p.inside.os);
                        }
                        if (p.inside.boot) {
                            holds.push('<small>/boot: ' + p.inside.boot + '</small>');
                        }
                    }
                    const used = p.used_kb !== null && p.used_kb !== undefined ? p.used_kb :
                        (p.inside && p.inside.usedkb ? p.inside.usedkb : null);
                    $('#copro_emmc').append('<tr><td>' + p.partition + '</td><td>' + val(size_kb(p.kb)) + '</td><td>' + role +
                        '</td><td>' + (p.mounted ? p.mounted + ' (' + (p.rw ? "{{ lang._('read-write') }}" : "{{ lang._('read-only') }}") + ')' :
                        "{{ lang._('no') }}") + '</td><td>' + val(size_kb(used)) + '</td><td>' + val(holds.join('<br>')) + '</td></tr>');
                });
            });
            $('#copro_when').text("{{ lang._('Read') }}" + ' ' + new Date(d.at * 1000).toLocaleString() +
                (d.slots_at ? ' - ' + "{{ lang._('the closed partitions looked inside') }}" + ' ' + new Date(d.slots_at * 1000).toLocaleString() : ''));
        }

        function copro_read(what) {
            const $b = $('#coproReadAct, #coproSlotsAct');
            $b.prop('disabled', true);
            ajaxCall('/api/xgs/status/coprocessor', {'read': what}, function (d) {
                $b.prop('disabled', false);
                copro(d);
            });
        }
        ajaxGet('/api/xgs/status/coprocessor', {}, function (d) {
            copro(d);
        });
        $('#coproReadAct').on('click', function () { copro_read('collect'); });
        $('#coproSlotsAct').on('click', function () { copro_read('slots'); });

        /* the selected tab in the address, as on OPNsense's own tabbed pages */
        if (window.location.hash !== '') {
            $('a[href="' + window.location.hash + '"]').tab('show');
        }
        $('.nav-tabs a').on('shown.bs.tab', function (e) {
            history.pushState(null, null, e.target.hash);
        });
    });
</script>

<ul class="nav nav-tabs" data-tabs="tabs" id="maintabs">
    <li class="active"><a data-toggle="tab" href="#status">{{ lang._('Status') }}</a></li>
    <li><a data-toggle="tab" href="#counters">{{ lang._('Datapath counters') }}</a></li>
    <li><a data-toggle="tab" href="#coprocessor">{{ lang._('Coprocessor') }}</a></li>
    <li><a data-toggle="tab" href="#details">{{ lang._('Technical details') }}</a></li>
</ul>

<div class="tab-content content-box">
    <div id="status" class="tab-pane active table-responsive">
        <div id="xgs_ask"></div>
        <div id="xgs_ids"></div>
        <div id="xgs_panel"></div>
        <div class="xgs-cards" id="xgs_cards"></div>
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th colspan="2">
                        <div class="xgs-head">
                            <b>{{ lang._('Coprocessor link') }}</b>
                            <span class="pull-right">
                                <button class="btn btn-xs btn-default" id="refreshAct" type="button" title="{{ lang._('Refresh') }}"><i class="fa fa-fw fa-refresh"></i></button>
                            </span>
                        </div>
                    </th>
                </tr>
            </thead>
            <tbody id="xgs_link"></tbody>
            <thead>
                <tr><th colspan="2"><div class="xgs-head"><b>{{ lang._('This board\'s limits') }}</b></div></th></tr>
            </thead>
            <tbody id="xgs_limits"></tbody>
            <thead>
                <tr><th colspan="2"><div class="xgs-head"><b>{{ lang._('Control facility') }}</b></div></th></tr>
            </thead>
            <tbody id="xgs_rpc"></tbody>
        </table>
    </div>

    <div id="counters" class="tab-pane">
        <table id="grid-counters" class="table table-condensed table-hover table-striped table-responsive">
            <thead>
                <tr>
                    <th data-column-id="name" data-formatter="mono" data-type="string" data-identifier="true" data-width="14em">{{ lang._('Counter') }}</th>
                    <th data-column-id="value" data-formatter="number" data-type="numeric" data-width="10em">{{ lang._('Value') }}</th>
                    <th data-column-id="description" data-type="string">{{ lang._('What it counts') }}</th>
                </tr>
            </thead>
            <tbody></tbody>
            <tfoot></tfoot>
        </table>
    </div>

    <div id="coprocessor" class="tab-pane table-responsive">
        <div class="xgs-cards" id="copro_cards"></div>
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th colspan="2">
                        <div class="xgs-head">
                            <b>{{ lang._('Its software') }}</b>
                            <span class="pull-right">
                                <button class="btn btn-xs btn-default" id="coproReadAct" type="button">
                                    <i class="fa fa-fw fa-refresh"></i> {{ lang._('Read now') }}
                                </button>
                                <button class="btn btn-xs btn-default" id="coproSlotsAct" type="button"
                                        title="{{ lang._('Opens each partition nothing has mounted, read-only, for a moment') }}">
                                    <i class="fa fa-fw fa-folder-open-o"></i> {{ lang._('Look inside the closed partitions') }}
                                </button>
                            </span>
                        </div>
                    </th>
                </tr>
            </thead>
            <tbody id="copro_sw"></tbody>
            <thead>
                <tr><th colspan="2"><div class="xgs-head"><b>{{ lang._('Chips') }}</b></div></th></tr>
            </thead>
            <tbody id="copro_chips"></tbody>
            <thead>
                <tr><th colspan="2"><div class="xgs-head"><b>{{ lang._('Flash') }}</b></div></th></tr>
            </thead>
            <tbody id="copro_flash"></tbody>
            <thead>
                <tr><th colspan="2"><div class="xgs-head"><b>{{ lang._('eMMC') }}</b></div></th></tr>
            </thead>
            <tbody id="copro_emmc_dev"></tbody>
        </table>
        <table class="table table-striped table-condensed">
            <thead>
                <tr>
                    <th>{{ lang._('Partition') }}</th>
                    <th>{{ lang._('Size') }}</th>
                    <th>{{ lang._('What it is') }}</th>
                    <th>{{ lang._('Mounted') }}</th>
                    <th>{{ lang._('In use') }}</th>
                    <th>{{ lang._('What it holds') }}</th>
                </tr>
            </thead>
            <tbody id="copro_emmc"></tbody>
        </table>
        <table class="table table-striped table-condensed">
            <thead>
                <tr><th><div class="xgs-head"><b>{{ lang._('What can be done from here') }}</b></div></th></tr>
            </thead>
            <tbody>
                <tr><td>{{ lang._('Nothing on this tab changes the coprocessor: it reads, and that is all it does. The few things that could be changed are left alone on purpose. Which copy of its system it starts is set in its U-Boot environment, and a mistake there is a coprocessor that does not start, so nothing in this project writes it. Restarting the coprocessor on its own stops the host of an XGS 3300 until the host is restarted too (issue 768). And writing to its flash or its eMMC can leave it unable to start. What can safely be changed has a page of its own: power on the PoE ports on XGS: Ports, a port found by its light on the Link Health page.') }}</td></tr>
            </tbody>
        </table>
        <div id="copro_when" style="padding: 0 10px 10px; opacity: 0.7;"></div>
    </div>

    <div id="details" class="tab-pane table-responsive">
        <table class="table table-striped table-condensed">
            <tbody id="xgs_raw"></tbody>
        </table>
    </div>
</div>

{{ partial("OPNsense/XGS/credit") }}
