"""D1.05 Policy 独立开发验证：快照源码、独占 Job、失败轮次保留。"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import uuid

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.evidence.process import execute

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline-policy', type=Path)
    parser.add_argument('--config', default='Debug', choices=['Debug', 'Release'])
    args = parser.parse_args()
    tag = uuid.uuid4().hex[:12]
    out = ROOT / 'evidence/bootstrap/D1.05' / ('inline-policy-' + tag)
    out.mkdir(parents=True, exist_ok=False)
    source = out / 'source'
    paths = [ROOT / p for p in ('packages/runtime/policy/policy.hpp', 'packages/runtime/policy/policy.cpp',
             'tests/contract/native/policy_inline_cases.hpp', 'tests/contract/authorization/fixtures.hpp',
             'tests/compile/contracts/test_support.hpp')]
    for folder in ('packages/contracts/include', 'packages/foundation/include'):
        paths.extend((ROOT/folder).rglob('*.hpp'))
    manifest = []
    for path in paths:
        relative = path.relative_to(ROOT)
        original = args.baseline_policy / path.name if args.baseline_policy and relative.parts[:3] == ('packages','runtime','policy') else path
        dest = source/relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, dest)
        manifest.append({'path':relative.as_posix(), 'copied_from':str(original), 'sha256':hashlib.sha256(dest.read_bytes()).hexdigest()})
    (out/'source.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    cases = ['ownership_and_identity', 'invalidation', 'deadlines_slots_and_cancellation',
             'required_permissions_and_tuple', 'global_quota_and_move_assignment',
             'close_and_original_retention', 'metadata_budget']
    main_cpp = '#include "tests/contract/native/policy_inline_cases.hpp"\nint main(int argc,char**argv) {\ntry {\n'
    for case in cases:
        main_cpp += f'if(argc==2 && std::string_view(argv[1])=="{case}") {{ policy_inline_test::{case}(); return 0; }}\n'
    main_cpp += 'return 2; } catch(const std::exception& e) { std::cerr << e.what() << "\\n"; return 1; }}\n'
    (out/'inline-policy-main.cpp').write_text(main_cpp, encoding='utf-8')
    includes = [source, source/'tests/compile/contracts', source/'packages/contracts/include',
                source/'packages/foundation/include', ROOT/'build/d0.06-a/cache/sources/expected-fe3b18aecb84/include']
    (out/'CMakeLists.txt').write_text('\n'.join([
        'cmake_minimum_required(VERSION 3.25)', 'project(InlinePolicy LANGUAGES CXX)',
        f'add_executable(inline_policy "{(source/"packages/runtime/policy/policy.cpp").as_posix()}" inline-policy-main.cpp)',
        'target_compile_features(inline_policy PRIVATE cxx_std_20)',
        'target_compile_options(inline_policy PRIVATE /utf-8 /EHsc /Zc:__cplusplus /permissive-)',
        'target_include_directories(inline_policy PRIVATE '+ ' '.join('"'+p.as_posix()+'"' for p in includes)+')'])+'\n', encoding='utf-8')
    records = []
    def run(name, argv):
        r = execute(argv, ROOT, out/(name+'-stdout.log'), out/(name+'-stderr.log'), 600)
        records.append(r)
        (out/'commands.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
        print(name, r['status'], r['exit_code'], out, flush=True)
        if r['status'] != 'Exited' or r['process_tree']['active_after'] != 0:
            raise RuntimeError('owned job did not complete')
        return r['exit_code']
    build = ROOT/'build'/('d1.05-inline-policy-'+tag)
    if run('configure', ['cmake','-S',str(out),'-B',str(build),'-G','Visual Studio 17 2022',
                        '-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
                        '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake')]): return 1
    if run('build',['cmake','--build',str(build),'--config',args.config,'--parallel','2','--','/nr:false']): return 1
    failed = False
    for case in cases:
        failed = bool(run(case,[str(build/args.config/'inline_policy.exe'),case])) or failed
    return int(failed)

if __name__ == '__main__':
    sys.exit(main())
