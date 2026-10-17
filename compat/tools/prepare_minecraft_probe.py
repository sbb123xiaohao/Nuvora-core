#!/usr/bin/env python3
"""Prepare a fresh private disk for an official Minecraft client launch probe.

Only pinned 1.20.1 and 1.21.1 metadata are accepted. This downloads the original
client/libraries, not a modified game. External asset objects are not installed;
the result is an experimental launch probe, not a playable installation.
"""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import stat
import sys
import tempfile
import urllib.parse
import urllib.request
import zipfile

ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from extent_store import import_tree
from import_media import partition
from prepare_openjdk_probe import JVM_OPTIONS, prepare as prepare_jdk
from runtime_probe import inspect

HOSTS={'piston-meta.mojang.com','piston-data.mojang.com','libraries.minecraft.net'}
TARGETS=json.loads((ROOT/'compat/minecraft/target.json').read_text())['targets']
GUEST='/home/minecraft'


def official_url(value):
    url=urllib.parse.urlsplit(value)
    if url.scheme!='https' or url.hostname not in HOSTS or url.port not in (None,443) or url.username or url.password:
        raise ValueError('Expected an official HTTPS artifact URL')
    return value


class OfficialRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,request,fp,code,message,headers,newurl):
        return super().redirect_request(request,fp,code,message,headers,official_url(newurl))


def fetch(path,entry,algorithm='sha1'):
    """Verify cached and streamed artifacts; publish only a complete download."""
    wanted=entry[algorithm];size=entry.get('size');url=official_url(entry['url'])
    if not re.fullmatch('[0-9a-f]{'+str(hashlib.new(algorithm).digest_size*2)+'}',wanted):
        raise ValueError('Invalid artifact digest')
    limit=size if size is not None else 8*1024*1024
    if not 0<limit<=96*1024*1024:raise ValueError('Artifact exceeds size limit')
    def valid(candidate):
        if not candidate.is_file() or candidate.stat().st_size>limit:return False
        if size is not None and candidate.stat().st_size!=size:return False
        with candidate.open('rb') as source:return hashlib.file_digest(source,algorithm).hexdigest()==wanted
    if path.is_symlink():raise ValueError('Refusing a symlink cache entry')
    if valid(path):return path
    path.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent,delete=False) as output:
        pending=pathlib.Path(output.name)
        try:
            opener=urllib.request.build_opener(OfficialRedirect())
            with opener.open(url,timeout=45) as source:
                total=0
                while data:=source.read(1024*1024):
                    total+=len(data)
                    if total>limit:raise ValueError('Artifact exceeds declared length')
                    output.write(data)
            output.flush()
            if not valid(pending):raise ValueError('Artifact digest/length mismatch')
            pending.replace(path)
        finally:pending.unlink(missing_ok=True)
    return path


def linux_library(library):
    rules=library.get('rules',[]);allowed=not rules
    for rule in rules:
        platform=rule.get('os',{})
        if rule.get('features') or platform.get('name','linux')!='linux':continue
        if platform.get('arch','x86_64') not in ('x86_64','amd64'):continue
        if 'version' in platform:raise ValueError('Unspecified Linux version rule')
        if rule['action'] not in ('allow','disallow'):raise ValueError('Unknown library rule')
        allowed=rule['action']=='allow'
    return allowed and 'artifact' in library.get('downloads',{})


def extract_natives(jar,directory):
    with zipfile.ZipFile(jar) as archive:
        members=archive.infolist()
        if len(members)>256:raise ValueError('Too many native archive members')
        total=0
        for member in members:
            if not member.filename.endswith('.so'):continue
            relative=pathlib.PurePosixPath(member.filename);name=relative.name
            if relative.is_absolute() or '..' in relative.parts or '\\' in member.filename or stat.S_ISLNK(member.external_attr>>16):
                raise ValueError('Unsafe native archive member')
            if not re.fullmatch(r'[A-Za-z0-9_.-]{1,31}',name):raise ValueError('Native filename exceeds filesystem limits')
            total+=member.file_size
            if total>64*1024*1024:raise ValueError('Native archive exceeds extraction budget')
            data=archive.read(member)
            if data[:7]!=b'\x7fELF\x02\x01\x01':raise ValueError('Expected a little-endian ELF64 native library')
            target=directory/name
            if target.exists() and target.read_bytes()!=data:raise ValueError('Conflicting native libraries')
            target.write_bytes(data)


def arguments(version,metadata,classpath):
    options=JVM_OPTIONS.replace('-Xint ','').replace('-Xmx32m','-Xmx256m').split()
    return options+['-Djava.awt.headless=true',f'-Djava.io.tmpdir={GUEST}/tmp',
        f'-Dorg.lwjgl.librarypath={GUEST}/natives','-Dorg.lwjgl.system.allocator=system',
        '-Dorg.lwjgl.util.Debug=true','-cp',':'.join(classpath),metadata['mainClass'],
        '--version',version,'--gameDir',GUEST+'/game','--assetsDir',GUEST+'/assets',
        '--assetIndex',metadata['assetIndex']['id'],'--accessToken','0','--username','NuvoraDemo',
        '--userType','legacy','--versionType','release','--width','854','--height','480','--demo']


def prepare(version,disk,cache,jdk_cache,work,fixtures):
    if disk.exists() or disk.is_symlink():raise ValueError('Refusing to overwrite an existing disk')
    provenance=disk.with_suffix('.provenance.json')
    if provenance.exists() or provenance.is_symlink():raise ValueError('Refusing to overwrite existing provenance')
    target=TARGETS[version];directory=cache/version;directory.mkdir(parents=True,exist_ok=True)
    metadata_file=fetch(cache/(version+'.json'),{'url':target['metadata_url'],'sha256':target['metadata_sha256']},'sha256')
    metadata=json.loads(metadata_file.read_text())
    if metadata['id']!=version or metadata['mainClass']!='net.minecraft.client.main.Main':raise ValueError('Unexpected client entry point')
    client=fetch(directory/'client.jar',metadata['downloads']['client'])
    libraries=[]
    for number,library in enumerate(metadata['libraries']):
        if not linux_library(library):continue
        entry=library['downloads']['artifact'];name=f'lib{number:03}.jar'
        fetch(directory/name,entry);libraries.append({'file':name,'coordinate':library['name'],**entry})
    (directory/'libraries.json').write_text(json.dumps(libraries,indent=2)+'\n')
    asset_index=metadata['assetIndex']
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,26}',asset_index['id']):raise ValueError('Unsafe asset index name')
    index=fetch(directory/'asset-index.json',asset_index)
    disk.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='mc-stage-',dir=work) as temporary:
        temp=pathlib.Path(temporary);staged_disk=temp/'data.img'
        jdk=prepare_jdk(staged_disk,jdk_cache,fixtures,work)
        stage=temp/'minecraft';stage.mkdir()
        for name in ('natives','game','assets/indexes','assets/objects','tmp'):(stage/name).mkdir(parents=True)
        shutil.copyfile(index,stage/'assets/indexes'/(asset_index['id']+'.json'))
        shutil.copyfile(client,stage/'client.jar');classpath=[GUEST+'/client.jar']
        for library in libraries:
            source=directory/library['file']
            if library['coordinate'].endswith(':natives-linux'):extract_natives(source,stage/'natives')
            else:
                shutil.copyfile(source,stage/library['file']);classpath.append(GUEST+'/'+library['file'])
        args=arguments(version,metadata,classpath)
        (stage/'demo.args').write_text('\n'.join(args)+'\n')
        dependencies=[inspect(p) for p in sorted((stage/'natives').glob('*.so'))]
        with staged_disk.open('r+b') as output:
            first,sectors=partition(output);entries,size=import_tree(output,first,sectors,stage,GUEST)
        report={'version':version,'metadata_sha256':target['metadata_sha256'],
            'minimum_java':metadata['javaVersion']['majorVersion'],'probe_java':21,'jdk':jdk,
            'client_sha1':metadata['downloads']['client']['sha1'],'libraries':libraries,
            'native_dependencies':dependencies,'game_tree_entries':entries,'metadata_bytes':size,
            'arguments':args,'asset_objects_installed':False,'external_assets_bytes':asset_index['totalSize'],
            'account_credentials_included':False,'demo_mode':True,'playable':False,
            'status':'Official client launch probe only; no world/input/save acceptance has passed.'}
        # A hard link publishes without a check/replace race or overwriting data.
        staged_report=temp/'report.json';staged_report.write_text(json.dumps(report,indent=2)+'\n')
        os.link(staged_report,provenance)
        try:os.link(staged_disk,disk)
        except BaseException:
            provenance.unlink();raise
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version',choices=TARGETS,default='1.21.1')
    parser.add_argument('--disk',type=pathlib.Path)
    parser.add_argument('--cache',type=pathlib.Path,default=ROOT/'build/minecraft-probe/downloads')
    parser.add_argument('--jdk-cache',type=pathlib.Path,default=ROOT/'build/openjdk-probe/downloads')
    parser.add_argument('--work',type=pathlib.Path,default=ROOT/'build/minecraft-probe')
    options=parser.parse_args()
    for path in (options.work,options.cache,options.jdk_cache):path.mkdir(parents=True,exist_ok=True)
    disk=options.disk or options.work/options.version/'data.img'
    report=prepare(options.version,disk,options.cache,options.jdk_cache,options.work,ROOT/'compat/fixtures/dynamic')
    print(json.dumps({k:report[k] for k in ('version','minimum_java','probe_java','external_assets_bytes','playable','status')},indent=2))
    print('Private launch probe disk:',disk)


if __name__=='__main__':main()
