"""同一合同校验合法 mock、非法后端和能力适用性，非生产后端成绩。"""
import copy
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/'tests/conformance'))
from support.harness import run_contract, descriptor, descriptor_for, COMMON_CASES
from support.fixtures import MockExecutor, DuplicateCallbackExecutor

class ConformanceBootstrapTests(unittest.TestCase):
    def test_T19_conformance_common_contract(self):
        r=run_contract(MockExecutor,descriptor('mock-inline'))
        self.assertTrue(r['qualified'],r)
        self.assertEqual({c['id'] for c in r['cases'] if c['status']=='Passed'},set(COMMON_CASES))
    def test_T19_conformance_fault_not_qualified(self):
        r=run_contract(DuplicateCallbackExecutor,descriptor('fault-duplicate'))
        self.assertFalse(r['qualified'])
        self.assertTrue(any(c['status']=='Failed' for c in r['cases']))
    def test_T19_conformance_optional_not_passed(self):
        r=run_contract(MockExecutor,descriptor('mock-inline'))
        optional=[c for c in r['cases'] if c['status']=='NotApplicable']
        self.assertEqual(len(optional),1);self.assertTrue(optional[0]['reason'])
    def test_T19_conformance_profile_requires_capability(self):
        r=run_contract(MockExecutor,descriptor('mock-inline'),required_capabilities=['parallel'])
        self.assertFalse(r['qualified']);self.assertTrue(r['errors'])
    def test_T19_conformance_common_cannot_be_waived(self):
        d=descriptor('mock-inline');d['skip_cases']=[COMMON_CASES[0]]
        self.assertFalse(run_contract(MockExecutor,d)['qualified'])
    def test_T19_conformance_missing_required_capability(self):
        d=descriptor('mock-inline');del d['capabilities']['inline']
        self.assertFalse(run_contract(MockExecutor,d)['qualified'])
    def test_T19_conformance_false_capability(self):
        d=descriptor('mock-inline');d['capabilities']['parallel']=True
        self.assertFalse(run_contract(MockExecutor,d)['qualified'])
    def test_T19_conformance_wrong_contract_version(self):
        d=descriptor('mock-inline');d['port_contract_version']='other/1'
        self.assertFalse(run_contract(MockExecutor,d)['qualified'])
    def test_T19_conformance_wrong_implementation_digest(self):
        d=descriptor('mock-inline');d['implementation_sha256']='0'*64
        self.assertFalse(run_contract(MockExecutor,d)['qualified'])
    def test_T19_conformance_factory_identity(self):
        self.assertFalse(run_contract(DuplicateCallbackExecutor,descriptor('mock-inline'))['qualified'])
    def test_T19_conformance_backend_exception(self):
        class Broken(MockExecutor):
            def submit(self,work,done):raise RuntimeError('broken boundary')
        r=run_contract(Broken,descriptor_for(Broken,'broken'));self.assertFalse(r['qualified']);self.assertTrue(any(c['status']=='Failed' for c in r['cases']))
    def test_T19_conformance_nested_worker_rejection(self):
        from support.fixtures import ContractError
        backend=MockExecutor();observed=[]
        def outer():
            backend.submit(lambda: 1, lambda result: None)
            for action in (backend.drain,backend.shutdown):
                try: action()
                except ContractError: observed.append('rejected')
                else: observed.append('returned')
        backend.submit(outer,lambda result: None)
        self.assertEqual(observed,['rejected','rejected'])
        self.assertFalse(backend.closed)
        self.assertTrue(backend.drain())
    def test_T19_conformance_fault_flag_not_qualification(self):
        d=descriptor('mock-inline');d['kind']='fault'
        self.assertFalse(run_contract(MockExecutor,d)['qualified'])

if __name__=='__main__':unittest.main()
