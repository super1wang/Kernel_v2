import copy
import unittest
from tests.conformance.logging.qualification import CASES,qualify
from tests.conformance.logging.verify_contract import good

class QualificationGuards(unittest.TestCase):
    def setUp(self):
        self.binding={'sha256':'a'*64,'sources':[{'path':'packages/runtime/observability/logging.cpp','sha256':'b'*64},
          {'path':'tests/conformance/logging/reference_backend.hpp','sha256':'c'*64}]}
        self.rows=[{'case':name,'run_id':'new','binary_sha256':'d'*64,'binding_sha256':'a'*64,'status':'Passed'} for name in CASES]
    def result(self,rows=None,claimed=None):return qualify(self.binding,self.rows if rows is None else rows,'new','d'*64,claimed)
    def test_complete_current_two_factory_positive(self):self.assertTrue(self.result()['qualified'])
    def test_missing_duplicate_and_failed_are_rejected(self):
        for rows in [self.rows[:-1],self.rows+[self.rows[0]],[dict(x,status='Failed') if i==0 else x for i,x in enumerate(self.rows)]]:
            with self.assertRaises(ValueError):self.result(rows)
    def test_old_run_and_wrong_binary_or_source_are_rejected(self):
        for key in ('run_id','binary_sha256','binding_sha256'):
            rows=copy.deepcopy(self.rows);rows[0][key]='old'
            with self.assertRaises(ValueError):self.result(rows)
    def test_version_factory_digest_capability_skip_and_fault_cannot_qualify(self):
        good={x['descriptor']['name']:x['descriptor'] for x in self.result()['results']}
        for key,value in [('port_contract_version','old'),('factory','fake'),('implementation_sha256','0'*64),('kind','fault'),('skip_cases',['format_redaction'])]:
            bad=copy.deepcopy(good);bad['memory'][key]=value;self.assertFalse(self.result(claimed=bad)['qualified'])
        bad=copy.deepcopy(good);bad['test']['capabilities']['file']=True;self.assertFalse(self.result(claimed=bad)['qualified'])
    def test_optional_absence_is_never_passed(self):
        self.assertTrue(all(r['status']=='NotApplicable' for b in self.result()['results'] for r in b['optional']))
    def test_failed_terminated_or_unowned_command_cannot_qualify(self):
        command={'status':'Exited','exit_code':0,'observed_exit_code':0,
          'process_tree':{'assigned_before_resume':True,'active_after':0,'terminated_owned_job':False}}
        self.assertTrue(good(command))
        for key,value in [('status','Timeout'),('exit_code',1),('observed_exit_code',1)]:
            bad=copy.deepcopy(command);bad[key]=value;self.assertFalse(good(bad))
        for key,value in [('assigned_before_resume',False),('active_after',1),('terminated_owned_job',True)]:
            bad=copy.deepcopy(command);bad['process_tree'][key]=value;self.assertFalse(good(bad))
