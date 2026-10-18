# Releasing

A release is what an appliance is updated to: the Updates page offers the newest one, and nothing
else. So a release is made on purpose - never by a push - and how soon depends on what waits.

## When

| what waits | when it is released |
|---|---|
| **a fix** - something that was broken on an appliance and is not any more | **as soon as it is merged and checked on the appliance it was broken on**: a fix waits for nothing else |
| **improvements and additions** | **gathered**, and released together once ten are waiting or the oldest has waited thirty days - whichever comes first |
| **documentation only** | with the next release of either kind; it changes nothing on an appliance |

A daily workflow (`.github/workflows/release-due.yml`) keeps one issue open, *A release is due*,
whenever either rule says so, and closes it when a release has taken what was waiting - so neither a
fix nor a batch is forgotten.

## The number

`MAJOR.MINOR.PATCH`, with `-alpha.N` while the project is in alpha:

- **a fix release** raises the last number: `0.1.0-alpha.1`, then `0.1.0-alpha.2`;
- **a gathered release** raises the minor number and starts the count again: `0.2.0-alpha.1`;
- the release is named by its number and nothing else, and while it carries `-alpha` it is
  published as a pre-release.

Leaving alpha - `1.0.0` - is the owner's decision, the day the notice at the top of the README is
lifted.

## How

1. Make sure every change since the last release is merged and its checks passed.
2. On a branch: `python3 tools/changelog.py --release 0.1.0-alpha.2` - this moves what the commits
   since the last tag say into a section of its own, dated today, under the right headings. Read it,
   and correct a line that says less than the change did.
3. Open a pull request titled `Release 0.1.0-alpha.2` with `CHANGELOG.md` and nothing else.
4. Merge it once its checks pass. On that push the release workflow (`.github/workflows/release.yml`)
   finds a version in `CHANGELOG.md` that has no tag yet, tags the merge commit `v0.1.0-alpha.2` and
   publishes the release with that version's section as its notes.

The Updates page on every appliance offers it from then on.

## What a release is not

- A push to `main` never makes one: only a new version heading in `CHANGELOG.md` does.
- A tag pushed by hand is not the way either: the workflow owns the tags, so the changelog and the
  releases cannot disagree.
- Nothing in a release is claimed beyond what its pages measured; the changelog links each change to
  the pull request that carries its evidence.
