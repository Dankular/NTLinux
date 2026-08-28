#!/usr/bin/env python3
"""
read-fat12-file.py -- reads one file's contents out of a real FAT12
1.44MB floppy image's root directory and writes it to stdout.

Companion to write-fat12-floppy.py (see that file's own header comment
for why this exists instead of using mtools' mcopy/mtype: mtools has no
readily-fetchable no-installer Windows binary, confirmed live rather
than assumed).

Usage: read-fat12-file.py <image-path> <8.3-name, e.g. RESULT.TXT>
Exits 1 (no stdout) if the file isn't found, matching mtype's own
behavior closely enough for run-test.sh's use (empty result == fail).
"""
import struct
import sys

SECTOR_SIZE = 512
SECTORS_PER_FAT = 9
NUM_FATS = 2
ROOT_ENTRIES = 224
ROOT_DIR_SECTORS = (ROOT_ENTRIES * 32) // SECTOR_SIZE
RESERVED_SECTORS = 1
FIRST_FAT_SECTOR = RESERVED_SECTORS
FIRST_ROOT_SECTOR = FIRST_FAT_SECTOR + NUM_FATS * SECTORS_PER_FAT
FIRST_DATA_SECTOR = FIRST_ROOT_SECTOR + ROOT_DIR_SECTORS


def make_83_name(name):
    if "." in name:
        base, ext = name.rsplit(".", 1)
    else:
        base, ext = name, ""
    base = base.upper()[:8].ljust(8)
    ext = ext.upper()[:3].ljust(3)
    return (base + ext).encode("ascii")


def get_fat12(fat, entry_index):
    offset = (entry_index * 3) // 2
    if entry_index % 2 == 0:
        return fat[offset] | ((fat[offset + 1] & 0x0F) << 8)
    return (fat[offset] >> 4) | (fat[offset + 1] << 4)


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 1
    image_path, target_name = sys.argv[1], sys.argv[2]
    target_83 = make_83_name(target_name)

    with open(image_path, "rb") as f:
        image = f.read()

    fat = image[FIRST_FAT_SECTOR * SECTOR_SIZE:(FIRST_FAT_SECTOR + SECTORS_PER_FAT) * SECTOR_SIZE]
    root_start = FIRST_ROOT_SECTOR * SECTOR_SIZE
    root_dir = image[root_start:root_start + ROOT_DIR_SECTORS * SECTOR_SIZE]

    for i in range(ROOT_ENTRIES):
        entry = root_dir[i * 32:(i + 1) * 32]
        if entry[0] in (0x00, 0xE5):
            continue
        if entry[0:11] != target_83:
            continue
        first_cluster = struct.unpack_from("<H", entry, 26)[0]
        size = struct.unpack_from("<I", entry, 28)[0]

        out = bytearray()
        cluster = first_cluster
        while cluster < 0xFF8 and len(out) < size:
            data_start = (FIRST_DATA_SECTOR + (cluster - 2)) * SECTOR_SIZE
            chunk = image[data_start:data_start + SECTOR_SIZE]
            out.extend(chunk[:max(0, min(SECTOR_SIZE, size - len(out)))])
            cluster = get_fat12(fat, cluster)
        sys.stdout.buffer.write(bytes(out[:size]))
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
