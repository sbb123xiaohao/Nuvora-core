#!/usr/bin/env python3
"""Package original sources, verified x64 and ARM64 builds and execution logs."""
import hashlib
import json
import pathlib
import sys
import zipfile

root = pathlib.Path(__file__).resolve().parent.parent
target = pathlib.Path(sys.argv[1]).resolve()
VERSION = '0.9.0'
selected = []
for path in sorted(root.rglob('*')):
    if not path.is_file():
        continue
    rel = path.relative_to(root)
    if rel.as_posix() == 'SHA256SUMS' or '__pycache__' in rel.parts or path.suffix == '.pyc':
        continue
    if rel.parts[0] == 'build':
        if len(rel.parts) < 3 or rel.parts[1] not in ['x86_64', 'aarch64']:
            continue
        tail = rel.parts[2:]
        keep = (len(tail) == 1 and (tail[0] in ['boot.elf', 'nuvora.elf', 'nuvora.map'] or
                (rel.parts[1] == 'aarch64' and tail[0] == 'Image') or
                (rel.parts[1] == 'x86_64' and tail[0] == f'nuvora-core-{VERSION}-x86_64.iso')))
        keep |= len(tail) == 1 and rel.parts[1] == 'x86_64' and tail[0] in ['BOOTX64.EFI', 'esp.img', f'nuvora-core-{VERSION}-x86_64-uefi.iso']
        keep |= len(tail) == 2 and tail[0] == 'apps' and path.suffix == '.elf'
        keep |= len(tail) == 2 and tail[0] == 'test-results' and path.suffix in ['.log', '.json', '.md', '.png']
        if not keep:
            continue
    selected.append(path)

for arch, assertions in [('x86_64', 137)]:
    build = root / 'build' / arch
    results = json.loads((build / 'test-results/results.json').read_text())
    execution = json.loads((build / 'test-results/execution.json').read_text())
    assert execution == {'arch': arch, 'version': VERSION, 'phase': 'all', 'iso': True,
                         'completed': True, 'checks': len(results)}, arch + ': incomplete test run'
    assert results and all(r['result'] == 'PASS' for r in results)
    required = {'GRUB BIOS ISO boot', 'GPT dual partition round-trip', 'Large data image / 4 TiB', 'USB hotplug and command ring wrap',
                'Restored file growth / 32 MiB'}
    if arch == 'x86_64':
        required.update({'UEFI stub boot', 'UEFI ISO boot', 'Fragmented kernel heap / 32 MiB'})
    assert required <= {r['test'] for r in results}, arch + ': missing required execution checks'
    probe_memories = [32, 64, 128, 256] + ([1024, 5120] if arch == 'x86_64' else [])
    for memory in probe_memories:
        log = (build / f'test-results/probe-{memory}MiB.log').read_text()
        assert f'PROBE RESULT: {assertions} passed, 0 failed' in log
    assert (build / f'nuvora-core-{VERSION}-{arch}.iso').is_file()
    if arch == 'x86_64':
        assert (build / 'BOOTX64.EFI').is_file() and (build / 'esp.img').is_file()
        assert (build / f'nuvora-core-{VERSION}-x86_64-uefi.iso').is_file()
    fingerprint = json.loads((build / 'test-results/build-fingerprint.json').read_text())
    for name, digest in fingerprint.items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest, name + ': changed after testing'

arm = root / 'build/aarch64'
arm_run = json.loads((arm / 'test-results/execution.json').read_text())
assert arm_run['arch'] == 'aarch64' and arm_run['version'] == VERSION
assert arm_run['completed'] and arm_run['checks'] == 4
assert arm_run['image_sha256'] == hashlib.sha256((arm / 'Image').read_bytes()).hexdigest()
arm_results = json.loads((arm / 'test-results/results.json').read_text())
assert len(arm_results) == 4 and all(row['result'] == 'PASS' for row in arm_results)
for memory in (64, 256, 1024, 5120):
    log = (arm / f'test-results/arm64-{memory}MiB.log').read_text()
    assert 'ARM64 RESULT: 15 passed, 0 failed' in log

manifest = []
prefix = f'nuvora-core-{VERSION}/'
with zipfile.ZipFile(target, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for path in selected:
        name = path.relative_to(root).as_posix()
        data = path.read_bytes()
        manifest.append(hashlib.sha256(data).hexdigest() + '  ' + name)
        archive.write(path, prefix + name)
    archive.writestr(prefix + 'SHA256SUMS', '\n'.join(manifest) + '\n')

with zipfile.ZipFile(target) as archive:
    assert archive.testzip() is None
    for line in manifest:
        digest, name = line.split('  ', 1)
        assert hashlib.sha256(archive.read(prefix + name)).hexdigest() == digest
print(f'{len(selected)} files; {target.stat().st_size} bytes')
print('ZIP CRC and every SHA-256 entry verified')
print(hashlib.sha256(target.read_bytes()).hexdigest())
