#!/usr/bin/env python3
"""Boot the GPT/FAT32 image as removable USB media, then run native Ring 3.

This is an OVMF firmware regression, not physical hardware certification.
Only private ESP, GPT and variable-store copies are modified.
"""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts'))
import qemu
from mkmedia import create
from test import EXPECTED_ASSERTIONS


def main():
    subprocess.run(['make','-s','diagnostics'],cwd=qemu.ROOT,check=True)
    firmware = qemu.find_uefi_firmware()
    if not firmware:
        raise SystemExit('Install OVMF or set NV_OVMF')
    output = qemu.BUILD / 'media-smoke'
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nuvora-removable-') as directory:
        private = pathlib.Path(directory)
        esp = private / 'esp.img'
        shutil.copyfile(qemu.BUILD / 'esp.img', esp)
        subprocess.run(['mcopy','-o','-i',str(esp),str(qemu.BUILD / 'nuvora-uefi-test.elf'),
                        '::/EFI/NUVORA/NUVORA.ELF'],check=True)
        cmdline = private / 'CMDLINE'
        cmdline.write_text('nv.test=1\n')
        subprocess.run(['mcopy', '-o', '-i', str(esp), str(cmdline),
                        '::/EFI/NUVORA/CMDLINE'], check=True)
        media = private / 'boot.img'
        create(esp, media)
        command = qemu.command(memory=256, machine='q35', kernel=False)
        original = qemu.BUILD
        try:
            qemu.BUILD = private
            command += qemu.firmware_arguments(firmware)
        finally:
            qemu.BUILD = original
        command += ['-device', 'qemu-xhci,id=xhci', '-drive',
                    f'file={media},format=raw,if=none,id=boot', '-device',
                    'usb-storage,bus=xhci.0,drive=boot,bootindex=1',
                    '-device', 'usb-kbd,bus=xhci.0', '-device', 'usb-tablet,bus=xhci.0',
                    '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
                    '-no-reboot', '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04']
        try:
            run = subprocess.run(command, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=120)
        except subprocess.TimeoutExpired as error:
            (output / 'serial.log').write_bytes(error.stdout or b'')
            raise
        out = run.stdout.decode(errors='replace').replace('\r', '')
        (output / 'serial.log').write_text(out)
        match = re.search(r'PROBE RESULT: (\d+) passed, 0 failed', out)
        assert run.returncode == 33 and match and int(match[1]) == EXPECTED_ASSERTIONS, out
        assert 'FAIL ' not in out and 'PANIC' not in out
        print(f'PASS removable UEFI GPT/FAT32: BOOTX64.EFI, USB xHCI and '
              f'{EXPECTED_ASSERTIONS} native/legacy Ring 3 assertions; QEMU exit 33')


if __name__ == '__main__':
    main()
