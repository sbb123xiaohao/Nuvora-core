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
parser.add_argument('--no-usb', action='store_true', help='Boot without virtual USB devices')
args = parser.parse_args()
env = os.environ.copy()
env['NV_ARCH'] = args.arch
build = root / 'build' / args.arch
if not (build / 'boot.elf').is_file():
    raise SystemExit(f'Missing kernel. Build it first: make ARCH={args.arch}')
subprocess.run([sys.executable, str(root / 'scripts/mkdisk.py'),
                str(build / 'nuvora-store.img'), '--if-missing'], check=True)
cmd = [sys.executable, str(root / 'scripts/run.py')]
if args.window:
    cmd.append('--window')
if args.no_usb:
    cmd.append('--no-usb')
raise SystemExit(subprocess.call(cmd, env=env))
