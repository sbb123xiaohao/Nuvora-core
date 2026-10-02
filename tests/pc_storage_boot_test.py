#!/usr/bin/env python3
"""Exercise SATA and PCIe storage topologies in real q35/OVMF guests.

These are simulated PC hardware checks, not physical hardware certification.
Every data disk, foreign disk, ESP copy and variable store is private to a run.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import qemu
import test as guest
from mkdisk import create

CASES = ('ahci-port0', 'ahci-port5', 'ahci-controller2', 'nvme-single',
         'nvme-ns2', 'nvme-controller2', 'pcie-nvme')


def hardware(case, data, foreign):
    data_path = str(data).replace(',', ',,')
    foreign_path = str(foreign).replace(',', ',,')
    drives = ['-drive', f'file={data_path},format=raw,if=none,id=data',
              '-drive', f'file={foreign_path},format=raw,if=none,id=foreign']
    devices = {
        'ahci-port0': ['ide-hd,drive=data,bus=ide.0',
                       'ide-hd,drive=foreign,bus=ide.5'],
        'ahci-port5': ['ide-hd,drive=foreign,bus=ide.0',
                       'ide-hd,drive=data,bus=ide.5'],
        'ahci-controller2': ['ide-hd,drive=foreign,bus=ide.0',
                             'ich9-ahci,id=sata2', 'ide-hd,drive=data,bus=sata2.0'],
        'nvme-single': ['ide-hd,drive=foreign,bus=ide.0',
                        'nvme,drive=data,serial=nuvora-data'],
        'nvme-ns2': ['nvme,id=nvme,serial=multi',
                     'nvme-ns,drive=foreign,bus=nvme,nsid=1',
                     'nvme-ns,drive=data,bus=nvme,nsid=2'],
        'nvme-controller2': ['nvme,drive=foreign,serial=foreign',
                             'nvme,drive=data,serial=nuvora-data'],
        'pcie-nvme': ['ide-hd,drive=foreign,bus=ide.0',
                      'pcie-root-port,id=rp1,chassis=1',
                      'nvme,drive=data,serial=nuvora-data,bus=rp1'],
    }
    return drives + [arg for device in devices[case] for arg in ('-device', device)]


def exercise(case, private, output):
    directory = private / case
    directory.mkdir()
    data, foreign = directory / 'data.img', directory / 'foreign.img'
    create(data, size_mib=64)
    foreign.write_bytes(b'FOREIGN_DISK'.ljust(8 * 1024 * 1024, b'\0'))
    before = hashlib.sha256(foreign.read_bytes()).digest()
    original_command = guest.command
    original_firmware = guest.firmware_arguments
    devices = hardware(case, data, foreign)

    def command(*args, **kwargs):
        return original_command(*args, **kwargs) + devices

    for phase in ('save', 'restore'):
        variables = directory / phase
        variables.mkdir()

        def firmware_arguments(firmware):
            with mock.patch.object(qemu, 'BUILD', variables):
                return original_firmware(firmware)

        with mock.patch.object(guest, 'command', command), \
             mock.patch.object(guest, 'firmware_arguments', firmware_arguments), \
             mock.patch.object(guest, 'REPORT', output):
            vm = guest.VM(f'{case}-{phase}', machine='q35', memory=512,
                          uefi=True, boot_timeout=90)
            try:
                vm.send('horizon', 'Data disk ready', timeout=30)
                if phase == 'save':
                    vm.send(f'weave /home/hardware "{case} persistent bytes"')
                    vm.send('anchor', 'Saved /home.', timeout=30)
                else:
                    assert 'restored /home generation 1' in vm.text(), vm.text()
                    vm.send('unfold /home/hardware', f'\n{case} persistent bytes\n')
                    vm.send('trial',
                            f'PROBE RESULT: {guest.EXPECTED_ASSERTIONS} passed, 0 failed',
                            timeout=120)
                    vm.send('horizon', 'Data disk ready')
            finally:
                vm.close()
    assert hashlib.sha256(foreign.read_bytes()).digest() == before, \
        f'{case}: foreign disk was modified'
    print(f'PASS {case}: save, reboot, restore, {guest.EXPECTED_ASSERTIONS} '
          'Ring 3 assertions, shell return and unchanged foreign disk', flush=True)
    return {'case': case, 'result': 'PASS', 'guest_assertions': guest.EXPECTED_ASSERTIONS,
            'foreign_sha256': before.hex()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=CASES, action='append',
                        help='Run selected cases; default is all seven')
    parser.add_argument('--output', type=pathlib.Path, default=qemu.BUILD / 'pc-storage')
    args = parser.parse_args()
    for tool in ('mcopy', 'mmd', 'mformat'):
        if not shutil.which(tool):
            raise SystemExit('Install mtools to create private UEFI boot images')
    if not qemu.qemu_binary() or not qemu.find_uefi_firmware():
        raise SystemExit('Install QEMU and OVMF, or set NV_QEMU/NV_OVMF')
    subprocess.run(['make', '-s', 'diagnostics', 'esp'], cwd=ROOT, check=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = output / 'results.json'
    report.write_text(json.dumps({'completed': False, 'cases': []}, indent=2) + '\n')
    results = []
    with tempfile.TemporaryDirectory(prefix='nuvora-pc-storage-') as directory:
        for case in dict.fromkeys(args.case or CASES):
            results.append(exercise(case, pathlib.Path(directory), output))
            report.write_text(json.dumps({'completed': False, 'cases': results}, indent=2) + '\n')
    with mock.patch.object(guest, 'REPORT', output):
        guest.probe(256, timeout=120)
    print('PASS initial probe retains emulator exit status 33', flush=True)
    report.write_text(json.dumps({'completed': True, 'initial_probe_exit': 33,
                                  'cases': results}, indent=2) + '\n')


if __name__ == '__main__':
    main()
