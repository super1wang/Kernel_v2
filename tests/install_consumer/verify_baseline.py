"""验证安装树迁移、公开版本头与未实现组件拒绝，不宣称 SDK Runtime 已实现。"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def run(argv, expected_success=True):
    p = subprocess.run(argv, capture_output=True)
    print('argv:', argv, 'exit_code:', p.returncode)
    if (p.returncode == 0) != expected_success:
        print(p.stdout.decode('utf-8', errors='replace'))
        print(p.stderr.decode('utf-8', errors='replace'))
        raise RuntimeError('unexpected command result')
    return p

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build',required=True);parser.add_argument('--config',required=True)
    args=parser.parse_args();build=Path(args.build).resolve()
    root=(ROOT/'build').resolve();root.mkdir(exist_ok=True)
    if not build.is_relative_to(root):raise ValueError('build must stay within workspace build directory')
    work=Path(tempfile.mkdtemp(prefix='install-check-',dir=root)).resolve()
    assert work.is_relative_to(root)
    original=work/'original';moved=work/'relocated'
    run(['cmake','--install',str(build),'--config',args.config,'--prefix',str(original)])
    shutil.copytree(original,moved)
    for p in moved.rglob('*.cmake'):
        data=p.read_text(encoding='utf-8')
        if str(ROOT).replace('\\','/') in data or str(original).replace('\\','/') in data:
            raise AssertionError('install export contains source or original absolute path')
    source=ROOT/'tests/install_consumer/baseline'
    base=['cmake','-S',str(source),'-G','Visual Studio 17 2022','-A','x64',f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake',f'-DCMAKE_PREFIX_PATH={moved}']
    run([*base,'-B',str(work/'consumer')])
    run(['cmake','--build',str(work/'consumer'),'--config',args.config,'--parallel'])
    output=run([str(work/'consumer'/args.config/'installed_metadata.exe')])
    assert output.stdout.strip()==b'0.1.0-dev.1'
    for component in ('Runtime','Observation'):
        rejected=run([*base,'-B',str(work/component),f'-DREQUIRE_COMPONENT={component}'],False)
        assert b'is not implemented in the current SDK' in rejected.stderr
    print('installed metadata and missing-component contracts verified; artifacts:',work)

if __name__=='__main__':main()
