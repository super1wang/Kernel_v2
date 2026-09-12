"""B6 StateNative 迁移安装、最小依赖和无文档消费者。"""
import argparse,json,os,re,shutil,sys,tempfile,uuid
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tests.install_consumer import verify_baseline as driver

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build',required=True);parser.add_argument('--config',required=True);args=parser.parse_args()
    producer=Path(args.build).resolve()
    if not producer.is_relative_to(ROOT/'build'):raise ValueError('workspace producer required')
    profile=next((producer/'CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
    compiler=re.search(r'set\(CMAKE_CXX_COMPILER "([^"]+)"\)',profile.read_text(encoding='utf-8')).group(1)
    os.environ['PATH']=str(Path(compiler).parent)+os.pathsep+os.environ.get('PATH','')
    acquisition=json.loads((producer/'dependency-acquisition.json').read_text(encoding='utf-8'))
    graph=json.loads((producer/'ock-target-graph.json').read_text(encoding='utf-8'))
    selected=set(acquisition['selected'])
    expected={'expected','immer'} if graph['build_components']=='StateNative' else {
        'expected','immer','jsoncons','asio','cli11','thread_pool'}
    if graph['build_components'] not in {'StateNative','B6Subset'} or selected!=expected:
        raise AssertionError({'profile':graph['build_components'],'selected':sorted(selected)})
    work=Path(tempfile.mkdtemp(prefix='state-install-',dir=ROOT/'build'))
    evidence_root=ROOT/'evidence/bootstrap/B6';evidence_root.mkdir(parents=True,exist_ok=True)
    evidence=evidence_root/('state-install-'+uuid.uuid4().hex[:10]);evidence.mkdir()
    driver._work=evidence;driver._records=[];run=driver.run
    original,relocated=work/'original',work/'relocated'
    run(['cmake','--install',str(producer),'--config',args.config,'--prefix',str(original)])
    shutil.copytree(original,relocated);original.rename(work/'unavailable-original')
    for p in relocated.rglob('*.cmake'):
        text=p.read_text(encoding='utf-8').replace('\\','/').casefold()
        assert ROOT.as_posix().casefold() not in text and original.as_posix().casefold() not in text and 'immer' not in text
    manifest=json.loads((relocated/'share/ock/sdk_api_manifest.json').read_text(encoding='utf-8'))
    assert manifest['sdk_version']=='0.1.0-dev.7' and manifest['stage']=='B6Subset'
    assert manifest['targets']['State']['kind']=='STATIC_LIBRARY'
    source=work/'source';shutil.copytree(ROOT/'tests/install_consumer/state',source)
    base=['cmake','-S',str(source),'-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake',f'-DCMAKE_PREFIX_PATH={relocated}']
    run([*base,'-B',str(work/'consumer')]);run(['cmake','--build',str(work/'consumer'),'--config',args.config,'--parallel','4','--','/nr:false'])
    result=run([str(work/'consumer'/args.config/'installed_state.exe')]);assert result.stdout.strip()==b'0.1.0-dev.7 State installed snapshot/commit passed'
    rejected=run([*base,'-B',str(work/'missing'),'-DREQUIRE_COMPONENT=Workspace'],False)
    assert b'is not implemented in the current SDK' in rejected.stderr
    (evidence/'result.json').write_text(json.dumps({'status':'Passed','scope':'B6 relocated State snapshot/commit consumer','configuration':args.config,'profile':manifest['installation_profile'],'selected':acquisition['selected']}),encoding='utf-8')
    print('B6 installed State consumer passed:',evidence)
if __name__=='__main__':main()
