from pathlib import Path
import sys,json,re
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R))
from tools.evidence.common import sha_file
def save(p,v):p.write_bytes((json.dumps(v,ensure_ascii=False,indent=2)+'\n').encode())
p=R/'sdk/sdk_api_manifest.json';v=json.loads(p.read_text(encoding='utf-8'));changed=[]
for h in v['headers']:
 digest=sha_file(R/h['path'])
 if h['sha256']!=digest:changed.append(h['path']);h['sha256']=digest
save(p,v);print('SDK refreshed by existing sha_file:',changed)
names='''T06.state.runtime_native
T08.state.snapshot_ownership
T09.state.memory_commit
T10.state.atomic_group
T06.contracts.outcome_closed_variants
T06.contracts.publication_proof_required
T06.contracts.before_apply_and_composite_proofs
T06.contracts.atomic_prepared_identity
T07.policy.cancel_arbitration
T07.policy.consume_before_revoke
T07.policy.expiry_boundary
T07.policy.permit_concurrent
T07.policy.permit_once
T07.policy.revoke_before_consume
T07.policy.target_lifecycle
T03.native.cancel_and_budget
T03.native.no_task_path
T06.native.business_failure
T06.native.managed_accepted_failures
T06.native.outcome_consistency
T03.native.managed_execution_path
T03.native.managed_record
T03.native.host_execution_resources
T03.native.host_required_record
T02.native.validation_exception
T02.contracts.compile_fixture_setup
T24.state.installed_consumer
T01.contracts.component_closure
T01.contracts.public_include_boundary
T24.contracts.public_headers
T24.sdk.version_header'''.splitlines()
profiles=['win-msvc-'+p for p in ('debug','release','asan')]
save(R/'tests/manifests/b6-semantic-v2.expected.json',{'format':'ock.expected/1','task_id':'D4.04','scope':'B6 semantic precision v2 F1-F5；按受影响 State/Native/Managed/Policy/Outcome 与安装 SDK 完成条件预先固定，保留历史全矩阵；不是 G4。','cases':[{'id':n,'repeat_required':1,'profiles':profiles} for n in names]})
for profile in profiles:
 p=R/('tests/runs/b6-closure-'+profile+'.json');spec=json.loads(p.read_text())
 spec['source_patterns']=[x for x in spec['source_patterns'] if x!='footprint-budgets.json' and not x.startswith('docs/reviews/')]
 spec['source_patterns']+=['docs/plans/B6-semantic-precision-v2.md']
 spec['required_artifacts']=['.gitattributes','docs/plans/B6-semantic-precision-v2.md','tests/manifests/b6-semantic-v2.expected.json']
 spec['expected_manifest']='tests/manifests/b6-semantic-v2.expected.json'
 spec['test_regex']='^('+'|'.join(re.escape(n) for n in names)+')$'
 spec['review_records']=['docs/reviews/B6-semantic-v2-'+profile+'-'+k+'.json' for k in ('spec','code')]
 save(R/('tests/runs/b6-semantic-v2-'+profile+'.json'),spec)
print('Frozen expected cases per configuration:',len(names))
