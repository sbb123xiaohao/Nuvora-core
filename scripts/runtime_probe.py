#!/usr/bin/env python3
"""Inspect real ELF runtime requirements without executing its code.

Format eligibility is not runtime compatibility. Missing threads, shared
libraries, signals and graphics still prevent OpenJDK/Minecraft startup.
"""
import argparse
import hashlib
import json
import pathlib
import struct


def inspect(path):
    if not path.is_file() or path.stat().st_size>512*1024*1024:raise ValueError('Expected regular ELF, at most 512 MiB')
    data=path.read_bytes()
    if len(data)<64 or data[:7]!=b'\x7fELF\x02\x01\x01':raise ValueError('Expected little-endian ELF64')
    kind,machine=struct.unpack_from('<HH',data,16)
    entry,phoff=struct.unpack_from('<QQ',data,24);width,count=struct.unpack_from('<HH',data,54)
    if width!=56 or not 0<count<=256 or phoff>len(data) or count>(len(data)-phoff)//56:raise ValueError('Invalid program headers')
    headers=[struct.unpack_from('<IIQQQQQQ',data,phoff+i*56) for i in range(count)]
    for p in headers:
        if p[2]>len(data) or p[5]>len(data)-p[2]:raise ValueError('Program header outside file')
    def mapped(address,size):
        for p in headers:
            if p[0]==1 and address>=p[3] and address-p[3]<=p[5] and size<=p[5]-(address-p[3]):
                at=p[2]+address-p[3];return data[at:at+size]
        raise ValueError('Dynamic data outside file-backed LOAD')
    interpreter=None;needed=[];tls=any(p[0]==7 for p in headers)
    for p in headers:
        if p[0]==3:
            raw=data[p[2]:p[2]+p[5]]
            if not raw or raw[-1] or len(raw)>4096:raise ValueError('Invalid interpreter')
            interpreter=raw[:-1].decode('utf-8')
        if p[0]!=2:continue
        if p[5]%16 or p[5]>1024*1024:raise ValueError('Invalid dynamic table')
        strings=size=0;names=[]
        for at in range(p[2],p[2]+p[5],16):
            tag,value=struct.unpack_from('<qQ',data,at)
            if not tag:break
            if tag==5:strings=value
            if tag==10:size=value
            if tag==1:names.append(value)
        if names:
            if not size or size>16*1024*1024:raise ValueError('Invalid string table')
            table=mapped(strings,size)
            for at in names:
                if at>=len(table) or b'\0' not in table[at:]:raise ValueError('Invalid NEEDED name')
                needed.append(table[at:].split(b'\0',1)[0].decode('utf-8'))
    blockers=[]
    if machine!=62:blockers.append('only x86-64 is implemented')
    if kind!=3:blockers.append('compatibility loader currently requires static PIE (ET_DYN)')
    if count>32:blockers.append('more than 32 program headers')
    if interpreter:blockers.append('ELF interpreter missing: '+interpreter)
    if needed:blockers.append('shared-library loader missing: '+', '.join(needed))
    if any(p[0]==1 and p[1]&3==3 for p in headers):blockers.append('writable executable segment is refused')
    if not any(p[0]==1 and p[1]&1 and p[3]<=entry<p[3]+p[6] for p in headers):blockers.append('entry does not select executable code')
    return {'file':path.name,'sha256':hashlib.sha256(data).hexdigest(),'machine':machine,'elf_type':kind,
            'interpreter':interpreter,'needed':needed,'tls_template':tls,'loader_blockers':blockers,
            'note':'No code executed; an empty blocker list does not prove runtime support.'}


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('files',nargs='+',type=pathlib.Path)
    args=parser.parse_args()
    try:results=[inspect(p) for p in args.files]
    except (OSError,UnicodeError,ValueError,struct.error) as error:parser.exit(2,str(error)+'\n')
    print(json.dumps({'files':results,'platform_missing':['dynamic loader/shared libraries','clone/threads and blocking synchronization',
          'Linux signals and exception delivery','file-backed mappings and reserved address space','JNI/LWJGL/GLFW/OpenGL backends'],
          'minecraft_client_started':False},indent=2))

if __name__=='__main__':main()
