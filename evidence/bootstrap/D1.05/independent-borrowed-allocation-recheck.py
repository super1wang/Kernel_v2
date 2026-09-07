"""独立只读复核既有集成证据；不运行业务、不修改被审源码。"""
import hashlib
import json
from pathlib import Path
import sys
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]

def read(p): return json.loads(p.read_text(encoding='utf-8'))
def sha(data): return hashlib.sha256(data).hexdigest()
def commands(folder, rows):
    for row in rows:
        tree = row['process_tree']
        assert row['status'] == 'Exited' and tree['mechanism'] == 'WindowsJobObject'
        assert tree['assigned_before_resume'] and tree['active_after'] == 0
        for raw in row['raw']:
            data = (folder/raw['path']).read_bytes()
            assert len(data) == raw['size'] and sha(data) == raw['sha256']

def review(folder):
    result = read(folder/'result.json')
    assert result['status'] == 'Passed' and not any(result['source_changed'].values())
    assert len(result['cases']) == 32 and all(c['status'] == 'Passed' for c in result['cases'])
    junit = ET.parse(folder/'native-junit.xml').getroot().findall('.//testcase')
    assert len(junit) == 32 and all(not any(c.find(tag) is not None for tag in ('failure','error','skipped')) for c in junit)
    assert sorted(c.get('name') for c in junit) == sorted(c['name'] for c in result['cases'])
    asan = result['profile'] == 'win-msvc-asan'
    source = read(folder/'source-inputs.json')
    source_digest = sha(json.dumps(source, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode('utf-8'))
    for row in source:
        data = (ROOT/row['path']).read_bytes()
        assert len(data) == row['size'] and sha(data) == row['sha256']
    with zipfile.ZipFile(folder/'source-inputs.zip') as archive:
        for row in source:
            data = archive.read(row['path'])
            assert len(data) == row['size'] and sha(data) == row['sha256']
    rows = read(folder/'commands.json')
    commands(folder, rows)
    assert len(rows) == 4 and all(r['exit_code'] == 0 for r in rows)
    artifacts = read(folder/'build-artifacts.json')
    with zipfile.ZipFile(folder/'build-artifacts.zip') as archive:
        assert len(archive.namelist()) == len(artifacts)
        for row in artifacts:
            data = archive.read(row['path'])
            assert len(data) == row['size'] and sha(data) == row['sha256']
    children = ROOT/result['build_dir']/'tests/contract/native/child-runs'
    seen = {}
    samples = 0
    child_count = 0
    probes = None
    borrowed_sample = None
    for child in children.iterdir():
        if not child.is_dir(): continue
        bundle = read(child/'commands.json')
        commands(child, bundle['commands'])
        for command in bundle['commands']:
            argv = command['argv']
            expected_exit = 0
            if argv[-1] == '--allocation-injected-red': expected_exit = 1
            elif argv[-1] == '--throwing-transport-probe': expected_exit = 86
            elif argv[-1] == '--asan-hook-registration-failure': expected_exit = 87
            elif '--target' in argv:
                expected_exit = 0 if argv[argv.index('--target')+1] == 'positive' else 1
            elif '-S' in argv and Path(argv[argv.index('-S')+1]).name == 'Runtime': expected_exit = 1
            assert command['exit_code'] == expected_exit
        child_count += len(bundle['commands'])
        structure = read(child/'structure.json')
        case = bundle['case'].split('.')[-1]
        assert case not in seen and structure['case'] == bundle['case']
        seen[case] = child.name
        if case == 'private_dispatch':
            assert structure['throwing_transport_exit'] == 86
            assert any(r['exit_code'] == 86 for r in bundle['commands'])
            assert b'locked_result_noexcept_transport_terminated' in (child/'throwing-transport-stderr.log').read_bytes()
        if case == 'allocation_probe':
            injected = read(child/'probe-rejection-stdout.log')
            assert injected['checks']['verified'] is False
            assert any(s['cpp'] > 0 for s in injected['samples'])
            assert read(child/'probe-rejection.json')['exit_code'] == 1
            if asan:
                assert read(child/'hook-registration-failure.json')['exit_code'] == 87
                assert b'native_asan_hook_registration_failed' in (child/'hook-registration-failure-stderr.log').read_bytes()
                assert any(s['asan_allocations'] == 1 for s in injected['samples'])
        if case.startswith('allocation_'):
            report = read(child/'allocation.json')
            assert report['checks']['verified'] is True
            assert all(s['success'] is True for s in report['samples'])
            debug = result['configuration'] == 'Debug'
            assert report['coverage']['debug_crt'] is debug
            assert report['coverage']['iterator_debug_level'] == (2 if debug else 0)
            assert report['coverage']['asan_allocator_hooks'] is asan
            assert report['coverage']['debug_crt_effective'] == ('unobserved_asan_intercepted' if asan else 'full' if debug else 'unavailable')
            for s in report['samples']:
                assert s['cpp_live_bytes_after'] == s['cpp_live_bytes_before'] + s['cpp_allocated_bytes'] - s['cpp_released_bytes']
                assert s['cpp_live_blocks_after'] == s['cpp_live_blocks_before'] + s['cpp'] - s['cpp_frees']
                assert max(s['cpp_live_bytes_before'],s['cpp_live_bytes_after']) <= s['cpp_peak_live_bytes'] <= s['cpp_live_bytes_before']+s['cpp_allocated_bytes']
                if not debug: assert s['crt'] is None
                if asan:
                    assert type(s['asan_allocations']) is int and s['asan_allocations'] >= 0
                    assert type(s['asan_frees']) is int and s['asan_frees'] >= 0
                else:
                    assert s['asan_allocations'] is None and s['asan_frees'] is None
            samples += len(report['samples'])
            if case == 'allocation_probe':
                probes = {s['name']:s['crt'] for s in report['samples']}
                assert len(probes) == 13 and all(n in probes for n in ('malloc','calloc','realloc','aligned_malloc'))
                if asan:
                    assert all(s['asan_allocations'] == 1 and s['crt'] == 0 for s in report['samples'][:12])
                    assert report['coverage']['crt_missing_positive_entries'] == [s['name'] for s in report['samples'][:12]]
                elif debug:
                    assert all(probes[n] > 0 for n in ('malloc','calloc','realloc','aligned_malloc'))
                    assert report['coverage']['crt_missing_positive_entries'] == []
                else: assert report['coverage']['crt_missing_positive_entries'] is None
            if case == 'allocation_other_costs':
                borrowed = [s for s in report['samples'] if s['name'] == 'borrowed_nonempty_context_only']
                assert len(borrowed) == 1
                b = borrowed[0]
                borrowed_sample = b
                assert b['success'] is True and b['cpp'] == b['cpp_frees'] == 0
                assert not debug or b['crt'] == 0
                assert not asan or b['asan_allocations'] == b['asan_frees'] == 0
            if case == 'allocation_steady':
                assert len(report['samples']) == 40
                assert all(s['cpp'] == s['cpp_frees'] == 0 and (not debug or s['crt'] == 0) for s in report['samples'])
                if asan: assert all(s['asan_allocations'] == s['asan_frees'] == 0 for s in report['samples'])
    assert set(seen) == {'private_dispatch','no_self_wait','example_consumer','component_boundary',
                         'allocation_probe','allocation_steady','allocation_governance','allocation_other_costs'}
    assert child_count == (28 if asan else 27) and samples == 69
    if asan:
        build = ROOT/result['build_dir']
        assert read(build/'toolchain-Debug.json')['asan_requested'] == 'ON'
        for relative in ('tests/contract/native/ock_native_tests.vcxproj',
                         'tests/contract/native/ock_invocation_internal.vcxproj',
                         'tests/contract/authorization/ock_policy_internal.vcxproj',
                         'tests/contract/registration/ock_registry_internal.vcxproj',
                         'examples/native_service/ock_native_service.vcxproj'):
            assert '/fsanitize=address' in (build/relative).read_text(encoding='utf-8-sig')
    return dict(directory=folder.name, profile=result['profile'], cases=32, wrappers=seen,
                child_commands=child_count, allocation_samples=samples, probes=probes, borrowed_sample=borrowed_sample,
                source_files=len(source), artifact_files=len(artifacts), result_sha256=sha((folder/'result.json').read_bytes()),
                source_digest=source_digest,
                reviewed_source={r['path']:r['sha256'] for r in source if r['path'].startswith('examples/native_service/') or r['path'] in {
                  'tests/contract/native/allocation_probe.hpp','tests/contract/native/allocation_probe.cpp',
                  'tests/contract/native/allocation_cases.hpp','tests/contract/native/verify_children.py',
                  'tests/contract/native/discover.py','tests/contract/native/CMakeLists.txt','tests/contract/native/integrate.py'}})

if __name__ == '__main__':
    findings = [review(ROOT/'evidence/bootstrap/D1.05'/name) for name in sys.argv[1:]]
    print(json.dumps(findings, ensure_ascii=False, indent=2))
