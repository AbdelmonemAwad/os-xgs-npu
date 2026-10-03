#!/usr/bin/env python3
"""Print a C structure, a union or an enumeration out of a binary's DWARF.

WHY THIS IS HERE. Every structure in this project that was guessed and later corrected was
available this way the whole time: the vendor ships usfp_rh.ko with full debug information, and
the names in it are the names the far side uses. The SA block, the LIF entry and the flow
structures were all read rather than inferred, and the one time a neighbouring attribute was
reached for by counting instead - 0x0b, FEC - it stopped the far side until the coprocessor was
rebooted. Read the declaration first; it costs a second.

    tools/dwarf-struct.py <file.ko> usfp_mflow_key          one structure, anonymous members expanded
    tools/dwarf-struct.py <file.ko> --list mflow            every type whose name contains "mflow"
    tools/dwarf-struct.py <file.ko> --enum rpc_cmd_type     an enumeration, by value

Bit-fields print their offset and width; anonymous unions and structures are expanded in place at
their own offset, which is what makes a packed request readable as bytes. Needs pyelftools.
"""
import sys

try:
    from elftools.elf.elffile import ELFFile
except ImportError:
    sys.exit("needs pyelftools: pkg install py39-pyelftools, or pip install pyelftools")

TAGS = ('DW_TAG_structure_type', 'DW_TAG_union_type')


def name_of(die):
    a = die.attributes.get('DW_AT_name')
    return a.value.decode() if a else None


def type_of(cu, die):
    """The DIE a member's DW_AT_type points at, or None."""
    t = die.attributes.get('DW_AT_type')
    if t is None:
        return None
    off = t.value
    if t.form.startswith('DW_FORM_ref') and not t.form.endswith('addr'):
        off += cu.cu_offset
    try:
        return cu.get_DIE_from_refaddr(off)
    except Exception:
        return None


def members(cu, die, indent, base):
    for m in die.iter_children():
        if m.tag != 'DW_TAG_member':
            continue
        off = m.attributes.get('DW_AT_data_member_location')
        off = off.value if off else 0
        bits = m.attributes.get('DW_AT_bit_size')
        bitoff = m.attributes.get('DW_AT_data_bit_offset')
        td = type_of(cu, m)
        tn = name_of(td) if td is not None else None
        if bits is not None:
            at = base + (bitoff.value // 8 if bitoff is not None else off)
            print("%s+0x%-4x %-24s %-24s : %d" % (' ' * indent, at, tn or 'bitfield',
                                                  name_of(m), bits.value))
            continue
        print("%s+0x%-4x %-24s %s" % (' ' * indent, base + off, tn or '?',
                                      name_of(m) or '(anonymous)'))
        # An anonymous union or structure is part of this one's byte layout, so expand it here.
        if td is not None and td.tag in TAGS and name_of(m) is None:
            members(cu, td, indent + 4, base + off)


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    path = argv[1]
    mode = 'struct'
    want = argv[2]
    if want in ('--list', '--enum'):
        mode = want[2:]
        if len(argv) < 4:
            sys.exit(__doc__)
        want = argv[3]

    with open(path, 'rb') as fh:
        elf = ELFFile(fh)
        if not elf.has_dwarf_info():
            sys.exit("%s carries no DWARF" % path)
        dw = elf.get_dwarf_info()

        if mode == 'list':
            seen = set()
            for cu in dw.iter_CUs():
                for die in cu.iter_DIEs():
                    if die.tag not in TAGS and die.tag != 'DW_TAG_enumeration_type':
                        continue
                    n = name_of(die)
                    if not n or want.lower() not in n.lower() or n in seen:
                        continue
                    seen.add(n)
                    sz = die.attributes.get('DW_AT_byte_size')
                    kind = 'enum' if die.tag == 'DW_TAG_enumeration_type' else die.tag[7:-5]
                    print("%-6s %-46s %s bytes" % (kind, n, sz.value if sz else '?'))
            return 0 if seen else 1

        if mode == 'enum':
            for cu in dw.iter_CUs():
                for die in cu.iter_DIEs():
                    if die.tag != 'DW_TAG_enumeration_type' or name_of(die) != want:
                        continue
                    print("enum %s" % want)
                    for c in die.iter_children():
                        if c.tag != 'DW_TAG_enumerator':
                            continue
                        print("  %5d  %s" % (c.attributes['DW_AT_const_value'].value,
                                             c.attributes['DW_AT_name'].value.decode()))
                    return 0
            print("no enumeration named %r" % want, file=sys.stderr)
            return 1

        for cu in dw.iter_CUs():
            for die in cu.iter_DIEs():
                if die.tag not in TAGS or name_of(die) != want:
                    continue
                sz = die.attributes.get('DW_AT_byte_size')
                print("%s %s  /* %s bytes */" % (die.tag[7:-5], want,
                                                 sz.value if sz else 'incomplete'))
                members(cu, die, 2, 0)
                return 0
    print("no structure or union named %r - try --list" % want, file=sys.stderr)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
