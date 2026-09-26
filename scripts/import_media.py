#!/usr/bin/env python3
"""Copy host files into C:/ on a powered-off Nuvora GPT data image.

Commits an inactive NVSTORE2 snapshot only after every byte is written.
Existing files are preserved; use --replace for an intentional overwrite.
"""
import argparse
import pathlib
import struct
import uuid
import zlib

from mkgptdisk import NV_TYPE

SECTOR = 512
FILE_LIMIT = 64 * 1024 * 1024
SLOT_LIMIT = 128 * 1024 * 1024


def crc(data):
    return zlib.crc32(data) & 0xffffffff


def read_at(stream, offset, count):
    stream.seek(offset)
    data = stream.read(count)
    if len(data) != count:
        raise ValueError('Data image is truncated.')
    return data


def partition(stream):
    header = bytearray(read_at(stream, SECTOR, SECTOR))
    if header[:8] != b'EFI PART':
        raise ValueError('Expected a GPT data image created by mkgptdisk.py.')
    length = struct.unpack_from('<I', header, 12)[0]
    checksum = struct.unpack_from('<I', header, 16)[0]
    if not 92 <= length <= SECTOR:
        raise ValueError('Invalid GPT header length.')
    struct.pack_into('<I', header, 16, 0)
    if crc(header[:length]) != checksum:
        raise ValueError('GPT header checksum failed.')
    table_lba = struct.unpack_from('<Q', header, 72)[0]
    count, width, expected = struct.unpack_from('<III', header, 80)
    if not 1 <= count <= 128 or width != 128:
        raise ValueError('Unsupported GPT partition table.')
    table = read_at(stream, table_lba * SECTOR, count * width)
    if crc(table) != expected:
        raise ValueError('GPT partition table checksum failed.')
    for i in range(count):
        item = table[i * width:(i + 1) * width]
        if item[:16] == NV_TYPE.bytes_le:
            first, last = struct.unpack_from('<QQ', item, 32)
            if first < 34 or last <= first:
                break
            return first, last - first + 1
    raise ValueError('No valid Nuvora C: partition found.')


def geometry(stream, first, sectors):
    header = read_at(stream, first * SECTOR, SECTOR)
    if header[:8] != b'NVSTORE2':
        raise ValueError('C: needs an NVSTORE2 partition.')
    version, sector, slot0, slot1, slot_sectors = struct.unpack_from('<IIIII', header, 8)
    if version != 2 or sector != SECTOR or crc(header[:40]) != struct.unpack_from('<I', header, 40)[0]:
        raise ValueError('Invalid NVSTORE2 header.')
    if not slot0 or slot1 < slot0 + slot_sectors or slot1 + slot_sectors > sectors:
        raise ValueError('NVSTORE2 slots are outside the partition.')
    return slot0, slot1, min(SLOT_LIMIT, (slot_sectors - 1) * SECTOR)


def snapshots(stream, first, slots, capacity):
    available = []
    for index, slot in enumerate(slots):
        header = bytearray(read_at(stream, (first + slot) * SECTOR, SECTOR))
        if header == bytes(SECTOR):
            continue
        if header[:8] != b'NVSS0001':
            continue
        generation, length, data_crc, header_crc = struct.unpack_from('<IIII', header, 8)
        struct.pack_into('<I', header, 20, 0)
        if not 4 <= length <= capacity or crc(header) != header_crc:
            continue
        payload = read_at(stream, (first + slot + 1) * SECTOR, length)
        if crc(payload) == data_crc:
            available.append((generation, index, payload))
    if available:
        return max(available, key=lambda item: item[0])
    for slot in slots:
        if read_at(stream, (first + slot) * SECTOR, SECTOR) != bytes(SECTOR):
            raise ValueError('Both snapshot slots are invalid; refusing to discard existing data.')
    return 0, -1, struct.pack('<I', 0)


def parse(payload):
    if len(payload) < 4:
        raise ValueError('Invalid snapshot.')
    count = struct.unpack_from('<I', payload)[0]
    if count > 480:
        raise ValueError('Snapshot has too many files.')
    result = []
    pos = 4
    for _ in range(count):
        if pos + 12 > len(payload):
            raise ValueError('Truncated snapshot entry.')
        kind, length, size = struct.unpack_from('<III', payload, pos)
        pos += 12
        if kind not in (1, 2) or length > 191 or size > FILE_LIMIT or pos + length + size > len(payload):
            raise ValueError('Invalid snapshot entry.')
        path = payload[pos:pos + length]
        pos += length
        body = payload[pos:pos + size]
        pos += size
        if not path.startswith(b'/home/') or b'\0' in path or (kind == 1 and size):
            raise ValueError('Snapshot path or kind is invalid.')
        result.append((kind, path, body))
    if pos != len(payload):
        raise ValueError('Snapshot contains trailing data.')
    return result


def pack(entries):
    out = bytearray(struct.pack('<I', len(entries)))
    for kind, path, body in entries:
        out += struct.pack('<III', kind, len(path), len(body)) + path + body
    return bytes(out)


def import_files(image, paths, replace=False):
    if not image.is_file():
        raise ValueError('Expected a regular GPT data image, not a device.')
    with image.open('r+b') as stream:
        first, sectors = partition(stream)
        slot0, slot1, capacity = geometry(stream, first, sectors)
        generation, active, payload = snapshots(stream, first, (slot0, slot1), capacity)
        entries = parse(payload)
        existing = {path: index for index, (_, path, _) in enumerate(entries)}
        for source in paths:
            if not source.is_file():
                raise ValueError(f'Expected a regular file: {source}')
            name = source.name.encode('ascii')
            if not 0 < len(name) <= 31 or b'/' in name:
                raise ValueError(f'File name must be at most 31 ASCII bytes: {source}')
            size = source.stat().st_size
            if size > FILE_LIMIT:
                raise ValueError(f'File must be at most 64 MiB: {source}')
            key = b'/home/' + name
            if key in existing and not replace:
                raise ValueError(f'C: already contains {source.name}; use --replace.')
            body = source.read_bytes()
            if len(body) != size:
                raise ValueError(f'Media file changed during import: {source}')
            value = (2, key, body)
            if key in existing:
                if entries[existing[key]][0] != 2:
                    raise ValueError(f'C: contains a directory named {source.name}.')
                entries[existing[key]] = value
            else:
                existing[key] = len(entries)
                entries.append(value)
        if len(entries) > 480:
            raise ValueError('C: has too many files.')
        payload = pack(entries)
        if len(payload) > capacity:
            raise ValueError(f'Snapshot needs {len(payload)} bytes; slot holds {capacity}.')
        target = slot1 if active == 0 else slot0
        offset = (first + target) * SECTOR
        stream.seek(offset)
        stream.write(bytes(SECTOR))  # invalidate the target before writing data
        stream.flush()
        stream.seek(offset + SECTOR)
        stream.write(payload)
        stream.write(bytes((-len(payload)) % SECTOR))
        stream.flush()
        import os
        os.fsync(stream.fileno())
        header = bytearray(SECTOR)
        struct.pack_into('<8sIIII', header, 0, b'NVSS0001',
                         generation + 1, len(payload), crc(payload), 0)
        struct.pack_into('<I', header, 20, crc(header))
        stream.seek(offset)
        stream.write(header)
        stream.flush()
        os.fsync(stream.fileno())
    return len(paths), len(payload)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disk', type=pathlib.Path, default=pathlib.Path('build/x86_64/nuvora-store.img'))
    parser.add_argument('--replace', action='store_true', help='overwrite matching names on C:')
    parser.add_argument('files', type=pathlib.Path, nargs='+')
    args = parser.parse_args()
    try:
        count, size = import_files(args.disk, args.files, args.replace)
    except (OSError, ValueError, UnicodeError) as error:
        parser.exit(1, f'Import refused: {error}\n')
    print(f'Imported {count} media file(s) into C: snapshot ({size} bytes).')
