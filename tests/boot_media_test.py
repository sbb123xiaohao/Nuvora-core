#!/usr/bin/env python3
"""Validate both GPT copies and the unchanged ESP payload in a boot image."""
import pathlib
import struct
import sys
import tempfile
import uuid
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
from mkmedia import create, ESP_START, ESP_TYPE, SECTOR


def check_header(image, at, other, entries_at):
    header = image[at * SECTOR:(at + 1) * SECTOR]
    assert header[:8] == b'EFI PART'
    assert struct.unpack_from('<I', header, 12)[0] == 92
    saved = struct.unpack_from('<I', header, 16)[0]
    zeroed = bytearray(header[:92])
    zeroed[16:20] = b'\0' * 4
    assert zlib.crc32(zeroed) == saved
    assert struct.unpack_from('<QQ', header, 24) == (at, other)
    assert struct.unpack_from('<Q', header, 72)[0] == entries_at
    assert struct.unpack_from('<II', header, 80) == (128, 128)
    entries = image[entries_at * SECTOR:(entries_at + 32) * SECTOR]
    assert zlib.crc32(entries) == struct.unpack_from('<I', header, 88)[0]
    assert entries[:16] == ESP_TYPE.bytes_le
    return entries


def main():
    with tempfile.TemporaryDirectory() as directory:
        esp = pathlib.Path(directory) / 'esp.img'
        image = pathlib.Path(directory) / 'media.img'
        payload = bytearray(1024 * 1024)
        payload[510:512] = b'\x55\xaa'
        struct.pack_into('<H', payload, 11, SECTOR)
        payload[13] = 1
        struct.pack_into('<H', payload, 14, 1)
        payload[16] = 2
        struct.pack_into('<H', payload, 19, len(payload) // SECTOR)
        payload[1000:1007] = b'BOOTX64'
        esp.write_bytes(payload)
        create(esp, image)
        raw = image.read_bytes()
        total = len(raw) // SECTOR
        assert raw[510:512] == b'\x55\xaa' and raw[450] == 0xee
        assert struct.unpack_from('<II', raw, 454) == (1, total - 1)
        primary = check_header(raw, 1, total - 1, 2)
        backup = check_header(raw, total - 1, 1, total - 33)
        assert primary == backup
        assert uuid.UUID(bytes_le=primary[:16]) == ESP_TYPE
        assert struct.unpack_from('<QQ', primary, 32) == (
            ESP_START, ESP_START + len(payload) // SECTOR - 1)
        assert raw[ESP_START * SECTOR:ESP_START * SECTOR + len(payload)] == payload
        assert esp.read_bytes() == payload
        try:
            create(esp, esp)
        except ValueError:
            pass
        else:
            raise AssertionError('must refuse to overwrite the source ESP')
    print('PASS UEFI media: protective MBR, primary/backup GPT CRC, ESP bytes, input isolation')


if __name__ == '__main__':
    main()
