#!/usr/bin/env python3
"""Run real static runtimes in the production desktop and capture framebuffer."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
import time
from desktop_boot_test import Monitor, first_account, qemu, create
from session_boot_test import wait_saved_file, saved_file
from capture_screenshots import write_png
from mkmedia import create as create_media
from import_media import import_files
from java_desktop_test import type_command


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    output=qemu.BUILD/'compat-desktop';output.mkdir(parents=True,exist_ok=True)
    serial=output/'serial.log';firmware=qemu.find_uefi_firmware()
    if not firmware:raise SystemExit('OVMF required')
    with tempfile.TemporaryDirectory(prefix='nuvora-compat-desktop-') as directory:
        private=pathlib.Path(directory);disk,esp,media=private/'data.img',private/'esp.img',private/'boot.img'
        create(disk,size_mib=128,partitions=1)
        fixtures=qemu.ROOT/'tests/fixtures/compat'
        privacy=private/'privacy.py'
        privacy.write_text('import os\nfor action in (lambda: open("/home/.system/accounts.db"), lambda: os.stat("/home/.system/accounts.db")):\n    try:\n        action()\n    except OSError as error:\n        assert error.args[0] == 13\n    else:\n        raise AssertionError("private account data exposed")\nwith open("python-privacy.txt", "w") as stream:\n    stream.write("PYTHON PRIVATE ACCESS DENIED\\n")\nprint("PYTHON PRIVATE ACCESS DENIED")\n')
        scripts=qemu.ROOT/'examples/compat'
        import_files(disk,[*sorted(fixtures.glob('*.elf')),*[scripts/name for name in ('world.lua','world.js','world.py','terrain.py')],privacy])
        shutil.copyfile(qemu.BUILD/'esp.img',esp);create_media(esp,media)
        original=qemu.BUILD
        try:
            qemu.BUILD=private
            command=qemu.command(memory=256,disk=disk,machine='q35',kernel=False,disk_bus='ahci')+qemu.firmware_arguments(firmware)
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
                    if process.poll() is not None or time.monotonic()>deadline:raise AssertionError('Desktop boot failed')
                    time.sleep(.2)
                first_account(monitor,output)
                def account_digest():
                    monitor.call('stop')
                    try:return hashlib.sha256(saved_file(disk,'/home/.system/accounts.db',size_limit=64*1024)).hexdigest()
                    finally:monitor.call('cont')
                before=account_digest()
                monitor.start(2);monitor.key('alt','f10')
                type_command(monitor,'compat /home/runtime_check.elf "two words" ""\ncompat /home/runtime_check.elf private\nweave compat-ready.txt ready\nanchor\n')
                wait_saved_file(monitor,disk,'/home/users/tester/compat-privacy.txt',b'ACCOUNT DATABASE ACCESS DENIED; uid=1000\n',timeout=20)
                wait_saved_file(monitor,disk,'/home/users/tester/compat-ready.txt',b'ready\n',timeout=20)
                wait_saved_file(monitor,disk,'/home/users/tester/runtime-check.txt',b'musl file I/O\n',timeout=20)
                assert before==account_digest(),'Compatibility process changed protected account data'
                records=[]
                def capture(name,commands):
                    type_command(monitor,'scrub\n'+commands);time.sleep(3)
                    width,height,pixels=monitor.shot(output,name)
                    assert width>=640 and height>=480 and len(set(pixels))>16
                    write_png(output/(name+'.png'),width,height,pixels)
                    records.append({'file':name+'.png','width':width,'height':height,'sha256':digest(output/(name+'.png')),
                                    'commands':commands.strip().splitlines()})
                capture('compat-runtime','compat /home/runtime_check.elf "two words" ""\ncompat /home/runtime_check.elf private\n')
                capture('compat-languages','compat /home/language.elf\ncompat /home/lua.elf /home/world.lua "hello world"\ncompat /home/js.elf /home/world.js "hello world"\n')
                capture('compat-python','compat /home/python.elf /home/world.py "hello world"\ncompat /home/python.elf /home/privacy.py\n')
                type_command(monitor,'weave compat-finished.txt finished\nanchor\n')
                wait_saved_file(monitor,disk,'/home/users/tester/lua-world.txt',b'Lua persistence: 8064\n',timeout=20)
                wait_saved_file(monitor,disk,'/home/users/tester/python-world.txt',b'Python persistence: 8064\n',timeout=20)
                wait_saved_file(monitor,disk,'/home/users/tester/python-privacy.txt',b'PYTHON PRIVATE ACCESS DENIED\n',timeout=20)
                assert before==account_digest(),'Python changed protected account data'
                wait_saved_file(monitor,disk,'/home/users/tester/compat-finished.txt',b'finished\n',timeout=20)
                text=serial.read_text(errors='replace');assert 'PANIC' not in text and '[isolate' not in text and 'PROBE RESULT:' not in text
                manifest={'capture':'production UEFI / q35 / 256 MiB / AHCI / USB keyboard',
                          'kernel_sha256':digest(qemu.BUILD/'nuvora-uefi.elf'),'esp_sha256':digest(qemu.BUILD/'esp.img'),
                          'runtime_elfs':{p.name:digest(p) for p in sorted(fixtures.glob('*.elf'))},'screenshots':records,
                          'protected_account_database_unchanged':True,'uid_from_actual_process':1000,
                          'python_private_open_and_stat_denied':True,
                          'note':'Real musl/C++ core/Lua/QuickJS/MicroPython on Nuvora. Minecraft and OpenJDK are not running.'}
                (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
                print('PASS production compatibility: inherited uid=1000, protected database denied/unchanged, native file writes and Lua persistence',flush=True)
                print('CAPTURE compat-runtime.png + compat-languages.png + compat-python.png: actual framebuffer, original PPM retained',flush=True)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:process.wait(timeout=5)
                    except subprocess.TimeoutExpired:process.kill();process.wait()

if __name__=='__main__':main()
