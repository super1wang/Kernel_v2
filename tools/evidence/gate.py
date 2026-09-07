"""按固定门禁矩阵汇总真实 run；拒绝混合源码、缺配置和自报审批。"""
import argparse
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.evidence.common import read_json, save_json, sha_file, now
from tools.evidence.validate import audit

def summarize(matrix_path, report_paths, root):
    matrix=read_json(matrix_path);rows=[];errors=[];identities=set();found=set()
    required={(r['task_id'],r['profile']) for r in matrix['required_runs']}
    if len(required)!=len(matrix['required_runs']) or not required: raise ValueError('empty or duplicate gate matrix')
    for path in report_paths:
        try:
            report=read_json(path);key=(report['task_id'],report['build']['profile'])
            if key in found: errors.append('duplicate run selection: '+str(key))
            found.add(key)
            problems,package=audit(path,root)
            errors.extend(str(key)+': '+problem for problem in problems)
            identities.add((report['source']['commit'],report['source']['build_inputs_sha256'],report['build']['dependency_lock_sha256']))
            rows.append({'task_id':key[0],'profile':key[1],'report':str(Path(path).resolve()),'sha256':sha_file(path),'automated_status':'Failed' if problems else 'Passed','package_status':package,'review_required':report['review']['required']})
        except (OSError,ValueError,KeyError) as exc:
            errors.append('unreadable run: '+str(exc))
    missing=sorted(required-found);extra=sorted(found-required)
    if extra: errors.append('unexpected task/profile: '+str(extra))
    if len(identities)>1: errors.append('cannot combine different final source/dependency identities')
    automated='Failed' if errors else 'Incomplete' if missing else 'Passed'
    package='Failed' if errors else 'InProgress' if missing or any(r['package_status']!='Passed' for r in rows) else 'Passed'
    return {'format':'ock.gate-summary/1','gate_id':matrix['gate_id'],'generated_at':now(),'matrix_sha256':sha_file(matrix_path),'rules_sha256':sha_file(Path(__file__)),'required_runs':matrix['required_runs'],'runs':rows,'missing':missing,'errors':errors,'automated_status':automated,'gate_status':package,'note':'自动验证与人工放行分开；历史批准另行保留，不凭历史批准推导当前变更已批准。'}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('matrix',type=Path);parser.add_argument('reports',nargs='+',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();result=summarize(args.matrix,args.reports,ROOT);save_json(args.output,result)
    print(result['gate_id']+': automated='+result['automated_status']+', gate='+result['gate_status'])
    return result['automated_status']!='Passed'
if __name__=='__main__':sys.exit(main())
