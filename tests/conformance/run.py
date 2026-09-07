"""将固定工厂 manifest、共同合同和真实故障结果输出为开发证据。"""
import hashlib
import json
from pathlib import Path
from support.fixtures import MockExecutor, DuplicateCallbackExecutor
from support.harness import COMMON_CASES, CONTRACT_VERSION, descriptor_for, run_contract
ROOT=Path(__file__).resolve().parent

def collect():
    path=ROOT/'support/manifest.json';manifest=json.loads(path.read_text(encoding='utf-8'))
    if manifest['common_required']!=list(COMMON_CASES) or manifest['port_contract_version']!=CONTRACT_VERSION:
        raise ValueError('fixed common contract manifest mismatch')
    factories={'mock-inline':MockExecutor,'fault-duplicate':DuplicateCallbackExecutor}
    if {b['name'] for b in manifest['backends']}!=set(factories): raise ValueError('fixture set differs')
    results=[]
    for backend in manifest['backends']:
        factory=factories[backend['name']]
        descriptor=descriptor_for(factory,backend['name'],backend['kind'])
        if any(descriptor[k]!=backend[k] for k in ('factory','capabilities','kind')): raise ValueError('fixture descriptor differs')
        result=run_contract(factory,descriptor)
        results.append({'descriptor':descriptor,'result':result})
    good=results[0]['result']['qualified'] and not results[1]['result']['qualified'] and any(c['id']=='exactly_once_completion' and c['status']=='Failed' for c in results[1]['result']['cases'])
    return {'format':'ock.conformance-bootstrap-result/1','manifest_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'contract_version':CONTRACT_VERSION,'results':results,'self_check_passed':good}

if __name__=='__main__':
    report=collect();print(json.dumps(report,ensure_ascii=False,indent=2));raise SystemExit(not report['self_check_passed'])
