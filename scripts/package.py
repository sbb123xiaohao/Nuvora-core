#!/usr/bin/env python3
"""Package every tracked file from clean HEAD; verify paths, count and bytes.

Build artifacts and local disks are delivered separately from this source ZIP.
"""
import argparse
import hashlib
import pathlib
import re
import subprocess
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def committed_files():
    entries = git('ls-tree', '-rz', '--full-tree', 'HEAD').split(b'\0')
    with subprocess.Popen(['git', 'cat-file', '--batch'], cwd=ROOT,
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
        try:
            for entry in entries:
                if not entry:
                    continue
                meta, name = entry.split(b'\t', 1)
                mode, kind, object_id = meta.split()
                if kind != b'blob':
                    raise RuntimeError('Source delivery requires full files, not submodule references')
                process.stdin.write(object_id + b'\n')
                process.stdin.flush()
                header = process.stdout.readline().split()
                assert len(header) == 3 and header[:2] == [object_id, b'blob']
                size = int(header[2])
                data = process.stdout.read(size)
                assert len(data) == size and process.stdout.read(1) == b'\n'
                yield name.decode('utf-8'), data, int(mode, 8)
        finally:
            process.stdin.close()
        assert process.wait() == 0


def validate_output(output, commit):
    resolved = output.resolve()
    tracked = git('ls-tree', '-r', '--name-only', '-z', commit).decode().split('\0')[:-1]
    if any(resolved == (ROOT / name).resolve() for name in tracked):
        raise SystemExit('Source output must not overwrite a tracked project file')
    directories = [ROOT / '.git']
    for option in ('--git-dir', '--git-common-dir'):
        directory = pathlib.Path(git('rev-parse', option).decode().strip())
        if not directory.is_absolute():
            directory = ROOT / directory
        directories.append(directory)
    for directory in directories:
        # Git may keep its objects in a symlinked directory. Protect both
        # the metadata path and its resolved target, including worktree files.
        if any(path.is_relative_to(base) for path in (output, resolved)
               for base in (directory.absolute(), directory.resolve())):
            raise SystemExit('Source output must not overwrite Git metadata')
    if output.exists() and not output.is_file():
        raise SystemExit('Source output must be a regular file')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--manifest', action='store_true', help='Write HEAD SHA256SUMS instead of ZIP')
    args = parser.parse_args()
    if git('status', '--porcelain').strip():
        raise SystemExit('Commit the complete current tree before packaging; HEAD must be clean')
    commit = git('rev-parse', 'HEAD').decode().strip()
    header = git('show', 'HEAD:include/nv/abi.h').decode()
    version = re.search(r'#define NV_VERSION "([^"]+)"', header)[1]
    prefix = f'nuvora-core-{version}-{commit[:7]}/'
    output = args.output.absolute()
    validate_output(output, commit)
    output.parent.mkdir(parents=True, exist_ok=True)
    # Publish only after the pending file is complete and verified. A failed
    # blob read or archive write must leave a previous deliverable intact.
    with tempfile.TemporaryDirectory(prefix='nuvora-source-', dir=output.parent) as directory:
        pending = pathlib.Path(directory) / output.name
        if args.manifest:
            lines = [hashlib.sha256(data).hexdigest() + '  ' + name
                     for name, data, _ in committed_files()]
            pending.write_text('\n'.join(lines) + '\n')
        else:
            expected = {}
            with zipfile.ZipFile(pending, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
                for name, data, mode in committed_files():
                    member = prefix + name
                    entry = zipfile.ZipInfo(member)
                    entry.create_system = 3
                    entry.external_attr = mode << 16
                    entry.compress_type = zipfile.ZIP_DEFLATED
                    archive.writestr(entry, data)
                    expected[member] = hashlib.sha256(data).digest()
            with zipfile.ZipFile(pending) as archive:
                names = archive.namelist()
                assert len(names) == len(expected) and set(names) == set(expected)
                assert archive.testzip() is None
                for name, digest in expected.items():
                    assert hashlib.sha256(archive.read(name)).digest() == digest, name
        assert git('rev-parse', 'HEAD').decode().strip() == commit
        pending.replace(output)
    if args.manifest:
        print(f'{len(lines)} committed files; HEAD {commit}; {output}')
        return
    print(f'{len(expected)} files; HEAD {commit}; every path and byte verified')
    print(f'{output.stat().st_size} bytes; {output}')
    print('SHA256 ' + hashlib.sha256(output.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
