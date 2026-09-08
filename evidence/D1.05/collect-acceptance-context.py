from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.evidence.common import read_json,save_json,sha_file,now
out=ROOT/'evidence/D1.05/current-20260908T010205Z'
gate=read_json(out/'gate-summary.json')
assert gate['automated_status']==gate['gate_status']=='Passed'
assert not gate['errors'] and not gate['missing']
rows=[];identities=set()
for item in gate['runs']:
 p=Path(item['report']);assert sha_file(p)==item['sha256']
 r=read_json(p)
 assert r['automated_status']==r['package_status']=='Passed' and not r['errors']
 assert len(r['tests']['rounds'])==1
 executed=r['tests']['rounds'][0]['executed']
 assert sorted(x['name'] for x in executed)==sorted(r['tests']['expected'])
 assert all(x['status']=='Passed' for x in executed)
 assert len(r['checks'])==3 and all(x['status']=='Passed' for x in r['checks'])
 identities.add((r['source']['commit'],r['source']['build_inputs_sha256'],r['build']['dependency_lock_sha256']))
 rows.append({'profile':r['build']['profile'],'report':p.relative_to(ROOT).as_posix(),'sha256':sha_file(p),'ctest_executions':len(executed),'checks_passed':len(r['checks'])})
assert len(identities)==1 and sum(r['ctest_executions'] for r in rows)==851
review=ROOT/'evidence/D1.05/final-acceptance-review.md'
assert review.is_file()
context={'format':'ock.acceptance-context/1','created_at':now(),'task_id':'D1.05','review_mode':'ai-self-review','human_review_required':False,'source_identity':list(next(iter(identities))),'gate_summary_sha256':sha_file(out/'gate-summary.json'),'policy_sha256':sha_file(ROOT/'docs/reviews/automatic-acceptance-policy.json'),'final_ai_review':{'path':review.relative_to(ROOT).as_posix(),'sha256':sha_file(review)},'git_input_verification':{'path':'evidence/D1.05/implementation-source.json','sha256':sha_file(ROOT/'evidence/D1.05/implementation-source.json')},'runs':rows,'ctest_executions':sum(r['ctest_executions'] for r in rows),'checks_passed':sum(r['checks_passed'] for r in rows),'scope':'仅D1.05内核Native Read/Compute及规划验证消费者；G1仍InProgress，不声明Host、异步或产品模块完成。'}
assert not (out/'acceptance-context.json').exists()
save_json(out/'acceptance-context.json',context)
print(context)
