from pathlib import Path
import os,sys,json,subprocess
R=Path(__file__).resolve().parents[1];os.chdir(R);sys.path.insert(0,str(R))
from tools.evidence.process import execute
from tools.footprint.analyze import owned_success
os.environ['PATH']='E:/vs2022IDE/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin;'+str(R/'build/runtime/python311')+';'+os.environ['PATH']
os.environ['PYTHONPATH']=str(R/'build/python-deps')+';'+str(R)
O=R/'evidence/B6/semantic-precision-912cb75';source='912cb7554cd4c78ea9bf6db5615a2875d215e5ed'
reports=json.loads((O/'matrix-reports-02.json').read_text());assert len(reports)==3
commands=[]
def run(label,args,timeout):
 print('START '+label,flush=True)
 r=execute(args,R,O/(label+'-stdout.log'),O/(label+'-stderr.log'),timeout)
 r['label']=label;commands.append(r);(O/'gate-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 print((O/(label+'-stdout.log')).read_text(encoding='utf-8',errors='replace')[-2000:],flush=True)
 if not owned_success(r):print((O/(label+'-stderr.log')).read_text(encoding='utf-8',errors='replace')[-2000:],flush=True);raise SystemExit(1)
run('formal-acceptance',[sys.executable,'-X','utf8','tools/evidence/accept_gate.py','tests/runs/b6-semantic-v2-matrix.json',*reports,'--policy','docs/reviews/automatic-acceptance-policy.json','--output',str(O/'formal-acceptance-02.json')],300)
print('START FOOTPRINT',flush=True)
r=subprocess.run([sys.executable,'-u','-X','utf8','build/b6v2-wait-footprint-run.py']);assert r.returncode==0
old=R/'evidence/B6/semantic-precision-32c38ad'
(O/'verify-footprint.py').write_bytes((old/'verify-footprint.py').read_bytes())
run('footprint-verification',[sys.executable,'-X','utf8',str(O/'verify-footprint.py')],300)
s=(old/'finalize.py').read_text(encoding='utf-8').replace('32c38ad54e875a2812a875685421a46074c292ff',source).replace('开发失败、首次审核附件缺少摘要的验收失败、所有首轮机器报告保持原文；最终仅选用同一来源和完整审核绑定的第二轮三配置及新来源 footprint。','开发失败、旧候选证据、912cb75 首轮 Debug 安装目录重命名 WinError 5 报告保持原文；最终仅选用新隔离目录完成的同一来源三配置与新来源 footprint。')
(O/'finalize.py').write_text(s,encoding='utf-8')
run('finalize',[sys.executable,'-X','utf8',str(O/'finalize.py')],60)
print('ALL FINAL GATES COMPLETE',flush=True)
