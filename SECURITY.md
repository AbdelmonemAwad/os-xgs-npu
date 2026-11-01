# Security

## Reporting a vulnerability

**Not in a public issue, please.** Report it privately through GitHub -
[**Report a vulnerability**](https://github.com/AbdelmonemAwad/os-xgs-npu/security/advisories/new),
on the Security tab - where only you and the owner see it, and the fix can be prepared before anything is
public. Without a GitHub account, write to **security@npuforge.org**. Either way, include:

- what is affected - a driver (`octep` on the XGS 3300, `npuep` on the XGS 136), the plugin's pages
  or its configd actions, the updater, the problem reporter or its relay;
- the appliance, the version - XGS: Updates shows it in the **Installed** row - and the
  OPNsense version;
- how to reproduce it, and what you saw.

This is one person's project, worked on in the evenings: expect an answer within days, not hours.
A fix is made in a pull request, named in the [CHANGELOG](CHANGELOG.md) of the release that
carries it, and the reporter credited there unless they ask not to be.

## Checking what you download

What the project publishes carries GitHub's own signature, and the [GitHub CLI](https://cli.github.com/)
checks it:

- **A release of this project** cannot be changed once it is published - its tag cannot be moved -
  and GitHub signs which commit it names:
  `gh release verify v0.1.0-alpha.10.6 -R AbdelmonemAwad/os-xgs-npu`
- **A plugin's package** is a file of such a release, in the plugin's own repository, and is signed
  with it: `gh release verify-asset v1.0.6 os-frontpanel-1.0.6.pkg -R AbdelmonemAwad/os-frontpanel`
- **An image** - ready-made, or from the build page - carries a statement GitHub signs when it is
  built: that xgs-image-builder's own workflow made it, from which commit, in which run:
  `gh attestation verify <the image>.img.xz -R AbdelmonemAwad/xgs-image-builder`

## How the repository is kept

<a href="https://scorecard.dev/viewer/?uri=github.com/AbdelmonemAwad/os-xgs-npu"><img src="https://img.shields.io/ossf-scorecard/github.com/AbdelmonemAwad/os-xgs-npu?label=OpenSSF%20Scorecard" alt="OpenSSF Scorecard"></a>

Secret scanning refuses a push that carries a credential, Dependabot keeps the Actions the workflows
use current, and code scanning reads the code on every pull request. The
[OpenSSF Scorecard](https://scorecard.dev/viewer/?uri=github.com/AbdelmonemAwad/os-xgs-npu) grades
how the repository is kept - weekly, and at every change to `main` - and its findings are on the
Security tab beside code scanning's.

## What is supported

The project is in alpha. Fixes go to `main` and into the next release, with no backports: the newest
release and `main` are what is supported. An appliance follows releases through XGS: Updates.

## Worth knowing before you report

- **The drivers run in the kernel of a firewall.** A frame from the network that crashes or hangs
  one is a vulnerability, not only a bug - report it here, privately. So is anything that lets
  traffic past pf because a connection was handed to the coprocessor.
- **The modules are built on the appliance**, for its own kernel ([CONTRIBUTING.md](CONTRIBUTING.md)).
  A crash with a module built for another kernel is that first: `install/verify.sh` says which.
- **The problem reporter sends text off the appliance.** Anything it sends that names the machine or
  its networks - an address, a MAC, a serial number, a host name - is a privacy defect: report it here
  as well. What it looks at and what it removes: [docs/problem-reports.md](docs/problem-reports.md).
- **Never send a configuration backup.** It holds keys and passwords. The support bundle on XGS
  Coprocessor: Problem Reports is made without it and cleaned before it is written.
