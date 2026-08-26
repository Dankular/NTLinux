#!/usr/bin/env bash
# shellcheck disable=SC2034

iso_name="ntlinux"
iso_label="NTLINUX_$(date +%Y%m)"
iso_publisher="NTLinux <https://github.com/Dankular/NTLinux>"
iso_application="NTLinux Live/Install Media"
iso_version="$(date +%Y.%m.%d)"
install_dir="ntlinux"
buildmodes=('iso')
bootmodes=('bios.syslinux.mbr' 'bios.syslinux.eltorito'
           'uefi-x64.systemd-boot.esp' 'uefi-x64.systemd-boot.eltorito')
arch="x86_64"
pacman_conf="pacman.conf"
airootfs_image_type="squashfs"
airootfs_image_tool_options=('-comp' 'zstd' '-Xcompression-level' '19')
file_permissions=(
  ["/etc/shadow"]="0:0:400"
  ["/root"]="0:0:750"
  ["/root/.automated_script.sh"]="0:0:755"
  ["/usr/local/bin/choose-mirror"]="0:0:755"
)
