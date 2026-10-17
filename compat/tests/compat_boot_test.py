#!/usr/bin/env python3
"""Run real musl C/C++, Lua, QuickJS and MicroPython in private guests."""
import pathlib
import sys
import re
import struct
import tempfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'scripts'),str(ROOT/'compat/tools')]
from test import VM
from mkgptdisk import create
from import_media import import_files


def main():
    with tempfile.TemporaryDirectory(prefix='nuvora-compat-') as directory:
        private=pathlib.Path(directory);disk=private/'data.img'
        create(disk,size_mib=128,partitions=1)
        fixtures=ROOT/'compat/fixtures/static'
        bad=private/'Bad.elf';bad.write_bytes((fixtures/'runtime_check.elf').read_bytes()[:100])
        data=bytearray((fixtures/'runtime_check.elf').read_bytes());at=struct.unpack_from('<Q',data,32)[0]
        count=struct.unpack_from('<H',data,56)[0]
        for i in range(count):
            if struct.unpack_from('<I',data,at+i*56)[0]==7:
                struct.pack_into('<I',data,at+i*56,3);break
        else:raise AssertionError('C fixture must contain PT_TLS')
        dynamic=private/'Dynamic.elf';dynamic.write_bytes(data)
        loop=private/'loop.js';loop.write_text('for (;;) {}\n')
        js_error=private/'error.js';js_error.write_text('throw new Error("expected JavaScript error");\n')
        lua_error=private/'error.lua';lua_error.write_text('error("expected Lua error")\n')
        py_error=private/'error.py';py_error.write_text('raise ValueError("expected Python error")\n')
        py_syntax=private/'syntax.py';py_syntax.write_text('def invalid(:\n')
        py_limit=private/'limit.py';py_limit.write_text('try:\n    blob = bytearray(8 * 1024 * 1024)\nexcept MemoryError:\n    print("PYTHON MEMORY LIMIT PASS")\nelse:\n    raise AssertionError("heap limit ignored")\n')
        scripts=ROOT/'compat/examples'
        import_files(disk,[*sorted(fixtures.glob('*.elf')),*[scripts/name for name in ('world.lua','world.js','world.py','terrain.py')],
                           bad,dynamic,loop,js_error,lua_error,py_error,py_syntax,py_limit])
        vm=VM('linux-runtime',disk=disk,memory=64,boot_timeout=30)
        try:
            c=vm.send('compat /home/runtime_check.elf "two words" ""','LINUX ABI C PASS',timeout=30)
            assert 'args=2, first=two words, empty=yes' in c
            assert 'FAIL line' not in c
            print('PASS real musl C: TLS, allocation, mmap split/protection, errno, bounds, clocks, descriptors and file I/O',flush=True)
            cpp=vm.send('compat /home/language.elf','C++ CORE PASS',timeout=30)
            assert 'sum=30' in cpp
            for name,marker in [('lua','LUA RUNTIME PASS'),('js','JAVASCRIPT RUNTIME PASS')]:
                extension='lua' if name=='lua' else 'js'
                out=vm.send(f'compat /home/{name}.elf /home/world.{extension} "hello world"',marker,timeout=30)
                assert 'checksum=8064' in out and 'arg=hello world' in out
                print(f'PASS real {name} engine: parser, objects, GC, exceptions and floating point',flush=True)
            out=vm.send('compat /home/python.elf /home/world.py "hello world"','PYTHON RUNTIME PASS',timeout=30)
            assert 'checksum=8064' in out and 'bigint=1208925819614629174706176' in out and 'arg=hello world' in out
            vm.send('compat /home/python.elf /home/limit.py','PYTHON MEMORY LIMIT PASS',timeout=30)
            print('PASS real MicroPython: module import, inheritance, generators, Unicode, big integers, math, JSON/regex, GC, files and bounded heap',flush=True)
            # compat replaces its own PID, so the shell owns the actual runtime.
            pids=[]
            for _ in range(2):
                out=vm.send('scatter compat /home/runtime_check.elf stress','C:/ :: ',timeout=20)
                pid=re.search(r'Started pid (\d+)',out)
                if not pid:pid=re.search(r'PID (\d+)',out)
                assert pid,out;pids.append(int(pid[1]))
            for pid in pids:vm.send(f'gather {pid}','C:/ :: ',timeout=30)
            assert vm.text().count('TLS CONTEXT PASS')==2
            print('PASS Linux/native scheduling: two concurrent TLS processes retain independent FS bases and waitable PIDs',flush=True)
            for command,marker in [('compat /home/js.elf /home/error.js','expected JavaScript error'),
                                   ('compat /home/lua.elf /home/error.lua','expected Lua error'),
                                   ('compat /home/python.elf /home/error.py','expected Python error'),
                                   ('compat /home/python.elf /home/syntax.py','SyntaxError')]:
                out=vm.send(command,'C:/ :: ',timeout=30)
                assert marker in out and 'exited 1' in out
            out=vm.send('scatter compat /home/js.elf /home/loop.js','C:/ :: ',timeout=20)
            pid=int(re.search(r'Started pid (\d+)',out)[1])
            vm.send('doze 30','C:/ :: ',timeout=20)
            assert 'js.elf' in vm.send('sparks','C:/ :: ',timeout=20)
            vm.send(f'quench {pid}','C:/ :: ',timeout=20)
            assert 'Exit status: 143' in vm.send(f'gather {pid}','C:/ :: ',timeout=20)
            print('PASS language exceptions and shell cancellation of the actual compatibility process',flush=True)
            for path in ('Bad.elf','Dynamic.elf'):
                out=vm.send(f'compat /home/{path}','Linux compatibility launch:',timeout=20)
                assert '[isolate' not in out
            out=vm.send('compat /home/runtime_check.elf fault','C:/ :: ',timeout=20)
            assert '[isolate x64]' in out and 'trap=14' in out
            vm.send('java --self-test','JAVA21 GUEST PASS',timeout=30)
            vm.send('forge pie --self-test','relative pointers, BSS and RELRO verified',timeout=30)
            # Cache all files first. Repeated successful exec/exit must reclaim
            # images, 256 KiB stacks, page tables, heaps, mappings and runtime GC.
            def free():
                out=vm.send('horizon','filesystem nodes:',timeout=15)
                return int(re.search(r'free: (\d+) KiB',out)[1])
            before=free()
            for _ in range(3):
                vm.send('compat /home/runtime_check.elf','LINUX ABI C PASS',timeout=30)
                vm.send('compat /home/lua.elf /home/world.lua','LUA RUNTIME PASS',timeout=30)
                vm.send('compat /home/js.elf /home/world.js','JAVASCRIPT RUNTIME PASS',timeout=30)
                vm.send('compat /home/python.elf /home/world.py','PYTHON RUNTIME PASS',timeout=30)
            after=free();assert before==after,(before,after)
            vm.send('unfold lua-world.txt','Lua persistence: 8064',timeout=20)
            vm.send('unfold python-world.txt','Python persistence: 8064',timeout=20)
            vm.send('weave compat-survived.txt survived','C:/ :: ',timeout=20)
            vm.send('unfold compat-survived.txt','survived',timeout=20)
            assert vm.text().count('[isolate x64]')==1 and 'PANIC' not in vm.text()
            print(f'PASS malformed/dynamic ELF rejection, isolated user fault, native Java/PIE recovery and exact reclamation: {before}/{after} KiB',flush=True)
        finally:vm.close()
    print('LINUX RUNTIME GUEST TEST PASSED',flush=True)


if __name__=='__main__':main()
