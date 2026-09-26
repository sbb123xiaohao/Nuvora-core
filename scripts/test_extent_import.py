#!/usr/bin/env python3
"""Offline NVSTORE3 and migration round trips, including >4 GiB sparse files."""
import hashlib
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
from extent_store import layout, roots, read_at, decode, encode
from import_media import import_files, partition
from mkgptdisk import create
from migrate_store import migrate
from prepare_media import prepare


def files(image):
    with image.open('rb') as f:
        first, sectors = partition(f); g = layout(f, first, sectors)
        result = roots(f, first, g)
        return first, g, result


def contents(image, first, entry, at, length):
    _, _, size, extents = entry
    assert at+length <= size
    out = bytearray(length)
    with image.open('rb') as f:
        for logical, physical, blocks in extents:
            lo, hi = max(at, logical*4096), min(at+length, (logical+blocks)*4096)
            if hi > lo:
                out[lo-at:hi-at] = read_at(f, first*512+physical*4096+lo-logical*4096, hi-lo)
    return bytes(out)

root = pathlib.Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='nuvora-extents-') as directory:
    d = pathlib.Path(directory); disk = d/'store.img'; create(disk, size_mib=32768)
    huge = d/'huge.bin'; offset = 6*1024**3+4093
    with huge.open('wb') as f:
        f.seek(offset); f.write(b'cross-4g-boundary')
    ordinary = d/'document.txt'; ordinary.write_bytes(b'first')
    import_files(disk, [huge, ordinary])
    first, g, r = files(disk); entries = r[-1][2]
    assert entries[0][2] == offset+17 and sum(e[2] for e in entries[0][3])*4096 < 1024*1024
    assert contents(disk, first, entries[0], offset-3, 20) == b'\0\0\0cross-4g-boundary'
    assert len(encode(entries)) < 1024
    # A real, non-sparse 140 MiB payload exceeds the old entire-volume limit.
    big = d/'large.dat'
    with big.open('wb') as f:
        for _ in range(140): f.write(b'Z'*1024*1024)
    import_files(disk, [big]); first, g, r = files(disk)
    assert contents(disk, first, r[-1][2][-1], 140*1024*1024-4, 4) == b'ZZZZ'
    if len(sys.argv) > 1:
        subprocess.run([sys.argv[1], str(disk)], check=True)
        first,g,r=files(disk)
        assert contents(disk,first,r[-1][2][0],10,13)==b'kernel-edited'
    old = r[-1][2][1]
    ordinary.write_bytes(b'second'); import_files(disk, [ordinary], replace=True)
    first, g, r = files(disk)
    assert contents(disk, first, old, 0, 5) == b'first'
    assert contents(disk, first, r[-1][2][1], 0, 6) == b'second'
    before = r[-1][0]
    for bad in ([ordinary], [disk]):
        try: import_files(disk, bad); raise AssertionError('invalid import accepted')
        except ValueError: pass
    assert files(disk)[2][-1][0] == before
    # Metadata corruption falls back without accepting partially written data.
    slot = r[-1][1]
    with disk.open('r+b') as f:
        f.seek((first+g[0][slot]+1)*512); f.write(b'bad!')
    assert files(disk)[2][-1][0] == before-1
    malformed = [(2,b'/home/a',4096,[(0,g[2],1)]),(2,b'/home/b',4096,[(0,g[2],1)])]
    try: decode(encode(malformed),g[2],g[3]);raise AssertionError('overlap accepted')
    except ValueError: pass
    olddisk=d/'old.img'; create(olddisk, size_mib=512, legacy=True)
    import_files(olddisk, [ordinary, root/'tests/fixtures/tone.mp3'])
    with olddisk.open('rb') as f: oldhash=hashlib.file_digest(f,'sha256').hexdigest()
    newdisk=d/'new.img'; assert migrate(olddisk,newdisk,8192)==2
    with olddisk.open('rb') as f: assert hashlib.file_digest(f,'sha256').hexdigest()==oldhash
    first,g,r=files(newdisk); assert contents(newdisk,first,r[-1][2][0],0,6)==b'second'
    if shutil.which('ffmpeg') and shutil.which('ffprobe'):
        mp4=d/'source.mp4'
        subprocess.run(['ffmpeg','-nostdin','-v','error','-i',str(root/'tests/fixtures/clip.mpg'),'-c:v','libx264','-c:a','aac',str(mp4)],check=True)
        converted=prepare(mp4,d/'converted.mpg')
        import_files(newdisk,[converted]); assert files(newdisk)[2][-1][2][-1][1]==b'/home/converted.mpg'
        prepare(root/'tests/fixtures/tone.mp3',d/'converted.flac',audio_only=True)
    else: print('SKIP external media conversion: FFmpeg/ffprobe not installed')
print('PASS extent importer: 6 GiB sparse file, 140 MiB data, COW preservation, corrupt-root fallback, legacy migration, media conversion')
