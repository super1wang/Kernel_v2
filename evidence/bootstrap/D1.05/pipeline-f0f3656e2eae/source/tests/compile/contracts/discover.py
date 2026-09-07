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
    for name in ('binary','output','includes','generator','platform','toolset','sdk','config','work','runtime-dir'):
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
    def quote(value):
        return '[==['+str(value)+']==]'
    lines=[]
    for name in names:
        argv=[args.binary,name]
        if name.startswith('T02.contracts.') or name in ('T05.contracts.typed_binding_fingerprint','T06.contracts.executor_reject_exception_cleanup'):
            argv=[sys.executable,'-X','utf8',str(Path(__file__).with_name('verify_children.py')),'--case',name]
            for key,value in vars(args).items():
                if key!='output': argv+=['--'+key.replace('_','-'),value]
        lines.append('add_test('+quote(name)+' '+' '.join(map(quote,argv))+')')
        timeout=600 if name.startswith('T02.contracts.') or name in ('T05.contracts.typed_binding_fingerprint','T06.contracts.executor_reject_exception_cleanup') else 180
        lines.append('set_tests_properties('+quote(name)+' PROPERTIES TIMEOUT '+str(timeout)+' LABELS "D1.02" ENVIRONMENT "MSBUILDDISABLENODEREUSE=1" ENVIRONMENT_MODIFICATION '+quote('PATH=path_list_prepend:'+str(runtime))+')')
    out.write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
    print(json.dumps({'actual_cases':names,'count':len(names)}))
if __name__=='__main__':
    main()
