"""B3Subset 配置投影检查；B4 与 Runtime-only 的真实安装分别由对应消费者证明。"""
import json
from pathlib import Path
import sys
import tempfile
import uuid
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tests.install_consumer import verify_baseline as driver

def main():
    evidence=ROOT/'evidence/bootstrap/B4'/('b3-projection-'+uuid.uuid4().hex[:10]);evidence.mkdir(parents=True)
    driver._work=evidence;driver._records=[]
    work=Path(tempfile.mkdtemp(prefix='b4-projection-',dir=ROOT/'build'))
    driver.run(['cmake','--preset','win-msvc-debug','-B',str(work),'-DBUILD_TESTING=OFF','-DOCK_BUILD_COMPONENTS=B3Subset','-DOCK_DEPENDENCY_COMPONENTS=CLI','-DOCK_DEPENDENCIES_OFFLINE=ON','-DPython3_EXECUTABLE='+sys.executable])
    graph=json.loads((work/'ock-target-graph.json').read_text(encoding='utf-8'))
    assert graph['targets']['Adapter::CpuPool']['kind']=='INTERFACE_LIBRARY'
    acquisition=json.loads((work/'dependency-acquisition.json').read_text(encoding='utf-8'))
    assert 'thread_pool' not in acquisition['selected']
    manifest=json.loads((work/'sdk_api_manifest.json').read_text(encoding='utf-8'))
    assert manifest['installation_profile']=='B3Subset' and manifest['targets']['Adapter::CpuPool']['implementation']=='ContractBaseline'
    assert not any(h['target']=='Adapter::CpuPool' for h in manifest['headers'])
    driver.run([sys.executable,'-X','utf8',str(ROOT/'tools/architecture/check.py'),'--graph',str(work/'ock-target-graph.json')])
    invalid=work/'invalid-b4'
    refused=driver.run(['cmake','--preset','win-msvc-debug','-B',str(invalid),'-DBUILD_TESTING=OFF','-DOCK_BUILD_COMPONENTS=B4Subset','-DOCK_DEPENDENCY_COMPONENTS=Embedded','-DOCK_DEPENDENCIES_OFFLINE=ON','-DPython3_EXECUTABLE='+sys.executable],False)
    assert b'B4Subset requires OCK_DEPENDENCY_COMPONENTS=CLI;CpuPool' in refused.stdout+refused.stderr
    assert not (invalid/'dependency-acquisition.json').exists()
    (evidence/'result.json').write_text(json.dumps({'status':'Passed','scope':'B3 configuration projection only','selected':acquisition['selected'],'work':str(work)}),encoding='utf-8')
    print('B3 projection preserved:',evidence)

if __name__=='__main__':main()
