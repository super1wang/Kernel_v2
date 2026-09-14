from pathlib import Path
import json,sys,subprocess,xml.etree.ElementTree as ET
sys.path.insert(0,str(Path.cwd()))
from tools.evidence.common import save_json,sha_file,now
root=Path.cwd();out=root/'evidence/B6/closure-a03304a';out.mkdir(parents=True,exist_ok=True)
commit='a03304a407b7716c1c68cb601cedc7c08d2ffa2c';digest='5d15ef17eb4e1305e9275a077a80c3f8a2fde46d24b6f553b4d886f7607d1e9a'
def ref(p):
 p=Path(p);return {'path':p.resolve().relative_to(root).as_posix(),'sha256':sha_file(p)}
reports=[];cost=[]
for profile in ['debug','release','asan']:
 found=[]
 for p in (root/'evidence/a03304a407b7-5d15ef17eb4e'/('win-msvc-'+profile)/'D4.04').glob('*/report.json'):
  v=json.loads(p.read_text())
  if v['automated_status']=='Passed':found.append((p,v))
 assert len(found)==1,(profile,len(found))
 p,v=found[0];assert v['source']['commit']==commit and v['source']['build_inputs_sha256']==digest and len(v['tests']['expected'])==89
 reports.append(p)
 junit=p.parent/'round-001-junit.xml';tree=ET.parse(junit);case=tree.find(".//testcase[@name='T23.state.structural_sharing_cost']");assert case is not None
 log=root/('build/b6-formal-'+profile)/'Testing/Temporary/LastTest.log'
 raw=log.read_bytes();text=raw.decode('utf-8',errors='replace');start=text.index('Testing: T23.state.structural_sharing_cost');end=text.find('Testing:',start+10);block=text[start:end if end>=0 else None]
 lines=[line for line in block.splitlines() if line.startswith('{"objects":')];full='\n'.join(lines)+'\n'
 prefix=case.findtext('system-out','').split('...\n[This part',1)[0];assert full.startswith(prefix),(profile,'JUnit prefix differs')
 from datetime import datetime
 assert datetime.fromisoformat(v['started_at']).timestamp()-5<=log.stat().st_mtime<=datetime.fromisoformat(v['finished_at']).timestamp()+5
 preserved=out/(profile+'-LastTest.log');preserved.write_bytes(raw)
 rows=[json.loads(line) for line in lines]
 assert {(r['objects'],r['changed'],r['references']) for r in rows}=={(n,k,b) for n in [1000,10000,100000] for k in [1,10,100] for b in [0,1]}
 assert len(rows)==18 and all(r['status']=='Passed' and r['index_after_reclaim_bytes']==r['baseline_index_bytes'] for r in rows)
 cost.append({'profile':'win-msvc-'+profile,'raw_junit':ref(junit),'raw_complete_ctest_log':ref(preserved),'captured_after_run':True,'junit_prefix_verified':True,'log_mtime_within_run':True,'samples':rows})
accept=out/'matrix-acceptance.json';assert not accept.exists()
command=[sys.executable,'-X','utf8','tools/evidence/accept_gate.py','tests/runs/b6-closure-matrix.json',*[str(p) for p in reports],'--policy','docs/reviews/automatic-acceptance-policy.json','--output',str(accept)]
with (out/'accept-stdout.log').open('wb') as stdout,(out/'accept-stderr.log').open('wb') as stderr:
 result=subprocess.run(command,stdout=stdout,stderr=stderr)
assert result.returncode==0,(out/'accept-stdout.log').read_text()
accepted=json.loads(accept.read_text());assert accepted['gate_status']=='Passed' and not accepted['errors'] and not accepted['review_errors']
save_json(out/'cost-samples.json',{'status':'Passed','source_commit':commit,'inputs_sha256':digest,'scope':'从最终三配置原始 CTest LastTest.log 提取，补充归档；JUnit 1024 字节截断前缀匹配并核对日志修改时间位于原运行区间，每配置 18 组合各一次真实采样；不声明统计性能 SLA。reclaim 为释放域和候选差额后回到仍保活基线根的索引占用。','profiles':cost})
state=root/'evidence/bootstrap/B6/closure-projections-900ec73eff';assert json.loads((state/'result.json').read_text())['status']=='Passed'
tree=ET.parse(state/'state.xml');cases=tree.findall('.//testcase');assert len(cases)==9 and all(c.find('failure') is None and c.find('error') is None and c.find('skipped') is None for c in cases)
runtime=root/'evidence/bootstrap/B2/sdk-installed_host-bf219542fe/result.json';assert json.loads(runtime.read_text())['status']=='Passed'
footprint=root/'evidence/B6/closure-6b817c3/footprint-integrity.json';assert json.loads(footprint.read_text())['status']=='Passed'
save_json(out/'closure-inputs.json',{'status':'ReadyForPackageDAG','created_at':now(),'source_commit':commit,'inputs_sha256':digest,'source_input_count':647,'matrix':ref(accept),'runs':[ref(p) for p in reports],'cost':ref(out/'cost-samples.json'),'state_native':ref(state/'result.json'),'state_native_junit':ref(state/'state.xml'),'runtime_install':ref(runtime),'footprint':ref(footprint),'supporting_archives':ref(root/'evidence/B6/closure-6b817c3/supporting-archives.json'),'independent_technical_conclusions':ref(root/'docs/reviews/B6-closure-package-conclusions.md')})
print('Final matrix acceptance Passed; 54 scale samples; StateNative 9; ready for package DAG')
