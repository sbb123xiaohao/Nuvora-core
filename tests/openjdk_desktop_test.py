#!/usr/bin/env python3
"""Run OpenJDK in an authenticated production UEFI Terminal and capture GOP."""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import tempfile
import time
from desktop_boot_test import Monitor, first_account, qemu
from session_boot_test import wait_saved_file, saved_file
from capture_screenshots import write_png
from mkmedia import create as create_media
from java_desktop_test import type_command


def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disk',type=pathlib.Path,default=qemu.ROOT/'build/openjdk-probe/data.img')
    options=parser.parse_args()
    if not options.disk.is_file():parser.error('Prepare a fresh OpenJDK probe disk first')
    output=qemu.BUILD/'openjdk-desktop';output.mkdir(parents=True,exist_ok=True)
    firmware=qemu.find_uefi_firmware()
    if not firmware:raise SystemExit('OVMF required')
    serial=output/'serial.log'
    with tempfile.TemporaryDirectory(prefix='nuvora-openjdk-desktop-') as directory:
        private=pathlib.Path(directory);disk=private/'data.img';esp=private/'esp.img';media=private/'boot.img'
        subprocess.run(['cp','--reflink=auto','--sparse=always',str(options.disk),str(disk)],check=True)
        shutil.copyfile(qemu.BUILD/'esp.img',esp);create_media(esp,media)
        original=qemu.BUILD
        try:
            qemu.BUILD=private
            command=qemu.command(memory=512,disk=disk,machine='q35',kernel=False,disk_bus='nvme')+qemu.firmware_arguments(firmware)
        finally:qemu.BUILD=original
        command+=['-display','none','-qmp','stdio','-serial',f'file:{serial}',
                  '-device','qemu-xhci,id=xhci','-device','usb-kbd,bus=xhci.0','-device','usb-tablet,bus=xhci.0',
                  '-drive',f'file={media},format=raw,if=none,id=boot_media',
                  '-device','usb-storage,bus=xhci.0,drive=boot_media,bootindex=1']
        with (output/'qemu.log').open('w') as log:
            process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log)
            try:
                monitor=Monitor(process);deadline=time.monotonic()+60
                while 'Ring 3: /apps/session' not in serial.read_text(errors='replace'):
                    if process.poll() is not None or time.monotonic()>deadline:raise AssertionError('Production desktop did not boot')
                    time.sleep(.2)
                first_account(monitor,output)
                def accounts():
                    monitor.call('stop')
                    try:return hashlib.sha256(saved_file(disk,'/home/.system/accounts.db',size_limit=64*1024)).hexdigest()
                    finally:monitor.call('cont')
                before=accounts();print('PASS production account creation and durable database',flush=True)
                monitor.start(2);monitor.key('alt','f10')
                command='compat --trace --jit /home/jdk/bin/java @/home/java-private.args'
                type_command(monitor,'scrub\n'+command+'\n')
                print('RUN production OpenJDK application and subsequent shell persistence',flush=True)
                # Large module reads can outlast USB key reports. Wait for the
                # actual JVM exit before typing the next shell command.
                deadline=time.monotonic()+240
                while True:
                    text=serial.read_text(errors='replace')
                    exited=re.search(r'\[linux \d+\] 231\(([0-9a-f]+),',text)
                    if exited:
                        assert exited[1]=='0','Production JVM exited with '+exited[1]
                        break
                    if process.poll() is not None or 'PANIC' in text or time.monotonic()>deadline:
                        raise AssertionError('Production JVM did not finish')
                    time.sleep(.2)
                print('PASS production JVM exit 0',flush=True)
                type_command(monitor,'weave openjdk-ready.txt ready\nanchor\n')
                wait_saved_file(monitor,disk,'/home/users/tester/openjdk-ready.txt',b'ready\n',timeout=240)
                wait_saved_file(monitor,disk,'/home/users/tester/jvm-proof.txt',
                                b'Real OpenJDK 21 on Nuvora: threads=4 sum=200020000\n',timeout=30)
                assert before==accounts(),'JVM modified the private account database'
                width,height,pixels=monitor.shot(output,'openjdk21');write_png(output/'openjdk21.png',width,height,pixels)
                text=serial.read_text(errors='replace');assert 'PANIC' not in text and '[isolate' not in text and 'PROBE RESULT:' not in text
                manifest={'capture':'production UEFI / q35 / 512 MiB / NVMe / USB keyboard / authenticated Terminal',
                          'kernel_sha256':digest(qemu.BUILD/'nuvora-uefi.elf'),'esp_sha256':digest(qemu.BUILD/'esp.img'),
                          'file':'openjdk21.png','width':width,'height':height,'sha256':digest(output/'openjdk21.png'),
                          'commands':[command],'account_database_unchanged':True,
                          'class_sha256':digest(qemu.ROOT/'examples/java/JavaRuntimeCheck.class'),
                          'note':'Real OpenJDK 21 application, pthreads, Java standard libraries and protected-account access denial; Minecraft is not running.'}
                (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
                print('PASS production OpenJDK: authenticated Terminal, real Java NIO/threads/crypto/GC, persisted file, subsequent shell command and account database unchanged',flush=True)
                print('CAPTURE openjdk21.png: actual GOP framebuffer; original PPM retained',flush=True)
            except BaseException:
                if process.poll() is None:
                    try:monitor.shot(output,'failure')
                    except Exception:pass
                raise
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:process.wait(timeout=5)
                    except subprocess.TimeoutExpired:process.kill();process.wait()


if __name__=='__main__':main()
