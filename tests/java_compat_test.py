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
                        str(ROOT/'tests/fixtures/java/JavaEdges.java')], check=True)
        for name in ('NuvoraJavaDemo', 'JavaEdges'):
            data = (out/(name+'.class')).read_bytes()
            assert data[:8] == bytes.fromhex('cafebabe00000041')
            assert data == (ROOT/'tests/fixtures/java'/f'{name}.class').read_bytes(), \
                f'{name} differs from checked-in javac output; review before regenerating fixtures'
        runner = out/'runner'
        subprocess.run([os.environ.get('CC', 'gcc'), '-std=c11', '-O1', '-g', '-Wall', '-Wextra',
                        '-Werror', '-fno-builtin', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        '-Iinclude', 'tests/java_runner.c', 'user/java_vm.c', 'common/string.c',
                        '-o', str(runner)], cwd=ROOT, check=True)
        cases = [('NuvoraJavaDemo', []), ('NuvoraJavaDemo', ['hello', '', 'two words', 'a"b', 'C:\\path']),
                 ('JavaEdges', ['utf', '你好🌍']), ('JavaEdges', ['arithmetic'])]
        for name, args in cases:
            expected = subprocess.run([*java, '-cp', str(out), name, *args], capture_output=True, check=True)
            actual = subprocess.run([str(runner), str(out/(name+'.class')), *args], capture_output=True, check=True)
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
    print('JAVA21 DIFFERENTIAL: 4 successful programs + 9 fault outcomes matched OpenJDK')


if __name__ == '__main__':
    main()
