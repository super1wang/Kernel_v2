"""首个真实消费者单对的固定能力记录，不代表 pilot 或预算。"""
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.footprint.pair_record import validate_record


class PairRecordTests(unittest.TestCase):
    def test_missing_false_or_wrong_counts_rejected(self):
        good={'kind':'native','warmup_calls':4,'steady_calls':40,'handler_entries':46,
              'checks':{key:True for key in ('ready_gate','read','compute','invalid_input','async_unavailable','start_log_flush','public_log_page','ordinary_invoke_no_log','shutdown','stopped_bound','bound_release','session_release','last_owner_release')},
              'counter_mode':'disabled','logging_mode':'default-memory','runtime_workers':0}
        validate_record(good,'native')
        for change in ({'warmup_calls':3},{'steady_calls':39},{'handler_entries':45},{'kind':'baseline'},{'counter_mode':'asan'},{'checks':{}}):
            with self.assertRaises(ValueError):validate_record({**good,**change},'native')
        for key in good['checks']:
            with self.subTest(key=key),self.assertRaises(ValueError):
                validate_record({**good,'checks':{**good['checks'],key:False}},'native')
        with self.assertRaises(ValueError):validate_record({**good,'runtime_workers':1},'native')


if __name__=='__main__':unittest.main()
