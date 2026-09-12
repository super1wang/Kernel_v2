"""验证当前 SDK 安装迁移、版本/阶段元数据及未实现组件拒绝。"""
import argparse
from pathlib import Path
import shutil
import sys
from types import SimpleNamespace
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
from tools.evidence.common import save_json,sha_file
_work=None
_records=[]

def run(argv, expected_success=True):
    raw=[_work/(str(len(_records))+'-'+s+'.log') for s in ('stdout','stderr')]
    row=execute(argv,ROOT,*raw,240)
    row['raw']=[{'path':x.name,'sha256':sha_file(x),'size':x.stat().st_size} for x in raw]
    _records.append(row);save_json(_work/'commands.json',_records)
    tree=row['process_tree']
    if row['status']!='Exited' or not tree['assigned_before_resume'] or tree['active_after'] or tree['terminated_owned_job']:
        raise ValueError('owned baseline child incomplete')
    p=SimpleNamespace(returncode=row['exit_code'],stdout=raw[0].read_bytes(),stderr=raw[1].read_bytes())
    print('argv:', argv, 'exit_code:', p.returncode)
    if (p.returncode == 0) != expected_success:
        print(p.stdout.decode('utf-8', errors='replace'))
        print(p.stderr.decode('utf-8', errors='replace'))
        raise RuntimeError('unexpected command result')
    return p

def main():
    global _work
    parser=argparse.ArgumentParser();parser.add_argument('--build',required=True);parser.add_argument('--config',required=True)
    args=parser.parse_args();build=Path(args.build).resolve()
    root=(ROOT/'build').resolve();root.mkdir(exist_ok=True)
    if not build.is_relative_to(root):raise ValueError('build must stay within workspace build directory')
    work=Path(tempfile.mkdtemp(prefix='install-check-',dir=root)).resolve()
    assert work.is_relative_to(root)
    _work=work
    original=work/'original';moved=work/'relocated'
    run(['cmake','--install',str(build),'--config',args.config,'--prefix',str(original)])
    shutil.copytree(original,moved)
    for p in moved.rglob('*.cmake'):
        data=p.read_text(encoding='utf-8')
        if str(ROOT).replace('\\','/') in data or str(original).replace('\\','/') in data:
            raise AssertionError('install export contains source or original absolute path')
    source=ROOT/'tests/install_consumer/baseline'
    base=['cmake','-S',str(source),'-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake',f'-DCMAKE_PREFIX_PATH={moved}']
    run([*base,'-B',str(work/'consumer')])
    run(['cmake','--build',str(work/'consumer'),'--config',args.config,'--parallel'])
    output=run([str(work/'consumer'/args.config/'installed_metadata.exe')])
    assert output.stdout.strip()==b'0.1.0-dev.7'
    for component in ('Automation','Observation'):
        rejected=run([*base,'-B',str(work/component),f'-DREQUIRE_COMPONENT={component}'],False)
        assert b'is not implemented in the current SDK' in rejected.stderr
    print('installed metadata and missing-component contracts verified; artifacts:',work)

if __name__=='__main__':main()
