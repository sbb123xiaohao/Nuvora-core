#!/usr/bin/env python3
import argparse
import os
import pathlib
import shutil
import subprocess
from qemu import BUILD, ARCH

parser = argparse.ArgumentParser(description='Build Nuvora Core ISO images.')
parser.add_argument('--uefi', action='store_true',
                    help='Build the UEFI El Torito ISO from build/ARCH/esp.img instead of the BIOS/GRUB ISO')
args = parser.parse_args()

VERSION = '0.7.2'


def xorriso_binary():
    return os.environ.get('NV_XORRISO') or shutil.which('xorriso')


if args.uefi:
    esp = BUILD / 'esp.img'
    if not esp.is_file():
        raise SystemExit(f'Missing {esp}; build it first: make esp (requires mtools).')
    xorriso = xorriso_binary()
    if not xorriso:
        raise SystemExit('Install xorriso to build the UEFI ISO.')
    # Pure UEFI ISO: the ESP image is the El Torito boot entry, no BIOS path.
    tree = BUILD / 'iso-root-uefi'
    tree.mkdir(parents=True, exist_ok=True)
    shutil.copy2(esp, tree / 'efiboot.img')
    subprocess.run([xorriso, '-as', 'mkisofs', '-R', '-V', 'NUVORA_UEFI',
                    '-o', str(BUILD / f'nuvora-core-{VERSION}-{ARCH}-uefi.iso'),
                    '-e', 'efiboot.img', '-no-emul-boot', str(tree)],
                   check=True)
    print(f'Created UEFI ISO: {BUILD / f"nuvora-core-{VERSION}-{ARCH}-uefi.iso"}')
else:
    grub = os.environ.get('NV_GRUB_MKRESCUE') or shutil.which('grub-mkrescue')
    if not grub:
        raise SystemExit('Install grub-pc-bin, grub-common and xorriso to build the BIOS ISO.')
    tree = BUILD / 'iso-root'
    (tree / 'boot/grub').mkdir(parents=True, exist_ok=True)
    shutil.copy2(BUILD / 'boot.elf', tree / 'boot/nuvora.elf')
    (tree / 'boot/grub/grub.cfg').write_text(
        f'set timeout=0\nset default=0\nmenuentry "Nuvora Core {VERSION}" {{\n'
        '  multiboot /boot/nuvora.elf\n  boot\n}\n')
    cmd = [grub]
    if os.environ.get('NV_GRUB_DIRECTORY'):
        cmd += ['-d', os.environ['NV_GRUB_DIRECTORY']]
    if os.environ.get('NV_XORRISO'):
        cmd += ['--xorriso=' + os.environ['NV_XORRISO']]
    cmd += ['-o', str(BUILD / f'nuvora-core-{VERSION}-{ARCH}.iso'), str(tree)]
    subprocess.run(cmd, check=True)
