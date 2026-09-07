"""真实 CMake/CTest 夹具：自动验收不得跳过归档授权与复核来源验证。"""
import copy
import json
import sys
import unittest
import zipfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_runner as fixture
from tools.evidence.common import digest, inputs, sha_file
from tools.evidence.validate import validate_report

class ReviewPolicyRunnerTests(unittest.TestCase):
    setUp = fixture.EvidenceTests.setUp
    write = fixture.EvidenceTests.write
    write_cmake = fixture.EvidenceTests.write_cmake
    collect = fixture.EvidenceTests.collect
    def prepare(self, kinds=('spec','code')):
        policy_path = 'docs/reviews/automatic-acceptance-policy.json'
        policy = {'format':'ock.review-policy/1','version':1,'mode':'ai-self-review',
            'user_text':'自我复核和自动验收，无需人工','required_reviews':['spec','code'],
            'review_actor':'AI','human_review_required':False}
        self.write(policy_path,json.dumps(policy,ensure_ascii=False))
        self.spec.update(review_policy={'format':'ock.review-policy-binding/1','mode':'ai-self-review',
            'path':policy_path,'sha256':sha_file(self.root/policy_path)},
            review_required=['spec','code'], review_records=['reviews/'+k+'.json' for k in kinds])
        self.write('run.json',json.dumps(self.spec))
        source_sha = digest(inputs(self.root,self.spec,'run.json'))
        for kind in kinds:
            self.write('reviews/'+kind+'.json',json.dumps({'task_id':'D0.06',
                'reviewed_inputs_sha256':source_sha,'review_kind':kind,'actor_type':'AI',
                'reviewer':'合同测试夹具','approval_text':'仅供验证记录绑定的测试数据。','review_status':'Approved'}))
    def test_T23_ai_runner_archive_and_approve(self):
        self.prepare()
        path,r = self.collect()
        self.assertEqual(r['package_status'],'Passed',r['errors'])
        self.assertEqual(validate_report(path,self.root),[])
        self.assertEqual(validate_report(path),[])
        self.assertIn(self.spec['review_policy']['path'],[row['path'] for row in r['source']['inputs']])
        self.assertIn('archive',r['review'])
        self.assertNotIn('human',r['review']['required'])
    def test_T23_ai_runner_missing_review_stays_pending(self):
        self.prepare(('spec',))
        path,r = self.collect()
        self.assertEqual(r['automated_status'],'Passed',r['errors'])
        self.assertEqual(r['package_status'],'InProgress')
        self.assertEqual(validate_report(path),[])
    def test_T23_ai_runner_forged_report_review_rejected_historically(self):
        self.prepare()
        path,r = self.collect()
        self.assertEqual(r['package_status'],'Passed',r['errors'])
        r['review']['records'][0]['record']['reviewer'] = '冒用归档审查者'
        path.write_text(json.dumps(r),encoding='utf-8')
        self.assertTrue(any('snapshot mismatch' in error for error in validate_report(path)))
    def test_T23_ai_runner_live_policy_change_does_not_rewrite_history(self):
        self.prepare()
        path,r = self.collect()
        self.assertEqual(r['package_status'],'Passed',r['errors'])
        self.write(self.spec['review_policy']['path'],'{}')
        self.assertTrue(validate_report(path,self.root))
        self.assertEqual(validate_report(path),[])
    def test_T23_ai_runner_source_change_invalidates_review(self):
        self.prepare()
        self.write('case.py',"print('changed implementation')\n")
        _,r = self.collect()
        self.assertEqual(r['package_status'],'Failed')
        self.assertTrue(any('review task or source mismatch' in error for error in r['errors']))
    def test_T23_ai_runner_forged_actor_rejected(self):
        self.prepare()
        record = json.loads((self.root/'reviews/spec.json').read_text())
        record['actor_type'] = 'human'
        self.write('reviews/spec.json',json.dumps(record))
        _,r = self.collect()
        self.assertEqual(r['package_status'],'Failed')
        self.assertTrue(any('actor must be AI' in error for error in r['errors']))
    def test_T23_ai_runner_missing_policy_rejected(self):
        self.prepare()
        (self.root/self.spec['review_policy']['path']).unlink()
        _,r = self.collect()
        self.assertEqual(r['package_status'],'Failed')
    def test_T23_ai_runner_claim_without_review_rejected(self):
        self.prepare(())
        path,r = self.collect()
        self.assertEqual(r['package_status'],'InProgress',r['errors'])
        r['package_status'] = 'Passed'
        path.write_text(json.dumps(r),encoding='utf-8')
        self.assertTrue(validate_report(path))
    def test_T23_ai_runner_review_archive_corruption_rejected(self):
        self.prepare()
        path,r = self.collect()
        self.assertEqual(r['package_status'],'Passed',r['errors'])
        (path.parent/r['review']['archive']['path']).write_bytes('损坏附件'.encode('utf-8'))
        self.assertTrue(validate_report(path))
    def test_T23_ai_runner_loaded_collector_includes_policy(self):
        from tools.evidence.run import LOADED_COLLECTOR
        self.assertIn(str(ROOT/'tools/evidence/review_policy.py'),[row['path'] for row in LOADED_COLLECTOR])

if __name__ == '__main__':
    unittest.main()
