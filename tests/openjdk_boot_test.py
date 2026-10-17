#!/usr/bin/env python3
"""Exercise the real OpenJDK 21 installation prepared by the pinned SDK step."""
import argparse
import pathlib
import re
import sys
ROOT=pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'scripts'))
from test import VM
from prepare_openjdk_probe import JVM_OPTIONS


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disk',type=pathlib.Path,default=ROOT/'build/openjdk-probe/data.img')
    parser.add_argument('--uefi',action='store_true')
    parser.add_argument('--trace',action='store_true')
    options=parser.parse_args()
    if not options.disk.is_file():parser.error('Prepare a private disk with scripts/prepare_openjdk_probe.py first')
    vm=VM('openjdk21-runtime',memory=512,boot_timeout=30,uefi=options.uefi,
          hardware=['-drive',f'file={options.disk},format=raw,if=none,id=jdkdisk','-device','nvme,drive=jdkdisk,serial=NV-JDK-TEST'])
    prefix='compat --jit '+('--trace ' if options.trace else '')+'/home/jdk/bin/java'
    ok=False
    try:
        out=vm.send(prefix+' '+JVM_OPTIONS+' -version','openjdk version "21.0.11"',timeout=180)
        assert 'OpenJDK 64-Bit Server VM' in out and 'exited 0' in out,out
        print('PASS real OpenJDK 21.0.11 / HotSpot interpreted startup and version',flush=True)
        out=vm.send(prefix+' @/home/java-check.args','OPENJDK JAVA PASS',timeout=180)
        assert 'sum=200020000' in out and 'exited 0' in out,out
        vm.send('unfold /home/jvm-proof.txt','Real OpenJDK 21 on Nuvora: threads=4 sum=200020000',timeout=30)
        print('PASS real Java 21: JAR/ZIP, four threads, atomics, latch/join, SHA-256 provider, BigInteger, NIO, clocks and GC',flush=True)
        def free():return int(re.search(r'free: (\d+) KiB',vm.send('horizon','filesystem nodes:',timeout=20))[1])
        before=free()
        vm.send(prefix+' @/home/java-check.args','OPENJDK JAVA PASS',timeout=180)
        after=free();assert before==after,(before,after)
        jit=prefix+' @/home/java-jit.args'
        out=vm.send(jit,'HOTSPOT JIT PASS checksum=dc9889355911c47f',timeout=180)
        assert 'JavaRuntimeCheck::hot' in out and 'OPENJDK JAVA PASS' in out and 'exited 0' in out,out
        print('PASS real HotSpot JIT: compiled JavaRuntimeCheck::hot, executed 200000 iterations and matched arithmetic checksum',flush=True)
        out=vm.send(prefix+' '+JVM_OPTIONS+' MissingClass','Could not find or load main class',timeout=180)
        assert 'exited 1' in out
        vm.send('java --self-test','JAVA21 GUEST PASS',timeout=30)
        assert 'KERNEL PANIC' not in vm.text() and '[isolate' not in vm.text()
        print(f'PASS JVM group exit, exact reclamation {before}/{after} KiB, missing-class error and native recovery',flush=True)
        ok=True
    finally:vm.close(normal=ok)
    print('OPENJDK21 RUNTIME GUEST TEST PASSED; Minecraft/LWJGL/OpenGL remain separate work',flush=True)


if __name__=='__main__':main()
