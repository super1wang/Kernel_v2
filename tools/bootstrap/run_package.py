"""D0.02/D0.03 bootstrap：真实命令/快照/CTest清单，不生成正式门禁或人工批准。"""
import argparse
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import uuid
import xml.etree.ElementTree as ET
import zipfile

ROOT=Path(__file__).resolve().parents[2]

def sha(data):return hashlib.sha256(data).hexdigest()
def now():return datetime.now(timezone.utc).isoformat()
def save(path,data):path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT,text=True,encoding='utf-8').strip()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('manifest',type=Path);args=parser.parse_args()
    manifest_path=args.manifest.resolve()
    if not manifest_path.is_relative_to(ROOT/'tests/manifests'):raise ValueError('manifest outside workspace test manifests')
    spec=json.loads(manifest_path.read_text(encoding='utf-8'))
    if spec['task_id'] not in ('D0.02','D0.03'):raise ValueError('only D0.02/D0.03 bootstrap supported')
    if not spec['commands']:raise ValueError('empty command manifest')
    expected_path=None
    if 'expected_manifest' in spec:
        expected_path=(ROOT/spec['expected_manifest']).resolve()
        if not expected_path.is_relative_to(ROOT/'tests/manifests'):raise ValueError('expected manifest outside test manifests')
        expected=json.loads(expected_path.read_text(encoding='utf-8'))
        if expected['task_id']!=spec['task_id']:raise ValueError('expected task mismatch')
        spec['expected_ctest']=[case['id'] for case in expected['cases']]
    names=spec.get('expected_ctest',[])
    if not names or len(names)!=len(set(names)):raise ValueError('empty or duplicate expected cases')
    commit=git('rev-parse','HEAD');dirty=bool(git('status','--porcelain'))
    run_id=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:12]
    out=ROOT/'evidence/bootstrap'/spec['task_id']/run_id;out.mkdir(parents=True,exist_ok=False)
    selected={manifest_path,Path(__file__).resolve()}
    if expected_path:selected.add(expected_path)
    for pattern in spec['source_patterns']:
        if pattern.endswith('/**'):pattern+='/*'
        selected.update(p for p in ROOT.glob(pattern) if p.is_file() and '__pycache__' not in p.parts and p.suffix not in ('.pyc','.pyo'))
    inputs=[]
    with zipfile.ZipFile(out/'source-inputs.zip','w',zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(selected):
            if not path.resolve().is_relative_to(ROOT):raise ValueError('source outside workspace')
            rel=path.relative_to(ROOT).as_posix();data=path.read_bytes()
            inputs.append({'path':rel,'sha256':sha(data)})
            archive.writestr(rel,data)
    save(out/'source-inputs.json',inputs)
    result={'format':'ock.bootstrap-package/1','task_id':spec['task_id'],'run_id':run_id,'started_at':now(),
      'source':{'commit':commit,'dirty':dirty,'inputs_sha256':sha(json.dumps(inputs,sort_keys=True,separators=(',',':')).encode('utf-8')),
                'archive_sha256':sha((out/'source-inputs.zip').read_bytes())},
      'profile':spec['profile'],'manifest_sha256':sha(manifest_path.read_bytes()),
      'environment':{'python':platform.python_version(),'python_executable':sys.executable,'platform':platform.platform()},
      'commands':[],'tests':{'expected':spec.get('expected_ctest',[]),'discovered':[],'executed':[]},
      'outputs':[],'formal_evidence_status':'Incomplete',
      'limitations':['G0前由D0.06正式采集器复核；当前不生成包级Passed。','没有实现完整Conformance/重复轮次/故障树/正式依赖锁门禁。','人工评审独立，运行结果不能替代批准。']}
    executables=[]
    for name in sorted({argv[0] for argv in spec['commands']}):
        resolved=sys.executable if name=='python' else shutil.which(name)
        executables.append({'name':name,'path':resolved,'sha256':sha(Path(resolved).read_bytes()) if resolved else None})
    result['environment']['command_executables']=executables
    errors=[]
    for path in spec.get('required_artifacts',[]):
        if not (ROOT/path).is_file():errors.append('missing required artifact: '+path)
    for i,raw_argv in enumerate(spec['commands'],1):
        argv=list(raw_argv)
        if argv[0]=='python':argv=[sys.executable,'-X','utf8',*argv[1:]]
        is_discovery=argv[0]=='ctest' and '--show-only=json-v1' in argv
        is_test=argv[0]=='ctest' and not is_discovery
        junit=out/f'{i:02}-junit.xml'
        if is_test:argv+=['--output-junit',str(junit)]
        command={'argv':argv,'cwd':str(ROOT),'started_at':now()}
        try:
            p=subprocess.run(argv,cwd=ROOT,capture_output=True,timeout=240)
            stdout,stderr=p.stdout,p.stderr;command.update(exit_code=p.returncode,status='Exited')
        except subprocess.TimeoutExpired as exc:
            stdout,stderr=exc.stdout or b'',exc.stderr or b''
            command.update(exit_code=None,status='Timeout',process_disposition='直接子进程由subprocess.run kill/wait；未验证后代排空，不可通过')
        except OSError as exc:
            stdout,stderr=b'',str(exc).encode('utf-8');command.update(exit_code=None,status='LaunchFailed')
        command['finished_at']=now();command['raw']=[]
        for stream,data in [('stdout',stdout),('stderr',stderr)]:
            file=out/f'{i:02}-{stream}.log';file.write_bytes(data)
            command['raw'].append({'path':file.name,'sha256':sha(data),'size':len(data),'truncated':False})
        result['commands'].append(command)
        print(f"{spec['task_id']} command {i}: {command['status']} exit={command['exit_code']}",flush=True)
        if command['status']!='Exited' or command['exit_code']!=0:
            errors.append(f'command {i} failed');break
        try:
            if is_discovery:
                names=[test['name'] for test in json.loads(stdout)['tests']]
                result['tests']['discovered']=names
                if len(names)!=len(set(names)) or set(names)!=set(result['tests']['expected']):
                    errors.append('CTest discovery differs from independently maintained expected');break
            if is_test:
                if not junit.is_file():raise ValueError('missing current JUnit')
                tests=ET.parse(junit).getroot().findall('.//testcase')
                result['tests']['executed']=[{'name':t.attrib['name'],'status':'Failed' if t.find('failure') is not None or t.find('error') is not None else 'Skipped' if t.find('skipped') is not None else 'Passed'} for t in tests]
                actual=result['tests']['executed']
                if not actual or len(actual)!=len(result['tests']['expected']) or {t['name'] for t in actual}!=set(result['tests']['expected']) or any(t['status']!='Passed' for t in actual):
                    errors.append('JUnit missing/extra/failed/skipped required cases')
                command['junit']={'path':junit.name,'sha256':sha(junit.read_bytes())}
        except (ValueError,KeyError,ET.ParseError) as exc:errors.append(str(exc))
    for entry in inputs:
        path=ROOT/entry['path']
        if not path.is_file() or sha(path.read_bytes())!=entry['sha256']:errors.append('source changed during run: '+entry['path'])
    if result['tests']['expected'] and not result['tests']['executed']:errors.append('expected CTest cases were not executed')
    for pattern in spec.get('build_outputs',[]):
        paths=list(ROOT.glob(pattern))
        if not paths:errors.append('missing build output: '+pattern)
        for path in paths:
            if path.is_file():result['outputs'].append({'path':path.relative_to(ROOT).as_posix(),'sha256':sha(path.read_bytes()),'size':path.stat().st_size})
    result['finished_at']=now();result['errors']=errors;result['collection_status']='Captured' if not errors else 'FailedOrIncomplete'
    save(out/'commands.json',result)
    print(out.relative_to(ROOT).as_posix(),flush=True)
    if errors:print(json.dumps(errors,ensure_ascii=False))
    return bool(errors)

if __name__=='__main__':sys.exit(main())
