"""独立审核发现的永久回归；伪时钟/伪命令不冒充真实 OS 校准。"""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.footprint import windows_process as m
from tools.footprint import run as driver
from tools.footprint.analyze import owned_success


class ReviewRegressions(unittest.TestCase):
    def test_root_exit_checks_pending_event_without_process_query(self):
        for signaled,expected in [(0,'ObserverFailed'),(258,'Complete')]:
            with self.subTest(signaled=signaled):
                s=m._Scope.__new__(m._Scope)
                s._closed=False;s._bound=True;s._pending=None;s._event=1
                s.protocol=m.ProtocolState(b'a'*16,1,123,0);s.protocol.next_index=len(m.PHASES)
                s.data={'events':[],'errors':[]};s.ticks=lambda:100
                class Kernel:
                    def WaitForSingleObject(self,handle,timeout):
                        if (handle,timeout)!=(1,0):raise AssertionError('unexpected handle/wait')
                        return signaled
                s._k=Kernel()
                s.root_exited()
                self.assertEqual(s.data['status'],expected)
                self.assertFalse(s._bound)
                with self.assertRaises(m.ObservationError):s._memory()
                with self.assertRaises(m.ObservationError):s._threads()

    def test_query_cannot_ack_or_reset_expired_deadline(self):
        for deadline,stage,error in [(1.,3.,'RunDeadlineReached'),(3.,1.,'StageDeadlineReached')]:
            with self.subTest(error=error):
                s=m._Scope.__new__(m._Scope)
                s._closed=False;s._bound=True;s._deadline=deadline;s._stage_deadline=stage
                s._pending=None;s._event=1;s._ack=2;s._view=3;s._next_sample=10.
                s.config=m.ObservationConfig('occupancy',3000,4000,40,20,20,260,1,4096,500,1)
                s.protocol=m.ProtocolState(b'a'*16,1,123,0);s.data={'phases':[]}
                clock=[0.];acks=[]
                class Kernel:
                    def WaitForSingleObject(self,*args):return 0
                    def SetEvent(self,*args):acks.append(clock[0]);return 1
                s._k=Kernel();s.ticks=lambda:100
                s._sample=lambda reason:clock.__setitem__(0,2.)
                raw=m.RECORD.pack(1,80,b'a'*16,123,0,1,0,1,1,15,0,1)
                with patch.object(m.time,'monotonic',side_effect=lambda:clock[0]),patch.object(m.ctypes,'string_at',return_value=raw):
                    with self.assertRaisesRegex(m.ObservationError,error):s.poll()
                self.assertEqual(acks,[])
                self.assertEqual(s._stage_deadline,stage)

    def test_invalid_owned_predecessor_never_schedules_build(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for relative in ['tools/evidence/process.py','tests/tools/evidence/test_process.py','cmake/LockedMSVC.cmake','cmake/msvc-validation-tools.json','dependencies.lock','docs/contracts/native-footprint-method.md','tools/footprint/controls/required.cpp','tools/footprint/controls/calibration.cpp']:
                p=root/relative;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('synthetic; never compiled',encoding='utf-8')
            (root/'tests/tools/footprint').mkdir(parents=True)
            config=root/'configuration.json'
            config.write_text(json.dumps(dict(mode='occupancy',setup_timeout_ms=3000,stage_timeout_ms=4000,max_samples=40,max_modules=20,max_threads=20,module_path_chars=260,module_attempts=1,calibration_bytes=4096,calibration_hold_ms=500,ready_delay_ms=1)))
            calls=[]
            def fake_execute(argv,cwd,out,err,timeout,**kwargs):
                calls.append(argv)
                if len(calls)>1:raise AssertionError('invalid predecessor scheduled build')
                Path(out).write_bytes(b'');Path(err).write_bytes(b'')
                return dict(status='DescendantsAlive',exit_code=0,process_tree=dict(assigned_before_resume=True,active_after=0,terminated_owned_job=True))
            with patch.object(driver,'ROOT',root),patch.object(driver,'execute',side_effect=fake_execute),patch.object(sys,'argv',['run','--configuration',str(config),'--mode','calibrate']):
                self.assertEqual(driver.main(),1)
            self.assertEqual(len(calls),1)
            self.assertNotIn('--build',calls[0])

    def test_owned_success_requires_all_facts(self):
        good=dict(status='Exited',exit_code=0,process_tree=dict(assigned_before_resume=True,active_after=0,terminated_owned_job=False))
        self.assertTrue(owned_success(good))
        for change in ({'status':'DescendantsAlive'},{'status':'Timeout'},{'exit_code':1},{'process_tree':None}):
            self.assertFalse(owned_success({**good,**change}))
        for key,value in [('assigned_before_resume',False),('active_after',1),('active_after',None),('terminated_owned_job',True)]:
            self.assertFalse(owned_success({**good,'process_tree':{**good['process_tree'],key:value}}))


if __name__=='__main__':unittest.main(verbosity=2)
