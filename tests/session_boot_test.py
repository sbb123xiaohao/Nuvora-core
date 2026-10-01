#!/usr/bin/env python3
"""Exercise the production graphical session, idle lock and real power controls.

Only private boot/data images are used. A production image must ignore
nv.test=1; native Ring 3 clients also exercise the kernel permission boundary.
"""
import argparse
import pathlib
import shutil
import struct
import subprocess
import tempfile
import time
from desktop_boot_test import Monitor, account_wait, first_account, ui_scale, qemu, create, desktop_visible
from import_media import partition
from extent_store import layout, roots, BLOCK
from mkmedia import create as create_media


def saved_file(disk, name):
    """Read a committed COW root while the guest is paused or powered off."""
    with disk.open('rb') as stream:
        first, sectors = partition(stream)
        records = roots(stream, first, layout(stream, first, sectors))
        if not records:
            raise AssertionError('No committed data root')
        for kind, path, size, extents in records[-1][2]:
            if path != name.encode():
                continue
            assert kind == 2 and size < 4096
            result = bytearray(size)
            for logical, physical, length in extents:
                offset = logical * BLOCK
                amount = min(length * BLOCK, size - offset)
                if amount <= 0:
                    continue
                stream.seek(first * 512 + physical * BLOCK)
                data = stream.read(amount)
                assert len(data) == amount
                result[offset:offset + amount] = data
            return bytes(result)
    raise AssertionError(f'Saved file missing: {name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, default=qemu.BUILD / 'session-smoke')
    parser.add_argument('--iso', type=pathlib.Path, help='Boot an unmodified production UEFI ISO')
    parser.add_argument('--boot-only', action='store_true', help='Verify graphical login and native client permissions only')
    args = parser.parse_args()
    if args.iso:
        args.iso=args.iso.resolve()
        if not args.iso.is_file(): parser.error('Production ISO not found')
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    firmware = qemu.find_uefi_firmware()
    if not firmware:
        raise SystemExit('OVMF not found')
    password = 'testable local passphrase 1'
    serial = output / 'serial.log'
    with tempfile.TemporaryDirectory(prefix='nuvora-production-session-') as directory:
        private = pathlib.Path(directory)
        disk, esp, media = private / 'data.img', private / 'esp.img', private / 'boot.img'
        create(disk, size_mib=128, partitions=1)
        if not args.iso:
            shutil.copyfile(qemu.BUILD / 'esp.img', esp)
            options = private / 'CMDLINE'; options.write_text('nv.test=1\n')
            subprocess.run(['mcopy', '-o', '-i', str(esp), str(options), '::/EFI/NUVORA/CMDLINE'], check=True)
            create_media(esp, media)
        original = qemu.BUILD; qemu.BUILD = private
        try:
            command = qemu.command(memory=256, disk=disk, machine='q35', kernel=False)
            command += qemu.firmware_arguments(firmware)
        finally:
            qemu.BUILD = original
        command += ['-display', 'none', '-qmp', 'stdio', '-serial', f'file:{serial}',
                    '-device', 'qemu-xhci,id=xhci', '-device', 'usb-kbd,bus=xhci.0',
                    '-device', 'usb-tablet,bus=xhci.0']
        if args.iso:
            command+=['-cdrom',str(args.iso),'-boot','d']
        else:
            command+=['-drive',f'file={media},format=raw,if=none,id=boot_media',
                      '-device','usb-storage,bus=xhci.0,drive=boot_media,bootindex=1']
        with (output / 'qemu.log').open('w') as log:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log)
            try:
                m = Monitor(process)
                def boot(count):
                    deadline = time.monotonic() + 60
                    while serial.read_text(errors='replace').count('Ring 3: /apps/session') < count:
                        if process.poll() is not None or time.monotonic() > deadline:
                            raise AssertionError('Production graphical supervisor did not boot')
                        time.sleep(.2)
                    time.sleep(1)
                boot(1); first_account(m, output, password=password)
                width, height, home = m.shot(output, '01-workspace')
                scale = ui_scale(width, height); sw = width // scale
                assert desktop_visible(home, scale)
                m.key('esc');m.type('help\n')
                assert desktop_visible(m.shot(output, '01a-no-console-escape')[2], scale)
                m.start(2);m.type('forge probe session-security\n');time.sleep(1)
                m.shot(output, '02-native-client-permissions');m.type('anchor\n');time.sleep(.5)
                m.call('stop')
                try: assert saved_file(disk, '/home/users/tester/session-security.txt') == b'SESSION SECURITY RESULT: 7 passed, 0 failed\n'
                finally: m.call('cont')
                if args.boot_only:
                    text=serial.read_text(errors='replace')
                    assert 'PANIC' not in text and '[trap]' not in text and 'PROBE RESULT:' not in text
                    assert 'Loom / Nuvora command environment' not in text
                    print(f'PASS production {"UEFI ISO" if args.iso else "USB media"}: graphical OOBE/workspace, terminal and 7 native client permission assertions',flush=True)
                    return
                m.start(6)
                wx, wy = (sw-min(560, sw-40))//2, 20
                ww = min(560, sw-40);bw=(ww-192)//3
                def click(cx, cy):
                    px, py = (wx+1+cx)*scale, (wy+28+cy)*scale
                    m.pointer(width,height,px,py,True);m.pointer(width,height,px,py,False)
                    time.sleep(.25)
                m.shot(output, '03-appearance');click(162+bw+16,158)
                m.shot(output, '03a-ocean');click(35,126);click(179,158)
                m.shot(output, '04-security-one-minute');m.key('esc')
                source='UEFI ISO' if args.iso else 'USB boot with nv.test=1 ignored'
                print(f'PASS production {source}: graphical authentication and native client authority checks', flush=True)
                deadline=time.monotonic()+180
                while True:
                    frame=m.shot(output,'05-idle-lock')[2]
                    if not desktop_visible(frame,scale): break
                    if time.monotonic()>deadline: raise AssertionError('Kernel idle lock did not activate')
                    time.sleep(.8)
                m.key('f10');assert m.shot(output,'05a-lock-input-barrier')[2]==frame
                m.type('incorrect');m.key('ret');account_wait(m,output,'05b-rejected',locked=True)
                assert not desktop_visible(m.shot(output,'05c-still-locked')[2],scale)
                m.key('ctrl','a');m.type(password);m.key('ret');account_wait(m,output,'06-unlocked',locked=True)
                assert desktop_visible(m.shot(output,'06a-resumed')[2],scale)
                print('PASS real idle lock, app/launcher barrier, wrong-password rejection and resume', flush=True)
                m.start(6);m.key('right');m.key('right');m.key('tab');m.key('ret')
                m.shot(output,'07-restart-confirmation');m.key('ret')
                assert serial.read_text(errors='replace').count('Ring 3: /apps/session')==1
                m.key('tab');m.key('ret');m.key('tab');m.key('ret');boot(2)
                m.shot(output,'08-reboot-sign-in');m.type(password);m.key('ret')
                account_wait(m,output,'09-persisted-workspace',locked=True)
                assert desktop_visible(m.shot(output,'09a-restored-preferences')[2],scale)
                m.start(6);m.key('right');m.key('right');m.key('tab');m.key('tab');m.key('ret')
                m.shot(output,'10-shutdown-confirmation');m.key('tab');m.key('ret')
                assert process.wait(timeout=20)==0
                assert struct.unpack('<IIII',saved_file(disk,'/home/users/tester/.desktop'))==(0x3155494e,1,1,0)
                text=serial.read_text(errors='replace')
                assert 'SMEP enabled' in text and 'Nuvora Core halted.' in text
                assert 'PANIC' not in text and '[trap]' not in text and 'PROBE RESULT:' not in text
                assert 'Loom / Nuvora command environment' not in text and 'firmware shutdown unavailable' not in text
                print('PASS graphical restart/shutdown: safe confirmation, account/preferences persistence, ACPI power and quiet kernel',flush=True)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired: process.kill();process.wait()


if __name__ == '__main__': main()
