#!/usr/bin/env python3
"""Build a real musl dynamic linker and shared-library/process ABI fixtures."""
import argparse
import json
import os
import pathlib
import shlex
import shutil
import subprocess
from build_compat_runtimes import ROOT, SOURCES, extract, run, digest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=pathlib.Path,default=ROOT/'build/dynamic-runtimes')
    parser.add_argument('--cache',type=pathlib.Path)
    parser.add_argument('--output',type=pathlib.Path,default=ROOT/'build/dynamic-examples')
    parser.add_argument('--jobs',type=int,default=4)
    options=parser.parse_args()
    if not 1<=options.jobs<=32:parser.error('--jobs must be 1..32')
    work=options.work.resolve();work.mkdir(parents=True,exist_ok=True)
    cache=(options.cache or work/'downloads').resolve();cache.mkdir(parents=True,exist_ok=True)
    output=options.output.resolve();output.mkdir(parents=True,exist_ok=True)
    source=extract('musl',work,cache);prefix=work/'musl';build=work/'musl-build';build.mkdir(exist_ok=True)
    cc=shlex.split(os.environ.get('CC','gcc'))
    with (work/'musl-build.log').open('w') as log:
        run([source/'configure','--prefix='+str(prefix),'--syslibdir='+str(prefix/'lib'),
             'CFLAGS=-O2 -fPIC','LDFLAGS=-Wl,--build-id=none'],cwd=build,
            env=dict(os.environ,CC=shlex.join(cc)),stdout=log,stderr=subprocess.STDOUT)
        run(['make','-j'+str(options.jobs),'install'],cwd=build,stdout=log,stderr=subprocess.STDOUT)
    flags=['-O2','-Wl,--build-id=none','-Wl,-z,noexecstack',
           '-ffile-prefix-map='+str(ROOT)+'=nuvora','-ffile-prefix-map='+str(work)+'=dynamic-build']
    compiler=prefix/'bin/musl-gcc'
    run([compiler,*flags,'-fPIE','-pie','-Wl,--dynamic-linker=/home/ldso',
         ROOT/'examples/compat/dynamic_check.c','-o',output/'dynamic.elf'])
    run([compiler,*flags,'-fPIC','-shared',ROOT/'examples/compat/dynamic_plugin.c','-o',output/'libnvprobe.so'])
    shutil.copyfile(prefix/'lib/libc.so',output/'ldso')
    for name in ('ldso','dynamic.elf','libnvprobe.so'):
        run([*shlex.split(os.environ.get('STRIP','strip')),'--strip-all',output/name])
        (output/name).chmod(0o755)
    shutil.copyfile(source/'COPYRIGHT',output/'musl-COPYRIGHT.txt')
    manifest={'compiler':subprocess.check_output([*cc,'--version'],text=True).splitlines()[0],
              'musl_source':{'file':SOURCES['musl'][0],'sha256':SOURCES['musl'][1]},
              'files':{p.name:digest(p) for p in sorted(output.iterdir()) if p.is_file() and p.name!='manifest.json'},
              'wrappers':{name:digest(ROOT/'examples/compat'/name) for name in ('dynamic_check.c','dynamic_plugin.c')},
              'scope':'Real dynamic musl, DSO/TLS, pthread, signal, exec and JIT fixtures; Minecraft is not exercised.'}
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('BUILT DYNAMIC RUNTIME FIXTURES:',output)


if __name__=='__main__':main()
