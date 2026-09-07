"""按显式AI政策追加验收决策；不修改历史run或补造人工批准。"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.evidence.common import read_json, save_json, sha_file, sha_bytes, digest, under, now
from tools.evidence.validate import audit
LOADED_RULES=[{'path':p.relative_to(ROOT).as_posix(),'sha256':sha_file(p)} for p in sorted((ROOT/'tools/evidence').glob('*.py'))]+[{'path':'schemas/evidence-v1.schema.json','sha256':sha_file(ROOT/'schemas/evidence-v1.schema.json')}]


def matrix_errors(required, found):
    errors=[]
    if not required or len(required)!=len(set(required)): errors.append('empty or duplicate required matrix')
    if len(found)!=len(set(found)): errors.append('duplicate selected run')
    if set(required)-set(found): errors.append('missing required task/profile')
    if set(found)-set(required): errors.append('unexpected task/profile')
    return errors


def candidate_errors(root, commit, rows):
    """验证声明的历史实现Git字节，不把它表述为当前工作树复验。"""
    errors=[]
    if not re.fullmatch(r'[0-9a-f]{40}',commit): return ['invalid candidate commit']
    if not rows or len({row['path'] for row in rows})!=len(rows): return ['empty or duplicate source inputs']
    for row in rows:
        try:
            under(root,row['path'])
            raw=subprocess.check_output(['git','-C',str(root),'cat-file','blob',commit+':'+row['path']],stderr=subprocess.DEVNULL)
            if len(raw)!=row['size'] or sha_bytes(raw)!=row['sha256']: errors.append('candidate Git bytes differ: '+row['path'])
        except (OSError,ValueError,subprocess.CalledProcessError): errors.append('candidate Git input unavailable: '+row['path'])
    return errors


def accept(matrix_path, report_paths, policy_path, root=ROOT):
    from tools.evidence.review_policy import evaluate
    root=Path(root).resolve(); matrix_path=Path(matrix_path).resolve();policy_path=Path(policy_path).resolve()
    policy_relative=policy_path.relative_to(root).as_posix()
    matrix=read_json(matrix_path);policy_raw=policy_path.read_bytes();policy=read_json(policy_path)
    references={matrix_path:sha_file(matrix_path),policy_path:sha_bytes(policy_raw)}
    required=[(x['task_id'],x['profile']) for x in matrix['required_runs']]
    rows=[];errors=[];review_errors=[];identities=set();candidates={};found=[];review_complete=True
    binding={'format':'ock.review-policy-binding/1','mode':'ai-self-review','path':policy_relative,'sha256':sha_bytes(policy_raw)}
    decision_spec={'review_policy':binding,'review_required':['spec','code']}
    policy_input=[{'path':policy_relative,'sha256':sha_bytes(policy_raw),'size':len(policy_raw)}]
    def read_policy(name):
        if name!=policy_relative: raise ValueError('unexpected decision policy path')
        return policy_raw
    # 此policy_input仅为本验收决策输入；被审核实现摘要保持原候选摘要。
    for report_path in report_paths:
        path=Path(report_path).resolve();references[path]=sha_file(path);r=read_json(path)
        key=(r['task_id'],r['build']['profile']);found.append(key)
        problems,_=audit(path,None)
        errors.extend(str(key)+': '+p for p in problems)
        if r['automated_status']!='Passed': errors.append(str(key)+': automatic run did not pass')
        identity=(r['source']['commit'],r['source']['build_inputs_sha256'],r['build']['dependency_lock_sha256'])
        identities.add(identity);candidates[identity]=(r['source']['inputs'],r['source']['dirty'])
        # 原矩阵也必须属于被测来源，不能临时减少配置。
        matrix_rows=[row for row in r['source']['inputs'] if row['path']==matrix_path.relative_to(root).as_posix()]
        if len(matrix_rows)!=1 or matrix_rows[0]['sha256']!=sha_file(matrix_path): errors.append('gate matrix differs from tested source')
        records=[]
        for item in r['review']['records']:
            record=item['record']
            if record.get('review_kind') not in ('spec','code'): continue
            try:
                real=under(root,item['path']);references[real]=item['sha256']
                if sha_file(real)!=item['sha256'] or read_json(real)!=record: review_errors.append('AI review snapshot differs: '+item['path'])
                if not record.get('evidence'): review_errors.append('AI review lacks technical evidence: '+item['path'])
                for evidence in record.get('evidence',[]):
                    references[under(root,evidence['path'])]=evidence['sha256']
                    if sha_file(under(root,evidence['path']))!=evidence['sha256']: review_errors.append('AI review evidence changed: '+evidence['path'])
            except (OSError,ValueError,KeyError) as exc: review_errors.append('AI review evidence unavailable: '+str(exc))
            records.append(record)
        problems,approved=evaluate(decision_spec,records,r['task_id'],r['source']['build_inputs_sha256'],read_policy,policy_input)
        review_errors.extend(str(key)+': '+p for p in problems);review_complete=review_complete and approved
        rows.append({'task_id':key[0],'profile':key[1],'report':path.relative_to(root).as_posix(),'sha256':sha_file(path),'run_id':r['run_id'],'original_package_status':r['package_status'],'original_source_dirty':r['source']['dirty'],'ctest_executions':sum(len(x['executed']) for x in r['tests']['rounds']),'technical_review_complete':approved})
    errors.extend(matrix_errors(required,found))
    if len(identities)!=1: errors.append('gate requires one final implementation identity')
    for identity,(source_rows,dirty) in candidates.items():
        # dirty标志可由未跟踪证据触发；是否对应提交只由每个实际输入的Git字节决定。
        errors.extend(candidate_errors(root,identity[0],source_rows))
    for path,expected_hash in references.items():
        if not path.is_file() or sha_file(path)!=expected_hash: errors.append('decision input changed: '+str(path))
    for row in LOADED_RULES:
        if sha_file(ROOT/row['path'])!=row['sha256']: errors.append('acceptance rules changed; start a fresh process')
    if not report_paths: review_complete=False
    # 完整自动事实及两类AI复核缺一不可；旧human状态只是原记录，不重写。
    status='Failed' if errors or review_errors else 'Passed' if review_complete else 'InProgress'
    return {'format':'ock.automatic-gate-acceptance/1','created_at':now(),'gate_id':matrix['gate_id'],'review_mode':'ai-self-review','human_review_required':False,'policy':{'path':policy_relative,'sha256':sha_file(policy_path),'version':policy.get('version')},'rules':LOADED_RULES,'decision_inputs':[{'path':p.relative_to(root).as_posix(),'sha256':h} for p,h in sorted(references.items())],'matrix':{'path':matrix_path.relative_to(root).as_posix(),'sha256':sha_file(matrix_path)},'implementation_identities':[{'commit':x[0],'inputs_sha256':x[1],'dependency_lock_sha256':x[2]} for x in sorted(identities)],'runs':rows,'errors':errors,'review_errors':review_errors,'automated_status':'Failed' if errors else 'Passed','gate_status':status,'note':'本决策验收列出的精确已提交历史实现及原始运行；不是对后续源码执行过同一测试的声明。所有旧报告及人工字段保持原样。'}


def main():
    parser=argparse.ArgumentParser();parser.add_argument('matrix',type=Path);parser.add_argument('reports',nargs='+',type=Path);parser.add_argument('--policy',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.exists(): parser.error('acceptance output must be a new path')
    result=accept(args.matrix,args.reports,args.policy)
    args.output.parent.mkdir(parents=True,exist_ok=True);save_json(args.output,result)
    print(json.dumps({'gate':result['gate_id'],'status':result['gate_status'],'errors':result['errors'],'review_errors':result['review_errors']},ensure_ascii=False))
    return result['gate_status']!='Passed'
if __name__=='__main__':sys.exit(main())
