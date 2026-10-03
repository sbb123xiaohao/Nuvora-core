#!/usr/bin/env python3
"""Check media snapshot import, preservation, replacement and refusal cases."""
import pathlib
import struct
import sys
import tempfile
import zlib
from import_media import import_files, geometry, pack, parse, partition, snapshots
from mkgptdisk import create

root = pathlib.Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='nuvora-media-') as directory:
    disk = pathlib.Path(directory) / 'data.img'
    create(disk, size_mib=512, legacy=True)
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


def seed_snapshot(stream, first, lba, generation, entries):
    payload = pack(entries)
    header = bytearray(512)
    struct.pack_into('<8sIIII', header, 0, b'NVSS0001', generation,
                     len(payload), zlib.crc32(payload), 0)
    struct.pack_into('<I', header, 20, zlib.crc32(header))
    stream.seek((first + lba) * 512)
    stream.write(header + payload)
    stream.flush()


def seeded_image(directory, generations, latest):
    image = directory / 'legacy.img'
    create(image, size_mib=64, legacy=True)
    expected = [(2, b'/home/version.txt', b'latest'),
                (2, b'/home/latest.txt', b'committed file')]
    with image.open('r+b') as stream:
        first, sectors = partition(stream)
        slot0, slot1, cap = geometry(stream, first, sectors)
        for slot, lba in enumerate((slot0, slot1)):
            entries = expected if slot == latest else [(2, b'/home/version.txt', b'old')]
            seed_snapshot(stream, first, lba, generations[slot], entries)
    return image, first, (slot0, slot1), cap, expected


def generation_selection(check_selection=True):
    from extent_store import layout, read_at, roots
    from migrate_store import migrate
    # Kernel generations are u32 serial numbers. Both physical slot orders
    # must select the same newest tree, before and after the counter wraps.
    for generations, latest in (((0xffffffff, 0), 1), ((0, 0xffffffff), 0),
                                ((10, 11), 1), ((11, 10), 0)):
        with tempfile.TemporaryDirectory(prefix='nuvora-generation-') as directory:
            directory = pathlib.Path(directory)
            image, first, slots, cap, expected = seeded_image(directory, generations, latest)
            with image.open('rb') as stream:
                generation, active, payload = snapshots(stream, first, slots, cap)
                source_slots = [read_at(stream, (first + lba) * 512, (cap + 512)) for lba in slots]
            if check_selection:
                assert (generation, active, parse(payload)) == (generations[latest], latest, expected)
            # Migration must copy that same committed tree without editing
            # either source slot, including a root at generation zero.
            destination = directory / 'migrated.img'
            assert migrate(image, destination, size_mib=64) == len(expected)
            with image.open('rb') as stream:
                assert source_slots == [read_at(stream, (first + lba) * 512, (cap + 512)) for lba in slots]
            with destination.open('rb') as stream:
                new_first, sectors = partition(stream)
                migrated = roots(stream, new_first, layout(stream, new_first, sectors))[-1][2]
                assert len(migrated) == len(expected)
                for item, (kind, path, body) in zip(migrated, expected):
                    assert item[:3] == (kind, path, len(body)) and len(item[3]) == 1
                    logical, physical, blocks = item[3][0]
                    assert logical == 0 and blocks == 1
                    assert read_at(stream, new_first * 512 + physical * 4096, len(body)) == body
    print('PASS legacy root selection and migration: generation wrap, both slot orders and ordinary generations')


def generation_commit():
    for active in (0, 1):
        with tempfile.TemporaryDirectory(prefix='nuvora-generation-commit-') as directory:
            directory = pathlib.Path(directory)
            generations = (0xffffffff, 0xfffffffe) if active == 0 else (0xfffffffe, 0xffffffff)
            image, first, slots, cap, expected = seeded_image(directory, generations, active)
            source = directory / 'added.txt'
            source.write_bytes(b'new import')
            assert import_files(image, [source])[0] == 1
            with image.open('rb') as stream:
                generation, current, payload = snapshots(stream, first, slots, cap)
            assert (generation, current) == (0, 1 - active)
            assert parse(payload) == expected + [(2, b'/home/added.txt', b'new import')]
            source.write_bytes(b'next import')
            assert import_files(image, [source], replace=True)[0] == 1
            with image.open('rb') as stream:
                generation, current, payload = snapshots(stream, first, slots, cap)
            assert (generation, current) == (1, active)
            assert parse(payload) == expected + [(2, b'/home/added.txt', b'next import')]
    print('PASS legacy commit: u32 generation rollover, latest files preserved and subsequent replacement')


mode = sys.argv[1] if len(sys.argv) == 2 else 'all'
if mode in ('all', 'generation-selection'):
    generation_selection()
if mode == 'generation-migration':
    generation_selection(check_selection=False)
if mode in ('all', 'generation-commit'):
    generation_commit()
