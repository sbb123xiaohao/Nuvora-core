#!/usr/bin/env python3
"""Exercise first setup, local users, passwords, locking and persisted sign-in.

Uses real UEFI/QEMU input on a private disk; exports screenshots for review.
"""
import argparse
import pathlib
import shutil
import subprocess
import tempfile
import time
from desktop_boot_test import Monitor, account_wait, first_account, ui_scale, qemu, create, color_count, desktop_visible
from session_boot_test import saved_file


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--machine', choices=['pc', 'q35'], default='q35')
    parser.add_argument('--keyboard', choices=['usb', 'ps2'], default='usb')
    parser.add_argument('--output', type=pathlib.Path, default=qemu.ROOT / 'build/account-smoke')
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    firmware = qemu.find_uefi_firmware()
    if not firmware:
        raise SystemExit('OVMF not found')
    password = 'testable local passphrase 1'
    replacement = 'replacement local password 2'
    with tempfile.TemporaryDirectory(prefix='nuvora-accounts-') as directory:
        private = pathlib.Path(directory)
        disk, esp = private / 'data.img', private / 'esp.img'
        create(disk, size_mib=128, partitions=1); shutil.copyfile(qemu.BUILD / 'esp.img', esp)
        original = qemu.BUILD; qemu.BUILD = private
        try:
            command = qemu.command(memory=256, disk=disk, machine=args.machine, kernel=False, esp=esp)
            command += qemu.firmware_arguments(firmware)
        finally:
            qemu.BUILD = original
        command += ['-display', 'none', '-qmp', 'stdio', '-serial', f'file:{output}/serial.log',
                    '-device', 'qemu-xhci,id=xhci', '-device', 'usb-tablet,bus=xhci.0']
        if args.keyboard == 'usb': command += ['-device', 'usb-kbd,bus=xhci.0']
        with (output / 'qemu.log').open('w') as log:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log)
            try:
                m = Monitor(process)
                deadline = time.monotonic() + 40
                while '[ok] entering Ring 3' not in (output / 'serial.log').read_text(errors='replace'):
                    if process.poll() is not None or time.monotonic() > deadline: raise RuntimeError('UEFI did not boot')
                    time.sleep(.2)
                time.sleep(1); first_account(m, output, password=password)
                width, height, home = m.shot(output, '01-desktop')
                assert desktop_visible(home)
                # A real credential barrier: the normal app launcher cannot open while locked.
                m.key('meta_l', 'l'); locked = m.shot(output, '02-locked')[2]
                m.key('f10'); assert m.shot(output, '02a-locked-launcher')[2] == locked
                m.type('incorrect'); m.key('ret'); account_wait(m, output, '03-wrong-password', locked=True)
                m.key('ctrl', 'a'); m.type(password); m.key('ret'); account_wait(m, output, '04-unlocked', locked=True)
                assert desktop_visible(m.shot(output, '04a-desktop')[2])
                m.start(5)
                width, height, accounts = m.shot(output, '05-accounts')
                scale = ui_scale(width, height); sw = width // scale
                wx, wy = (sw - min(560, sw - 40)) // 2, 20
                w, h = min(560, sw - 40) - 2, min(455, height // scale - 76) - 29
                def click(x, y):
                    m.pointer(width, height, (wx + 1 + x) * scale, (wy + 28 + y) * scale, True)
                    m.pointer(width, height, (wx + 1 + x) * scale, (wy + 28 + y) * scale, False)
                def wait(name): return account_wait(m, output, name, rectangle=(wx+1, wy+28, w, h))
                click(65, h-31); m.type('bob'); m.key('tab'); m.type('Bob'); m.key('tab')
                m.type(password); m.key('tab'); m.type(password); m.key('ret'); wait('06-user-added')
                click(40, 121); click(min(180, w//3)+40, 220)
                m.shot(output, '07-user-details')
                m.key('alt', 'd'); m.key('ret'); time.sleep(.5)
                m.shot(output, '07a-user-disabled')
                click(min(180, w//3)+40, 220); m.key('alt', 'd'); m.key('ret'); time.sleep(.5)
                # Sign out uses the Start footer. Close Accounts so no UI contains old credentials.
                m.key('alt', 'f4'); m.start(1);m.type('Unsaved document 0123456789')
                m.shot(output,'07b-editor');m.key('f10');m.key('ctrl','q')
                prompt=m.shot(output,'07c-signout-unsaved')[2];m.key('esc')
                assert m.shot(output,'07d-signout-cancelled')[2]!=prompt
                m.key('f10');m.key('ctrl','q');m.key('d');time.sleep(.8)
                m.shot(output, '08-sign-in'); m.key('shift', 'tab'); m.key('ctrl', 'a'); m.type('bob')
                m.key('tab'); m.type(password); m.key('ret'); account_wait(m, output, '09-bob-desktop', locked=True)
                assert desktop_visible(m.shot(output, '09a-bob-desktop')[2])
                assert color_count(m.shot(output,'09b-clean-session')[2],0xfaf9f5)<100
                m.start(2);m.type('forge probe session-security\n');time.sleep(.8)
                m.shot(output,'09c-standard-kernel-permissions');m.type('anchor\n');time.sleep(.5)
                m.call('stop')
                try: assert saved_file(disk,'/home/users/bob/session-security.txt')==b'SESSION SECURITY RESULT: 10 passed, 0 failed\n'
                finally: m.call('cont')
                m.key('alt','f4');time.sleep(.4)
                m.start(5); m.shot(output, '10-standard-user')
                click(min(180, w//3)+40, 180); m.type('incorrect'); m.key('tab'); m.type(replacement)
                m.key('tab'); m.type(replacement); m.key('ret')
                wrong = wait('10a-password-change-rejected')[2]
                # Small antialiased glyphs need not contain a fully covered
                # pixel. Inspect red text in the message area, away from icons.
                left, top = (wx+23)*scale, (wy+28+h-84)*scale
                rows = [wrong[(y*width+left)*3:(y*width+left+(w-44)*scale)*3]
                        for y in range(top, top+20*scale)]
                message = b''.join(rows)
                assert sum(r>g+35 and r>b+35 for r,g,b in zip(message[::3], message[1::3], message[2::3])) > 20
                m.key('shift', 'tab'); m.key('shift', 'tab')
                m.type(password); m.key('tab'); m.type(replacement)
                m.key('tab'); m.type(replacement); m.key('ret'); wait('11-password-changed')
                # Restart the actual machine, retaining the data image. OOBE must not reappear.
                m.call('system_reset'); time.sleep(5)
                sign_in = m.shot(output, '12-reboot-sign-in')[2]
                assert color_count(sign_in, 0xf6f8f9) < 180000  # Login form, not the larger OOBE form.
                m.key('shift', 'tab'); m.key('ctrl', 'a'); m.type('bob'); m.key('tab'); m.type(password)
                m.key('ret'); account_wait(m, output, '13-old-password-rejected', locked=True)
                assert not desktop_visible(m.shot(output, '13a-still-sign-in')[2])
                m.key('ctrl', 'a'); m.type(replacement); m.key('ret'); account_wait(m, output, '14-new-password-login', locked=True)
                assert desktop_visible(m.shot(output, '14a-persisted-desktop')[2])
                m.start(6);m.shot(output,'15-settings');m.key('esc');m.key('esc')
                assert desktop_visible(m.shot(output,'15a-graphical-workspace')[2])
                text = (output / 'serial.log').read_text(errors='replace')
                assert 'PANIC' not in text and '[trap]' not in text
                assert 'Ring 3: /apps/session' in text and 'Loom / Nuvora command environment' not in text
                print(f'PASS UEFI accounts ({args.machine}, {args.keyboard}): OOBE, bad password, lock input barrier, '
                      f'users, disable, 10 standard-client kernel permission assertions, rejected password change/retry and reboot persistence. Screens: {output}')
            finally:
                if process.poll() is None:
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired: process.kill(); process.wait()


if __name__ == '__main__': main()
