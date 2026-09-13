#!/usr/bin/env python3
"""Verify ELF32 and the actual Multiboot header bytes in the first 8 KiB."""
import pathlib
import struct
import subprocess
import sys

path = pathlib.Path(sys.argv[1])
data = path.read_bytes()
assert data[:4] == b'\x7fELF' and data[4] in [1, 2] and data[5:7] == b'\x01\x01', 'not little-endian ELF'
elf_class = data[4]
assert struct.unpack_from('<H', data, 18)[0] == (3 if elf_class == 1 else 62), 'wrong x86 machine'
found = False
for offset in range(0, min(8192, len(data)) - 11, 4):
    magic, flags, checksum = struct.unpack_from('<III', data, offset)
    if magic == 0x1BADB002:
        assert flags == 3 and (magic + flags + checksum) & 0xFFFFFFFF == 0
        print(f'Multiboot header verified at file offset {offset}')
        found = True
        break
assert found, 'Multiboot v1 header missing'
undefined = subprocess.check_output(['nm', '-u', str(path)], text=True)
assert not undefined.strip(), f'undefined symbols: {undefined}'
print(f'ELF{32 if elf_class == 1 else 64} verified: {len(data)} bytes; no undefined symbols')
