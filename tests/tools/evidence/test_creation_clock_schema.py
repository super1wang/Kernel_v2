"""真实首轮报告触发的 QPC 字段回归；只校验格式，不改其失败结论。"""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'build/python-deps'))
from jsonschema import Draft202012Validator


class CreationClockSchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.schema=json.loads((ROOT/'schemas/evidence-v1.schema.json').read_text(encoding='utf-8'))
        cls.report=json.loads((ROOT/'evidence/c4b5c974f8d1-257f5d1d66c1/win-msvc-debug/D3.07/20260909T233415Z-4005f95839c2/report.json').read_text(encoding='utf-8'))
        cls.validator=Draft202012Validator(cls.schema)

    def test_real_report_accepts_recorded_clock(self):
        self.assertTrue(any('creation_clock' in row for row in self.report['commands']))
        self.validator.validate(self.report)
        self.assertEqual(self.report['automated_status'],'Failed')

    def test_legacy_commands_may_omit_clock(self):
        report=copy.deepcopy(self.report)
        for row in report['commands']:row.pop('creation_clock',None)
        self.validator.validate(report)

    def test_invalid_clock_and_unknown_command_fields_rejected(self):
        for clock in ({'qpc_ticks':1,'qpc_frequency':0},{'qpc_ticks':-1,'qpc_frequency':1},
          {'qpc_ticks':True,'qpc_frequency':1},{'qpc_ticks':1,'qpc_frequency':2**63},
          {'qpc_ticks':1},{'qpc_ticks':1,'qpc_frequency':1,'extra':1}):
            with self.subTest(clock=clock):
                report=copy.deepcopy(self.report);report['commands'][0]['creation_clock']=clock
                self.assertTrue(list(self.validator.iter_errors(report)))
        report=copy.deepcopy(self.report);report['commands'][0]['unknown_field']=1
        self.assertTrue(list(self.validator.iter_errors(report)))


if __name__=='__main__':unittest.main()
