#!/usr/bin/env python3
"""Exercise the production graphical session, idle lock and real power controls.

Only private boot/data images are used. A production image must ignore
nv.test=1; native Ring 3 clients also exercise the kernel permission boundary.
"""
import argparse
import hashlib
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


def settings_page(monitor, index, output=None, timeout=10):
    assert 0 <= index < 5
    monitor.start(6)
    for _ in range(4):
        monitor.key('left')
    for _ in range(index):
        monitor.key('right')
    # QMP acknowledges input injection before the guest consumes its keys or
    # publishes the next frame. Keep testing keyboard navigation, and wait for
    # the requested sidebar selection before using that page's controls.
    with tempfile.TemporaryDirectory(prefix='nuvora-settings-page-') as directory:
        shots = pathlib.Path(directory) if output is None else output
        deadline = time.monotonic() + timeout
        while True:
            width,height,pixels = monitor.shot(shots,'settings-page')
            scale=ui_scale(width,height);sw=width//scale;ww=min(1000,sw-40)
            side=176 if ww-2>=760 else 146
            x=((sw-ww)//2+1+side-20)*scale
            selected=[]
            for page in range(5):
                y=(20+28+84+page*46+19)*scale
                at=(y*width+x)*3
                if int.from_bytes(pixels[at:at+3],'big')==0x1761f5:
                    selected.append(page)
            if selected==[index]: return
            if time.monotonic()>=deadline:
                raise AssertionError(f'Settings keyboard navigation: expected page {index}, selected {selected}')
            time.sleep(.1)


def settings_slider(width,height):
    scale=ui_scale(width,height);sw=width//scale;ww=min(1000,sw-40)
    client=ww-2;side=176 if client>=760 else 146
    left=side+16;area=client-left-16
    return scale,(sw-ww)//2+1+left+28,area-57,20+28+179


def assert_settings_volume(pixels,width,height,percent):
    scale,origin,span,y=settings_slider(width,height)
    knob_x=(origin+span*percent//100)*scale
    # Read the blue rim above its white center and the pointer cursor.
    offset=((y-6)*scale*width+knob_x)*3
    actual=int.from_bytes(pixels[offset:offset+3],'big')
    assert actual==0x1761f5, f'Volume {percent}% thumb at ({knob_x},{(y-6)*scale}): {actual:06x}'


def drag_volume(monitor, width, height, percent, output):
    scale,origin,span,y=settings_slider(width,height);y*=scale
    start=(origin+(span*20+99)//100+1)*scale
    target=(origin+(span*percent+99)//100+1)*scale
    monitor.pointer(width,height,start,y,True)
    monitor.pointer(width,height,target,y,True)
    # A dragged slider must redraw while the button is still held.
    deadline=time.monotonic()+10
    while True:
        _,_,pixels=monitor.shot(output,'volume-dragging')
        try:
            assert_settings_volume(pixels,width,height,percent)
            break
        except AssertionError:
            if time.monotonic()>=deadline: raise
            time.sleep(.1)
    monitor.pointer(width,height,target,y,False)


def saved_file(disk, name, size_limit=4096):
    """Read a committed COW root while the guest is paused or powered off."""
    with disk.open('rb') as stream:
        first, sectors = partition(stream)
        records = roots(stream, first, layout(stream, first, sectors))
        if not records:
            raise AssertionError('No committed data root')
        for kind, path, size, extents in records[-1][2]:
            if path != name.encode():
                continue
            assert kind == 2 and size < size_limit
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


def wait_saved_file(monitor,disk,name,expected,timeout=10):
    """Wait for the guest's committed COW root to contain the complete bytes."""
    deadline=time.monotonic()+timeout
    expected_digest=hashlib.sha256(expected).hexdigest()
    while True:
        monitor.call('stop')
        try:
            try:
                actual=saved_file(disk,name,size_limit=max(4096,len(expected)+1))
            except (AssertionError,OSError,ValueError) as error:
                detail=str(error)
            else:
                if actual==expected: return
                actual_digest=hashlib.sha256(actual).hexdigest()
                detail=(f'expected {len(expected)} bytes / SHA256 {expected_digest}; '
                        f'found {len(actual)} bytes / SHA256 {actual_digest}')
        finally:
            monitor.call('cont')
        if time.monotonic()>=deadline:
            raise AssertionError(f'File commit did not complete for {name}: {detail}')
        time.sleep(.1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, default=qemu.BUILD / 'session-smoke')
    parser.add_argument('--iso', type=pathlib.Path, help='Boot an unmodified production UEFI ISO')
    parser.add_argument('--boot-only', action='store_true', help='Verify graphical login and native client permissions only')
    parser.add_argument('--memory', type=int, default=256, metavar='MIB',
                        help='Guest memory in MiB (default: 256)')
    args = parser.parse_args()
    if args.memory <= 0:
        parser.error('--memory must be a positive integer')
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
            command = qemu.command(memory=args.memory, disk=disk, machine='q35', kernel=False)
            command += qemu.firmware_arguments(firmware)
        finally:
            qemu.BUILD = original
        command += ['-display', 'none', '-qmp', 'stdio', '-serial', f'file:{serial}',
                    '-device', 'qemu-xhci,id=xhci', '-device', 'usb-kbd,bus=xhci.0',
                    '-device', 'usb-tablet,bus=xhci.0', '-audiodev', 'none,id=audio0',
                    '-device', 'intel-hda', '-device', 'hda-duplex,audiodev=audio0']
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
                m.key('esc')  # Enter on the empty workspace can open Apps.
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
                settings_page(m,0);m.shot(output,'03-appearance');m.key('2')
                for _ in range(4): m.key('tab')
                m.key('ret');m.key('tab');m.key('ret')  # List and dot files.
                m.shot(output,'03a-ocean')
                settings_page(m,1);drag_volume(m,width,height,80,output)
                m.shot(output,'03b-volume-80')
                m.start(0);m.key('f8');m.key('alt','f9')  # Type sorting.
                settings_page(m,3);m.key('1')
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
                settings_page(m,4);m.key('tab');m.key('ret')
                m.shot(output,'07-restart-confirmation');m.key('ret')
                assert serial.read_text(errors='replace').count('Ring 3: /apps/session')==1
                m.key('tab');m.key('ret');m.key('tab');m.key('ret');boot(2)
                m.shot(output,'08-reboot-sign-in');m.type(password);m.key('ret')
                account_wait(m,output,'09-persisted-workspace',locked=True)
                assert desktop_visible(m.shot(output,'09a-restored-preferences')[2],scale)
                settings_page(m,1)
                _,_,sound=m.shot(output,'09b-restored-volume')
                assert_settings_volume(sound,width,height,80)
                settings_page(m,4);m.key('tab');m.key('tab');m.key('ret')
                m.shot(output,'10-shutdown-confirmation');m.key('tab');m.key('ret')
                assert process.wait(timeout=20)==0
                assert struct.unpack('<IIII',saved_file(disk,'/home/users/tester/.desktop'))==(0x3255494e,1,1,80|128|256|1024)
                text=serial.read_text(errors='replace')
                assert 'SMEP enabled' in text and 'Nuvora Core halted.' in text
                assert 'PANIC' not in text and '[trap]' not in text and 'PROBE RESULT:' not in text
                assert 'Loom / Nuvora command environment' not in text and 'firmware shutdown unavailable' not in text
                print('PASS graphical restart/shutdown: wallpaper, Files list/sort/hidden and live HDA volume persist; safe power confirmation and quiet kernel',flush=True)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired: process.kill();process.wait()


if __name__ == '__main__': main()
