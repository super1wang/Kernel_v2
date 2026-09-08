from dataclasses import replace,asdict
import unittest
from tools.footprint.windows_process import ObservationConfig,PHASES
from tools.footprint.analyze import require_sampling_v2


class LatencyIdentityTests(unittest.TestCase):
    def config(self):return ObservationConfig('occupancy',3000,4000,100,20,20,260,1,4096,500,1)

    def test_mode_identity(self):
        c=self.config();latency=replace(c,mode='latency')
        self.assertEqual(c.memory_sampling,'due_5ms');self.assertEqual(c.sample_interval_ms,5)
        self.assertEqual(replace(c,mode='allocation').memory_sampling,'due_5ms')
        self.assertEqual(latency.memory_sampling,'phase_boundaries');self.assertIsNone(latency.sample_interval_ms)
        self.assertNotEqual(c.identity(),latency.identity())

    def test_latency_rejects_periodic_and_wrong_metadata(self):
        c=replace(self.config(),mode='latency')
        query={'ok':True,'win32_error':0,'structure_bytes':80,'started_ticks':1,'completed_ticks':2}
        rows=[{'reason':reason,'memory_query':query,'thread_query':{**query,'started_ticks':2},'thread_ids':[1]}
              for reason in ('assigned_suspended',*PHASES)]
        observation={'configuration':asdict(c),'method_digest':c.identity(),'memory_sampling':c.memory_sampling,
                     'thread_sampling':c.thread_sampling,'samples':rows}
        require_sampling_v2(observation)
        for label in (None,'due_5ms'):
            with self.subTest(label=label),self.assertRaises(ValueError):
                require_sampling_v2({**observation,'memory_sampling':label})
        with self.assertRaises(ValueError):
            require_sampling_v2({**observation,'samples':rows+[{'reason':'periodic','memory_query':query,'thread_query':None,'thread_ids':None}]})

if __name__=='__main__':unittest.main()
