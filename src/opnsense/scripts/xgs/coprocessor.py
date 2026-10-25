#!/usr/local/bin/python3
#
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""The coprocessor itself - its software and its versions, the chips around it, its flash and its
eMMC with what each partition holds - for the Coprocessor tab of XGS: Overview.

    coprocessor.py collect   read the coprocessor now, keep it, print it
    coprocessor.py slots     the same, and open each eMMC partition nothing has mounted - the root
                             slot not running and the first partition - read-only for a moment,
                             for what it holds
    coprocessor.py show      print what was read last, without asking the coprocessor

READ ONLY. Every command here reads: files, /proc, /sys, the flash's own strings. Nothing is
written to the coprocessor's flash or eMMC, its U-Boot environment above all - which root slot it
boots is decided there, and a mistake there is a coprocessor that does not boot. `slots` mounts a
partition read-only with its journal left alone (ro,noload), reads, and unmounts at once.

WHAT IS WHERE (read on the XGS 136 and the second XGS 3300, 2026-10-25). The coprocessor's software
says its own SFOS build in /etc/sfversion - 21.0.0.169 on the XGS 136, 22.0.2.546 on the XGS 3300 -
which is the full number behind the generation the driver finds from the command numbering. Its
eMMC has four ext4 partitions on both families: the first a Linux of the board's maker with no SFOS
version, two root slots of 1.5 GB, one running read-only (root= on the kernel's command line) and
the other holding the build before or the same, and a small one mounted read-write at /persistent.
The flash holds the boot firmware and the U-Boot environment. The boot firmware's versions are in
the coprocessor's console at its boot, which scripts/xgs/console_log.py keeps; on the XGS 3300 the
coprocessor boots with the host, before the logger starts, so they are there only after a reboot of
the coprocessor alone.

Codes, not words, for what the page shows in its own language: a partition's role, where a chip's
name comes from.
"""
import json
import os
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    import models
except ImportError:
    models = None

NPU_RUN = '/usr/local/opnsense/scripts/xgs/npu-run.sh'
BOARD = '/usr/local/opnsense/scripts/xgs/board.sh'
STATE = '/var/db/os-xgs-npu/coprocessor.json'
CONSOLE = ('/var/log/xgs/npu-console.log', '/var/log/xgs/npu-console.log.1')

# One line a fact, each with its key first. Joined with "; " so the console's path, which sends a
# command as one line, takes it as well as the management link does.
READ = '; '.join([
    'echo "sfversion $(cat /etc/sfversion 2>/dev/null)"',
    'echo "kernel $(uname -r)"',
    'echo "os $(. /etc/os-release 2>/dev/null; echo $PRETTY_NAME)"',
    'echo "model $(tr -d "\\000" < /proc/device-tree/model 2>/dev/null)"',
    'echo "compatible $(tr "\\000" " " < /proc/device-tree/compatible 2>/dev/null)"',
    'echo "cmdline $(cat /proc/cmdline)"',
    'echo "cpus $(grep -c ^processor /proc/cpuinfo)"',
    'echo "memkb $(awk "/MemTotal/ {print \\$2}" /proc/meminfo)"',
    'sed "s/^/mtd /" /proc/mtd',
    'grep mmcblk /proc/partitions | sed "s/^/part /"',
    'grep mmcblk /proc/mounts | sed "s/^/mount /"',
    'df -k 2>/dev/null | grep -E "mmcblk|^/dev/root" | sed "s/^/df /"',
    'for d in /sys/block/mmcblk*; do [ -f $d/device/name ] && echo "emmc ${d##*/} $(cat $d/device/name) '
    '$(cat $d/device/manfid 2>/dev/null) $(cat $d/device/date 2>/dev/null)"; done',
    'for p in /persistent/*; do [ -e "$p" ] && echo "persist ${p##*/}"; done',
    'strings /dev/mtd0 2>/dev/null | grep -m1 -E "v[0-9]+\\.[0-9]+\\(release\\)" | sed "s/^/atf /"',
])

# eMMC makers by their JEDEC manufacturer id, the ones these boards carry or might
MAKERS = {0x02: 'SanDisk', 0x11: 'Kioxia', 0x13: 'Micron', 0x15: 'Samsung', 0x45: 'SanDisk',
          0x70: 'Kingston', 0x90: 'SK hynix', 0xfe: 'Micron'}

# The other chips on the boards this project has been run on, by assembly - each photographed and
# matched to the vendor's board file (docs/hardware/xgs136.md, docs/hardware/xgs3300.md). part: a
# code the page names; where: the ports it serves.
CHIPS = {
    'AMDA0201': [('switch', '88E6193X', 'Port1-8, F1, F2'), ('phy', 'AQR412C', 'Port9-12')],
    'AMDA0202': [('switch', '88E6193X', 'Port1-8, F3, F4'), ('phy', '88X5113', 'F1, F2'),
                 ('cpld', 'LCMXO3LF-2100C', '')],
}
SEEN_ON = {'AMDA0201': 'AMDA0201-0003', 'AMDA0202': 'AMDA0202-0004'}


def run(argv, timeout=60):
    try:
        out = subprocess.run(argv, capture_output=True, text=True, timeout=timeout, stdin=subprocess.DEVNULL)
    except (OSError, subprocess.SubprocessError):
        return ''
    return out.stdout


def sysctl(name):
    return run(['/sbin/sysctl', '-n', name], timeout=10).strip()


def family():
    """The second word board.sh prints: armada or octeontx."""
    words = run(['/bin/sh', BOARD], timeout=10).split()
    return words[1] if len(words) > 1 else ''


def boot_firmware():
    """The boot firmware's banners from the coprocessor's last boot that the console logger saw."""
    found = {}
    pats = [('uboot', re.compile(r'U-Boot (20\d\d\.\d\d\S*)')),
            ('atf', re.compile(r'BL31: (v\d+\.\d+\S*(?: \([^)]*\))?)')),
            ('bdk', re.compile(r'BDK [Vv]ersion:? *(\S.*)')),
            ('ddr_phy', re.compile(r'DDR\S* PHY Firmware version: *(\S+)'))]
    for path in reversed(CONSOLE):
        try:
            with open(path, 'r', errors='replace') as handle:
                for line in handle:
                    for key, pat in pats:
                        m = pat.search(line)
                        if m:
                            found[key] = m.group(1).strip()
        except OSError:
            continue
    return found


def parse(text):
    facts, mtd, parts, mounts, df, emmc, persist = {}, [], {}, {}, {}, [], []
    for line in text.splitlines():
        key, _, rest = line.partition(' ')
        if key in ('sfversion', 'kernel', 'os', 'model', 'compatible', 'cmdline', 'cpus', 'memkb', 'atf'):
            facts[key] = rest.strip()
        elif key == 'mtd':
            m = re.match(r'(mtd\d+): ([0-9a-f]+) ([0-9a-f]+) "(.*)"', rest.strip())
            if m:
                mtd.append({'device': m.group(1), 'bytes': int(m.group(2), 16), 'name': m.group(4)})
        elif key == 'part':
            w = rest.split()
            if len(w) == 4 and w[2].isdigit():
                parts[w[3]] = int(w[2])
        elif key == 'mount':
            w = rest.split()
            if len(w) >= 4:
                mounts[w[0].split('/')[-1]] = {'at': w[1], 'rw': w[3].split(',')[0] == 'rw'}
        elif key == 'df':
            w = rest.split()
            if len(w) >= 6 and w[2].isdigit():
                df[w[0].split('/')[-1]] = int(w[2])
        elif key == 'emmc':
            w = rest.split()
            if len(w) >= 2:
                emmc.append(w)
        elif key == 'persist':
            persist.append(rest.strip())
    return facts, mtd, parts, mounts, df, emmc, persist


def storage(facts, parts, mounts, df, emmc, persist):
    """The eMMC: the whole device, its boot areas, and each partition with its role."""
    m = re.search(r'root=/dev/(mmcblk\d+p\d+)', facts.get('cmdline', ''))
    root = m.group(1) if m else None
    disks = sorted(p for p in parts if re.match(r'mmcblk\d+$', p))
    out = []
    for disk in disks:
        ident = next((e for e in emmc if e[0] == disk), None)
        maker = None
        if ident and len(ident) >= 3:
            try:
                maker = MAKERS.get(int(ident[2], 16), ident[2])
            except ValueError:
                maker = ident[2]
        numbered = sorted((p for p in parts if re.match(re.escape(disk) + r'p\d+$', p)),
                          key=lambda p: int(p.rsplit('p', 1)[1]))
        roots = [p for p in numbered if parts[p] >= 1000000]
        rows = []
        for p in numbered:
            n = int(p.rsplit('p', 1)[1])
            mounted = mounts.get(p)
            used = df.get(p)
            if p == root:
                # the kernel mounts its root as /dev/root, under no partition's name
                mounted = mounted or mounts.get('root') or {'at': '/', 'rw': False}
                used = used if used is not None else df.get('root')
            if p == root:
                role = 'root_running'
            elif p in roots:
                role = 'root_other'
            elif mounted and mounted['at'] == '/persistent':
                role = 'persistent'
            elif n == 1:
                role = 'maker'
            else:
                role = 'unknown'
            rows.append({'partition': p, 'kb': parts[p], 'role': role,
                         'slot': ('AB'[roots.index(p)] if p in roots and len(roots) == 2 else None),
                         'mounted': (mounted or {}).get('at'), 'rw': (mounted or {}).get('rw'),
                         'used_kb': used,
                         'holds': persist if role == 'persistent' else None})
        out.append({'device': disk, 'kb': parts[disk], 'name': ident[1] if ident else None,
                    'maker': maker, 'date': ident[3] if ident and len(ident) >= 4 else None,
                    'boot_areas': [{'device': p, 'kb': parts[p]} for p in sorted(parts)
                                   if re.match(re.escape(disk) + r'boot\d$', p)],
                    'partitions': rows})
    return out


def look_inside(devices):
    """Each partition named, mounted read-only for a moment: its SFOS build, its system, how much
    it holds, its top level. One command for all of them."""
    steps = ['mkdir -p /tmp/xgs-slot']
    for p in devices:
        steps.append(
            'if mount -t ext4 -o ro,noload /dev/%s /tmp/xgs-slot 2>/dev/null; then '
            'echo "slot %s sfversion $(cat /tmp/xgs-slot/etc/sfversion 2>/dev/null)"; '
            'echo "slot %s os $(. /tmp/xgs-slot/etc/os-release 2>/dev/null; echo $PRETTY_NAME)"; '
            'echo "slot %s usedkb $(du -sk /tmp/xgs-slot 2>/dev/null | cut -f1)"; '
            'echo "slot %s boot $(ls /tmp/xgs-slot/boot 2>/dev/null | tr "\\n" " ")"; '
            'umount /tmp/xgs-slot; else echo "slot %s unmountable"; fi' % ((p,) * 6))
    steps.append('rmdir /tmp/xgs-slot')
    seen = {}
    for line in run(['/bin/sh', NPU_RUN, '90', '; '.join(steps)], timeout=150).splitlines():
        w = line.split(' ', 3)
        if len(w) >= 3 and w[0] == 'slot':
            entry = seen.setdefault(w[1], {})
            entry[w[2]] = w[3].strip() if len(w) > 3 else ''
    return seen


def chips(assembly, facts):
    prefix = assembly.split('-')[0] if assembly else ''
    rows = [{'part': 'host', 'name': sysctl('hw.model'), 'where': '', 'source': 'host'},
            {'part': 'coprocessor', 'name': (models.chip(assembly) if models and assembly else None) or '',
             'detail': facts.get('model') or facts.get('compatible') or '',
             'cpus': facts.get('cpus'), 'memkb': facts.get('memkb'),
             'source': 'board_file'}]
    for part, name, where in CHIPS.get(prefix, []):
        rows.append({'part': part, 'name': name, 'where': where,
                     'source': 'photographed' if SEEN_ON.get(prefix) == assembly else 'family'})
    return rows


def collect(slots=False):
    fam = family()
    assembly = ''
    if models is not None:
        try:
            assembly = models.this()[0] or ''
        except Exception:
            assembly = ''
    text = run(['/bin/sh', NPU_RUN, '60', READ], timeout=120)
    facts, mtd, parts, mounts, df, emmc, persist = parse(text)
    if not facts.get('kernel'):
        answer = {'at': int(time.time()), 'error': 'unread', 'assembly': assembly}
    else:
        drv = 'dev.npuep.0.rpc_channel' if fam == 'armada' else 'dev.octep.0.rpc'
        disks = storage(facts, parts, mounts, df, emmc, persist)
        fw = boot_firmware()
        if facts.get('atf') and 'atf' not in fw:
            m = re.search(r'(v\d+\.\d+\S*(?: \([^)]*\))?)', facts['atf'])
            fw['atf_flash'] = m.group(1) if m else facts['atf']
        answer = {
            'at': int(time.time()), 'assembly': assembly, 'family': fam,
            'model': models.MODELS[assembly][0] if models and assembly in models.MODELS else '',
            'software': {
                'sfversion': facts.get('sfversion') or None,
                'generation': sysctl(drv + '.sfos') or None,
                'cmd_shift': sysctl(drv + '.cmd_shift') or None,
                'kernel': facts.get('kernel'), 'os': facts.get('os') or None,
                'firmware': fw,
            },
            'chips': chips(assembly, facts),
            'flash': mtd,
            'emmc': disks,
        }
        if slots:
            closed = [r['partition'] for d in disks for r in d['partitions'] if not r['mounted']]
            inside = look_inside(closed) if closed else {}
            for d in disks:
                for r in d['partitions']:
                    if r['partition'] in inside:
                        r['inside'] = inside[r['partition']]
            answer['slots_at'] = int(time.time())
        else:
            # what an earlier look inside found stays, for the partitions it was about
            last = show()
            for d in answer['emmc']:
                for r in d['partitions']:
                    for od in last.get('emmc', []):
                        for orow in od.get('partitions', []):
                            if orow.get('partition') == r['partition'] and orow.get('inside'):
                                r['inside'] = orow['inside']
            if last.get('slots_at'):
                answer['slots_at'] = last['slots_at']
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    handle, temporary = tempfile.mkstemp(dir=os.path.dirname(STATE), prefix='.coprocessor-')
    with os.fdopen(handle, 'w') as stream:
        json.dump(answer, stream)
    os.replace(temporary, STATE)
    return answer


def show():
    try:
        with open(STATE, 'r') as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {}


def main(argv):
    what = argv[1] if len(argv) > 1 else 'show'
    if what == 'collect':
        print(json.dumps(collect()))
    elif what == 'slots':
        print(json.dumps(collect(slots=True)))
    elif what == 'show':
        print(json.dumps(show()))
    else:
        print(json.dumps({'error': 'usage: coprocessor.py collect|slots|show'}))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
