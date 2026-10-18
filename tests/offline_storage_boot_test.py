#!/usr/bin/env python3
"""Boot repaired legacy roots, migrated data and an isolated second volume.

Run after `make diagnostics esp`. NV_COMPAT=none selects the native build.
All images are temporary; serial logs remain under the selected build.
"""
import hashlib
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from test import VM
from import_media import partition, geometry, pack
from mkgptdisk import create
from offline_storage_test import seed


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').digest()


def import_cli(image, source):
    subprocess.run([sys.executable, str(ROOT / 'scripts/import_media.py'),
                    '--disk', str(image), str(source)], check=True)


def roundtrip(image, label, uefi=False, second=False):
    for reboot in (False, True):
        name = 'offline-' + label + ('-reboot' if reboot else '-write')
        vm = VM(name, image, memory=64, uefi=uefi, boot_timeout=40)
        try:
            vm.send('unfold C:/added.txt', 'new file')
            if second:
                vm.send('unfold D:/sentinel.txt', 'D-volume-untouched')
            else:
                vm.send('unfold C:/keep.txt', 'recover me')
                vm.send('unfold C:/bad.txt', 'path not found')
            if reboot:
                vm.send('unfold C:/guest.txt', label)
            else:
                vm.send('weave C:/guest.txt ' + label)
                vm.send('anchor', 'Saved /home.')
            assert 'restore failed' not in vm.text().lower(), vm.text()
        finally:
            vm.close()
    print('PASS', label, 'guest read/write/save/reboot', flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix='nuvora-offline-guest-') as temp:
        directory = pathlib.Path(temp)
        added = directory / 'added.txt'; added.write_bytes(b'new file')
        legacy = directory / 'legacy.img'
        create(legacy, size_mib=64, legacy=True)
        with legacy.open('r+b') as stream:
            first, sectors = partition(stream)
            s0, s1, cap = geometry(stream, first, sectors)
            seed(stream, first, s0, 0xffffffff, pack([(2, b'/home/keep.txt', b'recover me')]))
            seed(stream, first, s1, 0, pack([(2, b'/home/bad.txt', b'x'),
                                           (2, b'/home/bad.txt', b'y')]))
            stream.flush(); stream.seek((first + s0) * 512)
            recovery = stream.read(cap + 512)
        import_cli(legacy, added)
        with legacy.open('rb') as stream:
            stream.seek((first + s0) * 512)
            assert stream.read(cap + 512) == recovery
        migrated = directory / 'migrated.img'
        before = digest(legacy)
        subprocess.run([sys.executable, str(ROOT / 'scripts/migrate_store.py'),
                        str(legacy), str(migrated), '--size', '64'], check=True)
        assert digest(legacy) == before
        print('PASS import preserves recovery slot; migration preserves whole source', flush=True)
        roundtrip(legacy, 'legacy-bios')
        roundtrip(migrated, 'migrated-uefi', uefi=True)

        dual = directory / 'dual.img'
        create(dual, size_mib=128, partitions=2)
        vm = VM('offline-dual-seed', dual, memory=64)
        try:
            vm.send('volumes', 'D:/')
            vm.send('weave D:/sentinel.txt D-volume-untouched')
            vm.send('anchor', 'Other mounted drives saved.')
        finally:
            vm.close()
        with dual.open('rb') as stream:
            stream.seek(2 * 512 + 128 + 32)
            start, end = struct.unpack('<QQ', stream.read(16))
            stream.seek(start * 512)
            d_before = hashlib.sha256(stream.read((end - start + 1) * 512)).digest()
        import_cli(dual, added)
        with dual.open('rb') as stream:
            stream.seek(start * 512)
            assert hashlib.sha256(stream.read((end - start + 1) * 512)).digest() == d_before
        print('PASS C: import leaves every byte of D: unchanged', flush=True)
        roundtrip(dual, 'dual-bios', second=True)
    print('PASS all offline storage guest checks', flush=True)


if __name__ == '__main__':
    main()
