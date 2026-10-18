#!/usr/bin/env python3
"""Adversarial offline imports must preserve images and recover valid roots."""
import hashlib
import pathlib
import struct
import sys
import tempfile
import unittest
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from import_media import import_files, partition, geometry, snapshots, parse, pack
from mkgptdisk import create
from migrate_store import migrate
from extent_store import layout, roots, read_at


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').digest()


def edit_gpt(image, edit):
    with image.open('r+b') as stream:
        stream.seek(512); header = bytearray(stream.read(512))
        table_lba, = struct.unpack_from('<Q', header, 72)
        stream.seek(table_lba * 512); table = bytearray(stream.read(128 * 128))
        edit(header, table)
        struct.pack_into('<I', header, 88, zlib.crc32(table))
        struct.pack_into('<I', header, 16, 0)
        struct.pack_into('<I', header, 16, zlib.crc32(header[:92]))
        stream.seek(table_lba * 512); stream.write(table)
        stream.seek(512); stream.write(header)


def seed(stream, first, slot, generation, payload):
    header = bytearray(512)
    struct.pack_into('<8sIIII', header, 0, b'NVSS0001', generation,
                     len(payload), zlib.crc32(payload), 0)
    struct.pack_into('<I', header, 20, zlib.crc32(header))
    stream.seek((first + slot) * 512); stream.write(header + payload)


class OfflineStorage(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='nuvora-offline-boundaries-')
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.source = self.root / 'added.txt'; self.source.write_bytes(b'new file')

    def image(self, name='data', legacy=False, partitions=1):
        path = self.root / (name + '.img')
        create(path, size_mib=128 if partitions == 2 else 64, partitions=partitions, legacy=legacy)
        return path

    def refused_unchanged(self, image):
        before = digest(image); size = image.stat().st_size
        with self.assertRaises(ValueError):
            import_files(image, [self.source])
        self.assertEqual(digest(image), before)
        self.assertEqual(image.stat().st_size, size)
        destination = image.with_name(image.stem + '-refused.img')
        with self.assertRaises(ValueError):
            migrate(image, destination, size_mib=64)
        self.assertFalse(destination.exists())
        self.assertEqual(digest(image), before)

    def test_overlapping_partitions_refused(self):
        for kind in ('nuvora', 'foreign'):
            with self.subTest(kind=kind):
                image = self.image(kind, partitions=2)
                def overlap(header, table):
                    table[32:48] = table[160:176]
                    if kind == 'foreign': table[128:144] = bytes.fromhex('01000000000000000000000000000000')
                edit_gpt(image, overlap)
                self.refused_unchanged(image)

    def test_truncation_and_bad_gpt_geometry_refused(self):
        edits = {
            'revision': lambda h, t: struct.pack_into('<I', h, 8, 0),
            'reserved': lambda h, t: struct.pack_into('<I', h, 20, 1),
            'self-lba': lambda h, t: struct.pack_into('<Q', h, 24, 2),
            'alternate': lambda h, t: struct.pack_into('<Q', h, 32, 1),
            'table-in-data': lambda h, t: struct.pack_into('<Q', h, 72, 2048),
            'partition-in-gpt': lambda h, t: struct.pack_into('<Q', t, 32, 2),
            'partition-after-disk': lambda h, t: struct.pack_into('<Q', t, 40, 1 << 60),
        }
        for name, edit in edits.items():
            with self.subTest(name=name):
                image = self.image(name); edit_gpt(image, edit); self.refused_unchanged(image)
        image = self.image('truncated')
        with image.open('r+b') as stream: stream.truncate(16 * 1024 * 1024)
        self.refused_unchanged(image)

    def test_grown_image_and_single_sector_foreign_partition_remain_valid(self):
        image = self.image(partitions=2)
        def foreign(header, table):
            table[128:144] = bytes.fromhex('01000000000000000000000000000000')
            start, = struct.unpack_from('<Q', table, 160)
            struct.pack_into('<Q', table, 168, start)
        edit_gpt(image, foreign)
        with image.open('r+b') as stream: stream.truncate(132 * 1024 * 1024)
        import_files(image, [self.source])

    def test_legacy_geometry_matches_kernel(self):
        for name, offset, fmt, value in (('slot-zero', 16, '<I', 1), ('gap', 20, '<I', 40000),
                                         ('capacity', 32, '<Q', 1 << 60)):
            with self.subTest(name=name):
                image = self.image(name, legacy=True)
                with image.open('r+b') as stream:
                    first, _ = partition(stream); stream.seek(first * 512)
                    header = bytearray(stream.read(512)); struct.pack_into(fmt, header, offset, value)
                    struct.pack_into('<I', header, 40, zlib.crc32(header[:40]))
                    stream.seek(first * 512); stream.write(header)
                self.refused_unchanged(image)

    def test_invalid_newest_tree_does_not_destroy_recovery_root(self):
        malformed = {
            'duplicate': [(2, b'/home/keep.txt', b'a'), (2, b'/home/keep.txt', b'b')],
            'traversal': [(2, b'/home/../private', b'x')],
            'empty-name': [(2, b'/home/', b'x')],
            'missing-parent': [(2, b'/home/missing/child', b'x')],
            'file-parent': [(2, b'/home/file', b'x'), (2, b'/home/file/child', b'y')],
            'long-name': [(2, b'/home/' + b'x' * 32, b'x')],
            'empty-component': [(2, b'/home/a//child', b'x')],
        }
        for name, bad in malformed.items():
            with self.subTest(name=name):
                image = self.image(name, legacy=True)
                with image.open('r+b') as stream:
                    first, sectors = partition(stream); s0, s1, cap = geometry(stream, first, sectors)
                    valid = pack([(2, b'/home/keep.txt', b'recover me')])
                    seed(stream, first, s0, 0xffffffff, valid)
                    seed(stream, first, s1, 0, pack(bad))
                    stream.flush(); stream.seek((first + s0) * 512)
                    old_root = stream.read(cap + 512)
                    self.assertEqual(snapshots(stream, first, (s0, s1), cap), (0xffffffff, 0, valid))
                destination = self.root / (name + '-migrated.img')
                before = digest(image)
                self.assertEqual(migrate(image, destination, size_mib=64), 1)
                self.assertEqual(digest(image), before)
                with destination.open('rb') as stream:
                    df, ds = partition(stream)
                    entries = roots(stream, df, layout(stream, df, ds))[-1][2]
                    self.assertEqual(len(entries), 1)
                    kind, path, size, extents = entries[0]
                    self.assertEqual((kind, path, size), (2, b'/home/keep.txt', 10))
                    self.assertEqual(len(extents), 1)
                    self.assertEqual(read_at(stream, df * 512 + extents[0][1] * 4096, size), b'recover me')
                import_files(image, [self.source])
                with image.open('rb') as stream:
                    generation, active, payload = snapshots(stream, first, (s0, s1), cap)
                    self.assertEqual((generation, active), (0, 1))
                    self.assertEqual(parse(payload), [(2, b'/home/keep.txt', b'recover me'), (2, b'/home/added.txt', b'new file')])
                    stream.seek((first + s0) * 512); self.assertEqual(stream.read(cap + 512), old_root)

    def test_valid_nested_non_ascii_paths_survive_import_and_migration(self):
        image = self.image(legacy=True)
        folder = '/home/资料'.encode()
        expected = [(1, folder, b''), (2, folder + '/记录.txt'.encode(), b'valid nested file')]
        with image.open('r+b') as stream:
            first, sectors = partition(stream); s0, s1, cap = geometry(stream, first, sectors)
            seed(stream, first, s0, 3, pack(expected))
        import_files(image, [self.source])
        with image.open('rb') as stream:
            generation, active, payload = snapshots(stream, first, (s0, s1), cap)
            self.assertEqual((generation, active), (4, 1))
            self.assertEqual(parse(payload), expected + [(2, b'/home/added.txt', b'new file')])
        before = digest(image)
        self.assertEqual(migrate(image, self.root / 'unicode-migrated.img', size_mib=64), 3)
        self.assertEqual(digest(image), before)

    def test_both_structurally_invalid_roots_refused(self):
        image = self.image(legacy=True)
        with image.open('r+b') as stream:
            first, sectors = partition(stream); s0, s1, _ = geometry(stream, first, sectors)
            for slot in (s0, s1): seed(stream, first, slot, 1, pack([(2, b'/home/../invalid', b'x')]))
        self.refused_unchanged(image)


if __name__ == '__main__':
    unittest.main()
