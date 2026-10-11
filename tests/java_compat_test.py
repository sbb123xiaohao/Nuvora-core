#!/usr/bin/env python3
"""Compare the actual freestanding interpreter with an installed OpenJDK 21.

Requires a Java 21 JDK (JAVA/JAVAC can override the executable names). No network
downloads, Minecraft assets, or reimplementation of the interpreter in Python.
"""
import os
import pathlib
import shlex
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    java = shlex.split(os.environ.get('JAVA', 'java'))
    javac = shlex.split(os.environ.get('JAVAC', 'javac'))
    subprocess.run([*javac, '-version'], check=True)
    with tempfile.TemporaryDirectory(prefix='nuvora-java21-') as directory:
        out = pathlib.Path(directory)
        # -source/-target work with JDK distributions that omit ct.sym. Both
        # source and target are 21; the runtime itself must also be Java 21.
        subprocess.run([*javac, '-source', '21', '-target', '21', '-g:none', '-encoding', 'UTF-8',
                        '-d', str(out), str(ROOT/'examples/java/NuvoraJavaDemo.java'),
                        str(ROOT/'tests/fixtures/java/JavaEdges.java'),
                        *map(str, sorted((ROOT/'examples/java/multiclass').rglob('*.java')))], check=True)
        for name in ('NuvoraJavaDemo', 'JavaEdges'):
            data = (out/(name+'.class')).read_bytes()
            assert data[:8] == bytes.fromhex('cafebabe00000041')
            assert data == (ROOT/'tests/fixtures/java'/f'{name}.class').read_bytes(), \
                f'{name} differs from checked-in javac output; review before regenerating fixtures'
        for fixture in sorted((ROOT/'tests/fixtures/java/multiclass').rglob('*.class')):
            relative = fixture.relative_to(ROOT/'tests/fixtures/java/multiclass')
            assert (out/relative).read_bytes() == fixture.read_bytes(), relative
        env = dict(os.environ, JAVA_TEST_CLASSPATH=str(out))
        runner = out/'runner'
        subprocess.run([os.environ.get('CC', 'gcc'), '-std=c11', '-O1', '-g', '-Wall', '-Wextra',
                        '-Werror', '-fno-builtin', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        '-Iinclude', 'tests/java_runner.c', 'user/java_vm.c', 'common/string.c',
                        '-o', str(runner)], cwd=ROOT, check=True)
        cases = [('NuvoraJavaDemo', []), ('NuvoraJavaDemo', ['hello', '', 'two words', 'a"b', 'C:\\path']),
                 ('JavaEdges', ['utf', '你好🌍']), ('JavaEdges', ['arithmetic']),
                 ('MultiMain', []), ('MultiMain', ['two words', '', 'a"b', 'C:\\path']),
                 ('pkg.Entry', ['two words'])]
        for name, args in cases:
            expected = subprocess.run([*java, '-cp', str(out), name, *args], capture_output=True, check=True)
            actual = subprocess.run([str(runner), str(out/(name.replace('.', '/')+'.class')), *args], capture_output=True, check=True, env=env)
            assert actual.stdout == expected.stdout, (name, args, actual.stderr)
            if name == 'NuvoraJavaDemo' and not args:
                assert actual.stdout == (ROOT/'tests/fixtures/java/NuvoraJavaDemo.out').read_bytes()
            print(f'PASS OpenJDK 21 differential: {name} {args!r}, {len(actual.stdout)} output bytes')
        for mode, exception in [('divide', 'ArithmeticException'), ('long-divide', 'ArithmeticException'),
                                ('null', 'NullPointerException'), ('bounds', 'ArrayIndexOutOfBoundsException'),
                                ('store-bounds', 'ArrayIndexOutOfBoundsException'), ('negative', 'NegativeArraySizeException'),
                                ('store', 'ArrayStoreException'), ('cast', 'ClassCastException'),
                                ('char-bounds', 'StringIndexOutOfBoundsException')]:
            expected = subprocess.run([*java, '-cp', str(out), 'JavaEdges', mode], capture_output=True)
            actual = subprocess.run([str(runner), str(out/'JavaEdges.class'), mode], capture_output=True)
            assert expected.returncode != 0 and actual.returncode == 5
            assert exception.encode() in expected.stderr and exception.encode() in actual.stderr
            print(f'PASS matching fault: {mode} -> {exception} (guest reports and exits; catch is unsupported)')
        for mode, exception in [('store', 'ArrayStoreException'), ('cast', 'ClassCastException'),
                                ('init-fail', 'ArithmeticException')]:
            expected = subprocess.run([*java, '-cp', str(out), 'MultiMain', mode], capture_output=True)
            actual = subprocess.run([str(runner), str(out/'MultiMain.class'), mode], capture_output=True, env=env)
            assert expected.returncode != 0 and actual.returncode == 5
            assert actual.stdout == expected.stdout
            assert exception.encode() in expected.stderr and exception.encode() in actual.stderr
            print(f'PASS matching multi-class fault: {mode} -> {exception}')
        expected = subprocess.run([*java, '-cp', str(out), 'LimitMain'], capture_output=True, check=True)
        actual = subprocess.run([str(runner), str(out/'LimitMain.class')], capture_output=True, env=env)
        assert expected.stdout == b'16\n' and actual.returncode == 6 and b'loaded-class limit' in actual.stderr
        print('PASS explicit subset limit: OpenJDK runs 17 classes; bootstrap rejects class 17')
    print('JAVA21 DIFFERENTIAL: 7 successful programs + 12 fault outcomes matched OpenJDK; class cap checked')


if __name__ == '__main__':
    main()
