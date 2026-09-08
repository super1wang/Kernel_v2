"""从实际原生runner取得注册用例，不读取或生成期望集合。"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    for name in ('binary','output','includes','generator','platform','toolset','sdk','config','work','runtime-dir','profile'):
        parser.add_argument('--'+name, required=True)
    args=parser.parse_args()
    runtime=Path(args.runtime_dir).resolve()
    if not (runtime/'cl.exe').is_file():
        raise ValueError('configured MSVC runtime directory is invalid')
    env=dict(os.environ);env['PATH']=str(runtime)+os.pathsep+env.get('PATH','')
    result=subprocess.run([args.binary,'--list'],capture_output=True,check=True,env=env)
    out=Path(args.output);out.parent.mkdir(parents=True,exist_ok=True)
    (out.parent/'contracts-discovery-stdout.log').write_bytes(result.stdout)
    (out.parent/'contracts-discovery-stderr.log').write_bytes(result.stderr)
    names=result.stdout.decode('utf-8').splitlines()
    if not names or len(names)!=len(set(names)) or not all(re.fullmatch(r'T(?:02|05|06|19|24)[.]contracts[.][a-z_0-9]+',name) for name in names):
        raise ValueError('invalid actual CoreContracts case registry')
    from fixture import check_discovered
    check_discovered(names)
    def quote(value):
        return '[==['+str(value)+']==]'
    # 此随机值在实际 CTest 读取期生成，-N 的值不持久化也不被实际运行复用。
    lines=['string(RANDOM LENGTH 24 ALPHABET 0123456789abcdef _contracts_run_id)',
           'set(_contracts_fixture "'+str(Path(args.work).resolve()).replace('\\','/')+'/fixtures/${_contracts_run_id}")']
    fixture_args=[]
    for key,value in vars(args).items():
        if key!='output': fixture_args += ['--'+key.replace('_','-'),value]
    runtime_args=' '+quote('--fixture')+' "${_contracts_fixture}" '+quote('--run-id')+' "${_contracts_run_id}"'
    setup='T02.contracts.compile_fixture_setup'
    guards='T02.contracts.compile_fixture_guards'
    lines.append('add_test('+quote(setup)+' '+' '.join(map(quote,[sys.executable,'-X','utf8',str(Path(__file__).with_name('fixture.py')),*fixture_args]))+runtime_args+')')
    lines.append('set_tests_properties('+quote(setup)+' PROPERTIES FIXTURES_SETUP contracts_compile TIMEOUT 300 LABELS "Tools;CompileContracts" ENVIRONMENT_MODIFICATION '+quote('PATH=path_list_prepend:'+str(runtime))+')')
    lines.append('add_test('+quote(guards)+' '+' '.join(map(quote,[sys.executable,'-X','utf8','-m','unittest','discover','-s',str(Path(__file__).resolve().parents[2]/'tools/compile'),'-v']))+')')
    lines.append('set_tests_properties('+quote(guards)+' PROPERTIES TIMEOUT 60 LABELS "Tools;CompileContracts")')
    for name in names:
        argv=[args.binary,name]
        if name.startswith('T02.contracts.') or name in ('T05.contracts.typed_binding_fingerprint','T06.contracts.executor_reject_exception_cleanup'):
            argv=[sys.executable,'-X','utf8',str(Path(__file__).with_name('verify_children.py')),'--case',name]
            for key,value in vars(args).items():
                if key!='output': argv+=['--'+key.replace('_','-'),value]
        wrapped=name.startswith('T02.contracts.') or name in ('T05.contracts.typed_binding_fingerprint','T06.contracts.executor_reject_exception_cleanup')
        lines.append('add_test('+quote(name)+' '+' '.join(map(quote,argv))+(runtime_args if wrapped else '')+')')
        timeout=600 if name.startswith('T02.contracts.') or name in ('T05.contracts.typed_binding_fingerprint','T06.contracts.executor_reject_exception_cleanup') else 180
        lines.append('set_tests_properties('+quote(name)+' PROPERTIES TIMEOUT '+str(timeout)+' LABELS "D1.02" ENVIRONMENT "MSBUILDDISABLENODEREUSE=1" ENVIRONMENT_MODIFICATION '+quote('PATH=path_list_prepend:'+str(runtime))+')')
        if wrapped and name!='T06.contracts.executor_reject_exception_cleanup':
            lines.append('set_tests_properties('+quote(name)+' PROPERTIES FIXTURES_REQUIRED contracts_compile RESOURCE_LOCK contracts_compile_build)')
    out.write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
    print(json.dumps({'actual_cases':names,'count':len(names)}))
if __name__=='__main__':
    main()
