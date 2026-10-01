#!/usr/bin/env python3
"""Boot the real UEFI desktop and exercise native windows through QMP.

Requires make all/esp, QEMU, OVMF and FFmpeg. Uses a private disk/firmware copy and
exports PPM screenshots for human review; never opens the user's data image.
"""
import argparse
import json
import pathlib
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts'))
import qemu
from mkgptdisk import create
from import_media import import_files


class Monitor:
    def __init__(self, process):
        self.process = process
        self.messages = queue.Queue()
        self.reader = threading.Thread(target=self._read_output, daemon=True)
        self.reader.start()
        greeting = self._answer()
        if 'QMP' not in greeting:
            raise RuntimeError('QEMU did not open its QMP pipe')
        self.call('qmp_capabilities')

    def _read_output(self):
        for line in self.process.stdout:
            self.messages.put(line)
        self.messages.put(None)

    def _answer(self):
        try:
            line = self.messages.get(timeout=10)
        except queue.Empty as error:
            raise RuntimeError('QEMU QMP response timed out') from error
        if line is None:
            raise RuntimeError('QEMU closed its QMP pipe')
        return json.loads(line)

    def call(self, name, arguments=None):
        self.process.stdin.write((json.dumps({'execute': name, 'arguments': arguments or {}}) + '\n').encode())
        self.process.stdin.flush()
        while True:
            answer = self._answer()
            if 'error' in answer:
                raise RuntimeError(answer['error'])
            if 'return' in answer:
                return answer['return']

    def key(self, *codes):
        self.call('send-key', {'keys': [{'type': 'qcode', 'data': c} for c in codes], 'hold-time': 35})
        time.sleep(.12)

    def hold(self, code, down):
        self.call('input-send-event', {'events': [
            {'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': code}}}]})
        time.sleep(.15)

    def start(self, index):
        self.key('f10')
        for _ in range(index):
            self.key('down')
        self.key('ret')
        time.sleep(.8)

    def type(self, value):
        names = {' ': 'spc', '/': 'slash', '.': 'dot', '-': 'minus', '\n': 'ret'}
        shifted = {'_': 'minus', '+': 'equal', '*': '8', '!': '1', ':': 'semicolon', '@': '2'}
        for ch in value:
            if ch in shifted:
                self.key('shift', shifted[ch])
            elif ch.isupper():
                self.key('shift', ch.lower())
            else:
                self.key(names.get(ch, ch))
        time.sleep(.8)  # Allow the application and compositor to publish the input burst.

    def shot(self, output, name):
        time.sleep(.3)
        path = output / (name + '.ppm')
        self.call('screendump', {'filename': str(path)})
        header, size, maximum, pixels = path.read_bytes().split(b'\n', 3)
        assert header == b'P6' and maximum == b'255'
        width, height = map(int, size.split())
        assert len(pixels) == width * height * 3
        return width, height, pixels

    def pointer(self, width, height, x, y, down):
        self.call('input-send-event', {'events': [
            {'type': 'abs', 'data': {'axis': 'x', 'value': x * 32767 // (width - 1)}},
            {'type': 'abs', 'data': {'axis': 'y', 'value': y * 32767 // (height - 1)}},
            {'type': 'btn', 'data': {'button': 'left', 'down': down}}]})
        time.sleep(.15)


def color_count(pixels, color):
    word = color.to_bytes(3, 'big')
    return sum(pixels[i:i + 3] == word for i in range(0, len(pixels), 3))


def color_bounds(pixels, width, color):
    word = color.to_bytes(3, 'big')
    points = [i // 3 for i in range(0, len(pixels), 3) if pixels[i:i + 3] == word]
    assert points, f'No pixels of {color:06x}'
    return (min(i % width for i in points), min(i // width for i in points),
            max(i % width for i in points), max(i // width for i in points))


def has_video_frame(pixels):
    # The committed fixture is a color test pattern. Check decoded picture
    # pixels, rather than mistaking the player's dark background for video.
    bright = 0
    for i in range(0, len(pixels), 3):
        r, g, b = pixels[i:i + 3]
        bright += g > 200 and r < 80 and b < 80
    return bright > 5000


def ui_scale(width, height):
    return 4 if width >= 3840 and height >= 2000 else 3 if width >= 2880 and height >= 1620 else 2 if width >= 1600 and height >= 900 else 1


def desktop_visible(pixels, scale=1):
    """The Files icon is present in the workspace and Dock, never in sign-in."""
    return color_count(pixels, 0x568ced) > 500 * scale * scale


def account_wait(monitor, output, name, locked=False, rectangle=None):
    """Wait for the real async account button to leave its disabled state."""
    deadline = time.monotonic() + 150
    while True:
        width, height, pixels = monitor.shot(output, name)
        scale = ui_scale(width, height)
        if rectangle:
            x, y, w, h = rectangle
        else:
            sw, sh = width // scale, height // scale
            w, h = (460, 292) if locked else (560, 398)
            w, h = min(w, sw - 32), min(h, sh - 48)
            x, y = (sw - w) // 2, (sh - h) // 2
        px, py = (x + w - 160) * scale, (y + h - 42) * scale
        color = int.from_bytes(pixels[(py * width + px) * 3:(py * width + px) * 3 + 3], 'big')
        if color != 0xe1e6e7:
            return width, height, pixels
        if time.monotonic() > deadline:
            raise RuntimeError('Account password operation did not complete')
        time.sleep(.3)


def first_account(monitor, output, username='tester', password='testable local passphrase 1'):
    deadline=time.monotonic()+60
    while True:
        width,height,pixels=monitor.shot(output,'00-oobe-welcome')
        if color_count(pixels,0xf6f8f9)>100000*ui_scale(width,height)**2: break
        if time.monotonic()>deadline: raise RuntimeError('First sign-in form did not appear')
        time.sleep(.2)
    monitor.key('ret')
    monitor.type(username)
    monitor.key('tab')
    monitor.type('Test User')
    monitor.key('tab')
    monitor.type(password)
    monitor.key('tab')
    monitor.type(password)
    monitor.shot(output, '00a-oobe-create')
    monitor.key('ret')
    account_wait(monitor, output, '00b-oobe-ready')
    monitor.key('ret')
    time.sleep(.8)


def check_keypad(monitor, output):
    """Compare real keypad input with the same text typed on the main keys."""
    monitor.start(1)
    for codes in [('minus',), ('shift', 'equal'), ('shift', '8'), ('slash',)]:
        monitor.key(*codes)
    reference = monitor.shot(output, 'keypad-reference')[2]
    for _ in range(4):
        monitor.key('backspace')
    for codes in [('kp_subtract',), ('kp_add',), ('kp_multiply',), ('kp_divide',)]:
        monitor.key(*codes)
    actual = monitor.shot(output, 'keypad-operators')[2]
    assert actual == reference, 'Keypad -+*/ differs from main-key input in Text Editor'
    monitor.key('alt', 'f4')
    monitor.key('d')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--machine', choices=['pc', 'q35'], default='q35')
    parser.add_argument('--memory', type=int, default=256)
    parser.add_argument('--keyboard', choices=['usb', 'ps2'], default='usb')
    parser.add_argument('--keypad-only', action='store_true', help='Run only the keypad input regression')
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'build/desktop-smoke')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    firmware = qemu.find_uefi_firmware()
    if not firmware:
        raise SystemExit('OVMF not found; set NV_OVMF or install ovmf')
    esp = qemu.BUILD / 'esp.img'
    if not esp.is_file():
        raise SystemExit('Build the current UEFI image first: make all esp')
    ffmpeg = shutil.which('ffmpeg')
    if not ffmpeg:
        raise SystemExit('Install ffmpeg to extend the short playback fixture for interactive checks')
    with tempfile.TemporaryDirectory(prefix='nuvora-window-') as directory:
        private = pathlib.Path(directory)
        disk = private / 'data.img'
        private_esp = private / 'esp.img'
        shutil.copyfile(esp, private_esp)
        clip = private / 'clip.mpg'
        subprocess.run([ffmpeg, '-v', 'error', '-stream_loop', '12', '-i',
                        str(ROOT / 'tests/fixtures/clip.mpg'), '-t', '6', '-c', 'copy',
                        '-y', str(clip)], check=True)
        create(disk, size_mib=128, partitions=1)
        import_files(disk, [clip, ROOT / 'tests/fixtures/tone.mp3'])
        original_build = qemu.BUILD
        qemu.BUILD = private  # Firmware variables belong to this test only.
        try:
            command = qemu.command(memory=args.memory, disk=disk, cpu='max', machine=args.machine,
                                   kernel=False, esp=private_esp, disk_bus='ide')
            command += qemu.firmware_arguments(firmware)
        finally:
            qemu.BUILD = original_build
        command += ['-display', 'none', '-qmp', 'stdio',
                    '-serial', f'file:{output}/serial.log', '-no-reboot',
                    '-device', 'qemu-xhci,id=xhci',
                    '-device', 'usb-tablet,bus=xhci.0', '-audiodev', 'none,id=audio0',
                    '-device', 'intel-hda', '-device', 'hda-duplex,audiodev=audio0']
        if args.keyboard == 'usb':
            command += ['-device', 'usb-kbd,bus=xhci.0']
        with (output / 'qemu.log').open('w') as log:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log)
            try:
                monitor = Monitor(process)
                deadline = time.monotonic() + 40
                while True:
                    text = (output / 'serial.log').read_text(errors='replace')
                    if '[ok] entering Ring 3' in text:
                        break
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError('UEFI kernel did not reach Ring 3')
                    time.sleep(.2)
                time.sleep(1)
                first_account(monitor, output)
                width, height, home = monitor.shot(output, '01-home')
                scale = ui_scale(width, height)
                assert width >= 640 and height >= 480
                assert '[store] no Nuvora data disk' not in text
                check_keypad(monitor, output)
                if args.keypad_only:
                    text = (output / 'serial.log').read_text(errors='replace')
                    assert 'PANIC' not in text and '[trap]' not in text
                    print(f'PASS UEFI keypad ({args.machine}, {args.keyboard} keyboard): '
                          f'-+*/ matches main-key input in Text Editor. Screens: {output}')
                    return
                monitor.start(2)
                _, _, terminal = monitor.shot(output, '02-terminal')
                assert terminal != home and color_count(terminal, 0x15222b) > 50000
                monitor.type('help\n')
                help_screen = monitor.shot(output, '03-terminal-help')[2]
                assert help_screen != terminal
                monitor.key('meta_l')  # A bare Super release opens Start.
                monitor.type('no-such-app')
                empty = monitor.shot(output, '03a-search-empty')[2]
                monitor.key('ret')
                assert monitor.shot(output, '03b-search-empty-enter')[2] == empty
                monitor.key('esc')  # First Escape clears the search.
                assert monitor.shot(output, '03c-search-reset')[2] != empty
                monitor.key('esc')
                assert monitor.shot(output, '03d-search-input-isolated')[2] == help_screen
                monitor.key('f10')
                monitor.type('mp3 video')
                monitor.shot(output, '03e-search-media')
                monitor.key('ctrl', 'a')
                assert color_count(monitor.shot(output, '03f-search-select-all')[2], 0xd5e3e4) > 500 * scale
                monitor.type('media')
                monitor.key('ret')
                time.sleep(.8)
                media = monitor.shot(output, '04-media')[2]
                assert color_count(media, 0x19252b) > 20000
                monitor.start(4)
                folio = monitor.shot(output, '05-folio')[2]
                assert color_count(folio, 0xe1e6e4) > 50000
                monitor.hold('alt', True)
                monitor.key('tab')
                switcher = monitor.shot(output, '05a-switcher')[2]
                assert color_count(switcher, 0xf5f8ff) > 50000
                monitor.key('esc')
                monitor.hold('alt', False)
                assert monitor.shot(output, '05b-switch-cancel')[2] == folio
                monitor.hold('alt', True)
                monitor.key('tab')  # Media, then Terminal, from a stable MRU list.
                monitor.key('tab')
                monitor.hold('alt', False)
                assert color_count(monitor.shot(output, '05c-switch-terminal')[2], 0x15222b) > 50000
                monitor.key('alt', 'tab')
                assert color_count(monitor.shot(output, '05d-switch-mru')[2], 0xe1e6e4) > 50000
                monitor.key('alt', 'shift', 'tab')
                assert color_count(monitor.shot(output, '05e-switch-reverse')[2], 0x19252b) > 20000
                monitor.key('meta_l', 'tab')
                overview = monitor.shot(output, '05f-overview')[2]
                assert color_count(overview, 0x192744) > 50000
                monitor.key('right')  # Restore the already-open Folio.
                monitor.key('ret')
                assert color_count(monitor.shot(output, '05g-overview-select')[2], 0xe1e6e4) > 50000
                monitor.type('12345')
                edited = monitor.shot(output, '06-folio-edited')[2]
                assert edited != folio
                monitor.key('alt', 'f4')
                dialog = monitor.shot(output, '07-folio-unsaved')[2]
                assert dialog != edited
                monitor.key('esc')
                assert monitor.shot(output, '08-folio-cancel')[2] == edited
                monitor.key('alt', 'f4')
                monitor.key('d')
                time.sleep(.5)
                assert color_count(monitor.shot(output, '09-folio-closed')[2], 0xe1e6e4) < 50000
                for _ in range(3):
                    monitor.start(4)
                    monitor.key('7')
                    monitor.key('alt', 'f4')
                    monitor.key('d')
                    time.sleep(.4)
                monitor.key('alt', 'f9')
                assert color_count(monitor.shot(output, '10-media-minimized')[2], 0x19252b) < 20000
                monitor.key('f12')
                monitor.shot(output, '10a-overview-minimized')
                monitor.key('left')  # The minimized Media precedes the focused Terminal in MRU.
                monitor.key('ret')
                restored = monitor.shot(output, '11-media-restored')[2]
                assert color_count(restored, 0x19252b) > 20000
                monitor.key('meta_l', 'left')
                left_tiled = monitor.shot(output, '11a-media-tiled-left')[2]
                assert color_bounds(left_tiled, width, 0x19252b)[2] <= width // 2 + 2
                monitor.key('meta_l', 'right')
                right_tiled = monitor.shot(output, '11b-media-tiled-right')[2]
                assert color_bounds(right_tiled, width, 0x19252b)[0] >= width // 2 - 2
                monitor.key('meta_l', 'up')
                assert monitor.shot(output, '11c-media-super-maximized')[2] != restored
                monitor.key('meta_l', 'down')
                assert monitor.shot(output, '11d-media-super-restored')[2] == restored
                monitor.key('alt', 'f10')
                maximized = monitor.shot(output, '12-media-maximized')[2]
                assert maximized != restored
                monitor.key('alt', 'f10')
                assert monitor.shot(output, '13-media-unmaximized')[2] == restored
                original_bounds = color_bounds(restored, width, 0x19252b)
                monitor.key('meta_l', 'up')
                monitor.pointer(width, height, width // 3, 20, True)
                monitor.pointer(width, height, width // 3 + 80, 120, True)
                monitor.pointer(width, height, width // 3 + 80, 120, False)
                restored = monitor.shot(output, '13-restore-by-title-drag')[2]
                free_bounds = color_bounds(restored, width, 0x19252b)
                assert free_bounds[2] - free_bounds[0] == original_bounds[2] - original_bounds[0]
                assert free_bounds[3] - free_bounds[1] == original_bounds[3] - original_bounds[1]
                left, top, right, bottom = color_bounds(restored, width, 0x19252b)
                title_y = color_bounds(restored, width, 0xf2f5fc)[1] + 12 * scale
                monitor.pointer(width, height, left + 100, title_y, False)
                monitor.pointer(width, height, left + 100, title_y, True)
                monitor.pointer(width, height, left + 50, title_y - 35, True)
                monitor.pointer(width, height, left + 50, title_y - 35, False)
                moved = monitor.shot(output, '13a-media-dragged')[2]
                moved_bounds = color_bounds(moved, width, 0x19252b)
                assert moved_bounds[0] < left and moved_bounds[1] < top
                left, top, right, bottom = moved_bounds
                monitor.pointer(width, height, right + 1, bottom - 10, True)
                monitor.pointer(width, height, right - 69, bottom - 10, True)
                monitor.pointer(width, height, right - 69, bottom - 10, False)
                resized = monitor.shot(output, '13b-media-resized')[2]
                assert color_bounds(resized, width, 0x19252b)[2] < right - 20
                left, top, right, bottom = color_bounds(resized, width, 0x19252b)
                title_y = color_bounds(resized, width, 0xf2f5fc)[1] + 12 * scale
                monitor.pointer(width, height, left + 100, title_y, True)
                monitor.pointer(width, height, 3, height // 3, True)
                preview = monitor.shot(output, '13c-drag-snap-preview')[2]
                assert preview[:3] == bytes.fromhex('91b2f4')
                monitor.pointer(width, height, 3, height // 3, False)
                snapped = monitor.shot(output, '13d-drag-snap-committed')[2]
                assert color_bounds(snapped, width, 0x19252b)[2] <= width // 2 + 2
                monitor.key('meta_l', 'down')
                free = monitor.shot(output, '13e-drag-snap-restored')[2]
                assert color_bounds(free, width, 0x19252b) == color_bounds(resized, width, 0x19252b)
                monitor.start(0)
                monitor.start(1)
                for _ in range(3):
                    monitor.start(4)
                monitor.key('f12')
                first_page = monitor.shot(output, '13f-overview-seven-windows')[2]
                monitor.key('pgdn')
                second_page = monitor.shot(output, '13g-overview-next-page')[2]
                assert second_page != first_page and color_count(second_page, 0x15222b) > 100000
                assert color_count(second_page, 0xe1e6e4) < 50000
                monitor.key('pgup')
                assert monitor.shot(output, '13h-overview-previous-page')[2] == first_page
                left, top, _, _ = color_bounds(first_page, width, 0x91b2f4)
                monitor.pointer(width, height, left + 20, top + 20, True)
                monitor.pointer(width, height, left + 20, top + 20, False)
                assert color_count(monitor.shot(output, '13i-overview-pointer-select')[2], 0xe1e6e4) > 50000
                for _ in range(3):  # Three clean Folios, then the two built-in apps.
                    monitor.key('alt', 'f4')
                    time.sleep(.4)
                monitor.key('alt', 'f4')
                monitor.key('alt', 'f4')
                monitor.key('alt', 'f4')
                time.sleep(.5)
                monitor.start(2)  # Explicit focus after asynchronous client close events.
                monitor.type('media /home/clip.mpg\n')
                deadline = time.monotonic() + 20
                while True:
                    playback = monitor.shot(output, '14-video')[2]
                    if has_video_frame(playback):
                        break
                    if time.monotonic() > deadline:
                        raise AssertionError('Media did not publish a decoded MPEG frame')
                monitor.key('spc')
                time.sleep(.5)
                playback = monitor.shot(output, '14-video')[2]
                assert has_video_frame(playback)
                time.sleep(.7)
                assert monitor.shot(output, '14a-video-paused')[2] == playback
                monitor.key('alt', 'tab')
                during = monitor.shot(output, '15-switch-during-playback')[2]
                assert during != playback
                monitor.start(0)
                assert monitor.shot(output, '16-files-during-playback')[2] != during
                text = (output / 'serial.log').read_text(errors='replace')
                assert 'PANIC' not in text and '[trap]' not in text
                print(f'PASS UEFI desktop ({args.machine}, {args.memory} MiB, {args.keyboard} keyboard): keypad -+*/, native terminal/media/Folio, input, '
                      f'unsaved cancel/discard, minimize, maximize, restore, mouse drag/resize and '
                      f'search isolation, held/released Alt MRU, overview, keyboard/drag tiling, '
                      f'decoded MPEG picture with window switching. Screens: {output}')
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == '__main__':
    main()
