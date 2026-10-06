#!/usr/bin/env python3
"""Print the WHOLE of one function from a disassembly dump, or refuse to print any of it.

WHY THIS EXISTS. Two claims were published on 2026-10-06 and corrected the same day, and both came
from reading part of something. One read a function's first 258 instructions out of 506 and reported
what it "calls exactly"; the other counted call sites in a symbol table without asking whether the
live path would be a symbol at all. Neither was a wrong inference from complete evidence. Both were
complete-sounding inferences from partial evidence, which is the harder failure because nothing in the
output says it is partial.

So this tool does the bounding. It finds the function's header, finds the next header after it, and
prints everything between - then states the declared size, the number of instruction lines it printed,
and whether those agree. A disagreement is printed loudly rather than left for the reader to notice.

    tools/disasm-func.py DUMP NAME              the whole function, with a coverage line
    tools/disasm-func.py DUMP NAME --calls      every call it makes, direct and indirect, resolved
    tools/disasm-func.py DUMP 0x4477d8          by address instead of by name
    tools/disasm-func.py DUMP NAME --head 40    the first 40 lines, LABELLED AS PARTIAL

WHAT IT WILL NOT DO. It will not print a silent excerpt. --head exists because sometimes a glance is
all that is wanted, and it prints PARTIAL on every line of its own header so that a copied snippet
carries the warning with it.

THE OTHER HALF OF THE LESSON, which this tool cannot enforce. A symbol with no call site proves a
path absent only if that path would have been a symbol. In a statically linked DPDK the hot paths are
`static inline` in headers - rte_cryptodev_enqueue_burst and rte_cryptodev_dequeue_burst among them -
so they are not functions in the binary at all, while unused library functions ARE present as dead
code. --calls therefore reports indirect calls separately and never claims a function makes none.

THE DUMP FORMAT this expects is the one in this project's own captures:

    == NAME   [.text +0xADDR]   NNN bytes
      <6-hex-addr>  <mnemonic>  <operands>

A dump is a capture and stays out of this repository; this tool is a tool and lives in it.
"""
import argparse
import re
import sys

HEADER = re.compile(r"^== (?P<name>\S+)\s+\[\.(?P<sect>\w+) \+0x(?P<addr>[0-9a-f]+)\]\s+(?P<size>\d+) bytes")
INSN = re.compile(r"^\s{2,}(?P<addr>[0-9a-f]{4,16})(?::)?\s+(?P<mnem>\S+)(?:\s+(?P<ops>.*?))?\s*$")
DIRECT = re.compile(r"^\s*bl\b")
INDIRECT = re.compile(r"^\s*(blr|br)\b")
TARGET = re.compile(r"#0x(?P<addr>[0-9a-f]+)")


def index_headers(path):
    """Every function header, in file order: (line_no, name, addr, size)."""
    out = []
    with open(path, encoding="utf-8", errors="replace") as fh:
        for n, line in enumerate(fh, 1):
            m = HEADER.match(line)
            if m is not None:
                out.append((n, m["name"], int(m["addr"], 16), int(m["size"])))
    return out


def pick(headers, want):
    """The header the user meant, by name or by address. Exact name first, then a unique prefix."""
    if want.startswith("0x"):
        a = int(want, 16)
        hit = [h for h in headers if h[2] == a]
    else:
        hit = [h for h in headers if h[1] == want]
        if not hit:
            hit = [h for h in headers if h[1].startswith(want)]
    if not hit:
        return None, "no function matches %s" % want
    if len(hit) > 1:
        names = ", ".join(h[1] for h in hit[:6])
        return None, "%s matches %d functions, name one: %s" % (want, len(hit), names)
    return hit[0], None


def body(path, start_line, next_line):
    """The lines strictly between this header and the next one."""
    out = []
    end = next_line if next_line is not None else float("inf")
    with open(path, encoding="utf-8", errors="replace") as fh:
        for n, line in enumerate(fh, 1):
            if n <= start_line:
                continue
            if n >= end:
                break
            out.append(line.rstrip("\n"))
    return out


def resolve(headers, addr):
    for _, name, a, size in headers:
        if a == addr:
            return name
    return None


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("dump")
    ap.add_argument("name", help="function name, a unique prefix of one, or 0xADDR")
    ap.add_argument("--calls", action="store_true", help="list the calls instead of the body")
    ap.add_argument("--head", type=int, default=0, metavar="N",
                    help="print only the first N instruction lines, labelled PARTIAL")
    args = ap.parse_args()

    headers = index_headers(args.dump)
    if not headers:
        print("no function headers in %s - is it the right dump?" % args.dump)
        return 2

    hdr, err = pick(headers, args.name)
    if hdr is None:
        print(err)
        return 2
    line_no, name, addr, size = hdr

    after = [h[0] for h in headers if h[0] > line_no]
    lines = body(args.dump, line_no, after[0] if after else None)

    insns = [l for l in lines if INSN.match(l)]
    expect = size // 4
    agree = len(insns) == expect

    print("== %s   [.text +0x%x]   %d bytes" % (name, addr, size))
    print("   declared %d bytes = %d instructions; this bounding found %d%s"
          % (size, expect, len(insns), "" if agree else "  <-- THEY DISAGREE, do not trust either"))

    if args.head:
        print("   PARTIAL: the first %d of %d instruction lines. Any claim from this is PARTIAL."
              % (min(args.head, len(insns)), len(insns)))
        for l in insns[:args.head]:
            print(l)
        return 0

    if not args.calls:
        for l in lines:
            print(l)
        return 0

    direct, indirect = [], []
    for l in insns:
        m = INSN.match(l)
        rest = "%s %s" % (m["mnem"], m["ops"] or "")
        if INDIRECT.match(rest):
            indirect.append((m["addr"], rest.strip()))
        elif DIRECT.match(rest):
            t = TARGET.search(m["ops"] or "")
            if t is None:
                direct.append((m["addr"], rest.strip(), None))
            else:
                a = int(t["addr"], 16)
                direct.append((m["addr"], rest.strip(), (a, resolve(headers, a))))

    print("   %d direct call(s), %d indirect" % (len(direct), len(indirect)))
    seen = {}
    for _, _, tgt in direct:
        if tgt is None:
            continue
        a, nm = tgt
        seen[a] = (nm, seen.get(a, (None, 0))[1] + 1)
    for a in sorted(seen):
        nm, count = seen[a]
        print("   bl   0x%-8x x%-3d %s" % (a, count, nm if nm else "(not a listed function: a PLT stub or unnamed)"))
    for at, text in indirect:
        print("   %s  at 0x%s  <- a pointer: read where it was loaded from, the callee is not named here"
              % (text, at))
    if not indirect:
        print("   no indirect call in this function - but an inline callee would not appear as one either")
    return 0


if __name__ == "__main__":
    sys.exit(main())
