#!/usr/bin/env python3
"""Move a drifted citation's line number back onto the symbol beside it.

check-doc-references.py reports a citation whose line number no longer lands on the symbol it
names. Every edit to a header moves dozens of them, and fixing each by hand is a grep, a sed and a
chance to put the wrong number in - which happened once, when a name-table `case` line was taken for
a `#define`.

This reads the checker's own complaints and rewrites the numbers. It changes nothing else: a
complaint it cannot resolve is printed and left alone, because a citation pointing at the wrong line
is better than one pointing confidently at a different thing.

    tools/reanchor-doc-references.py            say what would change
    tools/reanchor-doc-references.py --write    change it

HOW IT CHOOSES. A citation drifts a little - a header grew by eight lines, a function moved by
seventy - so of the places a symbol is defined, the one nearest the OLD number is the one meant.
That rule decides three cases the first version of this tool refused, each of which was then done
by hand and one of which was done wrong:

  - the document line names two symbols and the checker complains about the first - "`probe()`
    and `release()`, the latter defined at :342" - so every backticked symbol on the line (and on
    the line before it, since sentences wrap) is a candidate, and `release` at 400 wins over `probe`
    at 143 because 400 is nearer 342;
  - the symbol is a suffix of the definition - `FW_STATE_REV_SET` for
    `#define OCTEP_RPC_CMD_FW_STATE_REV_SET` - which a mention in a comment would otherwise win;
  - the symbol is a sysctl leaf - `fw_rev` - defined by its quoted name in a SYSCTL_ADD line.

WHAT IT STILL WILL NOT DO. It prefers a definition over a mention and will not move a citation onto
a mention when a definition exists anywhere in the file. When two definitions are equally near, or
when no definition is found and the mentions are many, it says so and stops. It rewrites the number
on the complained-about document line only, so two citations of one line elsewhere are untouched.
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
TICKED = re.compile(r"`([^`]{2,60})`")


def symbols_on(doc_lines, n):
    """Backticked identifiers on document line n (1-based) and the line before it: the same
    two lines the checker reads for a citation."""
    out = []
    for i in (n - 1, n - 2):
        if i < 0:
            continue
        for tok in TICKED.findall(doc_lines[i]):
            tok = tok.strip()
            if re.fullmatch(r"[A-Za-z_][\w.]*(?:\(\))?", tok) and "/" not in tok:
                out.append(tok.rstrip("()"))
    return out


def definitions(src_lines, sym):
    """(line, kind) for every place sym looks defined in the file; kind 0 is a definition,
    1 a suffix definition, 2 a mention."""
    esc = re.escape(sym)
    # A sysctl is cited by its path - `rpc.allow_write` - and defined by its quoted leaf in the
    # SYSCTL_ADD line that publishes it: `OID_AUTO, "allow_write"`. ONLY there. A quoted name
    # anywhere else is the name table - `case OCTEP_RPC_CMD_LIF_ADD_UPDATE: return
    # ("LIF_ADD_UPDATE")` - which is the line that was once taken for a definition by hand.
    leaf = re.escape(sym.rsplit(".", 1)[-1])
    published = re.compile(r'OID_AUTO,\s*"' + leaf + r'"')
    hits = []
    for n, line in enumerate(src_lines, 1):
        if sym not in line and not published.search(line):
            continue
        s = line.strip()
        if re.search(r"#define\s+" + esc + r"\b", s) \
           or re.match(r"^" + esc + r"\s*\(", line) \
           or re.search(r"\b(struct|enum|union)\s+" + esc + r"\s*\{", s) \
           or re.search(r"^\s*(static\s+)?[A-Za-z_][\w \*]*\b" + esc + r"\s*\(", line) \
           or published.search(line):
            hits.append((n, 0))
        elif re.search(r"#define\s+\w*" + esc + r"\b", s):
            hits.append((n, 1))
        else:
            hits.append((n, 2))
    return hits


def choose(src_lines, syms, old):
    """The (line, symbol) nearest the old number among the best kind available; None when the
    choice is not clear."""
    best_kind = None
    pool = []
    for sym in syms:
        for n, kind in definitions(src_lines, sym):
            pool.append((kind, abs(n - old), n, sym))
    if not pool:
        return None, "no symbol on that line is in the file"
    best_kind = min(k for k, _, _, _ in pool)
    pool = sorted(p for p in pool if p[0] == best_kind)
    if best_kind == 2 and len(pool) > 1:
        return None, "only mentions, %d of them, none a definition" % len(pool)
    if len(pool) > 1 and pool[0][1] == pool[1][1]:
        return None, "two places equally near (%d and %d)" % (pool[0][2], pool[1][2])
    _, _, n, sym = pool[0]
    return (n, sym), None


def main():
    write = "--write" in sys.argv
    out = subprocess.run(
        [sys.executable, str(HERE / "check-doc-references.py"), str(ROOT)],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    ).stdout

    edits, stuck, src_cache, doc_cache = {}, 0, {}, {}
    for line in out.splitlines():
        m = COMPLAINT.match(line)
        if m is None:
            continue
        src = ROOT / m["src"]
        if not src.is_file():
            print("cannot find %s" % m["src"])
            stuck += 1
            continue
        if src not in src_cache:
            src_cache[src] = src.read_text(encoding="utf-8", errors="replace").split("\n")
        doc = ROOT / m["doc"]
        if doc not in doc_cache:
            doc_cache[doc] = doc.read_text(encoding="utf-8").split("\n")
        docline, old = int(m["docline"]), int(m["srcline"])
        syms = symbols_on(doc_cache[doc], docline) or [m["sym"]]
        # "command 3 is `LIF_ADD_UPDATE` and command 5 is `PPORT_UPDATE` (`h:1265`, `h:1266`)":
        # as many citations as symbols on the line means the k-th cites the k-th, and distance
        # alone would send both numbers to whichever definition is nearer.
        # The symbols are on this line, or - when this line is only the wrapped citations - on
        # the line before, which is where the checker read them from too.
        here = doc_cache[doc][docline - 1]
        cites = re.findall(r"`([^`]*/[^`]*:\d+)`", here)
        own = []
        for cand in (here, doc_cache[doc][docline - 2] if docline > 1 else ""):
            own = [t.rstrip("()") for t in TICKED.findall(cand)
                   if re.fullmatch(r"[A-Za-z_][\w.]*(?:\(\))?", t.strip()) and "/" not in t]
            if own:
                break
        needle = "%s:%d" % (m["src"], old)
        if len(cites) == len(own) > 1 and sum(1 for c in cites if c == needle) == 1:
            syms = [own[cites.index(needle)]]
        pick, why = choose(src_cache[src], syms, old)
        if pick is None:
            print("%s:%d cites %s:%d for `%s`: %s, leaving it alone"
                  % (m["doc"], docline, m["src"], old, m["sym"], why))
            stuck += 1
            continue
        new, sym = pick
        edits.setdefault(doc, []).append((docline, m["src"], old, new, sym))

    if not edits and not stuck:
        print("no citation has drifted")
        return 0

    for doc, items in sorted(edits.items()):
        text = doc_cache[doc]
        for docline, src, old, new, sym in items:
            needle = "%s:%d" % (src, old)
            if needle not in text[docline - 1]:
                print("%s:%d: %s is not on that line any more, leaving it alone"
                      % (doc.relative_to(ROOT).as_posix(), docline, needle))
                stuck += 1
                continue
            text[docline - 1] = text[docline - 1].replace(needle, "%s:%d" % (src, new), 1)
            print("%s:%d: `%s` %d -> %d" % (doc.relative_to(ROOT).as_posix(), docline, sym, old, new))
        if write:
            doc.write_text("\n".join(text), encoding="utf-8", newline="\n")

    if not write:
        print("\nnothing written; pass --write")
    return 1 if stuck else 0


if __name__ == "__main__":
    sys.exit(main())
