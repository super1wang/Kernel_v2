"""组合既有正式矩阵验收与 footprint 完整性记录；不改写原始报告。"""
from pathlib import Path
import json,hashlib,subprocess
from datetime import datetime,timezone
O=Path(__file__).resolve().parent;R=O.parents[2]
def read(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def ref(p):return {'path':p.relative_to(R).as_posix(),'sha256':sha(p)}
def verify_ref(v):assert sha(R/v['path'])==v['sha256'],v['path']
assert not (O/'acceptance.json').exists()
formal=read(O/'formal-acceptance-02.json');foot=read(O/'footprint-integrity.json')
assert formal['gate_status']=='Passed' and not formal['errors'] and not formal['review_errors']
assert foot['status']=='Passed' and foot['budget_limits_and_pilots_unchanged']
source='912cb7554cd4c78ea9bf6db5615a2875d215e5ed'
assert len(formal['implementation_identities'])==1 and formal['implementation_identities'][0]['commit']==source==foot['production_commit']
assert len(formal['runs'])==3 and len(foot['reports'])==10 and len(foot['pruning'])==6
for v in formal['decision_inputs']:verify_ref(v)
counts={}
for run in formal['runs']:
 p=R/run['report'];assert sha(p)==run['sha256'];r=read(p)
 assert r['automated_status']=='Passed' and r['package_status']=='Passed' and len(r['tests']['expected'])==31
 assert len(r['tests']['rounds'])==1 and len(r['tests']['rounds'][0]['executed'])==31
 counts[run['profile']]={'expected':31,'executed':31,'status':'Passed'}
for report in foot['reports']:
 for key in ('report','commands','samples','artifacts'):verify_ref(report[key])
for key in ('source_binding','expected','commands','budgets','previous_budgets'):verify_ref(foot[key])
for p in ('packages','apps','tests','tools','cmake','sdk','CMakeLists.txt','CMakePresets.json','dependencies.lock'):
 assert not subprocess.check_output(['git','diff',source,'--',p],cwd=R),p
value={'format':'ock.development-batch-closure/1','batch_id':'B6-semantic-v2','batch_status':'Passed','gate_status':'Passed','created_at':datetime.now(timezone.utc).isoformat(),'source_commit':source,'tree_sha':subprocess.check_output(['git','rev-parse',source+'^{tree}'],cwd=R,text=True).strip(),'inputs_sha256':formal['implementation_identities'][0]['inputs_sha256'],'formal_acceptance':ref(O/'formal-acceptance-02.json'),'footprint_integrity':ref(O/'footprint-integrity.json'),'matrix':counts,'native_footprint':'7/7 Passed','embedded_footprint':'3/3 Passed including Release startup','B7_entry':'GO','G4':'NotStarted','scope':'B6 semantic precision v2 F1-F5；v2 取代 v1 最终放行决定，历史 B6/C1-C6 Passed 与旧来源机器事实保留。未进入 B7 实现。','failure_policy':'开发失败、旧候选证据、912cb75 首轮 Debug 安装目录重命名 WinError 5 报告保持原文；最终仅选用新隔离目录完成的同一来源三配置与新来源 footprint。'}
(O/'acceptance.json').write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('B6 semantic precision v2: Passed; source '+source)
