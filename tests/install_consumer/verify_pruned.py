"""B2 收口：无测试的 Runtime-only 生产构建、安装裁剪与真实 Native 消费。"""
import argparse
import json
from pathlib import Path
import sys
import tempfile
import uuid

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tests.install_consumer import verify_baseline as driver

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--config',required=True)
    args=parser.parse_args()
    # 本专项证明组件选择；各 sanitizer/优化行为由同源 B2 影响矩阵验证。
    if args.config!='Debug':raise ValueError('pruning configuration is frozen to Debug')
    evidence=ROOT/'evidence/bootstrap/B2'/('native-pruned-'+uuid.uuid4().hex[:10]);evidence.mkdir()
    driver._work=evidence;driver._records=[];run=driver.run
    work=Path(tempfile.mkdtemp(prefix='native-pruned-',dir=ROOT/'build'))
    producer=work/'producer';prefix=work/'prefix'
    run(['cmake','--preset','win-msvc-debug','-B',str(producer),'-DBUILD_TESTING=OFF',
         '-DOCK_BUILD_COMPONENTS=Runtime','-DOCK_DEPENDENCY_COMPONENTS=Foundation',
         '-DOCK_DEPENDENCIES_OFFLINE=ON','-DPython3_EXECUTABLE='+sys.executable])
    acquisition=json.loads((producer/'dependency-acquisition.json').read_text(encoding='utf-8'))
    assert acquisition['selected']==['expected'],'Native producer acquired dynamic dependency'
    graph=json.loads((producer/'ock-target-graph.json').read_text(encoding='utf-8'))
    assert set(graph['targets'])=={'Foundation','CoreContracts','Runtime'}
    assert not (producer/'ock_Data.vcxproj').exists() and not (producer/'ock_Dynamic.vcxproj').exists()
    run([sys.executable,'-X','utf8',str(ROOT/'tools/architecture/check.py'),'--graph',str(producer/'ock-target-graph.json')])
    run(['cmake','--build',str(producer),'--config','Debug','--parallel','4','--','/nr:false'])
    run(['cmake','--install',str(producer),'--config','Debug','--prefix',str(prefix)])
    installed=json.loads((prefix/'share/ock/sdk_api_manifest.json').read_text(encoding='utf-8'))
    assert installed['installation_profile']=='Runtime' and set(installed['targets'])==set(graph['targets'])
    assert all(h['target'] in graph['targets'] for h in installed['headers'])
    assert not (prefix/'include/ock/data').exists() and not (prefix/'include/ock/dynamic').exists()
    assert not (prefix/'include/ock/control').exists() and not (prefix/'include/ock/control_protocol').exists()
    assert not (prefix/'share/ock/schemas/catalog').exists()
    assert set(p.name for p in (prefix/'lib').glob('*.lib'))=={'ock_Runtime.lib'}
    negative=work/'missing';negative.mkdir()
    (negative/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\nproject(Missing LANGUAGES NONE)\nfind_package(OCK CONFIG REQUIRED COMPONENTS Data)\n',encoding='utf-8')
    rejected=run(['cmake','-S',str(negative),'-B',str(work/'missing-build'),'-DCMAKE_PREFIX_PATH='+str(prefix)],False)
    assert b'OCK component Data is not implemented' in rejected.stdout+rejected.stderr
    run([sys.executable,'-X','utf8',str(ROOT/'tests/install_consumer/verify_native.py'),
         '--build',str(producer),'--config','Debug','--case','installed_host','--evidence-task','B2'])
    (evidence/'result.json').write_text(json.dumps({'status':'Passed','scope':'Runtime-only acquisition/build/install and real Native consumer',
        'producer':str(producer),'selected_dependencies':acquisition['selected'],'installed_components':sorted(graph['targets'])}),encoding='utf-8')
    print('Native-only production pruning passed:',evidence)

if __name__=='__main__':main()
