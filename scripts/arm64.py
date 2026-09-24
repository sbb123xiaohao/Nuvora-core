#!/usr/bin/env python3
"""Boot the Nuvora ARM64 bring-up image on QEMU virt and check its RAM/NEON tests."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
IMAGE = ROOT / 'build/aarch64/Image'
REPORT = ROOT / 'build/aarch64/test-results'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('action', choices=['run', 'test'])
parser.add_argument('--memory', type=int, default=256, help='QEMU RAM in MiB')
args = parser.parse_args()
if args.memory < 32:
    parser.error('at least 32 MiB is required')
if not IMAGE.is_file():
    parser.error('build/aarch64/Image missing: run make ARCH=aarch64')
binary = os.environ.get('NV_QEMU_ARM64') or shutil.which('qemu-system-aarch64')
if not binary:
    parser.error('qemu-system-aarch64 missing')


def command(memory):
    cmd = [binary, '-machine', 'virt,gic-version=3', '-cpu', 'cortex-a57',
           '-accel', 'tcg', '-m', str(memory), '-smp', '1', '-nic', 'none',
           '-kernel', str(IMAGE), '-nographic', '-monitor', 'none', '-no-reboot']
    if os.environ.get('NV_QEMU_DATA'):
        cmd.extend(['-L', os.environ['NV_QEMU_DATA']])
    return cmd


if args.action == 'run':
    raise SystemExit(subprocess.call(command(args.memory)))

REPORT.mkdir(parents=True, exist_ok=True)
results = []
for memory in (64, 256, 1024, 5120):
    completed = subprocess.run(command(memory), stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=45, check=False)
    output = completed.stdout.decode('utf-8', errors='replace').replace('\r', '')
    (REPORT / f'arm64-{memory}MiB.log').write_text(output)
    totals = re.search(r'managed RAM pages: (\d+); free: (\d+)', output)
    assert completed.returncode == 0 and 'ARM64 RESULT: 15 passed, 0 failed' in output, output
    assert totals and int(totals[1]) >= memory * 256 - 1024, output
    assert '[ok] EL1 4 KiB tables, kernel RO/NX, device MMIO' in output, output
    assert '[ok] 1 MiB compute buffer reclaimed; NEON dot product = 70' in output, output
    assert '[ok] EL0 NEON workload completed via SVC' in output, output
    assert '[ok] EL0 cannot read supervisor text' in output, output
    results.append({'test': f'ARM64 QEMU virt / {memory} MiB', 'result': 'PASS',
                    'detail': '15 guest checks, buddy and slab, EL0 NEON via SVC, supervisor isolation'})
    print(f'PASS ARM64 QEMU virt / {memory} MiB: boot, FDT, paging, EL0 NEON and isolation')
(REPORT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
(REPORT / 'execution.json').write_text(json.dumps({
    'arch': 'aarch64', 'version': '0.9.0', 'completed': True, 'checks': len(results),
    'image_sha256': hashlib.sha256(IMAGE.read_bytes()).hexdigest()}, indent=2) + '\n')
