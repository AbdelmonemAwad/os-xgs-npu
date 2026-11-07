---
date: 2026-11-07
slug: update-once-signed-releases
title: "Update once - the project's releases are signed now"
summary: "Since 0.1.0-alpha.15 every release of os-xgs-npu is signed, and an appliance installs a release only when its signature verifies with the project's release key. An appliance on 0.1.0-alpha.14 or earlier still has the updater from before, which checks no signature: update it once, and every update after that is checked. And OPNsense itself to 26.7.8 or later, for PHP2Shell."
---

# Update once - the project's releases are signed now

![Update once - the releases are signed now: update os-xgs-npu once to 0.1.0-alpha.15 or later, and OPNsense to 26.7.8 or later](../../docs/images/readme/notice-light.svg#only-light)
![Update once - the releases are signed now: update os-xgs-npu once to 0.1.0-alpha.15 or later, and OPNsense to 26.7.8 or later](../../docs/images/readme/notice-dark.svg#only-dark)

Since **0.1.0-alpha.15**, published on 2026-11-07, every release of os-xgs-npu is signed. An appliance
installs a release only when its signature verifies with the project's release key - and it is that
release which brings the check to the appliance.

<!-- more -->

## What changed

Each release carries three files beside its notes:
- its archive;
- a manifest naming the release: its tag, its commit, and the archive's SHA-256 and size;
- the manifest's Ed25519 signature.

The key that signs is the project owner's. The signing is done away from this repository and its CI.
The public half is on every appliance; its SHA-256 is
`13ed7f86a4a7669bdaee666d01894443e151da4051c1f500cb6406d7888fd33b`.

From then on, XGS: Updates:

- **offers** a release only when its manifest's signature verifies with that key;
- **reads the manifest again** before it downloads anything, and checks with GitHub that the tag still
  names the commit that was signed;
- **installs only the archive** the manifest names, with that size and that SHA-256.

Before, an appliance installed whatever GitHub - or the copy of GitHub's answers on npuforge.org - said
was the newest release. Now a release the key did not sign is neither offered nor installed, whoever
serves it. How a release is made and signed is in
[RELEASING.md](https://github.com/AbdelmonemAwad/os-xgs-npu/blob/main/RELEASING.md).

## Why update once, and soon

An appliance on **0.1.0-alpha.14 or earlier** has the updater from before signed releases, and that
updater checks no signature. So its next update is taken the old way, once. The release it installs
carries the new updater, and every update after that is checked. Until that one update, nothing on
the appliance tells a signed release from one that is not.

## What to do, on every appliance - a test one too

1. **XGS: Updates** - *Check for updates*, then *Update*. Reboot when the page asks.
2. **Check:** XGS: Updates says 0.1.0-alpha.15 or later is installed. From the next update on, its
   *Installer log* says the release is "signed with xgs-release-key-13ed7f86.pub".

## The appliance plugins

The plugins update themselves from their own repositories. Their self-updates check signatures the
same way from their next releases on: the code is in each plugin's repository, and each plugin's next
release brings it.

## The ready-made images, and OPNsense itself

The [ready-made images](https://github.com/AbdelmonemAwad/xgs-image-builder/releases) of 0.1.0-alpha.15
carry the new updater. They start from OPNsense 26.7's own image, so update OPNsense right after
installing one: since 26.7.8 OPNsense's update carries the fix for PHP2Shell
([that notice](/news/update-opnsense-php2shell/)).
