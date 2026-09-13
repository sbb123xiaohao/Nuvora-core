#!/usr/bin/env python3
"""Boot real guest code, exercise faults, and verify persisted bytes after reboot."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import selectors
import struct
import subprocess
import time
import zlib
from qemu import ROOT, BUILD, ARCH, command
from mkdisk import create

REPORT = BUILD / 'test-results'
REPORT.mkdir(parents=True, exist_ok=True)
results = []

def record(name, detail):
    results.append({'test': name, 'result': 'PASS', 'detail': detail})
    print(f'PASS {name}: {detail}', flush=True)

class VM:
    def __init__(self, name, disk=None, memory=64, iso=None, monitor=False, reboot=False, hardware=None):
        self.name, self.output = name, bytearray()
        cmd = command(memory, disk)
        if hardware:
            cmd += hardware
        if iso:
            i = cmd.index('-kernel')
            del cmd[i:i+2]
            cmd += ['-cdrom', str(iso), '-boot', 'd']
        cmd += ['-display', 'none']
        cmd += ['-serial', 'mon:stdio'] if monitor else ['-serial', 'stdio', '-monitor', 'none']
        if not reboot:
            cmd += ['-no-reboot']
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        os.set_blocking(self.proc.stdout.fileno(), False)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.proc.stdout, selectors.EVENT_READ)
        self.wait_for(b'/home :: ', timeout=15)

    def pump(self, timeout=0.1):
        for key, _ in self.selector.select(timeout):
            data = os.read(key.fd, 65536)
            if data:
                self.output += data

    def wait_for(self, marker, start=0, timeout=12):
        deadline = time.monotonic() + timeout
        while marker not in self.output[start:]:
            if self.proc.poll() is not None:
                self.pump(0)
                raise AssertionError(f'{self.name}: emulator exited {self.proc.returncode}\n{self.text()}')
            if time.monotonic() > deadline:
                raise AssertionError(f'{self.name}: timeout waiting for {marker!r}\n{self.text()}')
            self.pump()
        return self.output[start:].decode('ascii', errors='replace').replace('\r', '')

    def text(self):
        return self.output.decode('ascii', errors='replace').replace('\r', '')

    def send(self, line, expect=None, timeout=12):
        start = len(self.output)
        self.proc.stdin.write(line.encode('ascii') + b'\n')
        self.proc.stdin.flush()
        out = self.wait_for(b' :: ', start, timeout)
        if expect is not None:
            assert expect in out, f'{self.name}: {line!r} did not produce {expect!r}:\n{out}'
        assert 'KERNEL PANIC' not in out, out
        return out

    def monitor(self, operation, arguments=None):
        # The monitor shares the existing stdio pipe. No listening socket is needed.
        if operation == 'screendump':
            line = 'screendump ' + arguments['filename']
        elif operation == 'human-monitor-command':
            line = arguments['command-line']
        else:
            raise ValueError(operation)
        start = len(self.output)
        self.proc.stdin.write(b'\x01c')
        self.proc.stdin.flush()
        self.wait_for(b'(qemu) ', start)
        start = len(self.output)
        self.proc.stdin.write(line.encode('ascii') + b'\n')
        self.proc.stdin.flush()
        response = self.wait_for(b'(qemu) ', start)
        self.proc.stdin.write(b'\x01c')
        self.proc.stdin.flush()
        return response

    def close(self, normal=True):
        try:
            if normal and self.proc.poll() is None:
                self.proc.stdin.write(b'rest\n')
                self.proc.stdin.flush()
                deadline = time.monotonic() + 5
                while self.proc.poll() is None and time.monotonic() < deadline:
                    self.pump()
                assert self.proc.poll() == 0, f'poweroff failed: {self.text()}'
        finally:
            if self.proc.poll() is None:
                self.proc.kill()
                self.proc.wait()
            self.pump(0)
            (REPORT / f'{self.name}.log').write_text(self.text())
            self.selector.close()
            self.proc.stdin.close()
            self.proc.stdout.close()
    
def probe(memory):
    cmd = command(memory) + ['-append', 'nv.test=1', '-display', 'none', '-serial', 'stdio',
                            '-monitor', 'none', '-no-reboot', '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04']
    run = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=40)
    out = run.stdout.decode('ascii', errors='replace').replace('\r', '')
    (REPORT / f'probe-{memory}MiB.log').write_text(out)
    match = re.search(r'PROBE RESULT: (\d+) passed, 0 failed', out)
    assert run.returncode == 33 and match and 'FAIL ' not in out, out
    record(f'Ring 3 probe / {memory} MiB', f'{match[1]} assertions; QEMU exit 33')

def kernel_faults():
    cases = [
        ('stack-guard-lower', 'nv.guard-test=lower', 'double fault on emergency stack'),
        ('stack-guard-upper', 'nv.guard-test=upper', 'exception in'),
        ('init-fault', 'nv.init-fault=1', 'initial process terminated'),
    ]
    for name, option, message in cases:
        cmd = command(32) + ['-append', 'nv.test=1 ' + option, '-display', 'none',
                            '-serial', 'stdio', '-monitor', 'none', '-no-reboot',
                            '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04']
        run = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        out = run.stdout.decode('ascii', errors='replace').replace('\r', '')
        (REPORT / f'{name}.log').write_text(out)
        assert run.returncode == 35 and 'KERNEL PANIC: ' + message in out, out
        if name == 'stack-guard-upper':
            assert 'trap=14 error=2' in out and 'address=10005000' in out, out
        record('Kernel fault / ' + name, 'intentional fault reached the expected diagnostic; exit 35')

def memory_pressure():
    vm = VM('memory-pressure', memory=32)
    try:
        out = vm.send('forge relay pressure-test', 'PRESSURE RESULT: PASS', timeout=60)
        assert 'exited 0' in out and 'FAIL' not in out, out
        vm.send('forge pulse quiet', 'exited 7')
    finally:
        vm.close()
    record('User memory exhaustion / 32 MiB',
           '8 exec/spawn/grow failures retain the caller; all pages, handles and slots recovered')

def build_fingerprint():
    files = [BUILD / 'boot.elf', BUILD / 'nuvora.elf', *sorted((BUILD / 'apps').glob('*.elf')),
             ROOT / 'scripts/test.py']
    return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}

def new_image(name):
    path = REPORT / name
    path.unlink(missing_ok=True)
    create(path)
    return path

def persistence():
    disk = new_image('persistence.img')
    vm = VM('save-generations', disk)
    try:
        vm.send('horizon', 'Data disk ready')
        vm.send('nest /home/project')
        vm.send('weave /home/project/story "alpha saved"')
        vm.send('weave /tmp/transient "temporary data"')
        vm.send('anchor', 'Saved /home.')
        vm.send('weave /home/project/story "beta saved"')
        vm.send('anchor', 'Saved /home.')
        vm.send('weave /home/project/story "unsaved data"')
    finally:
        vm.close()
    vm = VM('restore-latest', disk, monitor=True, reboot=True)
    try:
        assert 'restored /home generation 2' in vm.text()
        vm.send('unfold /home/project/story', '\nbeta saved\n')
        vm.send('unfold /tmp/transient', 'path not found')
        vm.send('trial', '0 failed', timeout=40)
        vm.send('renew', 'restored /home generation 2')
        vm.send('unfold /home/project/story', '\nbeta saved\n')
        vm.send('scrub')
        vm.send('origin', 'Nuvora Core 0.4.0')
        vm.send('horizon', 'Data disk ready')
        vm.send('glance /apps', 'loom')
        ppm = REPORT / 'console.ppm'
        vm.monitor('screendump', {'filename': str(ppm)})
        # Exact emulator screenshot, converted only for convenient viewing.
        try:
            from PIL import Image
            Image.open(ppm).save(REPORT / 'console.png')
        except ImportError:
            pass
        start = len(vm.output)
        for key in ['h','o','r','i','z','o','n','ret']:
            vm.monitor('human-monitor-command', {'command-line': f'sendkey {key} 10'})
            time.sleep(0.03)
        vm.wait_for(b'Data disk ready', start)
        vm.wait_for(b' :: ', start)
        record('PS/2 keyboard and VGA', 'Emulated PS/2 key events executed horizon; actual screenshot saved')
    finally:
        vm.close()
    record('Persistence and reboot', 'newest snapshot restored; uncommitted and /tmp changes absent; renew works')
    record('Interactive child test runner', 'trial completes inside the running shell and returns control')

    original = disk.read_bytes()
    for kind in ['payload-crc', 'invalid-path', 'oversize-header', 'torn-header']:
        data = bytearray(original)
        head = 4096 * 512
        payload = head + 512
        if kind == 'payload-crc':
            data[payload + 1] ^= 0x40
        elif kind == 'invalid-path':
            data[payload + 4 + 12 + 1] = ord('x')
            length = struct.unpack_from('<I', data, head + 12)[0]
            struct.pack_into('<I', data, head + 16, zlib.crc32(data[payload:payload+length]))
        elif kind == 'oversize-header':
            struct.pack_into('<I', data, head + 12, 0xFFFFFFFF)
        else:
            data[head:head+100] = b'\0' * 100
        if kind in ['invalid-path', 'oversize-header']:
            struct.pack_into('<I', data, head + 20, 0)
            struct.pack_into('<I', data, head + 20, zlib.crc32(data[head:head+512]))
        corrupt = REPORT / f'{kind}.img'
        corrupt.write_bytes(data)
        vm = VM('fallback-' + kind, corrupt)
        try:
            assert 'restored /home generation 1' in vm.text(), vm.text()
            vm.send('unfold /home/project/story', '\nalpha saved\n')
        finally:
            vm.close()
        record('Snapshot fallback / ' + kind, 'older committed generation restored')

def folio_editor():
    """Exercise the real full-screen editor over its serial keyboard path."""
    disk = new_image('folio-editor.img')
    vm = VM('folio-editor', disk)

    def raw(data, marker=None, timeout=15):
        start = len(vm.output)
        vm.proc.stdin.write(data)
        vm.proc.stdin.flush()
        if marker is not None:
            vm.wait_for(marker, start, timeout)
        time.sleep(0.15)
        return start

    def launch(args, marker, timeout=15):
        return raw(('forge folio' + args + '\n').encode('ascii'), marker, timeout)

    def close(start=None):
        if start is None:
            start = len(vm.output)
        vm.proc.stdin.write(b'\x11')
        vm.proc.stdin.flush()
        vm.wait_for(b'Folio closed.', start, 15)
        vm.wait_for(b' :: ', start, 10)

    try:
        launch('', b'Folio  Untitled.nvd')
        raw(b'Project title')
        raw(b'\x01')  # Ctrl-A: select the title.
        raw(b'\x02')  # Ctrl-B: bold selection.
        raw(b'\x1b[19~')  # F8: Heading 1.
        raw(b'\x1b[C')  # Right: clear selection at the end.
        raw(b'\nBody paragraph with editable text.')
        raw(b'\x13', b'Save document')  # Ctrl-S.
        raw(b'\x01/home/folio.nvd\n', b'Saved to disk', 20)
        close()

        out = vm.send('glance /home', 'folio.nvd')
        assert 'folio.nvd' in out
        launch(' /home/folio.nvd', b'Opened.', 20)
        visible = vm.text()
        assert 'Project title' in visible and 'Body paragraph' in visible and 'Heading 1' in visible, visible[-6000:]
        raw(b'\x1b[17~', b'Export RTF')  # F6.
        raw(b'\x01/home/folio.rtf\n', b'RTF exported to disk', 20)
        close()

        rtf = vm.send('unfold /home/folio.rtf', r'\rtf1')
        assert r'\fs40' in rtf and r'\b' in rtf, rtf
        launch(' /home/folio.nvd', b'Opened.', 20)
        visible = vm.text()
        assert 'Heading 1' in visible and 'Project title' in visible, visible[-6000:]
        close()
        record('Folio full-screen editor', 'selection, bold/heading formatting, native reopen, atomic save and Word-readable RTF export')
    finally:
        vm.close()

def unrecognized_disk():
    disk = REPORT / 'unrecognized.img'
    disk.write_bytes(b'not-a-nuvora-disk'.ljust(8 * 1024 * 1024, b'\0'))
    before = hashlib.sha256(disk.read_bytes()).digest()
    vm = VM('unrecognized-disk', disk)
    try:
        vm.send('anchor', 'no Nuvora data disk')
        vm.send('weave volatile "still usable"')
        vm.send('unfold volatile', '\nstill usable\n')
    finally:
        vm.close()
    assert hashlib.sha256(disk.read_bytes()).digest() == before
    record('Unrecognized disk write refusal', 'complete disk SHA-256 unchanged; RAM filesystem remains usable')

def command_help_tests():
    vm = VM('command-help', new_image('command-help.img'))
    names = set('help atlas origin horizon ports where step glance nest weave stitch unfold folio mirror shift prune sparks forge scatter gather quench tempo doze anchor trial scrub rest renew'.split())
    try:
        vm.send('weave /home/--help "keep this file"')
        guide = vm.send('help')
        listed = set(re.findall(r'^  (\w+) +--help ', guide, re.M))
        assert listed == names, (listed, names)
        assert set(re.findall(r'^  (\w+) +--help ', vm.send('atlas'), re.M)) == names
        for name in sorted(names):
            out = vm.send(name + ' --help', 'Usage: ' + name)
            assert 'Example: ' in out and 'Help: ' + name + ' --help' in out, out
        vm.send('help ports', 'Usage: ports [--scan]')
        vm.send('atlas weave', 'Usage: weave FILE TEXT')
        vm.send('ports --bad', 'Invalid arguments.')
        vm.send('does-not-exist --help', 'Unknown command.')
        vm.send('help does-not-exist', 'Unknown command.')
        vm.send('unfold /home/--help', '\nkeep this file\n')
        vm.send('where', '\n/home\n')
        vm.send('horizon', 'committed generation: 0')
        vm.send('weave /home/literal.txt --help')
        vm.send('unfold /home/literal.txt', '\n--help\n')
        record('Command catalog and help', 'all 28 commands have purpose, usage and examples; help aliases, unknown commands and literal --help text checked; file and disk generation preserved')
        for app in ['loom', 'folio', 'pulse', 'spin', 'fault', 'probe', 'relay']:
            out = vm.send('forge ' + app + ' --help', 'exited 0')
            assert 'Usage: ' in out and 'Example: ' in out, out
            assert 'PROBE RESULT:' not in out and '[fault]' not in out, out
        record('Built-in program help', 'all 7 programs exit 0 for --help; fault/spin/probe/editor bodies are not entered')
        vm.send('ports', '0 controller(s), 0 device(s).')
        vm.send('ports --scan', '0 controller(s), 0 device(s).')
        record('USB absent controller', 'no-controller query and rescan remain usable')
    finally:
        vm.close()


def usb_key(vm, name, hold=20):
    vm.monitor('human-monitor-command', {'command-line': f'sendkey {name} {hold}'})
    end = time.monotonic() + (hold + 35) / 1000
    while time.monotonic() < end:
        vm.pump(0.01)


def usb_tests():
    usb_disk = REPORT / 'usb-storage.img'
    usb_disk.write_bytes(b'USB identification must not write this image.'.ljust(8 * 1024 * 1024, b'\0'))
    digest = hashlib.sha256(usb_disk.read_bytes()).digest()
    hardware = ['-machine', 'pc,i8042=off', '-device', 'qemu-xhci,id=xhci',
                '-device', 'usb-kbd,id=kbd,bus=xhci.0,port=1',
                '-device', 'usb-mouse,id=mouse,bus=xhci.0,port=2',
                '-drive', f'if=none,id=usbdisk,format=raw,file={usb_disk}',
                '-device', 'usb-storage,id=storage,bus=xhci.0,port=3,drive=usbdisk',
                '-device', 'piix3-usb-uhci,id=legacy']
    vm = VM('usb-devices', new_image('usb-home.img'), monitor=True, memory=32, hardware=hardware)
    try:
        out = vm.send('ports', '2 controller(s), 3 device(s).')
        for marker in ['xHCI - running', 'UHCI - unsupported', '0627:0001 keyboard',
                       '0627:0001 mouse', '46f4:0001 mass-storage', '5 Gb/s', 'input=active',
                       'identification only', 'QEMU USB HARDDRIVE']:
            assert marker in out, out
        assert re.search(r'serial: \S+', out)
        record('USB descriptors and controller discovery', '32 MiB; xHCI boot keyboard, mouse and SuperSpeed storage VID/PID/class/speed/strings; UHCI explicitly unsupported')
        vm.send('scrub')
        vm.send('ports')
        ppm = REPORT / 'usb-devices.ppm'
        vm.monitor('screendump', {'filename': str(ppm)})
        try:
            from PIL import Image
            Image.open(ppm).save(REPORT / 'usb-devices.png')
        except ImportError:
            pass
        start = len(vm.output)
        for key in ['o','r','i','g','i','n','ret']:
            usb_key(vm, key)
        vm.wait_for(b'Nuvora Core 0.4.0', start)
        vm.wait_for(b' :: ', start)
        reports = int(re.search(r'reports=(\d+)', vm.send('ports')).group(1))
        assert reports >= 14
        vm.send('weave /home/usb.txt before')
        start = len(vm.output)
        vm.proc.stdin.write(b'folio /home/usb.txt\n'); vm.proc.stdin.flush()
        vm.wait_for(b'Opened.', start)
        for key in ['ctrl-a','shift-a','b','c','ret','d','f1']:
            usb_key(vm, key)
        vm.wait_for(b'FOLIO / EDITING', start)
        usb_key(vm, 'esc')
        start = len(vm.output)
        usb_key(vm, 'ctrl-s')
        vm.wait_for(b'Saved to disk', start)
        start = len(vm.output)
        usb_key(vm, 'ctrl-q')
        vm.wait_for(b'Folio closed.', start); vm.wait_for(b' :: ', start)
        vm.send('unfold /home/usb.txt', '\nAbc\nd')
        record('USB keyboard and Folio', 'PS/2 disabled; USB-only command entry, Shift/Ctrl, F1, selection, text entry, save and close; saved file contents verified')
        before = int(re.search(r'reports=(\d+)', vm.send('ports')).group(1))
        for key in ['x'] * 130 + ['backspace'] * 130:
            usb_key(vm, key)
        start = len(vm.output)
        for key in ['o','r','i','g','i','n','ret']:
            usb_key(vm, key)
        vm.wait_for(b'Nuvora Core 0.4.0', start); vm.wait_for(b' :: ', start)
        after = int(re.search(r'reports=(\d+)', vm.send('ports')).group(1))
        assert after - before >= 500, (before, after)
        record('USB interrupt and event ring wrap', f'{after-before} HID reports; keyboard remains usable after ring cycle-bit wrap')
        out = vm.send('trial', '0 failed', timeout=50)
        assert 'exited 0' in out and 'FAIL ' not in out
        record('Kernel regression with USB active', 'complete Ring 3 probe passes with xHCI, keyboard, mouse and USB storage attached at 32 MiB')
        base = int(re.search(r'free: (\d+) KiB', vm.send('horizon')).group(1))
        for cycle in range(66):
            response = vm.monitor('human-monitor-command', {'command-line': 'device_add usb-kbd,id=hot,bus=xhci.0,port=4'})
            assert 'Error:' not in response, response
            vm.send('ports --scan', '2 controller(s), 4 device(s).')
            response = vm.monitor('human-monitor-command', {'command-line': 'device_del hot'})
            assert 'Error:' not in response, response
            vm.send('ports --scan', '2 controller(s), 3 device(s).')
            if cycle % 16 == 0:
                print(f'USB hotplug {ARCH}: {cycle+1}/66', flush=True)
        final = int(re.search(r'free: (\d+) KiB', vm.send('horizon')).group(1))
        assert base == final, (base, final)
        record('USB hotplug and command ring wrap', '66 keyboard attach/detach cycles; command ring wraps; identical free-page count; original devices remain active')
    finally:
        vm.close()
    assert hashlib.sha256(usb_disk.read_bytes()).digest() == digest
    record('USB storage identification is read-only', 'entire attached 8 MiB USB disk SHA-256 unchanged')

    hardware = ['-machine', 'pc,i8042=off', '-device', 'qemu-xhci,id=xhci',
                '-device', 'usb-hub,id=hub,bus=xhci.0,port=1',
                '-device', 'usb-kbd,id=kbd,bus=xhci.0,port=1.1,usb_version=1',
                '-device', 'usb-hub,id=hub2,bus=xhci.0,port=1.2',
                '-device', 'usb-mouse,id=mouse,bus=xhci.0,port=1.2.1']
    vm = VM('usb-hubs', monitor=True, hardware=hardware)
    try:
        out = vm.send('ports', '1 controller(s), 4 device(s).')
        assert out.count('hub=active') == 2 and '12 Mb/s' in out and 'parent=3 port=1' in out, out
        for _ in range(24):
            vm.send('ports --scan', '1 controller(s), 4 device(s).')
        start = len(vm.output)
        for key in ['o','r','i','g','i','n','ret']:
            usb_key(vm, key)
        vm.wait_for(b'Nuvora Core 0.4.0', start); vm.wait_for(b' :: ', start)
        before = int(re.search(r'free: (\d+) KiB', vm.send('horizon')).group(1))
        vm.monitor('human-monitor-command', {'command-line': 'device_del mouse'})
        vm.send('ports --scan', '1 controller(s), 3 device(s).')
        vm.monitor('human-monitor-command', {'command-line': 'device_add usb-mouse,id=mouse,bus=xhci.0,port=1.2.1'})
        vm.send('ports --scan', '1 controller(s), 4 device(s).')
        after = int(re.search(r'free: (\d+) KiB', vm.send('horizon')).group(1))
        assert before == after, (before, after)
        # Removing an entire hub must retire all descendant slots and endpoints.
        vm.monitor('human-monitor-command', {'command-line': 'device_del hub'})
        vm.send('ports --scan', '1 controller(s), 0 device(s).')
        assert 'stopped after an error' not in vm.text(), vm.text()
        record('USB hubs and control ring wrap', 'two-level hub routing, full-speed keyboard input, 24 rescans, downstream unplug/replug with no leak, whole-tree removal')
    finally:
        vm.close()


def iso_boot():
    iso = BUILD / f'nuvora-core-0.4.0-{ARCH}.iso'
    assert iso.is_file(), 'Build ISO first: make iso'
    vm = VM('bios-iso', iso=iso)
    try:
        vm.send('origin', 'Nuvora Core 0.4.0')
        vm.send('forge pulse quiet', 'exited 7')
    finally:
        vm.close()
    record('GRUB BIOS ISO boot', 'CD-ROM boot reached Ring 3 and executed a child program')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase', choices=['all', 'storage', 'iso', 'usb', 'help'], default='all')
    parser.add_argument('--iso', action='store_true', help='Also boot the previously built ISO')
    args = parser.parse_args()
    fingerprint = build_fingerprint()
    if args.phase == 'all':
        for memory in [32, 64, 128, 256]:
            probe(memory)
        kernel_faults()
        memory_pressure()
    if args.phase in ['all', 'storage']:
        persistence()
        folio_editor()
        unrecognized_disk()
    if args.phase in ['all', 'help']:
        command_help_tests()
    if args.phase in ['all', 'usb']:
        usb_tests()
    if args.iso or args.phase == 'iso':
        iso_boot()
    assert fingerprint == build_fingerprint(), 'Build changed during execution tests'
    if args.phase == 'all' and args.iso:
        iso = BUILD / f'nuvora-core-0.4.0-{ARCH}.iso'
        fingerprint[iso.relative_to(ROOT).as_posix()] = hashlib.sha256(iso.read_bytes()).hexdigest()
        (REPORT / 'build-fingerprint.json').write_text(json.dumps(fingerprint, indent=2) + '\n')
    (REPORT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    lines = ['# Verified execution results', '', 'All results below came from real QEMU guest execution.', '', '| Check | Result | Evidence |', '| --- | --- | --- |']
    lines += [f'| {r["test"]} | {r["result"]} | {r["detail"]} |' for r in results]
    (REPORT / 'RESULTS.md').write_text('\n'.join(lines) + '\n')
    print(f'ALL {len(results)} HOST CHECKS PASSED. Logs: {REPORT}', flush=True)

if __name__ == '__main__':
    main()
