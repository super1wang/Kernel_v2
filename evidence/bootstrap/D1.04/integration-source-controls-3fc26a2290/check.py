import ast,json,sys,uuid,os
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root))
from tools.evidence.common import inputs,read_json,sha_file
out=Path(sys.argv[1]).resolve();mode=sys.argv[2]
source=out/('old-driver.py' if mode=='old' else 'new-driver.py')
tree=ast.parse(source.read_text(encoding='utf8'));body=next(n for n in tree.body if isinstance(n,ast.Try)).body
start=next(i for i,n in enumerate(body) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==('changed' if mode=='old' else 'after_rows') for t in n.targets))
block=ast.Module(body=body[start:],type_ignores=[]);code=compile(block,str(source),'exec')
results=[]
for scenario in (['added'] if mode=='old' else ['added','removed','modified','unchanged']):
 fixture=root/'build'/('integration-source-control-'+uuid.uuid4().hex);fixture.mkdir();(fixture/'src').mkdir();(fixture/'src/a.txt').write_text('original');(fixture/'expected.json').write_text('{}');(fixture/'lock.json').write_text('{}')
 spec={'expected_manifest':'expected.json','dependency_lock':'lock.json','required_artifacts':[],'source_patterns':['src/**']};(fixture/'spec.json').write_text(json.dumps(spec))
 rows=inputs(fixture,spec,'spec.json')
 if scenario=='added':(fixture/'src/b.txt').write_text('new')
 elif scenario=='removed':(fixture/'src/a.txt').unlink()
 elif scenario=='modified':(fixture/'src/a.txt').write_text('changed')
 namespace={'ROOT':fixture,'spec_path':'spec.json','spec':spec,'rows':rows,'inputs':inputs,'read_json':read_json,'sha_file':sha_file,'result':{},'cases':[]}
 os.chdir(fixture);rejected=False
 try:exec(code,namespace)
 except AssertionError:rejected=True
 finally:os.chdir(root)
 row={'mode':mode,'scenario':scenario,'fixture':str(fixture),'rejected':rejected,'result':namespace['result']};results.append(row);print(json.dumps(row),flush=True)
 (out/(mode+'-results.json')).write_text(json.dumps(results,indent=2))
 assert rejected==(scenario!='unchanged'),row
 assert namespace['result']['status']==('Passed' if scenario=='unchanged' else 'SourceChanged'),row
 if mode=='new' and scenario!='unchanged':assert namespace['changed'][scenario],row
