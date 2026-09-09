"""固定版本的 Executor 资格输入；不把故障后端或 NotApplicable 计为 Passed。"""
import hashlib
import json
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]

def main():
    cases=json.loads((ROOT/'tests/conformance/executor/cases.json').read_text(encoding='utf-8'))
    manifest=json.loads((ROOT/'tests/conformance/executor/conformance_manifest.json').read_text(encoding='utf-8'))
    binding=subprocess.check_output([sys.argv[1],'--identity'],timeout=10).decode().strip()
    assert binding==hashlib.sha256((ROOT/'tests/conformance/executor/conformance_manifest.json').read_bytes()).hexdigest()
    print('compiled conformance binding:',binding)
    assert manifest['format']=='ock.executor-conformance-manifest/1' and manifest['port_contract_version']=='ock.executor-port/1'
    assert set(manifest['backends'])==set(cases['backends'])=={'cpu_pool','controlled','inline'}
    lock=json.loads((ROOT/'dependencies.lock').read_text(encoding='utf-8'))
    assert manifest['backends']['cpu_pool']['dependency']==lock['dependencies']['thread_pool']
    for name,backend in manifest['backends'].items():
        assert backend['identity'] and backend['capabilities']==cases['backends'][name]
        assert set(backend['common_required'])==set(cases['common_required'])
        assert all(test==f'T12.executor.{name}.{case}' for case,test in backend['common_required'].items())
        assert backend['worker_destruction']==f'T22.executor.{name}.worker_destruction'
        for case,capability in cases['optional'].items():
            row=backend['optional'][case]
            assert row['status']==('Required' if backend['capabilities'][capability] else 'NotApplicable')
            if row['status']=='NotApplicable':assert row['reason'] and 'test' not in row
        for record in backend['sources']:
            assert hashlib.sha256((ROOT/record['path']).read_bytes()).hexdigest()==record['sha256'],record['path']
    for record in manifest['fixtures']:
        assert hashlib.sha256((ROOT/record['path']).read_bytes()).hexdigest()==record['sha256'],record['path']
    assert manifest['fault_backends_qualified'] is False
    print('ExecutorConformance v1: three identities, common mandatory cases and capability applicability verified')

if __name__=='__main__':main()
