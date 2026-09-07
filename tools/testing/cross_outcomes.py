"""以 D0.03 正式 Schema+语义校验 D0.05 测试实际产生的结果。"""
import importlib.util
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[2]
for path in ('build/python-deps','tests/model'):sys.path.insert(0,str(ROOT/path))
spec=importlib.util.spec_from_file_location('outcome_contract',ROOT/'tests/model/execution_model/model.py')
contract=importlib.util.module_from_spec(spec);spec.loader.exec_module(contract)
from commit_model.model import CommitDomain,Effect
counts={'CommitDomain':0,'Effect':0}
def checked(cls):
    original=cls.outcome
    def outcome(self,*args,**kwargs):
        result=original(self,*args,**kwargs)
        if result is not None:contract.validate_document(result);counts[cls.__name__]+=1
        return result
    cls.outcome=outcome
for cls in (CommitDomain,Effect):checked(cls)
suite=unittest.defaultTestLoader.loadTestsFromNames(['commit_model.test_commit_model','dedup_model.test_dedup_model'])
result=unittest.TextTestRunner(verbosity=1).run(suite)
print(json.dumps({'check_id':'CHECK.models.outcome_consistency','actual_outcomes':counts,'tests_run':result.testsRun},ensure_ascii=False))
raise SystemExit(not(result.wasSuccessful() and result.testsRun>0 and not result.skipped and not result.expectedFailures and all(counts.values())))
