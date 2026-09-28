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

## House rules, and the reasons

- **Claim only what has been run.** A page here says what was measured, and says plainly what was
  not. "Should work" is not a state this project records.
- **Withdraw a wrong claim in the same change that disproves it**, and keep the reasoning that
  led to it. Several pages carry a diagnosis that turned out wrong together with what withdrew
  it, because deleting the reasoning loses why anyone believed it.
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
documentation, and checks that what the documents point at still exists - every repository path,
every `dev.<driver>.0.*` sysctl and every driver constant they name.

It does **not** compile the drivers - they cannot be built on a Linux runner - so a green tick says
nothing about whether the coprocessor is still written to correctly. Nor can it check a claim about
behaviour. A page saying a module is never loaded, beside a boot hook that loads it, is a defect no
name check finds; that one is caught by opening the code the sentence is about, and it is the reason
a change to a driver should come with a pass over the pages that describe it.
