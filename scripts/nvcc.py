#!/usr/bin/env python3
"""Compile freestanding C with main(argc, argv) for Nuvora x64 ABI 1."""
import argparse
import os
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sources', nargs='+', type=pathlib.Path)
    parser.add_argument('-o', '--output', required=True, type=pathlib.Path)
    args = parser.parse_args()
    cc = os.environ.get('CC', 'gcc')
    flags = ['-m64', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
             '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector',
             '-mno-red-zone', '-fno-asynchronous-unwind-tables', '-ffunction-sections',
             '-fdata-sections', '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'include')]
    sources = args.sources + [ROOT / 'sdk/libc.c', ROOT / 'sdk/crt64.S', ROOT / 'common/string.c']
    with tempfile.TemporaryDirectory(prefix='nvcc-') as temp:
        objects = []
        for index, src in enumerate(sources):
            obj = str(pathlib.Path(temp) / f'{index}.o')
            subprocess.run([cc, *flags, '-c', str(src.resolve()), '-o', obj], check=True)
            objects.append(obj)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([os.environ.get('LD', 'ld'), '-m', 'elf_x86_64', '--gc-sections',
                        '-z', 'max-page-size=4096', '-T', str(ROOT / 'user/linker64.ld'),
                        '-o', str(args.output), *objects], check=True)
    print('Nuvora static ELF64:', args.output)

if __name__ == '__main__':
    main()
