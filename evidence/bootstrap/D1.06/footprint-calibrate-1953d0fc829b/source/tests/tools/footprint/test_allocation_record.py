"""真实 allocation 报告缺项/通道/注入判定护栏。"""
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.footprint.allocation_record import validate_allocation


class AllocationRecordTests(unittest.TestCase):
    def record(self):
        def row(label,index=0):return {'window':label,'index':index,'success':True,'cpp':0,'crt':0,'asan_allocations':None,'asan_frees':None,
                                     'cpp_frees':0,'allocated_bytes':0,'released_bytes':0,'live_before':0,'live_after':0,'peak_live':0}
        samples=[row('probe',i) for i in range(12)]+[row('probe_negative')]
        for i,r in enumerate(samples[:12]):
            r['cpp']=1 if i<8 else 0;r['crt']=1
            if i<8:
                size=64 if i in (2,3,6,7) else 37
                r.update(cpp_frees=1,allocated_bytes=size,released_bytes=size,peak_live=size)
        samples += [row(name) for name in ('construct_register_start','log_proof_open_verify_bind','first_compute','first_read','invalid_input','shutdown','bound_release','session_release','last_owner_release')]
        samples += [row('warmup',i) for i in range(4)]+[row('invoke',i) for i in range(40)]
        return {'format':'ock.footprint-allocation/1','kind':'native','verified':True,'coverage':{'cpp':True,'crt':True,'asan':False},'samples':samples}
    def test_missing_duplicate_false_or_nonzero_cannot_pass(self):
        good=self.record();validate_allocation(good,'native','Debug',False)
        for change in ({'verified':False},{'samples':good['samples'][:-1]},{'samples':good['samples']+[good['samples'][-1]]}):
            with self.assertRaises(ValueError):validate_allocation({**good,**change},'native','Debug',False)
        for field,value in [('cpp',1),('crt',1),('success',False)]:
            bad=self.record();bad['samples'][-1][field]=value
            with self.assertRaises(ValueError):validate_allocation(bad,'native','Debug',False)
    def test_injection_requires_specific_positive_window_and_failed_record(self):
        good=self.record()
        with self.assertRaises(ValueError):validate_allocation(good,'native','Debug',False,injected=True)
        good['verified']=False;target=next(r for r in good['samples'] if r['window']=='invoke' and r['index']==0)
        target.update(cpp=1,crt=1,cpp_frees=1,allocated_bytes=37,released_bytes=37,peak_live=37)
        validate_allocation(good,'native','Debug',False,injected=True)
        target['cpp']=0
        with self.assertRaises(ValueError):validate_allocation(good,'native','Debug',False,injected=True)
    def test_inapplicable_channels_are_null_not_fabricated_zero(self):
        bad=self.record();bad['coverage']['crt']=False
        with self.assertRaises(ValueError):validate_allocation(bad,'native','Release',False)
    def test_ledger_fields_are_required_nonnegative_integers(self):
        for name in ('cpp_frees','allocated_bytes','released_bytes','live_before','live_after','peak_live'):
            for value in (None,-1,True):
                with self.subTest(name=name,value=value),self.assertRaises(ValueError):
                    bad=self.record();bad['samples'][-1][name]=value;validate_allocation(bad,'native','Debug',False)
            with self.subTest(missing=name),self.assertRaises(ValueError):
                bad=self.record();del bad['samples'][-1][name];validate_allocation(bad,'native','Debug',False)
    def test_ledger_balance_peak_and_probe_free_fact(self):
        for changes in ({'live_after':1},{'live_before':9,'live_after':9,'peak_live':8}):
            with self.subTest(changes=changes),self.assertRaises(ValueError):
                bad=self.record();bad['samples'][-1].update(changes);validate_allocation(bad,'native','Debug',False)
        bad=self.record();bad['samples'][0]['cpp_frees']=0
        with self.assertRaises(ValueError):validate_allocation(bad,'native','Debug',False)


if __name__=='__main__':unittest.main()
