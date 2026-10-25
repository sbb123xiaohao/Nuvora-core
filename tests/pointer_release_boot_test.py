#!/usr/bin/env python3
"""Check final pointer packets and tiled resize bounds in a production guest."""
import argparse
import json
import pathlib
import shutil
import tempfile
from desktop_boot_test import first_account, color_bounds, ui_scale, qemu
from launcher_boot_test import guest
from capture_screenshots import write_png
from mkgptdisk import create


def run(resolution, output):
    output.mkdir(parents=True, exist_ok=True)
    failures=[];results=[]
    with tempfile.TemporaryDirectory(prefix='nuvora-pointer-release-') as td:
        private=pathlib.Path(td);disk=private/'data.img';esp=private/'esp.img'
        create(disk,size_mib=128);shutil.copyfile(qemu.BUILD/'esp.img',esp)
        original=qemu.BUILD
        try:
            qemu.BUILD=private
            command=qemu.command(256,disk,machine='q35',kernel=False,esp=esp,
                                  disk_bus='nvme',resolution=resolution)
            command+=qemu.firmware_arguments(qemu.find_uefi_firmware())
        finally:qemu.BUILD=original
        command+=['-display','none','-qmp','stdio','-no-reboot','-device','qemu-xhci,id=xhci',
                  '-device','usb-kbd,bus=xhci.0','-device','usb-tablet,bus=xhci.0']
        with guest(command,output/'serial.log',output) as m:
            first_account(m,output)
            width,height,_=m.shot(output,'home');scale=ui_scale(width,height)
            assert f'{width}x{height}'==resolution
            sw,sh=width//scale,height//scale
            def pointer(x,y,down):m.pointer(width,height,x*scale,y*scale,down)
            def shot(name):return m.shot(output,name)[2]
            def bounds(name):
                pointer(16,sh-75,False)
                b=color_bounds(shot(name),width,0xdce2ed)
                return b[0]//scale,b[1]//scale,(b[2]+1)//scale-b[0]//scale,(b[3]+1)//scale-b[1]//scale
            def editor():
                m.start(1);m.key('meta_l','left');m.key('meta_l','down')
                return bounds('editor-original')
            def record(name,operation):
                try:
                    operation();results.append(name);print('PASS',resolution,name,flush=True)
                except AssertionError as error:
                    failures.append(name+': '+str(error));print('FAIL',resolution,name,str(error),flush=True)
                finally:m.key('alt','f4')

            def release_move():
                x,y,w,h=editor();px,py=x+100,y+14
                pointer(px,py,True);shot('move-down')
                # Motion and button-up share one QMP/USB report. There is no
                # preceding held-motion report for the desktop to rely on.
                pointer(px+60,py+30,False)
                actual=bounds('move-release')
                expected=(min(x+60,sw-w),min(y+30,sh-56-h),w,h)
                assert actual==expected,(actual,expected)
            record('final release moves window',release_move)

            def release_resize():
                x,y,w,h=editor();pointer(x+w-1,y+90,True);shot('resize-down')
                pointer(x+w-41,y+90,False)
                actual=bounds('resize-release');expected=(x,y,max(300,w-40),h)
                # Absolute USB coordinates have 15-bit precision; two rounded
                # positions can differ by one pixel after the guest maps them.
                assert actual[:2]==expected[:2] and actual[3]==h and abs(actual[2]-expected[2])<=1,(actual,expected)
            record('final release resizes window',release_resize)

            def stale_snap():
                x,y,w,h=editor();pointer(x+100,y+14,True);shot('snap-down')
                pointer(0,sh//3,True);shot('snap-preview')
                pointer(sw//2,sh//3+40,False)
                actual=bounds('snap-release')
                assert actual[2:]==(w,h),('old edge preview was committed',actual,(w,h))
            record('release position replaces stale snap preview',stale_snap)

            if sw==640:
                def tiled_resize():
                    m.start(0);m.key('meta_l','right')
                    pointer(sw-1,100,True);shot('tile-click-down')
                    pointer(sw-1,100,False);pointer(16,sh-75,False)
                    pixels=shot('tile-click-up');at=(sw//2)*scale*3
                    assert pixels[at:at+3]==bytes.fromhex('dce2ed'),'a resize click changed the tiled frame'
                    pointer(sw-1,100,True);shot('tile-down')
                    pointer(sw-31,100,True);shot('tile-held')
                    pointer(sw-31,100,False);pointer(16,sh-75,False)
                    pixels=shot('tile-bounded')
                    at=(100*scale*width+(sw-1)*scale)*3
                    write_png(output/'tile-bounded.png',width,height,pixels)
                    assert pixels[at:at+3]==bytes.fromhex('dce2ed'), 'right window border escaped the framebuffer'
                record('undersized tiled window stays within workspace',tiled_resize)
    (output/'results.json').write_text(json.dumps({'resolution':resolution,'passed':results,'failures':failures},indent=2)+'\n')
    assert not failures,failures


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resolution',type=qemu.display_resolution,default='640x480')
    parser.add_argument('--output',type=pathlib.Path,default=qemu.BUILD/'pointer-release')
    args=parser.parse_args();run(args.resolution,args.output.resolve())


if __name__=='__main__':main()
