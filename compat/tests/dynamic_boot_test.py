#!/usr/bin/env python3
"""Run the real dynamic linker, shared library, signals and exec in Nuvora."""
import pathlib
import sys
import re
import tempfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'scripts'),str(ROOT/'compat/tools')]
from test import VM
from import_media import import_files
from mkgptdisk import create


def main():
    with tempfile.TemporaryDirectory(prefix='nuvora-dynamic-') as directory:
        disk=pathlib.Path(directory)/'data.img';create(disk,size_mib=128,partitions=1)
        fixtures=ROOT/'compat/fixtures/dynamic'
        import_files(disk,[fixtures/name for name in ('ldso','dynamic.elf','libnvprobe.so')])
        vm=VM('dynamic-runtime',disk=disk,memory=64,boot_timeout=30)
        ok=False
        try:
            checks=[('','DYNAMIC PASS'),('--exec','EXEC PASS'),('--guard-check','GUARD PASS'),
                    ('--signal-check','SIGNAL PASS'),('--thread-check','THREAD PASS'),
                    ('--sleep-check','SLEEP PASS'),('--io-check','IO PASS'),('--jit-check','W^X DEFAULT PASS')]
            for option,marker in checks:
                out=vm.send('compat /home/dynamic.elf '+option,marker,timeout=30)
                assert 'exited 0' in out and 'DYNAMIC FAIL' not in out and '[isolate' not in out,out
                print('PASS real guest '+marker,flush=True)
            out=vm.send('compat --jit /home/dynamic.elf --jit-check','JIT OPT IN PASS',timeout=30)
            assert 'exited 0' in out
            def free():return int(re.search(r'free: (\d+) KiB',vm.send('horizon','filesystem nodes:',timeout=20))[1])
            before=free()
            for _ in range(3):
                vm.send('compat /home/dynamic.elf --exec','EXEC PASS',timeout=30)
                vm.send('compat /home/dynamic.elf --signal-check','SIGNAL PASS',timeout=30)
                vm.send('compat /home/dynamic.elf --thread-check','THREAD PASS',timeout=30)
                vm.send('compat /home/dynamic.elf --sleep-check','SLEEP PASS',timeout=30)
                vm.send('compat /home/dynamic.elf --io-check','IO PASS',timeout=30)
                vm.send('compat /home/dynamic.elf --guard-check','GUARD PASS',timeout=30)
            after=free();assert before==after,(before,after)
            vm.send('java --self-test','JAVA21 GUEST PASS',timeout=30)
            assert '[isolate' not in vm.text() and 'KERNEL PANIC' not in vm.text()
            print(f'PASS real dynamic/exec/signal/guard reclamation: {before}/{after} KiB',flush=True)
            ok=True
        finally:vm.close(normal=ok)
    print('DYNAMIC RUNTIME GUEST TEST PASSED',flush=True)


if __name__=='__main__':main()
