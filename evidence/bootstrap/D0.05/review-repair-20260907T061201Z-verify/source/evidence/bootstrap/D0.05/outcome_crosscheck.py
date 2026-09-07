"""复用已批准 D0.03 validator，对 D0.05 全部实际 Outcome 投影交叉验证。"""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'build/python-deps'))
sys.path.insert(0, str(ROOT / 'tests/model'))
from execution_model.model import validate_outcome
from commit_model.model import CommitDomain, Effect
from commit_model.run import discovered

counts = {'CommitDomain': 0, 'Effect': 0}
for cls in (CommitDomain, Effect):
    original = cls.outcome
    def checked(self, *args, _original=original, _name=cls.__name__, **kwargs):
        value = _original(self, *args, **kwargs)
        if value is not None:
            validate_outcome(value)
            counts[_name] += 1
        return value
    cls.outcome = checked
cases = discovered()
suite = unittest.defaultTestLoader.loadTestsFromNames(cases)
result = unittest.TextTestRunner(verbosity=2).run(suite)
print('D0.03 actual Outcome validation counts:', counts)
sys.exit(int(not (result.testsRun == len(cases) and result.wasSuccessful() and not result.skipped and not result.expectedFailures
                 and all(value > 0 for value in counts.values()))))
