# Kernel sources carried onto the appliance

Drop the OPNsense kernel-source tarball for an appliance's kernel in here, and `install/install.sh`
copies it to `/usr/local/share/os-xgs-npu/kernel-sources/`, where `fetch-sources.sh` uses it in
preference to the network — on the install itself and on every kernel update afterwards.

**Why this exists.** On the XGS 3300 the WAN is one of the coprocessor's own front ports, so there is
no internet until `octep` is built, and `octep` cannot be built without the sources for the running
kernel. A fresh install is inside that circle, and so is any update whose new kernel this appliance
has never built for. Carrying the sources is what breaks it.

The archives themselves are **not** in this repository: a `sys/` tree is about 350 MB and belongs
beside a release image, not in git. `.gitignore` keeps them out.

## Getting one

The kernel names the commit it was built from, and that is what the archive is keyed on:

```sh
uname -v
# FreeBSD 15.1-RELEASE-p3 stable/26.7-n283949-083dc7025377 SMP
#                                                ^^^^^^^^^^^^ the commit
fetch -o kernel-sources/src-083dc7025377.tar.gz \
    https://codeload.github.com/opnsense/src/tar.gz/083dc7025377
```

Any filename works as long as the commit is in it — `*<sha>*.tar.gz` is the glob both scripts use —
and the archive is the codeload tarball exactly as it comes down. Only `sys/` is ever extracted.

For a kernel that is not yet running, read its version off the kernel on disk instead:

```sh
strings -a /boot/kernel/kernel | grep -m1 '^FreeBSD 1.*stable/'
```

## Checking it will be used

```sh
sh contrib/npuep/fetch-sources.sh            # says "using the copy already here" and fetches nothing
DRY=1 sh install/kernel-follow.sh            # says what it would build, and changes nothing
```
