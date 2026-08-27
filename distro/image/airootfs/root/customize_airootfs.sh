#!/usr/bin/env bash
# archiso convention: if present, mkarchiso runs this script chrooted into
# the airootfs as the last customization step, then (per convention) it
# should remove itself. See:
# https://man.archlinux.org/man/mkarchiso.8#Automatically_run_scripts
#
# Builds/installs ntloader and installs ntlinux-app from the sources staged
# by distro/image/build.sh at /root/ntloader-src and /root/ntlinux-app
# (staging is why this script can't just reach into this repo's ntloader/
# and tooling/installer/ directly - it runs inside the chroot, with no
# access to anything outside the airootfs).
set -euo pipefail

if [ -d /root/ntloader-src ]; then
    make -C /root/ntloader-src install
    rm -rf /root/ntloader-src
else
    echo "customize_airootfs.sh: /root/ntloader-src missing - was this" \
         "profile built via distro/image/build.sh?" >&2
    exit 1
fi

if [ -f /root/ntlinux-app ]; then
    install -D -m 0755 /root/ntlinux-app /usr/bin/ntlinux-app
    rm -f /root/ntlinux-app
else
    echo "customize_airootfs.sh: /root/ntlinux-app missing - was this" \
         "profile built via distro/image/build.sh?" >&2
    exit 1
fi

# systemd-binfmt.service picks up usr/lib/binfmt.d/ntlinux-pe.conf
# (already part of this airootfs overlay) automatically at boot; it's a
# static unit pulled in by sysinit.target, nothing to enable explicitly.

rm -f /root/customize_airootfs.sh
