"""D1.05 根工程开发集成：实际 owned 命令，不替代正式三配置门禁。"""
import json
from pathlib import Path
import sys
import uuid
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute

out=ROOT/'evidence/bootstrap/D1.05'/('integration-'+uuid.uuid4().hex[:12])
out.mkdir(parents=True,exist_ok=False)
(out/'driver.py').write_bytes(Path(__file__).read_bytes())
rows=[]
commands=[('build',['cmake','--build',str(ROOT/'build/d1.05-dev'),'--config','Debug','--parallel','2','--','/nr:false']),
          ('ctest',['ctest','--test-dir',str(ROOT/'build/d1.05-dev'),'-C','Debug','-R','[.]native[.]','--output-on-failure','--no-tests=error','--output-junit',str(out/'junit.xml')])]
for name,argv in commands:
    row=execute(argv,ROOT,out/(name+'-stdout.log'),out/(name+'-stderr.log'),1800)
    rows.append(row)
    (out/'commands.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
    print(name,row['exit_code'],row['status'],out,flush=True)
    if row['status']!='Exited' or row['exit_code']!=0 or row['process_tree']['active_after']!=0:
        sys.exit(1)
