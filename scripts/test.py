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
from qemu import ROOT, BUILD, ARCH, command, find_uefi_firmware, firmware_arguments
from mkdisk import create, SLOT0_LBA, SLOT1_LBA, SLOT_SECTORS

REPORT = BUILD / 'test-results'
REPORT.mkdir(parents=True, exist_ok=True)
results = []
VERSION = '0.9.0'
EXPECTED_ASSERTIONS = 133
if ARCH != 'x86_64':
    raise SystemExit('Use scripts/arm64.py test for ARM64; 32-bit x86 is retired')

def record(name, detail):
    results.append({'test': name, 'result': 'PASS', 'detail': detail})
    print(f'PASS {name}: {detail}', flush=True)

class VM:
    def __init__(self, name, disk=None, memory=64, iso=None, monitor=False, reboot=False, hardware=None, cpu=None, machine='pc', kernel=True, uefi=False, boot_timeout=15):
        self.name, self.output = name, bytearray()
        if uefi:
            firmware = find_uefi_firmware()
            if not firmware:
                raise RuntimeError('UEFI firmware not found; set NV_OVMF to boot UEFI guests.')
            cmd = command(memory, disk, cpu=cpu, machine=machine, kernel=False,
                          esp=None if iso else BUILD / 'esp.img') + firmware_arguments(firmware)
        else:
            cmd = command(memory, disk, cpu=cpu, machine=machine, kernel=kernel)
        if hardware:
            cmd += hardware
        if iso:
            if '-kernel' in cmd:
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
        try:
            self.wait_for(b'/home :: ', timeout=boot_timeout)
        except BaseException:
            self.close(normal=False)
            raise

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
    
def probe(memory, cpu=None, timeout=40):
    cmd = command(memory, cpu=cpu) + ['-append', 'nv.test=1', '-display', 'none', '-serial', 'stdio',
                            '-monitor', 'none', '-no-reboot', '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04']
    run = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    out = run.stdout.decode('ascii', errors='replace').replace('\r', '')
    name = f'cpu-{cpu}' if cpu else f'probe-{memory}MiB'
    (REPORT / f'{name}.log').write_text(out)
    match = re.search(r'PROBE RESULT: (\d+) passed, 0 failed', out)
    assert run.returncode == 33 and match and int(match[1]) == EXPECTED_ASSERTIONS and 'FAIL ' not in out, out
    skips = len(re.findall(r'^SKIP ', out, re.M))
    record(f'CPU model / {cpu}' if cpu else f'Ring 3 probe / {memory} MiB', f'{match[1]} assertions; {skips} explicit hardware check skipped; QEMU exit 33')

def fragmented_heap():
    if ARCH != 'x86_64':
        return
    cmd = command(32) + ['-append', 'nv.test=1 nv.memory-test=fragmented',
                         '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
                         '-no-reboot', '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04']
    run = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    out = run.stdout.decode('ascii', errors='replace').replace('\r', '')
    (REPORT / 'fragmented-heap.log').write_text(out)
    assert run.returncode == 33 and f'PROBE RESULT: {EXPECTED_ASSERTIONS} passed, 0 failed' in out \
        and 'FAIL ' not in out, out
    record('Fragmented kernel heap / 32 MiB',
           'six reserved-page holes prevent contiguous 8 MiB allocation; scattered heap backing '
           'boots and completes the full user probe, OOM rollback and memory reclamation checks')

def probe_large(memory, timeout=240):
    """Same as probe() but with generous timeouts for multi-GiB TCG machines."""
    argv = command(memory) + ['-append', 'nv.test=1', '-display', 'none', '-serial', 'stdio',
                              '-monitor', 'none', '-no-reboot',
                              '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04']
    finished = subprocess.run(list(argv), shell=False, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=timeout)
    out = finished.stdout.decode('ascii', errors='replace').replace('\r', '')
    (REPORT / f'probe-{memory}MiB.log').write_text(out)
    match = re.search(r'PROBE RESULT: (\d+) passed, 0 failed', out)
    assert finished.returncode == 33 and match and int(match[1]) == EXPECTED_ASSERTIONS \
        and 'FAIL ' not in out, out
    record(f'Ring 3 probe / {memory} MiB',
           f'{match[1]} assertions at {memory} MiB physical RAM; QEMU exit 33')

def hardware_tests():
    models = ['core2duo', 'Nehalem', 'phenom', 'max'] if ARCH == 'x86_64' else ['pentium2,-fxsr', 'pentium3', 'max']
    for cpu in models:
        probe(64, cpu=cpu)
    rejected = ['qemu64,-' + f for f in ['nx','pae','fxsr','sse2','lm','msr','fpu','cmov']] if ARCH == 'x86_64' else ['486', 'pentium', 'qemu32,-fpu']
    for cpu in rejected:
        cmd = command(cpu=cpu) + ['-display','none','-serial','stdio','-monitor','none','-no-reboot']
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            proc.communicate(timeout=2)
            raise AssertionError(f'Unsupported CPU {cpu} should halt, not reset/exit')
        except subprocess.TimeoutExpired:
            proc.kill()
            out = proc.communicate()[0].decode('ascii',errors='replace')
        (REPORT / f'cpu-reject-{cpu}.log').write_text(out)
        assert 'CPU UNSUPPORTED:' in out and f'Nuvora Core {VERSION}' not in out, out
        record('CPU rejection / ' + cpu, 'early serial diagnostic, stable halt before any C kernel code')
    cases = [('default', [], 1), ('none', ['-vga','none'], 0),
             ('multifunction', ['-vga','none','-device','VGA,addr=2.0,multifunction=on',
                               '-device','secondary-vga,addr=2.1'], 2)]
    for label, devices, count in cases:
        vm = VM('gpu-' + label, hardware=devices)
        try:
            vm.send('silicon', 'Online CPUs: 1')
            out = vm.send('prism', f'{count} display adapter(s).')
            assert 'No GPU modesetting or acceleration.' in out and 'NVIDIA' not in out, out
            if count:
                assert 'vendor=0x1234 device=0x1111' in out and 'BAR0:' in out, out
            if count == 2:
                assert 'at 0:2.0' in out and 'at 0:2.1' in out, out
            for _ in range(3):
                assert vm.send('prism') == out
            # Run in two separate child processes: mappings belong to the
            # device snapshot, not the caller's lifetime.
            for _ in range(2):
                result = vm.send('forge probe devctl', 'exited 0')
                assert '0 failed' in result and 'FAIL ' not in result, result
                if count:
                    assert '600 repeated BAR requests reuse the same kernel window' in result, result
                else:
                    assert 'SKIP GPU mapping checks:' in result, result
            assert vm.send('prism') == out
            vm.send('forge vector', 'VECTOR RESULT: PASS')
        finally:
            vm.close()
        record('PCI display / ' + label, f'{count} QEMU VGA adapter(s); two DEVCTL guest runs, bounded cached mappings, unchanged discovery snapshot and FP isolation')
    # This uses the exact production decoder with controlled read-only data.
    # It is not a physical NVIDIA driver test.
    binary = REPORT / 'pci-decoder-test'
    subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fno-builtin','-Iinclude',
                    'tests/pci_decode_test.c','common/pci_decode.c','common/string.c',
                    '-o',str(binary)],cwd=ROOT,check=True)
    run = subprocess.run([str(binary)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    (REPORT / 'pci-decoder-synthetic.log').write_text(run.stdout)
    assert run.returncode == 0 and '25 checks, 0 failures' in run.stdout, run.stdout
    record('PCI decoder / synthetic NVIDIA', run.stdout.strip())
    acpi_binary = REPORT / 'acpi-decoder-test'
    subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fno-builtin','-Iinclude',
                    'tests/acpi_decode_test.c','common/acpi.c','common/string.c',
                    '-o',str(acpi_binary)],cwd=ROOT,check=True)
    run = subprocess.run([str(acpi_binary)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    (REPORT / 'acpi-decoder-synthetic.log').write_text(run.stdout)
    assert run.returncode == 0 and '11 checks, 0 failures' in run.stdout, run.stdout
    record('ACPI parser / synthetic firmware', run.stdout.strip())
    vm = VM('pcie-q35', machine='q35')
    try:
        out = vm.send('firmament', 'PCI config: ECAM, 4096 bytes per function')
        assert 'MCFG: 1 firmware entry(s), 1 active, 0 rejected.' in out and \
               'buses 0-255 at 0x00000000b0000000' in out, out
        vm.send('prism', '1 display adapter(s).')
        probe_out = vm.send('forge probe', f'PROBE RESULT: {EXPECTED_ASSERTIONS} passed', timeout=50)
        assert '0 failed' in probe_out and 'FAIL ' not in probe_out, probe_out
    finally:
        vm.close()
    vm = VM('pcie-q35-fallback', machine='q35', hardware=['-append', 'nv.no-ecam=1'])
    try:
        out = vm.send('firmament', 'PCI config: CF8/CFC legacy access, 256 bytes per function')
        assert 'MCFG: 1 firmware entry(s), 0 active, 0 rejected.' in out and \
               'ECAM was disabled by the nv.no-ecam=1 boot option.' in out, out
        vm.send('prism', '1 display adapter(s).')
    finally:
        vm.close()
    record('PCIe ECAM / Q35',
           'checksum-validated MCFG enabled 4 KiB config pages; full probe passed and nv.no-ecam fallback remained usable')

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
    files = {ROOT / 'Makefile', ROOT / 'start.py', BUILD / 'boot.elf', BUILD / 'nuvora.elf'}
    files.update((BUILD / 'apps').glob('*.elf'))
    for directory in ['kernel', 'arch', 'common', 'include', 'user', 'tests', 'scripts', 'sdk']:
        files.update(p for p in (ROOT / directory).rglob('*')
                     if p.is_file() and p.suffix in {'.c', '.h', '.S', '.ld', '.inc', '.py', '.mjs'})
    if ARCH == 'x86_64':
        files.update([BUILD / 'BOOTX64.EFI', BUILD / 'esp.img'])
    return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(files)}

def new_image(name, size_mib=64):
    path = REPORT / name
    path.unlink(missing_ok=True)
    create(path, size_mib=size_mib)
    return path

def store_slot_lbas(image):
    """Slot header LBAs as declared by the image itself (NVSTORE1 or NVSTORE2)."""
    if image[:8] == b'NVSTORE2':
        return struct.unpack_from('<II', image, 16)
    return (8, 4096)

def restored_file_growth():
    disk = new_image('restored-file-growth.img')
    path = b'/home/growth'
    contents = b'\x5a' * (128 * 1024 - 1)
    payload = struct.pack('<IIII', 1, 2, len(path), len(contents)) + path + contents
    header = bytearray(512)
    struct.pack_into('<8sIIII', header, 0, b'NVSS0001', 1, len(payload), zlib.crc32(payload), 0)
    struct.pack_into('<I', header, 20, zlib.crc32(header))
    with disk.open('r+b') as stream:
        stream.seek(SLOT0_LBA * 512)
        stream.write(header)
        stream.write(payload)
    vm = VM('restored-file-growth', disk, memory=32)
    try:
        assert 'restored /home generation 1' in vm.text(), vm.text()
        out = vm.send('forge probe file-growth', 'PROBE RESULT: 7 passed, 0 failed')
        assert 'FAIL ' not in out, out
    finally:
        vm.close()
    record('Restored file growth / 32 MiB',
           '7 guest assertions: append at 128 KiB, bounded heap use, preserved data, '
           'file limit and complete reclamation')

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
        vm.send('origin', f'Nuvora Core {VERSION}')
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
    slot0, slot1 = store_slot_lbas(original)
    for kind in ['payload-crc', 'invalid-path', 'oversize-header', 'torn-header']:
        data = bytearray(original)
        head = slot1 * 512
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
    names = set('help atlas origin silicon firmament prism horizon ports where step glance nest weave stitch unfold folio mirror shift prune sparks forge scatter gather quench tempo doze anchor trial scrub rest renew'.split())
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
        record('Command catalog and help', 'all 31 commands have purpose, usage and examples; help aliases, unknown commands and literal --help text checked; file and disk generation preserved')
        for app in ['loom', 'folio', 'pulse', 'spin', 'fault', 'probe', 'relay', 'vector']:
            out = vm.send('forge ' + app + ' --help', 'exited 0')
            assert 'Usage: ' in out and 'Example: ' in out, out
            assert 'PROBE RESULT:' not in out and '[fault]' not in out, out
        record('Built-in program help', 'all 8 programs exit 0 for --help; fault/spin/probe/editor bodies are not entered')
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
        vm.wait_for(f'Nuvora Core {VERSION}'.encode(), start)
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
        vm.wait_for(f'Nuvora Core {VERSION}'.encode(), start); vm.wait_for(b' :: ', start)
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
        vm.wait_for(f'Nuvora Core {VERSION}'.encode(), start); vm.wait_for(b' :: ', start)
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


def large_storage():
    """A 4 TiB sparse image exercises the u64 sector count and LBA48 transfers."""
    disk = new_image('persistence-big.img', size_mib=4 * 1024 * 1024)
    vm = VM('big-disk', disk)
    try:
        vm.send('horizon', 'Data disk ready')
        vm.send('nest /home/big')
        vm.send('weave /home/big/story "written to a 4 TiB data image"')
        vm.send('anchor', 'Saved /home.')
    finally:
        vm.close()
    vm = VM('big-disk-restore', disk, reboot=True, monitor=True)
    try:
        assert 'restored /home generation 1' in vm.text()
        vm.send('unfold /home/big/story', '\nwritten to a 4 TiB data image\n')
        vm.send('glance /home', 'big')
    finally:
        vm.close()
    record('Large data image / 4 TiB',
           'NVSTORE2 u64 sector count plus LBA48 addressing; anchor/restore round-trips on a '
           'sparse image whose capacity exceeds 32-bit sector counts; snapshot I/O '
           'remains in the low-address slots (this does not test high-LBA transfers)')

def uefi_boot():
    if ARCH != 'x86_64':
        return
    firmware = find_uefi_firmware()
    esp = BUILD / 'esp.img'
    if not firmware:
        print('SKIP UEFI boot: no OVMF/edk2 firmware found (set NV_OVMF to enable)', flush=True)
        return
    if not esp.is_file():
        print('SKIP UEFI boot: build/ARCH/esp.img missing (needs mtools)', flush=True)
        return
    disk = new_image('uefi-boot.img')
    vm = VM('uefi-boot', disk, uefi=True, boot_timeout=90)
    try:
        vm.send('origin', f'Nuvora Core {VERSION}')
        out = vm.send('horizon', 'Data disk ready')
        assert 'RAM managed' in out, out
        vm.send('weave /home/uefi.txt "saved from a UEFI boot"')
        vm.send('anchor', 'Saved /home.')
    finally:
        vm.close()
    vm = VM('uefi-restore', disk, uefi=True, reboot=True, monitor=True, boot_timeout=90)
    try:
        assert 'restored /home generation 1' in vm.text()
        vm.send('unfold /home/uefi.txt', '\nsaved from a UEFI boot\n')
        vm.send('trial', '0 failed', timeout=90)
    finally:
        vm.close()
    record('UEFI stub boot',
           'OVMF started BOOTX64.EFI; the stub passed the EFI memory map, GOP framebuffer and RSDP through '
           'boot_info, exited boot services and the kernel restored the anchored /home from the data disk')

def iso_boot():
    iso = BUILD / f'nuvora-core-{VERSION}-{ARCH}.iso'
    assert iso.is_file(), 'Build ISO first: make iso'
    vm = VM('bios-iso', iso=iso)
    try:
        vm.send('origin', f'Nuvora Core {VERSION}')
        vm.send('forge pulse quiet', 'exited 7')
    finally:
        vm.close()
    record('GRUB BIOS ISO boot', 'CD-ROM boot reached Ring 3 and executed a child program')
    uefi_iso = BUILD / f'nuvora-core-{VERSION}-{ARCH}-uefi.iso'
    if ARCH == 'x86_64' and find_uefi_firmware() and uefi_iso.is_file():
        vm = VM('uefi-iso', iso=uefi_iso, uefi=True, boot_timeout=90)
        try:
            vm.send('origin', f'Nuvora Core {VERSION}')
            vm.send('forge pulse quiet', 'exited 7')
        finally:
            vm.close()
        record('UEFI ISO boot', 'El Torito UEFI entry started BOOTX64.EFI; kernel reached Ring 3')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase', choices=['all', 'memory', 'storage', 'iso', 'usb', 'help', 'hardware'], default='all')
    parser.add_argument('--iso', action='store_true', help='Also boot the previously built ISO')
    args = parser.parse_args()
    fingerprint = build_fingerprint()
    if args.phase in ['all', 'memory']:
        for memory in [32, 64, 128, 256]:
            probe(memory)
        if ARCH == 'x86_64':
            probe_large(1024, timeout=120)
            probe_large(5120, timeout=480)  # crosses the 4 GiB physical boundary
        fragmented_heap()
        kernel_faults()
        memory_pressure()
    if args.phase in ['all', 'hardware']:
        hardware_tests()
    if args.phase in ['all', 'memory', 'storage']:
        restored_file_growth()
    if args.phase in ['all', 'storage']:
        persistence()
        large_storage()
        folio_editor()
        unrecognized_disk()
    if args.phase in ['all', 'help']:
        command_help_tests()
    if args.phase in ['all', 'usb']:
        usb_tests()
    if args.phase == 'all' and ARCH == 'x86_64':
        uefi_boot()
    if args.iso or args.phase == 'iso':
        iso_boot()
    assert fingerprint == build_fingerprint(), 'Build changed during execution tests'
    if args.phase == 'all' and args.iso:
        iso = BUILD / f'nuvora-core-{VERSION}-{ARCH}.iso'
        fingerprint[iso.relative_to(ROOT).as_posix()] = hashlib.sha256(iso.read_bytes()).hexdigest()
        if ARCH == 'x86_64':
            uefi_iso = BUILD / f'nuvora-core-{VERSION}-{ARCH}-uefi.iso'
            fingerprint[uefi_iso.relative_to(ROOT).as_posix()] = hashlib.sha256(uefi_iso.read_bytes()).hexdigest()
        (REPORT / 'build-fingerprint.json').write_text(json.dumps(fingerprint, indent=2) + '\n')
        execution = {'arch': ARCH, 'version': VERSION, 'phase': 'all', 'iso': True,
                     'completed': True, 'checks': len(results)}
        (REPORT / 'execution.json').write_text(json.dumps(execution, indent=2) + '\n')
    (REPORT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    lines = ['# Verified execution results', '', 'Guest checks ran in QEMU. The ACPI and PCI decoder fixtures ran on the host with explicitly synthetic data.',
             'SSE #XM delivery is explicitly skipped where TCG does not deliver the exception. No physical NVIDIA GPU was tested.', '', '| Check | Result | Evidence |', '| --- | --- | --- |']
    lines += [f'| {r["test"]} | {r["result"]} | {r["detail"]} |' for r in results]
    (REPORT / 'RESULTS.md').write_text('\n'.join(lines) + '\n')
    print(f'ALL {len(results)} HOST CHECKS PASSED. Logs: {REPORT}', flush=True)

if __name__ == '__main__':
    main()
