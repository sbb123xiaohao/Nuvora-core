#!/usr/bin/env python3
"""Build the EFI system partition image for UEFI boot (requires mtools)."""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
ARCH = os.environ.get('NV_ARCH', 'x86_64')
BUILD = ROOT / 'build' / ARCH


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', nargs='?', type=pathlib.Path, default=BUILD / 'esp.img')
    args = parser.parse_args()
    for tool in ('mformat', 'mmd', 'mcopy'):
        if not shutil.which(tool):
            raise SystemExit(f'UEFI ESP build needs mtools ({tool} not found). '
                             'BIOS/GRUB builds keep working without it.')
    bootx64 = BUILD / 'BOOTX64.EFI'
    kernel = BUILD / 'nuvora.elf'
    for required in (bootx64, kernel):
        if not required.is_file():
            raise SystemExit(f'Missing build artifact: {required}')
    esp = args.image
    esp.parent.mkdir(parents=True, exist_ok=True)
    # Replace only a complete image, so a failed mcopy never leaves a bootable
    # looking partial target and an existing ESP survives packaging errors.
    with tempfile.TemporaryDirectory(prefix='nuvora-esp-', dir=esp.parent) as directory:
        pending = pathlib.Path(directory) / 'esp.img'
        subprocess.run(['mformat', '-i', str(pending), '-C', '-T', '32768', '-h', '16', '-s', '32'],
                       check=True)
        def run(tool, *cmd):
            subprocess.run([tool, '-i', str(pending), *cmd], check=True)
        run('mmd', '::/EFI', '::/EFI/BOOT', '::/EFI/NUVORA')
        run('mcopy', str(bootx64), '::/EFI/BOOT/BOOTX64.EFI')
        run('mcopy', str(kernel), '::/EFI/NUVORA/NUVORA.ELF')
        cmdline = BUILD / 'uefi-cmdline.txt'
        if cmdline.is_file():
            run('mcopy', str(cmdline), '::/EFI/NUVORA/CMDLINE')
        pending.replace(esp)
    print(f'Created UEFI system partition image: {esp}')
    sys.exit(0)


if __name__ == '__main__':
    main()
