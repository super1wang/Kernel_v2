"""B4 新公开头与生产 CpuPool 的真实迁移安装消费者。"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import uuid
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tests.install_consumer import verify_baseline as driver

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build',required=True);parser.add_argument('--config',required=True);args=parser.parse_args()
    producer=Path(args.build).resolve()
    if not producer.is_relative_to(ROOT/'build'):raise ValueError('workspace producer required')
    cache=dict(m.groups() for line in (producer/'CMakeCache.txt').read_text(encoding='utf-8').splitlines() if (m:=re.fullmatch(r'([^#/][^:]*):[^=]+=(.*)',line)))
    profile=next((producer/'CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
    compiler=re.search(r'set\(CMAKE_CXX_COMPILER "([^"]+)"\)',profile.read_text(encoding='utf-8')).group(1)
    os.environ['PATH']=str(Path(compiler).parent)+os.pathsep+os.environ.get('PATH','')
    acquisition=json.loads((producer/'dependency-acquisition.json').read_text(encoding='utf-8'))
    assert set(acquisition['selected'])=={'expected','jsoncons','asio','cli11','thread_pool'},acquisition['selected']
    work=Path(tempfile.mkdtemp(prefix='b4-install-',dir=ROOT/'build'))
    evidence=ROOT/'evidence/bootstrap/B4'/('sdk-install-'+uuid.uuid4().hex[:10]);evidence.mkdir(parents=True)
    driver._work=evidence;driver._records=[];run=driver.run
    original,relocated=work/'original',work/'relocated'
    run(['cmake','--install',str(producer),'--config',args.config,'--prefix',str(original)])
    shutil.copytree(original,relocated)
    # 两个路径均为本次创建的目录；移走原安装，验证消费不依赖原位置。
    hidden=work/'unavailable-original'
    if not original.resolve().is_relative_to(work) or not hidden.resolve().is_relative_to(work):raise ValueError('relocation boundary')
    original.rename(hidden)
    for p in relocated.rglob('*.cmake'):
        data=p.read_text(encoding='utf-8').replace('\\','/').casefold()
        assert all(value not in data for value in (ROOT.as_posix().casefold(),original.as_posix().casefold(),'bs_thread_pool','ock_dep_thread_pool','jsoncons','ock_dep_asio'))
    manifest=json.loads((relocated/'share/ock/sdk_api_manifest.json').read_text(encoding='utf-8'))
    assert manifest['installation_profile'] in ('B4Subset','B5Subset','B6Subset') and manifest['targets']['Adapter::CpuPool']['kind']=='STATIC_LIBRARY'
    source=work/'consumer-source';shutil.copytree(ROOT/'tests/install_consumer/b4',source)
    new_headers=[('CoreContracts','ock/contracts/executor.hpp'),('Runtime','ock/runtime/scheduler.hpp'),('Runtime','ock/runtime/resources.hpp'),('Adapter::CpuPool','ock/adapters/cpu_pool/cpu_pool.hpp')]
    with (source/'CMakeLists.txt').open('a',encoding='utf-8') as cmake:
        for i,(owner,header) in enumerate(new_headers):
            (source/f'h{i}.cpp').write_text(f'#include <{header}>\n',encoding='utf-8')
            cmake.write(f'add_library(h{i} OBJECT h{i}.cpp)\ntarget_link_libraries(h{i} PRIVATE OCK::{owner})\n')
    command=['cmake','-S',str(source),'-B',str(work/'consumer'),'-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake',f'-DCMAKE_PREFIX_PATH={relocated}']
    if cache.get('OCK_ENABLE_ASAN')=='ON':command+=['-DCMAKE_CXX_FLAGS=/fsanitize=address','-DCMAKE_CXX_FLAGS_DEBUG='+re.sub(r'/RTC[1su]','',cache['CMAKE_CXX_FLAGS_DEBUG']),'-DCMAKE_EXE_LINKER_FLAGS=/INCREMENTAL:NO']
    run(command);run(['cmake','--build',str(work/'consumer'),'--config',args.config,'--parallel','4','--','/nr:false'])
    result=run([str(work/'consumer'/args.config/'installed_b4.exe')]);assert b'0.1.0-dev.7 B4Subset executor/scheduler/resources passed' in result.stdout
    maps=list((work/'consumer').rglob('installed_b4.map'));assert len(maps)==1
    text=maps[0].read_text(encoding='utf-8',errors='replace');assert 'ock_Adapter_CpuPool' in text and 'ock_Runtime' in text
    assert not any(name in text for name in ('ock_Data','ock_Dynamic','ock_Control'))
    shutil.copy2(maps[0],evidence/'installed-b4.map')
    (evidence/'result.json').write_text(json.dumps({'status':'Passed','scope':'B4 relocated CPU/Scheduler/Resources and four standalone public headers','configuration':args.config,'work':str(work),'selected':acquisition['selected']}),encoding='utf-8')
    print('B4 installed consumer passed:',evidence)

if __name__=='__main__':main()
