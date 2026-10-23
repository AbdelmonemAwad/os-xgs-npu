#-
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
#
"""
What the site's page for each model says beyond its ports: the rest of the board, the host, the
panel the ports are on, and the vendor's own figures - each with where it was read.

The ports themselves are scripts/xgs/models.py's, which the drivers use; nothing here repeats them.
What is here is what no driver needs and a reader does: the chips the vendor's board file names,
each port's speed and PoE, the host as it was read on the two appliances run here, the way the
ports sit on the panel, and the vendor's datasheet figures, labelled as the vendor's.
tools/devices-table.py puts it together with models.py into website/data/devices.json, and
website/devices.py writes the pages from that.

A fact has a source, and a page says which: 'board' is the vendor's board file for the assembly
(xgs-bsp/data in the vendor's GPL release), 'dt' its device tree, 'read' read on an appliance here,
and a datasheet by its key in SOURCES.
"""

# Where a fact was read. A page lists the ones it uses; the words for the kinds are the site's
# catalogue's (devices.source_kinds).
SOURCES = {
    'board': {'kind': 'board', 'title': "xgs-bsp/data/<assembly>.txt, the vendor's board file (GPL release, SDK 10.22.03)"},
    'dt': {'kind': 'dt', 'title': "sdk-ext-sophos patches dt1, dt2 and dt2-138, the vendor's device trees"},
    'platform': {'kind': 'board', 'title': "xgs-check-uboot-version.sh, the vendor's map of assembly to platform"},
    'startup': {'kind': 'board', 'title': "xgs-host-startup.sh, where the vendor's host tells the families apart by PCI id"},
    'read136': {'kind': 'read', 'title': 'the XGS 136 run here', 'page': 'docs/hardware/xgs136.md'},
    'read3300': {'kind': 'read', 'title': 'the two XGS 3300s run here', 'page': 'docs/hardware/xgs3300.md'},
    'openwrt107': {'kind': 'outside', 'title': 'openwrt-sophos-xgs107, its hardware page - an XGS 107w board read by another project',
                   'url': 'https://github.com/nicologiuliani6/openwrt-sophos-xgs107/blob/main/docs/hardware.md'},
    'br2026': {'kind': 'vendor', 'title': 'Sophos Firewall brochure, PDF of 2026-09-08', 'edition': '2026-09',
               'url': 'https://assets.sophos.com/X24WTUEQ/at/7wf85vbnnqf939bbhtxgfk/sophos-firewall-br.pdf'},
    'br2024-87': {'kind': 'vendor', 'title': "the vendor's XGS 87(w) page, brochure edition of 2024-11, a reseller's copy", 'edition': '2024-11',
                  'url': 'https://media.bechtle.com/asrc/180712/1c4b3d4ee288fc9434f5175bf56070570/c3/-/86f51e47f95948eaa8778fd028e70216/sophos-xgs-87-std-prot-smb-36m-data-sheet'},
    'br2024-107': {'kind': 'vendor', 'title': "the vendor's XGS 107(w) page, brochure edition of 2024-11, a reseller's copy", 'edition': '2024-11',
                   'url': 'https://media.bechtle.com/asrc/180712/1c4b3d4ee288fc9434f5175bf56070570/c3/-/f1923febf3fb44d6808efecb761c1f72/sophos-xgs-107-std-protect-smb-12m-data-sheet'},
    'br2024-116': {'kind': 'vendor', 'title': "the vendor's XGS 116(w) page, brochure edition of 2024-11, a reseller's copy", 'edition': '2024-11',
                   'url': 'https://media.bechtle.com/asrc/180712/1c4b3d4ee288fc9434f5175bf56070570/c3/-/ed95b2190268485096648bf1838fb473/sophos-xgs-116-std-protect-smb-12m-data-sheet'},
    'br2024-126': {'kind': 'vendor', 'title': "the vendor's XGS 126(w)/136(w) page, brochure edition of 2024-11, a reseller's copy", 'edition': '2024-11',
                   'url': 'https://media.bechtle.com/asrc/180712/1c4b3d4ee288fc9434f5175bf56070570/c3/-/7819ada394724170a6ee9ad8ccd96c56/sophos-xgs-126-std-protect-smb-36m-data-sheet'},
    'ds2022-126': {'kind': 'vendor', 'title': "the vendor's XGS 126/136 one-pager of 2022-08, a reseller's copy", 'edition': '2022-08',
                   'url': 'https://accessnetworks.com/wp-content/uploads/2022/08/XGS126-and-136.pdf'},
}

# The vendor's platform name for each board, which picks its coprocessor's boot loader.
PLATFORM = {'AMDA0200': 'xgsdt1', 'AMDA0201': 'xgsdt2-126136', 'AMDA0208': 'xgsdt2-116',
            'AMDA0224': 'xgsdt2-138', 'AMDA0202': 'xgs1us'}

# What the coprocessor is made of, where a page has read it.
CHIP_PARTS = {'CN9130': 'AP807 + CP115', 'CN9131': 'AP807 + two CP115', 'CN8360': 'OCTEON TX (CN83XX)',
              'CN8365': 'OCTEON TX (CN83XX)'}

# Each board: its switch, the coprocessor's link to it, and every port the switch does not carry -
# the MAC it is on and what is between that MAC and the socket. From the board file
# (npu0.deviceN, npu0.phyN.phy, init_speed, poe, ftwbump) and the device tree; the XGS 3300's
# MACs as its network agent reported them.
BOARDS = {
    'AMDA0200': {'switch': '88E6193X', 'uplink': ('CP0 eth0', '10GBASE-KR'), 'own': {}},
    'AMDA0208': {'switch': '88E6193X', 'uplink': ('CP0 eth0', '10GBASE-KR'), 'own': {}},
    'AMDA0201': {'switch': '88E6193X', 'uplink': ('CP0 eth0', '10GBASE-KR'),
                 'own': {'Port9': ('CP0 eth2', 'AQR412C'), 'Port12': ('CP1 eth0', 'AQR412C'),
                         'Port10': ('CP1 eth1', 'AQR412C'), 'Port11': ('CP1 eth2', 'AQR412C')}},
    'AMDA0224': {'switch': '88E6193X', 'uplink': ('CP1 eth1', '5GBASE-KR'),
                 'own': {'F1': ('CP0 eth0', None), 'Port5': ('CP0 eth2', 'AQR412'),
                         'F2': ('CP1 eth0', None), 'Port6': ('CP1 eth2', 'AQR412')}},
    'AMDA0202': {'switch': '88E6193X', 'uplink': ('QLM4', '10G'), 'slot': 'A',
                 'own': {'F1': ('QLM5', '88X5113'), 'F2': ('QLM6', '88X5113')}},
}

# Speeds other than a gigabit, PoE and the bypass pair, by board or by assembly and label - the board
# file's: the rate a port may offer on the cable, its phyN.ratecap where the file sets one, else the
# lane's init_speed. Not the lane alone: the XGS 126 and 136 share one board, whose four AQR412 lanes
# are all init_speed=2.5G, and its files cap them per model - labels 9 to 12 at 1000 on the 126 and
# 126w (-0001, -0002), labels 11 and 12 at 2500 on the 136 and 136w (-0003, -0004), as the 136 run
# here was read (docs/hardware/xgs136.md, the port map) and both datasheets give (2 x GE, 2 x 2.5 GE).
# Until 2026-10-24 the board's lanes stood here and every one of those pages said four 2.5G ports.
SPEED = {'AMDA0201-0003': {'Port11': '2.5G', 'Port12': '2.5G'},
         'AMDA0201-0004': {'Port11': '2.5G', 'Port12': '2.5G'},
         'AMDA0224': {'F1': '10G', 'F2': '10G', 'Port5': '2.5G', 'Port6': '2.5G'},
         'AMDA0202-0003': {'F1': '10G', 'F2': '10G'}, 'AMDA0202-0004': {'F1': '10G', 'F2': '10G'}}
POE = {'AMDA0201': ('Port11', 'Port12'), 'AMDA0224': ('Port5', 'Port6')}
BYPASS = {'AMDA0202': ('Port1', 'Port2')}

# The panel the ports are on, left to right as the appliance stands: '|' a gap, 'A/B' A above B,
# 'led:A/B' the lights of cages A and B beside them, and com, usb, mgmt, lcd, bay the rest. Two are
# drawn from the appliances photographed here; the others put the vendor's ports where the nearest
# of those two has them, and say so.
PANELS = {
    'XGS 87': ('back', 'com usb | F1 | Port1/Port2 Port3/Port4', 'XGS 136'),
    'XGS 107': ('back', 'com usb | F1 | Port1/Port2 Port3/Port4 Port5/Port6 Port7/Port8', 'XGS 136'),
    'XGS 116': ('back', 'com usb | F1 | Port1/Port2 Port3/Port4 Port5/Port6 | Port7/Port8 | bay', 'XGS 136'),
    'XGS 126': ('back', 'com usb | F1/F2 | Port1/Port2 Port3/Port4 Port5/Port6 Port7/Port8 | Port9/Port10 Port11/Port12 | bay', 'XGS 136'),
    'XGS 136': ('back', 'com usb | F1/F2 | Port1/Port2 Port3/Port4 Port5/Port6 Port7/Port8 | Port9/Port10 Port11/Port12 | bay', None),
    'XGS 138': ('back', 'com usb | F1/F2 | Port1/Port2 Port3/Port4 | Port5/Port6 | bay', 'XGS 136'),
    'XGS 2100': ('front', 'lcd | com usb mgmt | led:F2/F1 F1 F2 | Port2/Port1 Port4/Port3 Port6/Port5 Port8/Port7 | bay', 'XGS 3300'),
    'XGS 2300': ('front', 'lcd | com usb mgmt | led:F2/F1 F1 F2 | Port2/Port1 Port4/Port3 Port6/Port5 Port8/Port7 | bay', 'XGS 3300'),
    'XGS 3100': ('front', 'lcd | com usb mgmt | led:F2/F1 F1 F2 led:F4/F3 F3 F4 | Port2/Port1 Port4/Port3 Port6/Port5 Port8/Port7 | bay', 'XGS 3300'),
    'XGS 3300': ('front', 'lcd | com usb mgmt | led:F2/F1 F1 F2 led:F4/F3 F3 F4 | Port2/Port1 Port4/Port3 Port6/Port5 Port8/Port7 | bay', None),
}

# The host, the x86 side, where it has been read.
HOSTS = {
    'XGS 87': {'cpu': 'AMD RX-216TD', 'memory': '4 GB', 'link': 'PCIe 11ab:7080', 'source': 'openwrt107'},
    'XGS 107': {'cpu': 'AMD RX-216TD', 'memory': '4 GB', 'link': 'PCIe 11ab:7080', 'source': 'openwrt107'},
    'XGS 136': {'cpu': 'AMD Ryzen Embedded R1606G, 4 threads', 'memory': '8 GB', 'link': 'PCIe 11ab:7080, Gen3 x4',
                'source': 'read136'},
    'XGS 3300': {'cpu': 'AMD Ryzen Embedded V1780B, 8 threads', 'memory': '16 GB', 'link': 'PCIe 177d:a300, an SDP endpoint',
                 'source': 'read3300', 'mgmt': 'Intel I210 or I211 (igb)'},
}
LINK = {'armada': 'PCIe 11ab:7080', 'octeontx': 'PCIe 177d:a300'}

# The appliances photographed here: a picture of the board, the page it is on, the measurements.
PHOTOS = {
    'XGS 136': {'image': 'docs/images/hardware/xgs136/top-annotated.jpg', 'page': 'docs/hardware/xgs136.md',
                'drawing': 'docs/images/hardware/xgs136/port-chain.svg', 'measured': 'docs/measurements/README.md'},
    'XGS 3300': {'image': 'docs/images/hardware/xgs3300/inside-numbered.jpg', 'page': 'docs/hardware/xgs3300.md',
                 'drawing': 'docs/images/hardware/xgs3300/board-map.svg', 'measured': 'docs/measurements/xgs3300.md'},
}

# The vendor's own figures, as the datasheet prints them, by edition; the newest first.
METRICS = ['fw', 'imix', 'latency', 'ips', 'tp', 'ngfw', 'conns', 'new', 'ipsec', 'ipsec_tunnels', 'sslvpn_tunnels',
           'tls', 'tls_conns']


def _figures(*values):
    return dict(zip(METRICS, values))


VENDOR = {
    'XGS 87': [('br2024-87', _figures('3,850 Mbps', '3,000 Mbps', '6 µs', '1,200 Mbps', '850 Mbps', '700 Mbps', '1,600,000',
                                      '35,700', '3,000 Mbps', '500', '500', '375 Mbps', '8,192'))],
    'XGS 107': [('br2024-107', _figures('7,000 Mbps', '3,750 Mbps', '6 µs', '1,500 Mbps', '1,110 Mbps', '1,050 Mbps', '1,600,000',
                                        '44,400', '4,000 Mbps', '1,000', '1,000', '420 Mbps', '8,192'))],
    'XGS 116': [('br2024-116', _figures('7,700 Mbps', '4,500 Mbps', '8 µs', '2,500 Mbps', '2,160 Mbps', '2,000 Mbps', '1,600,000',
                                        '61,500', '4,800 Mbps', '1,500', '1,250', '650 Mbps', '8,192'))],
    'XGS 126': [('br2024-126', _figures('10,500 Mbps', '5,250 Mbps', '8 µs', '3,250 Mbps', '2,700 Mbps', '2,500 Mbps', '5,000,000',
                                        '69,900', '5,500 Mbps', '2,500', '1,500', '800 Mbps', '12,288')),
                ('ds2022-126', _figures('10,500 Mbps', '4,000 Mbps', '8 µs', '2,600 Mbps', '900 Mbps', '2,500 Mbps', '5,000,000',
                                        '69,900', '4,100 Mbps', '-', '1,500', '800 Mbps', '12,288'))],
    'XGS 136': [('br2024-126', _figures('11,500 Mbps', '6,500 Mbps', '8 µs', '4,000 Mbps', '3,000 Mbps', '3,000 Mbps', '6,400,000',
                                        '74,500', '6,350 Mbps', '2,500', '1,500', '950 Mbps', '18,432')),
                ('ds2022-126', _figures('11,500 Mbps', '4,700 Mbps', '8 µs', '3,300 Mbps', '1,000 Mbps', '3,000 Mbps', '6,400,000',
                                        '74,500', '4,800 Mbps', '-', '1,500', '950 Mbps', '18,432'))],
    'XGS 138': [('br2026', _figures('19.1 Gbps', '10.5 Gbps', '-', '5.85 Gbps', '4.75 Gbps', '5.1 Gbps', '6,550,000',
                                    '105,000', '6.6 Gbps', '2,500', '1,500', '1.7 Gbps', '18,432'))],
    'XGS 2100': [('br2026', _figures('30 Gbps', '16.5 Gbps', '6 µs', '6 Gbps', '5 Gbps', '5 Gbps', '6,500,000',
                                     '134,700', '17 Gbps', '5,000', '2,500', '1.1 Gbps', '18,432'))],
    'XGS 2300': [('br2026', _figures('39 Gbps', '20 Gbps', '4 µs', '7 Gbps', '5.5 Gbps', '6.3 Gbps', '6,500,000',
                                     '148,000', '20.5 Gbps', '5,000', '2,500', '1.45 Gbps', '18,432'))],
    'XGS 3100': [('br2026', _figures('47 Gbps', '23.5 Gbps', '4 µs', '10.5 Gbps', '7.4 Gbps', '9 Gbps', '12,260,000',
                                     '186,500', '25 Gbps', '6,500', '5,000', '2.47 Gbps', '55,296'))],
    'XGS 3300': [('br2026', _figures('58 Gbps', '27 Gbps', '4 µs', '14 Gbps', '10 Gbps', '12.5 Gbps', '13,700,000',
                                     '257,800', '31.1 Gbps', '6,500', '5,000', '3.13 Gbps', '102,400'))],
}

# The vendor's physical sheet: storage, the module slots, the box, its power, PoE and Wi-Fi.
# 'psu' is (where, watts or None); 'power' is (idle, most) for the model and, after it, its w twin.
_DESK = {'mount': 'desktop', 'console': ['COM (RJ45)', 'Micro-USB'], 'io': ['USB 2.0', 'USB 3.0']}
_ONEU = {'mount': '1U', 'console': ['MGMT (RJ45)', 'COM (RJ45)', 'Micro-USB'], 'io': ['2 x USB 3.0', 'USB 2.0'],
         'slots': 1, 'psu': ('internal', None), 'size': '438 x 44 x 405 mm', 'weight': '4.7 kg', 'display': 'LCD',
         'bypass': '1 pair'}
PHYSICAL = {
    'XGS 87': dict(_DESK, source='br2024-87', storage='16 GB eMMC', slots=0, size='230 x 44 x 205.5 mm', weight='1.36 kg',
                   psu=('external', '60 W'), power=[('23.2 W', '43.4 W'), ('27.1 W', '46.8 W')], wifi='Wi-Fi 5, 2x2'),
    'XGS 107': dict(_DESK, source='br2024-107', storage='64 GB SSD', slots=0, size='230 x 44 x 205.5 mm', weight='1.4 kg',
                    psu=('external', '60 W'), power=[('26.1 W', '53.9 W'), ('29.8 W', '57.3 W')], wifi='Wi-Fi 5, 2x2'),
    'XGS 116': dict(_DESK, source='br2024-116', storage='64 GB SSD', slots=1, size='320 x 44 x 213 mm', weight='2.2 kg',
                    psu=('external', '150 W'), power=[('28 W', '57 W'), ('30 W', '60 W')], wifi='Wi-Fi 5, 2x2',
                    poe='1 x GE, 30 W'),
    'XGS 126': dict(_DESK, source='br2024-126', storage='64 GB SSD', slots=1, size='320 x 44 x 213 mm', weight='2.4 kg',
                    psu=('external', '150 W'), power=[('30 W', '59 W'), ('32 W', '62 W')], wifi='Wi-Fi 5, 3x3',
                    poe='2 x GE, 30 W a port'),
    'XGS 136': dict(_DESK, source='br2024-126', storage='64 GB SSD', slots=1, size='320 x 44 x 213 mm', weight='2.4 kg',
                    psu=('external', '150 W'), power=[('30 W', '62 W'), ('32 W', '65 W')], wifi='Wi-Fi 5, 3x3',
                    poe='2 x 2.5 GE, 30 W a port'),
    'XGS 138': dict(_DESK, source='br2026', storage='64 GB M.2', slots=1, size='320 x 44 x 212 mm', weight='2.4 kg',
                    psu=('external', '150 W'), power=[('33 W', '51 W')], poe='2 x 2.5 GE, 30 W a port'),
    'XGS 2100': dict(_ONEU, source='br2026', storage='120 GB SATA SSD or more', power=[('43 W', '162 W')]),
    'XGS 2300': dict(_ONEU, source='br2026', storage='120 GB SATA SSD or more', power=[('45 W', '167 W')]),
    'XGS 3100': dict(_ONEU, source='br2026', storage='240 GB SATA SSD or more', power=[('50 W', '182 W')]),
    'XGS 3300': dict(_ONEU, source='br2026', storage='240 GB SATA SSD or more', power=[('50 W', '201 W')]),
}

# The coprocessor's module bay - the 1U boards' bay A, the larger boards' A and B or A to D: every
# module the vendor's board files describe, restated from its template (xgs-bsp/data, the module's
# own assembly) - each port's number in the bay, its socket, speed and the PHY between it and the
# coprocessor (none: the cage is the coprocessor's own), whether it carries PoE or is one of a
# bypass pair - and the name the vendor's datasheet gives it, which is how the datasheet's list of
# modules for each appliance is matched to an assembly.
def _ports(spec):
    """'4:SFP+:10G:88X5113 ...' - number, socket, speed, PHY ('-' for none), then p (PoE), b (bypass)."""
    out = []
    for item in spec.split():
        n, socket, speed, phy = item.split(':')[:4]
        flags = item.split(':')[4:]
        out.append({'n': int(n), 'socket': socket, 'speed': speed, 'phy': None if phy == '-' else phy,
                    'poe': 'p' in flags, 'bypass': 'b' in flags})
    return out


MODULES = {
    'AMDA0106-0001': ('8 x 1000Base-T', '8 port GE copper', 'phymod',
                      _ports(' '.join('%d:RJ45:1G:88E1543' % n for n in range(8, 0, -1)))),
    'AMDA0108-0001': ('8 x 1000Base-SX (SFP)', '8 port GE SFP fiber', 'phymod',
                      _ports(' '.join('%d:SFP:1G:-' % n for n in range(8, 0, -1)))),
    'AMDA0207-0001': ('4 x 10GBase-SR (SFP+)', '4 port 10 GE SFP+ fiber', 'phymod',
                      _ports(' '.join('%d:SFP+:10G:88X5113' % n for n in range(4, 0, -1)))),
    'AMDA0109-0001': ('4 x 1000Base-T, fail-to-wire', '4 port GE copper bypass (2 pairs)', 'phymod',
                      _ports(' '.join('%d:RJ45:1G:88E1543:b' % n for n in range(4, 0, -1)))),
    'AMDA0124-0001': ('4 x 1000Base-T, 4 x 1000Base-T PoE', '4 port GE copper PoE + 4 port GE copper', 'phymod',
                      _ports(' '.join(['%d:RJ45:1G:88E1543:p' % n for n in range(8, 4, -1)] +
                                      ['%d:RJ45:1G:88E1543' % n for n in range(4, 0, -1)]))),
    'AMDA0214-0001': ('2 x NBase-T (RJ45), 2 x 10GBase-SR (SFP+)', '2 port 10 GE NBASE-T copper + 2 port 10 GE SFP+ fiber', 'phymod',
                      _ports('4:SFP+:10G:88X5113 3:SFP+:10G:88X5113 1:RJ45:10G:MVL3610 2:RJ45:10G:MVL3610')),
    'AMDA0110-0001': ('4 x 1000Base-SX (SFP), 2 x 1000Base-SX fail-to-wire', '2 port GE Fiber (LC) bypass + 4 port GE SFP Fiber', 'phymod',
                      _ports('1:SFP:1G:-:b 2:SFP:1G:-:b 6:SFP:1G:- 5:SFP:1G:- 4:SFP:1G:- 3:SFP:1G:-')),
    'AMDA0108-0002': ('8 x 10GBase-SR (SFP+)', '8 port 10 GE SFP+ fiber', 'phymod',
                      _ports(' '.join('%d:SFP+:10G:-' % n for n in range(8, 0, -1)))),
    'AMDA0110-0002': ('4 x 10GBase-SR (SFP+), 2 x 10GBase-SR fail-to-wire', '2 port 10 GE Fiber (LC) bypass + 4 port 10 GE SFP+ Fiber', 'phymod',
                      _ports('1:SFP+:10G:-:b 2:SFP+:10G:-:b 6:SFP+:10G:- 5:SFP+:10G:- 4:SFP+:10G:- 3:SFP+:10G:-')),
    'AMDA0112-0001': ('2 x 40GBase-SR4 (QSFP)', '2 port 40 GE QSFP+ fiber', 'phymod',
                      _ports('2:QSFP:40G:- 1:QSFP:40G:-')),
    'AMDA0213-0001': ('12 x 1000Base-T, 4 x 2.5GBase-T', 'High-density module (NIC): 12 port GE copper + 4 port 2.5 GE copper', 'nicmod',
                      _ports(' '.join(['%d:RJ45:2.5G:AQR412' % n for n in range(13, 17)] +
                                      ['%d:RJ45:1G:88E1543' % n for n in range(12, 0, -1)]))),
    'AMDA0105-0001': ('4 x 2.5GBase-T PoE', None, 'phymod',
                      _ports(' '.join('%d:RJ45:2.5G:AQR412:p' % n for n in range(4, 0, -1)))),
    'AMDA0110-0003': ('2 x 10GBase-SR fail-to-wire', None, 'phymod', _ports('1:SFP+:10G:-:b 2:SFP+:10G:-:b')),
    'AMDA0112-0002': ('1 x 40GBase-SR4 (QSFP)', None, 'phymod', _ports('1:QSFP:40G:-')),
}

# Fitted and run here: the module, the appliance, what was seen.
MODULES_RUN = {'AMDA0207-0001': 'XGS 3300'}

# Each appliance's module bays and the modules its datasheet lists for them (br2026). The 1U boards
# and the XGS 4300 and 4500 list the same seven; the 2U ones nine, the 40G and 8 x 10G among them.
_FITS_1U = ['AMDA0106-0001', 'AMDA0108-0001', 'AMDA0207-0001', 'AMDA0109-0001', 'AMDA0124-0001', 'AMDA0214-0001',
            'AMDA0110-0001']
_FITS_2U = ['AMDA0106-0001', 'AMDA0108-0001', 'AMDA0207-0001', 'AMDA0109-0001', 'AMDA0112-0001', 'AMDA0108-0002',
            'AMDA0110-0001', 'AMDA0110-0002', 'AMDA0213-0001']
BAYS_FIT = {
    'XGS 2100': ('A', _FITS_1U), 'XGS 2300': ('A', _FITS_1U), 'XGS 3100': ('A', _FITS_1U), 'XGS 3300': ('A', _FITS_1U),
    'XGS 4300': ('A, B', _FITS_1U), 'XGS 4500': ('A, B', _FITS_1U),
    'XGS 5500': ('A-D', _FITS_2U), 'XGS 6500': ('A-D', _FITS_2U), 'XGS 7500': ('A-D', _FITS_2U), 'XGS 8500': ('A-D', _FITS_2U),
}

# The desktops' bay is the host's, not the coprocessor's: what the vendor's datasheet sells for it.
HOST_BAY = {
    'XGS 116': ('br2024-116', ['3G/4G', '5G', 'Wi-Fi (116w)']),
    'XGS 126': ('br2024-126', ['3G/4G', '5G', 'Wi-Fi (126w)']),
    'XGS 136': ('br2024-126', ['3G/4G', '5G', 'Wi-Fi (136w)']),
    'XGS 138': ('br2026', ['5G']),
}
