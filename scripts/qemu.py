"""Shared QEMU invocation. Environment overrides allow an ordinary local install."""
import os
import pathlib
import shutil
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
ARCH = os.environ.get('NV_ARCH', 'x86_64')
BUILD = ROOT / 'build' / ARCH

OVMF_CODE_FILES = ('OVMF_CODE_4M.fd', 'OVMF_CODE.fd', 'edk2-x86_64-code.fd')
OVMF_COMBINED_FILES = ('OVMF.fd',)
OVMF_DIRS = (
    pathlib.Path('/usr/share/qemu'),
    pathlib.Path('/usr/share/OVMF'),
    pathlib.Path('/usr/share/ovmf'),
    pathlib.Path('/usr/share/edk2-ovmf/x64'),
    pathlib.Path('/usr/share/edk2-ovmf'),
)


def qemu_binary():
    return os.environ.get('NV_QEMU') or shutil.which('qemu-system-x86_64')


def variables_template(code):
    name = code.name
    if 'CODE' in name:
        return code.with_name(name.replace('CODE', 'VARS'))
    if '-code' in name:
        return code.with_name(name.replace('-code', '-vars'))
    return None


def find_uefi_firmware():
    """Locate an x64 UEFI firmware image (OVMF / QEMU edk2 builds)."""
    env = os.environ.get('NV_OVMF')
    if env:
        path = pathlib.Path(env)
        return path if path.is_file() else None
    directories = []
    binary = qemu_binary()
    if binary:
        qdir = pathlib.Path(binary).resolve().parent
        directories += [qdir / 'share', qdir.parent / 'share' / 'qemu']
    directories += list(OVMF_DIRS)
    # Ubuntu ships both a combined OVMF.fd and a code/variables pair. Prefer
    # the pair so UEFI variables live in a writable, private image.
    candidates = [d / name for name in OVMF_CODE_FILES for d in directories]
    candidates += [d / name for name in OVMF_COMBINED_FILES for d in directories]
    for candidate in candidates:
        template = variables_template(candidate)
        if candidate.is_file() and (template is None or template.is_file()):
            return candidate
    return None


def firmware_arguments(firmware):
    """Pair split OVMF images and keep the packaged VARS template untouched."""
    firmware = pathlib.Path(firmware).resolve()
    size = firmware.stat().st_size
    template = variables_template(firmware)
    if template is not None:
        if not template.is_file():
            raise RuntimeError(f'Matching UEFI VARS image missing for {firmware}: expected {template}')
        var_size = template.stat().st_size
        if not size or not var_size or size % 4096 or var_size % 4096:
            raise RuntimeError('UEFI flash images must be nonempty and 4 KiB aligned')
        BUILD.mkdir(parents=True, exist_ok=True)
        variables = BUILD / 'ovmf-vars.fd'
        if not variables.exists():
            with tempfile.NamedTemporaryFile(prefix='ovmf-vars-', dir=BUILD, delete=False) as pending:
                pending_path = pathlib.Path(pending.name)
            try:
                shutil.copyfile(template, pending_path)
                pending_path.replace(variables)
            finally:
                pending_path.unlink(missing_ok=True)
        if variables.stat().st_size != var_size:
            raise RuntimeError(f'{variables} has a different size from {template}; move it aside before switching firmware')
        code = str(firmware).replace(',', ',,')
        var = str(variables.resolve()).replace(',', ',,')
        return ['-drive', f'if=pflash,unit=0,format=raw,readonly=on,file={code}',
                '-drive', f'if=pflash,unit=1,format=raw,file={var}']
    if size and size & (size - 1) == 0:
        return ['-bios', str(firmware)]
    raise RuntimeError(f'Unknown UEFI firmware layout: {firmware}')


def command(memory=64, disk=None, cpu=None, machine='pc', kernel=True, esp=None,
            disk_bus='ide', diagnostics=False):
    if disk_bus not in ('ide', 'ahci', 'nvme'):
        raise ValueError(f'Unsupported data-disk bus: {disk_bus}')
    binary = qemu_binary()
    if not binary:
        raise RuntimeError('Install qemu-system-x86 (Debian/Ubuntu) or qemu-system-x86 (Arch).')
    cmd = [binary, '-accel', 'tcg', '-machine', machine, '-cpu', cpu or 'max',
           '-m', str(memory), '-smp', '1', '-nic', 'none']
    if kernel:
        cmd += ['-kernel', str(BUILD / ('boot-test.elf' if diagnostics else 'boot.elf'))]
    if os.environ.get('NV_QEMU_DATA'):
        cmd += ['-L', os.environ['NV_QEMU_DATA']]
    if kernel and os.environ.get('NV_QEMU_BIOS'):
        cmd += ['-bios', os.environ['NV_QEMU_BIOS']]
    if machine == 'q35' and disk and disk_bus == 'ide':
        cmd += ['-device', 'isa-ide,id=legacyide']
    if disk:
        disk_path = pathlib.Path(disk).resolve()
        if not disk_path.is_file():
            raise RuntimeError(f'Data image must be an existing regular file: {disk_path}')
        # QEMU key-value syntax uses doubled commas for literal commas in a path.
        disk_spec = str(disk_path).replace(",", ",,")
        if disk_bus == 'nvme':
            cmd += ['-drive', f'file={disk_spec},format=raw,if=none,id=nuvora_disk',
                    '-device', 'nvme,drive=nuvora_disk,serial=nuvora-data']
        elif disk_bus == 'ahci':
            cmd += ['-drive', f'file={disk_spec},format=raw,if=none,id=nuvora_disk',
                    '-device', 'ich9-ahci,id=nuvora_sata',
                    '-device', 'ide-hd,drive=nuvora_disk,bus=nuvora_sata.0']
        elif machine == 'q35':
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
                    '-device', 'ide-hd,drive=nuvora_esp,bus=ide.1,unit=0,bootindex=1']
        else:
            # The primary slave keeps the ESP separate from the data disk.
            # An explicit firmware boot priority avoids falling into the shell
            # when the data disk has no EFI loader.
            cmd += ['-drive', f'file={esp_spec},format=raw,if=none,id=nuvora_esp',
                    '-device', 'ide-hd,drive=nuvora_esp,bus=ide.0,unit=1,bootindex=1']
    return cmd
