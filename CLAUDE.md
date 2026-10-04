# Rules for anyone working on this repository

[CONTRIBUTING.md](CONTRIBUTING.md) is the guide: how the modules are built, why they are not
packaged, the house rules that come out of what has gone wrong here, and what CI does and does not
check. **Read that first.** This file does not repeat it.

What is here is the handful of rules about *publishing* - the ones that apply to a commit message, a
pull request, an issue comment, a document - because those are written from outside the tree, where
most of CI cannot see them, and they are the ones that have actually been broken.

## Nothing here credits a tool

No AI attribution anywhere: not in a commit message, a commit trailer, a pull request title or body,
an issue or review comment, a document, or a code comment. No line, no footer, no separator before
one, no link to an assistant session, no `Co-Authored-By` naming a model or a vendor.

The reason is the one behind every other rule in CONTRIBUTING.md. A reader of this repository is
asked to trust measurements taken on one appliance, and a provenance line that names a tool instead
of a person is a claim about where the evidence came from that nobody can check. Authorship here is
the person who ran the thing.

`tools/check-attribution.py` enforces it over the tree and over commit messages. **It cannot see
GitHub** - a pull request body, an issue comment and a review comment never pass through a checkout,
and both times this rule was broken it was broken there. So check those by eye before and after
posting, and if something appends a footer to what you wrote, say so plainly rather than leaving it.

## English in the code and in the documents

Source, comments, identifiers, log lines, commit messages and every document are English. Arabic
belongs in a translation catalogue where a reviewer can diff it and a translator can find it.
`tools/check-code-language.py` enforces it; CONTRIBUTING.md explains why.

## A pull request, never a push to `main`

Open a pull request, as a draft while it is still being read. `main` is what an appliance installs
from, and a change that has not been read by anyone is not something to put in front of a firewall.

## Claim only what has been tested

CONTRIBUTING.md states this and it is the rule everything else here serves. In practice, for a
change written away from the appliance: say so in the pull request, in the commit message and on the
page itself. Mark a design as a design. Do not write a number you did not measure, and do not write
"should work" - name the measurement that would settle it instead.

A document that argues from the tree cites it: the function, then the literal expression, then the
file and line. Line numbers move with every kernel update; a function and an expression do not.

## Before opening a pull request

    python3 tools/check-code-language.py .
    python3 tools/check-doc-references.py .
    python3 tools/check-private-data.py .
    python3 tools/check-datapath-constants.py
    python3 tools/check-attribution.py . --commits origin/main..HEAD

All five run in CI as well. None of them compiles a driver, and none of them can tell you whether
the coprocessor is still being written to correctly - CONTRIBUTING.md is explicit about that, and a
green tick is not evidence about behaviour.
