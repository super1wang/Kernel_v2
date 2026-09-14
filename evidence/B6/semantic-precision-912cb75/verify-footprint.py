"""B6 最终精确化缺失门禁的来源、预算与原始证据复核。"""
import hashlib,json,sys,shutil,subprocess
from pathlib import Path
from datetime import datetime,timezone
O=Path(__file__).resolve().parent;R=O.parents[2];sys.path.insert(0,str(R))
from tools.footprint.formal import bounded_metrics
from tools.footprint.analyze import owned_success,require_complete,require_sampling_v2
from tools.footprint.windows_process import PHASES
from tools.footprint.pair_record import validate_record
from tools.footprint.allocation_record import validate_allocation
def read(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def item(p):return {'path':p.relative_to(R).as_posix(),'sha256':sha(p),'bytes':p.stat().st_size}
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def verify(items):
 for x in items:
  p=Path(x['path']);assert sha(p)==x['sha256'] and p.stat().st_size==x['bytes'],str(p)
source=read(O/'source-binding.json');expected=read(O/'expected.json');runs=read(O/'reports.json')
assert [{k:r[k] for k in ('kind','profile','mode')} for r in runs]==expected['jobs']
assert len(runs)==10 and expected['blocks']==6
for x in source['inputs']:
 p=R/x['path'];assert sha(p)==x['sha256'],str(p)
 assert hashlib.sha256(subprocess.check_output(['git','show',source['production_commit']+':'+x['path']],cwd=R)).hexdigest()==x['sha256']
old=read(O/'previous-budgets.json');budgets=read(R/'footprint-budgets.json')
for section in (None,'embedded'):
 a=old if section is None else old[section];b=budgets if section is None else budgets[section]
 assert a['budgets'].keys()==b['budgets'].keys()
 for k in a['budgets']:
  assert {x:v for x,v in a['budgets'][k].items() if x not in ('method_digest','approved_at')}=={x:v for x,v in b['budgets'][k].items() if x not in ('method_digest','approved_at')}
checks=[];pruning=[]
for run in runs:
 p=Path(run['report']);d=p.parent;r=read(p);samples=read(d/'samples.json');commands=read(d/'commands.json')
 assert r['status']=='Passed' and r['blocks']==6 and r['profile']==run['profile']
 assert [(x['block'],x['kind']) for x in samples]==[(b,k) for b in range(6) for k in 'ABBA']
 assert len({x['run_id'] for x in samples})==24
 assert all(x['identity']==r['method_digest'] for x in samples)
 for c in commands:
  verify(c['raw'])
  if c.get('label')=='injected':
   assert run['kind']=='native' and run['mode']=='allocation'
   tree=c['process_tree'];obs=c['observation']
   assert c['status']=='Exited' and c['exit_code']==1 and tree['assigned_before_resume'] and tree['active_after']==0 and not tree['terminated_owned_job']
   assert obs['status']=='Complete' and [x['phase'] for x in obs['phases']]==list(PHASES)
   require_sampling_v2(obs)
   records=[json.loads(s) for s in (d/'injected-stdout.log').read_text().splitlines()]
   validate_record(records[0],'native','allocation')
   validate_allocation(records[1],'native','Release' if run['profile']=='win-msvc-release' else 'Debug',run['profile']=='win-msvc-asan',injected=True)
  else:
   assert owned_success(c),c.get('label')
   if c.get('observation') is not None:require_complete(c)
 verify(read(d/'artifacts.json'))
 identity=read(d/'identity.json')
 for x in identity['sources']:assert sha(R/x['path'])==x['sha256']
 keys=[run['profile']+'/'+run['mode']] if run['kind']=='native' else r['budget_keys']
 selected=budgets['budgets'] if run['kind']=='native' else budgets['embedded']['budgets']
 for key in keys:
  assert source['methods'][run['kind']+'/'+key]==r['method_digest']==selected[key]['method_digest']
  bounded_metrics(r['startup_metrics'] if key.endswith('/startup') else r['metrics'],selected[key]['limits'])
 if run['kind']=='native':assert r['new_native_threads']==0 and len(r['run_ids'])==24
 else:
  assert r['kernel_thread_structural_upper_bound']==3
  if run['profile']!='win-msvc-release':
   assert r['allocation_zero_verified'] is True
   windows=[v for x in samples if x['kind']=='B' for v in x['allocation']['samples'] if v['zero_required']]
   assert len(windows)==480 and all(v['cpp']==0 and v['process_allocations']==0 for v in windows)
  else:assert len(read(d/'startup/samples.json'))==24
 checks.append({**run,'report':item(p),'commands':item(d/'commands.json'),'samples':item(d/'samples.json'),'artifacts':item(d/'artifacts.json'),'budget_keys':keys,'blocks':6,'samples_count':24,'commands_count':len(commands),'status':'Passed'})
for kind in ('native','embedded'):
 for profile in ('debug','release','asan'):
  producer=R/('build/b6-'+kind+'-'+profile);dest=O/'pruning'/(kind+'-'+profile);dest.mkdir(parents=True,exist_ok=True)
  graph=read(producer/'ock-target-graph.json');acq=read(producer/'dependency-acquisition.json')
  assert set(graph['targets'])==({'Foundation','CoreContracts','Runtime'} if kind=='native' else {'Foundation','CoreContracts','Runtime','Adapter::CpuPool'})
  assert set(acq['selected'])==({'expected'} if kind=='native' else {'expected','thread_pool'})
  for name in ('ock-target-graph.json','dependency-acquisition.json','CMakeCache.txt'):shutil.copyfile(producer/name,dest/name)
  pruning.append({'kind':kind,'profile':profile,'targets':list(graph['targets']),'selected':acq['selected'],'graph':item(dest/'ock-target-graph.json'),'acquisition':item(dest/'dependency-acquisition.json'),'status':'Passed'})
assert all(c['exit_code']==0 for c in read(O/'commands.json'))
assert not (O/'footprint-integrity.json').exists(), 'preserve prior verification output'
save(O/'footprint-integrity.json',{'status':'Passed','checked_at':datetime.now(timezone.utc).isoformat(),'production_commit':source['production_commit'],'source_binding':item(O/'source-binding.json'),'expected':item(O/'expected.json'),'commands':item(O/'commands.json'),'budgets':item(R/'footprint-budgets.json'),'previous_budgets':item(O/'previous-budgets.json'),'budget_limits_and_pilots_unchanged':True,'verified_source_inputs':len(source['inputs']),'reports':checks,'pruning':pruning,'scope':'B6 semantic v2 同一来源的 Native/Embedded 原预算 footprint 完整性；不单独授予最终放行。'})
print('PASS: 10 reports, 11 budget keys, 6 pruning configurations, source '+source['production_commit'])
