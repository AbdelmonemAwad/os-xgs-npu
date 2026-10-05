#!/usr/bin/env python3
"""Move a drifted citation's line number back onto the symbol beside it.

check-doc-references.py reports a citation whose line number no longer lands on the symbol it
names. Every edit to a header moves dozens of them, and fixing each by hand is a grep, a sed and a
chance to put the wrong number in - which happened once, when a name-table `case` line was taken for
a `#define`.

This reads the checker's own complaints and rewrites the numbers. It changes nothing else: a
complaint it cannot resolve unambiguously is printed and left alone, because a citation pointing at
the wrong line is better than one pointing confidently at a different thing.

    tools/reanchor-doc-references.py            say what would change
    tools/reanchor-doc-references.py --write    change it

WHAT IT WILL NOT DO. It will not touch a citation whose symbol appears in more than one plausible
place in the file, and it prefers a definition - `#define NAME`, `NAME(` at the start of a line, a
`struct NAME {` - over a mention. Where it cannot tell a definition from a use it says so and stops,
which is the case that put the wrong number in by hand.
"""
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

COMPLAINT = re.compile(
    r"^doc reference: (?P<doc>[^:]+):(?P<docline>\d+) cites (?P<src>[^:]+):(?P<srcline>\d+) "
    r"for `(?P<sym>[^`]+)`"
)


def candidates(path, sym):
    """Line numbers where sym is defined, best first, 1-based."""
    text = path.read_text(encoding="utf-8", errors="replace").splitlines()
    strong, weak = [], []
    for n, line in enumerate(text, 1):
        if sym not in line:
            continue
        s = line.strip()
        if (s.startswith("#define") and re.search(r"#define\s+" + re.escape(sym) + r"\b", s)) \
           or re.match(r"^" + re.escape(sym) + r"\s*\(", line) \
           or re.search(r"\b(struct|enum|union)\s+" + re.escape(sym) + r"\s*\{", s) \
           or re.search(r"^\s*(static\s+)?[A-Za-z_][\w \*]*\b" + re.escape(sym) + r"\s*\(", line):
            strong.append(n)
        else:
            weak.append(n)
    return strong or weak, bool(strong)


def main():
    write = "--write" in sys.argv
    out = subprocess.run(
        [sys.executable, str(HERE / "check-doc-references.py"), str(ROOT)],
        capture_output=True, text=True,
    ).stdout

    edits, stuck = {}, 0
    for line in out.splitlines():
        m = COMPLAINT.match(line)
        if m is None:
            continue
        src = ROOT / m["src"]
        if not src.is_file():
            print("cannot find %s" % m["src"])
            stuck += 1
            continue
        hits, strong = candidates(src, m["sym"])
        if len(hits) != 1:
            print("%s:%s cites `%s`: %d places in %s, leaving it alone"
                  % (m["doc"], m["docline"], m["sym"], len(hits), m["src"]))
            stuck += 1
            continue
        if not strong:
            print("%s:%s cites `%s`: only a mention in %s, not a definition, leaving it alone"
                  % (m["doc"], m["docline"], m["sym"], m["src"]))
            stuck += 1
            continue
        edits.setdefault(m["doc"], []).append((m["src"], int(m["srcline"]), hits[0], m["sym"]))

    if not edits and not stuck:
        print("no citation has drifted")
        return 0

    for doc, items in sorted(edits.items()):
        path = ROOT / doc
        text = path.read_text(encoding="utf-8")
        for src, old, new, sym in items:
            needle = "%s:%d" % (src, old)
            if text.count(needle) != 1:
                print("%s: `%s` cites %s in more than one place, leaving it alone"
                      % (doc, sym, needle))
                stuck += 1
                continue
            text = text.replace(needle, "%s:%d" % (src, new), 1)
            print("%s: `%s` %d -> %d" % (doc, sym, old, new))
        if write:
            path.write_text(text, encoding="utf-8", newline="\n")

    if not write:
        print("\nnothing written; pass --write")
    return 1 if stuck else 0


if __name__ == "__main__":
    sys.exit(main())
