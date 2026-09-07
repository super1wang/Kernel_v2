"""仅记录 D0.03 验证用 Python 依赖；不替代 D0.06 产品依赖锁。"""
import hashlib
import importlib.metadata
import json
from pathlib import Path
import platform
import sys

ROOT=Path(__file__).resolve().parents[2]
DEPS=ROOT/'build/python-deps'

def main():
    sys.path.insert(0,str(DEPS))
    result={'scope':'D0.03 development validation only','python':platform.python_version(),'distributions':[]}
    for line in (ROOT/'tests/model/requirements-validation.txt').read_text(encoding='utf-8').splitlines():
        line=line.strip()
        if not line or line.startswith('#'):continue
        name,version=line.split('==')
        dist=importlib.metadata.distribution(name)
        if dist.version!=version:raise ValueError(f'{name}: version differs from validation requirements')
        files=[]
        for item in dist.files or []:
            path=Path(dist.locate_file(item)).resolve()
            # pip --target 将控制台脚本搬到目标 bin；RECORD 可能仍是原 wheel 的相对布局。
            if not path.is_relative_to(DEPS.resolve()) and str(item).replace('\\','/').startswith('../../bin/'):
                path=(DEPS/'bin'/Path(item).name).resolve()
            if not path.is_relative_to(DEPS.resolve()):raise ValueError(f'{name}: dependency is outside isolated validation directory')
            if '__pycache__' in path.parts or path.suffix=='.pyc':continue
            if not path.is_file():raise ValueError(f'{name}: missing distribution file {item}')
            files.append({'path':path.relative_to(DEPS).as_posix(),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
        if not files:raise ValueError(f'{name}: no distribution files')
        result['distributions'].append({'name':name,'version':version,'files':sorted(files,key=lambda f:f['path'])})
    print(json.dumps(result,ensure_ascii=False,indent=2))
    return 0

if __name__=='__main__':sys.exit(main())
