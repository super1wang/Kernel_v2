"""隔离编译拒绝与fail-fast，保存每次实际子进程原始字节。"""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import uuid
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
from tools.evidence.common import save_json,sha_file


def main():
    parser=argparse.ArgumentParser()
    for name in ('case','binary','includes','generator','platform','toolset','sdk','config','work','runtime-dir'):
        parser.add_argument('--'+name,required=True)
    args=parser.parse_args()
    os.environ['MSBUILDDISABLENODEREUSE']='1'
    runtime=Path(args.runtime_dir).resolve()
    if not (runtime/'cl.exe').is_file():
        raise ValueError('configured MSVC runtime directory is invalid')
    os.environ['PATH']=str(runtime)+os.pathsep+os.environ.get('PATH','')
    out=Path(args.work)/uuid.uuid4().hex;out.mkdir(parents=True,exist_ok=False)
    commands=[]
    def command(name,argv):
        stdout=out/(name+'-stdout.log');stderr=out/(name+'-stderr.log')
        row=execute(argv,ROOT,stdout,stderr,120)
        row['raw']=[{'path':p.name,'sha256':sha_file(p),'size':p.stat().st_size} for p in (stdout,stderr)]
        commands.append(row);save_json(out/'commands.json',{'case':args.case,'commands':commands})
        return row,stdout.read_bytes()+stderr.read_bytes()
    def good(row):
        return row['status']=='Exited' and row['exit_code']==0 and row['process_tree']['active_after']==0
    row,_=command('native',[args.binary,args.case]);assert good(row)
    if args.case=='T04.foundation.invariant_fail_fast':
        def terminated(row,raw):
            return (row['status'] in ('Exited','Crashed') and row.get('observed_exit_code')==3
                    and row['process_tree']['active_after']==0 and not row['process_tree']['terminated_owned_job']
                    and b'ock foundation invariant requested' in raw
                    and b'ock foundation invariant unexpected-return' not in raw)
        control,control_raw=command('invariant-noop',[args.binary,'--fail-fast-noop'])
        assert good(control) and b'ock foundation invariant unexpected-return' in control_raw
        assert not terminated(control,control_raw), 'no-op invariant must not be accepted as fail-fast'
        row,raw=command('invariant',[args.binary,'--fail-fast'])
        assert terminated(row,raw)
    elif args.case=='T04.foundation.static_error_domain':
        header='#include <ock/foundation/foundation.hpp>\nusing namespace ock::foundation;\n'
        sources={'positive':'inline constexpr ErrorDomain domain{"static"}; constexpr auto code=ErrorCode::make<domain>(3); static_assert(code.value()==3);',
            'stack':'void test(){ErrorDomain domain{"stack"}; auto code=ErrorCode::make<domain>(3);}',
            'temporary':'auto code=ErrorCode::make<ErrorDomain{"temporary"}>(3);',
            'pointer':'inline constexpr ErrorDomain domain{"static"}; auto code=ErrorCode(&domain,3);'}
        includes=[p for p in args.includes.split('|') if p]
        lines=['cmake_minimum_required(VERSION 3.25)','project(FoundationDomainRejection LANGUAGES CXX)',
               'set(CMAKE_CXX_STANDARD 20)','set(CMAKE_CXX_STANDARD_REQUIRED ON)']
        for name,text in sources.items():
            (out/(name+'.cpp')).write_text(header+text+'\n',encoding='utf-8',newline='\n')
            lines+=['add_library('+name+' OBJECT EXCLUDE_FROM_ALL '+name+'.cpp)',
                'target_compile_options('+name+' PRIVATE /EHsc /utf-8 /Zc:__cplusplus /permissive-)',
                'target_include_directories('+name+' PRIVATE '+' '.join('[==['+p.replace('\\','/')+']==]' for p in includes)+')']
        (out/'CMakeLists.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
        argv=['cmake','-S',str(out),'-B',str(out/'build'),'-G',args.generator]
        if args.platform:argv+=['-A',args.platform]
        if args.toolset:argv+=['-T',args.toolset]
        if args.sdk:argv+=['-DCMAKE_SYSTEM_VERSION='+args.sdk]
        row,_=command('configure',argv);assert good(row)
        row,_=command('positive',['cmake','--build',str(out/'build'),'--config',args.config,'--target','positive']);assert good(row)
        for name in ('stack','temporary','pointer'):
            row,raw=command(name,['cmake','--build',str(out/'build'),'--config',args.config,'--target',name])
            assert row['status']=='Exited' and row['exit_code']!=0 and row['process_tree']['active_after']==0
            assert re.search(rb'error C[0-9]{4}',raw), 'expected actual MSVC compile diagnostic'
    else:
        raise ValueError('unknown child case')
    print(json.dumps({'case':args.case,'status':'Passed','evidence':str(out)},ensure_ascii=False))
if __name__=='__main__':
    main()
