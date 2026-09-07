import sys,json,hashlib,tempfile
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.development.msvc_isolation import prepare
with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as td:
 root=Path(td).resolve();source=root/'installed';source.mkdir();rows=[]
 for name in ('cl.exe','c1.dll','c1xx.dll','c2.dll','link.exe','lib.exe','vctip.exe'):
  p=source/name;p.write_bytes(name.encode());rows.append({'path':name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
 lock=root/'lock.json';lock.write_text(json.dumps({'format':'ock.msvc-validation-tools/1','source_hint':str(source),'files':rows[:-1],'excluded':rows[-1:]}),encoding='utf-8')
 original=Path.resolve
 def extended(path,*args,**kwargs):
  value=original(path,*args,**kwargs)
  return Path(chr(92)*2+'?'+chr(92)+str(value)) if path==root/'build/msvc-validation' else value
 with patch.object(Path,'resolve',extended):
  ready=prepare(lock,root)
  assert (ready/'bin/cl.exe').read_bytes()==b'cl.exe'
 print('equivalent extended drive path accepted')
