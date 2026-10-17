#!/usr/bin/env python3
"""Verify dynamic ELF provenance, native reference behavior and tree imports."""
import hashlib
import json
import pathlib
import sys
import struct
import subprocess
import tempfile
from unittest import mock
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'scripts'),str(ROOT/'compat/tools')]
import extent_store
from import_media import partition
from mkgptdisk import create
from prepare_openjdk_probe import patch_interpreter


def state(disk):
    with disk.open('rb') as f:
        first,sectors=partition(f);g=extent_store.layout(f,first,sectors)
        return extent_store.roots(f,first,g)


def main():
    fixtures=ROOT/'compat/fixtures/dynamic';manifest=json.loads((fixtures/'manifest.json').read_text())
    for name,wanted in manifest['files'].items():assert hashlib.sha256((fixtures/name).read_bytes()).hexdigest()==wanted,name
    for name,wanted in manifest['wrappers'].items():assert hashlib.sha256((ROOT/'compat/examples'/name).read_bytes()).hexdigest()==wanted,name
    java=json.loads((ROOT/'compat/examples/java/runtime-manifest.json').read_text())
    for name,wanted in java['files'].items():assert hashlib.sha256((ROOT/'compat/examples/java'/name).read_bytes()).hexdigest()==wanted,name
    assert (ROOT/'compat/examples/java/JavaRuntimeCheck.class').read_bytes()[:8]==bytes.fromhex('cafebabe00000041')
    for arguments in ([str(fixtures/'libnvprobe.so')],['--signal-check',str(fixtures/'libnvprobe.so')]):
        run=subprocess.run([str(fixtures/'ldso'),str(fixtures/'dynamic.elf'),*arguments],capture_output=True,timeout=20,check=True)
        assert b'DYNAMIC PASS' in run.stdout and not run.stderr
        if arguments[0]=='--signal-check':assert b'SIGNAL PASS' in run.stdout
    image=bytearray(256);image[:7]=b'\x7fELF\x02\x01\x01';struct.pack_into('<Q',image,32,64);struct.pack_into('<HH',image,54,56,1)
    old=b'/lib/ld-musl-x86_64.so.1\0';struct.pack_into('<IIQQQQQQ',image,64,3,4,192,192,192,len(old),len(old),1);image[192:192+len(old)]=old
    changed=patch_interpreter(image);assert len(changed)==len(image) and changed[192:203]==b'/home/ldso\0'
    allowed=set(range(96,112))|set(range(192,192+len(old)))
    assert all(i in allowed for i,(a,b) in enumerate(zip(image,changed)) if a!=b)
    for bad in (b'',image[:63],bytes(256)):
        try:patch_interpreter(bad)
        except ValueError:pass
        else:raise AssertionError('Malformed launcher accepted')
    with tempfile.TemporaryDirectory(prefix='nuvora-tree-') as directory:
        root=pathlib.Path(directory);source=root/'runtime';(source/'lib/server').mkdir(parents=True);(source/'bin').mkdir();(source/'empty').mkdir()
        (source/'bin/java').write_bytes(b'launcher');(source/'lib/server/libjvm.so').write_bytes(b'library'*1000)
        disk=root/'data.img';create(disk,size_mib=64,partitions=1)
        def install(replace=False):
            with disk.open('r+b') as f:
                first,sectors=partition(f);return extent_store.import_tree(f,first,sectors,source,'/home/jdk',replace)
        install();before=state(disk);entries={item[1]:item for item in before[-1][2]}
        assert entries[b'/home/jdk/empty'][0]==1 and entries[b'/home/jdk/bin/java'][2]==8
        first_generation=before[-1][0]
        for bad in ('collision','long-name','symlink'):
            extra=source/('x'*32 if bad=='long-name' else 'link')
            if bad=='long-name':extra.write_bytes(b'x')
            elif bad=='symlink':extra.symlink_to(source/'bin/java')
            original=hashlib.sha256(disk.read_bytes()).digest()
            try:install(replace=bad!='collision')
            except ValueError:pass
            else:raise AssertionError('Unsafe tree accepted: '+bad)
            assert hashlib.sha256(disk.read_bytes()).digest()==original
            if extra.exists() or extra.is_symlink():extra.unlink()
        (source/'bin/java').write_bytes(b'new launcher')
        real_write=extent_store.write_source
        def failed(*args,**kwargs):
            real_write(*args,**kwargs);raise OSError('Injected source read error after data write')
        with mock.patch.object(extent_store,'write_source',failed):
            try:install(True)
            except OSError:pass
            else:raise AssertionError('Failed import committed')
        assert state(disk)==before
        install(True);after=state(disk)
        assert after[-1][0]==first_generation+1 and after[-2]==before[-1]
    print('PASS dynamic tools: hashes, real musl/TLS/signals on Linux reference, bounded ELF interpreter relocation, nested COW imports and failure preservation')


if __name__=='__main__':main()
