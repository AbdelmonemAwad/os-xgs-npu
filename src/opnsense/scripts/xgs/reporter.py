#!/usr/local/bin/python3
#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
Problem reports: what went wrong on this appliance, found in its own logs and state, cleaned of
anything that identifies the machine or its networks, and filed as a GitHub issue in the repository
of the part it belongs to - through a relay, so that nobody needs a GitHub account to report one.

    reporter.py collect          find new problems and queue them; send them if sending is automatic
    reporter.py boot             at boot: whether the last boot ended cleanly, then collect
    reporter.py list             the queue as JSON, for the page
    reporter.py show <id>        exactly what is sent for one report, as JSON
    reporter.py send <id>        send one report now, whatever the mode (the page's Send)
    reporter.py dismiss <id>     forget one report; it is not sent, and comes back only if it recurs
    reporter.py manual <b64>     a report written by the owner: base64 of JSON {title, description, part}
    reporter.py acknowledge      the owner has seen how reporting works; the notice goes away
    reporter.py system           the appliance block every report carries, as JSON

WHAT IS LOOKED AT, and nothing else (docs/problem-reports.md lists it for the owner):
  - the system log, this plugin's log and configd's: a line of severity error or worse from one of
    the parts' own programs, a kernel line from the coprocessor's drivers that says something failed,
    and a configd action of one of the parts that failed;
  - PHP's error log, for a line in one of the parts' pages;
  - a kernel crash dump in /var/crash (read, never removed: OPNsense's own crash reporter owns it);
  - at boot, whether the boot before ended in a shutdown at all, and what the host's chipset
    recorded as the cause of the reset (scripts/xgs/reset_reason.py);
  - the coprocessor's driver: attached on a board that has one, and ready ten minutes after boot;
  - the driver module's kernel stamp against the running kernel;
  - the last update of this plugin, and a fan incident of os-fanctl.
Each part is looked at only when it is installed, and each one's reports go to its own repository.

WHAT LEAVES THE MACHINE is the text the page shows under View, and only that: built here, cleaned
here, posted as it is. Addresses (IPv4, IPv6, MAC), e-mail addresses, the SMBIOS serials and UUID,
the host and domain names, key material and fingerprints are replaced before anything is stored.
The appliance is named by a hash that cannot be turned back into its serial.

THE MODE is the owner's: automatic (the default), manual (reports wait for Send) or off (nothing is
looked at and nothing is sent). It is read from the settings, never assumed.
"""

import base64
import datetime
import glob
import hashlib
import ipaddress
import json
import os
import re
import subprocess
import sys
import tarfile
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET

try:
    import fcntl
except ImportError:		# only away from the appliance, in the tests
    fcntl = None

STATE = os.environ.get('XGS_REPORT_STATE', '/var/db/os-xgs-npu/reports')
QUEUE = os.path.join(STATE, 'queue')
CONFIG = os.environ.get('XGS_REPORT_CONFIG', '/conf/config.xml')
ROOT = os.environ.get('XGS_REPORT_ROOT', '')		# a prefix for every file read, for the tests
RELAY = 'https://reports.npuforge.org/v1/report'
RELAY_OVERRIDE = '/usr/local/etc/os-xgs-npu/report-relay'
OWNER = 'AbdelmonemAwad'
SCHEMA = 1

NEW_PER_DAY = 5			# new issues a day from one appliance, at most
UPDATES_PER_DAY = 20		# "seen again" notes a day, at most
RESEND_AFTER = 86400		# a report that recurs is mentioned again at most once a day
KEEP = 200			# reports kept in the queue
SCAN_LIMIT = 4 * 1024 * 1024	# bytes of one log read in one pass
EVIDENCE_LINES = 30
EVIDENCE_WIDTH = 400
READY_AFTER = 600		# seconds after boot by which the driver must be ready
INSTALL_LOG = '/var/db/os-xgs-npu/install-build.log'	# the installer's build of the module (install.sh)
BUILD_LOG_LINES = 12	# of it, the last lines a report of a driver that is not attached carries

# Each part, how to tell it is installed, the programs it logs under, the kernel devices it owns,
# and the paths that name it in PHP's error log and in configd's.
PARTS = [
    {
        'name': 'os-xgs-npu',
        'present': '/usr/local/opnsense/scripts/xgs',
        'tags': ('octep', 'octep-follow', 'octep-stop', 'npuep', 'npuctl', 'xgs-assign', 'xgs-switch'),
        'kernel': re.compile(r'^(?:octep|npuep)\d+:|^(?:oxp|npup)\d+:|^mvmgmt\d+:'),
        'paths': ('/OPNsense/XGS/', '/scripts/xgs/', '/scripts/octep/', '/scripts/npuctl/'),
    },
    {
        'name': 'os-fanctl',
        'present': '/usr/local/opnsense/scripts/fanctl',
        'tags': ('fanctl',),
        'kernel': None,
        'paths': ('/OPNsense/FanCtl/', '/scripts/fanctl/'),
    },
    {
        'name': 'os-frontpanel',
        'present': '/usr/local/opnsense/scripts/frontpanel',
        'tags': ('frontpanel', 'LCDd'),
        'kernel': None,
        'paths': ('/OPNsense/FrontPanel/', '/scripts/frontpanel/'),
    },
    {
        'name': 'os-linkhealth',
        'present': '/usr/local/opnsense/scripts/linkhealth',
        'tags': ('linkhealth',),
        'kernel': None,
        'paths': ('/OPNsense/LinkHealth/', '/scripts/linkhealth/'),
    },
    {
        'name': 'os-netreport',
        'present': '/usr/local/opnsense/scripts/netreport',
        'tags': ('netreport',),
        'kernel': None,
        'paths': ('/OPNsense/NetReport/', '/scripts/netreport/'),
    },
]

# A driver line is a problem when it says something failed. device_printf carries no severity, so
# the words are what there is; these are the ones the two drivers use when something did not work.
# Not "not ready": octep prints "NPU not ready yet" while it waits for the coprocessor at every boot
# (twelve times in eight days of the XGS 3300's log, every one a normal boot), and the real failure
# says "refusing to start". A driver still not ready ten minutes after boot is from_driver()'s.
DRIVER_TROUBLE = re.compile(
    r'\b(?:failed|failure|timed out|refusing|refused|did not complete|giving up|'
    r'stuck|lost|panic|error -?\d+)\b', re.IGNORECASE)
# A tally is not a failure. npuep prints "nwa: 44332 commands, 0 refused, 0 timed out" when it is
# unloaded, and two of its words are DRIVER_TROUBLE's - an unload on 2026-10-20 was reported as one
# (#599). A count of none is taken out of the line before the words are looked for.
NONE_COUNTED = re.compile(
    r'\b(?:0|no|none) (?:failed|failures?|timed out|refused|lost|stuck)\b', re.IGNORECASE)

SYSLOG = re.compile(r'^<(\d{1,3})>1 (\S+) \S+ (\S+) \S+ \S+ (?:\[[^\]]*\] )?(.*)$')
KERNEL_TEXT = re.compile(r'^(?:<(\d{1,3})>)?(?:\[\d+\] )?(.*)$')
PHP_LINE = re.compile(r'^\[([^\]]+)\] (PHP [A-Za-z ]+?):\s+(.*)$')


# ------------------------------------------------------------------------------------- small pieces

def path(p):
    return ROOT + p if ROOT else p


def run(argv, timeout=10):
    """A command's output, or '' when it is not there or fails: every source here is optional."""
    try:
        out = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.SubprocessError):
        return ''
    return out.stdout if out.returncode == 0 else ''


def succeeds(argv, timeout=30):
    """Whether a command exits 0 - savecore -C says whether a dump is waiting by its status alone."""
    try:
        return subprocess.run(argv, capture_output=True, timeout=timeout).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def sysctl(name):
    return run(['/sbin/sysctl', '-n', name]).strip()


def read(p, limit=None):
    try:
        with open(path(p), 'r', encoding='utf-8', errors='replace') as handle:
            return handle.read(limit) if limit else handle.read()
    except OSError:
        return ''


def load(name, default):
    try:
        with open(os.path.join(STATE, name), 'r', encoding='utf-8') as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return default


def save(name, value):
    os.makedirs(STATE, exist_ok=True)
    target = os.path.join(STATE, name)
    with open(target + '.new', 'w', encoding='utf-8') as handle:
        json.dump(value, handle, indent=1, sort_keys=True)
    os.replace(target + '.new', target)


def today():
    return time.strftime('%Y-%m-%d', time.gmtime())


def iso(ts):
    return time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime(ts)) if ts else ''


def boot_time():
    raw = sysctl('kern.boottime')
    match = re.search(r'sec = (\d+)', raw)
    return int(match.group(1)) if match else 0


# -------------------------------------------------------------------------------------- the settings

def settings():
    """The owner's choices from config.xml; automatic when the plugin has never been saved."""
    out = {'mode': 'automatic', 'email': ''}
    try:
        node = ET.parse(path(CONFIG)).getroot().find('OPNsense/XGS/reporting')
    except (OSError, ET.ParseError):
        node = None
    if node is not None:
        mode = (node.findtext('mode') or '').strip()
        if mode in ('automatic', 'manual', 'off'):
            out['mode'] = mode
        out['email'] = (node.findtext('email') or '').strip()
    return out


def relay_url():
    override = read(RELAY_OVERRIDE).strip()
    return override if override.startswith('https://') else RELAY


# ------------------------------------------------------------------------------------- the cleaning

KEY_BLOCK = re.compile(r'-----BEGIN [A-Z ]+-----.*?-----END [A-Z ]+-----', re.DOTALL)
EMAIL = re.compile(r'\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b')
MAC = re.compile(r'(?<![0-9A-Fa-f:])(?:[0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}(?![0-9A-Fa-f:])')
IPV4 = re.compile(r'(?<![\d.])(?:\d{1,3}\.){3}\d{1,3}(?:/\d{1,2})?(?![\d.])')
IPV6 = re.compile(r'(?<![\w:.])(?:[0-9A-Fa-f]{0,4}:){2,7}[0-9A-Fa-f]{0,4}(?:%[\w.]+)?(?:/\d{1,3})?(?![\w:])')
# A key's fingerprint as the boot banner prints it - "SHA256" and sixteen hex pairs for the GUI's
# certificate, "SHA256" and base64 for an SSH host key - and the second line of pairs it runs onto.
FINGERPRINT = re.compile(r'\b(SHA256|SHA1|MD5)[: ]+(?:(?:[0-9A-Fa-f]{2}[ :]?){8,}|[A-Za-z0-9+/=]{16,})')
HEX_PAIRS = re.compile(r'\b(?:[0-9A-Fa-f]{2}[ :]){7,}[0-9A-Fa-f]{2}\b')
LONG_HEX = re.compile(r'\b[0-9A-Fa-f]{32,}\b')


def kept_address(text):
    """Loopback, unspecified and link-local addresses say nothing about anybody's network."""
    try:
        addr = ipaddress.ip_address(text.split('%')[0].split('/')[0])
    except ValueError:
        return None
    return addr.is_loopback or addr.is_unspecified or addr.is_link_local


class Scrubber:
    """Everything that names this machine or its networks, taken out of a piece of text."""

    def __init__(self):
        words = []
        for key in ('smbios.system.serial', 'smbios.system.uuid', 'smbios.planar.serial',
                    'smbios.chassis.serial', 'smbios.memory.serial'):
            value = run(['/bin/kenv', '-q', key]).strip()
            if len(value) >= 4 and value.lower() not in ('default string', 'none', 'unknown'):
                words.append(value)
        for line in read('/var/db/os-xgs-npu/npu-platform.txt').splitlines():
            key, _, value = line.partition('=')
            if key.endswith('.serial') and len(value.strip()) >= 4:
                words.append(value.strip())
        host = domain = ''
        try:
            system = ET.parse(path(CONFIG)).getroot().find('system')
            host = (system.findtext('hostname') or '').strip()
            domain = (system.findtext('domain') or '').strip()
        except (OSError, ET.ParseError, AttributeError):
            pass
        if host and domain:
            words.append('%s.%s' % (host, domain))
        if '.' in domain:
            words.append(domain)
        # A hostname left at a common word is not a name, and replacing it would eat the text.
        if len(host) >= 4 and host.lower() not in ('opnsense', 'firewall', 'localhost', 'router', 'gateway'):
            words.append(host)
        self.words = sorted(set(words), key=len, reverse=True)

    def __call__(self, text):
        if not text:
            return text
        text = str(text)
        # Whole things first: an address whose domain went first would leave its local part behind.
        text = KEY_BLOCK.sub('<key withheld>', text)
        text = EMAIL.sub('<e-mail>', text)
        for word in self.words:
            text = re.sub(re.escape(word), '<withheld>', text, flags=re.IGNORECASE)
        text = FINGERPRINT.sub(lambda m: m.group(1) + ' <withheld>', text)
        text = MAC.sub('<mac>', text)
        text = HEX_PAIRS.sub('<withheld>', text)
        text = IPV6.sub(lambda m: address(m, '<ipv6>'), text)
        text = IPV4.sub(lambda m: address(m, '<ipv4>'), text)
        text = LONG_HEX.sub('<hex>', text)
        return text


def address(match, label):
    """An address replaced by its label; something that only looks like one, or names nobody, kept."""
    kept = kept_address(match.group(0))
    return match.group(0) if kept is None or kept else label


def normalise(text):
    """What stays the same when the same problem happens again: no numbers, addresses or names."""
    text = str(text).lower()
    # A request id, as configd puts in every line: its four-digit groups are not caught below.
    text = re.sub(r'\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b', '#', text)
    text = re.sub(r'<[a-z -]+>', '#', text)
    text = re.sub(r'0x[0-9a-f]+', '#', text)
    text = re.sub(r'\b[0-9a-f]{8,}\b', '#', text)
    text = re.sub(r'\d+', '#', text)
    text = re.sub(r"'[^']*'|\"[^\"]*\"", '#', text)
    return ' '.join(text.split())


def fingerprint(part, kind, signature):
    raw = '%s|%s|%s' % (part, kind, normalise(signature))
    return hashlib.sha256(raw.encode('utf-8')).hexdigest()[:16]


# --------------------------------------------------------------------------------- the appliance

def appliance_id():
    """A name for this machine that cannot be turned back into its serial."""
    parts = [run(['/bin/kenv', '-q', key]).strip()
             for key in ('smbios.planar.serial', 'smbios.system.uuid')]
    raw = 'xgs-report|' + '|'.join(parts)
    return hashlib.sha256(raw.encode('utf-8')).hexdigest()[:12]


def installed(name):
    try:
        record = json.loads(read('/usr/local/share/%s/installed.json' % name) or '{}')
    except ValueError:
        record = {}
    if not record:
        return None
    return {
        'commit': str(record.get('commit', ''))[:12],
        'date': str(record.get('date', '')),
        'source': str(record.get('source', '')),
    }


def modified_files():
    """The plugin's own files that differ from what its installer wrote, by name only."""
    manifest = read('/usr/local/share/os-xgs-npu/manifest.sha256')
    if not manifest:
        return None
    changed = []
    for line in manifest.splitlines():
        # sha256 -r: the digest, a space, the path
        digest, _, name = line.strip().partition(' ')
        name = name.strip()
        if not name:
            continue
        try:
            with open(path(name), 'rb') as handle:
                now = hashlib.sha256(handle.read()).hexdigest()
        except OSError:
            changed.append(name + ' (missing)')
            continue
        if now != digest:
            changed.append(name)
    return changed


def module_stamp():
    """Whether the coprocessor's driver was built for the kernel that is running."""
    running = sysctl('kern.version').splitlines()
    running = running[0].strip() if running else ''
    for module in ('octep', 'npuep'):
        stamp = read('/boot/modules/%s.ko.kernel' % module).strip().splitlines()
        if stamp:
            return {'module': module, 'built_for': stamp[0].strip(), 'running': running,
                    'matches': stamp[0].strip() == running}
    return None


def kenv(key):
    value = run(['/bin/kenv', '-q', key]).strip()
    return '' if value.lower() == 'default string' else ' '.join(value.split())


PCI_LINE = re.compile(r'^([a-z]+)(\d+)@pci\S+\s+class=(0x[0-9a-f]+) rev=0x([0-9a-f]+) \S+ '
                      r'vendor=0x([0-9a-f]+) device=0x([0-9a-f]+)')


def pci_devices():
    """
    The host's network controllers and the coprocessor, as PCI reports them: the device, its ids,
    its revision and the name it gives itself. Two XGS 3300 boards differ here - the management
    port is an I210 on one and an I211 on the other - and a plugin that knew only one missed the
    other, so a report says which this one has.
    """
    out = []
    current = None
    for line in run(['/usr/sbin/pciconf', '-lv']).splitlines():
        match = PCI_LINE.match(line)
        if match:
            driver, unit, klass, rev, vendor, device = match.groups()
            wanted = klass.startswith('0x02') or driver in ('octep', 'npuep') or vendor in ('177d', '11ab')
            # The coprocessors by their ids first: the ARMADA's (11ab:7080) says it is a network
            # controller, and reports listed it as one, with "none seen" for the coprocessor.
            coprocessor = (vendor, device) in (('11ab', '7080'), ('177d', 'a300')) or driver in ('octep', 'npuep')
            current = {'device': driver + unit,
                       'class': 'coprocessor' if coprocessor or not klass.startswith('0x02') else 'network',
                       'id': '%s:%s' % (vendor, device), 'revision': rev, 'model': ''} if wanted else None
            if current is not None:
                out.append(current)
            continue
        named = re.match(r"^\s+device\s+=\s+'(.*)'", line)
        if current is not None and named:
            current['model'] = named.group(1).strip()
    return out


def system_block():
    """What every report says about the machine it came from. Names and versions; no serials."""
    board = run(['/bin/sh', '/usr/local/opnsense/scripts/xgs/board.sh']).split()
    platform = {}
    for line in read('/var/db/os-xgs-npu/npu-platform.txt').splitlines():
        key, _, value = line.partition('=')
        # Names, revisions and vendors only: the serial and the MACs in the same file are never read.
        if key in ('npu0.device0', 'npu0.device1', 'npu0.assembly.partno', 'npu0.assembly.revision',
                   'npu0.assembly.vendor') or re.match(r'^npu0\.slot\w+\.assembly\.(vendor|partno|revision)$', key):
            platform[key] = value.strip()
    expansion = []
    try:
        ports = json.loads(run(['/usr/local/opnsense/scripts/xgs/portmap.py'], timeout=30) or '{}')
    except ValueError:
        ports = {}
    for bay, slot in sorted(((ports.get('module') or {}).get('slots') or {}).items()):
        expansion.append({'bay': bay, 'part': slot.get('assembly.partno', ''),
                          'revision': slot.get('assembly.revision', ''),
                          'vendor': slot.get('assembly.vendor') or platform.get('npu0.slot%s.assembly.vendor' % bay, '')})
    # The board's vendor is the first three letters of its SMBIOS serial (LAA, NEA - the two XGS 3300
    # here differ), and it is also in the coprocessor's record; the rest of the serial is never kept.
    serial_vendor = re.match(r'^([A-Z]{3})', kenv('smbios.planar.serial'))
    driver = ''
    for node in ('dev.octep.0', 'dev.npuep.0'):
        desc = sysctl(node + '.%desc')
        if desc:
            driver = node.split('.')[1]
            break
    try:
        memory = '%d GB' % round(int(kenv('smbios.memory.enabled')) / 1048576)
    except ValueError:
        memory = ''
    version = sysctl('kern.version').splitlines()
    build = re.search(r'-n\d+-([0-9a-f]{12})', version[0]) if version else None
    plugins = {}
    for part in PARTS[1:]:
        if os.path.isdir(path(part['present'])):
            plugins[part['name']] = installed(part['name']) or {'commit': 'unknown'}
    boot = boot_time()
    return {
        'model': ' '.join(filter(None, [kenv('smbios.system.maker'), kenv('smbios.system.product'),
                                        kenv('smbios.system.version')])),
        'board': {
            'assembly': platform.get('npu0.assembly.partno') or (board[0] if board else ''),
            'revision': platform.get('npu0.assembly.revision') or kenv('smbios.planar.version'),
            'smbios_revision': kenv('smbios.planar.version'),
            'vendor': platform.get('npu0.assembly.vendor') or (serial_vendor.group(1) if serial_vendor else ''),
            'family': board[1] if len(board) > 1 else '',
            'research_and_development': len(board) > 2 and board[2] == 'rnd',
        },
        'pci': pci_devices(),
        'bios': ' '.join(filter(None, [kenv('smbios.bios.version'), kenv('smbios.bios.reldate')])),
        'processor': kenv('smbios.processor.version'),
        'memory': memory,
        'coprocessor': {
            'device': platform.get('npu0.device0', ''),
            'switch': platform.get('npu0.device1', ''),
            'driver': driver,
            'description': sysctl('dev.%s.0.%%desc' % driver) if driver else '',
            'barmap': sysctl('dev.octep.0.barmap_version') if driver == 'octep' else '',
            'ready': sysctl('dev.octep.0.ready') if driver == 'octep' else ('1' if driver else ''),
        },
        'expansion': expansion,
        'opnsense': run(['/usr/local/sbin/opnsense-version', '-v']).strip(),
        'kernel': ' '.join(filter(None, [run(['/bin/freebsd-version', '-k']).strip(),
                                         build.group(1) if build else ''])),
        'module': module_stamp(),
        'os-xgs-npu': installed('os-xgs-npu'),
        'plugins': plugins,
        'modified': modified_files(),
        'uptime': int(time.time()) - boot if boot else None,
    }


def system_table(system):
    def ago(seconds):
        if seconds is None:
            return ''
        hours, rest = divmod(int(seconds), 3600)
        return '%d h %d m' % (hours, rest // 60)
    board = system.get('board') or {}
    cop = system.get('coprocessor') or {}
    module = system.get('module') or {}
    own = system.get('os-xgs-npu') or {}
    def pci(kind):
        return '; '.join('%s: %s (%s rev %s)' % (d['device'], d['model'] or 'unnamed', d['id'], d['revision'])
                         for d in system.get('pci') or [] if d['class'] == kind) or 'none seen'
    revision = board.get('revision', '')
    if board.get('smbios_revision') and board['smbios_revision'] != revision:
        revision += ' (SMBIOS says %s)' % board['smbios_revision']
    rows = [
        ('Model', system.get('model', '')),
        ('Board', ', '.join(filter(None, [board.get('assembly', ''),
                                          ('vendor ' + board['vendor']) if board.get('vendor') else '',
                                          board.get('family', '')]))),
        # On a line of its own: two XGS 3300 boards here are revision 17 and 09, and a plugin that
        # matched the first by its revision did not know the second.
        ('Board revision', revision or 'not read'),
        ('Processor', ', '.join(filter(None, [system.get('processor', ''), system.get('memory', '')]))),
        ('BIOS', system.get('bios', '')),
        ('Network controllers', pci('network')),
        ('Coprocessor', ', '.join(filter(None, [cop.get('device', ''),
                                                ('switch ' + cop['switch']) if cop.get('switch') else '',
                                                cop.get('description', ''),
                                                ('barmap ' + cop['barmap']) if cop.get('barmap') else '',
                                                ('ready ' + cop['ready']) if cop.get('ready') else '']))),
        ('Coprocessor on PCI', pci('coprocessor')),
        ('Expansion', ', '.join('bay %s: %s rev %s%s' % (e['bay'], e['part'], e['revision'],
                                                         (', vendor ' + e['vendor']) if e.get('vendor') else '')
                                for e in system.get('expansion') or []) or 'none'),
        ('OPNsense', system.get('opnsense', '')),
        ('Kernel', system.get('kernel', '')),
        ('Driver module', ('%s.ko, built for the running kernel: %s' % (
            module['module'], 'yes' if module.get('matches') else 'NO')) if module else 'not installed'),
        ('os-xgs-npu', ' '.join(filter(None, [own.get('commit', ''), own.get('date', ''),
                                              ('(%s)' % own['source']) if own.get('source') else ''])) or 'unknown'),
        ('Plugins', ', '.join('%s %s' % (name, (rec or {}).get('commit', ''))
                              for name, rec in sorted((system.get('plugins') or {}).items())) or 'none'),
        ('Modified files', 'not recorded' if system.get('modified') is None else
         (', '.join(system['modified']) or 'none')),
        ('Uptime', ago(system.get('uptime'))),
    ]
    lines = ['| | |', '|---|---|']
    for key, value in rows:
        lines.append('| %s | %s |' % (key, str(value).replace('|', '/')))
    return '\n'.join(lines)


# --------------------------------------------------------------------------------- the queue

def lock():
    os.makedirs(QUEUE, exist_ok=True)
    handle = open(os.path.join(STATE, 'lock'), 'w')
    if fcntl is not None:
        fcntl.flock(handle, fcntl.LOCK_EX)
    return handle


def entries():
    out = {}
    for name in glob.glob(os.path.join(QUEUE, '*.json')):
        try:
            with open(name, 'r', encoding='utf-8') as handle:
                entry = json.load(handle)
            out[entry['id']] = entry
        except (OSError, ValueError, KeyError):
            continue
    return out


def store(entry):
    os.makedirs(QUEUE, exist_ok=True)
    target = os.path.join(QUEUE, entry['id'] + '.json')
    with open(target + '.new', 'w', encoding='utf-8') as handle:
        json.dump(entry, handle, indent=1, sort_keys=True)
    os.replace(target + '.new', target)


def remember(found, scrub, problem):
    """A problem found, added to the queue or counted on the report it is a repeat of."""
    part, kind = problem['part'], problem['kind']
    ident = fingerprint(part, kind, problem['signature'])
    now = int(time.time())
    evidence = [scrub(line)[:EVIDENCE_WIDTH] for line in problem.get('evidence', [])]
    entry = found.get(ident)
    if entry is None:
        entry = {
            'id': ident, 'part': part, 'kind': kind,
            'title': scrub(problem['title'])[:150],
            'summary': scrub(problem.get('summary', '')),
            'evidence': evidence[-EVIDENCE_LINES:],
            'first_seen': problem.get('at') or now, 'last_seen': problem.get('at') or now,
            'count': 1, 'status': 'new', 'issue': None, 'sent_at': 0, 'sent_count': 0, 'error': '',
        }
    else:
        entry['count'] += 1
        entry['last_seen'] = max(entry['last_seen'], problem.get('at') or now)
        entry['evidence'] = (entry['evidence'] + [line for line in evidence
                                                  if line not in entry['evidence']])[-EVIDENCE_LINES:]
        if entry['status'] == 'dismissed':
            entry['status'] = 'new'		# it came back: a dismissal is for what was seen then
    found[ident] = entry
    store(entry)
    return entry


def prune(found):
    ordered = sorted(found.values(), key=lambda e: e['last_seen'], reverse=True)
    for entry in ordered[KEEP:]:
        try:
            os.unlink(os.path.join(QUEUE, entry['id'] + '.json'))
        except OSError:
            pass
        found.pop(entry['id'], None)


# ---------------------------------------------------------------------------------- the sources

def parts_present():
    return [part for part in PARTS if os.path.isdir(path(part['present']))]


def log_files():
    files = []
    for pattern in ('/var/log/system/system_*.log', '/var/log/xgs/xgs_*.log',
                    '/var/log/configd/configd_*.log'):
        files.extend(sorted(glob.glob(path(pattern)))[-2:])
    return files


def new_text(cursor, name, first_run):
    """What was appended to one file since the last pass; on the very first pass, nothing."""
    try:
        size = os.path.getsize(name)
    except OSError:
        return ''
    if name not in cursor:
        cursor[name] = size if first_run else 0
    start = cursor[name]
    if size < start:
        start = 0		# the file was replaced under the same name
    if size == start:
        return ''
    with open(name, 'r', encoding='utf-8', errors='replace') as handle:
        handle.seek(max(start, size - SCAN_LIMIT))
        text = handle.read(SCAN_LIMIT)
    cursor[name] = size
    return text


# Lines one program logs within this many seconds of each other are one event: the kernel follower
# explains a failed build over four lines, and until 2026-10-19 that was four issues (#509).
EVENT_SECONDS = 3


def from_logs(parts, cursor, first_run):
    problems = []
    tags = {}
    events = {}         # (part, program) -> the problem its last line went into

    def add(key, at, line, problem):
        last = events.get(key)
        if last is not None and 0 <= at - last['_last'] <= EVENT_SECONDS:
            last['evidence'].append(line)
            last['_last'] = at
            return
        problem['_last'] = at
        problems.append(problem)
        events[key] = problem

    for part in parts:
        for tag in part['tags']:
            tags[tag] = part
    for name in log_files():
        for line in new_text(cursor, name, first_run).splitlines():
            match = SYSLOG.match(line)
            if not match:
                continue
            severity = int(match.group(1)) & 7
            stamp, program, message = match.group(2), match.group(3), match.group(4).strip()
            at = parse_stamp(stamp)
            if program == 'kernel':
                inner = KERNEL_TEXT.match(message)
                level, text = inner.group(1), inner.group(2).strip()
                for part in parts:
                    if part['kernel'] is None or not part['kernel'].search(text):
                        continue
                    if (level is not None and int(level) & 7 <= 3) or \
                            DRIVER_TROUBLE.search(NONE_COUNTED.sub('', text)):
                        line_text = '%s kernel: %s' % (stamp, text)
                        add((part['name'], 'kernel'), at, line_text, {
                            'part': part['name'], 'kind': 'driver', 'at': at,
                            'title': 'driver: ' + text,
                            'summary': 'The coprocessor driver logged a failure.',
                            'evidence': [line_text],
                            'signature': text,
                        })
                    break
                continue
            if severity > 3:
                continue
            part = tags.get(program)
            if part is None and program == 'self-update':
                part = next((p for p in parts if p['name'] in message), None)
            if part is None and program.startswith('configd'):
                part = next((p for p in parts if any(m in message for m in p['paths'])), None)
            if part is None:
                continue
            line_text = '%s %s: %s' % (stamp, program, message)
            add((part['name'], program), at, line_text, {
                'part': part['name'], 'kind': 'log-error', 'at': at,
                'title': '%s: %s' % (program, message),
                'summary': 'A program of this part logged an error.',
                'evidence': [line_text],
                'signature': program + ' ' + message,
            })
    for problem in problems:
        problem.pop('_last', None)
    return problems


def parse_stamp(stamp):
    """syslog-ng's RFC 5424 time, offset included, as seconds since the epoch."""
    try:
        return int(datetime.datetime.fromisoformat(stamp).timestamp())
    except ValueError:
        return int(time.time())


def from_php(parts, cursor, first_run):
    problems = []
    name = path('/var/lib/php/tmp/PHP_errors.log')
    for line in new_text(cursor, name, first_run).splitlines():
        match = PHP_LINE.match(line.strip())
        if not match:
            continue
        part = next((p for p in parts if any(m in match.group(3) for m in p['paths'])), None)
        if part is None:
            continue
        problems.append({
            'part': part['name'], 'kind': 'php', 'title': '%s: %s' % (match.group(2), match.group(3)),
            'summary': 'A page or script of this part failed in PHP.',
            'evidence': [line.strip()], 'signature': match.group(2) + ' ' + match.group(3),
        })
    return problems


def from_update():
    record = load_json('/var/db/os-xgs-npu/update/apply.json')
    if record.get('status') != 'failed':
        return []
    log = read('/var/db/os-xgs-npu/update/apply.log').splitlines()[-EVIDENCE_LINES:]
    target = str(record.get('target') or record.get('sha') or '')[:12]
    return [{
        'part': 'os-xgs-npu', 'kind': 'update-failed',
        'title': 'update to %s failed: %s' % (target or 'the checked version',
                                               re.sub(r'<[^>]+>', '', str(record.get('message', '')))[:100]),
        'summary': 'The update from XGS: Updates did not complete.',
        'evidence': log, 'signature': 'update failed ' + target,
    }]


def load_json(p):
    try:
        return json.loads(read(p) or '{}')
    except ValueError:
        return {}


def from_fanctl(parts):
    if not any(p['name'] == 'os-fanctl' for p in parts):
        return []
    incident = load_json('/var/db/fanctl/incident.json')
    if not incident:
        return []
    text = json.dumps(incident, sort_keys=True)
    return [{
        'part': 'os-fanctl', 'kind': 'fan-incident',
        'title': 'fan incident: %s' % str(incident.get('what') or incident.get('reason')
                                           or incident.get('message') or 'a restore did not complete')[:100],
        'summary': 'Fan control recorded an incident: the fans may not be back under hardware control.',
        'evidence': [text[i:i + EVIDENCE_WIDTH] for i in range(0, min(len(text), 4000), EVIDENCE_WIDTH)],
        'signature': 'fan incident ' + str(incident.get('what') or incident.get('reason') or ''),
    }]


def module_state(module):
    """What became of the driver's module: installed or not, built for which kernel, loaded or not,
    and the last lines of the installer's build of it. A report of a driver that did not attach carried
    only the kernel's messages about it, and on an XGS 126 whose module was never built there were none
    (#625): why - no kernel sources, and no network to fetch them - was in the build log (#628, #657)."""
    lines = []
    ko = '/boot/modules/%s.ko' % module
    if not os.path.exists(path(ko)):
        lines.append('module: %s is not installed' % ko)
    else:
        stamp = read(ko + '.kernel').strip().splitlines()
        running = sysctl('kern.version').splitlines()
        lines.append('module: %s, built for %s' % (ko, stamp[0].strip() if stamp else 'a kernel it does not name'))
        lines.append('running: %s' % (running[0].strip() if running else 'unknown'))
        lines.append('loaded: %s' % ('yes' if run(['/sbin/kldstat', '-n', module + '.ko']).strip() else 'no'))
    tail = [line for line in read(INSTALL_LOG).splitlines() if line.strip()][-BUILD_LOG_LINES:]
    if tail:
        lines.append('%s, its last %d lines:' % (INSTALL_LOG, len(tail)))
        lines += ['  ' + line for line in tail]
    else:
        lines.append('%s: not there' % INSTALL_LOG)
    return lines


def from_driver():
    """The driver attached on a board that has a coprocessor, and ready once the boot is long done."""
    board = run(['/bin/sh', '/usr/local/opnsense/scripts/xgs/board.sh']).split()
    if len(board) < 2:
        return []		# not a board this project knows: nothing is expected of it
    family = board[1]
    node = 'dev.octep.0' if family == 'octeontx' else 'dev.npuep.0'
    boot = boot_time()
    if not boot or time.time() - boot < READY_AFTER:
        return []
    if not sysctl(node + '.%desc'):
        return [{
            'part': 'os-xgs-npu', 'kind': 'driver-absent',
            'title': 'the %s driver is not attached ten minutes after boot' % node.split('.')[1],
            'summary': 'The board has a coprocessor and its driver is not attached: the module did not '
                       'load, or declined to attach.',
            'evidence': ([line for line in run(['/sbin/dmesg']).splitlines()
                          if re.search(r'octep|npuep|KLD', line)][-(EVIDENCE_LINES - BUILD_LOG_LINES - 4):]
                         + module_state(node.split('.')[1])),
            'signature': 'driver absent ' + family,
        }]
    if family == 'octeontx' and sysctl(node + '.ready') == '0':
        return [{
            'part': 'os-xgs-npu', 'kind': 'driver-not-ready',
            'title': 'octep is attached but not ready ten minutes after boot',
            'summary': 'The handshake with the coprocessor did not complete.',
            'evidence': ['hs_state: ' + sysctl(node + '.sdp.hs_state'),
                         'target_status: ' + sysctl(node + '.target_status'),
                         'host_status: ' + sysctl(node + '.host_status')],
            'signature': 'driver not ready octeontx',
        }]
    return []


def from_untested_model():
    """A model this project has not been run on (board.sh's "rnd"): once a boot, ten minutes in, its
    driver's ports held against what its coprocessor reports - scripts/xgs/portcheck.py, which compares
    on the appliance and prints only labels, tags and interface names. Its first owner's report is what
    confirms the model's table, or shows where it is wrong."""
    board = run(['/bin/sh', '/usr/local/opnsense/scripts/xgs/board.sh']).split()
    if len(board) < 3 or board[2] != 'rnd':
        return []
    boot = boot_time()
    if not boot or time.time() - boot < READY_AFTER or load('untested.json', {}).get('boot') == boot:
        return []
    try:
        import portcheck
        result = portcheck.check()
    except Exception:		# a check that cannot run says nothing; the driver's own report covers it
        return []
    save('untested.json', {'boot': boot})
    said = {'agrees': 'they agree', 'disagrees': 'they do NOT agree - the table is wrong for this model'}
    return [{
        'part': 'os-xgs-npu', 'kind': 'untested-model',
        'title': '%s (%s): added untested - its ports as its coprocessor reports them'
                 % (result.get('model'), result.get('assembly')),
        'summary': "This model was added from the vendor's board files without being tested: there is no "
                   "such appliance here. This holds its driver's port table against what its coprocessor "
                   "reports, port by port: %s." % said.get(result['verdict'], 'the coprocessor could not be read'),
        'evidence': result['lines'],
        'signature': 'untested model %s %s' % (result.get('assembly'), result['verdict']),
    }]


def from_module():
    stamp = module_stamp()
    if stamp is None or stamp['matches']:
        return []
    return [{
        'part': 'os-xgs-npu', 'kind': 'module-mismatch',
        'title': '%s.ko was built for another kernel' % stamp['module'],
        'summary': 'The driver module does not match the running kernel: the kernel follower did not '
                   'rebuild it, and a module built for another kernel can load and misread its structures.',
        'evidence': ['built for: ' + stamp['built_for'], 'running:   ' + stamp['running']],
        'signature': 'module mismatch',
    }]


# The frames every panic shares - the debugger's entry, the panic, the trap, and the lock code a
# panic about a lock passes through - say nothing about where it happened. Until 2026-10-18 the
# fingerprint was the first three frames, which for every page fault are kdb_enter, panic and
# trap_pfault: two different crashes on two appliances were filed as one issue (#467).
GENERIC_FRAMES = re.compile(r'^(?:kdb_\w+|vpanic|panic|trap|trap_\w+|calltrap|db_\w+|doreti\w*|fork_\w+'
                            r'|Xfast\w*|propagate_priority|turnstile_\w+|_*mtx_\w+|_*sx_\w+|sleepq_\w+'
                            r'|witness_\w+)$')
# A frame of the coprocessor's drivers: octep, and the npuep module's parts - npurpc, npuflow, npupf,
# npugiu, npunwa, npunhop - which a match on "npuep" alone missed (#468).
DRIVER_FRAME = re.compile(r'\b(?:octep|npu(?:ep|rpc|flow|pf|giu|nwa|nhop|mgmt|ctl)|mvmgmt)\w*\(\) at ')


def from_crash(seen):
    """A kernel crash dump, read where savecore left it. Nothing in /var/crash is removed here."""
    problems = []
    for info in sorted(glob.glob(path('/var/crash/info.*'))):
        # info.last is savecore's link to the newest dump, not a dump of its own: read it as one and
        # the newest crash is counted twice (#469 said three times for two).
        if not re.search(r'\.\d+$', info):
            continue
        try:
            mtime = int(os.path.getmtime(info))
        except OSError:
            continue
        key = '%s@%d' % (os.path.basename(info), mtime)
        if key in seen:
            continue
        seen.append(key)
        text = read(info[len(ROOT):] if ROOT else info)
        panic = re.search(r'Panic String:\s*(.*)', text)
        panic = panic.group(1).strip() if panic else 'unknown'
        number = info.rsplit('.', 1)[-1]
        evidence = ['panic: ' + panic]
        frames = []
        textdump = path('/var/crash/textdump.tar.%s' % number)
        if os.path.exists(textdump):
            try:
                with tarfile.open(textdump) as tar:
                    for member in ('ddb.txt', 'msgbuf.txt'):
                        try:
                            body = tar.extractfile(member).read().decode('utf-8', 'replace')
                        except (KeyError, AttributeError):
                            continue
                        lines = body.splitlines()
                        if member == 'ddb.txt':
                            trace = [l for l in lines if re.match(r'^\S+\(\) at ', l)]
                            frames = [f for f in (l.split('()')[0] for l in trace)
                                      if not GENERIC_FRAMES.match(f)][:8]
                            evidence += ['ddb: ' + l for l in trace[:20]]
                        else:
                            evidence += ['msgbuf: ' + l for l in lines[-15:]]
            except (OSError, tarfile.TarError):
                pass
        names_driver = any(DRIVER_FRAME.search(l) for l in evidence)
        problems.append({
            'part': 'os-xgs-npu', 'kind': 'kernel-panic', 'at': mtime,
            'title': 'kernel panic: %s' % panic,
            'summary': 'The kernel panicked and saved a crash dump. ' + (
                'The backtrace names the coprocessor\'s driver.' if names_driver else
                'The backtrace does not name the coprocessor\'s drivers.'),
            'evidence': evidence, 'signature': 'panic %s %s' % (panic, ' '.join(frames[:3])),
        })
    return problems


# -------------------------------------------------------------------------------- the reset check

def clean_since(previous):
    """Whether the boot that started at `previous` ended in an orderly shutdown."""
    # Written by rc.syshook.d/stop/90-xgs-report, which rc.reboot, rc.halt and rc.shutdown all run.
    try:
        with open(os.path.join(STATE, 'clean-shutdown'), 'r', encoding='utf-8') as handle:
            marker = handle.read().strip()
    except OSError:
        marker = ''
    if marker.isdigit() and int(marker) >= previous:
        return True
    # A shutdown that did not pass through rc.syshook (reboot(8) at a shell) still leaves its utx
    # record. last(1) prints it with the year when asked, before the time - "shutdown time  Sat Oct
    # 17 2026 06:57", read on the XGS 3300 - and to the minute, so the minute before counts too.
    for line in run(['/usr/bin/last', '-y', '-n', '30']).splitlines():
        if not line.startswith('shutdown time'):
            continue
        match = re.search(r'(\w{3} \w{3} +\d+ \d{4} \d\d:\d\d)', line)
        if not match:
            continue
        try:
            when = int(time.mktime(time.strptime(' '.join(match.group(1).split()), '%a %b %d %Y %H:%M')))
        except ValueError:
            continue
        if when >= previous - 60:
            return True
    return False


def last_lines_before(boot):
    """The system log's last lines before this boot: what the machine was doing when it went."""
    lines = []
    for name in sorted(glob.glob(path('/var/log/system/system_*.log')))[-2:]:
        with open(name, 'r', encoding='utf-8', errors='replace') as handle:
            for line in handle:
                match = SYSLOG.match(line.rstrip('\n'))
                if match and parse_stamp(match.group(2)) < boot - 5:
                    lines.append('%s %s: %s' % (match.group(2), match.group(3), match.group(4).strip()))
                    lines = lines[-20:]
    return lines


def reset_reason(boot):
    """What the host's chipset recorded as the cause of the reset that started this boot, as the
    start hook 06-xgs-reset-reason kept it (scripts/xgs/reset_reason.py); None when nothing was kept
    for this boot - no such register on this host, or a hook that has not run."""
    kept = load_json('/var/db/os-xgs-npu/reset-reason.json')
    if not isinstance(kept, dict) or not boot or abs(int(kept.get('boot') or 0) - boot) > 5:
        return None
    return kept


def from_reset():
    boot = boot_time()
    record = load('boots.json', {})
    previous = record.get('last_boot', 0)
    problems = []
    if previous and boot and previous != boot and not clean_since(previous):
        dumpdev = sysctl('kern.shutdown.dumpdevname')
        dumped = bool(dumpdev) and succeeds(['/sbin/savecore', '-C', '/dev/' + dumpdev])
        kept = run(['/sbin/dmesg', '-a']).count('<<BOOT>>') > 1
        if dumped:
            why = 'A crash dump is waiting on the dump device: the kernel panicked, and the dump is reported on its own once savecore has saved it.'
        elif kept:
            why = ('No crash dump was written and the message buffer survived: the machine was reset '
                   'without losing power, by a watchdog, a reset line or a reset button.')
        elif dumpdev:
            why = ('No crash dump was written and the message buffer did not survive, so memory was '
                   'cleared: the machine lost power or was reset by its hardware. A kernel panic would '
                   'have written a dump to ' + dumpdev + '.')
        else:
            why = ('No crash dump device is configured, so a panic cannot be told apart from a loss of '
                   'power; the message buffer did ' + ('' if kept else 'not ') + 'survive.')
        # The chipset's own record names the cause where the logs cannot (#454); its named bits go
        # into the signature, so that a watchdog reset and a sync flood are two problems, not one.
        reason = reset_reason(boot)
        named, said = [], ''
        if reason:
            bits = [b for b in reason.get('bits', []) if isinstance(b, dict)]
            named = [str(b.get('bit')) for b in bits if 'no published meaning' not in str(b.get('reason'))]
            said = (' The host\'s chipset recorded the reset as: %s (FCH S5_RESET_STATUS %s).' % (
                '; '.join(str(b.get('reason')) for b in bits), reason.get('value'))
                if bits else ' The host\'s chipset recorded no cause for the reset (FCH S5_RESET_STATUS 0).')
        problems.append({
            'part': 'os-xgs-npu', 'kind': 'unexpected-reset', 'at': boot,
            'title': 'the appliance restarted without a shutdown',
            'summary': 'The boot that started at %s ended without an orderly shutdown. %s%s' % (
                iso(previous), why, said),
            'evidence': ((['reset reason: ' + reason.get('value', '') + ' - ' + '; '.join(
                'bit %s: %s' % (b.get('bit'), b.get('reason')) for b in reason.get('bits', []))]
                if reason else []) + last_lines_before(boot)),
            'signature': 'unexpected reset ' + ('dump' if dumped else 'warm' if kept else 'cold')
                         + (' bits ' + ','.join(named) if named else ''),
        })
    save('boots.json', {'last_boot': boot, 'previous': previous})
    try:
        os.unlink(os.path.join(STATE, 'clean-shutdown'))
    except OSError:
        pass
    return problems


# ------------------------------------------------------------------------------------ the sending

def payload(entry, mode, system, scrub):
    body = '\n'.join([
        '### What happened', '', entry['summary'] or entry['title'], '',
        '### Evidence', '', '```text', '\n'.join(entry['evidence']) or '(none)', '```', '',
        '### The appliance', '', system_table(system), '',
        'Seen %d time(s) between %s and %s. Sent %s by the problem reporter of os-xgs-npu.' % (
            entry['count'], iso(entry['first_seen']), iso(entry['last_seen']),
            'automatically' if mode == 'automatic' else 'by its owner'),
    ])
    return {
        'schema': SCHEMA,
        'owner': OWNER,
        'repo': entry['part'],
        'fingerprint': entry['id'],
        'kind': entry['kind'],
        'title': '[%s] %s' % (entry['kind'], entry['title'])[:200],
        'body': scrub(body),
        'appliance': appliance_id(),
        'model': system.get('model', ''),
        'count': entry['count'],
        'mode': mode,
        'contact': settings()['email'] or None,
    }


def post(document):
    request = urllib.request.Request(
        relay_url(), data=json.dumps(document).encode('utf-8'), method='POST',
        headers={'Content-Type': 'application/json', 'User-Agent': 'os-xgs-npu-reporter/1'})
    try:
        with urllib.request.urlopen(request, timeout=20) as answer:
            return json.loads(answer.read().decode('utf-8') or '{}')
    except urllib.error.HTTPError as err:
        try:
            detail = json.loads(err.read().decode('utf-8') or '{}').get('message', '')
        except ValueError:
            detail = ''
        return {'status': 'failed', 'message': 'the relay answered %d %s' % (err.code, detail)}
    except (urllib.error.URLError, OSError, ValueError) as err:
        return {'status': 'failed', 'message': 'the relay could not be reached: %s' % err}


def send(entry, mode, system, scrub, budget):
    first = entry['sent_count'] == 0
    key = 'new' if first else 'updates'
    if budget[key] <= 0:
        entry['error'] = 'the day\'s allowance of reports is used up; it goes tomorrow'
        store(entry)
        return False
    answer = post(payload(entry, mode, system, scrub))
    budget[key] -= 1
    if answer.get('status') == 'ok':
        entry['status'] = 'sent'
        entry['issue'] = {'number': answer.get('issue'), 'url': answer.get('url')}
        entry['sent_at'] = int(time.time())
        entry['sent_count'] = entry['count']
        entry['error'] = ''
    else:
        entry['status'] = 'failed'
        entry['error'] = str(answer.get('message', 'the relay did not accept it'))[:300]
    store(entry)
    return entry['status'] == 'sent'


def allowance():
    record = load('sends.json', {})
    if record.get('day') != today():
        record = {'day': today(), 'new': NEW_PER_DAY, 'updates': UPDATES_PER_DAY}
    return record


def due(entry):
    """Automatic sending: a new report at once, a repeat at most once a day, a failure again."""
    if entry['status'] in ('new', 'failed') and entry['sent_count'] == 0:
        return True
    if entry['status'] in ('sent', 'failed', 'new') and entry['count'] > entry['sent_count']:
        return time.time() - entry['sent_at'] >= RESEND_AFTER
    return False


def summary(found, mode):
    """What the header's status needs, in one small file it can read on every page."""
    save('summary.json', {
        'mode': mode,
        'notice_seen': os.path.exists(os.path.join(STATE, 'notice-seen')),
        'waiting': len([e for e in found.values() if e['status'] in ('new', 'failed')]),
        'last_problem': max([e['last_seen'] for e in found.values() if e['status'] != 'dismissed'] or [0]),
        'sent_today': len([e for e in found.values()
                           if e['sent_at'] and time.time() - e['sent_at'] < 86400]),
    })


# ----------------------------------------------------------------------------------- the commands

def collect(at_boot=False):
    config = settings()
    handle = lock()
    try:
        found = entries()
        if at_boot:
            resets = from_reset()		# kept up to date even with reporting off
        else:
            resets = []
        if config['mode'] == 'off':
            summary(found, config['mode'])
            return {'status': 'ok', 'mode': 'off', 'found': 0, 'sent': 0}
        scrub = Scrubber()
        parts = parts_present()
        cursor = load('cursor.json', None)
        first_run = cursor is None
        cursor = cursor or {}
        crashes = load('crashes.json', [])
        problems = resets + from_logs(parts, cursor, first_run) + from_php(parts, cursor, first_run)
        problems += from_update() + from_fanctl(parts) + from_driver() + from_untested_model() + from_module() + from_crash(crashes)
        save('cursor.json', cursor)
        save('crashes.json', crashes[-50:])
        for problem in problems:
            remember(found, scrub, problem)
        prune(found)
        sent = 0
        # XGS_REPORT_NO_SEND: find and queue, send nothing - for trying it on an appliance by hand.
        if config['mode'] == 'automatic' and not os.environ.get('XGS_REPORT_NO_SEND'):
            waiting = [e for e in found.values() if due(e)]
            if waiting:
                system = system_block()
                budget = allowance()
                for entry in sorted(waiting, key=lambda e: e['first_seen']):
                    sent += 1 if send(entry, config['mode'], system, scrub, budget) else 0
                save('sends.json', budget)
        summary(found, config['mode'])
        return {'status': 'ok', 'mode': config['mode'], 'found': len(problems), 'sent': sent}
    finally:
        handle.close()


def listing():
    found = entries()
    rows = []
    for entry in sorted(found.values(), key=lambda e: e['last_seen'], reverse=True):
        rows.append({
            'id': entry['id'], 'part': entry['part'], 'kind': entry['kind'], 'title': entry['title'],
            'count': entry['count'], 'first_seen': iso(entry['first_seen']),
            'last_seen': iso(entry['last_seen']), 'status': entry['status'],
            'issue': (entry.get('issue') or {}).get('number') or '',
            'url': (entry.get('issue') or {}).get('url') or '', 'error': entry.get('error', ''),
            'repeat': entry['status'] == 'sent' and entry['count'] > entry['sent_count'],
        })
    config = settings()
    return {'status': 'ok', 'mode': config['mode'], 'rows': rows,
            'notice_seen': os.path.exists(os.path.join(STATE, 'notice-seen')), 'relay': relay_url()}


def one(ident):
    if not re.match(r'^[0-9a-f]{16}$', ident or ''):
        return None
    return entries().get(ident)


def main(argv):
    command = argv[1] if len(argv) > 1 else ''
    if command == 'collect':
        out = collect()
    elif command == 'boot':
        out = collect(at_boot=True)
    elif command == 'list':
        out = listing()
    elif command == 'system':
        out = system_block()
    elif command == 'acknowledge':
        os.makedirs(STATE, exist_ok=True)
        with open(os.path.join(STATE, 'notice-seen'), 'w', encoding='utf-8') as handle:
            handle.write('%d\n' % int(time.time()))
        summary(entries(), settings()['mode'])
        out = {'status': 'ok'}
    elif command in ('show', 'send', 'dismiss'):
        entry = one(argv[2] if len(argv) > 2 else '')
        if entry is None:
            out = {'status': 'failed', 'message': 'no such report'}
        elif command == 'show':
            mode = settings()['mode']
            out = {'status': 'ok', 'relay': relay_url(),
                   'payload': payload(entry, 'automatic' if mode == 'automatic' else 'manual',
                                      system_block(), Scrubber())}
        elif command == 'dismiss':
            handle = lock()
            try:
                entry['status'] = 'dismissed'
                store(entry)
                summary(entries(), settings()['mode'])
            finally:
                handle.close()
            out = {'status': 'ok'}
        elif entry['status'] == 'sent' and entry['count'] <= entry['sent_count']:
            out = {'status': 'ok', 'issue': entry.get('issue'),
                   'message': 'already sent, and nothing new has happened since'}
        else:
            handle = lock()
            try:
                budget = allowance()
                budget['new'] = max(budget['new'], 1)		# the owner's own Send is never refused
                budget['updates'] = max(budget['updates'], 1)
                ok = send(entry, 'manual', system_block(), Scrubber(), budget)
                save('sends.json', budget)
                summary(entries(), settings()['mode'])
            finally:
                handle.close()
            out = {'status': 'ok' if ok else 'failed', 'issue': entry.get('issue'),
                   'message': entry.get('error', '')}
    elif command == 'manual':
        try:
            request = json.loads(base64.b64decode(argv[2] if len(argv) > 2 else '').decode('utf-8'))
        except (ValueError, UnicodeDecodeError):
            request = {}
        title = ' '.join(str(request.get('title', '')).split())[:150]
        description = str(request.get('description', ''))[:4000]
        part = request.get('part') if request.get('part') in [p['name'] for p in PARTS] else 'os-xgs-npu'
        if not title or not description:
            out = {'status': 'failed', 'message': 'a title and a description are needed'}
        else:
            handle = lock()
            try:
                found = entries()
                problem = {'part': part, 'kind': 'manual', 'title': title,
                           'summary': description, 'evidence': [],
                           'signature': 'manual %s %d' % (title, int(time.time()))}
                entry = remember(found, Scrubber(), problem)
                summary(found, settings()['mode'])
            finally:
                handle.close()
            out = {'status': 'ok', 'id': entry['id']}
    else:
        sys.stderr.write(__doc__)
        return 2
    json.dump(out, sys.stdout, indent=1)
    sys.stdout.write('\n')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
