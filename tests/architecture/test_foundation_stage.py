"""D1.01：只有Foundation前进，第三方公开依赖须显式登记。"""
from copy import deepcopy
import unittest
from tools.architecture.check import validate_implementation_stage

class FoundationStageTests(unittest.TestCase):
    def manifest(self):
        return {'stage':'Foundation','targets':{
            'Foundation':{'implementation':'Implemented','external_dependencies':['expected']},
            'Runtime':{'implementation':'ContractBaseline','external_dependencies':[]}}}
    def test_foundation_only_is_allowed(self):
        self.assertEqual(validate_implementation_stage(self.manifest()),[])
    def test_runtime_cannot_claim_implementation(self):
        m=self.manifest();m['targets']['Runtime']['implementation']='Implemented'
        self.assertTrue(validate_implementation_stage(m))
    def test_unknown_stage_rejected(self):
        m=self.manifest();m['stage']='AllImplemented'
        self.assertTrue(validate_implementation_stage(m))
    def test_external_dependency_cannot_disappear_or_expand(self):
        for values in ([],['expected','sqlite'],['some_private_target']):
            m=self.manifest();m['targets']['Foundation']['external_dependencies']=values
            self.assertTrue(validate_implementation_stage(m))
    def test_other_component_cannot_add_public_dependency(self):
        m=self.manifest();m['targets']['Runtime']['external_dependencies']=['expected']
        self.assertTrue(validate_implementation_stage(m))
    def test_historical_baseline_still_supported(self):
        m=self.manifest();m['stage']='ContractBaseline'
        for t in m['targets'].values():t['implementation']='ContractBaseline';t['external_dependencies']=[]
        self.assertEqual(validate_implementation_stage(m),[])
if __name__=='__main__':unittest.main()
