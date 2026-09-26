#!/usr/bin/env python3
"""Check media snapshot import, preservation, replacement and refusal cases."""
import pathlib
import tempfile
from import_media import import_files, geometry, parse, partition, snapshots
from mkgptdisk import create

root = pathlib.Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='nuvora-media-') as directory:
    disk = pathlib.Path(directory) / 'data.img'
    create(disk)
    tone = root / 'tests/fixtures/tone.mp3'
    clip = root / 'tests/fixtures/clip.mpg'
    document = pathlib.Path(directory) / 'notes.txt'
    document.write_text('ordinary data file\n')
    empty = pathlib.Path(directory) / 'empty.bin'
    empty.touch()
    large = pathlib.Path(directory) / 'sample.dat'
    large.write_bytes(b'\x5a' * (5 * 1024 * 1024))
    import_files(disk, [tone, clip, document, empty, large])
    with disk.open('rb') as stream:
        first, sectors = partition(stream)
        slot0, slot1, cap = geometry(stream, first, sectors)
        generation, active, payload = snapshots(stream, first, (slot0, slot1), cap)
        entries = parse(payload)
    assert generation == 1 and active == 0 and len(entries) == 5
    assert cap == 128 * 1024 * 1024
    assert entries[0] == (2, b'/home/tone.mp3', tone.read_bytes())
    assert entries[1] == (2, b'/home/clip.mpg', clip.read_bytes())
    assert entries[2] == (2, b'/home/notes.txt', document.read_bytes())
    assert entries[3] == (2, b'/home/empty.bin', b'')
    assert entries[4] == (2, b'/home/sample.dat', large.read_bytes())
    try:
        import_files(disk, [tone])
        raise AssertionError('duplicate media was accepted')
    except ValueError:
        pass
    import_files(disk, [tone], replace=True)
    with disk.open('rb') as stream:
        first, sectors = partition(stream)
        slot0, slot1, cap = geometry(stream, first, sectors)
        generation, active, payload = snapshots(stream, first, (slot0, slot1), cap)
    assert generation == 2 and active == 1 and len(parse(payload)) == 5
print('PASS file import: 128 MiB slots, arbitrary files, two-slot preservation and replacement')
