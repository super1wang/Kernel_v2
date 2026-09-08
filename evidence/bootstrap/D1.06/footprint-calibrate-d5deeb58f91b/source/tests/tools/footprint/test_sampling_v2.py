"""v2 有限采样回归；伪查询仅验证路由和元数据，不代替 held-thread 实测。"""
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.footprint.windows_process import ObservationConfig,_Scope,PHASES,ObservationError


class SamplingV2Tests(unittest.TestCase):
    def scope(self):
        s=_Scope.__new__(_Scope);s._closed=False;s._bound=True
        s.config=ObservationConfig('occupancy',3000,4000,100,20,20,260,1,4096,500,1)
        s.data={'samples':[]};self.clock=0;self.thread_calls=0
        def ticks():self.clock+=1;return self.clock
        def threads():self.thread_calls+=1;return {'ids':[1,3],'structure_bytes':28,'win32_error':0}
        s.ticks=ticks;s._memory=lambda:{'ok':True,'win32_error':0,'structure_bytes':80,'private_bytes':10,'working_set_bytes':20,'peak_working_set_bytes':30,'peak_commit_bytes':40}
        s._threads=threads;return s

    def test_periodic_memory_has_no_thread_query_or_old_values(self):
        s=self.scope()
        boundary=s._sample('HostReady');self.assertEqual(boundary['thread_ids'],[1,3])
        row=s._sample('periodic',7)
        self.assertEqual(self.thread_calls,1);self.assertIsNone(row['thread_ids']);self.assertIsNone(row['thread_query'])
        self.assertEqual(row['missed_intervals'],7)
        self.assertTrue(row['memory_query']['ok'])
        self.assertLessEqual(row['memory_query']['started_ticks'],row['memory_query']['completed_ticks'])

    def test_each_fixed_boundary_queries_threads_with_separate_ticks(self):
        s=self.scope()
        for name in ('assigned_suspended',*PHASES):
            row=s._sample(name)
            self.assertEqual(row['thread_ids'],[1,3]);self.assertTrue(row['thread_query']['ok'])
            self.assertLessEqual(row['memory_query']['completed_ticks'],row['thread_query']['started_ticks'])
            self.assertLessEqual(row['thread_query']['started_ticks'],row['thread_query']['completed_ticks'])
        self.assertEqual(self.thread_calls,12)

    def test_failure_keeps_attempt_timing_without_fake_zero(self):
        s=self.scope()
        def fail():raise ObservationError('thread-control-failure')
        s._threads=fail
        with self.assertRaises(ObservationError):s._sample('HostReady')
        row=s.data['samples'][0]
        self.assertFalse(row['ok']);self.assertTrue(row['memory_query']['ok']);self.assertFalse(row['thread_query']['ok'])
        self.assertIsNone(row['thread_ids']);self.assertIsNone(row['thread_query']['win32_error'])
        self.assertIsNotNone(row['thread_query']['completed_ticks'])
        s=self.scope();s._memory=fail
        with self.assertRaises(ObservationError):s._sample('periodic')
        row=s.data['samples'][0];self.assertFalse(row['memory_query']['ok']);self.assertIsNone(row['private_bytes'])
        self.assertIsNotNone(row['memory_query']['completed_ticks'])

    def test_v2_identity_differs_from_v1_same_limits(self):
        s=self.scope();c=s.config
        old={'method':'ock.native-footprint/1',**asdict(c),'sample_interval_ms':5}
        old_digest=hashlib.sha256(json.dumps(old,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        self.assertNotEqual(c.identity(),old_digest)
        self.assertEqual(c.memory_sampling,'due_5ms');self.assertEqual(c.thread_sampling,'phase_boundaries')

    def test_summary_counts_only_real_boundary_thread_queries(self):
        from tools.footprint.analyze import summarize_observation
        s=self.scope()
        for reason in ('assigned_suspended',*PHASES):s._sample(reason)
        periodic=s._sample('periodic')
        observation={'method':'ock.native-footprint/2','method_digest':s.config.identity(),'configuration':asdict(s.config),
                     'memory_sampling':'due_5ms','thread_sampling':'phase_boundaries','status':'Complete',
                     'samples':s.data['samples'],'phases':[{'phase':p} for p in PHASES]}
        result={'status':'Exited','exit_code':0,'process_tree':{'assigned_before_resume':True,'active_after':0,'terminated_owned_job':False},'observation':observation}
        summary=summarize_observation(result)
        self.assertEqual(summary['observed_thread_peak'],2)
        self.assertEqual(summary['thread_sample_count'],12)
        self.assertEqual(summary['thread_peak_scope'],'phase_boundaries')
        for method in (None,'ock.native-footprint/1','ock.native-footprint/999'):
            with self.subTest(method=method),self.assertRaises(ValueError):
                summarize_observation({**result,'observation':{**observation,'method':method}})
        missing={k:v for k,v in observation.items() if k!='method'}
        with self.assertRaises(ValueError):summarize_observation({**result,'observation':missing})
        periodic['thread_ids']=[1,3]
        with self.assertRaises(ValueError):summarize_observation(result)


if __name__=='__main__':unittest.main()
