#!/usr/bin/env python3
"""Create a new Nuvora data image. Never format an existing file or device."""
import argparse
import pathlib
import struct
import zlib

# Legacy NVSTORE2 snapshot geometry remains available with --legacy.
# New NVSTORE3 disks reserve two 2 MiB metadata slots and use the rest for data.
SLOT_SECTORS = 16 * 1024 * 1024 // 512 + 1
SLOT0_LBA = 8
SLOT1_LBA = SLOT0_LBA + SLOT_SECTORS
MIN_MIB = (SLOT1_LBA + SLOT_SECTORS) * 512 // (1024 * 1024) + 1
MAX_MIB = (1 << 48) * 512 // (1024 * 1024)  # ATA LBA48, not an unlimited filesystem
LARGE_SLOT_SECTORS = 128 * 1024 * 1024 // 512 + 1


def slot_sectors(capacity):
    return LARGE_SLOT_SECTORS if capacity >= SLOT0_LBA + 2 * LARGE_SLOT_SECTORS else SLOT_SECTORS


def create(path: pathlib.Path, if_missing: bool = False, size_mib: int = 8192, legacy: bool = False):
    if path.exists() and if_missing:
        if not path.is_file():
            raise SystemExit(f'Not a regular file: {path}')
        with path.open('rb') as stream:
            magic = stream.read(8)
            if magic not in (b'NVSTORE1', b'NVSTORE2', b'NVSTORE3'):
                raise SystemExit(f'Refusing an unrecognized existing file: {path}')
        return
    if size_mib < MIN_MIB or size_mib > MAX_MIB:
        raise SystemExit(f'Data image size must be {MIN_MIB}..{MAX_MIB} MiB (ATA LBA48).')
    path.parent.mkdir(parents=True, exist_ok=True)
    sectors = slot_sectors(size_mib * 2048) if legacy else 4097
    body = (b'NVSTORE2' if legacy else b'NVSTORE3') + struct.pack('<IIIII', 2 if legacy else 3, 512, SLOT0_LBA,
                                     SLOT0_LBA + sectors, sectors)
    body += struct.pack('<I', 0) + struct.pack('<Q', size_mib * 1024 * 1024 // 512)
    header = body + struct.pack('<I', zlib.crc32(body))
    with path.open('xb') as stream:
        try:
            stream.write(header.ljust(512, b'\0'))
            stream.truncate(size_mib * 1024 * 1024)
        except BaseException:
            stream.close()
            path.unlink()
            raise
    print(f'Created {size_mib} MiB Nuvora data image: {path}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path', type=pathlib.Path)
    parser.add_argument('--if-missing', action='store_true')
    parser.add_argument('--size', type=int, default=8192, metavar='MIB',
                        help='image size in MiB (default: 8192)')
    parser.add_argument('--legacy', action='store_true', help='create the old bounded snapshot format')
    args = parser.parse_args()
    create(args.path, args.if_missing, args.size, args.legacy)
