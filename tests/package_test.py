#!/usr/bin/env python3
"""Exercise source packaging in disposable clean Git repositories."""
import argparse
import contextlib
import hashlib
import importlib.util
import io
import pathlib
import shutil
import subprocess
import sys
import tempfile
from unittest import mock
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def git(root, *args):
    return subprocess.check_output(['git', *args], cwd=root, stderr=subprocess.PIPE)


def repository(directory):
    root = directory / 'source'
    (root / 'scripts').mkdir(parents=True)
    (root / 'include/nv').mkdir(parents=True)
    (root / 'README.md').write_bytes(b'Keep the original project source.\n')
    (root / 'include/nv/abi.h').write_text('#define NV_VERSION "1.2.3"\n')
    (root / '.gitignore').write_text('SHA256SUMS\n__pycache__/\n*.pyc\n')
    shutil.copyfile(ROOT / 'scripts/package.py', root / 'scripts/package.py')
    git(root, 'init', '-q')
    git(root, 'add', '.')
    git(root, '-c', 'user.name=Package fixture', '-c', 'user.email=fixture@example.invalid',
        'commit', '-qm', 'Packaging fixture')
    return root


def package(root, output, manifest=False):
    return subprocess.run([sys.executable, str(root / 'scripts/package.py'),
                           *(['--manifest'] if manifest else []), str(output)],
                          cwd=root, capture_output=True, text=True)


def protected_output(case):
    for manifest in (False, True):
        with tempfile.TemporaryDirectory(prefix='nuvora-package-') as temporary:
            directory = pathlib.Path(temporary)
            root = repository(directory)
            if case == 'worktree':
                linked = directory / 'linked'
                git(root, 'worktree', 'add', '--detach', str(linked))
                root = linked
            if case == 'source':
                target = root / 'README.md'
            elif case == 'git':
                target = root / '.git/config'
            elif case == 'git-link':
                object_id = git(root, 'rev-parse', 'HEAD:README.md').decode().strip()
                external = directory / 'objects'
                shutil.move(root / '.git/objects', external)
                (root / '.git/objects').symlink_to(external, target_is_directory=True)
                target = root / '.git/objects' / object_id[:2] / object_id[2:]
            elif case == 'worktree':
                target = root / '.git'
            else:
                target = root / 'README.md'
            original = target.read_bytes()
            output = target
            if case == 'alias':
                output = directory / 'output-link'
                output.symlink_to(target)
            result = package(root, output, manifest)
            assert target.read_bytes() == original, (case, 'source overwritten')
            assert result.returncode != 0, (case, manifest, 'protected output accepted')
            expected_error = 'tracked project file' if case in ('source', 'alias') else 'Git metadata'
            assert expected_error in result.stderr, (case, 'unexpected refusal', result.stderr)
            assert not git(root, 'status', '--porcelain').strip()
            if case == 'alias':
                assert output.is_symlink()
    print(f'PASS package {case}: ZIP and manifest refuse protected output without changes')


def interrupted_output():
    with tempfile.TemporaryDirectory(prefix='nuvora-package-') as temporary:
        directory = pathlib.Path(temporary)
        root = repository(directory)
        output = directory / 'source.zip'
        original = b'Previous complete deliverable'
        output.write_bytes(original)
        spec = importlib.util.spec_from_file_location('packaging_fixture', root / 'scripts/package.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        def interrupted():
            yield 'README.md', b'partial source', 0o100644
            raise RuntimeError('Injected committed blob read failure')

        with mock.patch.object(sys, 'argv', ['package.py', str(output)]), \
             mock.patch.object(module, 'committed_files', interrupted), \
             contextlib.redirect_stdout(io.StringIO()):
            try:
                module.main()
            except RuntimeError as error:
                assert 'Injected committed blob' in str(error)
            else:
                raise AssertionError('interrupted archive reported success')
        assert output.read_bytes() == original, 'Failed packaging destroyed previous archive'
        assert sorted(p.name for p in directory.iterdir()) == ['source', 'source.zip']
        assert not git(root, 'status', '--porcelain').strip()
    print('PASS package failure: previous archive survives, pending files are removed')


def successful_output():
    with tempfile.TemporaryDirectory(prefix='nuvora-package-') as temporary:
        directory = pathlib.Path(temporary)
        root = repository(directory)
        output = directory / 'source.zip'
        output.write_bytes(b'Old archive')
        result = package(root, output)
        assert result.returncode == 0, result.stderr
        commit = git(root, 'rev-parse', 'HEAD').decode().strip()
        names = git(root, 'ls-tree', '-r', '--name-only', '-z', 'HEAD').decode().split('\0')[:-1]
        prefix = f'nuvora-core-1.2.3-{commit[:7]}/'
        expected = {name: git(root, 'show', 'HEAD:' + name) for name in names}
        with zipfile.ZipFile(output) as archive:
            assert archive.testzip() is None
            assert sorted(archive.namelist()) == sorted(prefix + name for name in names)
            for name, data in expected.items():
                assert archive.read(prefix + name) == data
        manifest = root / 'SHA256SUMS'
        result = package(root, manifest, manifest=True)
        assert result.returncode == 0, result.stderr
        assert manifest.read_text().splitlines() == [
            hashlib.sha256(expected[name]).hexdigest() + '  ' + name for name in names]
        assert not git(root, 'status', '--porcelain').strip()
    print('PASS package success: complete clean HEAD ZIP and ignored manifest, every byte verified')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=('all', 'source', 'git', 'git-link', 'worktree', 'alias', 'atomic', 'success'),
                        default='all')
    args = parser.parse_args()
    for case in ('source', 'git', 'git-link', 'worktree', 'alias'):
        if args.case in ('all', case):
            protected_output(case)
    if args.case in ('all', 'atomic'):
        interrupted_output()
    if args.case in ('all', 'success'):
        successful_output()


if __name__ == '__main__':
    main()
