#!/usr/bin/env python3
import argparse
import subprocess
from qemu import ROOT, BUILD, ARCH, command

parser = argparse.ArgumentParser(description='Boot Nuvora Core in QEMU.')
parser.add_argument('--window', action='store_true', help='Use a VGA window and USB keyboard')
parser.add_argument('--no-usb', action='store_true', help='Boot without the virtual xHCI controller and USB devices')
args = parser.parse_args()
cmd = command(disk=BUILD / 'nuvora-store.img')
if not args.no_usb:
    cmd += ['-device', 'qemu-xhci,id=xhci',
            '-device', 'usb-kbd,id=keyboard,bus=xhci.0,port=1',
            '-device', 'usb-mouse,id=mouse,bus=xhci.0,port=2']
if args.window:
    cmd += ['-serial', 'stdio']
else:
    cmd += ['-display', 'none', '-serial', 'mon:stdio']
    print('QEMU serial console: Ctrl+A then X exits the emulator.', flush=True)
raise SystemExit(subprocess.call(cmd))
