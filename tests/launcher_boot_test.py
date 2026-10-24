#!/usr/bin/env python3
"""Exercise real production pointer gestures and durable, per-user Apps pins.

Requires make diagnostics esp, QEMU and OVMF. Uses only private data/boot images.
"""
import argparse
from contextlib import contextmanager
import hashlib
import json
import pathlib
import shutil
import struct
import subprocess
import tempfile
import time
from desktop_boot_test import Monitor, first_account, account_wait, color_bounds, ui_scale, qemu
from session_boot_test import wait_saved_file
from capture_screenshots import write_png
from mkgptdisk import create


@contextmanager
def guest(command, serial, output):
    with (output / (serial.stem + '-qemu.log')).open('w') as log:
        proc = subprocess.Popen(command + ['-serial', 'file:' + str(serial)],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log)
        try:
            monitor = Monitor(proc)
            deadline = time.monotonic() + 60
            while 'Ring 3: /apps/session' not in serial.read_text(errors='replace'):
                assert proc.poll() is None and time.monotonic() < deadline, 'Desktop did not boot'
                time.sleep(.2)
            yield monitor
        finally:
            if proc.poll() is None:
                proc.terminate()
                try: proc.wait(timeout=5)
                except subprocess.TimeoutExpired: proc.kill(); proc.wait()


def menu_geometry(sw, sh):
    w, h = min(560, sw-32), min(450, sh-104)
    return (sw-w)//2, 44+(sh-100-h)//2, w, h


def menu_open(pixels, width, scale, sw, sh):
    x, y, w, _ = menu_geometry(sw, sh)
    at = ((y+20)*scale*width+(x+w-10)*scale)*3
    return pixels[at:at+3] == bytes.fromhex('f5f8ff')


def crop(pixels, width, x, y, w, h, scale):
    return b''.join(pixels[(row*width+x*scale)*3:(row*width+(x+w)*scale)*3]
                    for row in range(y*scale, (y+h)*scale))


def run(resolution, output, firmware):
    output.mkdir(parents=True, exist_ok=True)
    captures = []
    with tempfile.TemporaryDirectory(prefix='nuvora-apps-pin-') as td:
        private = pathlib.Path(td); disk, esp = private/'data.img', private/'esp.img'
        create(disk, size_mib=128); shutil.copyfile(qemu.BUILD/'esp.img', esp)
        original = qemu.BUILD
        try:
            qemu.BUILD = private
            command = qemu.command(256, disk, machine='q35', kernel=False, esp=esp,
                                   disk_bus='nvme', resolution=resolution)
            command += qemu.firmware_arguments(firmware)
        finally: qemu.BUILD = original
        command += ['-display', 'none', '-qmp', 'stdio', '-no-reboot',
                    '-device', 'qemu-xhci,id=xhci', '-device', 'usb-kbd,bus=xhci.0',
                    '-device', 'usb-tablet,bus=xhci.0']
        password = 'testable local passphrase 1'
        with guest(command, output/'serial.log', output) as m:
            first_account(m, output, password=password)
            width, height, _ = m.shot(output, 'home')
            assert f'{width}x{height}' == resolution
            scale=ui_scale(width,height); sw,sh=width//scale,height//scale

            def frame(name): return m.shot(output,name)[2]
            def capture(name, pixels):
                path=output/(name+'.png');write_png(path,width,height,pixels)
                record={'file':f'{resolution}/{path.name}',
                        'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
                captures[:]=[item for item in captures if item['file']!=record['file']]+[record]
            def wait_color(name, x, y, color):
                deadline=time.monotonic()+15
                while True:
                    pixels=frame(name);at=(y*scale*width+x*scale)*3
                    if pixels[at:at+3]==color.to_bytes(3,'big'): return pixels
                    assert time.monotonic()<deadline, f'{name}: expected color {color:06x}'
            def pins(mask):
                wait_saved_file(m,disk,'/home/users/tester/.apps',struct.pack('<II',0x3150414e,mask))
            def pointer(x,y,down): m.pointer(width,height,x*scale,y*scale,down)
            def pin_window(title_x,title_y):
                pointer(title_x,title_y,True)
                pointer(sw-187,23,True)
                pixels=wait_color('drop-preview',sw-240,23,0x658346)
                capture('drop-preview',pixels)
                pointer(sw-187,23,False)
                deadline=time.monotonic()+15
                while not menu_open(frame('pinned-menu'),width,scale,sw,sh):
                    assert time.monotonic()<deadline,'Drop did not open Apps'

            # Hold, leave, return and release: a drag must never become a click.
            m.key('f10');x,y,w,h=menu_geometry(sw,sh)
            cols=3 if sw>=580 else 2;rows=(7+cols-1)//cols
            cell=(w-40-(cols-1)*12)//cols;row=min(88,(h-154)//rows)
            cx,cy=x+20+cell//2,y+98+(row-4)//2
            pointer(cx,cy,True)
            wait_color('app-held',cx,y+101,0xcddfe6)
            assert menu_open(frame('app-held'),width,scale,sw,sh),'App started on down'
            pointer(16,sh-75,True);pointer(cx,cy,True);pointer(cx,cy,False)
            assert menu_open(frame('cancelled-click'),width,scale,sw,sh),'Drag became a click'
            m.key('esc')
            print('PASS',resolution,'release-only launch; leave/return cancels click',flush=True)

            # A real mouse click still starts the native editor.
            m.start(1,mouse=True);pointer(16,sh-75,False)
            bounds=color_bounds(frame('editor-in-workspace'),width,0xdce2ed)
            assert bounds[3]<(sh-56)*scale,'Initial editor extends underneath the Dock'
            editor_w,editor_h=min(680,sw-155),min(470,sh-104)
            editor_x,editor_y=min(140,sw-editor_w),min(62,sh-56-editor_h)
            before=crop(frame('editor-original'),width,editor_x,editor_y,editor_w,editor_h,scale)
            pointer(editor_x+100,editor_y+14,True)
            pointer(editor_x+140,editor_y+40,True)
            m.key('esc');pointer(editor_x+140,editor_y+40,False);pointer(16,sh-75,False)
            after=crop(frame('escape-restored'),width,editor_x,editor_y,editor_w,editor_h,scale)
            assert after==before,'Escape did not restore the exact window geometry/content'
            pin_window(editor_x+100,editor_y+14);pins(1<<1)
            capture('pinned-menu',frame('pinned-menu'))
            m.key('esc');pointer(16,sh-75,False)
            assert crop(frame('editor-after-pin'),width,editor_x,editor_y,editor_w,editor_h,scale)==before
            print('PASS',resolution,'native window pin restores geometry; durable private mask',flush=True)

            # Unpin by keyboard, pin again, then test the right-click path.
            m.key('f10');m.key('ctrl','p');pins(0)
            m.key('ctrl','p');pins(1<<1)
            pointer(cx,cy,False)
            m.call('input-send-event',{'events':[{'type':'btn','data':{'button':'right','down':True}}]})
            pins(0)
            m.call('input-send-event',{'events':[{'type':'btn','data':{'button':'right','down':False}}]})
            m.key('esc');m.key('alt','f4')

            # The real child Terminal carries its launcher identity, including
            # a drag out of maximized mode. Pinning must restore maximization.
            m.start(2);m.key('alt','f10')
            pin_window(sw//2,14);pins(1<<2)
            m.key('esc');pointer(16,sh-75,False)
            bounds=color_bounds(frame('terminal-maximized'),width,0x15222b)
            assert bounds==(scale,28*scale,width-scale-1,(sh-56)*scale-scale-1),bounds
            print('PASS',resolution,'client identity and maximized window restored after pin',flush=True)

            # Lock while holding an app: no pending launch may survive unlock.
            m.key('f10');pointer(cx,cy,True)
            wait_color('lock-held',cx,y+101,0xcddfe6)
            m.key('meta_l','l');pointer(cx,cy,False)
            m.type(password);m.key('ret');account_wait(m,output,'unlocked',locked=True)
            m.key('f10');capture('before-reboot',frame('before-reboot'))
            assert menu_open(frame('before-reboot'),width,scale,sw,sh)
            print('PASS',resolution,'lock cancels pending click',flush=True)

        with guest(command, output/'reboot-serial.log', output) as m:
            m.type(password);m.key('ret');account_wait(m,output,'reboot-login',locked=True)
            m.key('f10');pixels=m.shot(output,'restored-menu')[2]
            assert menu_open(pixels,width,scale,sw,sh)
            # Terminal is the only pin and therefore the first item.
            m.key('ret');time.sleep(.8)
            pixels=m.shot(output,'restored-terminal')[2]
            assert color_bounds(pixels,width,0x15222b)
            m.key('f10');pixels=m.shot(output,'restored-menu')[2]
            path=output/'restored-menu.png';write_png(path,width,height,pixels)
            captures.append({'file':f'{resolution}/{path.name}',
                             'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
            pins_value=struct.pack('<II',0x3150414e,1<<2)
            wait_saved_file(m,disk,'/home/users/tester/.apps',pins_value)
            print('PASS',resolution,'reboot restores pin order and launches the correct app',flush=True)
        for log in (output/'serial.log',output/'reboot-serial.log'):
            text=log.read_text(errors='replace')
            assert 'PANIC' not in text and '[isolate' not in text and 'PROBE RESULT:' not in text
    return captures


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resolutions',nargs='+',type=qemu.display_resolution,default=['1280x800'])
    parser.add_argument('--output',type=pathlib.Path,default=qemu.BUILD/'launcher-desktop')
    args=parser.parse_args();firmware=qemu.find_uefi_firmware()
    if not firmware: raise SystemExit('OVMF required')
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    manifest={'capture':'production UEFI/q35/NVMe/USB/256 MiB','images':[]}
    for resolution in args.resolutions:
        manifest['images']+=run(resolution,output/resolution,firmware)
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('PASS launcher pointer, pin persistence and production desktop checks',flush=True)


if __name__=='__main__': main()
