"""NVSTORE3 offline import/export. Only use with a powered-off data image.

File contents are streamed; both committed roots remain allocated until the
new metadata root is durable. No operation overwrites the volume geometry.
"""
import errno
import os
import pathlib
import struct
import zlib

BLOCK = 4096
MAX_FILE = 0x7ffffffffffff000
MAX_ENTRIES = 480  # leaves space for boot apps, devices and mount roots


def read_at(f, offset, size):
    f.seek(offset)
    data = f.read(size)
    if len(data) != size:
        raise ValueError('Truncated data image')
    return data


def layout(f, first, sectors):
    h = read_at(f, first * 512, 512)
    fields = struct.unpack_from('<IIIII', h, 8)
    total, = struct.unpack_from('<Q', h, 32)
    if (h[:8] != b'NVSTORE3' or fields[:3] != (3, 512, 8)
            or zlib.crc32(h[:40]) != struct.unpack_from('<I', h, 40)[0]):
        raise ValueError('Invalid NVSTORE3 geometry')
    _, _, slot0, slot1, length = fields
    if (not 8 <= length <= 128*1024*1024//512+1 or slot1 != slot0+length
            or total > sectors or total < slot1+length):
        raise ValueError('Metadata slots outside partition')
    start, end = (slot1 + length + 7)//8, total//8
    if start >= end:
        raise ValueError('No file data region')
    return (slot0, slot1), (length-1)*512, start, end


def validate(entries, start, end):
    seen = {b'/home': 1}
    used = []
    if len(entries) > MAX_ENTRIES:
        raise ValueError('Directory table is full')
    for kind, path, size, extents in entries:
        if (kind not in (1, 2) or not 0 <= size <= MAX_FILE or len(path) >= 192
                or not path.startswith(b'/home/') or b'\0' in path or path in seen
                or (kind == 1 and (size or extents))):
            raise ValueError('Invalid file metadata')
        if any(not 1 <= len(p) <= 31 or p in (b'.', b'..') for p in path.split(b'/')[2:]):
            raise ValueError('Invalid file path')
        if seen.get(path.rsplit(b'/', 1)[0]) != 1:
            raise ValueError('Parent directory must precede its children')
        seen[path] = kind
        previous = 0
        for logical, physical, blocks in extents:
            if (not blocks or logical < previous or logical+blocks > (size+BLOCK-1)//BLOCK
                    or physical < start or physical+blocks > end):
                raise ValueError('File extent outside valid range')
            previous = logical+blocks
            used.append((physical, physical+blocks))
    used.sort()
    if any(a[1] > b[0] for a, b in zip(used, used[1:])):
        raise ValueError('Files share physical blocks')


def decode(payload, start, end):
    if len(payload) < 4:
        raise ValueError('Truncated metadata')
    count, = struct.unpack_from('<I', payload)
    if count > MAX_ENTRIES:
        raise ValueError('Directory table is full')
    entries, pos = [], 4
    for _ in range(count):
        if len(payload)-pos < 24:
            raise ValueError('Truncated entry')
        kind, n, size, count_ext, reserved = struct.unpack_from('<IIQII', payload, pos)
        pos += 24
        if reserved or n >= 192 or n + count_ext*24 > len(payload)-pos:
            raise ValueError('Truncated path or extent map')
        path = bytes(payload[pos:pos+n]); pos += n
        extents = [struct.unpack_from('<QQQ', payload, pos+i*24) for i in range(count_ext)]
        pos += count_ext*24
        entries.append((kind, path, size, extents))
    if pos != len(payload):
        raise ValueError('Trailing metadata')
    validate(entries, start, end)
    return entries


def encode(entries):
    out = bytearray(struct.pack('<I', len(entries)))
    for kind, path, size, extents in entries:
        out += struct.pack('<IIQII', kind, len(path), size, len(extents), 0)+path
        for e in extents:
            out += struct.pack('<QQQ', *e)
    return out


def roots(f, first, geometry):
    slots, cap, start, end = geometry
    valid, blank = [], True
    for slot, lba in enumerate(slots):
        h = bytearray(read_at(f, (first+lba)*512, 512))
        blank &= h == bytes(512)
        generation, length, crc, hc = struct.unpack_from('<IIII', h, 8)
        struct.pack_into('<I', h, 20, 0)
        if h[:8] != b'NVSS0002' or not 4 <= length <= cap or zlib.crc32(h) != hc:
            continue
        payload = read_at(f, (first+lba+1)*512, length)
        if zlib.crc32(payload) != crc:
            continue
        try:
            entries = decode(payload, start, end)
        except ValueError:
            continue
        valid.append((generation, slot, entries))
    if not valid and not blank:
        raise ValueError('No valid metadata root; refusing to discard existing files')
    if len(valid) == 2 and 0 < ((valid[0][0]-valid[1][0]) & 0xffffffff) < 0x80000000:
        valid.reverse()
    return valid


def free_ranges(used, start, end):
    result, at = [], start
    for lo, hi in sorted(used):
        if lo > at: result.append([at, lo])
        at = max(at, hi)
    if at < end: result.append([at, end])
    return result


def source_ranges(f, size):
    """Preserve host sparse holes when SEEK_DATA/SEEK_HOLE is available."""
    if not size: return
    if not hasattr(os, 'SEEK_DATA'):
        yield 0, size; return
    at = 0
    while at < size:
        try:
            lo = os.lseek(f.fileno(), at, os.SEEK_DATA)
            hi = min(size, os.lseek(f.fileno(), lo, os.SEEK_HOLE))
        except OSError as error:
            if error.errno == errno.ENXIO: return
            if error.errno in (errno.EINVAL, errno.ENOTSUP):
                yield at, size; return
            raise
        yield lo, hi
        at = hi


def write_source(out, first, source, size, free):
    extents = []
    with source.open('rb', buffering=0) as inp:
        last = 0
        for lo, hi in source_ranges(inp, size):
            logical = max(last, lo//BLOCK)
            stop = (hi+BLOCK-1)//BLOCK
            while logical < stop:
                while free and free[0][0] == free[0][1]: free.pop(0)
                if not free: raise ValueError('Insufficient disk space for copy-on-write import')
                physical = free[0][0]
                blocks = min(stop-logical, free[0][1]-physical)
                free[0][0] += blocks
                inp.seek(logical*BLOCK)
                out.seek(first*512+physical*BLOCK)
                todo = blocks*BLOCK
                while todo:
                    chunk = min(todo, 1024*1024)
                    actual = min(chunk, size-inp.tell())
                    data = inp.read(max(0, actual))
                    if len(data) != max(0, actual): raise ValueError('Source changed during import')
                    out.write(data+bytes(chunk-len(data)))
                    todo -= chunk
                if extents and extents[-1][0]+extents[-1][2] == logical and extents[-1][1]+extents[-1][2] == physical:
                    old = extents[-1]; extents[-1] = (old[0], old[1], old[2]+blocks)
                else: extents.append((logical, physical, blocks))
                logical += blocks
            last = stop
    return extents


def commit(f, first, geometry, generation, active, entries):
    slots, cap, start, end = geometry
    validate(entries, start, end)
    payload = encode(entries)
    if len(payload) > cap: raise ValueError('Extent metadata area is full')
    slot = 1 if active == 0 else 0
    offset = (first+slots[slot])*512
    f.seek(offset); f.write(bytes(512)); f.flush(); os.fsync(f.fileno())
    f.seek(offset+512); f.write(payload); f.write(bytes((-len(payload)) % 512))
    f.flush(); os.fsync(f.fileno())  # file data must precede the root commit
    h = bytearray(512)
    struct.pack_into('<8sIIII', h, 0, b'NVSS0002', (generation+1)&0xffffffff, len(payload), zlib.crc32(payload), 0)
    struct.pack_into('<I', h, 20, zlib.crc32(h))
    f.seek(offset); f.write(h); f.flush(); os.fsync(f.fileno())
    return len(payload)


def import_extents(f, first, sectors, paths, replace=False):
    geometry = layout(f, first, sectors)
    available = roots(f, first, geometry)
    generation, active, entries = available[-1] if available else (0, -1, [])
    entries = list(entries)
    free = free_ranges([(physical, physical+blocks)
                        for _, _, files in available for _, _, _, ext in files
                        for _, physical, blocks in ext], geometry[2], geometry[3])
    # Validate every request before any disk writes.
    sources = []
    names = {p: i for i, (_, p, _, _) in enumerate(entries)}
    for source in paths:
        source = pathlib.Path(source)
        if not source.is_file(): raise ValueError(f'Expected a regular file: {source}')
        if os.path.samestat(source.stat(), os.fstat(f.fileno())): raise ValueError('Cannot import the data image into itself')
        name = source.name.encode('ascii')
        if not 1 <= len(name) <= 31 or name in (b'.', b'..'): raise ValueError('Filename exceeds 31 ASCII bytes')
        key = b'/home/'+name
        if key in names and (not replace or entries[names[key]][0] != 2):
            raise ValueError(f'C: already contains {source.name}; use --replace for files')
        if any(p == key for _, p, _ in sources): raise ValueError('Duplicate source filename')
        size = source.stat().st_size
        if size > MAX_FILE: raise ValueError('File exceeds 64-bit filesystem range')
        sources.append((source, key, size))
    if len(entries)+sum(key not in names for _, key, _ in sources) > MAX_ENTRIES:
        raise ValueError('Directory table is full')
    for source, key, size in sources:
        stamp = source.stat()
        extents = write_source(f, first, source, size, free)
        now = source.stat()
        if (stamp.st_size, stamp.st_mtime_ns, stamp.st_ino) != (now.st_size, now.st_mtime_ns, now.st_ino):
            raise ValueError('Source changed during import')
        item = (2, key, size, extents)
        if key in names: entries[names[key]] = item
        else: entries.append(item)
    return len(paths), commit(f, first, geometry, generation, active, entries)
