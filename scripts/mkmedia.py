#!/usr/bin/env python3
"""Wrap the existing FAT ESP in a standard GPT disk image for UEFI USB boot.

Only regular image files are written. This program never writes a host block
device, and the resulting image contains no Nuvora data partition.
"""
import argparse
import os
import pathlib
import struct
import tempfile
import uuid
import zlib

try:
    from .mkgptdisk import _gpt_header
except ImportError:
    from mkgptdisk import _gpt_header

SECTOR = 512
ESP_START = 2048  # 1 MiB alignment
ENTRIES_SIZE = 128 * 128
ESP_TYPE = uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b')
BUILD = pathlib.Path(__file__).resolve().parent.parent / 'build' / 'x86_64'


def create(esp: pathlib.Path, output: pathlib.Path) -> None:
    esp = esp.resolve(strict=True)
    output = output.absolute()
    if not esp.is_file() or not output.parent.is_dir() or (
            output.exists() and not output.is_file()) or esp == output.resolve():
        raise ValueError('ESP and output must be distinct regular image files')
    esp_bytes = esp.stat().st_size
    if esp_bytes < 1024 * 1024 or esp_bytes % SECTOR or esp_bytes > 512 * 1024 * 1024:
        raise ValueError('ESP must be a 512-byte-aligned image between 1 and 512 MiB')
    with esp.open('rb') as source:
        boot = source.read(SECTOR)
    esp_sectors = esp_bytes // SECTOR
    sectors16 = struct.unpack_from('<H', boot, 19)[0]
    sectors32 = struct.unpack_from('<I', boot, 32)[0]
    cluster = boot[13]
    if (boot[510:512] != b'\x55\xaa' or
            struct.unpack_from('<H', boot, 11)[0] != SECTOR or
            not cluster or cluster & (cluster - 1) or
            not struct.unpack_from('<H', boot, 14)[0] or not boot[16] or
            (sectors16 or sectors32) != esp_sectors):
        raise ValueError('ESP does not contain a matching FAT boot sector')
    # Round the whole disk up to a MiB and leave room for the backup GPT.
    total = ((ESP_START + esp_sectors + 34 + 2047) // 2048) * 2048
    last = total - 34
    entries = bytearray(ENTRIES_SIZE)
    entries[:16] = ESP_TYPE.bytes_le
    entries[16:32] = uuid.uuid4().bytes_le
    struct.pack_into('<QQQ', entries, 32, ESP_START, ESP_START + esp_sectors - 1, 0)
    name = 'Nuvora Boot'.encode('utf-16le')
    entries[56:56 + len(name)] = name
    crc = zlib.crc32(entries)
    disk_guid = uuid.uuid4()
    mbr = bytearray(SECTOR)
    mbr[446 + 4] = 0xee
    struct.pack_into('<II', mbr, 446 + 8, 1, total - 1)
    mbr[510:512] = b'\x55\xaa'
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix='nuvora-media-', suffix='.partial',
                                         dir=output.parent, delete=False) as stream:
            temporary = pathlib.Path(stream.name)
            stream.truncate(total * SECTOR)
            stream.seek(0)
            stream.write(mbr)
            stream.write(_gpt_header(1, total - 1, 2, ESP_START, last, disk_guid, crc))
            stream.write(entries)
            stream.seek((total - 33) * SECTOR)
            stream.write(entries)
            stream.write(_gpt_header(total - 1, 1, total - 33, ESP_START,
                                     last, disk_guid, crc))
            stream.seek(ESP_START * SECTOR)
            with esp.open('rb') as source:
                while data := source.read(1024 * 1024):
                    stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(output)
    finally:
        if temporary and temporary.exists():
            temporary.unlink()
    print(f'Created UEFI GPT boot image: {output} ({total * SECTOR // 1024 // 1024} MiB)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', nargs='?', type=pathlib.Path,
                        default=BUILD / 'nuvora-uefi-media.img')
    parser.add_argument('--esp', type=pathlib.Path, default=BUILD / 'esp.img')
    arguments = parser.parse_args()
    create(arguments.esp, arguments.output)
