#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""The read-only privileges in models/OPNsense/XGS/ACL/ACL.xml, matched as OPNsense matches an address against
a privilege's patterns (models/OPNsense/Core/ACL.php, urlMatch: "*" a wildcard, the address whole): each
reaches the requests its page reads with on OPNsense 26.7 - read from the page's own view there,
2026-11-15 - and none it saves, adds, deletes, toggles, imports or applies with, whether named in snake
case as the pages call them or in camel case as an API client may.

    python3 tools/test-readonly-acl.py"""
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACL = os.path.join(ROOT, 'src', 'opnsense', 'mvc', 'app', 'models', 'OPNsense', 'XGS', 'ACL', 'ACL.xml')


def url_match(url, mask):
    """OPNsense's ACL::urlMatch, line for line."""
    match = mask.replace('.', '\\.').replace('*', '.*').replace('?', '\\?')
    match = re.sub(r'([/&?])\.\*$', r'(\1.*)?', match)
    url = re.sub(r'#.*$', '', url)
    return re.match('^/' + match + '$', url) is not None


# privilege: (reads its page makes, writes it must not reach)
CASES = {
    'page-xgs-ro-firewall-alias': (
        ['/ui/firewall/alias', '/api/firewall/alias/search_item', '/api/firewall/alias/searchItem',
         '/api/firewall/alias/get_item/0e1f', '/api/firewall/alias/get_alias_uuid/lan_net',
         '/api/firewall/alias/get_geo_ip', '/api/firewall/alias/get_table_size', '/api/firewall/alias/list_categories',
         '/api/firewall/alias/list_countries', '/api/firewall/alias/list_network_aliases',
         '/api/firewall/alias/list_user_groups', '/api/firewall/alias/export'],
        ['/api/firewall/alias/add_item', '/api/firewall/alias/addItem', '/api/firewall/alias/del_item/0e1f',
         '/api/firewall/alias/set_item/0e1f', '/api/firewall/alias/setItem/0e1f', '/api/firewall/alias/set',
         '/api/firewall/alias/toggle_item/0e1f', '/api/firewall/alias/import', '/api/firewall/alias/reconfigure',
         '/api/firewall/alias/update', '/api/firewall/alias_util/add/lan', '/api/firewall/alias_util/flush/lan']),
    'page-xgs-ro-firewall-filter': (
        ['/ui/firewall/filter', '/api/firewall/filter/search_rule', '/api/firewall/filter/get_rule/0e1f',
         '/api/firewall/filter/get_interface_list', '/api/firewall/filter/list_categories',
         '/api/firewall/filter/list_network_select_options', '/api/firewall/filter/list_port_select_options',
         '/api/firewall/filter/download_rules', '/api/firewall/filter_util/rule_stats'],
        ['/api/firewall/filter/add_rule', '/api/firewall/filter/del_rule/0e1f', '/api/firewall/filter/set_rule/0e1f',
         '/api/firewall/filter/toggle_rule/0e1f', '/api/firewall/filter/toggle_rule_log/0e1f',
         '/api/firewall/filter/move_rule_before/0e1f/0e2f', '/api/firewall/filter/apply',
         '/api/firewall/filter/upload_rules', '/api/firewall/filter/flush_inspect_cache',
         '/api/firewall/filter/savepoint', '/api/firewall/filter/revert/1']),
    'page-xgs-ro-trust-cert': (
        ['/ui/trust/cert', '/api/trust/cert/search', '/api/trust/cert/get/0e1f', '/api/trust/cert/ca_info/abc',
         '/api/trust/cert/ca_list', '/api/trust/cert/user_list', '/api/trust/cert/raw_dump/0e1f'],
        ['/api/trust/cert/add', '/api/trust/cert/del/0e1f', '/api/trust/cert/set/0e1f',
         '/api/trust/cert/generate_file/0e1f/pem', '/api/trust/ca/add']),
    'page-xgs-ro-ipsec-connections': (
        ['/ui/ipsec/connections', '/api/ipsec/connections/search_connection',
         '/api/ipsec/connections/get_connection/0e1f', '/api/ipsec/connections/search_local',
         '/api/ipsec/connections/get_child/0e1f', '/api/ipsec/connections/is_enabled',
         '/api/ipsec/connections/connection_exists/0e1f', '/api/ipsec/pools/search', '/api/ipsec/pools/get/0e1f'],
        ['/api/ipsec/connections/add_connection', '/api/ipsec/connections/add_child',
         '/api/ipsec/connections/del_connection/0e1f', '/api/ipsec/connections/del_child/0e1f',
         '/api/ipsec/connections/set_connection/0e1f', '/api/ipsec/connections/set_child/0e1f',
         '/api/ipsec/connections/toggle/0', '/api/ipsec/connections/toggle_connection/0e1f',
         '/api/ipsec/connections/toggle_child/0e1f', '/api/ipsec/pools/add', '/api/ipsec/pools/del/0e1f',
         '/api/ipsec/pools/set/0e1f', '/api/ipsec/pools/toggle/0e1f', '/api/ipsec/service/reconfigure']),
    'page-xgs-ro-xgs-settings': (
        ['/ui/xgs/settings', '/api/xgs/settings/get', '/api/xgs/status/get', '/api/xgs/ipsec/get',
         '/api/xgs/settings/recommended'],
        ['/api/xgs/settings/set', '/api/xgs/offload/apply', '/api/xgs/settings/reassign',
         '/api/xgs/settings/apply_recommended', '/api/xgs/poe/set']),
}

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


check('OPNsense\'s matching: a trailing "*" after "/" also matches the address without it',
      url_match('/api/xgs/status', 'api/xgs/status/*'), True)
check('OPNsense\'s matching: "get*" does not reach set_item', url_match('/api/x/y/set_item', 'api/x/y/get*'), False)

root = ET.parse(ACL).getroot()
found = {el.tag: [p.text for p in el.findall('patterns/pattern')] for el in root if el.tag.startswith('page-xgs-ro-')}
check('every read-only privilege is checked here', sorted(found), sorted(CASES))
for key, (reads, writes) in CASES.items():
    patterns = found.get(key, [])
    check('%s: every read its page makes is reached' % key,
          [u for u in reads if not any(url_match(u, p) for p in patterns)], [])
    check('%s: no write is reached' % key,
          [u for u in writes if any(url_match(u, p) for p in patterns)], [])
    name = root.find(key + '/name')
    check('%s: named "Read only: ..."' % key, name is not None and name.text.startswith('Read only: '), True)

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
