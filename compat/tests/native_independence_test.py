#!/usr/bin/env python3
"""Verify the native profile contains no Linux backend and boots independently."""
import os
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import hashlib
ROOT=pathlib.Path(__file__).resolve().parents[2]
os.environ['NV_COMPAT']='none'
sys.path.insert(0,str(ROOT/'scripts'))
import test


def verify_esp(build):
    # Verify the production payload before VM's diagnostic override can hide a
    # packager that took its kernel from a different build profile.
    payloads = {'EFI/BOOT/BOOTX64.EFI': 'BOOTX64.EFI', 'EFI/NUVORA/NUVORA.ELF': 'nuvora-uefi.elf'}
    if (build / 'uefi-cmdline.txt').is_file():
        payloads['EFI/NUVORA/CMDLINE'] = 'uefi-cmdline.txt'
    with tempfile.TemporaryDirectory(prefix='nuvora-native-esp-') as directory:
        for path, source in payloads.items():
            target = pathlib.Path(directory) / source
            subprocess.run(['mcopy', '-i', str(build / 'esp.img'), '::/' + path, str(target)], check=True)
            actual = hashlib.sha256(target.read_bytes()).hexdigest()
            expected = hashlib.sha256((build / source).read_bytes()).hexdigest()
            assert actual == expected, f'Native ESP {path}: expected {expected}, actual {actual}'
    print('PASS native production ESP: kernel, EFI loader and optional command line match the selected native build', flush=True)


def main():
    build=ROOT/'build/x86_64-native'
    kernel=build/'nuvora.elf'
    if not kernel.is_file():raise SystemExit('Build first: make COMPAT=none -j4 diagnostics esp')
    symbols=subprocess.check_output(['nm','--defined-only',str(kernel)],text=True)
    assert not re.search(r'\b(?:linux_|task_linux_)\w*$',symbols,re.M), 'Linux implementation linked into native profile'
    assert not (build/'compat/linux').exists(), 'Native profile unexpectedly compiled Linux objects'
    assert not subprocess.check_output(['nm','-u',str(kernel)]).strip()
    verify_esp(build)
    archive=(build/'init.nvar').read_bytes();assert archive[:8]==b'NVAR0001'
    count,=struct.unpack_from('<I',archive,8);at=12;apps=[]
    for _ in range(count):
        name,size=struct.unpack_from('<II',archive,at);at+=8
        apps.append(archive[at:at+name].decode('ascii'));at+=name+size
    assert at==len(archive) and 'compat' not in apps
    assert {'desktop','terminal','folio','media','probe','java'}<=set(apps)
    print('PASS native independence: no Linux backend objects/symbols, no external runtime, no compat application; native apps retained',flush=True)
    test.probe(32)
    test.probe(256)
    test.fragmented_heap()
    test.uefi_boot()
    vm=test.VM('native-compat-disabled',memory=64)
    try:
        out=vm.send('compat --help')
        assert 'Compatibility module is disabled' in out
        vm.send('weave /home/native-only.txt independent')
        assert 'independent' in vm.send('unfold /home/native-only.txt')
        vm.send('trial','0 failed',timeout=90)
    finally:vm.close()
    print('PASS native-only BIOS/UEFI, 153-assertion probes, filesystem and shell with compatibility disabled',flush=True)


if __name__=='__main__':main()
