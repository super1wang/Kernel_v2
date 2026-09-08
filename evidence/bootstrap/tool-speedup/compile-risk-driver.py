"""P0夹具配置传播专项：每配置只构建runner与read_shape包装。"""
import argparse
import json
from pathlib import Path
import sys
import uuid
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
from tools.evidence.common import save_json,sha_file,junit_cases


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--config',choices=['Debug','Release'],required=True)
    p.add_argument('--asan',choices=['ON','OFF'],required=True)
    p.add_argument('--verify-existing')
    a=p.parse_args()
    out=ROOT/'evidence/bootstrap/tool-speedup'/('compile-risk-'+a.config.lower()+'-'+a.asan.lower()+'-'+uuid.uuid4().hex[:10])
    out.mkdir(parents=True)
    build=ROOT/'build'/out.name
    commands=[]
    def run(name,argv):
        paths=[out/(name+'-'+x+'.log') for x in ('stdout','stderr')]
        row=execute(argv,ROOT,*paths,600)
        row['raw']=[{'path':x.name,'sha256':sha_file(x),'size':x.stat().st_size} for x in paths]
        commands.append(row);save_json(out/'commands.json',commands)
        if not(row['status']=='Exited' and row['exit_code']==0 and row['process_tree']['active_after']==0
               and row['process_tree']['assigned_before_resume'] and not row['process_tree']['terminated_owned_job']):
            raise ValueError('实际专项命令失败，保留原始证据：'+name)
    if a.verify_existing:
        previous=Path(a.verify_existing).resolve()
        prior_commands=json.loads((previous/'commands.json').read_text(encoding='utf-8'))
        configure=prior_commands[0]['argv']
        build=Path(configure[configure.index('-B')+1])
        (out/'junit.xml').write_bytes((previous/'junit.xml').read_bytes())
        save_json(out/'read-only-source.json',{'path':str(previous),'commands_sha256':sha_file(previous/'commands.json'),
                  'junit_sha256':sha_file(previous/'junit.xml'),'reason':'原读器误写EnableASAN；真实属性EnableAsan，仅更正只读核验，不重跑编译'})
    else:
        run('configure',['cmake','-S',str(ROOT),'-B',str(build),'-G','Visual Studio 17 2022','-A','x64',
                     '-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
                     '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake'),
                     '-DOCK_DEPENDENCY_CACHE='+str(ROOT/'build/d0.06-a/cache'),'-DOCK_DEPENDENCIES_OFFLINE=ON',
                     '-DOCK_BUILD_CORE_CONTRACTS_TESTS=ON','-DOCK_ENABLE_ASAN='+a.asan])
        run('build',['cmake','--build',str(build),'--config',a.config,'--target','ock_contracts_tests','--parallel','2','--','/nr:false'])
    contracts=build/'tests/compile/contracts'
    profile_path=contracts/('compile-profile-'+a.config+'.txt')
    profile=dict(line.split('=',1) for line in profile_path.read_text().splitlines())
    expected_crt='MultiThreadedDebugDLL' if a.config=='Debug' else 'MultiThreadedDLL'
    if profile['config']!=a.config or profile['asan']!=a.asan or profile['crt']!=expected_crt:
        raise ValueError('实际父target配置/CRT/ASan身份不符')
    if ('/fsanitize=address' in profile['options'])!=(a.asan=='ON'):
        raise ValueError('实际父target ASan编译选项不符')
    if a.asan=='ON' and '/RTC' in profile['config_flags']:
        raise ValueError('ASan实际配置仍有不兼容RTC')
    if not a.verify_existing:
        run('ctest',['ctest','--test-dir',str(contracts),'-C',a.config,'-R','^T02[.]contracts[.]read_shape$',
                    '--output-on-failure','--output-junit',str(out/'junit.xml')])
    rows=junit_cases(out/'junit.xml')
    if sorted(x['name'] for x in rows)!=['T02.contracts.compile_fixture_setup','T02.contracts.read_shape'] or not all(x['status']=='Passed' for x in rows):
        raise ValueError('专项缺少精确两项实际执行')
    suite=ET.parse(out/'junit.xml').getroot()
    setup=json.loads(next(x for x in suite.findall('testcase') if x.attrib['name'].endswith('compile_fixture_setup')).findtext('system-out'))
    directory=Path(setup['fixture'])
    ready=json.loads((directory/'ready.json').read_text(encoding='utf-8'))
    if ready['identity']['profile']!=profile:
        raise ValueError('fixture配置身份没有传播父配置')
    ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
    project_checks=[]
    for target,item in ready['identity']['targets'].items():
        if item['case']!='read_shape':continue
        project=directory/'build'/(target+'.vcxproj')
        tree=ET.parse(project)
        crts={x.text for x in tree.findall('.//m:RuntimeLibrary',ns)}
        asan={x.text for x in tree.findall('.//m:EnableAsan',ns)}
        if crts!={expected_crt} or ('true' in asan)!=(a.asan=='ON'):
            raise ValueError('实际生成target CRT/ASan属性不符')
        project_checks.append({'target':target,'name':item['name'],'runtime_library':sorted(crts),
                               'enable_asan':sorted(asan),'path':str(project),'sha256':sha_file(project)})
    save_json(out/'result.json',{'status':'Passed','scope':'配置传播专项，非包级验收','config':a.config,
                                'asan':a.asan,'profile':profile,'projects':project_checks,'executed':rows,
                                'fixture':str(directory),'build':str(build),'driver_sha256':sha_file(__file__)})
    print(json.dumps({'evidence':str(out),'status':'Passed'},ensure_ascii=False))


if __name__=='__main__':main()
