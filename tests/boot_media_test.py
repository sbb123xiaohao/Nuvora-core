#!/usr/bin/env python3
"""Validate both GPT copies and the unchanged ESP payload in a boot image."""
import pathlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import uuid
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
from mkmedia import create, ESP_START, ESP_TYPE, SECTOR
ROOT = pathlib.Path(__file__).resolve().parents[1]


def profile_payloads(root, payload):
    """Run real CLI entrypoints with distinct, stale opposite-profile inputs.

    Only mtools is replaced here, so this host test needs neither FAT tools
    nor a VM. Real FAT payload/firmware checks live in native_independence_test.
    """
    scripts = root / 'scripts'; scripts.mkdir(parents=True)
    for name in ('qemu.py', 'mkesp.py', 'mkmedia.py', 'mkgptdisk.py', 'mkdisk.py'):
        shutil.copyfile(ROOT / 'scripts' / name, scripts / name)
    profiles = {'linux': root / 'build/x86_64', 'none': root / 'build/x86_64-native'}
    for mode, build in profiles.items():
        build.mkdir(parents=True)
        for name in ('BOOTX64.EFI', 'nuvora-uefi.elf', 'uefi-cmdline.txt'):
            (build / name).write_bytes(f'{mode}:{name}\n'.encode())
        data = bytearray(payload); data[1000:1016] = mode.encode().ljust(16, b' ')
        (build / 'esp.img').write_bytes(data)
    runner = '''import pathlib,sys
from unittest import mock
sys.path.insert(0,sys.argv[1])
import mkesp
def run(command,check):
    assert check
    image=pathlib.Path(command[command.index('-i')+1])
    if command[0]=='mformat':image.write_bytes(b'')
    elif command[0]=='mcopy':
        with image.open('ab') as out:out.write(pathlib.Path(command[-2]).read_bytes())
    else:assert command[0]=='mmd'
sys.argv=['mkesp.py',sys.argv[2]]
with mock.patch.object(mkesp.shutil,'which',return_value='mtools'), mock.patch.object(mkesp.subprocess,'run',side_effect=run):
    mkesp.main()
'''
    selected = root / 'selected-esp.img'
    media = root / 'selected-media.img'
    for mode, build in profiles.items():
        env = dict(os.environ, NV_ARCH='x86_64', NV_COMPAT=mode)
        command = [sys.executable, '-c', runner, str(scripts), str(selected)]
        subprocess.run(command, env=env, check=True, capture_output=True)
        expected = b''.join((build / name).read_bytes() for name in ('BOOTX64.EFI', 'nuvora-uefi.elf', 'uefi-cmdline.txt'))
        assert selected.read_bytes() == expected, f'{mode}: ESP used another profile'
        subprocess.run([sys.executable, str(scripts / 'mkmedia.py'), str(media)], env=env, check=True, capture_output=True)
        data = (build / 'esp.img').read_bytes()
        assert media.read_bytes()[ESP_START * SECTOR:ESP_START * SECTOR + len(data)] == data, f'{mode}: media used another ESP'
        subprocess.run([sys.executable, str(scripts / 'mkmedia.py')], env=env, check=True, capture_output=True)
        assert (build / 'nuvora-uefi-media.img').is_file(), f'{mode}: default output used another profile'
        # Missing selected artifacts must fail, even with the other build present.
        kernel = build / 'nuvora-uefi.elf'; saved = kernel.read_bytes(); kernel.unlink()
        before = selected.read_bytes()
        assert subprocess.run(command, env=env, capture_output=True).returncode != 0
        assert selected.read_bytes() == before
        kernel.write_bytes(saved)
        esp = build / 'esp.img'; esp.unlink(); before = media.read_bytes()
        assert subprocess.run([sys.executable, str(scripts / 'mkmedia.py'), str(media)], env=env, capture_output=True).returncode != 0
        assert media.read_bytes() == before
        esp.write_bytes(data)
    print('PASS boot profiles: selected kernel/loader/cmdline/ESP, stale opposite inputs and missing-input preservation')


def check_header(image, at, other, entries_at):
    header = image[at * SECTOR:(at + 1) * SECTOR]
    assert header[:8] == b'EFI PART'
    assert struct.unpack_from('<I', header, 12)[0] == 92
    saved = struct.unpack_from('<I', header, 16)[0]
    zeroed = bytearray(header[:92])
    zeroed[16:20] = b'\0' * 4
    assert zlib.crc32(zeroed) == saved
    assert struct.unpack_from('<QQ', header, 24) == (at, other)
    assert struct.unpack_from('<Q', header, 72)[0] == entries_at
    assert struct.unpack_from('<II', header, 80) == (128, 128)
    entries = image[entries_at * SECTOR:(entries_at + 32) * SECTOR]
    assert zlib.crc32(entries) == struct.unpack_from('<I', header, 88)[0]
    assert entries[:16] == ESP_TYPE.bytes_le
    return entries


def main():
    with tempfile.TemporaryDirectory() as directory:
        esp = pathlib.Path(directory) / 'esp.img'
        image = pathlib.Path(directory) / 'media.img'
        payload = bytearray(1024 * 1024)
        payload[510:512] = b'\x55\xaa'
        struct.pack_into('<H', payload, 11, SECTOR)
        payload[13] = 1
        struct.pack_into('<H', payload, 14, 1)
        payload[16] = 2
        struct.pack_into('<H', payload, 19, len(payload) // SECTOR)
        payload[1000:1007] = b'BOOTX64'
        esp.write_bytes(payload)
        create(esp, image)
        raw = image.read_bytes()
        total = len(raw) // SECTOR
        assert raw[510:512] == b'\x55\xaa' and raw[450] == 0xee
        assert struct.unpack_from('<II', raw, 454) == (1, total - 1)
        primary = check_header(raw, 1, total - 1, 2)
        backup = check_header(raw, total - 1, 1, total - 33)
        assert primary == backup
        assert uuid.UUID(bytes_le=primary[:16]) == ESP_TYPE
        assert struct.unpack_from('<QQ', primary, 32) == (
            ESP_START, ESP_START + len(payload) // SECTOR - 1)
        assert raw[ESP_START * SECTOR:ESP_START * SECTOR + len(payload)] == payload
        assert esp.read_bytes() == payload
        try:
            create(esp, esp)
        except ValueError:
            pass
        else:
            raise AssertionError('must refuse to overwrite the source ESP')
        profile_payloads(pathlib.Path(directory) / 'profiles', payload)
    print('PASS UEFI media: protective MBR, primary/backup GPT CRC, ESP bytes, input isolation')


if __name__ == '__main__':
    main()
