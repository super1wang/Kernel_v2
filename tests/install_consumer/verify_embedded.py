"""B5 Embedded 的实际依赖裁剪、迁移安装与公开 typed 消费；不代替 footprint。"""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import uuid

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tests.install_consumer import verify_baseline as driver

def main():
    evidence=ROOT/'evidence/B5'/('embedded-install-'+uuid.uuid4().hex[:10]);evidence.mkdir(parents=True)
    driver._work=evidence;driver._records=[];run=driver.run
    work=Path(tempfile.mkdtemp(prefix='embedded-install-',dir=ROOT/'build'))
    producer=work/'producer';original=work/'original';relocated=work/'relocated'
    run(['cmake','--preset','win-msvc-debug','-B',str(producer),'-DBUILD_TESTING=OFF',
         '-DOCK_BUILD_COMPONENTS=Embedded','-DOCK_DEPENDENCY_COMPONENTS=Foundation;CpuPool',
         '-DOCK_DEPENDENCIES_OFFLINE=ON','-DPython3_EXECUTABLE='+sys.executable])
    acquisition=json.loads((producer/'dependency-acquisition.json').read_text(encoding='utf-8'))
    assert set(acquisition['selected'])=={'expected','thread_pool'},acquisition
    graph=json.loads((producer/'ock-target-graph.json').read_text(encoding='utf-8'))
    expected={'Foundation','CoreContracts','Runtime','Adapter::CpuPool'}
    assert set(graph['targets'])==expected,graph
    run([sys.executable,'-X','utf8',str(ROOT/'tools/architecture/check.py'),'--graph',str(producer/'ock-target-graph.json')])
    run(['cmake','--build',str(producer),'--config','Debug','--parallel','4','--','/nr:false'])
    run(['cmake','--install',str(producer),'--config','Debug','--prefix',str(original)])
    shutil.copytree(original,relocated)
    hidden=work/'unavailable-original'
    assert original.resolve().is_relative_to(work) and hidden.resolve().is_relative_to(work)
    original.rename(hidden)
    installed=json.loads((relocated/'share/ock/sdk_api_manifest.json').read_text(encoding='utf-8'))
    assert installed['installation_profile']=='Embedded' and set(installed['targets'])==expected
    assert all(header['target'] in expected for header in installed['headers'])
    assert set(p.name for p in (relocated/'lib').glob('*.lib'))=={'ock_Runtime.lib','ock_Adapter_CpuPool.lib'}
    assert not any((relocated/'include/ock'/name).exists() for name in ('data','dynamic','control','control_protocol','state'))
    for path in relocated.rglob('*.cmake'):
        content=path.read_text(encoding='utf-8').replace('\\','/').casefold()
        assert ROOT.as_posix().casefold() not in content and original.as_posix().casefold() not in content
        assert all(name not in content for name in ('ock_dep_thread_pool','jsoncons','ock_dep_asio'))
    source=work/'consumer-source';shutil.copytree(ROOT/'examples/embedded_service',source)
    run(['cmake','-S',str(source),'-B',str(work/'consumer'),'-G','Visual Studio 17 2022','-A','x64',
         '-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
         f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake',f'-DCMAKE_PREFIX_PATH={relocated}'])
    run(['cmake','--build',str(work/'consumer'),'--config','Debug','--parallel','4','--','/nr:false'])
    result=run([str(work/'consumer/Debug/ock_embedded_service.exe')])
    assert b'40 Invoke/Submit parity cycles and quiescent shutdown passed' in result.stdout
    maps=list((work/'consumer').rglob('ock_embedded_service.map'));assert len(maps)==1
    content=maps[0].read_text(encoding='utf-8',errors='replace')
    assert 'ock_Runtime' in content and 'ock_Adapter_CpuPool' in content
    assert all(name not in content for name in ('ock_Data','ock_Dynamic','ock_Control','jsoncons','asio'))
    shutil.copy2(maps[0],evidence/'consumer.map')
    negative=work/'missing';negative.mkdir()
    (negative/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\nproject(Missing LANGUAGES NONE)\nfind_package(OCK CONFIG REQUIRED COMPONENTS Data)\n',encoding='utf-8')
    rejected=run(['cmake','-S',str(negative),'-B',str(work/'missing-build'),'-DCMAKE_PREFIX_PATH='+str(relocated)],False)
    assert b'OCK component Data is not implemented' in rejected.stdout+rejected.stderr
    (evidence/'result.json').write_text(json.dumps({'status':'Passed','scope':'Embedded Debug acquisition, relocated install and public typed parity',
        'producer':str(producer),'selected_dependencies':acquisition['selected'],'installed_components':sorted(expected)}),encoding='utf-8')
    print('Embedded installation passed:',evidence,flush=True)

if __name__=='__main__':main()
