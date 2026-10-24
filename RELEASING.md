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

`MAJOR.MINOR.PATCH`, with `-alpha.N` while the project is in alpha - and the steps kept small, on the
owner's word (2026-10-18), so the numbers are not used up quickly:

| what is released | how the number moves | example |
|---|---|---|
| **a small update** - a fix, or one small improvement | a fourth part is added or raised | `0.1.0-alpha.1`, then `0.1.0-alpha.1.1`, `0.1.0-alpha.1.2` |
| **a gathered release** - improvements and additions together | the alpha number is raised, the fourth part dropped | `0.1.0-alpha.2` |
| **a stage** - a step in what the project is | the minor number is raised and the count starts again | `0.2.0-alpha.1` |
| **leaving alpha** | the owner's decision alone | `1.0.0` |

~~a fix release raises the last number: `0.1.0-alpha.1`, then `0.1.0-alpha.2`; a gathered release
raises the minor number and starts the count again: `0.2.0-alpha.1`~~ - the steps above replace these
two. Every number is valid semantic versioning and sorts as it should - `alpha.1` before `alpha.1.1`
before `alpha.2` - and the changelog, the installer, the Updates page and the release badge take it as
it is.

- the release is named by its number and nothing else, and ~~while it carries `-alpha` it is
  published as a pre-release~~ it is published as the latest release - the `-alpha` in the number is
  what says alpha. A pre-release is never GitHub's latest, so the repository's page showed only its
  tags (2026-10-18).

Leaving alpha - `1.0.0` - is the owner's decision~~, the day the notice at the top of the README is
lifted~~. The work-in-progress notice was lifted on 2026-10-18 without it: the main part of the work is
done, and the number stays alpha until the owner says otherwise.

## How

1. Make sure every change since the last release is merged and its checks passed.
2. On a branch: `python3 tools/changelog.py --release 0.1.0-alpha.1.1` - this moves what the commits
   since the last tag say into a section of its own, dated today, under the right headings. Read it,
   and correct a line that says less than the change did.
3. Open a pull request titled `Release 0.1.0-alpha.1.1` with `CHANGELOG.md` and nothing else.
4. Merge it once its checks pass. On that push the release workflow (`.github/workflows/release.yml`)
   finds a version in `CHANGELOG.md` that has no tag yet, tags the merge commit `v0.1.0-alpha.1.1` and
   publishes the release with that version's section as its notes.

The Updates page on every appliance offers it from then on.

## What a release is not

- A push to `main` never makes one: only a new version heading in `CHANGELOG.md` does.
- A tag pushed by hand is not the way either: the workflow owns the tags, so the changelog and the
  releases cannot disagree.
- Nothing in a release is claimed beyond what its pages measured; the changelog links each change to
  the pull request that carries its evidence.
- The site is not in a release. A change to npuforge.org (`website/`) or to the problem reports'
  relay (`relay/`) alone is deployed by its own workflow, is left out of the changelog and of what
  makes a release due, and is not in the archive an appliance downloads (`.gitattributes`,
  `export-ignore`). Since 2026-10-22.
- Nor is anything an appliance does not read: the documentation and its photographs, the workflows,
  the files about the project itself, and the repository's own tools. What an appliance carries -
  an update's tarball, and an image's copy since xgs-image-builder takes `git archive` - is the
  driver, the plugin, their install, and the tools run on a firewall; `.gitattributes` lists the
  rest and why. A file an appliance starts to read must leave that list in the same change.
  Since 2026-10-22.
- ~~A release gets its ready-made images with nothing done here: within six hours xgs-image-builder's
  `release-images.yml` sees the new tag,~~ A main release - a gathered one, a stage, or leaving alpha
  - gets its ready-made images with nothing done here: the release workflow starts xgs-image-builder's
  `release-images.yml` as it publishes (with the secret `IMAGES_DISPATCH_TOKEN`; without it, the
  builder's own look every six hours finds the release), which builds four images with it - serial,
  38400 and 115200 baud, each with the front panel plugin and without - and publishes them as its
  release `images-<tag>`, which [build.npuforge.org](https://build.npuforge.org/) offers first. Since
  2026-10-23; a main release at once, and a small release - its number has a fourth part - with no
  images of its own, since 2026-10-24 on the owner's word. An appliance installed from the images of
  a main release takes the small releases after it from XGS: Updates.
