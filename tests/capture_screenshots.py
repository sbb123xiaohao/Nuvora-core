#!/usr/bin/env python3
"""Capture the production UEFI desktop through QEMU's real framebuffer.

Requires make all esp, QEMU, OVMF and mtools. Boot, firmware variables
and data are private copies. PNG conversion preserves every framebuffer pixel.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import struct
import subprocess
import tempfile
import time
import zlib

from desktop_boot_test import Monitor, first_account, desktop_visible, ui_scale, qemu, create
from mkmedia import create as create_media
from session_boot_test import saved_file


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_png(path, width, height, pixels):
    """Store unmodified RGB framebuffer rows in a standard lossless PNG."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    stride = width * 3
    rows = b''.join(b'\0' + pixels[y * stride:(y + 1) * stride] for y in range(height))
    header = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) +
                     chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, default=qemu.BUILD / 'screenshots')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    firmware = qemu.find_uefi_firmware()
    if not firmware:
        raise SystemExit('OVMF not found')
    production = qemu.BUILD / 'nuvora-uefi.elf'
    source_esp = qemu.BUILD / 'esp.img'
    if not production.is_file() or not source_esp.is_file():
        raise SystemExit('Build the production image first: make all esp')
    pictures = []
    serial = output / 'serial.log'
    with tempfile.TemporaryDirectory(prefix='nuvora-screenshots-') as directory:
        private = pathlib.Path(directory)
        disk, esp, media = private / 'data.img', private / 'esp.img', private / 'boot.img'
        create(disk, size_mib=128, partitions=1)
        shutil.copyfile(source_esp, esp)
        create_media(esp, media)
        original = qemu.BUILD
        qemu.BUILD = private
        try:
            command = qemu.command(memory=256, disk=disk, machine='q35', kernel=False, disk_bus='ahci')
            command += qemu.firmware_arguments(firmware)
        finally:
            qemu.BUILD = original
        command += ['-display', 'none', '-qmp', 'stdio', '-serial', f'file:{serial}',
                    '-device', 'qemu-xhci,id=xhci', '-device', 'usb-kbd,bus=xhci.0',
                    '-device', 'usb-tablet,bus=xhci.0',
                    '-drive', f'file={media},format=raw,if=none,id=boot_media',
                    '-device', 'usb-storage,bus=xhci.0,drive=boot_media,bootindex=1']
        with (output / 'qemu.log').open('w') as log:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log)
            try:
                monitor = Monitor(process)
                deadline = time.monotonic() + 60
                while 'Ring 3: /apps/session' not in serial.read_text(errors='replace'):
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError('Production graphical session did not boot')
                    time.sleep(.2)
                first_account(monitor, output)

                def capture(name):
                    width, height, pixels = monitor.shot(output, name)
                    assert desktop_visible(pixels, ui_scale(width, height)), name
                    target = output / (name + '.png')
                    write_png(target, width, height, pixels)
                    pictures.append({'file': target.name, 'width': width, 'height': height,
                                     'sha256': digest(target)})
                    print(f'CAPTURE {target.name}: {width}x{height}', flush=True)

                capture('desktop')
                monitor.start(1)
                monitor.key('meta_l', 'right')
                paragraph = ('Native desktop with files and terminal and a saved document on the private AHCI data volume. '
                             'This text wraps across visual rows.')
                prefix = 'Nuvora Core\n\n'
                document = prefix + paragraph + '\n'
                monitor.type(prefix + paragraph)
                # The paragraph wraps in this tiled editor. Up then Down must
                # return to its final visual row before inserting the newline.
                monitor.key('up')
                monitor.key('down')
                monitor.key('ret')
                monitor.key('ctrl', 's')
                monitor.key('ret')
                monitor.call('stop')
                try:
                    assert saved_file(disk, '/home/users/tester/Untitled.txt') == document.encode()
                finally:
                    monitor.call('cont')
                monitor.key('alt', 'f9')
                monitor.start(2)
                monitor.type('origin\nhorizon\nvolumes\n')
                monitor.key('meta_l', 'right')
                monitor.start(0)
                monitor.key('meta_l', 'left')
                monitor.key('f5')
                capture('files-terminal')
                monitor.start(1)
                capture('files-editor')
                for app in (0, 1, 2):
                    monitor.start(app)
                    monitor.key('alt', 'f9')
                monitor.start(6)
                capture('settings')
                text = serial.read_text(errors='replace')
                assert 'PANIC' not in text and '[trap]' not in text
                assert 'PROBE RESULT:' not in text and 'Loom / Nuvora command environment' not in text
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
    manifest = {'capture': 'QMP screendump; lossless P6 to RGB PNG',
                'boot': 'production UEFI USB; q35; 256 MiB; AHCI; USB keyboard/tablet',
                'qemu': subprocess.check_output([qemu.qemu_binary(), '--version'], text=True).splitlines()[0],
                'kernel_sha256': digest(production), 'esp_sha256': digest(source_esp),
                'firmware_sha256': digest(firmware), 'screenshots': pictures}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('PASS actual production captures: wrapped editor Up/Down, saved document and desktop applications', flush=True)


if __name__ == '__main__':
    main()
