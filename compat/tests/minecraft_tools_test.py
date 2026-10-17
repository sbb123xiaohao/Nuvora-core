#!/usr/bin/env python3
"""Check the download/extraction boundary without network or game binaries."""
import hashlib
import io
import json
import pathlib
import sys
import tempfile
import unittest
import zipfile
from unittest import mock

ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'compat/tools'))
import prepare_minecraft_probe as probe
import minecraft_client_probe as client_probe


class ProbeTools(unittest.TestCase):
    def test_official_urls(self):
        for value in ('http://libraries.minecraft.net/a','https://other.invalid/a',
                      'https://libraries.minecraft.net:444/a','https://user@libraries.minecraft.net/a',
                      'file:///etc/passwd','https://libraries.minecraft.net.evil.invalid/a'):
            with self.assertRaises(ValueError):probe.official_url(value)
        self.assertEqual(probe.official_url('https://libraries.minecraft.net/a'),'https://libraries.minecraft.net/a')
        with self.assertRaises(ValueError):
            probe.OfficialRedirect().redirect_request(None,None,302,'',{},'http://localhost/secret')

    def test_rules(self):
        def library(rules):return {'rules':rules,'downloads':{'artifact':{}}}
        self.assertTrue(probe.linux_library(library([])))
        self.assertFalse(probe.linux_library(library([{'action':'allow','os':{'name':'windows'}}])))
        self.assertFalse(probe.linux_library(library([{'action':'allow','os':{'arch':'aarch64'}}])))
        self.assertFalse(probe.linux_library(library([{'action':'allow','features':{'custom':True}}])))
        self.assertFalse(probe.linux_library(library([{'action':'allow'},{'action':'disallow','os':{'name':'linux'}}])))
        self.assertTrue(probe.linux_library(library([{'action':'disallow'},{'action':'allow','os':{'name':'linux'}}])))

    def test_download_transaction(self):
        body=b'verified original bytes';entry={'url':'https://libraries.minecraft.net/a','size':len(body),
                                             'sha1':hashlib.sha1(body).hexdigest()}
        with tempfile.TemporaryDirectory() as directory:
            path=pathlib.Path(directory)/'artifact';path.write_bytes(b'old cache')
            for invalid in (body+b'overrun',b'bad digest',body[:-1]):
                with mock.patch.object(probe.urllib.request,'build_opener') as opener:
                    opener.return_value.open.return_value=io.BytesIO(invalid)
                    with self.assertRaises(ValueError):probe.fetch(path,entry)
                self.assertEqual(path.read_bytes(),b'old cache')
                self.assertEqual([p.name for p in path.parent.iterdir()],['artifact'])
            with mock.patch.object(probe.urllib.request,'build_opener') as opener:
                opener.return_value.open.return_value=io.BytesIO(body);probe.fetch(path,entry)
            self.assertEqual(path.read_bytes(),body)
            with mock.patch.object(probe.urllib.request,'build_opener',side_effect=AssertionError('Network on cache hit')):
                probe.fetch(path,entry)
            link=path.parent/'link';link.symlink_to(path)
            with self.assertRaises(ValueError):probe.fetch(link,entry)

    def test_native_extraction(self):
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory);native=root/'natives';native.mkdir();jar=root/'native.jar'
            elf=b'\x7fELF\x02\x01\x01'+bytes(57)
            for name in ('../escape.so','/absolute.so','folder/../../escape.so','folder\\escape.so','x'*32+'.so'):
                with zipfile.ZipFile(jar,'w') as archive:archive.writestr(name,elf)
                with self.assertRaises(ValueError):probe.extract_natives(jar,native)
                self.assertFalse(list(native.iterdir()))
            with zipfile.ZipFile(jar,'w') as archive:
                member=zipfile.ZipInfo('link.so');member.external_attr=0o120777<<16;archive.writestr(member,elf)
            with self.assertRaises(ValueError):probe.extract_natives(jar,native)
            with zipfile.ZipFile(jar,'w') as archive:archive.writestr('linux/x64/library.so',elf)
            probe.extract_natives(jar,native);self.assertEqual((native/'library.so').read_bytes(),elf)
            with zipfile.ZipFile(jar,'w') as archive:archive.writestr('library.so',elf+b'different')
            with self.assertRaises(ValueError):probe.extract_natives(jar,native)
            self.assertEqual((native/'library.so').read_bytes(),elf)

    def test_existing_disk_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory);disk=root/'data.img';disk.write_bytes(b'user data')
            with mock.patch.object(probe,'fetch',side_effect=AssertionError('Unexpected download')):
                with self.assertRaises(ValueError):probe.prepare('1.21.1',disk,root,root,root,root)
            self.assertEqual(disk.read_bytes(),b'user data')

    def test_demo_arguments(self):
        args=probe.arguments('1.21.1',{'mainClass':'net.minecraft.client.main.Main','assetIndex':{'id':'17'}},['/home/client.jar'])
        self.assertIn('-Xmx256m',args);self.assertNotIn('-Xint',args)
        self.assertIn('--demo',args);self.assertEqual(args[args.index('--accessToken')+1],'0')
        self.assertEqual(args[args.index('-cp')+1],'/home/client.jar')

    def test_probe_cleanup_and_acceptance(self):
        class Guest:
            def __init__(self,*args,**kwargs):self.output=bytearray()
            def pump(self,*args,**kwargs):pass
            def send(self,line,*args,**kwargs):
                out='Process 2 exited 254\n' if line.startswith('compat') else 'filesystem nodes: 20\n'
                self.output.extend(out.encode());self.pump();return out
            def text(self):return self.output.decode()
            def close(self,**kwargs):self.pump(0) # Real VM.close pumps after closing the streaming context.
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory);disk=root/'data.img';disk.touch();log=root/'client.log'
            disk.with_suffix('.provenance.json').write_text(json.dumps({'version':'1.21.1'}))
            args=['probe','--disk',str(disk),'--output',str(log),'--require-playable']
            with mock.patch.object(client_probe,'VM',Guest),mock.patch.object(sys,'argv',args),mock.patch('builtins.print'):
                self.assertEqual(client_probe.main(),2)
            report=json.loads(log.with_suffix('.json').read_text())
            self.assertEqual(report['guest_exit_status'],254)
            self.assertTrue(report['native_shell_recovered']);self.assertFalse(report['playable'])
            self.assertTrue(all(value=='not verified' for value in report['acceptance'].values()))
            self.assertIn('exited 254',log.read_text())


if __name__=='__main__':unittest.main()
