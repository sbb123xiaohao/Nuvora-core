#!/usr/bin/env python3
"""Copy a legacy single-volume data image into a NEW NVSTORE3 GPT image.

The source is read-only. The destination must not exist; sparse copies use
bounded buffers. Multi-volume images are refused rather than dropping drives.
"""
import argparse
import pathlib
import struct
import tempfile
from import_media import read_at, geometry, snapshots, parse, partition
from mkgptdisk import create, NV_TYPE
from extent_store import layout, commit, write_source, free_ranges


def migrate(source, destination, size_mib=8192):
    source, destination = pathlib.Path(source), pathlib.Path(destination)
    if not source.is_file() or destination.exists():
        raise ValueError('Source must be a regular file and destination must not exist')
    with source.open('rb') as src:
        signature = read_at(src, 0, 8)
        if signature in (b'NVSTORE1', b'NVSTORE2'): first, sectors = 0, source.stat().st_size//512
        else:
            first, sectors = partition(src)
            h = read_at(src, 512, 512)
            table_lba, = struct.unpack_from('<Q', h, 72)
            count, width = struct.unpack_from('<II', h, 80)
            table = read_at(src, table_lba*512, count*width)
            if sum(table[i*width:i*width+16] == NV_TYPE.bytes_le for i in range(count)) != 1:
                raise ValueError('Migration currently requires a single Nuvora volume; source is unchanged')
        if read_at(src, first*512, 8) == b'NVSTORE1':
            slots, cap = (8, 4096), 1024*1024
        else:
            s0, s1, cap = geometry(src, first, sectors); slots = (s0, s1)
        _, _, payload = snapshots(src, first, slots, cap)
        entries = parse(payload)
    # Legacy payloads are bounded by 128 MiB. No new-format file is read whole.
    create(destination, size_mib=size_mib)
    try:
        with destination.open('r+b') as out, tempfile.TemporaryDirectory(prefix='nuvora-migrate-') as td:
            first, sectors = partition(out); g = layout(out, first, sectors)
            free = free_ranges([], g[2], g[3]); migrated = []
            for i, (kind, path, body) in enumerate(entries):
                temp = pathlib.Path(td)/str(i)
                temp.write_bytes(body)
                mapping = write_source(out, first, temp, len(body), free) if kind == 2 else []
                migrated.append((kind, path, len(body), mapping)); temp.unlink()
            commit(out, first, g, 0, -1, migrated)
    except BaseException:
        destination.unlink(missing_ok=True)
        raise
    return len(entries)

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=pathlib.Path); p.add_argument('destination', type=pathlib.Path)
    p.add_argument('--size', type=int, default=8192, metavar='MIB')
    a = p.parse_args()
    try: count = migrate(a.source, a.destination, a.size)
    except (OSError, ValueError) as error: p.exit(1, f'Migration refused: {error}\n')
    print(f'Copied {count} entries to {a.destination}. Original image unchanged.')
