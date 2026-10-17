<!-- reviewed: 2026-10-18 -->
# How the pages are kept current

There are more than a hundred pages under `docs/`, and they are of different kinds: some describe
something that keeps changing - a driver, a family, how far along it is - and some record what was
found on a given day and are never rewritten. A page of the first kind goes stale quietly, by the tree
moving under it; one of the second kind does not go stale, but its links can break and a later page
can correct it. So every page is read again on a schedule, and how often depends on its kind.

## The line at the top of every page

    <!-- reviewed: 2026-10-18 -->

The first line of every page, invisible when it is read on GitHub: the last day someone read the page
against the tree and the appliances and either corrected it or found nothing to correct. When the line
was first put on every page, on 2026-10-18, each page got the day it was last changed before then - the
last time its author read it whole - and the pages written that day got that day. A page that was only
corrected in part that day, a row or a paragraph, kept its earlier date: it has not been read whole.

Reading a page and changing nothing still counts, and still moves the date: `tools/docs-index.py --stamp
<page>` sets it to today.

## How long a page may go unread

| kind | folder | read again every | what reading it means |
|---|---|---|---|
| the hub and the pages beside it | `docs/` | 60 days | every link still lands, every table still lists what is there |
| the guide | `docs/guide/` | 60 days | the first thing anyone reads: every claim still true of the tree and the boards; a claim that is not is struck, not deleted, with what replaced it |
| the families, the appliances, the two drivers | `docs/families/`, `docs/hardware/`, `docs/npuep/`, `docs/octep/`, `docs/octeontx/` | 90 days | the code they cite still says what they say it says - `tools/check-doc-references.py` finds a citation that has drifted, a person decides whether the claim has |
| the measurements | `docs/measurements/` | 180 days | a measurement does not change; the page says which build it was, and whether a newer measurement has replaced it is linked from it |
| the findings | `docs/findings/` | 365 days | dated records, never rewritten: links, and a pointer to any later page that corrected them |

## What keeps the schedule

- [**INDEX.md**](INDEX.md) lists every page, the date at its top and the day it is due again. It is
  written by `tools/docs-index.py` from the pages alone, and the `docs` check in CI fails when it does
  not match them - so a page added, or a date moved, without the catalogue is caught on the pull
  request.
- **Once a month** the `docs review` workflow asks the same tool which pages are due and keeps one
  issue open, titled *Pages due for review*, with them as a checklist. When none is due the issue is
  closed, and the workflow's badge on the README turns red only while some are.
- **More pages need no more of this.** The schedule is per page and by kind, so a hundred more findings
  add a hundred lines to the catalogue and, a year later, to one monthly checklist - nothing else.

## What a review does not do

It does not rewrite history. A finding that was wrong stays wrong on its page, struck through, with a
link to what replaced it; a measurement that was superseded keeps its numbers. The rule is the
repository's, stated in [README.md](README.md): nothing here is deleted to look better.
