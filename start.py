#!/usr/bin/env python3
"""Run a prebuilt Nuvora kernel in QEMU; default architecture is x86-64."""
import argparse
import os
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--arch', choices=['x86_64', 'i686'], default='x86_64')
parser.add_argument('--window', action='store_true', help='Use a VGA window instead of the serial terminal')
parser.add_argument('--uefi', action='store_true', help='Boot via the UEFI stub (x86_64 only; needs OVMF and mtools-built ESP)')
parser.add_argument('--no-usb', action='store_true', help='Boot without virtual USB devices')
parser.add_argument('--disk-size', type=int, default=64, metavar='MIB', help='New data image size in MiB (default: 64)')
parser.add_argument('--memory', type=int, default=64, metavar='MIB', help='Guest memory in MiB (default: 64)')
parser.add_argument('--cpu', help='QEMU CPU model; defaults to qemu64 or qemu32')
parser.add_argument('--machine', choices=['pc', 'q35'], default='pc', help='QEMU machine model')
parser.add_argument('--no-ecam', action='store_true', help='Disable PCIe ECAM and use CF8/CFC fallback')
args = parser.parse_args()
if args.memory < 32:
    parser.error('--memory must be at least 32 MiB')
if args.uefi and args.arch != 'x86_64':
    parser.error('UEFI boot is only implemented for x86_64')
env = os.environ.copy()
env['NV_ARCH'] = args.arch
build = root / 'build' / args.arch
if not (build / 'boot.elf').is_file():
    raise SystemExit(f'Missing kernel. Build it first: make ARCH={args.arch}')
subprocess.run([sys.executable, str(root / 'scripts/mkdisk.py'),
                str(build / 'nuvora-store.img'), '--if-missing',
                '--size', str(args.disk_size)], check=True)
cmd = [sys.executable, str(root / 'scripts/run.py')]
cmd += ['--machine', args.machine]
cmd += ['--memory', str(args.memory)]
if args.cpu:
    cmd += ['--cpu', args.cpu]
if args.window:
    cmd.append('--window')
if args.uefi:
    if args.arch != 'x86_64':
        raise SystemExit('UEFI boot is only implemented for x86_64.')
    cmd.append('--uefi')
if args.no_usb:
    cmd.append('--no-usb')
if args.no_ecam:
    cmd.append('--no-ecam')
raise SystemExit(subprocess.call(cmd, env=env))
