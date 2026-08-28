#!/usr/bin/env python3
"""
write-fat12-floppy.py -- writes a standard, real FAT12 1.44MB floppy
image and copies files into its root directory, without needing
mtools (mformat/mcopy) installed.

Why this exists: run-test.sh (driver/vsdev/) needs mtools to build the
floppy image vsdev_test.exe reads VSDEV.SYS/VSDTEST.EXE from inside the
booted ReactOS guest. mtools has no maintained no-installer Windows
binary (the only zip-distributed build, GnuWin32's, is no longer
directly fetchable - SourceForge serves an HTML/cookie-consent page to
scripted requests instead of the file, confirmed live rather than
assumed), and this Windows host has no admin rights to fix
chocolatey's own broken lock-file state to install mingw/mtools that
way either (also confirmed live - see ROADMAP.md). Rather than block
on either of those, this narrowly reimplements exactly the two mtools
operations run-test.sh needs (format a blank 1.44MB FAT12 image, copy a
small number of files into its root directory) directly - real FAT12,
readable by any real FAT12 implementation including ReactOS's, not a
custom/fake format.

Usage: write-fat12-floppy.py <image-path> <file1> [file2 ...]
Each file is copied into the floppy's root directory under its
uppercased 8.3-safe basename (files here are already 8.3-safe:
VSDEV.SYS, VSDTEST.EXE).
"""
import os
import struct
import sys

SECTOR_SIZE = 512
TOTAL_SECTORS = 2880          # 1.44MB
SECTORS_PER_FAT = 9
NUM_FATS = 2
ROOT_ENTRIES = 224
ROOT_DIR_SECTORS = (ROOT_ENTRIES * 32) // SECTOR_SIZE   # 14
RESERVED_SECTORS = 1
FIRST_FAT_SECTOR = RESERVED_SECTORS
FIRST_ROOT_SECTOR = FIRST_FAT_SECTOR + NUM_FATS * SECTORS_PER_FAT
FIRST_DATA_SECTOR = FIRST_ROOT_SECTOR + ROOT_DIR_SECTORS
DATA_CLUSTERS = TOTAL_SECTORS - FIRST_DATA_SECTOR  # 1 sector/cluster


def build_boot_sector():
    b = bytearray(SECTOR_SIZE)
    b[0:3] = b"\xEB\x3C\x90"                 # jmp + nop
    b[3:11] = b"NTLINUX "                    # OEM name, 8 bytes
    struct.pack_into("<H", b, 11, SECTOR_SIZE)      # bytes/sector
    b[13] = 1                                 # sectors/cluster
    struct.pack_into("<H", b, 14, RESERVED_SECTORS)
    b[16] = NUM_FATS
    struct.pack_into("<H", b, 17, ROOT_ENTRIES)
    struct.pack_into("<H", b, 19, TOTAL_SECTORS)
    b[21] = 0xF0                              # media descriptor (1.44MB)
    struct.pack_into("<H", b, 22, SECTORS_PER_FAT)
    struct.pack_into("<H", b, 24, 18)         # sectors/track
    struct.pack_into("<H", b, 26, 2)          # heads
    struct.pack_into("<I", b, 28, 0)          # hidden sectors
    struct.pack_into("<I", b, 32, 0)          # total sectors (32-bit, unused)
    b[36] = 0x00                              # drive number
    b[37] = 0x00                              # reserved
    b[38] = 0x29                              # extended boot signature
    struct.pack_into("<I", b, 39, 0x4E544C58)  # volume serial "NTLX"
    b[43:54] = b"NTLINUXVSD ".ljust(11)[:11]  # volume label, 11 bytes
    b[54:62] = b"FAT12   "                    # fs type, 8 bytes
    b[510] = 0x55
    b[511] = 0xAA
    return bytes(b)


def make_83_name(filename):
    base, ext = os.path.splitext(os.path.basename(filename))
    base = base.upper()[:8].ljust(8)
    ext = ext.lstrip(".").upper()[:3].ljust(3)
    return (base + ext).encode("ascii")


def main():
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 1
    image_path = sys.argv[1]
    files = sys.argv[2:]

    fat = bytearray(SECTORS_PER_FAT * SECTOR_SIZE)
    # First two FAT12 entries are reserved: entry 0 = media descriptor
    # (low byte) + 0xFF, entry 1 = 0xFFF (end-of-chain marker reused).
    fat[0] = 0xF0
    fat[1] = 0xFF
    fat[2] = 0xFF

    root_dir = bytearray(ROOT_DIR_SECTORS * SECTOR_SIZE)
    data_area = bytearray(DATA_CLUSTERS * SECTOR_SIZE)

    next_free_cluster = 2  # clusters 0/1 are reserved in FAT12

    def set_fat12(entry_index, value):
        # Two 12-bit entries packed into 3 bytes, low-endian nibble order.
        offset = (entry_index * 3) // 2
        if entry_index % 2 == 0:
            fat[offset] = value & 0xFF
            fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F)
        else:
            fat[offset] = (fat[offset] & 0x0F) | ((value & 0x0F) << 4)
            fat[offset + 1] = (value >> 4) & 0xFF

    for i, path in enumerate(files):
        if i >= ROOT_ENTRIES:
            print(f"write-fat12-floppy.py: too many files (root dir holds {ROOT_ENTRIES})", file=sys.stderr)
            return 1
        with open(path, "rb") as f:
            data = f.read()
        size = len(data)
        clusters_needed = max(1, (size + SECTOR_SIZE - 1) // SECTOR_SIZE)
        if next_free_cluster + clusters_needed > DATA_CLUSTERS + 2:
            print(f"write-fat12-floppy.py: {path} ({size} bytes) doesn't fit on the floppy", file=sys.stderr)
            return 1

        first_cluster = next_free_cluster
        chain = list(range(first_cluster, first_cluster + clusters_needed))
        for j, cl in enumerate(chain):
            nxt = chain[j + 1] if j + 1 < len(chain) else 0xFFF
            set_fat12(cl, nxt)
            start = (cl - 2) * SECTOR_SIZE
            chunk = data[j * SECTOR_SIZE:(j + 1) * SECTOR_SIZE]
            data_area[start:start + len(chunk)] = chunk
        next_free_cluster += clusters_needed

        entry = bytearray(32)
        entry[0:11] = make_83_name(path)
        entry[11] = 0x20  # ARCHIVE attribute
        struct.pack_into("<H", entry, 26, first_cluster)
        struct.pack_into("<I", entry, 28, size)
        root_dir[i * 32:(i + 1) * 32] = entry

        print(f"write-fat12-floppy.py: wrote {os.path.basename(path)} "
              f"({size} bytes, cluster {first_cluster}, {clusters_needed} cluster(s))")

    image = bytearray(TOTAL_SECTORS * SECTOR_SIZE)
    image[0:SECTOR_SIZE] = build_boot_sector()
    fat_start = FIRST_FAT_SECTOR * SECTOR_SIZE
    image[fat_start:fat_start + len(fat)] = fat
    image[fat_start + len(fat):fat_start + 2 * len(fat)] = fat  # second FAT copy
    root_start = FIRST_ROOT_SECTOR * SECTOR_SIZE
    image[root_start:root_start + len(root_dir)] = root_dir
    data_start = FIRST_DATA_SECTOR * SECTOR_SIZE
    image[data_start:data_start + len(data_area)] = data_area

    with open(image_path, "wb") as f:
        f.write(image)
    print(f"write-fat12-floppy.py: wrote {image_path} ({len(image)} bytes, real FAT12)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
