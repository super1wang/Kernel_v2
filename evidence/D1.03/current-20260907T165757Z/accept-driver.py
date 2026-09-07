from pathlib import Path
import sys,shutil
sys.path.insert(0,str(Path.cwd()))
from tools.evidence.common import read_json,save_json,sha_file,now
from tools.evidence.process import execute
root=Path.cwd();source='fb08a1955ebf-75090b892715'
reports=[]
for profile in ('debug','release','asan'):
 matches=list(root.glob(f'evidence/{source}/win-msvc-{profile}/D1.03/*/report.json'))
 assert len(matches)==1,(profile,matches)
 report=read_json(matches[0]);assert report['automated_status']=='Passed' and report['package_status']=='Passed'
 reports.append(matches[0])
out=root/'evidence/D1.03'/('current-'+now().replace('-','').replace(':','').split('.')[0]+'Z');out.mkdir()
shutil.copyfile('build/d1.03-accept.py',out/'accept-driver.py')
r=execute([sys.executable,'-X','utf8','tools/evidence/gate.py','tests/runs/d1.03-matrix.json',*[str(p) for p in reports],'--output',str(out/'gate-summary.json')],root,out/'stdout.log',out/'stderr.log',600)
save_json(out/'command.json',r)
assert r['status']=='Exited' and r['exit_code']==0 and r['process_tree']['active_after']==0
gate=read_json(out/'gate-summary.json');assert gate['gate_status']=='Passed',gate
shutil.copyfile('evidence/D1.03/source-provenance-fb08a19.json',out/'source-provenance.json')
shutil.copyfile('tests/runs/d1.03-matrix.json',out/'matrix.json')
records=[read_json(p) for p in reports]
context={'review_mode':'ai-self-review','human_review_required':False,'policy':'docs/reviews/automatic-acceptance-policy.json','policy_sha256':sha_file('docs/reviews/automatic-acceptance-policy.json'),'gate_summary_sha256':sha_file(out/'gate-summary.json'),'implementation_commit':records[0]['source']['commit'],'inputs_sha256':records[0]['source']['build_inputs_sha256'],'ctest_executions':sum(len(r['tests']['expected']) for r in records),'checks_passed':sum(sum(c['status']=='Passed' for c in r['checks']) for r in records),'scope':'仅D1.03注册批次及不可变目录；G1仍InProgress。未执行后续Invocation、Host或产品模块。'}
save_json(out/'acceptance-context.json',context)
print(out.relative_to(root));print(context)
