"""从实际授权runner发现固定命名用例，不使用expected构造实际注册。"""
import argparse,json,os,re,subprocess,sys
from pathlib import Path
def main():
 p=argparse.ArgumentParser()
 for n in ('binary','output','runtime-dir','includes','generator','platform','toolset','sdk','config','work','root-build','target-metadata'):p.add_argument('--'+n,required=True)
 a=p.parse_args();env=dict(os.environ);env['PATH']=a.runtime_dir+os.pathsep+env.get('PATH','')
 r=subprocess.run([a.binary,'--list'],capture_output=True,check=True,env=env)
 names=r.stdout.decode().splitlines()
 if not names or len(names)!=len(set(names)) or not all(re.fullmatch(r'T(?:07|19|20)[.]policy[.][a-z_0-9]+',n) for n in names):raise ValueError('invalid native policy')
 out=Path(a.output);(out.parent/('policy-discovery-'+a.config+'-stdout.log')).write_bytes(r.stdout);(out.parent/('policy-discovery-'+a.config+'-stderr.log')).write_bytes(r.stderr)
 def q(v):return '[==['+str(v)+']==]'
 lines=[]
 children={'authentication_source','permit_origin_binding','group_substitution','transmission_start_arbitration','internal_component_boundary'}
 for n in names:
  argv=[a.binary,n]
  if n.split('.')[-1] in children:
   argv=[sys.executable,'-X','utf8',str(Path(__file__).with_name('verify_children.py')),'--case',n]
   for k,v in vars(a).items():
    if k!='output':argv+=['--'+k.replace('_','-'),v]
  lines += ['add_test('+q(n)+' '+' '.join(map(q,argv))+')','set_tests_properties('+q(n)+' PROPERTIES TIMEOUT 600 LABELS "D1.04" ENVIRONMENT "MSBUILDDISABLENODEREUSE=1" ENVIRONMENT_MODIFICATION '+q('PATH=path_list_prepend:'+a.runtime_dir)+')']
 out.write_text('\n'.join(lines)+'\n',encoding='utf-8');print(json.dumps({'actual_cases':names,'count':len(names)}))
if __name__=='__main__':main()
