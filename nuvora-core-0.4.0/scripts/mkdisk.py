#!/usr/bin/env python3
"""Create a new Nuvora data image. Never format an existing file or device."""
import argparse
import pathlib
import struct
import zlib

def create(path: pathlib.Path, if_missing: bool = False):
    if path.exists() and if_missing:
        if not path.is_file():
            raise SystemExit(f'Not a regular file: {path}')
        with path.open('rb') as stream:
            if stream.read(8) != b'NVSTORE1':
                raise SystemExit(f'Refusing an unrecognized existing file: {path}')
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    header = b'NVSTORE1' + struct.pack('<IIIII', 1, 512, 8, 4096, 2049)
    header += struct.pack('<I', zlib.crc32(header))
    with path.open('xb') as stream:
        stream.write(header.ljust(512, b'\0'))
        stream.truncate(8 * 1024 * 1024)
    print(f'Created 8 MiB Nuvora data image: {path}')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path', type=pathlib.Path)
    parser.add_argument('--if-missing', action='store_true')
    args = parser.parse_args()
    create(args.path, args.if_missing)
