#!/usr/bin/env python3
"""Build real musl static PIE examples, Lua, QuickJS and MicroPython.

Optional SDK step; ordinary Nuvora builds/tests use the checked-in ELF fixtures.
Downloads are pinned, verified before extraction, and never modify host disks.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import pathlib
import shlex
import shutil
import subprocess
import tarfile
import urllib.request

ROOT=pathlib.Path(__file__).resolve().parent.parent
SOURCES={
    'musl':('musl-1.2.5.tar.gz','a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4',
            ['https://musl.libc.org/releases/musl-1.2.5.tar.gz','https://distfiles.alpinelinux.org/distfiles/v3.22/musl-1.2.5.tar.gz']),
    'lua':('lua-5.4.8.tar.gz','4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae',
           ['https://www.lua.org/ftp/lua-5.4.8.tar.gz']),
    'quickjs':('quickjs-2025-09-13-2.tar.xz','996c6b5018fc955ad4d06426d0e9cb713685a00c825aa5c0418bd53f7df8b0b4',
               ['https://bellard.org/quickjs/quickjs-2025-09-13-2.tar.xz']),
    'micropython':('micropython-v1.26.1.tar.gz','10600e844f02a8b448cf176df71031d2eb3d731097a809a963ce9c35cef3d613',
                  ['https://codeload.github.com/micropython/micropython/tar.gz/refs/tags/v1.26.1']),
}

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def run(command,**kwargs): subprocess.run(list(map(str,command)),check=True,**kwargs)

def extract(name,work,cache):
    filename,wanted,urls=SOURCES[name]; archive=cache/filename
    if not archive.is_file():
        error=None
        for url in urls:
            try:
                with urllib.request.urlopen(url,timeout=45) as stream: data=stream.read(16*1024*1024+1)
                if len(data)>16*1024*1024 or hashlib.sha256(data).hexdigest()!=wanted:
                    raise ValueError('Source digest/size mismatch: '+url)
                archive.write_bytes(data);break
            except (OSError,ValueError) as exc:error=exc
        else:raise RuntimeError('No verified source for '+name) from error
    if digest(archive)!=wanted:raise ValueError('Cached source digest mismatch: '+str(archive))
    dest=work/(name+'-source')
    if not dest.exists():
        staging=work/(name+'-extract')
        if staging.exists():shutil.rmtree(staging)
        staging.mkdir()
        with tarfile.open(archive) as file:file.extractall(staging,filter='data')
        roots=list(staging.iterdir())
        if len(roots)!=1 or not roots[0].is_dir():raise ValueError('Unexpected source archive layout')
        roots[0].rename(dest);staging.rmdir()
    return dest

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=pathlib.Path,default=ROOT/'build/compat-runtimes')
    parser.add_argument('--cache',type=pathlib.Path)
    parser.add_argument('--output',type=pathlib.Path,default=ROOT/'build/compat-examples')
    parser.add_argument('--jobs',type=int,default=min(os.cpu_count() or 1,4))
    options=parser.parse_args()
    if not 1<=options.jobs<=32:parser.error('--jobs must be 1..32')
    work=options.work.resolve();work.mkdir(parents=True,exist_ok=True)
    cache=(options.cache or work/'downloads').resolve();cache.mkdir(parents=True,exist_ok=True)
    output=options.output.resolve();output.mkdir(parents=True,exist_ok=True)
    cc=shlex.split(os.environ.get('CC','gcc'));version=subprocess.check_output([*cc,'--version'],text=True).splitlines()[0]
    source={name:extract(name,work,cache) for name in SOURCES}
    musl=work/'musl';build=work/'musl-build';build.mkdir(exist_ok=True)
    with (work/'musl-build.log').open('w') as log:
        env=dict(os.environ,CC=shlex.join(cc))
        run([source['musl']/'configure','--prefix='+str(musl),'--disable-shared','CFLAGS=-O2 -fPIC'],cwd=build,env=env,stdout=log,stderr=subprocess.STDOUT)
        run(['make','-j'+str(options.jobs)],cwd=build,stdout=log,stderr=subprocess.STDOUT)
        run(['make','install'],cwd=build,stdout=log,stderr=subprocess.STDOUT)
    def gcc_file(name):return subprocess.check_output([*cc,'-print-file-name='+name],text=True).strip()
    base=[*cc,'-O2','-fPIE','-nostdinc','-isystem',str(musl/'include'),'-isystem',gcc_file('include'),
          '-ffile-prefix-map='+str(ROOT)+'=nuvora','-ffile-prefix-map='+str(work)+'=compat-build']
    link=[*cc,'-static-pie','-nostdlib','-Wl,--build-id=none','-Wl,-z,noexecstack',musl/'lib/rcrt1.o',musl/'lib/crti.o',gcc_file('crtbeginS.o')]
    libraries=['-L'+str(musl/'lib'),'-Wl,--start-group','-lc','-lgcc','-lgcc_eh','-Wl,--end-group',gcc_file('crtendS.o'),musl/'lib/crtn.o']
    object_dir=work/'objects';object_dir.mkdir(exist_ok=True)
    def compile_one(path,flags=()):
        obj=object_dir/(path.stem+'.o');run([*base,*flags,'-c',path,'-o',obj]);return obj
    groups={}
    groups['runtime_check']=[compile_one(ROOT/'examples/compat/runtime_check.c',['-std=c11'])]
    groups['language']=[compile_one(ROOT/'examples/compat/language.cpp',['-fno-exceptions','-fno-rtti'])]
    with concurrent.futures.ThreadPoolExecutor(max_workers=options.jobs) as pool:
        groups['lua']=list(pool.map(lambda path:compile_one(path,['-std=c99','-DLUA_COMPAT_5_3']),
                                   [p for p in sorted((source['lua']/'src').glob('*.c')) if p.name!='luac.c']))
        js_flags=['-D_GNU_SOURCE','-DCONFIG_VERSION="2025-09-13"','-I'+str(source['quickjs'])]
        js_sources=[source['quickjs']/(name+'.c') for name in ('quickjs','dtoa','libregexp','libunicode','cutils')]
        groups['js']=list(pool.map(lambda path:compile_one(path,js_flags),js_sources+[ROOT/'examples/compat/javascript.c']))
    python_build=work/'python-build'
    with (work/'python-build.log').open('w') as log:
        # Build unmodified upstream sources with our variant. The intermediate
        # host-style link is discarded; rcrt1 below produces the delivered PIE.
        run(['make','-C',source['micropython']/'ports/unix','-j'+str(options.jobs),
             'VARIANT_DIR='+str(ROOT/'examples/compat/python-port'),'BUILD='+str(python_build),
             'CC='+str(musl/'bin/musl-gcc'),
             'CFLAGS_EXTRA=-fPIE -ffile-prefix-map='+str(work)+'=compat-build',
             'LDFLAGS_EXTRA=-static'],env=dict(os.environ,REALGCC=shlex.join(cc)),stdout=log,stderr=subprocess.STDOUT)
    groups['python']=sorted(python_build.rglob('*.o'))
    if not groups['python']:raise ValueError('MicroPython build produced no objects')
    for name,objects in groups.items():
        target=output/(name+'.elf');run([*link,*objects,*libraries,'-o',target])
        run([*shlex.split(os.environ.get('STRIP','strip')),'--strip-all',target])
    licenses=output/'licenses';licenses.mkdir(exist_ok=True)
    shutil.copyfile(source['musl']/'COPYRIGHT',licenses/'musl-COPYRIGHT.txt')
    lua_header=(source['lua']/'src/lua.h').read_text();start=lua_header.index('/******************************************************************************')
    (licenses/'Lua-COPYRIGHT.txt').write_text(lua_header[start:lua_header.index('#endif',start)].rstrip()+'\n')
    shutil.copyfile(source['quickjs']/'LICENSE',licenses/'QuickJS-LICENSE.txt')
    shutil.copyfile(source['micropython']/'LICENSE',licenses/'MicroPython-LICENSE.txt')
    # GCC runtime support is linked under the Runtime Library Exception; keep
    # the compiler distribution's complete copyright and license notice.
    gcc_notice=pathlib.Path('/usr/share/doc/gcc-14/copyright')
    if gcc_notice.is_file() and '14.' in version:
        (licenses/'GCC-runtime-notice.txt').write_text('\n'.join(line.rstrip() for line in gcc_notice.read_text().splitlines())+'\n')
    metadata={'compiler':version,'source_archives':{n:{'file':v[0],'sha256':v[1]} for n,v in SOURCES.items()},
              'elfs':{n+'.elf':digest(output/(n+'.elf')) for n in groups},
              'wrappers':{p.relative_to(ROOT/'examples/compat').as_posix():digest(p)
                          for p in sorted((ROOT/'examples/compat').rglob('*')) if p.is_file() and '__pycache__' not in p.parts},
              'limits':'static musl PIE; C++ language core only; QuickJS has no Node.js/DOM/workers; MicroPython is not CPython/pip; no JVM or Minecraft launch'}
    (output/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print('BUILT REAL STATIC RUNTIMES:',output)
    print(json.dumps(metadata['elfs'],indent=2))

if __name__=='__main__':main()
