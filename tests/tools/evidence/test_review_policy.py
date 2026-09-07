"""自动复核政策合同：归档授权、精确来源与真实角色缺一不可。"""
import copy
import json
import sys
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.evidence.common import sha_bytes
from tools.evidence.review_policy import evaluate

class ReviewPolicyTests(unittest.TestCase):
    def setUp(self):
        self.path = 'docs/reviews/automatic-acceptance-policy.json'
        self.policy = {'format': 'ock.review-policy/1', 'version': 1, 'mode': 'ai-self-review',
            'user_text': '自我复核和自动验收，无需人工', 'required_reviews': ['spec', 'code'],
            'review_actor': 'AI', 'human_review_required': False}
        self.raw = json.dumps(self.policy, ensure_ascii=False).encode('utf-8')
        self.spec = {'review_policy': {'format': 'ock.review-policy-binding/1',
            'mode': 'ai-self-review', 'path': self.path, 'sha256': sha_bytes(self.raw)},
            'review_required': ['spec', 'code']}
        self.rows = [{'path': self.path, 'sha256': sha_bytes(self.raw), 'size': len(self.raw)}]
        self.records = [{'task_id': 'D0.06', 'reviewed_inputs_sha256': 'a'*64,
            'review_kind': kind, 'actor_type': 'AI', 'reviewer': '自动复核代理',
            'review_status': 'Approved', 'approval_text': '已实际核对合同及代码。'}
            for kind in ('spec', 'code')]
    def check(self):
        return evaluate(self.spec, self.records, 'D0.06', 'a'*64,
                        lambda name: self.raw if name == self.path else None, self.rows)
    def test_T23_review_policy_approved_pair(self):
        self.assertEqual(self.check(), ([], True))
    def test_T23_review_policy_missing_ai_is_pending(self):
        self.records.pop()
        self.assertEqual(self.check(), ([], False))
    def test_T23_review_policy_legacy_requires_human(self):
        self.spec = {'review_required': ['spec', 'code']}
        self.assertTrue(self.check()[0])
    def test_T23_review_policy_legacy_human_remains_valid(self):
        self.spec = {'review_required': ['human']}
        self.records = [{'task_id':'D0.06','reviewed_inputs_sha256':'a'*64,
            'review_status':'Approved','approval_text':'批准'}]
        self.assertEqual(self.check(), ([], True))
    def test_T23_review_policy_empty_or_partial_required_rejected(self):
        for required in ([], ['spec'], ['code'], ['spec','spec'], ['human','spec','code']):
            with self.subTest(required=required):
                self.spec['review_required'] = required
                self.assertTrue(self.check()[0])
    def test_T23_review_policy_missing_source_rejected(self):
        self.rows = []
        self.assertTrue(self.check()[0])
    def test_T23_review_policy_wrong_hash_rejected(self):
        self.spec['review_policy']['sha256'] = 'b'*64
        self.assertTrue(self.check()[0])
    def test_T23_review_policy_live_content_cannot_replace_archive(self):
        self.raw = b'{}'
        self.assertTrue(self.check()[0])
    def test_T23_review_policy_false_authorization_rejected(self):
        self.policy['user_text'] = '自动测试'
        self.raw = json.dumps(self.policy).encode()
        self.spec['review_policy']['sha256'] = sha_bytes(self.raw)
        self.rows = [{'path':self.path,'sha256':sha_bytes(self.raw),'size':len(self.raw)}]
        self.assertTrue(self.check()[0])
    def test_T23_review_policy_forged_actor_rejected(self):
        for actor in ('human', '', None):
            with self.subTest(actor=actor):
                self.records[0]['actor_type'] = actor
                self.assertTrue(self.check()[0])
    def test_T23_review_policy_wrong_task_or_source_rejected(self):
        for field, value in (('task_id','D0.05'),('reviewed_inputs_sha256','b'*64)):
            records = copy.deepcopy(self.records)
            self.records[0][field] = value
            self.assertTrue(self.check()[0])
            self.records = records
    def test_T23_review_policy_empty_reviewer_or_approval_rejected(self):
        for field in ('reviewer','approval_text'):
            records = copy.deepcopy(self.records)
            self.records[0][field] = '  '
            self.assertTrue(self.check()[0])
            self.records = records
    def test_T23_review_policy_human_claim_rejected(self):
        self.records[0]['human_review_status'] = 'Approved'
        self.assertTrue(self.check()[0])
    def test_T23_review_policy_unknown_mode_rejected(self):
        self.spec['review_policy']['mode'] = 'automatic'
        self.assertTrue(self.check()[0])
    def test_T23_review_policy_duplicate_review_rejected(self):
        self.records.append(copy.deepcopy(self.records[0]))
        self.assertTrue(self.check()[0])
    def test_T23_review_policy_missing_archive_rejected(self):
        def missing(name):
            raise KeyError(name)
        self.assertTrue(evaluate(self.spec,self.records,'D0.06','a'*64,missing,self.rows)[0])

    def test_T23_review_policy_noncanonical_path_rejected(self):
        for name in ('../policy.json','/policy.json','docs//policy.json','C:/policy.json'):
            with self.subTest(name=name):
                self.spec['review_policy']['path'] = name
                self.assertTrue(self.check()[0])
    def test_T23_review_policy_pending_review_is_not_approved(self):
        self.records[0]['review_status'] = 'Pending'
        self.records[0].pop('approval_text')
        self.assertEqual(self.check(),([],False))
    def test_T23_review_policy_archived_requirements_cannot_weaken(self):
        for field,value in (('required_reviews',[]),('required_reviews',['spec']),
                            ('review_actor','human'),('human_review_required',True)):
            policy = copy.deepcopy(self.policy)
            policy[field] = value
            self.raw = json.dumps(policy).encode()
            self.spec['review_policy']['sha256'] = sha_bytes(self.raw)
            self.rows = [{'path':self.path,'sha256':sha_bytes(self.raw),'size':len(self.raw)}]
            self.assertTrue(self.check()[0])
    def test_T23_review_policy_archived_duplicate_member_rejected(self):
        self.raw = self.raw[:-1] + b',"mode":"ai-self-review"}'
        self.spec['review_policy']['sha256'] = sha_bytes(self.raw)
        self.rows = [{'path':self.path,'sha256':sha_bytes(self.raw),'size':len(self.raw)}]
        self.assertTrue(self.check()[0])

    def test_T23_review_policy_version_required_and_supported(self):
        for version in (None, 2, True, '1'):
            with self.subTest(version=version):
                policy = copy.deepcopy(self.policy)
                if version is None:
                    policy.pop('version')
                else:
                    policy['version'] = version
                self.raw = json.dumps(policy).encode()
                self.spec['review_policy']['sha256'] = sha_bytes(self.raw)
                self.rows = [{'path':self.path,'sha256':sha_bytes(self.raw),'size':len(self.raw)}]
                self.assertTrue(self.check()[0])

if __name__ == '__main__':
    unittest.main()
