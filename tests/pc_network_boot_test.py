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
import re
import struct
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
BROADCASTS = [('255.255.255.255', b'nuvora-limited-broadcast'),
              ('10.0.2.255', b'nuvora-subnet-broadcast')]


def ethernet_frames(path):
    data = path.read_bytes()
    assert len(data) >= 24, 'Truncated network capture'
    magic = data[:4]
    assert magic in (b'\xd4\xc3\xb2\xa1', b'\xa1\xb2\xc3\xd4',
                     b'\x4d\x3c\xb2\xa1', b'\xa1\xb2\x3c\x4d'), 'Unknown PCAP header'
    endian = '<' if magic in (b'\xd4\xc3\xb2\xa1', b'\x4d\x3c\xb2\xa1') else '>'
    version = struct.unpack_from(endian + 'HH', data, 4)
    snaplen, link_type = struct.unpack_from(endian + 'II', data, 16)
    assert version == (2, 4) and snaplen >= 1514 and link_type == 1
    offset = 24
    while offset < len(data):
        assert offset + 16 <= len(data), 'Truncated PCAP record'
        captured, original = struct.unpack_from(endian + 'II', data, offset + 8)
        offset += 16
        assert captured == original and captured <= snaplen and offset + captured <= len(data)
        yield data[offset:offset + captured]
        offset += captured


def check_broadcasts(path):
    observed = set()
    for frame in ethernet_frames(path):
        if len(frame) < 42 or frame[12:14] != b'\x08\x00':
            continue
        ip = frame[14:]
        header = (ip[0] & 15) * 4
        total = struct.unpack_from('!H', ip, 2)[0]
        if ip[0] >> 4 != 4 or header < 20 or ip[9] != 17 or total < header + 8:
            continue
        assert len(ip) >= total, 'Truncated IPv4 capture'
        source_port, dest_port, length = struct.unpack_from('!HHH', ip, header)
        payload = ip[header + 8:header + length]
        for address, expected in BROADCASTS:
            if dest_port != 40555 or payload != expected:
                continue
            destination = bytes(int(part) for part in address.split('.'))
            assert frame[:6] == b'\xff' * 6 and ip[16:20] == destination
            assert ip[12:16] == b'\x0a\x00\x02\x0f' and source_port == 40000
            assert length == 8 + len(expected) and total == header + length
            observed.add(address)
    assert observed == {address for address, _ in BROADCASTS}, ('Missing broadcast frames', observed)


def send_broadcasts(vm):
    output = vm.send('net')
    active = re.search(r'^\* (\d+) ', output, re.M)
    assert active, output
    index = active[1]
    vm.send(f'net static {index} 10.0.2.15 255.255.255.0 0.0.0.0')
    output = vm.send('net', 'IPv4 configured')
    assert 'IP 10.0.2.15  gateway 0.0.0.0' in output, output
    for address, payload in BROADCASTS:
        vm.send(f'net send {address} 40555 {payload.decode("ascii")}')


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
    capture = directory / f'{model}.pcap'
    capture_spec = str(capture).replace(',', ',,')
    hardware = ['-netdev', 'user,id=n0',
                '-object', f'filter-dump,id=capture,netdev=n0,file={capture_spec}',
                '-device', 'qemu-xhci,id=xhci',
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
            send_broadcasts(vm)
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
        check_broadcasts(capture)
    finally:
        suite.firmware_arguments = original_firmware
    digest = hashlib.sha256(PAYLOAD).hexdigest()
    print(f'PASS {model}: captured two UDP broadcasts without a gateway; DHCP, ping, reconnect, '
          f'saved {len(PAYLOAD)} bytes SHA256 {digest}', flush=True)
    return {'nic': model, 'result': 'PASS', 'broadcasts': len(BROADCASTS),
            'bytes': len(PAYLOAD), 'sha256': digest}


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
