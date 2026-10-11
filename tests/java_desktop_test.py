#!/usr/bin/env python3
"""Capture the real Java bootstrap in the production graphical Terminal."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
import time

from desktop_boot_test import Monitor, first_account, qemu, create
from session_boot_test import wait_saved_file
from capture_screenshots import write_png
from mkmedia import create as create_media
from import_media import import_files


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def type_command(monitor,text):
    # QMP accepts physical key names, not the literal double-quote character.
    for index,part in enumerate(text.split('"')):
        if index: monitor.key('shift','apostrophe')
        if part: monitor.type(part)


def main():
    output=qemu.BUILD/'java-desktop'
    output.mkdir(parents=True,exist_ok=True)
    serial=output/'serial.log'
    firmware=qemu.find_uefi_firmware()
    if not firmware: raise SystemExit('OVMF is required')
    with tempfile.TemporaryDirectory(prefix='nuvora-java-desktop-') as directory:
        private=pathlib.Path(directory)
        disk,esp,media=private/'data.img',private/'esp.img',private/'boot.img'
        create(disk,size_mib=128,partitions=1)
        fixtures=qemu.ROOT/'tests/fixtures/java/multiclass'
        import_files(disk,sorted(fixtures.glob('*.class')))
        shutil.copyfile(qemu.BUILD/'esp.img',esp)
        create_media(esp,media)
        original=qemu.BUILD
        try:
            qemu.BUILD=private
            command=qemu.command(memory=256,disk=disk,machine='q35',kernel=False,disk_bus='ahci')
            command+=qemu.firmware_arguments(firmware)
        finally: qemu.BUILD=original
        command+=['-display','none','-qmp','stdio','-serial',f'file:{serial}',
                  '-device','qemu-xhci,id=xhci','-device','usb-kbd,bus=xhci.0','-device','usb-tablet,bus=xhci.0',
                  '-drive',f'file={media},format=raw,if=none,id=boot_media',
                  '-device','usb-storage,bus=xhci.0,drive=boot_media,bootindex=1']
        with (output/'qemu.log').open('w') as log:
            process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log)
            try:
                monitor=Monitor(process)
                deadline=time.monotonic()+60
                while 'Ring 3: /apps/session' not in serial.read_text(errors='replace'):
                    if process.poll() is not None or time.monotonic()>deadline: raise AssertionError('Desktop boot failed')
                    time.sleep(.2)
                first_account(monitor,output)
                monitor.start(2);monitor.key('alt','f10')
                type_command(monitor,'java -cp /home MultiMain "native runtime"\nweave java-ready.txt ready\nanchor\n')
                wait_saved_file(monitor,disk,'/home/users/tester/java-ready.txt',b'ready\n')
                type_command(monitor,'scrub\njava --version\njava -cp /home MultiMain "native runtime"\n')
                time.sleep(3)
                width,height,pixels=monitor.shot(output,'java21-multiclass')
                # Maximized Terminal intentionally covers most of the desktop.
                assert width>=640 and height>=480 and len(set(pixels))>16
                write_png(output/'java21-multiclass.png',width,height,pixels)
                text=serial.read_text(errors='replace')
                assert 'PANIC' not in text and '[trap]' not in text and 'PROBE RESULT:' not in text
                manifest={'capture':'production UEFI / QEMU q35 / 256 MiB / AHCI / USB keyboard',
                          'kernel_sha256':digest(qemu.BUILD/'nuvora-uefi.elf'),
                          'esp_sha256':digest(qemu.BUILD/'esp.img'),
                          'classes':{p.name:digest(p) for p in sorted(fixtures.glob('*.class'))},
                          'file':'java21-multiclass.png','width':width,'height':height,
                          'sha256':digest(output/'java21-multiclass.png'),
                          'commands':['java --version','java -cp /home MultiMain "native runtime"'],
                          'note':'Real Java 21 multi-class subset; Minecraft is not running.'}
                (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
                print('PASS production Terminal: Java child returned; subsequent file write persisted',flush=True)
                print(f'CAPTURE java21-multiclass.png: {width}x{height}; raw PPM retained for independent comparison',flush=True)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired: process.kill();process.wait()


if __name__=='__main__': main()
