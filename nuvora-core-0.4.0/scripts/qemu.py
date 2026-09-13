"""Shared QEMU invocation. Environment overrides allow an ordinary local install."""
import os
import pathlib
import shutil

ROOT = pathlib.Path(__file__).resolve().parent.parent
ARCH = os.environ.get('NV_ARCH', 'x86_64')
BUILD = ROOT / 'build' / ARCH

def command(memory=64, disk=None):
    binary = os.environ.get('NV_QEMU') or shutil.which('qemu-system-x86_64') or (shutil.which('qemu-system-i386') if ARCH == 'i686' else None)
    if not binary:
        raise RuntimeError('Install qemu-system-x86 (Debian/Ubuntu) or qemu-system-x86 (Arch).')
    cmd = [binary, '-accel', 'tcg', '-machine', 'pc', '-cpu', 'qemu64' if ARCH == 'x86_64' else 'qemu32',
           '-m', str(memory), '-smp', '1', '-nic', 'none',
           '-kernel', str(BUILD / 'boot.elf')]
    if os.environ.get('NV_QEMU_DATA'):
        cmd += ['-L', os.environ['NV_QEMU_DATA']]
    if os.environ.get('NV_QEMU_BIOS'):
        cmd += ['-bios', os.environ['NV_QEMU_BIOS']]
    if disk:
        disk_path = pathlib.Path(disk).resolve()
        if not disk_path.is_file():
            raise RuntimeError(f'Data image must be an existing regular file: {disk_path}')
        # QEMU key-value syntax uses doubled commas for literal commas in a path.
        cmd += ['-drive', f'file={str(disk_path).replace(",", ",,")},format=raw,if=ide,index=0']
    return cmd
