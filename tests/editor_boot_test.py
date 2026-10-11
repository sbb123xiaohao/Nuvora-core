#!/usr/bin/env python3
"""Open a real >4 GiB sparse file in both production editors without data loss.

Uses a private NVSTORE3 disk and firmware copy. Checks committed document bytes,
large-file length and both allocated boundary blocks with the guest paused.
"""
import argparse
import pathlib
import shutil
import subprocess
import tempfile
import time

from desktop_boot_test import Monitor, first_account, qemu, create
from session_boot_test import wait_saved_file
from import_media import import_files, partition
from extent_store import layout, roots, BLOCK


def check_large(monitor, disk):
    monitor.call('stop')
    try:
        with disk.open('rb') as stream:
            first, sectors = partition(stream)
            records = roots(stream, first, layout(stream, first, sectors))
            entry = next(e for e in records[-1][2] if e[1] == b'/home/huge.txt')
            kind, _, size, extents = entry
            assert kind == 2 and size == (1 << 32) + 1
            assert sum(e[2] for e in extents) == 2
            for offset, expected in ((0, b'X'), (1 << 32, b'Y')):
                logical, physical, _ = next(e for e in extents
                                           if e[0]*BLOCK <= offset < (e[0]+e[2])*BLOCK)
                stream.seek(first*512 + physical*BLOCK + offset-logical*BLOCK)
                assert stream.read(1) == expected
    finally:
        monitor.call('cont')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, default=qemu.BUILD / 'editor-io')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    firmware = qemu.find_uefi_firmware()
    if not firmware:
        raise SystemExit('OVMF not found')
    serial = output / 'serial.log'
    with tempfile.TemporaryDirectory(prefix='nuvora-editor-') as directory:
        private = pathlib.Path(directory)
        huge, disk, esp = private/'huge.txt', private/'data.img', private/'esp.img'
        with huge.open('wb') as stream:
            stream.write(b'X')
            stream.seek(1 << 32)
            stream.write(b'Y')
        create(disk, size_mib=128, partitions=1)
        import_files(disk, [huge])
        shutil.copyfile(qemu.BUILD / 'esp.img', esp)
        original = qemu.BUILD
        qemu.BUILD = private
        try:
            command = qemu.command(memory=256, disk=disk, machine='q35',
                                   kernel=False, esp=esp, disk_bus='ahci')
            command += qemu.firmware_arguments(firmware)
        finally:
            qemu.BUILD = original
        command += ['-display', 'none', '-qmp', 'stdio', '-serial', f'file:{serial}',
                    '-device', 'qemu-xhci,id=xhci', '-device', 'usb-kbd,bus=xhci.0',
                    '-no-reboot']
        with (output / 'qemu.log').open('w') as log:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log)
            try:
                monitor = Monitor(process)
                deadline = time.monotonic() + 40
                while '[ok] entering Ring 3' not in serial.read_text(errors='replace'):
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError('Production desktop did not boot')
                    time.sleep(.2)
                first_account(monitor, output)
                check_large(monitor, disk)
                monitor.start(1)
                monitor.type('Keep')
                monitor.key('ctrl', 's')
                monitor.key('ret')
                wait_saved_file(monitor, disk, '/home/users/tester/Untitled.txt', b'Keep')
                monitor.start(0)
                monitor.key('backspace')  # /home/users
                monitor.key('backspace')  # /home
                monitor.key('ctrl', 'f')
                monitor.type('huge.txt')
                monitor.key('ret')
                monitor.key('ret')
                monitor.shot(output, 'desktop-large-rejected')
                # Save through the retained path and cursor; a truncated open
                # would instead overwrite huge.txt and leave Untitled unchanged.
                monitor.type('Z')
                monitor.key('ctrl', 's')
                wait_saved_file(monitor, disk, '/home/users/tester/Untitled.txt', b'KeepZ')
                check_large(monitor, disk)
                print('PASS native Text Editor: >4 GiB load rejected; document, path and cursor retained; large file unchanged', flush=True)

                monitor.start(4)
                monitor.type('FolioKeep')
                monitor.key('ctrl', 's')
                monitor.type('/home/users/tester/Folio.txt\n')
                wait_saved_file(monitor, disk, '/home/users/tester/Folio.txt', b'FolioKeep')
                monitor.key('ctrl', 'o')
                monitor.type('/home/huge.txt\n')
                monitor.shot(output, 'folio-large-rejected')
                monitor.type('Z')
                monitor.key('ctrl', 's')
                wait_saved_file(monitor, disk, '/home/users/tester/Folio.txt', b'FolioKeepZ')
                check_large(monitor, disk)
                text = serial.read_text(errors='replace')
                assert 'PANIC' not in text and '[trap]' not in text
                print('PASS native Folio: >4 GiB load rejected; document, path and cursor retained; large file unchanged', flush=True)
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == '__main__':
    main()
