#!/usr/bin/env python3
"""Build the small, deterministic read-only /apps archive."""
import pathlib
import re
import struct
import sys

if sys.argv[1] == '--assembly':
    pathlib.Path(sys.argv[2]).write_text(
        '.section .rodata.archive,"a"\n.balign 16\n'
        '.global archive_start,archive_end\narchive_start:\n'
        f'.incbin "{pathlib.Path(sys.argv[2]).parent / "init.nvar"}"\narchive_end:\n'
        '.section .note.GNU-stack,"",@progbits\n')
else:
    target = pathlib.Path(sys.argv[1])
    files = sorted(map(pathlib.Path, sys.argv[2:]), key=lambda p: p.stem)
    out = bytearray(b'NVAR0001' + struct.pack('<I', len(files)))
    abi = (pathlib.Path(__file__).parent.parent / 'include/nv/abi.h').read_text()
    match = re.search(r'#define NV_FILE_MAX \((\d+)u \* 1024u \* 1024u\)', abi)
    if not match:
        raise SystemExit('Cannot read NV_FILE_MAX from the ABI header.')
    limit = int(match.group(1)) * 1024 * 1024
    for path in files:
        name, data = path.stem.encode('ascii'), path.read_bytes()
        if not 0 < len(name) <= 31 or len(data) > limit:
            raise SystemExit(f'Archive limits exceeded: {path}')
        out += struct.pack('<II', len(name), len(data)) + name + data
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(out)
    print(f'Archive: {len(files)} applications, {len(out)} bytes')
