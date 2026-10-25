#!/usr/bin/env python3
"""A same-titled client must not replace Terminal's registered launch target."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
import time
from desktop_boot_test import first_account, color_count, qemu
from launcher_boot_test import guest
from session_boot_test import wait_saved_file, saved_file
from capture_screenshots import write_png
from mkgptdisk import create
from import_media import import_files


def fixture(output):
    source=qemu.ROOT/'tests/fixtures/window_title_client.c'
    obj=output/'window-title.o';binary=output/'window-title.elf'
    subprocess.run(['gcc','-m64','-mno-red-zone','-mgeneral-regs-only','-ffreestanding','-fno-builtin',
        '-fno-pie','-fno-pic','-fno-stack-protector','-fno-asynchronous-unwind-tables','-mno-sse','-mno-sse2',
        '-mno-mmx','-msoft-float','-std=c11','-O2','-g1','-Wall','-Wextra','-Werror',
        '-I'+str(qemu.ROOT/'include'),'-I'+str(qemu.ROOT/'user'),'-ffunction-sections','-fdata-sections',
        '-mcmodel=large','-c',str(source),'-o',str(obj)],check=True)
    common=[qemu.BUILD/'user'/name for name in ('runtime.o','start64.o','string.o','wide64.o')]
    subprocess.run(['ld','-m','elf_x86_64','--gc-sections','-z','max-page-size=4096',
        '-T',str(qemu.ROOT/'user/linker64.ld'),'-o',str(binary),str(obj),*map(str,common)],check=True)
    return binary


def run(output):
    output.mkdir(parents=True,exist_ok=True);binary=fixture(output)
    with tempfile.TemporaryDirectory(prefix='nuvora-window-title-') as td:
        private=pathlib.Path(td);disk,esp=private/'data.img',private/'esp.img'
        create(disk,size_mib=128);import_files(disk,[binary]);shutil.copyfile(qemu.BUILD/'esp.img',esp)
        original=qemu.BUILD
        try:
            qemu.BUILD=private
            command=qemu.command(256,disk,machine='q35',kernel=False,esp=esp,disk_bus='nvme',resolution='1280x800')
            command+=qemu.firmware_arguments(qemu.find_uefi_firmware())
        finally:qemu.BUILD=original
        command+=['-display','none','-qmp','stdio','-no-reboot','-device','qemu-xhci,id=xhci',
                  '-device','usb-kbd,bus=xhci.0','-device','usb-tablet,bus=xhci.0']
        with guest(command,output/'serial.log',output) as m:
            first_account(m,output);m.start(2)
            m.type('scatter /home/window-title.elf Terminal\n');m.key('alt','f4')
            wait_saved_file(m,disk,'/home/users/tester/title-client.txt',b'READY\n',timeout=30)
            # The registered Terminal has closed; only the unrelated titled
            # client remains. Launching Terminal must create the real shell.
            width,height,pixels=m.shot(output,'unregistered-title')
            # Both text clients use the terminal background; the fixture has
            # only 480x270 pixels while the real Terminal requests 960x650.
            assert color_count(pixels,0x15222b)<150000,'Original Terminal did not close'
            m.start(2);width,height,pixels=m.shot(output,'terminal-launched')
            real_pixels=color_count(pixels,0x15222b)
            print('Registered Terminal background pixels:',real_pixels,flush=True)
            m.type('weave identity-ok.txt genuine-shell\nanchor\n')
            m.call('stop')
            try:received=saved_file(disk,'/home/users/tester/title-client.txt')
            finally:m.call('cont')
            result={'real_terminal_pixels':real_pixels,'unregistered_client_received':received.decode('ascii',errors='replace'),
                    'fixture_sha256':hashlib.sha256(binary.read_bytes()).hexdigest()}
            (output/'results.json').write_text(json.dumps(result,indent=2)+'\n')
            write_png(output/'terminal-launched.png',width,height,pixels)
            print('Unregistered titled client received:',repr(received),flush=True)
            assert real_pixels>200000,'Same-titled client occupied the Terminal launcher slot'
            assert received==b'READY\n','Terminal command input went to the unrelated client'
            wait_saved_file(m,disk,'/home/users/tester/identity-ok.txt',b'genuine-shell\n')
            print('PASS title isolation: real Terminal executed and persisted the command; unrelated client received no keys',flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=pathlib.Path,default=qemu.BUILD/'window-identity')
    args=parser.parse_args();run(args.output.resolve())


if __name__=='__main__':main()
