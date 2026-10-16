#!/usr/bin/env python3
"""Check fixture provenance and run the same real engines on Linux as reference."""
import hashlib
import json
import pathlib
import struct
import subprocess
import sys
import tempfile
ROOT=pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'scripts'))
from runtime_probe import inspect


def main():
    fixtures=ROOT/'tests/fixtures/compat';manifest=json.loads((fixtures/'manifest.json').read_text())
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    for name,wanted in manifest['elfs'].items():
        path=fixtures/name;assert digest(path)==wanted,name
        info=inspect(path);assert not info['interpreter'] and not info['needed'] and not info['loader_blockers'],info
        if name=='runtime_check.elf':assert info['tls_template']
    for name,wanted in manifest['wrappers'].items():assert digest(ROOT/'examples/compat'/name)==wanted,name
    with tempfile.TemporaryDirectory(prefix='nuvora-runtime-reference-') as directory:
        for name,source,marker in [('language',None,'C++ CORE PASS'),('lua','world.lua','LUA RUNTIME PASS'),('js','world.js','JAVASCRIPT RUNTIME PASS')]:
            command=[str(fixtures/(name+'.elf'))]
            if source:command+=[str(ROOT/'examples/compat'/source),'hello world']
            run=subprocess.run(command,cwd=directory,capture_output=True,check=True,timeout=20)
            assert marker.encode() in run.stdout and not run.stderr
            print(f'PASS same static ELF on Linux reference: {name}, {len(run.stdout)} stdout bytes')
        path=pathlib.Path(directory)/'broken.elf';valid=(fixtures/'runtime_check.elf').read_bytes()
        for n in (0,4,63):
            path.write_bytes(valid[:n])
            try:inspect(path)
            except ValueError:pass
            else:raise AssertionError('Truncated probe accepted')
        malformed=bytearray(valid);struct.pack_into('<Q',malformed,32,0xffffffffffffffff);path.write_bytes(malformed)
        try:inspect(path)
        except ValueError:pass
        else:raise AssertionError('Bad PHDR bounds accepted')
    print('PASS runtime fixture hashes, source hashes, static PIE/TLS metadata and malformed probe inputs')

if __name__=='__main__':main()
