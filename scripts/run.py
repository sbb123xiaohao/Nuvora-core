#!/usr/bin/env python3
import argparse
import subprocess
from qemu import BUILD, ARCH, find_uefi_firmware, firmware_arguments, command, display_resolution

parser = argparse.ArgumentParser(description='Boot Nuvora Core in QEMU.')
parser.add_argument('--window', action='store_true', help='Use a graphics window with virtual USB keyboard and mouse')
parser.add_argument('--fullscreen', action='store_true', help='Start the graphics window fullscreen (implies --window)')
parser.add_argument('--resolution', type=display_resolution, metavar='WIDTHxHEIGHT',
                    help='Preferred UEFI display mode, for example 1920x1080')
parser.add_argument('--audio', action='store_true', help='Attach an Intel HDA controller and analog codec')
parser.add_argument('--memory', type=int, default=64, metavar='MIB', help='Guest memory in MiB (default: 64)')
parser.add_argument('--uefi', action='store_true', help='Boot via the UEFI stub (OVMF firmware required)')
parser.add_argument('--no-usb', action='store_true', help='Boot without the virtual xHCI controller and USB devices')
parser.add_argument('--cpu', help='QEMU CPU model (for example: core2duo, phenom, max)')
parser.add_argument('--machine', choices=['pc', 'q35'], default='pc', help='QEMU machine model')
parser.add_argument('--disk-bus', choices=['ide', 'ahci', 'nvme'], default='ide',
                    help='Attach the existing Nuvora GPT image to IDE, AHCI/SATA or NVMe (default: ide)')
parser.add_argument('--network', action='store_true', help='Attach an e1000e NIC with QEMU user networking')
parser.add_argument('--no-ecam', action='store_true', help='Disable PCIe ECAM and use CF8/CFC fallback')
args = parser.parse_args()
args.window = args.window or args.fullscreen
if ARCH != 'x86_64':
    parser.error('Use scripts/arm64.py for ARCH=aarch64; 32-bit x86 is retired')
if args.memory < 32:
    parser.error('--memory must be at least 32 MiB')
if args.resolution and not args.uefi:
    parser.error('--resolution requires --uefi')

esp = BUILD / 'esp.img'
esp_command = 'make COMPAT=none esp' if BUILD.name.endswith('-native') else 'make esp'
firmware = None
if args.uefi:
    if ARCH != 'x86_64':
        raise SystemExit('UEFI boot is only implemented for x86_64.')
    if args.no_ecam:
        raise SystemExit(f'For UEFI, put nv.no-ecam=1 in {BUILD / "uefi-cmdline.txt"} '
                         f'and rebuild with {esp_command}.')
    firmware = find_uefi_firmware()
    if not firmware:
        raise SystemExit('UEFI firmware not found. Install OVMF (or QEMU edk2 firmware) or set NV_OVMF '
                         'to the firmware file, e.g. /usr/share/OVMF/OVMF_CODE.fd '
                         'or /usr/share/edk2/x64/OVMF_CODE.4m.fd on Arch Linux.')
    if not esp.is_file():
        raise SystemExit(f'Missing {esp}. Build it with: {esp_command} (needs mtools).')
cmd = command(memory=args.memory, disk=BUILD / 'nuvora-store.img', cpu=args.cpu, machine=args.machine,
              kernel=not args.uefi, esp=esp if args.uefi else None, disk_bus=args.disk_bus,
              resolution=args.resolution)
if firmware:
    try:
        cmd += firmware_arguments(firmware)
    except (OSError, RuntimeError) as error:
        raise SystemExit(f'UEFI firmware: {error}') from error
if args.network:
    cmd += ['-netdev', 'user,id=nuvora_net', '-device', 'e1000e,netdev=nuvora_net']
if args.no_ecam:
    cmd += ['-append', 'nv.no-ecam=1']
if not args.no_usb:
    mouse_model = 'usb-tablet' if args.window else 'usb-mouse'
    cmd += ['-device', 'qemu-xhci,id=xhci',
            '-device', 'usb-kbd,id=keyboard,bus=xhci.0,port=1',
            '-device', f'{mouse_model},id=mouse,bus=xhci.0,port=2']
if args.audio:
    cmd += ['-device', 'intel-hda,msi=off', '-device', 'hda-duplex']
if args.window:
    cmd += ['-serial', 'stdio']
    if args.fullscreen:
        cmd += ['-full-screen']
else:
    cmd += ['-display', 'none', '-serial', 'mon:stdio']
    print('QEMU serial console: Ctrl+A then X exits the emulator.', flush=True)
raise SystemExit(subprocess.call(cmd))
