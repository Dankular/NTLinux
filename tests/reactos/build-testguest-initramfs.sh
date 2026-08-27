#!/bin/bash
# build-testguest-initramfs.sh — builds the minimal initramfs used to
# boot the ntbridge-guest-test stand-in (see tests/reactos/README.md and
# ntbridge-guest-test.c's file header for why this exists instead of a
# real ReactOS driver cell).
#
# Contents: a static busybox (for /init's shell + mount/poweroff), the
# statically-linked ntbridge-guest-test binary, and nothing else — no
# distro, no package manager, on purpose. Uses the host's own
# vmlinuz/system busybox as raw materials the same way Phase 0 reused
# archiso's releng profile rather than hand-authoring a bootloader: real
# upstream binaries, assembled, not reimplemented (Rule 1's spirit
# applied to test infrastructure, not just NT compatibility code).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$HERE/initramfs-root"
OUT="$HERE/initramfs.cpio.gz"

rm -rf "$ROOT"
mkdir -p "$ROOT"/{bin,sbin,proc,sys,dev,tmp}

BUSYBOX="$(command -v busybox || echo /usr/bin/busybox)"
if [ ! -x "$BUSYBOX" ]; then
    echo "build-testguest-initramfs.sh: busybox not found (apt install busybox-static)" >&2
    exit 1
fi
cp "$BUSYBOX" "$ROOT/bin/busybox"

if [ ! -x "$HERE/ntbridge-guest-test" ]; then
    echo "build-testguest-initramfs.sh: run 'make' in tests/reactos/ first" >&2
    exit 1
fi
cp "$HERE/ntbridge-guest-test" "$ROOT/bin/ntbridge-guest-test"

# busybox installs its applet symlinks itself via --install; keep /init
# tiny and let busybox do that at boot rather than pre-creating dozens
# of symlinks here.
cat > "$ROOT/init" <<'EOF'
#!/bin/busybox sh
/bin/busybox --install -s /bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true

echo "[testguest-init] ntbridge test guest booted, starting ntbridge-guest-test"
DURATION=12
for arg in $(cat /proc/cmdline); do
    case "$arg" in
        ntbridge_duration=*) DURATION="${arg#ntbridge_duration=}" ;;
    esac
done
/bin/ntbridge-guest-test "$DURATION"
STATUS=$?
echo "[testguest-init] ntbridge-guest-test exited with status $STATUS"

# Make the exit status observable on the serial console even though
# there's no host-visible process exit for a QEMU guest, then power off
# so the launcher's `qemu-system-x86_64 ... -no-reboot` returns.
echo "[testguest-init] powering off"
sync
poweroff -f -d 1 2>/dev/null || echo o > /proc/sysrq-trigger
sleep 5
EOF
chmod +x "$ROOT/init"

( cd "$ROOT" && find . | cpio -o -H newc 2>/dev/null | gzip -9 > "$OUT" )

echo "build-testguest-initramfs.sh: wrote $OUT ($(du -h "$OUT" | cut -f1))"
