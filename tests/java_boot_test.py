#!/usr/bin/env python3
"""Run .class files from a private Nuvora disk, in real Ring 3 processes."""
import pathlib
import re
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT/'scripts'))
from test import VM
from mkgptdisk import create
from import_media import import_files


def main():
    with tempfile.TemporaryDirectory(prefix='nuvora-java-guest-') as directory:
        private = pathlib.Path(directory)
        disk = private/'data.img'
        create(disk, size_mib=128, partitions=1)
        demo = private/'Java Demo.class'
        demo.write_bytes((ROOT/'tests/fixtures/java/NuvoraJavaDemo.class').read_bytes())
        bad = private/'Bad.class'
        bad.write_bytes(demo.read_bytes()[:100])
        jar = private/'Game.jar'
        jar.write_bytes(b'PK\x03\x04' + b'\0'*28)
        multi = ROOT/'tests/fixtures/java/multiclass'
        import_files(disk, [demo, bad, jar, ROOT/'tests/fixtures/java/JavaEdges.class',
                           *[p for p in sorted(multi.rglob('*.class')) if p.name != 'Missing.class']])
        vm = VM('java21-disk', disk=disk, memory=64, boot_timeout=30)
        try:
            version = vm.send('java --version', 'not a Java SE 21 runtime.', timeout=20)
            assert 'class format 52..65' in version
            for command in ('java --demo "two words"', 'java "/home/Java Demo.class" "two words"',
                            'forge java "/home/Java Demo.class" "two words"'):
                out = vm.send(command, 'JAVA21 DEMO PASS', timeout=30)
                assert 'checks=20, chunk=2672080966' in out and 'arg[0]=two words' in out
                assert 'arg[1]=' not in out
                print(f'PASS Ring 3: {command}', flush=True)
            for mode, marker in [('bounds', 'ArrayIndexOutOfBoundsException'), ('null', 'NullPointerException'),
                                 ('memory', 'Java array exceeds arena'), ('recursive', 'Java call-depth limit'),
                                 ('forever', 'Java instruction budget exhausted'), ('library', 'Java library method is not implemented')]:
                vm.send(f'java /home/JavaEdges.class {mode}', marker, timeout=45)
                print(f'PASS isolated guest failure: {mode} -> {marker}', flush=True)
            vm.send('java /home/Bad.class', 'Java stopped:', timeout=20)
            vm.send('java /home/Game.jar', 'JAR loading is not implemented', timeout=20)
            vm.send('java -jar /home/Game.jar', 'JAR/options are not implemented', timeout=20)
            for command in ('java /home/MultiMain.class "two words" ""',
                            'java -cp /home MultiMain "two words" ""',
                            'forge java --class-path /home MultiMain "two words" ""'):
                out = vm.send(command, 'MULTICLASS PASS', timeout=30)
                assert 'lazy=1, null=true' in out and 'cycle=5,3' in out
                assert 'sum=2469135780256, calls=1' in out and 'final-order=1325674' in out
                assert 'arg[0]=two words' in out and 'arg[1]=\n' in out.replace('\r', '')
                print(f'PASS Ring 3 multi-class: {command}', flush=True)
            for command in ('nest /home/pkg', 'shift /home/Entry.class /home/pkg/Entry.class',
                            'shift /home/Value.class /home/pkg/Value.class'):
                vm.send(command, 'C:/ :: ', timeout=20)
            out = vm.send('java -cp /home pkg.Entry "package argument"', 'PACKAGE PASS', timeout=30)
            assert 'package=21, arg=package argument' in out
            print('PASS packaged classpath: pkg.Entry -> pkg.Value from real nested files', flush=True)
            for mode, marker in [('store', 'ArrayStoreException'), ('cast', 'ClassCastException'),
                                 ('missing', 'Class not found or unreadable'), ('init-fail', 'ArithmeticException')]:
                vm.send(f'java -cp /home MultiMain {mode}', marker, timeout=30)
                print(f'PASS multi-class guest failure: {mode} -> {marker}', flush=True)
            vm.send('java -cp /home LimitMain', 'Java loaded-class limit exceeded', timeout=30)
            vm.send('java -cp /home ../MultiMain', 'Java: invalid class name', timeout=20)
            vm.send('mirror /home/Cold.class /home/Wrong.class', 'C:/ :: ', timeout=20)
            vm.send('java -cp /home Wrong', 'Entry class name does not match', timeout=20)
            # All class files have been cached by now. Repeated child launches
            # must return their entire 2 MiB arena and image/stack pages.
            def free_pages():
                out = vm.send('horizon', 'filesystem nodes:', timeout=15)
                return int(re.search(r'free: (\d+) KiB', out)[1])
            before = free_pages()
            for _ in range(3):
                vm.send('java --self-test', 'JAVA21 GUEST PASS', timeout=30)
                vm.send('java -cp /home MultiMain', 'MULTICLASS PASS', timeout=30)
            assert free_pages() == before, 'Java exit leaked guest pages'
            vm.send('weave /home/java-survived.txt survived', 'C:/ :: ')
            vm.send('unfold /home/java-survived.txt', 'survived')
            assert 'PANIC' not in vm.text() and '[trap]' not in vm.text()
            print('PASS Java guest exit reclamation, multi-class reclamation, malformed/JAR rejection, shell and filesystem recovery', flush=True)
        finally:
            vm.close()
    print('JAVA21 GUEST DISK TEST PASSED', flush=True)


if __name__ == '__main__':
    main()
