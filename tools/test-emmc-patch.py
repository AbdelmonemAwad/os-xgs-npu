#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""contrib/emmc/amd-fch-emmc.patch, the eMMC of the XGS 87 (#1205), away from any appliance.

The two functions the patch adds are cut out of it and compiled with stand-ins for the kernel around
them, every warning fatal, then run: the width mmc_compare_bus_width() leaves a card at when EXT_CSD
reads back the same over the wider bus, reads back changed, or a step fails, and that it always hands
the card and the host back at 1 bit; and which chipsets sdhci_pci_amd_fch() gives the Transfer Mode
quirk, by their SMBus and its revision. That needs only the patch and a C compiler.

With the FreeBSD 15.1 sources it is for - EMMC_FREEBSD_SRC, a directory holding mmc/ and sdhci/ as
sys/dev has them - also: that it applies to a copy of them exactly where it was written, with no fuzz
and no offset, and comes off again; that the stand-ins' values are the sources' own; that
sdhci_set_transfer_mode(), cut out of the patched sdhci.c, writes 0 before a command without data
only with the quirk and writes what it always did for one with data; and the order of the calls the
patch moves. A GitHub runner has no such tree, and these are skipped there, saying so.

What is not had here: the whole of mmc.c, sdhci.c and sdhci_pci.c compiled. They need a FreeBSD
kernel tree and its machine headers, and are built against the running kernel's sources on a FreeBSD
15.1 machine; nothing here stands in for that.

    EMMC_FREEBSD_SRC=/path/to/sys/dev python3 tools/test-emmc-patch.py
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATCH = os.path.join(ROOT, 'contrib', 'emmc', 'amd-fch-emmc.patch')
SOURCES = os.environ.get('EMMC_FREEBSD_SRC', '')
CC = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
PATCHCMD = shutil.which('patch')

# The values of FreeBSD's own that the stand-ins need, checked against the sources where they are here.
EXISTING = {
    'mmc/bridge.h': {'MMC_CAP_4_BIT_DATA': '(1 <<  0)', 'MMC_CAP_8_BIT_DATA': '(1 <<  1)'},
    'mmc/mmcreg.h': {
        'MMC_ERR_NONE': '0', 'MMC_ERR_FAILED': '4', 'MMC_EXTCSD_SIZE': '512',
        'EXT_CSD_PART_SUPPORT': '160', 'EXT_CSD_BUS_WIDTH': '183', 'EXT_CSD_REV': '192',
        'EXT_CSD_CARD_TYPE': '196', 'EXT_CSD_SEC_CNT': '212', 'EXT_CSD_HC_WP_GRP_SIZE': '221',
        'EXT_CSD_ERASE_TO_MULT': '223', 'EXT_CSD_ERASE_GRP_SIZE': '224',
        'EXT_CSD_SEC_FEATURE_SUPPORT': '231', 'EXT_CSD_CMD_SET_NORMAL': '1',
        'EXT_CSD_BUS_WIDTH_1': '0', 'EXT_CSD_BUS_WIDTH_4': '1', 'EXT_CSD_BUS_WIDTH_8': '2'},
    'mmc/bridge.h enum': {'bus_width_1': '0', 'bus_width_4': '2', 'bus_width_8': '3'},
}
# Linux's mmc_compare_ext_csds(), by EXT_CSD byte: what the patch's comparison has to cover.
LINUX_COMPARED = {160, 181, 192, 194, 196, 217, 221, 223, 224, 229, 230, 231, 232, 212, 213, 214, 215}


def post_image(patch_text):
    """{file: [hunk text after the patch]}, each hunk's context and added lines in order."""
    files, current, hunk = {}, None, None
    for line in patch_text.splitlines():
        if line.startswith('+++ b/'):
            current = line[len('+++ b/'):].replace('sys/dev/', '')
            files[current] = []
            hunk = None
        elif line.startswith('@@') and current:
            hunk = []
            files[current].append(hunk)
        elif hunk is not None and line[:1] in (' ', '+'):
            hunk.append(line[1:])
        elif hunk is not None and line == '':
            hunk.append('')
    return {f: ['\n'.join(h) for h in hunks] for f, hunks in files.items()}


def function(text, name):
    """The definition of name in C text: its return type's line to its closing brace at column 0."""
    m = re.search(r'^(?:static )?[\w ]+\*?\n%s\(.*?^}\n' % re.escape(name), text + '\n', re.S | re.M)
    return m.group(0) if m else None


def defines(text, prefix):
    return ''.join(m.group(0) + '\n' for m in re.finditer(r'^#define\s+%s\w*\s+[^\n\\]+?(?=\s*(/\*|$))' % prefix,
                                                         text, re.M))


def compile_and_run(test, source):
    if CC is None:
        test.skipTest('no C compiler here')
    tmp = tempfile.TemporaryDirectory()
    test.addCleanup(tmp.cleanup)
    c = os.path.join(tmp.name, 't.c')
    exe = os.path.join(tmp.name, 't')
    with open(c, 'w', encoding='utf-8', newline='\n') as f:
        f.write(source)
    done = subprocess.run([CC, '-std=gnu99', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Werror', '-o', exe, c],
                          capture_output=True, text=True)
    test.assertEqual(done.returncode, 0, done.stderr)
    ran = subprocess.run([exe], capture_output=True, text=True, timeout=30)
    test.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
    return ran.stdout


with open(PATCH, encoding='utf-8') as f:
    PATCH_TEXT = f.read()
AFTER = post_image(PATCH_TEXT)


class ThePatch(unittest.TestCase):

    def test_it_touches_only_the_two_drivers(self):
        self.assertEqual(sorted(AFTER), ['mmc/bridge.h', 'mmc/mmc.c', 'mmc/mmcreg.h', 'sdhci/sdhci.c',
                                         'sdhci/sdhci.h', 'sdhci/sdhci_pci.c'])

    def test_the_comparison_covers_what_linux_compares(self):
        body = function('\n'.join(AFTER['mmc/mmc.c']), 'mmc_compare_bus_width')
        self.assertIsNotNone(body, 'mmc_compare_bus_width is not whole in one hunk')
        table = re.search(r'fields\[\] = \{(.*?)\};', body, re.S).group(1)
        values = dict((k, v) for d in EXISTING.values() for k, v in d.items())
        for added in re.finditer(r'#define\s+(EXT_CSD_\w+)\s+(\d+)', '\n'.join(AFTER['mmc/mmcreg.h'])):
            values[added.group(1)] = added.group(2)
        bytes_ = set()
        for item in (t.strip() for t in table.split(',') if t.strip()):
            name, _, plus = item.partition('+')
            bytes_.add(int(values[name.strip()]) + (int(plus) if plus else 0))
        self.assertEqual(bytes_, LINUX_COMPARED)


class CompareBusWidth(unittest.TestCase):
    """mmc_compare_bus_width() with a card and a host that are stand-ins."""

    HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
typedef unsigned int u_int;
typedef struct dev *device_t;
#define nitems(x) (sizeof(x) / sizeof((x)[0]))
#define M_DEVBUF 0
#define M_WAITOK 0
#define malloc(s, t, f) malloc(s)
#define free(p, t) free(p)
%(defines)s
enum mmc_bus_width { bus_width_1 = %(bus_width_1)s, bus_width_4 = %(bus_width_4)s, bus_width_8 = %(bus_width_8)s };
struct mmc_softc { device_t dev; int squelched; };
struct mmc_ivars { uint16_t rca; u_int cmd6_time; uint8_t raw_ext_csd[MMC_EXTCSD_SIZE]; };

static uint32_t caps;
static int card = EXT_CSD_BUS_WIDTH_1, host = bus_width_1;
static int refuse = -1, garble = -1, read_fails = -1;
static char said[256];

static uint32_t mmcbr_get_caps(device_t d) { return caps; }
static void mmcbr_set_bus_width(device_t d, int w) { host = w; }
static void mmcbr_update_ios(device_t d) { }
static int mmc_switch(device_t b, device_t d, uint16_t rca, uint8_t set, uint8_t index, uint8_t value,
    u_int timeout, bool status)
{
	char s[8];
	if (index != EXT_CSD_BUS_WIDTH || timeout == 0 || !status) return MMC_ERR_FAILED;
	snprintf(s, sizeof(s), "w%%d ", value); strcat(said, s);
	if (value == refuse) return MMC_ERR_FAILED;
	card = value;
	return MMC_ERR_NONE;
}
static struct mmc_ivars ivar;
static int mmc_send_ext_csd(device_t b, device_t d, uint8_t *buf)
{
	strcat(said, "r ");
	if (card == read_fails) return MMC_ERR_FAILED;
	memcpy(buf, ivar.raw_ext_csd, MMC_EXTCSD_SIZE);
	if (card == garble) buf[EXT_CSD_SEC_CNT + 2] ^= 0x10;
	return MMC_ERR_NONE;
}

%(function)s

int main(int argc, char **argv)
{
	struct mmc_softc sc = { 0, 0 };
	int i, w;
	for (i = 0; i < MMC_EXTCSD_SIZE; i++) ivar.raw_ext_csd[i] = (uint8_t)(i * 7);
	ivar.cmd6_time = 500000;
	caps = %(caps)s; refuse = %(refuse)s; garble = %(garble)s; read_fails = %(read_fails)s;
	w = mmc_compare_bus_width(&sc, &ivar);
	printf("width=%%d card=%%d host=%%d squelched=%%d said=%%s\n", w, card, host, sc.squelched, said);
	return 0;
}
'''

    def run_case(self, caps, refuse='-1', garble='-1', read_fails='-1'):
        text = '\n'.join(AFTER['mmc/mmc.c'])
        added = defines('\n'.join(AFTER['mmc/bridge.h']), 'MMC_CAP_NO_BUS_TEST')
        own = ''.join('#define %s %s\n' % (k, v) for d in ('mmc/bridge.h', 'mmc/mmcreg.h')
                      for k, v in EXISTING[d].items())
        new = defines('\n'.join(AFTER['mmc/mmcreg.h']), 'EXT_CSD_')
        new = ''.join(l + '\n' for l in new.splitlines() if l.split()[1] not in own)
        source = self.HARNESS % dict(defines=own + new + added, function=function(text, 'mmc_compare_bus_width'),
                                     caps=caps, refuse=refuse, garble=garble, read_fails=read_fails,
                                     **EXISTING['mmc/bridge.h enum'])
        out = compile_and_run(self, source).strip()
        r = dict(kv.split('=', 1) for kv in out.split(' ', 4))
        r['said'] = r['said'].strip()
        return r

    def back_at_one_bit(self, r):
        self.assertEqual((r['card'], r['host'], r['squelched']), ('0', '0', '0'))

    def test_a_card_that_reads_back_the_same_over_four_bits_keeps_them(self):
        r = self.run_case('MMC_CAP_4_BIT_DATA')
        self.assertEqual(r['width'], '2')
        self.assertEqual(r['said'], 'w1 r w0')
        self.back_at_one_bit(r)

    def test_a_card_that_reads_back_changed_is_left_at_one_bit(self):
        r = self.run_case('MMC_CAP_4_BIT_DATA', garble='EXT_CSD_BUS_WIDTH_4')
        self.assertEqual(r['width'], '0')
        self.back_at_one_bit(r)

    def test_eight_bits_tried_first_and_four_when_eight_reads_back_changed(self):
        r = self.run_case('MMC_CAP_8_BIT_DATA | MMC_CAP_4_BIT_DATA', garble='EXT_CSD_BUS_WIDTH_8')
        self.assertEqual(r['width'], '2')
        self.assertEqual(r['said'], 'w2 r w0 w1 r w0')
        self.back_at_one_bit(r)

    def test_eight_bits_kept_and_four_not_tried(self):
        r = self.run_case('MMC_CAP_8_BIT_DATA | MMC_CAP_4_BIT_DATA')
        self.assertEqual(r['width'], '3')
        self.assertEqual(r['said'], 'w2 r w0')

    def test_a_switch_the_card_refuses_reads_nothing_and_stays_at_one_bit(self):
        r = self.run_case('MMC_CAP_4_BIT_DATA', refuse='EXT_CSD_BUS_WIDTH_4')
        self.assertEqual((r['width'], r['said']), ('0', 'w1'))
        self.back_at_one_bit(r)

    def test_a_read_that_fails_over_four_bits_is_one_bit(self):
        r = self.run_case('MMC_CAP_4_BIT_DATA', read_fails='EXT_CSD_BUS_WIDTH_4')
        self.assertEqual((r['width'], r['said']), ('0', 'w1 r w0'))
        self.back_at_one_bit(r)

    def test_a_host_of_one_bit_sends_nothing(self):
        r = self.run_case('0')
        self.assertEqual((r['width'], r['said']), ('0', ''))


class AmdFch(unittest.TestCase):
    """sdhci_pci_amd_fch() with the SMBus the stand-in PCI bus has."""

    HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
typedef unsigned int u_int;
struct dev { uint16_t vendor, device; uint8_t revid; };
typedef struct dev *device_t;
%(defines)s
static struct dev bus[] = { %(bus)s { 0, 0, 0 } };
static int bootverbose = 1;
static device_t pci_find_device(uint16_t v, uint16_t d)
{
	struct dev *p;
	for (p = bus; p->vendor != 0; p++)
		if (p->vendor == v && p->device == d) return p;
	return NULL;
}
static uint8_t pci_get_revid(device_t d) { return d->revid; }
static int device_printf(device_t d, const char *fmt, ...) { return 0; }

%(function)s

int main(void)
{
	u_int quirks = 0x5;
	uint32_t caps = sdhci_pci_amd_fch(NULL, &quirks);
	printf("%%s %%s %%s\n", caps == MMC_CAP_NO_BUS_TEST ? "nobustest" : "caps?",
	    (quirks & SDHCI_QUIRK_CLEAR_TRANSFER_MODE) ? "clear" : "noclear",
	    (quirks & 0x5) == 0x5 ? "kept" : "lost");
	return 0;
}
'''

    def run_case(self, bus):
        pci = '\n'.join(AFTER['sdhci/sdhci_pci.c'])
        source = self.HARNESS % dict(
            defines=defines(pci, 'AMD_') + defines('\n'.join(AFTER['sdhci/sdhci.h']), 'SDHCI_QUIRK_CLEAR')
            + defines('\n'.join(AFTER['mmc/bridge.h']), 'MMC_CAP_NO_BUS_TEST'),
            bus=''.join('{ 0x%04x, 0x%04x, 0x%02x }, ' % b for b in bus),
            function=function(pci, 'sdhci_pci_amd_fch'))
        return compile_and_run(self, source).split()

    def test_the_xgs_87_s_smbus_revision_4a_is_cz_and_gets_the_quirk(self):
        self.assertEqual(self.run_case([(0x1022, 0x790b, 0x4a)]), ['nobustest', 'clear', 'kept'])

    def test_revision_51_and_later_do_not(self):
        for rev in (0x51, 0x61):
            with self.subTest(rev=rev):
                self.assertEqual(self.run_case([(0x1022, 0x790b, rev)]), ['nobustest', 'noclear', 'kept'])

    def test_a_hudson_2_smbus_gets_it(self):
        self.assertEqual(self.run_case([(0x1022, 0x780b, 0x42)]), ['nobustest', 'clear', 'kept'])

    def test_no_smbus_known_gets_no_quirk_and_still_no_bus_test(self):
        self.assertEqual(self.run_case([(0x8086, 0x790b, 0x10)]), ['nobustest', 'noclear', 'kept'])


@unittest.skipUnless(SOURCES and os.path.isfile(os.path.join(SOURCES, 'mmc', 'mmc.c')),
                     'EMMC_FREEBSD_SRC names no FreeBSD 15.1 sys/dev here; the patch is not applied')
class AgainstTheSources(unittest.TestCase):

    def setUp(self):
        if PATCHCMD is None:
            self.skipTest('no patch(1) here')
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.tree = os.path.join(self.tmp.name, 'sys', 'dev')
        for d in ('mmc', 'sdhci'):
            shutil.copytree(os.path.join(SOURCES, d), os.path.join(self.tree, d))

    def patch(self, *more):
        return subprocess.run([PATCHCMD, '-p1', '--fuzz=0', '--no-backup-if-mismatch', '-d', self.tmp.name,
                               '-i', PATCH] + list(more), capture_output=True, text=True)

    def read(self, name):
        with open(os.path.join(self.tree, name), encoding='utf-8') as f:
            return f.read()

    def test_it_applies_where_it_was_written_and_comes_off_again(self):
        done = self.patch()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertNotRegex(done.stdout, r'(?i)offset|fuzz|hunk')
        again = self.patch('-R')
        self.assertEqual(again.returncode, 0, again.stdout + again.stderr)
        for d in ('mmc', 'sdhci'):
            for name in os.listdir(os.path.join(SOURCES, d)):
                path = os.path.join(SOURCES, d, name)
                if os.path.isfile(path):
                    with open(path, 'rb') as f, open(os.path.join(self.tree, d, name), 'rb') as g:
                        self.assertEqual(f.read(), g.read(), name)

    def test_the_stand_ins_values_are_the_sources_own(self):
        for name in ('mmc/bridge.h', 'mmc/mmcreg.h'):
            text = self.read(name)
            for k, v in EXISTING[name].items():
                self.assertRegex(text, r'#define\s+%s\s+%s(?=\s|$)' % (k, re.escape(v)), name)
        enum = re.search(r'enum mmc_bus_width \{(.*?)\}', self.read('mmc/bridge.h'), re.S).group(1)
        self.assertEqual([w.strip().split()[0] for w in enum.split(',') if w.strip()],
                         ['bus_width_1', 'bus_width_4', 'bus_width_8'])
        self.assertRegex(enum, r'bus_width_1\s*=\s*0')
        self.assertRegex(enum, r'bus_width_4\s*=\s*2')

    def test_the_order_of_what_discovery_sends(self):
        self.assertEqual(self.patch().returncode, 0)
        mmc = self.read('mmc/mmc.c')
        discover = function(mmc, 'mmc_discover_cards')
        erase = discover.index('EXT_CSD_ERASE_GRP_DEF,')
        width = discover.index('mmc_compare_bus_width(sc')
        self.assertLess(erase, width, 'ERASE_GROUP_DEF is to be set before the width is looked for')
        self.assertLess(discover.index('ivar->cmd6_time = 500 * 1000'), erase)
        self.assertLess(width, discover.index('ivar->bus_width = mmc_test_bus_width(sc)'))
        self.assertIn('MMC_CAP_NO_BUS_TEST', discover[discover.rindex('\n', 0, width - 80):width])
        attach = function(self.read('sdhci/sdhci_pci.c'), 'sdhci_pci_attach')
        self.assertLess(attach.index('slot->host.caps = caps;'), attach.index('sdhci_init_slot('))
        self.assertLess(attach.index('sdhci_pci_amd_fch(dev, &sc->quirks)'), attach.index('~sdhci_quirk_clear'))

    def test_transfer_mode_cleared_before_a_command_without_data_with_the_quirk_only(self):
        self.assertEqual(self.patch().returncode, 0)
        sdhci_h = self.read('sdhci/sdhci.h')
        body = function(self.read('sdhci/sdhci.c'), 'sdhci_set_transfer_mode')
        harness = r'''
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "%(mmcreg)s"
typedef unsigned int u_int;
%(defines)s
struct sdhci_slot { u_int quirks; int flags; struct mmc_request *req; };
#define WR2(slot, off, val) printf("%%02x=%%04x ", (unsigned)(off), (unsigned)(val))
#define __predict_true(x) (x)

%(function)s

int main(void)
{
	struct mmc_request req;
	struct sdhci_slot slot = { 0, SDHCI_USE_DMA, &req };
	struct mmc_data data;
	memset(&req, 0, sizeof(req));
	memset(&data, 0, sizeof(data));
	data.len = 512; data.flags = MMC_DATA_READ;
	sdhci_set_transfer_mode(&slot, NULL); printf("| ");
	sdhci_set_transfer_mode(&slot, &data); printf("| ");
	slot.quirks = SDHCI_QUIRK_CLEAR_TRANSFER_MODE;
	sdhci_set_transfer_mode(&slot, NULL); printf("| ");
	sdhci_set_transfer_mode(&slot, &data); printf("\n");
	return 0;
}
'''
        source = harness % dict(mmcreg=os.path.join(self.tree, 'mmc', 'mmcreg.h').replace('\\', '/'),
                                defines=defines(sdhci_h, 'SDHCI_'), function=body)
        out = compile_and_run(self, source).split('|')
        with_data = out[1].strip()
        self.assertEqual(out[0].strip(), '', 'written without the quirk')
        self.assertEqual(out[2].strip(), '0c=0000')
        self.assertEqual(out[3].strip(), with_data, 'a command with data is set as it always was')
        self.assertEqual(with_data, '0c=0013', 'block count, read, DMA')


if __name__ == '__main__':
    unittest.main(verbosity=2)
