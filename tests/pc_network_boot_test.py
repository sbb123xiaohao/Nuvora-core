#!/usr/bin/env python3
"""Exercise Q35/OVMF NICs, reconnect them, and verify a saved HTTP download.

Only temporary data images and firmware variable copies are modified.
QEMU's user network reaches a local HTTP server through 10.0.2.2.
"""
import argparse
import hashlib
import http.server
import json
import pathlib
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts'))
import qemu
import test as suite
from extent_store import BLOCK, layout, roots
from mkdisk import create

PAYLOAD = bytes(range(256)) * 1024
NICS = ('e1000', 'e1000e', 'usb-net')


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Length', str(len(PAYLOAD)))
        self.end_headers()
        self.wfile.write(PAYLOAD)

    def log_message(self, *args):
        pass


def saved_download(disk):
    with disk.open('rb') as stream:
        geometry = layout(stream, 0, disk.stat().st_size // 512)
        records = roots(stream, 0, geometry)
        assert records, 'No committed NVSTORE3 root'
        for kind, path, size, extents in records[-1][2]:
            if path != b'/home/blob':
                continue
            assert kind == 2 and size == len(PAYLOAD), (kind, size)
            result = bytearray(size)
            for logical, physical, blocks in extents:
                offset = logical * BLOCK
                count = min(blocks * BLOCK, size - offset)
                if count > 0:
                    stream.seek(physical * BLOCK)
                    data = stream.read(count)
                    assert len(data) == count, 'Truncated saved extent'
                    result[offset:offset + count] = data
            return bytes(result)
    raise AssertionError('Downloaded file absent from committed root')


def configure(vm):
    deadline = time.monotonic() + 15
    while True:
        out = vm.send('net')
        if 'link ready' in out or 'IPv4 configured' in out:
            break
        assert time.monotonic() < deadline, out
        vm.pump(0.2)
    vm.send('net dhcp', 'DHCP started')
    deadline = time.monotonic() + 15
    while True:
        out = vm.send('net')
        if 'IPv4 configured' in out:
            assert '10.0.2.15' in out, out
            break
        assert time.monotonic() < deadline, out
        vm.pump(0.2)
    vm.send('ping 10.0.2.2 3', '3 received', timeout=20)


def run(model, directory, port):
    disk = directory / f'{model}.img'
    create(disk, size_mib=64)
    hardware = ['-netdev', 'user,id=n0', '-device', 'qemu-xhci,id=xhci',
                '-device', 'usb-kbd,id=kbd,bus=xhci.0,port=1',
                '-device', 'usb-tablet,id=mouse,bus=xhci.0,port=2']
    device = 'usb-net,id=nic,netdev=n0,bus=xhci.0,port=3'
    hardware += ['-device', device if model == 'usb-net' else f'{model},id=nic,netdev=n0']
    original_firmware = suite.firmware_arguments

    def private_firmware(firmware):
        original_build = qemu.BUILD
        qemu.BUILD = directory / model
        try:
            return qemu.firmware_arguments(firmware)
        finally:
            qemu.BUILD = original_build

    suite.firmware_arguments = private_firmware
    try:
        vm = suite.VM(f'q35-uefi-{model}', disk, memory=256, machine='q35',
                      uefi=True, hardware=hardware, monitor=True, boot_timeout=90)
        try:
            configure(vm)
            vm.send(f'wget -O /home/blob http://10.0.2.2:{port}/blob',
                    f'Downloaded {len(PAYLOAD)} bytes to /home/blob', timeout=90)
            vm.send('anchor', 'Saved /home.')
            if model == 'usb-net':
                vm.monitor('human-monitor-command', {'command-line': 'device_del nic'})
                vm.send('ports --scan')
            else:
                vm.monitor('human-monitor-command', {'command-line': 'set_link nic off'})
            out = vm.send('net', 'link down')
            assert 'IP 0.0.0.0' in out and 'gateway 0.0.0.0' in out, out
            if model == 'usb-net':
                vm.monitor('human-monitor-command', {'command-line': f'device_add {device}'})
                vm.send('ports --scan')
            else:
                vm.monitor('human-monitor-command', {'command-line': 'set_link nic on'})
            configure(vm)
        finally:
            vm.close()
        assert saved_download(disk) == PAYLOAD, f'{model}: saved HTTP bytes differ'
    finally:
        suite.firmware_arguments = original_firmware
    digest = hashlib.sha256(PAYLOAD).hexdigest()
    print(f'PASS {model}: DHCP, ping, reconnect, saved {len(PAYLOAD)} bytes SHA256 {digest}', flush=True)
    return {'nic': model, 'result': 'PASS', 'bytes': len(PAYLOAD), 'sha256': digest}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nic', choices=NICS, action='append', help='Run selected NICs; default: all three')
    args = parser.parse_args()
    subprocess.run(['make', '-s', 'diagnostics', 'esp'], cwd=ROOT, check=True)
    if not qemu.find_uefi_firmware():
        raise SystemExit('Install OVMF or set NV_OVMF')
    suite.REPORT = qemu.BUILD / 'pc-network-results'
    suite.REPORT.mkdir(parents=True, exist_ok=True)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='nuvora-pc-network-') as directory:
            results = [run(model, pathlib.Path(directory), server.server_port)
                       for model in args.nic or NICS]
        (suite.REPORT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == '__main__':
    main()
