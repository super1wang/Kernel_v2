"""局部开发工具物化不能接受篡改、外来文件或路径逃逸。"""
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from concurrent.futures import ThreadPoolExecutor
import threading
from tools.development.msvc_isolation import prepare, resolved_path
from tools.architecture.check import ROOT

class MSVCIsolationTests(unittest.TestCase):
    @staticmethod
    def fixture(root):
        source=root/'installed';source.mkdir();rows=[]
        for name in ('cl.exe','c1.dll','c1xx.dll','c2.dll','link.exe','lib.exe','vctip.exe'):
            p=source/name;p.write_bytes(name.encode());rows.append({'path':name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
        lock=root/'lock.json'
        lock.write_text(json.dumps({'format':'ock.msvc-validation-tools/1','source_hint':str(source),'files':rows[:-1],'excluded':rows[-1:]}),encoding='utf-8')
        return lock

    @unittest.skipUnless(os.name == 'nt', 'Windows path representation')
    def test_extended_drive_is_same_boundary_but_outside_still_rejected(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as temporary:
            root=Path(temporary).resolve();lock=self.fixture(root);original=Path.resolve
            def extended(path,*args,**kwargs):
                value=original(path,*args,**kwargs)
                return Path(chr(92)*2+'?'+chr(92)+str(value)) if path==root/'build/msvc-validation' else value
            with patch.object(Path,'resolve',extended):
                ready=prepare(lock,root)
                self.assertEqual((ready/'bin/cl.exe').read_bytes(),b'cl.exe')
            def outside(path,*args,**kwargs):
                return Path(chr(92)*2+'?'+chr(92)+str(root.parent)) if path==root/'build/msvc-validation' else original(path,*args,**kwargs)
            with patch.object(Path,'resolve',outside):
                with self.assertRaisesRegex(ValueError,'cache escaped workspace'):prepare(lock,root)
            network=chr(92)*2+'?'+chr(92)+'UNC'+chr(92)+'server'+chr(92)+'share'+chr(92)+'cache'
            with patch.object(Path,'resolve',return_value=Path(network)):
                self.assertEqual(resolved_path(Path('unused')),Path(chr(92)*2+'server'+chr(92)+'share'+chr(92)+'cache'))

    def test_concurrent_publish_is_complete_and_existing_metadata_readonly(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as temporary:
            root=Path(temporary).resolve();lock=self.fixture(root)
            barrier=threading.Barrier(8);original=Path.rename
            def simultaneous(stage,target):
                barrier.wait(timeout=20)
                return original(stage,target)
            with patch.object(Path,'rename',simultaneous), ThreadPoolExecutor(max_workers=8) as pool:
                ready=list(pool.map(lambda _:prepare(lock,root),range(8)))
            self.assertEqual(len(set(ready)),1)
            self.assertEqual(list((root/'build/msvc-validation').glob('.partial-*')),[])
            before=(ready[0]/'inputs.json').stat().st_mtime_ns
            prepare(lock,root)
            self.assertEqual((ready[0]/'inputs.json').stat().st_mtime_ns,before)
            (ready[0]/'inputs.json').write_bytes(b'tampered')
            with self.assertRaisesRegex(ValueError,'metadata differs'):prepare(lock,root)
            self.assertEqual((ready[0]/'inputs.json').read_bytes(),b'tampered')

    def test_copy_is_exact_reusable_and_excludes_only_uploader(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as temporary:
            root=Path(temporary); source=root/'installed';source.mkdir()
            names=['cl.exe','c1.dll','c1xx.dll','c2.dll','link.exe','lib.exe','2052/clui.dll','vctip.exe']
            records=[]
            for name in names:
                p=source/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(name.encode())
                records.append({'path':name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
            lock=root/'lock.json';data={'format':'ock.msvc-validation-tools/1','source_hint':str(source),'files':records[:-1],'excluded':records[-1:]}
            lock.write_text(json.dumps(data),encoding='utf-8')
            ready=prepare(lock,root)
            self.assertFalse((ready/'bin/vctip.exe').exists())
            self.assertEqual((ready/'bin/cl.exe').read_bytes(),b'cl.exe')
            self.assertEqual(ready,prepare(lock,root))
            (ready/'bin/cl.exe').write_bytes(b'changed')
            with self.assertRaises(ValueError): prepare(lock,root)
            self.assertEqual((ready/'bin/cl.exe').read_bytes(),b'changed')
    def test_cached_metadata_hardlink_cannot_overwrite_original(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as temporary:
            root=Path(temporary);source=root/'installed';source.mkdir()
            names=['cl.exe','c1.dll','c1xx.dll','c2.dll','link.exe','lib.exe','vctip.exe']
            records=[]
            for name in names:
                p=source/name;p.write_bytes(name.encode());records.append({'path':name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
            lock=root/'lock.json';data={'format':'ock.msvc-validation-tools/1','source_hint':str(source),'files':records[:-1],'excluded':records[-1:]}
            lock.write_text(json.dumps(data),encoding='utf-8')
            ready=prepare(lock,root)
            (ready/'inputs.json').unlink()
            os.link(source/'cl.exe',ready/'inputs.json')
            with self.assertRaises(ValueError):prepare(lock,root)
            self.assertEqual((source/'cl.exe').read_bytes(),b'cl.exe')

    def test_unlisted_files_and_path_escape_rejected(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as temporary:
            root=Path(temporary);source=root/'installed';source.mkdir()
            names=['cl.exe','c1.dll','c1xx.dll','c2.dll','link.exe','lib.exe','vctip.exe']
            records=[]
            for name in names:
                p=source/name;p.write_bytes(name.encode());records.append({'path':name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
            lock=root/'lock.json';data={'format':'ock.msvc-validation-tools/1','source_hint':str(source),'files':records[:-1],'excluded':records[-1:]}
            lock.write_text(json.dumps(data),encoding='utf-8')
            ready=prepare(lock,root)
            (ready/'bin/vctip.exe').write_bytes(b'unexpected')
            with self.assertRaises(ValueError):prepare(lock,root)
            data['files'][0]['path']='../escape.exe';lock.write_text(json.dumps(data),encoding='utf-8')
            with self.assertRaises(ValueError):prepare(lock,root)
            self.assertFalse((root/'escape.exe').exists())

if __name__=='__main__':unittest.main()
