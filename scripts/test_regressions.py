#!/usr/bin/env python3
"""Run host fixtures against the actual Nuvora loader, ATA and snapshot code."""
import os
import pathlib
import subprocess
import tempfile
from qemu import ROOT, BUILD

def main():
    with tempfile.TemporaryDirectory(prefix='nuvora-regression-') as directory:
        for name in ['address', 'buddy', 'memory', 'heap_map', 'fs_memory', 'disk', 'store', 'uefi']:
            output = pathlib.Path(directory) / name
            cmd = [os.environ.get('CC', 'gcc'), '-std=c11', '-O1', '-g',
                   '-Wall', '-Wextra', '-Werror', '-fshort-wchar', '-fno-builtin',
                   '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                   '-fsanitize=undefined', '-fno-sanitize-recover=all', '-Iinclude',
                   str(ROOT / 'tests' / f'{name}_test.c'), 'common/string.c',
                   'common/page_buddy.c', 'common/slab.c', '-o', str(output)]
            subprocess.run(cmd, cwd=ROOT, check=True)
            args = [str(output)] + ([str(ROOT / 'build/x86_64/nuvora.elf')] if name == 'uefi' else [])
            subprocess.run(args, check=True)
    print('ALL 8 HOST REGRESSION GROUPS PASSED (buddy/slab/ramfs/mapping and synthetic firmware/port I/O, UBSan enabled)')

if __name__ == '__main__':
    main()
