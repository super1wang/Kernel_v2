"""D1.06 有限观测配置、阶段协议和原始分析的反例；不替代真实 OS 校准。"""
import dataclasses
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.footprint.windows_process import ObservationConfig, ProtocolState, PHASES, RECORD, ObservationError
from tools.footprint.analyze import nearest_rank, paired_differences, require_complete, validate_budget


def configuration(**changes):
    values=dict(mode='occupancy',setup_timeout_ms=3000,stage_timeout_ms=4000,max_samples=4000,
                max_modules=256,max_threads=1024,module_path_chars=32768,module_attempts=3,
                calibration_bytes=16777216,calibration_hold_ms=500,ready_delay_ms=100)
    values.update(changes)
    return ObservationConfig(**values)


def message(index,**changes):
    values=dict(version=1,size=RECORD.size,nonce=b'a'*16,pid=123,phase=index,sequence=index+1,
                reserved=0,ticks=110+index,checks=1,owners=15,sentinels=0,generation=7)
    values.update(changes)
    return RECORD.pack(*values.values())


class ObservationGuards(unittest.TestCase):
    def test_T23_footprint_finite_configuration(self):
        config=configuration()
        self.assertEqual(config.sample_interval_ms,5)
        with self.assertRaises(dataclasses.FrozenInstanceError):config.max_samples=1
        for name in ('setup_timeout_ms','stage_timeout_ms','max_samples','max_modules','max_threads','module_path_chars','module_attempts','calibration_bytes','calibration_hold_ms','ready_delay_ms'):
            for value in (0,-1,True,float('nan'),float('inf'),10**30):
                with self.subTest(name=name,value=value),self.assertRaises(ValueError):configuration(**{name:value})
        for data in ({'callback':lambda:None},{'pid':123},{'handle':123},{'command':['echo']}):
            with self.assertRaises(TypeError):configuration(**data)
        with self.assertRaises(ValueError):configuration(mode='unknown')

    def test_T23_footprint_strict_stage_identity(self):
        for key,value in [('version',2),('size',1),('nonce',b'b'*16),('pid',321),('phase',1),('sequence',2),('reserved',1),('ticks',201),('checks',0),('generation',8)]:
            with self.subTest(key=key),self.assertRaises(ObservationError):
                ProtocolState(b'a'*16,7,123,100).accept(message(0,**{key:value}),200)
        with self.assertRaises(ObservationError):ProtocolState(b'a'*16,7,123,100).accept(b'',200)
        state=ProtocolState(b'a'*16,7,123,100)
        state.accept(message(0),200)
        with self.assertRaises(ObservationError):state.accept(message(0),200)
        for index in range(1,len(PHASES)):state.accept(message(index),200)
        self.assertTrue(state.complete)
        with self.assertRaises(ObservationError):state.accept(message(10),200)

    def test_T23_footprint_nearest_rank_signed_pairs(self):
        self.assertEqual(nearest_rank([1,2,3,4],.95),4)
        self.assertEqual(nearest_rank([1,2,3,4],.5),2)
        for values in ([],[float('nan')],[float('inf')]):
            with self.assertRaises(ValueError):nearest_rank(values,.95)
        values=[dict(run_id=str(i),kind=k,block=0,value=v,identity='same') for i,(k,v) in enumerate(zip('ABBA',[10,8,7,9]))]
        self.assertEqual(paired_differences(values,1),[-2,-2])
        for wrong in (values[:3],values+[values[0]],[{**v,'identity':str(i)} for i,v in enumerate(values)]):
            with self.assertRaises(ValueError):paired_differences(wrong,1)

    def test_T23_footprint_missing_observation_never_zero_or_passed(self):
        config=configuration()
        samples=[{'ok':True,'private_bytes':1,'working_set_bytes':2,'reason':name,'thread_ids':[1],
                  'memory_query':{'ok':True,'win32_error':0,'structure_bytes':80,'started_ticks':1,'completed_ticks':2},
                  'thread_query':{'ok':True,'win32_error':0,'structure_bytes':28,'started_ticks':3,'completed_ticks':4}}
                 for name in ('assigned_suspended',*PHASES)]
        good={'status':'Exited','exit_code':0,'process_tree':{'assigned_before_resume':True,'active_after':0,'terminated_owned_job':False},
              'observation':{'status':'Complete','samples':samples,'method':'ock.native-footprint/2',
                             'configuration':dataclasses.asdict(config),'method_digest':config.identity(),
                             'memory_sampling':'due_5ms','thread_sampling':'phase_boundaries',
                             'phases':[{'phase':name} for name in PHASES]}}
        require_complete(good)
        for key,value in [('assigned_before_resume',False),('active_after',1),('terminated_owned_job',True)]:
            with self.subTest(key=key),self.assertRaises(ValueError):
                require_complete({**good,'process_tree':{**good['process_tree'],key:value}})
        for field,value in [('status','LaunchFailed'),('exit_code',7),('observation',None)]:
            with self.assertRaises(ValueError):require_complete({**good,field:value})
        for change in ({'status':'Incomplete'},{'samples':[]},{'samples':[{'ok':False,'private_bytes':None}]},{'phases':[]}):
            with self.assertRaises(ValueError):require_complete({**good,'observation':{**good['observation'],**change}})

    def test_T23_footprint_budget_not_automatically_approved(self):
        for budget in (None,{}, {'status':'NotApproved'}, {'status':'Approved','method_digest':'wrong'}):
            with self.assertRaises(ValueError):validate_budget(budget,method_digest='method',report_created='2026-09-09T00:00:00Z',run_ids=['new'])
        valid={'status':'Approved','method_digest':'method','approved_at':'2026-09-08T00:00:00Z','pilot_run_ids':['old'],'limits':{'private_bytes':100}}
        validate_budget(valid,method_digest='method',report_created='2026-09-09T00:00:00Z',run_ids=['new'])
        for change in ({'approved_at':'2026-09-10T00:00:00Z'},{'pilot_run_ids':['new']},{'limits':{'private_bytes':float('inf')}},{'limits':{'private_bytes':0}},{'automatic_margin':1.2}):
            with self.assertRaises(ValueError):validate_budget({**valid,**change},method_digest='method',report_created='2026-09-09T00:00:00Z',run_ids=['new'])

    def test_T23_footprint_required_module_omission(self):
        from tools.footprint.analyze import require_modules
        required=[{'path':'required.dll','sha256':'a'*64,'bytes':4096}]
        require_modules(required,required)
        for actual in ([],[{**required[0],'sha256':'b'*64}],required+required):
            with self.assertRaises(ValueError):require_modules(required,actual)

    def test_T23_footprint_execute_rejects_arbitrary_observer_before_launch(self):
        from tools.evidence.process import execute
        with tempfile.TemporaryDirectory() as directory:
            folder=Path(directory)
            for wrong in (lambda:None,{'pid':123},{'handle':123},'observer.py'):
                with self.assertRaises(ValueError):
                    execute([sys.executable,'-c','pass'],folder,folder/'out',folder/'err',1,observation=wrong)
                self.assertFalse((folder/'out').exists())

    def test_T23_footprint_closed_scope_never_attaches(self):
        from tools.footprint.windows_process import _Scope
        scope=_Scope(configuration());scope.close();scope.close()
        with self.assertRaises(ObservationError):scope._memory()
        with self.assertRaises(ObservationError):scope._threads()
        with self.assertRaises(ObservationError):scope._launch_material()
        with self.assertRaises(ObservationError):scope.bind_assigned(123,None)


if __name__=='__main__':unittest.main()
