#!/usr/bin/env python3
import argparse
import subprocess
from qemu import BUILD, ARCH, find_uefi_firmware, firmware_arguments, command

parser = argparse.ArgumentParser(description='Boot Nuvora Core in QEMU.')
parser.add_argument('--window', action='store_true', help='Use a graphics window with virtual USB keyboard and mouse')
parser.add_argument('--audio', action='store_true', help='Attach an Intel HDA controller and analog codec')
parser.add_argument('--memory', type=int, default=64, metavar='MIB', help='Guest memory in MiB (default: 64)')
parser.add_argument('--uefi', action='store_true', help='Boot via the UEFI stub (OVMF firmware required)')
parser.add_argument('--no-usb', action='store_true', help='Boot without the virtual xHCI controller and USB devices')
parser.add_argument('--cpu', help='QEMU CPU model (for example: core2duo, phenom, max)')
parser.add_argument('--machine', choices=['pc', 'q35'], default='pc', help='QEMU machine model')
parser.add_argument('--disk-bus', choices=['ide', 'nvme'], default='ide',
                    help='Attach the existing Nuvora GPT image to IDE or NVMe (default: ide)')
parser.add_argument('--no-ecam', action='store_true', help='Disable PCIe ECAM and use CF8/CFC fallback')
args = parser.parse_args()
if ARCH != 'x86_64':
    parser.error('Use scripts/arm64.py for ARCH=aarch64; 32-bit x86 is retired')
if args.memory < 32:
    parser.error('--memory must be at least 32 MiB')

esp = BUILD / 'esp.img'
firmware = None
if args.uefi:
    if ARCH != 'x86_64':
        raise SystemExit('UEFI boot is only implemented for x86_64.')
    if args.no_ecam:
        raise SystemExit('For UEFI, put nv.no-ecam=1 in build/x86_64/uefi-cmdline.txt '
                         'and rebuild with make esp.')
    firmware = find_uefi_firmware()
    if not firmware:
        raise SystemExit('UEFI firmware not found. Install OVMF (or QEMU edk2 firmware) or set NV_OVMF '
                         'to the firmware file, e.g. /usr/share/OVMF/OVMF_CODE.fd.')
    if not esp.is_file():
        raise SystemExit(f'Missing {esp}. Build it with: make esp (needs mtools).')
cmd = command(memory=args.memory, disk=BUILD / 'nuvora-store.img', cpu=args.cpu, machine=args.machine,
              kernel=not args.uefi, esp=esp if args.uefi else None, disk_bus=args.disk_bus)
if firmware:
    cmd += firmware_arguments(firmware)
if args.no_ecam:
    cmd += ['-append', 'nv.no-ecam=1']
if not args.no_usb:
    cmd += ['-device', 'qemu-xhci,id=xhci',
            '-device', 'usb-kbd,id=keyboard,bus=xhci.0,port=1',
            '-device', 'usb-mouse,id=mouse,bus=xhci.0,port=2']
if args.audio:
    cmd += ['-device', 'intel-hda,msi=off', '-device', 'hda-duplex']
if args.window:
    cmd += ['-serial', 'stdio']
else:
    cmd += ['-display', 'none', '-serial', 'mon:stdio']
    print('QEMU serial console: Ctrl+A then X exits the emulator.', flush=True)
raise SystemExit(subprocess.call(cmd))
