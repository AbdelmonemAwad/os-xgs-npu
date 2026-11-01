# Working on this

Start here if you have an appliance in front of you, or if you want to work on a family nobody
here has hardware for. Both are possible and the second is less obvious.

## The one thing to understand first

**The kernel modules are not packaged and that is deliberate.** They are C against one kernel's
headers, OPNsense ships no kernel sources, and a module built against a different kernel in the
same branch **loads without complaining** and then reads structures at the wrong offsets. The
loud refusal you would expect does not arrive.

So the modules ship as source, are built on the appliance by its own clang with every warning
fatal, and are loaded by hand. `build.sh` stamps each one with `uname -v` and `install/verify.sh`
compares the stamp rather than probing. If you change nothing else about how you work here,
carry that habit.

## With an appliance

    sh contrib/npuep/fetch-sources.sh        # pins kernel sources to the running kernel
    sh contrib/octep/build.sh                # or contrib/npuep/build.sh for ARMADA
    kldload ./octep.ko
    sh install/verify.sh

Then read [docs/families/octeon-tx-reference.md](docs/families/octeon-tx-reference.md) for the
bring-up order. The order matters more than it looks: the endpoint handshake gates the control
plane as well as the datapath, so a blank NetAgent window means "do the handshake", not
"NetAgent is broken".

**Console speeds differ by what is booted.** The host console is 38400 under the vendor firmware
and 115200 under OPNsense, on the same port. The coprocessor's own console is the host's third
UART at 115200.

## Without an appliance

More is possible here than the page count suggests. The vendor ships a fast-path binary per
family - ARMADA, OCTEON TX, OCTEON TX2 and TX2 98XX - and they carry **full symbols and DWARF**.
Most of what this project knows about the OCTEON TX control plane came out of one of those
rather than out of source. Two families have no hardware here and nobody has done that work.

See [docs/families/README.md](docs/families/README.md) for what material exists per family.

## Copyright, and what a contribution agrees to

The copyright in this repository is Abdelmonem Awad's - AUTHORS says so and every source file
carries the line - and the licence is BSD-2-Clause, in LICENSE. A licence is permission; it does not
move the copyright, and the copyright holder is the one who can change the licence later.

So that this stays so with more than one pair of hands, a contribution is taken on these terms,
and sending a pull request is agreeing to them:

- you wrote it, or have the right to give it;
- you give the copyright holder a perpetual, irrevocable, royalty-free right to use, change,
  relicense and distribute it, under this licence or any other, without owing you anything but
  the credit the licence already requires;
- you keep your own copyright and every right to use your own work elsewhere.

The reason is the licence itself. It can only be changed by whoever holds the copyright, and a
repository with ten contributors and no agreement has ten holders and cannot be relicensed
without all of them. `tools/add-copyright.py` puts the holder's line in every new source file and
the checks workflow refuses a file without one.

## House rules, and the reasons

- **Claim only what has been run.** A page here says what was measured, and says plainly what was
  not. "Should work" is not a state this project records.
- **Cite the function and the expression, then the file and line.** Line numbers move with every
  kernel update and every SDK drop; a function name and a literal expression do not. A document
  written on 2026-10-04 cited `pf.c:1871` correctly, and the appliance took a kernel update the
  same day that moved it to 1873 - the content was right and the number was not, and nothing about
  reading it would have said so. Write it as `pf_state_insert(), at LIST_INSERT_HEAD(&ih->states,
  s, entry) - sys/netpfil/pf/pf.c:1873 in stable/26.7 at 083dc7025377`, naming the tree, so the
  next reader can find it by the expression when the number has moved.
- **Withdraw a wrong claim in the same change that disproves it**, and keep the reasoning that
  led to it. Several pages carry a diagnosis that turned out wrong together with what withdrew
  it, because deleting the reasoning loses why anyone believed it.
- **No measurement is left out, and none waits.** Every figure that was taken goes into the page it
  bears on, or into [the appliance's measurements](docs/measurements/README.md), in the change
  that took it - the rows that do not fit the account being written as well as the ones that do:
  a build on the way to the merged one, one frame dropped in one rekey of nine, a tunnel that went
  down for a reason nobody looked for. A row taken on a build that turned out to be at fault is
  published as withdrawn, with why, and not dropped. The same goes for a page: it is written when
  the work is, not later. And where a figure bears on an issue, open or closed, a comment goes on
  that issue at the same time. The reason is one day's omissions, all found by being asked: a
  ten-hour run kept in a private directory, whose "64 downloads, every one cut" turned out on
  reading to be sixty and four; a day's rows held back for a page whose change was not merged;
  and a replay drop left out of the page of the very build it happened on. A measurement nobody
  else can read cannot be checked, and the ones left out are the ones that did not fit.
- **A page says when it was last read.** Every page under `docs/` opens with
  `<!-- reviewed: YYYY-MM-DD -->`; a new page gets the day it was written, and a page read against the
  tree and found right gets today with `tools/docs-index.py --stamp <page>`. Then run
  `tools/docs-index.py` and commit the `docs/INDEX.md` it writes - CI holds the catalogue to the
  pages, and [docs/REVIEW.md](docs/REVIEW.md) says how often each kind of page is read again.
- **A null reading is not a result until the instrument is proven.** Two separate measurements
  here were nearly recorded from tools that had silently failed. Grep for something you know is
  present alongside whatever you are looking for.
- **Never read BAR1 entry 15.** It is the GICD window and reading it hangs the appliance hard.
- **Never re-arm the endpoint handshake.** It runs once per coprocessor boot.
- **Do not sweep unknown message sub-codes at a live service.** A cleanly rejected input is
  evidence about that input and nothing else; a sweep here took a service down permanently.

## What CI does and does not do

It parses Python and shell, refuses Arabic written straight into code, refuses a real MAC or
serial number in a document, checks that the derived datapath constants still agree with the
documentation, checks that what the documents point at still exists - every repository path,
every `dev.<driver>.0.*` sysctl and every driver constant they name - and checks that
`docs/INDEX.md` lists every page with the date at its top. Once a month a separate workflow keeps
one issue open with the pages whose review is due.

One check it cannot run is the one for citations into somebody else's source - the
`giu_nic.c:1714` form, of which this repository now has more than twenty. CI has no vendor tree. Whoever holds one
should run it after unpacking a different SDK drop, because a new release renumbers every line of
every file while every citation here goes on reading perfectly plausibly:

    python3 tools/check-doc-references.py . --vendor path/to/the/unpacked/sources

It does **not** compile the drivers - they cannot be built on a Linux runner - so a green tick says
nothing about whether the coprocessor is still written to correctly. Nor can it check a claim about
behaviour. A page saying a module is never loaded, beside a boot hook that loads it, is a defect no
name check finds; that one is caught by opening the code the sentence is about, and it is the reason
a change to a driver should come with a pass over the pages that describe it.

## Labels, and waiting for an answer

An issue opened from a form is labelled with the model it names, and a pull request with what it
changes ([.github/workflows/labels.yml](.github/workflows/labels.yml)). When an issue waits on an
answer from you, it carries the label `waiting for reply`: a week without one brings a single reminder,
and nothing is ever closed for it ([tools/reminder.py](tools/reminder.py)).

## Conduct, and vulnerabilities

Everyone here is held to the [Code of Conduct](CODE_OF_CONDUCT.md). A vulnerability goes to the
address in [SECURITY.md](SECURITY.md), not into an issue. A problem, a measurement or an idea goes
into an issue through its form, a question into
[Discussions](https://github.com/AbdelmonemAwad/os-xgs-npu/discussions), and a pull request through
its template. Your first report from the appliance or measurement form, first problem solved or first
pull request merged is answered once by the project's bot with a thank-you
([tools/thanks.py](tools/thanks.py)).
