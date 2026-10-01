#!/usr/bin/env python3
"""Boot BIOS text mode and verify masked setup, saved login and wrong passwords."""
import pathlib
import argparse
import subprocess
import tempfile
import time
from desktop_boot_test import Monitor, create, qemu


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso',type=pathlib.Path,help='Boot a production BIOS ISO through GRUB')
    parser.add_argument('--output',type=pathlib.Path,default=qemu.BUILD/'account-console-smoke')
    args=parser.parse_args()
    output=args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    password = 'testable console password 1'
    serial = output / 'serial.log'
    with tempfile.TemporaryDirectory(prefix='nuvora-console-account-') as directory:
        disk = pathlib.Path(directory) / 'data.img'; create(disk, size_mib=128, partitions=1)
        command = qemu.command(memory=64, disk=disk,kernel=not args.iso)
        if args.iso: command+=['-cdrom',str(args.iso.resolve()),'-boot','d']
        command += ['-display', 'none', '-qmp', 'stdio', '-serial', f'file:{serial}']
        with (output / 'qemu.log').open('w') as log:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log)
            try:
                monitor = Monitor(process)
                def wait(text, offset=0):
                    deadline = time.monotonic() + 150
                    while True:
                        content = serial.read_text(errors='replace')
                        if text in content[offset:]: return content
                        if process.poll() is not None or time.monotonic() > deadline:
                            raise AssertionError(f'Text login did not reach {text!r}')
                        time.sleep(.2)
                wait('Username:'); monitor.type('tester\n');wait('Display name:')
                monitor.type('Console User\n');wait('Password:')
                monitor.type(password+'\n');wait('Repeat password:');monitor.type(password+'\n')
                content=wait('Loom / Nuvora command environment');assert password not in content
                assert '****************' in content
                monitor.type('horizon\n'); content=wait('RAM managed:');offset=len(content)
                monitor.type('renew\n'); wait('Nuvora local sign-in',offset)
                monitor.type('tester\n');monitor.type('incorrect\n');wait('Sign-in:',offset)
                content=wait('Username:',offset);time.sleep(.3)
                monitor.type('tester\n');monitor.type(password+'\n')
                content=wait('Loom / Nuvora command environment',offset)
                assert password not in content and 'PANIC' not in content and '[trap]' not in content
                monitor.type('rest\n'); assert process.wait(timeout=15)==0
                content=serial.read_text(errors='replace')
                assert 'Nuvora Core halted.' in content and 'firmware shutdown unavailable' not in content
                print('PASS BIOS accounts/power: masked OOBE, saved login, OS reboot and ACPI shutdown')
            finally:
                if process.poll() is None:
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired: process.kill();process.wait()


if __name__ == '__main__': main()
