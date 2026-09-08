"""有限 Debug 校准/原始采集入口；不批准预算，不执行正式 ABBA 矩阵。"""
import argparse
from dataclasses import asdict,replace
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import uuid

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
from tools.footprint.windows_process import ObservationConfig,PHASES
from tools.footprint.analyze import require_complete,summarize_observation,require_modules,owned_success


def save(path,value):
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--configuration',type=Path,required=True)
    parser.add_argument('--mode',choices=['guards','calibrate'],required=True)
    args=parser.parse_args()
    configuration=ObservationConfig(**json.loads(args.configuration.read_text(encoding='utf-8')))
    out=ROOT/'evidence/bootstrap/D1.06'/('footprint-'+args.mode+'-'+uuid.uuid4().hex[:12]);out.mkdir(parents=True)
    save(out/'configuration.json',asdict(configuration))
    files=[ROOT/'tools/evidence/process.py',ROOT/'tests/tools/evidence/test_process.py',ROOT/'cmake/LockedMSVC.cmake',ROOT/'cmake/msvc-validation-tools.json',ROOT/'dependencies.lock',ROOT/'docs/contracts/native-footprint-method.md']
    for directory in ('tools/footprint','tests/tools/footprint'):
        files.extend(p for p in (ROOT/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    rows=[]
    for path in files:
        relative=path.relative_to(ROOT);destination=out/'source'/relative;destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(path,destination);rows.append({'path':relative.as_posix(),'sha256':hashlib.sha256(destination.read_bytes()).hexdigest()})
    save(out/'sources.json',sorted(rows,key=lambda r:r['path']))
    commands=[]
    def run(label,argv,observation=None,timeout=60):
        r=execute(argv,ROOT,out/(label+'-stdout.log'),out/(label+'-stderr.log'),timeout,observation=observation)
        r['label']=label;r['raw']=[{'path':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size} for p in (out/(label+'-stdout.log'),out/(label+'-stderr.log'))]
        commands.append(r);save(out/'commands.json',commands)
        print(label,r['status'],r['exit_code'],r.get('observation',{}).get('status'),str(out),flush=True)
        return r
    if args.mode=='guards':
        r=run('unit',[sys.executable,'-X','utf8','-m','unittest','discover','-s',str(out/'source/tests/tools/footprint'),'-v'])
        previous=run('existing-process',[sys.executable,'-X','utf8','-m','unittest','discover','-s',str(out/'source/tests/tools/evidence'),'-p','test_process.py','-v'])
        ok=all(owned_success(x) for x in (r,previous))
        save(out/'result.json',{'scope':'pure guard and existing process regression only','status':'Passed' if ok else 'Failed','budget_status':'NotApproved'})
        return 0 if ok else 1
    source=out/'source';build=ROOT/'build'/out.name
    (out/'CMakeLists.txt').write_text('\n'.join([
        'cmake_minimum_required(VERSION 3.25)','project(FootprintCalibration LANGUAGES CXX)',
        'add_library(footprint_required SHARED "'+(source/'tools/footprint/controls/required.cpp').as_posix()+'")',
        'target_compile_features(footprint_required PRIVATE cxx_std_20)',
        'target_compile_options(footprint_required PRIVATE /utf-8 /EHsc /Zc:__cplusplus /permissive-)',
        'add_executable(footprint_calibration "'+(source/'tools/footprint/controls/calibration.cpp').as_posix()+'")',
        'target_link_libraries(footprint_calibration PRIVATE footprint_required)',
        'target_compile_features(footprint_calibration PRIVATE cxx_std_20)',
        'target_compile_options(footprint_calibration PRIVATE /utf-8 /EHsc /Zc:__cplusplus /permissive-)',
        'set_property(TARGET footprint_calibration PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)'])+'\n',encoding='utf-8')
    configured=run('configure',['cmake','-S',str(out),'-B',str(build),'-G','Visual Studio 17 2022','-A','x64',
                               '-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
                               '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake')])
    if not owned_success(configured):return 1
    built=run('build',['cmake','--build',str(build),'--config','Debug','--parallel','2','--','/nr:false'])
    if not owned_success(built):return 1
    binary=build/'Debug/footprint_calibration.exe'
    save(out/'artifact.json',{'path':str(binary),'sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'bytes':binary.stat().st_size,'configuration':'Debug','purpose':'independent calibration only, not NativeSubset'})
    for item in (build/'CMakeCache.txt',build/'footprint_calibration.vcxproj',build/'footprint_required.vcxproj'):
        shutil.copyfile(item,out/item.name)
    tool_hint=json.loads((ROOT/'cmake/msvc-validation-tools.json').read_text(encoding='utf-8'))['source_hint']
    dumpbin=Path(tool_hint)/'dumpbin.exe'
    imported=run('imports',[str(dumpbin),'/imports',str(binary)])
    if not owned_success(imported) or 'footprint_required.dll' not in (out/'imports-stdout.log').read_text(encoding='utf-8',errors='replace').lower():
        save(out/'result.json',{'status':'Failed','reason':'required DLL not proven in actual imports','budget_status':'NotApproved'});return 1
    observations={};assertions=[]
    for mode in ('none','memory','thread','delay','raw'):
        r=run(mode,[str(binary),'--control',mode,'--bytes',str(configuration.calibration_bytes),'--delay-ms',str(configuration.ready_delay_ms)],configuration,30)
        try:require_complete(r)
        except ValueError as exc:
            save(out/'result.json',{'status':'Failed','failed_control':mode,'error':str(exc),'budget_status':'NotApproved'});return 1
        observations[mode]=r;save(out/(mode+'-summary.json'),summarize_observation(r))
    def stage(mode,name):return next(s for s in observations[mode]['observation']['samples'] if s['reason']==name)
    memory_before=stage('memory','HostConstructionBegin')['private_bytes'];memory_after=stage('memory','HostReady')['private_bytes']
    assertions.append({'name':'committed_touched_region_observed','passed':memory_after-memory_before>=configuration.calibration_bytes,'before':memory_before,'after':memory_after,'committed_bytes':configuration.calibration_bytes})
    baseline_threads=stage('none','HostReady')['thread_ids'];positive_threads=stage('thread','HostReady')['thread_ids'];released_threads=stage('thread','ShutdownComplete')['thread_ids']
    assertions.append({'name':'held_thread_and_join_observed','passed':len(positive_threads)==len(baseline_threads)+1 and len(released_threads)==len(baseline_threads),'baseline':baseline_threads,'held':positive_threads,'released':released_threads})
    delay=observations['delay']['observation'];minimum=configuration.ready_delay_ms*delay['qpc_frequency']//1000
    assertions.append({'name':'ready_delay_in_internal_clock','passed':delay['ready']['internal_ticks']>=minimum,'actual_ticks':delay['ready']['internal_ticks'],'minimum_ticks':minimum})
    assertions.append({'name':'binary_raw_streams_unchanged','passed':(out/'raw-stdout.log').read_bytes()==b'\x00\xffout\r\n' and (out/'raw-stderr.log').read_bytes()==b'\xfe\x00err\n'})
    for mode in ('bad_nonce','early_exit'):
        r=run(mode,[str(binary),'--control',mode],configuration,30)
        rejected=False
        try:require_complete(r)
        except ValueError:rejected=True
        assertions.append({'name':mode+'_cannot_pass','passed':rejected and r['process_tree']['active_after']==0,'root_status':r['status'],'root_exit_code':r['exit_code'],'observation_status':r.get('observation',{}).get('status')})
    capacity=run('capacity',[str(binary)],replace(configuration,max_samples=1),30)
    assertions.append({'name':'sample_capacity_fails_owned','passed':capacity.get('observation',{}).get('status')=='ObserverFailed' and capacity['process_tree']['active_after']==0})
    timeout=run('absolute-timeout',[str(binary)],configuration,.1)
    # 查询内与 owner 下一次轮询都可能先看到同一个绝对截止；两条路径均须真实终止并排空。
    deadline_seen=(timeout['status']=='Timeout' or
        (timeout.get('observation',{}).get('status')=='ObserverFailed' and
         timeout.get('error')=='ObservationError: RunDeadlineReached'))
    assertions.append({'name':'sampling_cannot_extend_deadline','passed':deadline_seen and
        timeout['process_tree']['assigned_before_resume'] and timeout['process_tree']['active_after']==0 and
        timeout['process_tree']['terminated_owned_job'],'status':timeout['status'],'error':timeout.get('error')})
    paths={p.casefold():p for item in observations.values() for snapshot in item['observation']['modules'] for p in snapshot['paths']}
    modules=[];windows=Path(os.environ['SystemRoot']).resolve()
    for path_text in sorted(paths.values()):
        path=Path(path_text);data=path.read_bytes();name=path.name.casefold()
        role=('consumer_exe' if path==binary else 'required_test_distribution' if name=='footprint_required.dll' else
              'crt_runtime' if any(x in name for x in ('vcruntime','msvcp','ucrt','clang_rt.asan')) else
              'os_environment' if path.resolve().is_relative_to(windows) else 'unclassified_loaded_module')
        modules.append({'path':str(path),'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'role':role})
    save(out/'loaded-modules.json',modules)
    required_path=build/'Debug/footprint_required.dll'
    required=[{'path':str(required_path),'sha256':hashlib.sha256(required_path.read_bytes()).hexdigest(),'bytes':required_path.stat().st_size}]
    require_modules(required,modules)
    omitted=False
    try:require_modules(required,[m for m in modules if m['role']!='required_test_distribution'])
    except ValueError:omitted=True
    assertions.append({'name':'actual_required_import_omission_rejected','passed':omitted,'required':required})
    save(out/'controls.json',assertions)
    ok=all(item['passed'] for item in assertions)
    save(out/'result.json',{'status':'Passed' if ok else 'Failed','scope':'Debug real Win32 calibration and observer guards only','budget_status':'NotApproved','formal_abba_executed':False})
    return 0 if ok else 1


if __name__=='__main__':sys.exit(main())
