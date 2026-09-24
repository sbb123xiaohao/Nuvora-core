"""Shared QEMU invocation. Environment overrides allow an ordinary local install."""
import os
import pathlib
import shutil

ROOT = pathlib.Path(__file__).resolve().parent.parent
ARCH = os.environ.get('NV_ARCH', 'x86_64')
BUILD = ROOT / 'build' / ARCH

OVMF_FILES = ('OVMF.fd', 'OVMF_CODE_4M.fd', 'OVMF_CODE.fd', 'edk2-x86_64-code.fd')
OVMF_DIRS = (
    pathlib.Path('/usr/share/qemu'),
    pathlib.Path('/usr/share/OVMF'),
    pathlib.Path('/usr/share/ovmf'),
    pathlib.Path('/usr/share/edk2-ovmf/x64'),
    pathlib.Path('/usr/share/edk2-ovmf'),
)


def qemu_binary():
    return os.environ.get('NV_QEMU') or shutil.which('qemu-system-x86_64')


def find_uefi_firmware():
    """Locate an x64 UEFI firmware image (OVMF / QEMU edk2 builds)."""
    env = os.environ.get('NV_OVMF')
    if env:
        path = pathlib.Path(env)
        return path if path.is_file() else None
    candidates = []
    binary = qemu_binary()
    if binary:
        qdir = pathlib.Path(binary).resolve().parent
        candidates += [qdir / 'share' / name for name in OVMF_FILES]
        candidates += [qdir.parent / 'share' / 'qemu' / name for name in OVMF_FILES]
    candidates += [d / name for d in OVMF_DIRS for name in OVMF_FILES]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def firmware_arguments(firmware):
    """Use split OVMF code/vars as flash; keep the supplied VARS file untouched."""
    firmware = pathlib.Path(firmware).resolve()
    size = firmware.stat().st_size
    if size and size & (size - 1) == 0:
        return ['-bios', str(firmware)]
    names = [firmware.name.replace('CODE', 'VARS'),
             firmware.name.replace('-code', '-vars'), 'edk2-i386-vars.fd']
    for name in names:
        variables = firmware.with_name(name)
        if variables == firmware or not variables.is_file():
            continue
        total = size + variables.stat().st_size
        if total & (total - 1):
            continue
        code = str(firmware).replace(',', ',,')
        var = str(variables).replace(',', ',,')
        return ['-drive', f'if=pflash,unit=0,format=raw,readonly=on,file={code}',
                '-drive', f'if=pflash,unit=1,format=raw,snapshot=on,file={var}']
    raise RuntimeError(f'Matching UEFI VARS image missing for {firmware}')


def command(memory=64, disk=None, cpu=None, machine='pc', kernel=True, esp=None):
    binary = qemu_binary()
    if not binary:
        raise RuntimeError('Install qemu-system-x86 (Debian/Ubuntu) or qemu-system-x86 (Arch).')
    cmd = [binary, '-accel', 'tcg', '-machine', machine, '-cpu', cpu or 'qemu64',
           '-m', str(memory), '-smp', '1', '-nic', 'none']
    if kernel:
        cmd += ['-kernel', str(BUILD / 'boot.elf')]
    if os.environ.get('NV_QEMU_DATA'):
        cmd += ['-L', os.environ['NV_QEMU_DATA']]
    if kernel and os.environ.get('NV_QEMU_BIOS'):
        cmd += ['-bios', os.environ['NV_QEMU_BIOS']]
    if machine == 'q35' and (disk or esp):
        cmd += ['-device', 'isa-ide,id=legacyide']
    if disk:
        disk_path = pathlib.Path(disk).resolve()
        if not disk_path.is_file():
            raise RuntimeError(f'Data image must be an existing regular file: {disk_path}')
        # QEMU key-value syntax uses doubled commas for literal commas in a path.
        disk_spec = str(disk_path).replace(",", ",,")
        if machine == 'q35':
            # q35 normally exposes only its AHCI controller.  Nuvora's small
            # ATA PIO driver intentionally talks to the legacy 0x1f0/0x3f6
            # ports, so provide an ISA IDE bridge and attach the image to its
            # first channel explicitly.
            cmd += ['-drive', f'file={disk_spec},format=raw,if=none,id=nuvora_disk',
                    '-device', 'ide-hd,drive=nuvora_disk,bus=legacyide.0,unit=0']
        else:
            cmd += ['-drive', f'file={disk_spec},format=raw,if=ide,index=0']
    if esp:
        esp_path = pathlib.Path(esp).resolve()
        if not esp_path.is_file():
            raise RuntimeError(f'UEFI system partition image must exist: {esp_path}')
        esp_spec = str(esp_path).replace(",", ",,")
        if machine == 'q35':
            cmd += ['-drive', f'file={esp_spec},format=raw,if=none,id=nuvora_esp',
                    '-device', 'ide-hd,drive=nuvora_esp,bus=legacyide.0,unit=1']
        else:
            # Primary slave: the Nuvora ATA driver only probes primary
            # master, so the ESP never collides with the data disk.
            cmd += ['-drive', f'file={esp_spec},format=raw,if=ide,index=1']
    return cmd
