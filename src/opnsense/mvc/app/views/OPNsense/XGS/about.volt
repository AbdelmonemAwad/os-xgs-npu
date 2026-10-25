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
 #
 # XGS: About. The same facts the README, the site's Legal and Contact pages and SECURITY.md give,
 # in the GUI, in tabs: nothing here says more than those do.
 #}

<script>
    $(document).ready(function () {
        /* the tab in the address, as the other pages keep it */
        if (window.location.hash !== '') {
            $('a[href="' + window.location.hash + '"]').tab('show');
        }
        $('.nav-tabs a').on('shown.bs.tab', function (e) {
            history.pushState(null, null, e.target.hash);
        });
    });
</script>

<ul class="nav nav-tabs" data-tabs="tabs" id="maintabs">
    <li class="active"><a data-toggle="tab" href="#project">{{ lang._('Project') }}</a></li>
    <li><a data-toggle="tab" href="#developer">{{ lang._('Developer') }}</a></li>
    <li><a data-toggle="tab" href="#links">{{ lang._('Links') }}</a></li>
    <li><a data-toggle="tab" href="#contact">{{ lang._('Contact') }}</a></li>
    <li><a data-toggle="tab" href="#disclaimer">{{ lang._('Disclaimer') }}</a></li>
    <li><a data-toggle="tab" href="#licences">{{ lang._('Licences') }}</a></li>
    <li><a data-toggle="tab" href="#releases">{{ lang._('Releases') }}</a></li>
</ul>

<div class="tab-content content-box">
    <div id="project" class="tab-pane active table-responsive">
        <table class="table table-striped opnsense_standard_table_form">
            <colgroup><col class="col-md-3"/><col class="col-md-9"/></colgroup>
            <tbody>
                <tr>
                    <td><b>os-xgs-npu</b></td>
                    <td>{{ lang._('OPNsense on Sophos XGS appliances. The appliance is two computers in one box: an x86 host, and a coprocessor that owns every front port. This project is the two FreeBSD drivers for that coprocessor - npuep for the ARMADA family (the XGS 136), octep for the OCTEON TX family (the XGS 3300) - and this plugin around them: the ports, connections and IPsec forwarded by the coprocessor itself, the transceivers, the problem reports and the updates.') }}</td>
                </tr>
                <tr>
                    <td>{{ lang._('Status') }}</td>
                    <td>{{ lang._('Alpha. The main part of the work is done; what each appliance still lacks is listed, measured, on the parity page.') }}
                        <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/blob/main/docs/families/parity.md" target="_blank" rel="noopener noreferrer">{{ lang._('Parity') }}</a></td>
                </tr>
                <tr>
                    <td>{{ lang._('Installed') }}</td>
                    <td>{% if release != '' %}<b>{{ release|escape }}</b> {% endif %}{% if commit != '' %}<code>{{ commit|escape }}</code> {% endif %}<span class="text-muted">{{ installedDate|escape }}</span>
                        {% if release == '' and commit == '' %}<span class="text-muted">{{ lang._('not recorded') }}</span>{% endif %}
                        &middot; <a href="/ui/xgs/updates">{{ lang._('XGS: Updates') }}</a></td>
                </tr>
                <tr>
                    <td>{{ lang._('Purpose') }}</td>
                    <td>{{ lang._('An open-source project, run privately and without any commercial purpose: nothing is sold and nothing is advertised. It gives hardware whose vendor support has ended a second life as a firewall.') }}</td>
                </tr>
            </tbody>
        </table>
    </div>

    <div id="developer" class="tab-pane table-responsive">
        <table class="table table-striped opnsense_standard_table_form">
            <colgroup><col class="col-md-3"/><col class="col-md-9"/></colgroup>
            <tbody>
                <tr>
                    <td>{{ lang._('Author') }}</td>
                    <td><b>Abdelmonem Awad</b> &middot;
                        <a href="https://github.com/AbdelmonemAwad" target="_blank" rel="noopener noreferrer">github.com/AbdelmonemAwad</a></td>
                </tr>
                <tr>
                    <td>{{ lang._('How it is worked on') }}</td>
                    <td>{{ lang._('One person, in the evenings: an answer takes days, not hours. Every measurement on the project pages was taken on the appliances of the lab of the author, and is photographed.') }}</td>
                </tr>
                <tr>
                    <td>{{ lang._('By the same author') }}</td>
                    <td>{{ lang._('The appliance plugins this one works with: the Arabic translation and right-to-left layout of OPNsense, fan control, the front panel, link health, the network report and the XGS theme - each in its own repository, under Links.') }}</td>
                </tr>
            </tbody>
        </table>
    </div>

    <div id="links" class="tab-pane table-responsive">
        <table class="table table-striped table-condensed">
            <tbody>
                <tr><td style="width: 30%;">{{ lang._('The site') }}</td><td><a href="https://npuforge.org" target="_blank" rel="noopener noreferrer">npuforge.org</a></td></tr>
                <tr><td>{{ lang._('The source') }}</td><td><a href="https://github.com/AbdelmonemAwad/os-xgs-npu" target="_blank" rel="noopener noreferrer">github.com/AbdelmonemAwad/os-xgs-npu</a></td></tr>
                <tr><td>{{ lang._('Releases') }}</td><td><a href="https://github.com/AbdelmonemAwad/os-xgs-npu/releases" target="_blank" rel="noopener noreferrer">github.com/AbdelmonemAwad/os-xgs-npu/releases</a></td></tr>
                <tr><td>{{ lang._('What changed') }}</td><td><a href="https://github.com/AbdelmonemAwad/os-xgs-npu/blob/main/CHANGELOG.md" target="_blank" rel="noopener noreferrer">CHANGELOG.md</a></td></tr>
                <tr><td>{{ lang._('Documentation') }}</td><td><a href="https://npuforge.org/docs/" target="_blank" rel="noopener noreferrer">npuforge.org/docs</a> &middot; <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/wiki" target="_blank" rel="noopener noreferrer">{{ lang._('the wiki') }}</a></td></tr>
                <tr><td>{{ lang._('An installation image') }}</td><td><a href="https://build.npuforge.org/" target="_blank" rel="noopener noreferrer">build.npuforge.org</a></td></tr>
                <tr><td>{{ lang._('Issues') }}</td><td><a href="https://github.com/AbdelmonemAwad/os-xgs-npu/issues" target="_blank" rel="noopener noreferrer">github.com/AbdelmonemAwad/os-xgs-npu/issues</a></td></tr>
                <tr><td>{{ lang._('The plugins') }}</td><td>
                    <a href="https://github.com/AbdelmonemAwad/opnsense-arabic" target="_blank" rel="noopener noreferrer">opnsense-arabic</a> &middot;
                    <a href="https://github.com/AbdelmonemAwad/opnsense-rtl" target="_blank" rel="noopener noreferrer">opnsense-rtl</a> &middot;
                    <a href="https://github.com/AbdelmonemAwad/os-fanctl" target="_blank" rel="noopener noreferrer">os-fanctl</a> &middot;
                    <a href="https://github.com/AbdelmonemAwad/os-frontpanel" target="_blank" rel="noopener noreferrer">os-frontpanel</a> &middot;
                    <a href="https://github.com/AbdelmonemAwad/os-linkhealth" target="_blank" rel="noopener noreferrer">os-linkhealth</a> &middot;
                    <a href="https://github.com/AbdelmonemAwad/os-netreport" target="_blank" rel="noopener noreferrer">os-netreport</a> &middot;
                    <a href="https://github.com/AbdelmonemAwad/os-watchdog" target="_blank" rel="noopener noreferrer">os-watchdog</a> &middot;
                    <a href="https://github.com/AbdelmonemAwad/os-kernelfollow" target="_blank" rel="noopener noreferrer">os-kernelfollow</a> &middot;
                    <a href="https://github.com/AbdelmonemAwad/os-blocklists" target="_blank" rel="noopener noreferrer">os-blocklists</a> &middot;
                    <a href="https://github.com/AbdelmonemAwad/os-portguard" target="_blank" rel="noopener noreferrer">os-portguard</a> &middot;
                    <a href="https://github.com/AbdelmonemAwad/os-fallbackcrypto" target="_blank" rel="noopener noreferrer">os-fallbackcrypto</a> &middot;
                    <a href="https://github.com/AbdelmonemAwad/os-theme-xgs" target="_blank" rel="noopener noreferrer">os-theme-xgs</a></td></tr>
            </tbody>
        </table>
    </div>

    <div id="contact" class="tab-pane table-responsive">
        <table class="table table-striped table-condensed">
            <tbody>
                <tr>
                    <td style="width: 30%;">{{ lang._('A question, an idea, an appliance not listed') }}</td>
                    <td><a href="mailto:contact@npuforge.org">contact@npuforge.org</a> &middot;
                        <a href="https://npuforge.org/contact/" target="_blank" rel="noopener noreferrer">{{ lang._('the form on the site') }}</a></td>
                </tr>
                <tr>
                    <td>{{ lang._('Something wrong on this appliance') }}</td>
                    <td><a href="/ui/xgs/reports">{{ lang._('XGS: Problem Reports') }}</a>{{ lang._(', which sends what is needed and nothing private - or an issue on GitHub.') }}</td>
                </tr>
                <tr>
                    <td>{{ lang._('A security problem') }}</td>
                    <td>{{ lang._('Privately, never in an issue:') }}
                        <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/security/advisories/new" target="_blank" rel="noopener noreferrer">{{ lang._('report a vulnerability on GitHub') }}</a>,
                        {{ lang._('or without a GitHub account') }} <a href="mailto:security@npuforge.org">security@npuforge.org</a></td>
                </tr>
                <tr>
                    <td>{{ lang._('A rights holder') }}</td>
                    <td><a href="mailto:contact@npuforge.org">contact@npuforge.org</a>{{ lang._(': it is looked at promptly, and corrected or taken down if it should be.') }}</td>
                </tr>
            </tbody>
        </table>
    </div>

    <div id="disclaimer" class="tab-pane" style="padding: 15px;">
        <p><b>{{ lang._('No warranty.') }}</b> {{ lang._('The software is provided as is, without a warranty of any kind, as its licence says in full. Installing another operating system on an appliance replaces the one of its vendor, may end the support of the vendor for it, and can leave it unable to start until it is recovered. No one else is answerable for what it does to your hardware, your network or your data: if you load it, that decision and its consequences are yours. Try a release on an appliance you can reach before you trust it with a network you depend on.') }}</p>
        <p>{{ lang._('The measurements were taken on the appliances named on each page, in the conditions each page describes. They say what happened there, not what any other appliance or network will do.') }}</p>
        <p><b>{{ lang._('Independent.') }}</b> {{ lang._('This project is not affiliated with, endorsed by or sponsored by Deciso B.V., Sophos Ltd. or Marvell, and their names are used only to say what this software runs on and works with. OPNsense is a registered trademark of Deciso B.V.; Sophos and XGS are trademarks of Sophos Ltd.; Marvell, ARMADA, OCTEON and Cavium are trademarks of Marvell. Every other name belongs to whoever owns it.') }}</p>
        <p>{{ lang._('No Sophos software or firmware is distributed here. The drivers are written by this project; where code of a vendor was read to learn how the hardware behaves, or a fact was read from software on the appliance, the provenance pages say what was read and why.') }}
            <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/blob/main/docs/provenance.md" target="_blank" rel="noopener noreferrer">{{ lang._('Provenance') }}</a></p>
    </div>

    <div id="licences" class="tab-pane table-responsive">
        <table class="table table-striped table-condensed">
            <tbody>
                <tr>
                    <td style="width: 30%;">{{ lang._('The software and its documentation') }}</td>
                    <td><a href="https://github.com/AbdelmonemAwad/os-xgs-npu/blob/main/LICENSE" target="_blank" rel="noopener noreferrer">BSD-2-Clause</a> &middot; Copyright &copy; 2026 Abdelmonem Awad</td>
                </tr>
                <tr>
                    <td>{{ lang._('The photographs and screenshots') }}</td>
                    <td><a href="https://creativecommons.org/licenses/by/4.0/" target="_blank" rel="noopener noreferrer">CC BY 4.0</a>{{ lang._(': use them freely, naming the author and linking to the project.') }}</td>
                </tr>
                <tr>
                    <td>{{ lang._('Each plugin') }}</td>
                    <td>{{ lang._('Under the licence of its own repository.') }}</td>
                </tr>
                <tr>
                    <td>OPNsense</td>
                    <td>{{ lang._('OPNsense belongs to Deciso B.V. and is under its own licence; its footer below is left as it is.') }}</td>
                </tr>
            </tbody>
        </table>
    </div>

    <div id="releases" class="tab-pane table-responsive">
        {% if releases|length == 0 %}
        <table class="table table-striped table-condensed"><tbody><tr><td class="text-muted">
            {{ lang._('This install keeps no list of the releases; the releases page on GitHub has them all.') }}
            <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/releases" target="_blank" rel="noopener noreferrer">github.com/AbdelmonemAwad/os-xgs-npu/releases</a>
        </td></tr></tbody></table>
        {% endif %}
        {% for r in releases %}
        <table class="table table-condensed" style="margin-bottom: 18px;">
            <thead>
                <tr>
                    <th>
                        <i class="fa fa-tag fa-fw text-muted"></i>
                        <a href="https://github.com/AbdelmonemAwad/os-xgs-npu/releases/tag/v{{ r['version']|escape }}" target="_blank" rel="noopener noreferrer">{{ r['version']|escape }}</a>
                        <small class="text-muted">{{ r['date']|escape }}</small>
                        {% if r['version'] == installedVersion %}<span class="label label-success">{{ lang._('installed here') }}</span>{% endif %}
                    </th>
                </tr>
            </thead>
            <tbody>
                {% for p in r['summary'] %}
                <tr><td>{{ p|escape }}</td></tr>
                {% endfor %}
                {% for s in r['sections'] %}
                <tr>
                    <td>
                        <b>{% if s['name'] == 'Fixes' %}{{ lang._('Fixes') }}{% elseif s['name'] == 'Improvements and additions' %}{{ lang._('Improvements and additions') }}{% elseif s['name'] == 'Documentation' %}{{ lang._('Documentation') }}{% else %}{{ s['name']|escape }}{% endif %}</b>
                        <ul style="margin: 6px 0 0 0;">
                            {% for i in s['items'] %}
                            <li>{{ i['text']|escape }}{% if i['pr'] != '' %} <a href="{{ i['url']|escape }}" target="_blank" rel="noopener noreferrer">#{{ i['pr']|escape }}</a>{% endif %}</li>
                            {% endfor %}
                        </ul>
                    </td>
                </tr>
                {% endfor %}
            </tbody>
        </table>
        {% endfor %}
    </div>
</div>

{{ partial("OPNsense/XGS/credit") }}
