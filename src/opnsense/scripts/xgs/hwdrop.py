#!/usr/local/bin/python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""Drop the connections a cut names on the coprocessor itself, by their verdict, before their pf
states are killed - XGS: Settings "Cut in hardware" (discovery 3, 2026-10-23).

lists_cut.py and ja4_cut.py kill pf states by id, and each driver then takes the connection out of
hardware - within a second or two, and every frame after that crosses to this host for pf to drop.
With "Cut in hardware" on, every plain connection is made with the microflow action IPS and the
fast path's IPS switch is on (dp.mflow_action / flow.mflow_action, offload.sh), so a connection's
verdict decides each of its frames: DROP, written here, stops them on the coprocessor at once, both
ways, and the driver keeps the connection there while its sender goes on sending. A connection made
before the setting was turned on, and a tunnel's, is made to forward without asking: DROP changes
nothing for it, and the pf kill cuts it as before.

    import hwdrop
    hwdrop.drop_addresses(contains)  connections with either end in contains (an `in` test)
    hwdrop.drop_ends(groups)         connections both of whose ends, (address, port), are in one of
                                     groups - each the ends of one pf state being cut

Each returns how many connections the coprocessor took DROP for. Never raises.

A verdict names its connection by index and revision, both as the table printed them: "<index> 1
<rev>". Until 2026-11-06 it went by the index alone, and the table is read once, before the writes -
a connection that closed in between left its index to the next one made (each driver hands the index
freed last out first), and that one, which no list named, was dropped on the coprocessor and kept
there while its frames kept coming. A driver as of that day refuses an index whose revision has moved,
and changes nothing; an older one reads the index and the verdict and stops there, as before.
"""
import fcntl
import ipaddress
import os
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flows  # noqa: E402

CONFIG = '/conf/config.xml'
SYSCTL = '/sbin/sysctl'

# Each driver's table: what prints its first page, the verdict to write, how flows.py reads it, and
# the two sysctls that print the rest a page at a time - the place a page starts from, which every
# reader shares, and the page.
TABLES = (('dev.octep.0.dp.flow_table', 'dev.octep.0.dp.flow_verdict', flows.octep,
           'dev.octep.0.dp.flow_table_from', 'dev.octep.0.dp.flow_table_page'),
          ('dev.npuep.0.flow.flows', 'dev.npuep.0.flow.verdict', flows.npuep,
           'dev.npuep.0.flow.flows_from', 'dev.npuep.0.flow.flows_page'))
# The first page's last line when the table holds more than it prints, on both families.
MORE = re.compile(r'^and \d+ more connections, not printed$', re.M)
# A page's first line, which names the place it was printed from, and its last, which names the
# place the next page starts from - or says "the last page".
PAGE_FROM = re.compile(r'from (\d+), \d+ in the table')
NEXT_FROM = re.compile(r'^next from (\d+)$', re.M)
END = 4294967295        # a place past the end of any live list: the reading starts at its end
PAGES = 2048            # at most: the tables' ceiling, 2,000,000 connections, is 489 pages of 4,096
TRIES = 3               # writes of one place, should another reader's come in between
# The one place is every reader's, so readers take turns: lockf(1)'s kind of lock, on this file.
LOCK = '/var/run/xgs-flow-table.lock'
LOCK_WAIT = 30          # seconds, as offload.sh waits for the rpc lock


def enabled(path=CONFIG):
    """On unless XGS: Settings has "Cut in hardware" off."""
    try:
        g = ET.parse(path).getroot().find('OPNsense/XGS/general')
    except (OSError, ET.ParseError):
        return True
    return g is None or (g.findtext('cut_in_hardware') or '1').strip() != '0'


def table():
    """(the driver's verdict sysctl, the connections as flows.py reads them), or (None, []).

    Each driver prints the first 4,096 connections of its table and counts the rest. Until
    2026-11-06 that first page was all this read, and a connection past it was never given its
    verdict (the review's D13). The first page is still read first and alone, as before, and when it
    says there are more the connections come as a generator: the first page's, then the whole table
    again a page at a time (pages). Each page is read once the one before it has been gone through,
    so its connections are as fresh as the first page's were."""
    for name, verdict, parse, start, page in TABLES:
        text = flows.sysctl(name)
        if text is None:
            continue
        if MORE.search(text):
            return verdict, every(parse(text), start, page, parse)
        return verdict, parse(text)
    return None, []


def every(first, start, page, parse):
    """The first page's connections, and then every page's."""
    yield from first
    yield from pages(start, page, parse)


def pages(start, page, parse):
    """The table a page at a time, from the end of its live list down - every connection there for
    the whole reading is on a page, and one moved down by another that went may be on two (once,
    in drop). The place a page starts from is one setting for every reader, so this takes the lock
    first; not had within LOCK_WAIT seconds, or a driver older than the pages, and the first page
    is all there is, as before. The place is put back to 0 at the end, where a page holds nothing,
    so that a walk of the sysctl tree which reads the page finds two lines and no connections."""
    lock = locked()
    if lock is None:
        return
    try:
        at = END
        for _ in range(PAGES):
            text = page_at(start, page, at)
            if text is None:
                return
            yield from parse(text)
            nxt = NEXT_FROM.search(text)
            # the last page says so; a next place not below this one would never end
            if nxt is None or not 0 < int(nxt.group(1)) < at:
                return
            at = int(nxt.group(1))
    finally:
        try:
            subprocess.run([SYSCTL, '%s=0' % start], capture_output=True, timeout=10)
        except (OSError, subprocess.SubprocessError):
            pass
        lock.close()


def page_at(start, page, at):
    """The page from place at, or None. The page names the place it was printed from: another one
    is a write in between, by hand or by a reader without the lock, and the place is written again,
    TRIES times at most."""
    for _ in range(TRIES):
        try:
            done = subprocess.run([SYSCTL, '%s=%d' % (start, at)], capture_output=True, timeout=10)
        except (OSError, subprocess.SubprocessError):
            return None
        if done.returncode != 0:
            return None     # no such sysctl: a driver older than the pages
        text = flows.sysctl(page)
        if text is None:
            return None
        head = PAGE_FROM.fullmatch(text.split('\n', 1)[0])
        if head and int(head.group(1)) == at:
            return text
    return None


def locked():
    """LOCK held, as an open file that lets it go when closed; None when it is not had in time."""
    try:
        fh = open(LOCK, 'w')
    except OSError:
        return None
    until = time.monotonic() + LOCK_WAIT
    while True:
        try:
            fcntl.flock(fh, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return fh
        except OSError:
            if time.monotonic() >= until:
                fh.close()
                return None
            time.sleep(0.2)


def once(match):
    """match, true only the first time for each connection - by its index and revision, since an
    index goes to the next connection made once its own has gone: the first page's connections are
    on a later page too, and one moved down the live list may be on two."""
    done = set()

    def first(f):
        if not match(f):
            return False
        key = (f.get('id'), f.get('rev'))
        if key in done:
            return False
        done.add(key)
        return True
    return first


def address(text):
    try:
        return str(ipaddress.ip_address(text))
    except ValueError:
        return None


def drop(match, path=CONFIG):
    match = once(match)
    if not enabled(path):
        return 0
    oid, conns = table()
    if oid is None:
        return 0
    dropped = 0
    for f in conns:
        try:
            if not match(f):
                continue
            subprocess.run([SYSCTL, '%s=%d 1 %d' % (oid, f['id'], f['rev'])], capture_output=True, timeout=10)
            said = subprocess.run([SYSCTL, '-n', oid], capture_output=True, text=True, errors='replace', timeout=10).stdout
        except (KeyError, TypeError, ValueError, OSError, subprocess.SubprocessError):
            continue
        if 'on the coprocessor' in said:
            dropped += 1
    return dropped


def drop_addresses(contains, path=CONFIG):
    return drop(lambda f: any(a is not None and a in contains
                              for a in (address(f.get('src', '')), address(f.get('dst', '')))), path)


def drop_ends(groups, path=CONFIG):
    # Both ends in the same state's. Until 2026-11-06 the ends of every state being cut were one set,
    # so a connection that took its source from one of them and its destination from another was
    # dropped though it was not cut: a client that used one port to two servers, its connection to
    # the first cut in the same reading as another client's to the second, lost both.
    def both(f):
        a, b = (address(f.get('src', '')), f.get('sport')), (address(f.get('dst', '')), f.get('dport'))
        return any(a in ends and b in ends for ends in groups)
    return drop(both, path)
