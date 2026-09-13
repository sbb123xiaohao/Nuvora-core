#!/usr/bin/env python3
import os
import pathlib
import shutil
import subprocess
from qemu import ROOT, BUILD, ARCH

grub = os.environ.get('NV_GRUB_MKRESCUE') or shutil.which('grub-mkrescue')
if not grub:
    raise SystemExit('Install grub-pc-bin, grub-common and xorriso to build the BIOS ISO.')
tree = BUILD / 'iso-root'
(tree / 'boot/grub').mkdir(parents=True, exist_ok=True)
shutil.copy2(BUILD / 'boot.elf', tree / 'boot/nuvora.elf')
(tree / 'boot/grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "Nuvora Core 0.4.0" {\n  multiboot /boot/nuvora.elf\n  boot\n}\n')
cmd = [grub]
if os.environ.get('NV_GRUB_DIRECTORY'):
    cmd += ['-d', os.environ['NV_GRUB_DIRECTORY']]
if os.environ.get('NV_XORRISO'):
    cmd += ['--xorriso=' + os.environ['NV_XORRISO']]
cmd += ['-o', str(BUILD / f'nuvora-core-0.4.0-{ARCH}.iso'), str(tree)]
subprocess.run(cmd, check=True)
