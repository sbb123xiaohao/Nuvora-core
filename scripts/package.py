#!/usr/bin/env python3
"""Package original sources, both verified builds, logs and GRUB source materials."""
import hashlib
import json
import pathlib
import sys
import zipfile

root = pathlib.Path(__file__).resolve().parent.parent
target = pathlib.Path(sys.argv[1]).resolve()
selected = []
for path in sorted(root.rglob('*')):
    if not path.is_file():
        continue
    rel = path.relative_to(root)
    if '__pycache__' in rel.parts or path.suffix == '.pyc':
        continue
    if rel.parts[0] == 'build':
        if len(rel.parts) < 3 or rel.parts[1] not in ['x86_64', 'i686']:
            continue
        tail = rel.parts[2:]
        keep = (len(tail) == 1 and (tail[0] in ['boot.elf', 'nuvora.elf', 'nuvora.map'] or tail[0] == f'nuvora-core-0.4.0-{rel.parts[1]}.iso'))
        keep |= len(tail) == 2 and tail[0] == 'apps' and path.suffix == '.elf'
        keep |= len(tail) == 2 and tail[0] == 'test-results' and path.suffix in ['.log', '.json', '.md', '.png']
        if not keep:
            continue
    selected.append(path)

for arch, assertions in [('x86_64', 104), ('i686', 100)]:
    build = root / 'build' / arch
    results = json.loads((build / 'test-results/results.json').read_text())
    assert len(results) == 28 and all(r['result'] == 'PASS' for r in results)
    for memory in [32, 64, 128, 256]:
        log = (build / f'test-results/probe-{memory}MiB.log').read_text()
        assert f'PROBE RESULT: {assertions} passed, 0 failed' in log
    assert (build / f'nuvora-core-0.4.0-{arch}.iso').is_file()
    fingerprint = json.loads((build / 'test-results/build-fingerprint.json').read_text())
    for name, digest in fingerprint.items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest, name + ': changed after testing'

manifest = []
prefix = 'nuvora-core-0.4.0/'
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
