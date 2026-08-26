#!/usr/bin/env bash
# shellcheck disable=SC2034
#
# Boot-loader wiring (bootmodes, file_permissions) mirrors upstream archiso's
# `releng` reference profile — see syslinux/, efiboot/, grub/, airootfs/ in
# this directory, copied from https://gitlab.archlinux.org/archlinux/archiso
# configs/releng/ rather than hand-authored (Rule 1: reuse, don't reinvent).
# Branding fields (iso_name/label/publisher/application, install_dir) and the
# compression choice are NTLinux's own.

iso_name="ntlinux"
iso_label="NTLINUX_$(date --date="@${SOURCE_DATE_EPOCH:-$(date +%s)}" +%Y%m)"
iso_publisher="NTLinux <https://github.com/Dankular/NTLinux>"
iso_application="NTLinux Live/Install Media"
iso_version="$(date --date="@${SOURCE_DATE_EPOCH:-$(date +%s)}" +%Y.%m.%d)"
install_dir="ntlinux"
buildmodes=('iso')
bootmodes=('bios.syslinux' 'uefi.systemd-boot')
arch="x86_64"
pacman_conf="pacman.conf"
airootfs_image_type="squashfs"
airootfs_image_tool_options=('-comp' 'zstd' '-Xcompression-level' '19')
file_permissions=(
  ["/etc/shadow"]="0:0:400"
  ["/root"]="0:0:750"
  ["/root/.automated_script.sh"]="0:0:755"
  ["/root/.gnupg"]="0:0:700"
  ["/usr/local/bin/choose-mirror"]="0:0:755"
  ["/usr/local/bin/Installation_guide"]="0:0:755"
  ["/usr/local/bin/livecd-sound"]="0:0:755"
)
