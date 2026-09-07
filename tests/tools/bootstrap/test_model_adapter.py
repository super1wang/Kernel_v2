"""真实 unittest 结果必须是一个执行成功的必需用例。"""
import contextlib
import importlib.util
import io
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('model_cases',ROOT/'tools/testing/model_cases.py')
adapter=importlib.util.module_from_spec(spec);spec.loader.exec_module(adapter)

class ModelAdapterTests(unittest.TestCase):
    def run_fixture(self,method,expected_failure=False):
        if expected_failure:method=unittest.expectedFailure(method)
        fixture=type('Fixture',(unittest.TestCase,),{'test_case':method})
        suite=unittest.TestSuite([fixture('test_case')])
        with patch.object(adapter,'cases',return_value=[('T99.fixture.case','Fixture.test_case')]), patch.object(adapter.sys,'argv',['adapter','--case','Fixture.test_case']), patch.object(adapter.unittest.defaultTestLoader,'loadTestsFromName',return_value=suite), contextlib.redirect_stderr(io.StringIO()):
            return adapter.main()
    def test_success_runs_and_exits_zero(self):
        self.assertEqual(self.run_fixture(lambda case:case.assertTrue(True)),0)
    def test_failure_exits_nonzero(self):
        self.assertNotEqual(self.run_fixture(lambda case:case.fail('actual failure')),0)
    def test_skip_is_not_success(self):
        self.assertNotEqual(self.run_fixture(lambda case:case.skipTest('required case skipped')),0)
    def test_expected_failure_is_not_success(self):
        self.assertNotEqual(self.run_fixture(lambda case:case.fail('expected failure'),True),0)
    def test_unexpected_success_is_not_success(self):
        self.assertNotEqual(self.run_fixture(lambda case:case.assertTrue(True),True),0)
    def test_missing_case_rejected_before_execution(self):
        with patch.object(adapter,'cases',return_value=[('T99.fixture.case','Fixture.test_case')]), patch.object(adapter.sys,'argv',['adapter','--case','Fixture.missing']):
            with self.assertRaises(ValueError):adapter.main()

if __name__=='__main__':unittest.main()
