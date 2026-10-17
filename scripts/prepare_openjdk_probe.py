#!/usr/bin/env python3
"""Create a new private NVSTORE3 disk for probing real Alpine OpenJDK 21.

This prepares an experimental runtime probe, not a Minecraft installer.
Packages are fetched by HTTPS and pinned digest; no APK scripts are executed.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import struct
import tarfile
import tempfile
import urllib.request
import zipfile
from extent_store import import_tree
from import_media import import_files, partition
from mkgptdisk import create

ROOT=pathlib.Path(__file__).resolve().parent.parent
PACKAGES={
    'jdk':('openjdk21-jre-headless-21.0.11_p10-r0.apk',
           '66487bbb57861b06482a53868c8c7a37ac7e838c748b2cf696f63df5154a6e09','community'),
    'zlib':('zlib-1.3.2-r1.apk','af24e502645e2b392cae833856d776691ac21655362e5db69d7b89f147f76216','main'),
}
JVM_OPTIONS='-Xint -Xmx32m -XX:+UseSerialGC -XX:ActiveProcessorCount=1 -XX:-UsePerfData -XX:-UseCompressedOops -XX:-UseCompressedClassPointers'


def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()


def download(package,cache):
    name,wanted,repository=PACKAGES[package];path=cache/name
    if not path.exists():
        url=f'https://dl-cdn.alpinelinux.org/alpine/v3.22/{repository}/x86_64/{name}'
        with tempfile.NamedTemporaryFile(dir=cache,delete=False) as output:
            pending=pathlib.Path(output.name)
            try:
                with urllib.request.urlopen(url,timeout=45) as response:
                    total=0
                    while data:=response.read(1024*1024):
                        total+=len(data)
                        if total>80*1024*1024:raise ValueError('Package exceeds pinned size limit')
                        output.write(data)
                output.flush()
                if digest(pending)!=wanted:raise ValueError('Downloaded package digest mismatch')
                pending.replace(path)
            finally:pending.unlink(missing_ok=True)
    if digest(path)!=wanted:raise ValueError('Cached package digest mismatch: '+name)
    return path


def patch_interpreter(data):
    """Change only PT_INTERP to the writable native runtime location."""
    data=bytearray(data)
    if len(data)<64 or data[:7]!=b'\x7fELF\x02\x01\x01':raise ValueError('Expected ELF64 little endian')
    start=struct.unpack_from('<Q',data,32)[0];width,count=struct.unpack_from('<HH',data,54)
    if width!=56 or not 0<count<=32 or start>len(data) or count>(len(data)-start)//56:
        raise ValueError('Invalid program header table')
    found=0
    for i in range(count):
        at=start+i*56
        if struct.unpack_from('<I',data,at)[0]!=3:continue
        found+=1;offset=struct.unpack_from('<Q',data,at+8)[0];size=struct.unpack_from('<Q',data,at+32)[0]
        if offset>len(data) or size>len(data)-offset or data[offset:offset+size]!=b'/lib/ld-musl-x86_64.so.1\0':
            raise ValueError('Unexpected original interpreter')
        value=b'/home/ldso\0';data[offset:offset+size]=value+bytes(size-len(value))
        struct.pack_into('<QQ',data,at+32,len(value),len(value))
    if found!=1:raise ValueError('Expected exactly one interpreter')
    return bytes(data)


def prepare(disk,cache,fixtures,work):
    if disk.exists() or disk.is_symlink():raise ValueError('Refusing to overwrite an existing disk')
    manifest=json.loads((fixtures/'manifest.json').read_text())
    for name in ('ldso','dynamic.elf','libnvprobe.so'):
        if digest(fixtures/name)!=manifest['files'][name]:raise ValueError('Dynamic fixture digest mismatch: '+name)
    packages={name:download(name,cache) for name in PACKAGES}
    with tempfile.TemporaryDirectory(prefix='jdk-stage-',dir=work) as directory:
        stage=pathlib.Path(directory)/'jdk';stage.mkdir()
        prefix='usr/lib/jvm/java-21-openjdk/'
        with tarfile.open(packages['jdk'],'r:gz') as archive:
            for member in archive:
                if not member.isfile() or not member.name.startswith(prefix):continue
                relative=pathlib.PurePosixPath(member.name[len(prefix):])
                if relative.is_absolute() or '..' in relative.parts:raise ValueError('Unsafe package path')
                if relative.parts[0] not in ('bin','lib','conf','release','LICENSE','ASSEMBLY_EXCEPTION','README.md','legal'):continue
                if member.size>160*1024*1024:raise ValueError('Runtime member exceeds size limit')
                target=stage/relative;target.parent.mkdir(parents=True,exist_ok=True)
                with archive.extractfile(member) as source,target.open('wb') as output:shutil.copyfileobj(source,output)
        java=stage/'bin/java';original=digest(java);java.write_bytes(patch_interpreter(java.read_bytes()))
        with tarfile.open(packages['zlib'],'r:gz') as archive:
            member=archive.getmember('usr/lib/libz.so.1.3.2')
            if not member.isfile() or member.size>1024*1024:raise ValueError('Unexpected zlib member')
            (stage/'lib/libz.so.1').write_bytes(archive.extractfile(member).read())
        disk.parent.mkdir(parents=True,exist_ok=True);create(disk,size_mib=512,partitions=1)
        jar=pathlib.Path(directory)/'java-check.jar'
        check=ROOT/'examples/java/JavaRuntimeCheck.class'
        if check.read_bytes()[:8]!=bytes.fromhex('cafebabe00000041'):raise ValueError('Expected Java 21 runtime fixture')
        with zipfile.ZipFile(jar,'w',zipfile.ZIP_DEFLATED) as archive:
            archive.writestr('JavaRuntimeCheck.class',check.read_bytes())
            archive.writestr('META-INF/MANIFEST.MF','Manifest-Version: 1.0\nMain-Class: JavaRuntimeCheck\n\n')
        argument_files=[]
        for name,arguments in {
                'java-check.args':JVM_OPTIONS+' -jar /home/java-check.jar jvm-proof.txt',
                'java-jit.args':JVM_OPTIONS.replace('-Xint ','')+' -Xbatch -XX:+PrintCompilation -jar /home/java-check.jar jvm-proof.txt jit',
                'java-private.args':JVM_OPTIONS+' -jar /home/java-check.jar jvm-proof.txt privacy'}.items():
            path=pathlib.Path(directory)/name;path.write_text(arguments+'\n');argument_files.append(path)
        import_files(disk,[*[fixtures/name for name in ('ldso','dynamic.elf','libnvprobe.so')],jar,*argument_files])
        with disk.open('r+b') as f:
            first,sectors=partition(f);entries,metadata=import_tree(f,first,sectors,stage,'/home/jdk')
        report={'packages':{name:{'file':value[0],'sha256':value[1]} for name,value in PACKAGES.items()},
                'launcher_original_sha256':original,'launcher_packaged_sha256':digest(java),
                'only_elf_change':'PT_INTERP /lib/ld-musl-x86_64.so.1 -> /home/ldso; segment string length adjusted',
                'runtime_tree_entries':entries,'metadata_bytes':metadata,
                'java_check_sha256':digest(check),'java_check_jar_sha256':digest(jar),
                'status':'Experimental OpenJDK 21 probe; not a Minecraft installer or a general Java compatibility guarantee.'}
        disk.with_suffix('.provenance.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disk',type=pathlib.Path,default=ROOT/'build/openjdk-probe/data.img')
    parser.add_argument('--cache',type=pathlib.Path,default=ROOT/'build/openjdk-probe/downloads')
    parser.add_argument('--work',type=pathlib.Path,default=ROOT/'build/openjdk-probe')
    parser.add_argument('--fixtures',type=pathlib.Path,default=ROOT/'tests/fixtures/dynamic')
    options=parser.parse_args()
    options.cache.mkdir(parents=True,exist_ok=True);options.work.mkdir(parents=True,exist_ok=True)
    print(json.dumps(prepare(options.disk,options.cache,options.fixtures,options.work),indent=2))
    print('Prepared probe disk:',options.disk)


if __name__=='__main__':main()
