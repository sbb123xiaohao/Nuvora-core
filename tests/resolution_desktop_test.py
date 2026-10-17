#!/usr/bin/env python3
"""Check responsive Terminal windows in real production UEFI framebuffers."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
import time
from desktop_boot_test import Monitor, first_account, ui_scale, qemu
from session_boot_test import wait_saved_file
from capture_screenshots import write_png
from mkgptdisk import create
from import_media import import_files


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def body(pixels, width, scale):
    top, bottom = 28 * scale, (len(pixels) // (width * 3) // scale - 56) * scale - scale
    return b''.join(pixels[(y * width + scale) * 3:
                           (y * width + width - scale) * 3] for y in range(top, bottom))


def check_fill(width, height, pixels):
    scale = ui_scale(width, height)
    font_scale = 2 if width >= 1000 and height >= 700 else 1
    cell_w, cell_h = 6 * font_scale, 13 if font_scale == 1 else 12 * font_scale
    left, top = scale, 28 * scale
    right, bottom = width - scale, (height // scale - 56) * scale - scale
    for x, y in ((left, top), (right - 1, top), (left, bottom - 1), (right - 1, bottom - 1)):
        assert pixels[(y * width + x) * 3:(y * width + x) * 3 + 3] == b'\x15\x22\x2b', \
            'Maximized Terminal does not cover its content rectangle'
    points = []
    for y in range(top, bottom):
        row = pixels[y * width * 3:(y + 1) * width * 3]
        points.extend((x, y) for x in range(left, right) if row[x * 3:x * 3 + 3] == b'\xe1\xe6\xe4')
    assert points, 'Terminal glyphs missing'
    bounds = (min(x for x, _ in points), min(y for _, y in points),
              max(x for x, _ in points), max(y for _, y in points))
    padding = 6 * font_scale
    assert bounds[0] < left + padding + cell_w, 'Text is still horizontally centered'
    assert bounds[1] < top + padding + cell_h, 'Text is still vertically centered'
    assert bounds[2] > right - padding - 2 * cell_w, 'Output still wraps in a fixed narrow grid'
    assert bounds[3] > bottom - padding - 3 * cell_h, 'Output does not use the available rows'
    return bounds


def run(resolution, output, firmware):
    output.mkdir(parents=True, exist_ok=True)
    serial = output / 'serial.log'
    captures = []
    with tempfile.TemporaryDirectory(prefix='nuvora-resolution-') as directory:
        private = pathlib.Path(directory)
        disk, esp = private / 'data.img', private / 'esp.img'
        shutil.copyfile(qemu.BUILD / 'esp.img', esp)
        fixture = private / 'terminal-grid.txt'
        fixture.write_text(''.join(f'{n:03d} | Nuvora responsive Terminal | ' +
                                  '0123456789 abcdefghijklmnopqrstuvwxyz ' * 5 + '\n'
                                  for n in range(160)), encoding='ascii')
        create(disk, size_mib=128, partitions=1)
        import_files(disk, [fixture])
        original = qemu.BUILD
        try:
            qemu.BUILD = private
            command = qemu.command(memory=256, disk=disk, machine='q35', kernel=False,
                                   esp=esp, disk_bus='nvme', resolution=resolution)
            command += qemu.firmware_arguments(firmware)
        finally:
            qemu.BUILD = original
        command += ['-display', 'none', '-qmp', 'stdio', '-serial', f'file:{serial}', '-no-reboot',
                    '-device', 'qemu-xhci,id=xhci', '-device', 'usb-kbd,bus=xhci.0',
                    '-device', 'usb-tablet,bus=xhci.0']
        with (output / 'qemu.log').open('w') as log:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log)
            try:
                monitor = Monitor(process)
                deadline = time.monotonic() + 60
                while 'Ring 3: /apps/session' not in serial.read_text(errors='replace'):
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise AssertionError('Production desktop did not boot')
                    time.sleep(.2)
                first_account(monitor, output)
                monitor.start(2)
                monitor.key('alt', 'f10')
                monitor.type('scrub\nunfold /home/terminal-grid.txt\n')
                width, height, pixels = monitor.shot(output, 'maximized')
                assert f'{width}x{height}' == resolution, 'UEFI did not select the requested mode'
                bounds = check_fill(width, height, pixels)

                def capture(name, frame):
                    path = output / (name + '.png')
                    write_png(path, width, height, frame)
                    captures.append({'file': f'{resolution}/{name}.png', 'width': width,
                                     'height': height, 'sha256': digest(path)})

                capture('maximized', pixels)
                expected = body(pixels, width, ui_scale(width, height))
                monitor.key('meta_l', 'left')
                capture('half-screen', monitor.shot(output, 'half-screen')[2])
                monitor.key('meta_l', 'up')
                restored = monitor.shot(output, 'after-half-screen')[2]
                assert body(restored, width, ui_scale(width, height)) == expected, \
                    'Reflow changed output when returning from half-screen'
                monitor.key('alt', 'f10')
                capture('floating', monitor.shot(output, 'floating')[2])
                monitor.key('alt', 'f10')
                restored = monitor.shot(output, 'restored')[2]
                assert body(restored, width, ui_scale(width, height)) == expected, \
                    'Reflow changed output after restoring the original window'
                monitor.type('weave layout-ready.txt resized-ok\nanchor\n')
                wait_saved_file(monitor, disk, '/home/users/tester/layout-ready.txt',
                                b'resized-ok\n', timeout=120)
                text = serial.read_text(errors='replace')
                assert 'PANIC' not in text and '[isolate' not in text and 'PROBE RESULT:' not in text
                print(f'PASS {resolution}: full content bounds, glyph bounds {bounds}, '
                      'half-screen/floating/maximize reflow exact, USB input and saved file', flush=True)
            except BaseException:
                if process.poll() is None:
                    try:
                        monitor.shot(output, 'failure')
                    except Exception:
                        pass
                raise
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
    return captures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resolutions', nargs='+', type=qemu.display_resolution,
                        default=['640x480', '1280x800', '1920x1080'])
    parser.add_argument('--output', type=pathlib.Path, default=qemu.BUILD / 'resolution-desktop')
    options = parser.parse_args()
    output = options.output.resolve()
    firmware = qemu.find_uefi_firmware()
    if not firmware:
        raise SystemExit('OVMF required')
    manifest = {'capture': 'production UEFI / q35 / 256 MiB / NVMe / USB keyboard',
                'kernel_sha256': digest(qemu.BUILD / 'nuvora-uefi.elf'),
                'esp_sha256': digest(qemu.BUILD / 'esp.img'), 'images': []}
    for resolution in options.resolutions:
        manifest['images'] += run(resolution, output / resolution, firmware)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('PASS responsive Terminal: actual GOP captures; original PPM files retained', flush=True)


if __name__ == '__main__':
    main()
