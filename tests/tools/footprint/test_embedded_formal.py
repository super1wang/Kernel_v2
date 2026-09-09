"""G3-C 汇总拒绝真实样本的缺失、重复与非有限预算输入。"""
import copy,json,unittest
from pathlib import Path
from tools.footprint.embedded_formal import allocations,check_limits
ROOT=Path(__file__).resolve().parents[3]
class EmbeddedFormalTests(unittest.TestCase):
    def sample(self):
        p=ROOT/'evidence/B5/embedded-pilot-d8d809d523/1-embedded-stdout.log'
        return [json.loads(s) for s in p.read_text(encoding='utf-8').splitlines() if s.startswith('{')]
    def test_actual_allocator_sample(self):
        allocations(self.sample(),'embedded','win-msvc-asan')
    def test_corrupted_actual_sample_rejected(self):
        original=self.sample()
        for mutation in ('duplicate','remove','nonzero','wrong_channel','background','probe'):
            rows=copy.deepcopy(original);counts=next(r for r in rows if r.get('format')=='ock.embedded-allocation/2')
            if mutation=='duplicate':counts['samples'][6]=copy.deepcopy(counts['samples'][4])
            if mutation=='remove':counts['samples'].pop()
            if mutation=='nonzero':counts['samples'][4]['process_allocations']=1
            if mutation=='wrong_channel':counts['process_channel']='DebugCRT'
            if mutation=='background':counts['background_positive']=0
            if mutation=='probe':rows[0]['samples'][0]['success']=False
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):allocations(rows,'embedded','win-msvc-asan')
    def test_budget_omission_excess_nonfinite_rejected(self):
        check_limits({'bytes':10},{'limits':{'bytes':10}})
        for metrics in ({},{'bytes':11},{'bytes':float('nan')},{'bytes':True}):
            with self.subTest(metrics=metrics),self.assertRaises(ValueError):check_limits(metrics,{'limits':{'bytes':10}})
if __name__=='__main__':unittest.main()
