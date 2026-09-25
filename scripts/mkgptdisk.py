#!/usr/bin/env python3
"""Create a NEW GPT disk with one or more independent Nuvora data partitions.

This tool never edits an existing disk. Partition count is selected before
installation; the default is one partition occupying the available space.
"""
import argparse
import pathlib
import struct
import uuid
import zlib

try:
    from .mkdisk import SLOT0_LBA, SLOT1_LBA, SLOT_SECTORS
except ImportError:  # direct execution from scripts/
    from mkdisk import SLOT0_LBA, SLOT1_LBA, SLOT_SECTORS

SECTOR = 512
MIB_SECTORS = 2048
GPT_ENTRIES = 128
GPT_ENTRY_BYTES = 128
NV_TYPE = uuid.UUID('9b431778-b821-4748-a3f8-2d17c37a5601')


def _gpt_header(lba, alternate, entries_lba, first, last, disk_guid, entries_crc):
    header = bytearray(512)
    struct.pack_into('<8sIIIIQQQQ16sQIII', header, 0, b'EFI PART', 0x10000,
                     92, 0, 0, lba, alternate, first, last, disk_guid.bytes_le,
                     entries_lba, GPT_ENTRIES, GPT_ENTRY_BYTES, entries_crc)
    struct.pack_into('<I', header, 16, zlib.crc32(header[:92]))
    return header


def create(path: pathlib.Path, if_missing=False, size_mib=64, partitions=1):
    if path.exists():
        if if_missing and path.is_file():
            with path.open('rb') as stream:
                lead = stream.read(1024)
            if lead[:8] in (b'NVSTORE1', b'NVSTORE2') or lead[512:520] == b'EFI PART':
                print(f'Existing data image preserved without repartitioning: {path}')
                return
        raise SystemExit(f'Refusing to overwrite an existing file: {path}')
    if not 1 <= partitions <= 4:
        raise SystemExit('Partition count must be between 1 and 4.')
    if size_mib < 64 or size_mib > (1 << 48) // MIB_SECTORS:
        raise SystemExit('Disk size must be 64 MiB or more, within ATA LBA48.')
    total = size_mib * MIB_SECTORS
    first, last = 2048, total - 34
    available = last - first + 1
    # All but the final partition are MiB aligned. Each needs room for both
    # 16 MiB snapshots and its NVSTORE2 sector, even on a full disk.
    chunk = available // partitions // MIB_SECTORS * MIB_SECTORS
    if chunk < SLOT1_LBA + SLOT_SECTORS:
        raise SystemExit('Disk is too small for this partition count (roughly 34 MiB each).')
    entries = bytearray(GPT_ENTRIES * GPT_ENTRY_BYTES)
    regions = []
    for i in range(partitions):
        begin = first + i * chunk
        end = first + (i + 1) * chunk - 1 if i + 1 < partitions else last
        regions.append((begin, end))
        entry = i * GPT_ENTRY_BYTES
        entries[entry:entry + 16] = NV_TYPE.bytes_le
        entries[entry + 16:entry + 32] = uuid.uuid4().bytes_le
        struct.pack_into('<QQQ', entries, entry + 32, begin, end, 0)
        label = f'Nuvora {chr(67 + i)}'.encode('utf-16le')
        entries[entry + 56:entry + 56 + len(label)] = label
    entry_crc = zlib.crc32(entries)
    guid = uuid.uuid4()
    protective = bytearray(512)
    protective[446 + 4] = 0xee
    struct.pack_into('<II', protective, 446 + 8, 1, min(total - 1, 0xffffffff))
    protective[510:512] = b'\x55\xaa'
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as out:
        try:
            out.truncate(total * SECTOR)
            out.seek(0); out.write(protective)
            out.seek(SECTOR); out.write(_gpt_header(1, total - 1, 2, first, last, guid, entry_crc))
            out.seek(2 * SECTOR); out.write(entries)
            out.seek((total - 33) * SECTOR); out.write(entries)
            out.seek((total - 1) * SECTOR)
            out.write(_gpt_header(total - 1, 1, total - 33, first, last, guid, entry_crc))
            for begin, end in regions:
                capacity = end - begin + 1
                body = b'NVSTORE2' + struct.pack('<IIIII', 2, 512, SLOT0_LBA,
                                                  SLOT1_LBA, SLOT_SECTORS)
                body += struct.pack('<I', 0) + struct.pack('<Q', capacity)
                header = body + struct.pack('<I', zlib.crc32(body))
                out.seek(begin * SECTOR); out.write(header.ljust(SECTOR, b'\0'))
        except BaseException:
            path.unlink()
            raise
    print(f'Created {size_mib} MiB GPT disk with {partitions} Nuvora partition(s): {path}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path', type=pathlib.Path)
    parser.add_argument('--if-missing', action='store_true')
    parser.add_argument('--size', type=int, default=64, metavar='MIB')
    parser.add_argument('--partitions', type=int, default=1, metavar='COUNT')
    options = parser.parse_args()
    create(options.path, options.if_missing, options.size, options.partitions)
