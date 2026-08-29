#!/bin/bash
# assemble-rosntcell-boot.sh — assembles build-reactos-core.sh's real
# ntoskrnl.exe/hal.dll/freeldr.sys into an actual bootable ROS-NTCELL
# El Torito CD image, and (with --test) boots it live under QEMU and
# proves it with a real serial-console kernel-boot trace
# (ROADMAP.md Phase 13's remaining task).
#
# Reuses ReactOS's OWN boot-image-assembly machinery throughout (Rule 1)
# rather than hand-rolling an ISO/registry format:
#   - native-mkisofs + native-isohybrid + the real isoboot.bin/isombr.bin
#     El Torito boot sectors (boot/freeldr/bootsect/) — the exact tools
#     and El Torito option set ReactOS's own `ninja bootcd` target uses
#     (boot/boot_images.cmake), just fed a hand-picked minimal file list
#     instead of the full desktop-shell `livecd` file list `bootcd`
#     normally depends on (that full list is what actually needs the
#     desktop-shell components that don't build — see
#     build-reactos-core.sh's README for why `bootcd` itself isn't used
#     directly).
#   - native-mkhive, fed ReactOS's own real boot/bootdata/*.inf registry
#     description files, for the SYSTEM/SOFTWARE/DEFAULT/SAM/SECURITY
#     hives (`ninja livecd_hives` — the exact command
#     create_registry_hives() in sdk/cmake/CMakeMacros.cmake runs). No
#     hand-built registry hive format.
#   - freeldr.ini's [NTCell] entry is a trimmed copy of the real
#     BootType=Windows2003 / SystemPath=\reactos entry shape
#     boot/bootdata/bootcd.ini's own "LiveImg_Debug" entry uses.
#
# What's hand-assembled (the genuinely new part, since ROS-NTCELL's
# whole point is a smaller, non-desktop file list than `bootcd` ships):
# the -path-list file below, and the specific driver set it names.
#
# HOW THAT DRIVER SET WAS FOUND — real, empirical, live iteration, not
# guessed: started from just ntoskrnl.exe + hal.dll + freeldr.sys (the
# three build-reactos-core.sh binaries) on the CD, booted it under QEMU,
# read the real error FreeLoader/ntoskrnl printed, added exactly the one
# real file that error named, and repeated. Twenty-some iterations of
# that loop, each one a real QEMU boot with real output (not inferred
# from source reading), produced the exact list below:
#   1. freeldr.ini itself was missing at first — "Unable to load second
#      stage loader" (freeldr.sys's own pcat bootstrap loads
#      loader\rosload.exe as its real "second stage"; add it).
#   2. NLS files: ntoskrnl's loader wants the ANSI/OEM codepage and
#      Unicode case tables (c_1252.nls, c_437.nls, l_intl.nls, +7 more
#      unconditionally-shipped ones from media/nls/CMakeLists.txt)
#      before it will even open the registry hive's file for real.
#   3. kdcom.dll + bootvid.dll: real, unconditional ntoskrnl.exe PE
#      import-table dependencies (KD transport + boot video), not
#      optional even with /NOGUIBOOT.
#   4. Sixteen real Start=0 (boot-start) services out of
#      boot/bootdata/hivesys.inf's ~3500 lines (found via `grep
#      '"Start",0x00010001,0x00000000'`, not guessed): swenum, sacdrv,
#      mup, ndis, nmidebug, usbhub, usbehci, usbohci, usbuhci, usbstor,
#      usbccgp, mountmgr, acpi, pci, ramdisk, disk.
#   5. Beyond that, QEMU's own emulated i440fx/PIIX3 hardware — once
#      acpi.sys + pci.sys actually enumerate it — makes the real PnP
#      manager dynamically pull in more drivers via the SYSTEM hive's
#      CriticalDeviceDatabase (isapnp for the ISA bridge, pciide/atapi/
#      cdrom for the virtual IDE/CD-ROM, i8042prt for the PS/2
#      controller, vga for the emulated VGA card, wdf01000/wdfldr since
#      several of the above are KMDF drivers) — each one, again, added
#      only after a real boot named it as missing, not pre-guessed.
#   6. A handful of real transitive PE import dependencies these pull
#      in: wdfldr (wdf01000's loader), usbd/usbport (the USB drivers'
#      shared libraries), pciidex (pciide's class/port library),
#      buslogic+scsiport (BusLogic's own boot-start SCSI HBA driver and
#      its scsiport.sys dependency — present in the real hive even
#      though this VM has no BusLogic HBA; it fails to *find* the
#      hardware harmlessly once its files exist), wmilib+classpnp
#      (WMI/class-driver support atapi.sys and disk.sys both need),
#      ksecdd (kernel security support library), cdfs (CD-ROM
#      filesystem — needed for the OS's own I/O manager to keep reading
#      the boot CD, distinct from freeldr's own built-in ISO9660 reader),
#      ks (kernel streaming, swenum's real dependency).
#   7. videoprt + null/beep/fs_rec/kbdclass/mouclass/floppy/blue/msfs/
#      npfs/afd/netio: the last real batch — System-start (Start=1)
#      drivers ntoskrnl's Phase 1 init (`IoInitSystem`) pulls in once
#      boot-start init finishes ("BOOT DRIVERS LOADED" in the real log
#      below), needed to get past that point rather than hang the
#      headless console on a per-missing-driver dialog (see CAVEAT
#      below).
#
# CAVEAT — the missing-driver dialog and why --test can't just "answer
# it": FreeLoader's `UiMessageBox()` (real source:
# boot/freeldr/freeldr/ntldr/winldr.c's WinLdrLoadDeviceDriverAndImports
# error path) blocks for a keypress on EVERY missing boot driver, even
# though the underlying failure is non-fatal and it would otherwise just
# skip that one driver and continue. With no VGA device attached (this
# box is headless) FreeLoader's own console falls back to COM1, and
# QEMU's `-serial file:...` backend is write-only — no keypress ever
# arrives, so any *remaining* missing driver hangs the whole boot
# indefinitely rather than failing loudly. This is why the list above
# had to be driven all the way to zero missing-boot/PnP-driver errors
# empirically (verified via a second, throwaway `-vga std` + QMP
# screendump boot for the early iterations, before the COM1 path was
# trusted) rather than stopping at "probably good enough".
#
# WHAT ACTUALLY STOPS THIS FROM BOOTING FURTHER, and why that's not a
# missing-driver gap to keep closing: once every boot/PnP-triggered
# *driver* loads clean, ntoskrnl's Phase 1 init reaches the one thing no
# driver list can supply — creating the initial user-mode process,
# smss.exe. ROS-NTCELL deliberately ships no user-mode Windows
# executables at all (ARCHITECTURE.md section 15: no Explorer/Winlogon/
# shell — this takes that further, to *no user-mode whatsoever*, not
# even smss.exe). Real Windows/ReactOS kernels are not designed to
# complete boot without it: `KeBugCheckEx` fires bug check 0x6B
# (PHASE1_INITIALIZATION_FAILED) with STATUS_OBJECT_NAME_NOT_FOUND
# (0xc0000034) as soon as the smss.exe open fails — a real, well-known
# NT bug check, not a ROS-NTCELL-specific failure. See --test's real
# captured output for exactly where this happens; that is this pass's
# honest stopping point, not a bug still being chased.
set -euo pipefail

REACTOS_SRC="${1:-./reactos-src}"
OUTDIR="${REACTOS_SRC}/output-MinGW-amd64"
BUILD_DIR="${2:-./rosntcell-build}"

if [ ! -f "$OUTDIR/ntoskrnl/ntoskrnl.exe" ]; then
    echo >&2 "assemble-rosntcell-boot.sh: $OUTDIR/ntoskrnl/ntoskrnl.exe not found — run build-reactos-core.sh $REACTOS_SRC first."
    exit 1
fi

mkdir -p "$BUILD_DIR"
cd "$OUTDIR"

# ---------------------------------------------------------------------
# 1. Build every real ReactOS target this image's boot path actually
#    named as missing, in the order discovered (see the log above) —
#    plus the real boot-sector/hive/host-tool machinery. All of this is
#    ReactOS's own source, built with the same stock mingw-w64 toolchain
#    build-reactos-core.sh already established works with no RosBE.
# ---------------------------------------------------------------------
echo >&2 "assemble-rosntcell-boot.sh: building bootsector/host-tools/registry-hive machinery..."
ninja 'boot/freeldr/bootsect/isoboot.bin' 'boot/freeldr/bootsect/isombr.bin' \
      'host-tools/bin/mkisofs' 'host-tools/bin/isohybrid' 'host-tools/bin/mkhive' \
      rosload livecd_hives

echo >&2 "assemble-rosntcell-boot.sh: building NLS tables (ANSI/OEM codepage + Unicode case tables)..."
ninja 'media/nls/c_1252.nls' 'media/nls/c_437.nls'

echo >&2 "assemble-rosntcell-boot.sh: building the real driver set found by empirical boot iteration (see header)..."
ninja kdcom bootvid \
      swenum sacdrv mup ndis nmidebug usbhub usbehci usbohci usbuhci usbstor usbccgp \
      mountmgr acpi pci ramdisk disk \
      wdf01000 wdfldr \
      isapnp pciide atapi cdrom vga i8042prt partmgr \
      usbd usbport pciidex buslogic scsiport wmilib classpnp ksecdd cdfs ks \
      videoprt null beep fs_rec kbdclass mouclass floppy blue msfs npfs afd netio

# ---------------------------------------------------------------------
# 2. freeldr.ini — a trimmed copy of boot/bootdata/bootcd.ini's real
#    "LiveImg_Debug" entry shape (BootType=Windows2003, SystemPath, and
#    the same /DEBUG /DEBUGPORT=COM1 /BAUDRATE=115200 /SOS option
#    vocabulary that ini already uses for its own debug boot entries),
#    just with our own OS-entry name and TimeOut=1 (no interactive menu
#    needed for an unattended headless boot test).
# ---------------------------------------------------------------------
cat > "$BUILD_DIR/freeldr.ini" <<'EOF'
[FREELOADER]
DefaultOS=NTCell
TimeOut=1

[Display]
TitleText=NTLinux ROS-NTCELL driver cell
MinimalUI=Yes

[Operating Systems]
NTCell="ROS-NTCELL driver cell"

[NTCell]
BootType=Windows2003
SystemPath=\reactos
Options=/DEBUG /DEBUGPORT=COM1 /BAUDRATE=115200 /SOS /FASTDETECT /MININT /NOGUIBOOT
EOF

# ---------------------------------------------------------------------
# 3. The ISO's file list — a real mkisofs -graft-points path-list, the
#    ROS-NTCELL-specific part this script hand-assembles (see header).
#    "loader/" + "reactos/system32/..." layout matches exactly what
#    isoboot.S (LOADER\FREELDR.SYS) and freeldr's SystemPath=\reactos
#    boot-type expect — both real, unmodified ReactOS conventions.
# ---------------------------------------------------------------------
PATHLIST="$BUILD_DIR/pathlist.txt"
cat > "$PATHLIST" <<EOF
loader/freeldr.sys=$OUTDIR/boot/freeldr/freeldr/freeldr.sys
loader/isoboot.bin=$OUTDIR/boot/freeldr/bootsect/isoboot.bin
loader/isombr.bin=$OUTDIR/boot/freeldr/bootsect/isombr.bin
loader/rosload.exe=$OUTDIR/boot/freeldr/freeldr/rosload.exe
freeldr.ini=$BUILD_DIR/freeldr.ini
reactos/system32/ntoskrnl.exe=$OUTDIR/ntoskrnl/ntoskrnl.exe
reactos/system32/hal.dll=$OUTDIR/hal/halx86/hal.dll
reactos/system32/kdcom.dll=$OUTDIR/drivers/base/kdrosdbg/kdcom.dll
reactos/system32/bootvid.dll=$OUTDIR/drivers/base/bootvid/bootvid.dll
reactos/system32/config/system=$OUTDIR/boot/bootdata/system
reactos/system32/config/software=$OUTDIR/boot/bootdata/software
reactos/system32/config/default=$OUTDIR/boot/bootdata/default
reactos/system32/config/sam=$OUTDIR/boot/bootdata/sam
reactos/system32/config/security=$OUTDIR/boot/bootdata/security
reactos/system32/c_1252.nls=$OUTDIR/media/nls/c_1252.nls
reactos/system32/c_437.nls=$OUTDIR/media/nls/c_437.nls
reactos/system32/l_intl.nls=$REACTOS_SRC/media/nls/l_intl.nls
reactos/system32/c_856.nls=$REACTOS_SRC/media/nls/c_856.nls
reactos/system32/c_878.nls=$REACTOS_SRC/media/nls/c_878.nls
reactos/system32/ctype.nls=$REACTOS_SRC/media/nls/ctype.nls
reactos/system32/geo.nls=$REACTOS_SRC/media/nls/geo.nls
reactos/system32/l_except.nls=$REACTOS_SRC/media/nls/l_except.nls
reactos/system32/locale.nls=$REACTOS_SRC/media/nls/locale.nls
reactos/system32/sortkey.nls=$REACTOS_SRC/media/nls/sortkey.nls
reactos/system32/sorttbls.nls=$REACTOS_SRC/media/nls/sorttbls.nls
reactos/system32/unicode.nls=$REACTOS_SRC/media/nls/unicode.nls
reactos/system32/drivers/swenum.sys=$OUTDIR/drivers/ksfilter/swenum/swenum.sys
reactos/system32/drivers/sacdrv.sys=$OUTDIR/drivers/sac/driver/sacdrv.sys
reactos/system32/drivers/mup.sys=$OUTDIR/drivers/filesystems/mup/mup.sys
reactos/system32/drivers/ndis.sys=$OUTDIR/drivers/network/ndis/ndis.sys
reactos/system32/drivers/nmidebug.sys=$OUTDIR/drivers/base/nmidebug/nmidebug.sys
reactos/system32/drivers/usbhub.sys=$OUTDIR/drivers/usb/usbhub/usbhub.sys
reactos/system32/drivers/usbehci.sys=$OUTDIR/drivers/usb/usbehci/usbehci.sys
reactos/system32/drivers/usbohci.sys=$OUTDIR/drivers/usb/usbohci/usbohci.sys
reactos/system32/drivers/usbuhci.sys=$OUTDIR/drivers/usb/usbuhci/usbuhci.sys
reactos/system32/drivers/usbstor.sys=$OUTDIR/drivers/usb/usbstor/usbstor.sys
reactos/system32/drivers/usbccgp.sys=$OUTDIR/drivers/usb/usbccgp/usbccgp.sys
reactos/system32/drivers/mountmgr.sys=$OUTDIR/drivers/storage/mountmgr/mountmgr.sys
reactos/system32/drivers/acpi.sys=$OUTDIR/drivers/bus/acpi/acpi.sys
reactos/system32/drivers/pci.sys=$OUTDIR/drivers/bus/pci/pci.sys
reactos/system32/drivers/ramdisk.sys=$OUTDIR/drivers/storage/class/ramdisk/ramdisk.sys
reactos/system32/drivers/disk.sys=$OUTDIR/drivers/storage/class/disk/disk.sys
reactos/system32/drivers/wdf01000.sys=$OUTDIR/sdk/lib/drivers/wdf/wdf01000.sys
reactos/system32/drivers/wdfldr.sys=$OUTDIR/sdk/lib/drivers/wdf/wdfldr/wdfldr.sys
reactos/system32/drivers/isapnp.sys=$OUTDIR/drivers/bus/isapnp/isapnp.sys
reactos/system32/drivers/pciide.sys=$OUTDIR/drivers/storage/ide/pciide/pciide.sys
reactos/system32/drivers/atapi.sys=$OUTDIR/drivers/storage/ide/atapi/atapi.sys
reactos/system32/drivers/cdrom.sys=$OUTDIR/drivers/storage/class/cdrom/cdrom.sys
reactos/system32/drivers/vga.sys=$OUTDIR/win32ss/drivers/miniport/vga/vga.sys
reactos/system32/drivers/i8042prt.sys=$OUTDIR/drivers/input/i8042prt/i8042prt.sys
reactos/system32/drivers/partmgr.sys=$OUTDIR/drivers/storage/partmgr/partmgr.sys
reactos/system32/drivers/usbd.sys=$OUTDIR/drivers/usb/usbd/usbd.sys
reactos/system32/drivers/usbport.sys=$OUTDIR/drivers/usb/usbport/usbport.sys
reactos/system32/drivers/pciidex.sys=$OUTDIR/drivers/storage/ide/pciidex/pciidex.sys
reactos/system32/drivers/buslogic.sys=$OUTDIR/drivers/storage/port/buslogic/buslogic.sys
reactos/system32/drivers/scsiport.sys=$OUTDIR/drivers/storage/port/scsiport/scsiport.sys
reactos/system32/drivers/wmilib.sys=$OUTDIR/drivers/wmi/wmilib.sys
reactos/system32/drivers/classpnp.sys=$OUTDIR/drivers/storage/class/classpnp/classpnp.sys
reactos/system32/drivers/ksecdd.sys=$OUTDIR/drivers/crypto/ksecdd/ksecdd.sys
reactos/system32/drivers/cdfs.sys=$OUTDIR/drivers/filesystems/cdfs/cdfs.sys
reactos/system32/drivers/ks.sys=$OUTDIR/drivers/ksfilter/ks/ks.sys
reactos/system32/drivers/videoprt.sys=$OUTDIR/win32ss/drivers/videoprt/videoprt.sys
reactos/system32/drivers/null.sys=$OUTDIR/drivers/base/null/null.sys
reactos/system32/drivers/beep.sys=$OUTDIR/drivers/base/beep/beep.sys
reactos/system32/drivers/fs_rec.sys=$OUTDIR/drivers/filesystems/fs_rec/fs_rec.sys
reactos/system32/drivers/kbdclass.sys=$OUTDIR/drivers/input/kbdclass/kbdclass.sys
reactos/system32/drivers/mouclass.sys=$OUTDIR/drivers/input/mouclass/mouclass.sys
reactos/system32/drivers/floppy.sys=$OUTDIR/drivers/storage/floppy/floppy/floppy.sys
reactos/system32/drivers/blue.sys=$OUTDIR/drivers/setup/blue/blue.sys
reactos/system32/drivers/msfs.sys=$OUTDIR/drivers/filesystems/msfs/msfs.sys
reactos/system32/drivers/npfs.sys=$OUTDIR/drivers/filesystems/npfs/npfs.sys
reactos/system32/drivers/afd.sys=$OUTDIR/drivers/network/afd/afd.sys
reactos/system32/drivers/netio.sys=$OUTDIR/drivers/network/netio/netio.sys
EOF

# ---------------------------------------------------------------------
# 4. The real El Torito ISO — same tool, same core option set as
#    boot/boot_images.cmake's own `bootcd` target (ISO_COMMON_OPTIONS +
#    ISO_BOOT_OPTIONS there), just against our own minimal path-list
#    instead of bootcd.$<CONFIG>.lst.
# ---------------------------------------------------------------------
MKISOFS="$OUTDIR/host-tools/bin/mkisofs"
ISO="$BUILD_DIR/rosntcell.iso"
echo >&2 "assemble-rosntcell-boot.sh: building $ISO..."
"$MKISOFS" -o "$ISO" \
    -iso-level 4 -publisher NTLinux -preparer NTLinux -volid ROSNTCELL -volset ROSNTCELL \
    -eltorito-platform x86 -eltorito-boot loader/isoboot.bin -no-emul-boot -boot-load-size 4 \
    -duplicates-once -no-cache-inodes -graft-points \
    -path-list "$PATHLIST"

echo >&2 "assemble-rosntcell-boot.sh: done. $ISO:"
ls -la "$ISO"

# ---------------------------------------------------------------------
# 5. --test: boot it live under QEMU (this build server is headless —
#    no /dev/kvm here either, same TCG-fallback situation
#    driver/cell/launcher/ntcell already documents) and dump the real
#    serial-console kernel-boot trace. Mirrors ntcell's boot-reactos
#    QEMU invocation shape (-M pc -m 512 -accel tcg/kvm, -serial
#    file:...), adapted to -cdrom our own image instead of the stock
#    release ISO, and to -nographic (no VGA device at all) rather than
#    -vga std + QMP screendump — deliberate, not just a headless
#    convenience: with no VGA device present, FreeLoader's own console
#    falls back to COM1 too, so /DEBUGPORT=COM1 above captures
#    FreeLoader's *and* ntoskrnl's real boot messages in one log instead
#    of splitting them across a screendump and a debug-only serial log.
#    See the CAVEAT above for why this will currently sit at the "kdb:>"
#    debugger prompt at PHASE1_INITIALIZATION_FAILED until QEMU is
#    killed — that hang is real ntoskrnl behavior (KD waiting for a
#    debugger to attach after a real bug check), not a script bug.
# ---------------------------------------------------------------------
if [ "${3:-}" = "--test" ]; then
    SERIAL_LOG="$BUILD_DIR/serial.log"
    rm -f "$SERIAL_LOG"
    accel=tcg
    [ -w /dev/kvm ] 2>/dev/null && accel=kvm
    echo >&2 "assemble-rosntcell-boot.sh: booting $ISO under QEMU (accel=$accel)..."
    timeout 90 qemu-system-x86_64 -M pc -m 512 -accel "$accel" \
        -cdrom "$ISO" -boot d \
        -display none -nographic \
        -serial "file:$SERIAL_LOG" \
        -no-reboot < /dev/null > "$BUILD_DIR/qemu-stdout.log" 2>&1 || true
    echo "--- ROS-NTCELL serial console ($SERIAL_LOG) ---"
    cat -v "$SERIAL_LOG"
    echo "--- end ---"
fi
