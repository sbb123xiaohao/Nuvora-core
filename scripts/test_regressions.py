#!/usr/bin/env python3
"""Run host fixtures against the actual Nuvora loader, ATA and snapshot code."""
import os
import pathlib
import subprocess
import tempfile
from qemu import ROOT, BUILD
from mkdisk import create as create_raw_disk
from mkgptdisk import create as create_gpt_disk

def main():
    with tempfile.TemporaryDirectory(prefix='nuvora-regression-') as directory:
        disk_image = pathlib.Path(directory) / 'two-volumes.img'
        create_gpt_disk(disk_image, size_mib=128, partitions=2, legacy=True)
        extent_image = pathlib.Path(directory) / 'extent-volumes.img'
        create_gpt_disk(extent_image, size_mib=128, partitions=2)
        raw_image = pathlib.Path(directory) / 'raw-extent.img'
        create_raw_disk(raw_image, size_mib=64)
        for name in ['crypto', 'account', 'account_ui', 'address', 'native64', 'power', 'buddy', 'memory', 'heap_map', 'fs_memory', 'disk', 'gpt_disk', 'nvme', 'ahci', 'ahci_multiport', 'pci_ecam', 'usb_scratch', 'rndis', 'store', 'extent', 'uefi', 'network', 'net_tools', 'network_e1000', 'e1000', 'igc', 'audio', 'window', 'display', 'desktop_shell', 'keyboard', 'pointer', 'media', 'media_formats']:
            output = pathlib.Path(directory) / name
            cmd = [os.environ.get('CC', 'gcc'), '-std=c11', '-O1', '-g',
                   '-Wall', '-Wextra', '-Werror', '-fshort-wchar', '-fno-builtin',
                   '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                   '-DNV_HOST_TEST', '-fsanitize=undefined', '-fno-sanitize-recover=all', '-Iinclude',
                   str(ROOT / 'tests' / f'{name}_test.c'), 'tests/window_stubs.c', 'common/string.c',
                   'common/page_buddy.c', 'common/slab.c', 'common/crypto.c', '-o', str(output)]
            if name == 'net_tools':
                cmd.insert(cmd.index('-Iinclude'), '-Wno-unused-function')
            subprocess.run(cmd, cwd=ROOT, check=True)
            args = [str(output)] + ([str(ROOT / 'build/x86_64/nuvora-uefi.elf')] if name == 'uefi' else [])
            if name in ('gpt_disk', 'nvme'):
                args += [str(disk_image), '128']
            if name == 'media':
                args += [str(ROOT / 'tests/fixtures/tone.mp3'),
                         str(ROOT / 'tests/fixtures/clip.mpg')]
            if name == 'media_formats':
                args += [str(ROOT / 'tests/fixtures/tone.flac'), str(ROOT / 'tests/fixtures/hd.mpg')]
            subprocess.run(args, check=True)
            if name == 'network':
                for scenario in ('usb-mac', 'tcp-reset', 'tcp-window-ack', 'tcp-future-ack',
                                 'tcp-zero-window', 'tcp-eof', 'tcp-wrap', 'tcp-ack-backpressure'):
                    subprocess.run([str(output), scenario], check=True)
            if name == 'igc':
                for scenario in ('zero', 'multicast', 'broadcast', 'unprogrammed'):
                    subprocess.run([str(output), scenario], check=True)
            if name in ('gpt_disk', 'nvme'):
                subprocess.run([str(output), str(extent_image), '128', 'extent'], check=True)
            if name == 'nvme':
                for scenario in ('multi', 'controller', 'sparse', 'legacy-list', 'admin-ownership'):
                    subprocess.run([str(output), str(extent_image), '128', scenario], check=True)
            if name == 'gpt_disk':
                subprocess.run([str(output), str(raw_image), '64', 'raw'], check=True)
        subprocess.run(['python3', 'scripts/test_media_import.py'], cwd=ROOT, check=True)
        subprocess.run(['python3', 'scripts/test_extent_import.py', str(pathlib.Path(directory) / 'extent')], cwd=ROOT, check=True)
        subprocess.run(['python3', 'tests/qemu_options_test.py'], cwd=ROOT, check=True)
        subprocess.run(['python3', 'tests/boot_media_test.py'], cwd=ROOT, check=True)
        subprocess.run(['python3', 'tests/package_test.py'], cwd=ROOT, check=True)
    print('ALL 39 HOST REGRESSION GROUPS PASSED (including desktop shell, keyboard modifiers, native windows, storage, network, media and source packaging; C fixtures use UBSan)')

if __name__ == '__main__':
    main()
