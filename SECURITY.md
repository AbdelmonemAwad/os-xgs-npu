# Security

## Reporting a vulnerability

**Not in a public issue, please.** Report it privately through GitHub -
[**Report a vulnerability**](https://github.com/AbdelmonemAwad/os-xgs-npu/security/advisories/new),
on the Security tab - where only you and the owner see it, and the fix can be prepared before anything is
public. Without a GitHub account, write to **eg2@live.com**. Either way, include:

- what is affected - a driver (`octep` on the XGS 3300, `npuep` on the XGS 136), the plugin's pages
  or its configd actions, the updater, the problem reporter or its relay;
- the appliance, the version - XGS: Updates shows it in the **Installed** row - and the
  OPNsense version;
- how to reproduce it, and what you saw.

This is one person's project, worked on in the evenings: expect an answer within days, not hours.
A fix is made in a pull request, named in the [CHANGELOG](CHANGELOG.md) of the release that
carries it, and the reporter credited there unless they ask not to be.

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
