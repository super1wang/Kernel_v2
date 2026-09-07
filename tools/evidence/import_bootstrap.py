"""只导入历史可验证字节事实；不为缺失的退出/构建绑定补造成功。"""
import argparse
import json
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.evidence.common import read_json,save_json,sha_file,now

def inventory(folder):
    folder=Path(folder).resolve();files=[];issues=[];references=0
    for path in sorted(folder.rglob('*')):
        if not path.is_file():continue
        files.append({'path':path.relative_to(folder).as_posix(),'sha256':sha_file(path),'size':path.stat().st_size})
        if path.suffix!='.json':continue
        try: value=read_json(path)
        except (OSError,ValueError) as exc:
            issues.append(str(path)+': '+str(exc));continue
        def visit(value):
            nonlocal references
            if isinstance(value,dict):
                if set(('path','sha256')).issubset(value) and isinstance(value['path'],str):
                    target=path.parent/value['path']
                    if target.is_file() and target.resolve().is_relative_to(folder):
                        references+=1
                        if sha_file(target)!=value['sha256']:issues.append('historical referenced bytes differ: '+str(target))
                for child in value.values():visit(child)
            elif isinstance(value,list):
                for child in value:visit(child)
        visit(value)
    return {'format':'ock.bootstrap-import/1','imported_at':now(),'root':str(folder),'files':files,'verified_local_hash_references':references,'issues':issues,'automated_status':'Failed' if issues else 'Incomplete','package_status':'Failed' if issues else 'InProgress','limitations':['历史格式没有统一且完整的当次配置、发现、逐轮JUnit和必需审批绑定；保留原始退出/快照，不升级为正式Passed。','当前D0.01至D0.06使用正式采集器重新验证；本索引不能替代当前运行。']}
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();result=inventory(args.folder);save_json(args.output,result);print(result['automated_status'],len(result['files']));raise SystemExit(bool(result['issues']))
