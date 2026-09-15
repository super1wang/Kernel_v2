from pathlib import Path
import os,sys,json
R=Path(__file__).resolve().parents[1];os.chdir(R);sys.path.insert(0,str(R))
from tools.evidence.process import execute
from tools.footprint.analyze import owned_success
os.environ['PATH']='E:/vs2022IDE/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin;'+str(R/'build/runtime/python311')+';'+os.environ['PATH']
os.environ['PYTHONPATH']=str(R/'build/python-deps')+';'+str(R)
os.environ['MSBUILDDISABLENODEREUSE']='1'
O=R/'evidence/B6/semantic-precision-5fd2310';D=O/'matrix-01';D.mkdir(parents=True);commands=[];reports=[]
for p in ('debug','release','asan'):
 print('START FORMAL '+p,flush=True)
 r=execute([sys.executable,'-X','utf8','tools/evidence/run.py','tests/runs/b6-final-linearization-win-msvc-'+p+'.json'],R,D/(p+'-stdout.log'),D/(p+'-stderr.log'),2400)
 r['label']=p;commands.append(r);(D/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 output=(D/(p+'-stdout.log')).read_text(errors='replace');print(output[-2500:],flush=True)
 if not owned_success(r):print((D/(p+'-stderr.log')).read_text(errors='replace')[-2500:],flush=True);sys.exit(1)
 paths=[R/line.split('; ',1)[1] for line in output.splitlines() if line.startswith('D4.04 win-msvc-')]
 assert len(paths)==1,output
 report=json.loads(paths[0].read_text());assert report['automated_status']=='Passed' and report['package_status']=='Passed',report.get('collection_issues')
 reports.append(str(paths[0]));(O/'matrix-reports.json').write_text(json.dumps(reports,indent=2)+'\n')
 print('PASS FORMAL '+p,flush=True)
print('FORMAL MATRIX COMPLETE',flush=True)
