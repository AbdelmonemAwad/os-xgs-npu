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
    coprocessor.py show      print what was read last, without asking the coprocessor, and the read
                             under way if there is one
    coprocessor.py start collect|slots
                             what the tab's buttons run, through configd: the read started in the
                             background, and what was read last printed at once

IN THE BACKGROUND, ONE AT A TIME (since 2026-11-06). Over the coprocessor's console a read takes a
minute and more - the console is shared one session at a time, and each session logs in - and configd
answers a page's action for two minutes at most. Until 2026-11-06 the page read in configd's own time,
and a read of the closed partitions - over the console a session of 60 s and one of 90, each with some
seven seconds more - came back to it as no reading at all. Now the buttons start the read and are
answered at once with the last reading and the read under way ("reading": what, and since when), and
the page asks show again until that is gone. A read holds LOCK for as long as it runs; one asked while
another runs waits for it, and takes its answer when that answer covers what was asked.

IN SESSIONS OF SESSION SECONDS AT MOST (since 2026-11-06). The watch over the coprocessor (feeder.py)
has to have its console within URGENT_WAIT, 30 s, once the management link goes silent, and a session
holds the console its seconds and some seven more at most (npu-run.sh). Until then a read was one
session of 60 s and the closed partitions one of 90; now each piece of PIECES is a session of its own,
and so is each closed partition. A session refused the console - another caller's, or kept for the
watch - is asked again a little later. A piece that does not answer is left out of the reading, not
guessed: the watchdog's and the memory errors' then read "not read yet", never "not fed".

READ ONLY. Every command here reads: files, /proc, /sys, the flash's own strings. Nothing is
written to the coprocessor's flash or eMMC, its U-Boot environment above all - which root slot it
boots is decided there, and a mistake there is a coprocessor that does not boot. `slots` mounts a
partition read-only with its journal left alone (ro,noload), reads, and unmounts at once.

WHAT IS WHERE (read on the XGS 136 and the second XGS 3300, 2026-10-25). The coprocessor's software
says its own vendor build in /etc/sfversion - 21.0.0.169 on the XGS 136, 22.0.2.546 on the XGS 3300 -
which is the full number behind the generation the driver finds from the command numbering. Its
eMMC has four ext4 partitions on both families: the first a Linux of the board's maker with no vendor
version, two root slots of 1.5 GB, one running read-only (root= on the kernel's command line) and
the other holding the build before or the same, and a small one mounted read-write at /persistent.
The flash holds the boot firmware and the U-Boot environment. The boot firmware's versions are in
the coprocessor's console at its boot, which scripts/xgs/console_log.py keeps; on the XGS 3300 the
coprocessor boots with the host, before the logger starts, so they are there only after a reboot of
the coprocessor alone.

Codes, not words, for what the page shows in its own language: a partition's role, where a chip's
name comes from.
"""
import fcntl
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
# the last reading that read the coprocessor, kept when a look cannot (last_good below)
LAST_GOOD = '/var/db/os-xgs-npu/coprocessor.last-good.json'
CONSOLE = ('/var/log/xgs/npu-console.log', '/var/log/xgs/npu-console.log.1')
# A read under way (IN THE BACKGROUND above): LOCK held by it for as long as it runs, and READING
# saying what it reads and since when - written by start a moment before the read itself has LOCK, and
# then by the read, with its process.
LOCK = '/var/run/xgs-coprocessor.lock'
READING = '/var/run/xgs-coprocessor.reading'
STARTING = 15			# how long a read started counts as under way before it has LOCK
DAEMON = '/usr/sbin/daemon'
# One session on the console (IN SESSIONS above): read for SESSION seconds at most, and waiting up to
# LOCK_WAIT for the console's lock rather than npu-run.sh's 5 s - a read runs in the background now, and
# waiting out another caller's session costs it nothing. Refused (BUSY), it is asked again PAUSE seconds
# on, TRIES times in all; the watch's mark is fresh for a minute at most.
SESSION = 20
LOCK_WAIT = 20
BUSY = 75			# npu-run.sh's exit when the console is another caller's, or kept for the watch
TRIES = 3
PAUSE = 15

# One line a fact, each with its key first, in pieces of one session each - "end <piece>" its last
# line, so that a piece cut short is told from one that ran to its end. Joined with "; " so the
# console's path, which sends a command as one line, takes it as well as the management link does.
PIECES = (
    # who the coprocessor is, and its storage: the rest is asked only once this has answered
    ('identity', '; '.join([
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
    ])),
    ('flash', '; '.join([
        'strings /dev/mtd0 2>/dev/null | grep -m1 -E "v[0-9]+\\.[0-9]+\\(release\\)" | sed "s/^/atf /"',
        # the U-Boot the flash holds, as it names itself - its version carries the SDK it was built
        # from, which the system copies' kernels carry too (2026-10-29)
        'strings /dev/mtd0 2>/dev/null | grep -m1 -E "U-Boot 20[0-9][0-9]\\.[0-9][0-9]" | sed "s/^/ubootflash /"',
    ])),
    # its own watchdog, the process that keeps it fed, and its memory error counters (discovery 24) -
    # in one piece, so that a watchdog is never shown without what feeds it
    ('health', '; '.join([
        'echo "wdt $(dmesg | grep -o -m1 -E "(sbsa-gwdt|orion_wdt|[a-z0-9_-]*wdt)[^:]*: Initiali[sz]ed[^,]*")"',
        'for p in /proc/[0-9]*; do ls -l $p/fd 2>/dev/null | grep -q /dev/watchdog && '
        'echo "wdtfeed $(tr "\\000" " " < $p/cmdline)"; done',
        'n=0; ce=0; ue=0; for f in $(find /sys/devices/system/edac -name ce_count 2>/dev/null); do n=$((n+1)); '
        'ce=$((ce+$(cat $f))); [ -f ${f%ce_count}ue_count ] && ue=$((ue+$(cat ${f%ce_count}ue_count))); done; '
        'echo "edac $n $ce $ue"',
    ])),
    # the build its fast path carries (2026-10-29): eight hex digits, a plus and a kind in brackets,
    # read out of the running executable of the coprocessor's busiest processes through /proc - the
    # fast path is the busiest by its threads - so no file of the coprocessor's software is named
    ('fastpath', 'v=; for p in $(for d in /proc/[0-9]*; do echo "$(ls $d/task 2>/dev/null | wc -l) $d"; done | '
     'sort -rn | head -3 | cut -d" " -f2); do v=$(grep -a -o -E "[0-9a-f]{8}\\+\\([A-Z]+\\)" $p/exe '
     '2>/dev/null | head -1); [ -n "$v" ] && break; done; echo "fastpath $v"'),
)

# eMMC makers by their JEDEC manufacturer id, the ones these boards carry or might
MAKERS = {0x02: 'SanDisk', 0x11: 'Kioxia', 0x13: 'Micron', 0x15: 'Samsung', 0x45: 'SanDisk',
          0x70: 'Kingston', 0x90: 'SK hynix', 0xfe: 'Micron'}

# The other chips on the boards this project has been run on, by assembly - each photographed and
# matched to the vendor's board file (docs/hardware/xgs136.md, docs/hardware/xgs3300.md). part: a
# code the page names; serves: which of the model's ports, as models.py has them - 'switch' the ones
# behind the switch, 'own' the ones on the coprocessor's own MACs, None none. Until 2026-11-06 each
# board's ports stood here as the model photographed has them, and an XGS 2100 or 2300 - whose F1
# and F2 are the switch's, with no cages on the coprocessor - was shown the XGS 3300's: the switch
# serving F3 and F4, and the 88X5113 serving F1 and F2. Whether those two boards carry the 88X5113 at
# all, and the same CPLD, has not been read; the rows stay, marked as photographed on another model.
CHIPS = {
    'AMDA0201': [('switch', '88E6193X', 'switch'), ('phy', 'AQR412C', 'own')],
    'AMDA0202': [('switch', '88E6193X', 'switch'), ('phy', '88X5113', 'own'),
                 ('cpld', 'LCMXO3LF-2100C', None)],
}
SEEN_ON = {'AMDA0201': 'AMDA0201-0003', 'AMDA0202': 'AMDA0202-0004'}


def run(argv, timeout=60):
    try:
        out = subprocess.run(argv, capture_output=True, text=True, errors='replace', timeout=timeout, stdin=subprocess.DEVNULL)
    except (OSError, subprocess.SubprocessError):
        return ''
    return out.stdout


def npu_run(seconds, command):
    """One session through npu-run.sh: (what it printed, its exit; None when it could not be run). Its
    timeout is well past the session's own most - the link's try, the wait for the lock and the session -
    so that it never fires: it would leave npu-run.sh's lockf holding the console."""
    try:
        done = subprocess.run(['/bin/sh', NPU_RUN, str(seconds), command], capture_output=True, text=True,
                              errors='replace', timeout=seconds + LOCK_WAIT + 60, stdin=subprocess.DEVNULL,
                              env=dict(os.environ, NPU_RUN_WAIT=str(LOCK_WAIT)))
    except (OSError, subprocess.SubprocessError):
        return '', None
    return done.stdout, done.returncode


def ask(command, end):
    """One session of SESSION seconds at most: (what it printed, whether its last line, end, came). The
    exit says nothing of that over the link - it is the command's own, its last loop's - so the line is
    looked for. Refused the console (BUSY), it is asked again PAUSE seconds on, TRIES times in all."""
    for attempt in range(TRIES):
        out, rc = npu_run(SESSION, '%s; echo "%s"' % (command, end))
        if rc != BUSY:
            return out, end in [line.strip() for line in out.splitlines()]
        if attempt + 1 < TRIES:
            time.sleep(PAUSE)
    return '', False


def read_pieces():
    """PIECES, a session each, in order: (the names of those that ran to their end, what they printed).
    What a piece cut short printed is left out, and nothing more is asked once who the coprocessor is
    (identity) has not answered."""
    answered, text = set(), ''
    for name, command in PIECES:
        out, done = ask(command, 'end ' + name)
        if done:
            answered.add(name)
            text += out + '\n'
        elif name == 'identity':
            break
    return answered, text


def sysctl(name):
    return run(['/sbin/sysctl', '-n', name], timeout=10).strip()


def family():
    """The second word board.sh prints: armada or octeontx."""
    words = run(['/bin/sh', BOARD], timeout=10).split()
    return words[1] if len(words) > 1 else ''


def boot_firmware():
    """The boot firmware's banners from the coprocessor's last boot that the console logger saw.

    Two of them are dates (2026-10-27): when the U-Boot it ran was built, on both families, and on
    the OCTEON TX boards when its boot image was built - the one a vendor install writes, so two
    appliances on the same vendor version can still carry different builds (an XGS 3300's was
    rebuilt for vendor version 22: 2022-06-06 before, 2024-11-22 since)."""
    found = {}
    pats = [('uboot', re.compile(r'U-Boot (20\d\d\.\d\d\S*)')),
            ('uboot_built', re.compile(r'U-Boot 20\d\d\.\d\d\S* \(([^)]*)\)')),
            ('image_built', re.compile(r'Firmware Version: *(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)')),
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
        if key in ('sfversion', 'kernel', 'os', 'model', 'compatible', 'cmdline', 'cpus', 'memkb', 'atf',
                   'wdt', 'wdtfeed', 'edac', 'fastpath', 'ubootflash'):
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
    """Each partition named, mounted read-only for a moment: its vendor build, its system, how much
    it holds, its top level. A session for each (IN SESSIONS above) - until 2026-11-06 one of 90 s for
    all of them - each at a mount point of its own, so that a look cut short over the link leaves its
    own partition mounted there and not under the next one's look."""
    seen = {}
    for p in devices:
        at = '/tmp/xgs-slot-' + p
        command = ('mkdir -p {at}; if mount -t ext4 -o ro,noload /dev/{p} {at} 2>/dev/null; then '
                   'echo "slot {p} sfversion $(cat {at}/etc/sfversion 2>/dev/null)"; '
                   'echo "slot {p} os $(. {at}/etc/os-release 2>/dev/null; echo $PRETTY_NAME)"; '
                   'echo "slot {p} usedkb $(du -sk {at} 2>/dev/null | cut -f1)"; '
                   'echo "slot {p} boot $(ls {at}/boot 2>/dev/null | tr "\\n" " ")"; '
                   'echo "slot {p} modules $(ls {at}/lib/modules 2>/dev/null | tr "\\n" " ")"; '
                   'umount {at}; else echo "slot {p} unmountable"; fi; rmdir {at}').format(at=at, p=p)
        out, _ = ask(command, 'end ' + p)
        for line in out.splitlines():
            w = line.split(' ', 3)
            if len(w) >= 3 and w[0] == 'slot' and w[1] == p:
                entry = seen.setdefault(w[1], {})
                entry[w[2]] = w[3].strip() if len(w) > 3 else ''
    return seen


def health(facts):
    """Its own watchdog and what feeds it, and its memory error counters (EDAC): none registered on
    the ARMADA family, nineteen on the XGS 3300 - the memory controller's and nine L2 caches'."""
    w = facts.get('edac', '').split()
    edac = None
    if len(w) == 3 and all(x.isdigit() for x in w):
        edac = {'counters': int(w[0]), 'corrected': int(w[1]), 'uncorrected': int(w[2])}
    wdt = facts.get('wdt', '')
    fed = facts.get('wdtfeed', '')
    every = re.search(r'-t (\d+)', fed)
    return {'watchdog': wdt.split()[0] if wdt else None, 'fed_by': fed or None,
            'feed_every': int(every.group(1)) if every else None, 'edac': edac}


def fastpath(text):
    """The fast path's build, its eight hex digits and its kind: 3f430ea4 and PROD on vendor version
    23, 51f76d3b on 22, 1cacb7d1 on 21 (2026-10-29). Finer than the vendor build: two appliances
    on one vendor version can carry different fast paths."""
    m = re.match(r'([0-9a-f]{8})\+\(([A-Z]+)\)$', (text or '').strip())
    return {'build': m.group(1), 'kind': m.group(2)} if m else None


SDK = re.compile(r'-(\d+\.\d+\.\d+)(?:\.[A-Z0-9-]+)?$')


def sdk_match(facts, fw, disks):
    """Whether U-Boot and each system copy come from one SDK (2026-10-29): U-Boot's version ends in
    the SDK it was built from (2019.10-10.22.03), and so does a system copy's kernel - the running
    one's, and each other copy's as its modules directory names it, once the closed copies have
    been looked inside. A copy built from another SDK than the U-Boot that boots it is named."""
    m = re.search(r'U-Boot 20\d\d\.\d\d\S*', facts.get('ubootflash', ''))
    uboot = m.group(0)[len('U-Boot '):] if m else (fw.get('uboot') or '')
    m = SDK.search(uboot.split(' ')[0])
    if not m:
        return None
    systems = {}
    m_run = SDK.search(facts.get('kernel', ''))
    for d in disks:
        for r in d.get('partitions', []):
            # the system copies only, as the vendor's own check: the board maker's Linux carries no
            # vendor build and is not booted by this U-Boot's normal command
            if not str(r.get('role', '')).startswith('root_'):
                continue
            if r.get('role') == 'root_running' and m_run:
                systems[r['partition']] = m_run.group(1)
            for name in ((r.get('inside') or {}).get('modules') or '').split():
                k = SDK.search(name)
                if k:
                    systems.setdefault(r['partition'], k.group(1))
    if not systems and m_run:
        systems['running'] = m_run.group(1)
    return {'uboot': m.group(1), 'systems': systems,
            'differ': sorted(p for p, v in systems.items() if v != m.group(1))}


def span(labels):
    """'Port1-8, F3, F4' from the labels: the ports first, then the cages, three or more in a row as
    a range."""
    def split(label):
        name = label.rstrip('0123456789')
        return name, int(label[len(name):] or 0)
    runs = []
    for name, n in sorted(map(split, labels), key=lambda s: (s[0] != 'Port', s)):
        if runs and runs[-1][0] == name and runs[-1][2] + 1 == n:
            runs[-1][2] = n
        else:
            runs.append([name, n, n])
    out = []
    for name, a, b in runs:
        out += ['%s%d-%d' % (name, a, b)] if b - a >= 2 else ['%s%d' % (name, k) for k in range(a, b + 1)]
    return ', '.join(out)


def served(assembly, prefix, serves):
    """The ports a chip serves on this model (CHIPS), from models.py: a model the table does not have
    is given its board's photographed one, as the bring-up and the drivers give it that panel."""
    if models is None or serves is None:
        return ''
    a = assembly if assembly in models.MODELS else SEEN_ON.get(prefix)
    if a not in models.MODELS:
        return ''
    ports = models.ports(a)
    return span([p['label'] for p in ports if p['attach'].startswith('switch port') == (serves == 'switch')])


def chips(assembly, facts):
    prefix = assembly.split('-')[0] if assembly else ''
    rows = [{'part': 'host', 'name': sysctl('hw.model'), 'where': '', 'source': 'host'},
            {'part': 'coprocessor', 'name': (models.chip(assembly) if models and assembly else None) or '',
             'detail': facts.get('model') or facts.get('compatible') or '',
             'cpus': facts.get('cpus'), 'memkb': facts.get('memkb'),
             'source': 'board_file'}]
    for part, name, serves in CHIPS.get(prefix, []):
        rows.append({'part': part, 'name': name, 'where': served(assembly, prefix, serves),
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
    answered, text = read_pieces()
    facts, mtd, parts, mounts, df, emmc, persist = parse(text)
    if 'identity' not in answered or not facts.get('kernel'):
        answer = {'at': int(time.time()), 'error': 'unread', 'assembly': assembly}
        # a reading from before LAST_GOOD existed is kept there before this one replaces it
        if not os.path.exists(LAST_GOOD) and last_good():
            write(LAST_GOOD, last_good())
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
                'fastpath': fastpath(facts.get('fastpath')),
                'generation': sysctl(drv + '.sfos') or None,
                'cmd_shift': sysctl(drv + '.cmd_shift') or None,
                'kernel': facts.get('kernel'), 'os': facts.get('os') or None,
                'firmware': fw,
            },
            'chips': chips(assembly, facts),
            # not read - its session refused or cut short - is None, which the page shows as not read
            # yet: health() of nothing is a watchdog that nothing feeds
            'health': health(facts) if 'health' in answered else None,
            'flash': mtd,
            'emmc': disks,
        }
        inside, closed = {}, []
        if slots:
            closed = [r['partition'] for d in disks for r in d['partitions'] if not r['mounted']]
            inside = look_inside(closed) if closed else {}
        # what an earlier look inside found stays, for the partitions this one did not look into - a
        # session each since 2026-11-06, and one of them refused loses nothing that was known of it
        last = last_good()
        for d in answer['emmc']:
            for r in d['partitions']:
                if r['partition'] in inside:
                    r['inside'] = inside[r['partition']]
                    continue
                for od in last.get('emmc', []):
                    for orow in od.get('partitions', []):
                        if orow.get('partition') == r['partition'] and orow.get('inside'):
                            r['inside'] = orow['inside']
        if slots and (inside or not closed):
            answer['slots_at'] = int(time.time())
        elif last.get('slots_at'):
            answer['slots_at'] = last['slots_at']
        # after the closed copies' contents are in, from this look or the last
        answer['software']['sdk'] = sdk_match(facts, fw, answer['emmc'])
    write(STATE, answer)
    if 'error' not in answer:
        write(LAST_GOOD, answer)
    return answer


def write(path, answer):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    handle, temporary = tempfile.mkstemp(dir=os.path.dirname(path), prefix='.coprocessor-')
    with os.fdopen(handle, 'w') as stream:
        json.dump(answer, stream)
    os.replace(temporary, path)


def show(path=None):
    try:
        with open(path or STATE, 'r') as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {}


def last_good():
    """The last reading that read the coprocessor: a look that could not still says so in STATE, and
    until 2026-11-06 that was all there was - what an earlier look inside found was lost with it, and
    a problem report lost the vendor build."""
    state = show()
    return state if state and 'error' not in state else show(LAST_GOOD)


def locked():
    """Whether a read holds LOCK now. Asked with a shared lock, given back at once: a read that comes
    for LOCK meanwhile waits that moment, and is never refused for it."""
    try:
        fd = os.open(LOCK, os.O_RDONLY)
    except OSError:
        return False
    try:
        fcntl.flock(fd, fcntl.LOCK_SH | fcntl.LOCK_NB)
    except OSError:
        return True
    finally:
        os.close(fd)
    return False


def under_way():
    """The read under way, {'what': collect or slots, 'since': when it began}, or None: one that holds
    LOCK, or one started less than STARTING seconds ago that has not taken it yet. A read that died
    holds no lock, and its READING is soon too old to count."""
    mark = show(READING)
    mark = mark if isinstance(mark, dict) else {}
    since = mark.get('since') if isinstance(mark.get('since'), int) else None
    if locked() or (since is not None and 0 <= time.time() - since < STARTING):
        return {'what': mark.get('what') if mark.get('what') in ('collect', 'slots') else 'collect',
                'since': since}
    return None


def shown():
    """What show prints: what was read last, and as "reading" the read under way, if there is one."""
    out = show()
    now = under_way()
    if now:
        out['reading'] = now
    return out


def read(what):
    """collect or slots, one at a time (LOCK, held for as long as it runs). One asked while another runs
    waits for it, and takes its answer when that came after this was asked and covers what was asked -
    a read of the closed partitions covers a plain read, not the other way round."""
    asked = int(time.time())
    try:
        fd = os.open(LOCK, os.O_RDWR | os.O_CREAT, 0o644)
    except OSError:
        fd = None		# no lock to be had: read as before 2026-11-06 rather than not at all
    try:
        waited = False
        if fd is not None:
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError:
                fcntl.flock(fd, fcntl.LOCK_EX)
                waited = True
        if waited:
            last = show()
            if 'error' not in last and (last.get('at') or 0) >= asked and \
                    (what == 'collect' or (last.get('slots_at') or 0) >= asked):
                return last
        try:
            write(READING, {'what': what, 'since': int(time.time()), 'pid': os.getpid()})
        except OSError:
            pass
        try:
            return collect(slots=(what == 'slots'))
        finally:
            try:
                os.unlink(READING)
            except OSError:
                pass
    finally:
        if fd is not None:
            os.close(fd)


def start(what):
    """The tab's buttons, through configd ([coprocessor.collect], [coprocessor.slots]): the read started
    in the background as its own process - detached by daemon(8), as plugins.py starts an update - unless
    one is under way, and what was read last answered at once, with the read under way."""
    if under_way() is None:
        try:
            write(READING, {'what': what, 'since': int(time.time())})
            subprocess.Popen([DAEMON, '-f', sys.executable, os.path.abspath(__file__), what],
                             stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            try:
                os.unlink(READING)
            except OSError:
                pass
    return shown()


def main(argv):
    what = argv[1] if len(argv) > 1 else 'show'
    if what in ('collect', 'slots'):
        print(json.dumps(read(what)))
    elif what == 'start' and len(argv) > 2 and argv[2] in ('collect', 'slots'):
        print(json.dumps(start(argv[2])))
    elif what == 'show':
        print(json.dumps(shown()))
    else:
        print(json.dumps({'error': 'usage: coprocessor.py collect|slots|show|start collect|slots'}))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
