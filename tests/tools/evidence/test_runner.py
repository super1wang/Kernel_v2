"""真实 CMake/CTest 夹具验证证据采集拒绝条件；不伪造成功输出。"""
import copy
import zipfile
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.evidence.run import run
from tools.evidence.validate import validate_report

class EvidenceTests(unittest.TestCase):
    def setUp(self):
        fixture_root=ROOT/'build/evidence-fixtures'
        fixture_root.mkdir(parents=True,exist_ok=True)
        self.root=Path(tempfile.mkdtemp(prefix='ock-evidence-fixture-',dir=fixture_root))
        self.write('case.py',"print('fixture executed')\n")
        self.write('dependencies.lock',json.dumps({'format':'fixture-deps/1','dependencies':[]}))
        self.write('expected.json',json.dumps({'task_id':'D0.06','cases':[{'id':'T23.fixture.executed','repeat_required':1}],'checks':[]}))
        self.write_cmake()
        self.spec={'format':'ock.run-manifest/1','task_id':'D0.06','profile':'fixture-debug','configuration':'Debug','preset':None,
          'source_patterns':['*.py','*.json','CMakeLists.txt','dependencies.lock'],
          'required_artifacts':['case.py','dependencies.lock'], 'expected_manifest':'expected.json','dependency_lock':'dependencies.lock',
          'build_dir':'build','configure':['cmake','-S','.','-B','build','-G','Ninja','-DCMAKE_BUILD_TYPE=Debug'],'build':['cmake','--build','build','--config','Debug'],
          'build_outputs':['build/CMakeCache.txt','build/fixture.bin'],'test_regex':'^T23[.]fixture[.]','repeat':1,'timeout_seconds':20,'checks':[],'review_records':[]}
        self.write('run.json',json.dumps(self.spec))
        subprocess.run(['git','init','-q'],cwd=self.root,check=True,capture_output=True)
        subprocess.run(['git','add','.'],cwd=self.root,check=True,capture_output=True)
        subprocess.run(['git','-c','user.name=EvidenceFixture','-c','user.email=fixture@invalid','commit','-qm','fixture'],cwd=self.root,check=True,capture_output=True)
    def write(self,name,text):
        p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8',newline='\n')
    def write_cmake(self,extra=''):
        py=Path(sys.executable).as_posix()
        self.write('CMakeLists.txt',f'cmake_minimum_required(VERSION 3.25)\nproject(Fixture NONE)\nenable_testing()\nfile(WRITE "${{CMAKE_BINARY_DIR}}/fixture.bin" "binary-v1")\nadd_test(NAME T23.fixture.executed COMMAND "{py}" -X utf8 "${{CMAKE_SOURCE_DIR}}/case.py")\n'+extra)
    def collect(self,**changes):
        self.spec.update(changes);self.write('run.json',json.dumps(self.spec))
        path=run(self.root,self.root/'run.json')
        frozen=self.root/'original-runs';frozen.mkdir(exist_ok=True)
        with zipfile.ZipFile(frozen/(path.parent.name+'.zip'),'w',zipfile.ZIP_DEFLATED) as z:
            for file in path.parent.iterdir():
                if file.is_file():z.write(file,file.name)
        return path,json.loads(path.read_text(encoding='utf-8'))
    def test_T23_evidence_actual_execution(self):
        path,r=self.collect()
        self.assertEqual(r['automated_status'],'Passed',r['errors'])
        self.assertEqual(r['package_status'],'InProgress')
        self.assertEqual(r['tests']['rounds'][0]['executed'][0]['status'],'Passed')
        self.assertEqual(validate_report(path,self.root),[])
    def test_T23_evidence_print_success_exit_failure(self):
        self.write('case.py',"print('Passed');raise SystemExit(3)\n")
        _,r=self.collect();self.assertEqual(r['automated_status'],'Failed')
    def test_T23_evidence_zero_tests(self):
        self.write_cmake('set_property(DIRECTORY PROPERTY TESTS "")\n')
        # CTest 的实际空发现由删除真实 add_test 产生，固定 expected 不变。
        p=self.root/'CMakeLists.txt';p.write_text('\n'.join(x for x in p.read_text().splitlines() if not x.startswith('add_test') and not x.startswith('set_property'))+'\n')
        _,r=self.collect();self.assertNotEqual(r['automated_status'],'Passed')
    def test_T23_evidence_wrong_filter(self):
        _,r=self.collect(test_regex='^missing$');self.assertNotEqual(r['automated_status'],'Passed')
    def test_T23_evidence_removed_expected_case(self):
        d=json.loads((self.root/'expected.json').read_text());d['cases'].append({'id':'T23.fixture.deleted','repeat_required':1});self.write('expected.json',json.dumps(d))
        _,r=self.collect();self.assertNotEqual(r['automated_status'],'Passed')
    def test_T23_evidence_skip_rejected(self):
        self.write('case.py','raise SystemExit(77)\n');self.write_cmake('set_tests_properties(T23.fixture.executed PROPERTIES SKIP_RETURN_CODE 77)\n')
        _,r=self.collect();self.assertEqual(r['automated_status'],'Failed')
    def test_T23_evidence_old_report_not_reused(self):
        self.write('build/old-junit.xml','<testsuite tests="1"><testcase name="T23.fixture.executed"/></testsuite>')
        self.write('case.py','raise SystemExit(9)\n')
        path,r=self.collect();self.assertEqual(r['automated_status'],'Failed')
        self.assertNotIn('old-junit.xml',path.read_text())
    def test_T23_evidence_repeat_required(self):
        d=json.loads((self.root/'expected.json').read_text());d['cases'][0]['repeat_required']=2;self.write('expected.json',json.dumps(d))
        _,r=self.collect(repeat=1);self.assertNotEqual(r['automated_status'],'Passed')
    def test_T23_evidence_all_rounds_retained(self):
        path,r=self.collect(repeat=2);self.assertEqual(r['automated_status'],'Passed',r['errors']);self.assertEqual(len(r['tests']['rounds']),2)
        self.assertNotEqual(r['tests']['rounds'][0]['junit']['path'],r['tests']['rounds'][1]['junit']['path'])
        r['tests']['rounds'].pop();path.write_text(json.dumps(r));self.assertTrue(validate_report(path,self.root))
    def test_T23_evidence_missing_report(self):
        path,r=self.collect();(path.parent/r['tests']['rounds'][0]['junit']['path']).unlink();self.assertTrue(validate_report(path,self.root))
    def test_T23_evidence_corrupt_report(self):
        path,r=self.collect();(path.parent/r['tests']['rounds'][0]['junit']['path']).write_text('broken');self.assertTrue(validate_report(path,self.root))
    def test_T23_evidence_changed_binary(self):
        path,_=self.collect();self.write('build/fixture.bin','other binary');self.assertTrue(validate_report(path,self.root))
    def test_T23_evidence_changed_source(self):
        path,_=self.collect();self.write('case.py','pass\n');self.assertTrue(validate_report(path,self.root))
    def test_T23_evidence_added_source(self):
        path,_=self.collect();self.write('new.py','pass\n');self.assertTrue(validate_report(path,self.root))
    def test_T23_evidence_changed_lock(self):
        path,_=self.collect();self.write('dependencies.lock','{}');self.assertTrue(validate_report(path,self.root))
    def test_T23_evidence_changed_raw(self):
        path,r=self.collect();raw=r['commands'][0]['raw'][0];(path.parent/raw['path']).write_bytes(b'changed');self.assertTrue(validate_report(path,self.root))
    def test_T23_evidence_failed_then_rerun(self):
        self.write('case.py','raise SystemExit(2)\n');first,bad=self.collect();self.write('case.py','pass\n');second,good=self.collect()
        self.assertNotEqual(first,second);self.assertEqual(bad['automated_status'],'Failed');self.assertEqual(good['automated_status'],'Passed',good['errors'])
        self.assertEqual(json.loads(first.read_text())['automated_status'],'Failed')
    def test_T23_evidence_source_mutation_during_test(self):
        self.write('case.py',"from pathlib import Path;Path(__file__).write_text('changed')\n")
        _,r=self.collect();self.assertNotEqual(r['automated_status'],'Passed')
    def test_T23_evidence_binary_mutation_during_test(self):
        self.write('case.py',"from pathlib import Path;Path('fixture.bin').write_text('changed')\n")
        _,r=self.collect();self.assertNotEqual(r['automated_status'],'Passed')
    def test_T23_evidence_missing_artifact(self):
        _,r=self.collect(required_artifacts=['missing.py']);self.assertNotEqual(r['automated_status'],'Passed')
    def test_T23_evidence_forged_status_rejected(self):
        self.write('case.py','raise SystemExit(4)\n');path,r=self.collect();r['automated_status']='Passed';r['errors']=[];path.write_text(json.dumps(r));self.assertTrue(validate_report(path,self.root))
    def test_T23_evidence_configuration_mismatch(self):
        _,r=self.collect(configure=['cmake','-S','.','-B','build','-G','Ninja','-DCMAKE_BUILD_TYPE=Release'])
        self.assertNotEqual(r['automated_status'],'Passed')
    def test_T23_evidence_review_required_not_waived(self):
        path,r=self.collect(review_required=['human','code'])
        self.assertEqual(r['package_status'],'InProgress')
        r['review']['required']=['human'];path.write_text(json.dumps(r))
        self.assertTrue(validate_report(path,self.root))
    def test_T23_evidence_gate_missing_profiles(self):
        from tools.evidence.gate import summarize
        self.write('gate.json',json.dumps({'gate_id':'G0','required_runs':[{'task_id':'D0.06','profile':'fixture-debug'},{'task_id':'D0.06','profile':'fixture-release'}]}))
        path,r=self.collect();self.assertEqual(r['automated_status'],'Passed',r['errors'])
        summary=summarize(self.root/'gate.json',[path],self.root)
        self.assertEqual(summary['automated_status'],'Incomplete');self.assertEqual(summary['gate_status'],'InProgress')
    def test_T23_evidence_gate_duplicate_runs(self):
        from tools.evidence.gate import summarize
        self.write('gate.json',json.dumps({'gate_id':'G0','required_runs':[{'task_id':'D0.06','profile':'fixture-debug'}]}))
        path,r=self.collect();self.assertEqual(r['automated_status'],'Passed',r['errors'])
        summary=summarize(self.root/'gate.json',[path,path],self.root)
        self.assertEqual(summary['automated_status'],'Failed');self.assertTrue(any('duplicate' in error for error in summary['errors']))
    def test_T23_evidence_legacy_corruption(self):
        from tools.evidence.import_bootstrap import inventory
        from tools.evidence.common import sha_file
        self.write('legacy/stdout.log','actual bytes')
        self.write('legacy/commands.json',json.dumps({'raw':[{'path':'stdout.log','sha256':sha_file(self.root/'legacy/stdout.log')}]}))
        report=inventory(self.root/'legacy');self.assertEqual(report['automated_status'],'Incomplete');self.assertEqual(report['verified_local_hash_references'],1)
        self.write('legacy/stdout.log','changed bytes');self.assertEqual(inventory(self.root/'legacy')['automated_status'],'Failed')
    def test_T23_evidence_runtime_artifact_integrity(self):
        from tools.evidence.common import runtime_path_allowed
        for name in ('packages/runtime/x','docs/x','evidence/commit/profile/report.json','evidence/bootstrap-other/x'):
            self.assertFalse(runtime_path_allowed(self.root,name))
        with self.assertRaises(ValueError):runtime_path_allowed(self.root,'../outside')
        for base in ('build','evidence/bootstrap/D1.06','evidence/G1'):
            self.write('case.py',f"from pathlib import Path;import uuid;p=Path(__file__).parent/{base!r}/('fault-'+uuid.uuid4().hex);p.mkdir(parents=True);(p/'artifact.bin').write_bytes(b'actual runtime bytes')\n")
            path,r=self.collect(runtime_artifact_patterns=[base+'/fault-*/*'],required_runtime_artifacts=[{'pattern':base+'/fault-*/artifact.bin','minimum':1}])
            self.assertEqual(r['automated_status'],'Passed',r['errors']);self.assertEqual(len(r['runtime_artifacts']),1)
            (path.parent/r['runtime_archive']['path']).write_bytes(b'corrupt');self.assertTrue(validate_report(path,self.root))
    def test_T23_evidence_runtime_duplicate_rejected(self):
        self.write('case.py',"from pathlib import Path;import uuid;p=Path(__file__).parent/'build'/('fault-'+uuid.uuid4().hex);p.mkdir();(p/'artifact.bin').write_bytes(b'one actual artifact')\n")
        path,r=self.collect(runtime_artifact_patterns=['build/fault-*/*'],required_runtime_artifacts=[{'pattern':'build/fault-*/artifact.bin','minimum':1}])
        self.assertEqual(r['automated_status'],'Passed',r['errors'])
        r['runtime_artifacts'].append(dict(r['runtime_artifacts'][0]));path.write_text(json.dumps(r))
        self.assertTrue(any('duplicate runtime' in error for error in validate_report(path,self.root)))
    def test_T23_evidence_stale_runtime_artifact_rejected(self):
        self.write('build/fault-old/artifact.bin','old bytes')
        _,r=self.collect(runtime_artifact_patterns=['build/fault-*/*'],required_runtime_artifacts=[{'pattern':'build/fault-*/artifact.bin','minimum':1}])
        self.assertNotEqual(r['automated_status'],'Passed');self.assertEqual(r['runtime_artifacts'],[])
    def test_T23_evidence_gate_mixed_source_rejected(self):
        from tools.evidence.gate import summarize
        self.write('gate.json',json.dumps({'gate_id':'G0','required_runs':[{'task_id':'D0.06','profile':'fixture-debug'},{'task_id':'D0.06','profile':'fixture-second'}]}))
        first,r=self.collect();self.assertEqual(r['automated_status'],'Passed',r['errors'])
        self.write('case.py',"print('changed source')\n")
        second,r=self.collect(profile='fixture-second');self.assertEqual(r['automated_status'],'Passed',r['errors'])
        summary=summarize(self.root/'gate.json',[first,second],self.root)
        self.assertEqual(summary['automated_status'],'Failed');self.assertTrue(any('different final source' in error for error in summary['errors']))
    def test_T23_evidence_source_directory_named_evidence(self):
        self.write('tools/evidence/helper.py','VALUE=1\n')
        path,r=self.collect(source_patterns=self.spec['source_patterns']+['tools/**'])
        self.assertEqual(r['automated_status'],'Passed',r['errors'])
        self.assertIn('tools/evidence/helper.py',[row['path'] for row in r['source']['inputs']])
        self.write('tools/evidence/helper.py','VALUE=2\n');self.assertTrue(validate_report(path,self.root))
    def test_T23_evidence_collector_process_fingerprint(self):
        from tools.evidence import run as collector
        from unittest.mock import patch
        stale=[dict(row) for row in collector.LOADED_COLLECTOR];stale[0]['sha256']='0'*64
        with patch.object(collector,'LOADED_COLLECTOR',stale):
            with self.assertRaisesRegex(ValueError,'fresh process'): self.collect()
    def test_T23_evidence_schema_unknown_field(self):
        path,r=self.collect();self.assertEqual(r['automated_status'],'Passed',r['errors'])
        r['unreviewed_override']='Passed';path.write_text(json.dumps(r))
        self.assertTrue(any('schema' in error for error in validate_report(path,self.root)))
    def test_T23_evidence_forged_human_approval(self):
        path,r=self.collect();r['package_status']='Passed';path.write_text(json.dumps(r));self.assertTrue(validate_report(path,self.root))

if __name__=='__main__':unittest.main()
