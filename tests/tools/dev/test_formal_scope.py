"""候选清单完整性；不运行正式入口、不生成预期。"""
import json
from pathlib import Path
import re
import unittest
ROOT=Path(__file__).resolve().parents[3]

class FormalScope(unittest.TestCase):
    def test_exact_hand_selected_profile_counts_and_regex(self):
        expected=json.loads((ROOT/'tests/manifests/d1.06.expected.json').read_text(encoding='utf8'))
        ids=[c['id'] for c in expected['cases']];self.assertEqual(len(ids),len(set(ids)))
        for suffix,count in [('debug',270),('release',192),('asan',193)]:
            profile='win-msvc-'+suffix
            wanted={c['id'] for c in expected['cases'] if profile in c['profiles']}
            self.assertEqual(len(wanted),count)
            run=json.loads((ROOT/f'tests/runs/d1.06-{profile}.json').read_text(encoding='utf8'))
            self.assertEqual(wanted,{name for name in ids if re.fullmatch(run['test_regex'],name)})
            self.assertEqual(run['configure'][1:3],['-C','tests/runs/d1.06-prerequisites.cmake'])
            self.assertFalse(any('_internal.lib' in x for x in run['build_outputs']))
            self.assertIn('footprint-budgets.json',run['required_artifacts'])
            self.assertTrue(any(x['pattern'].endswith('/qualification.json') for x in run['required_runtime_artifacts']))
    def test_new_fifty_one_responsibilities_are_not_lost(self):
        expected=json.loads((ROOT/'tests/manifests/d1.06.expected.json').read_text(encoding='utf8'))
        for family,count in [('host',20),('logging',12),('native_sdk',8),('footprint',11)]:
            self.assertEqual(sum('.'+family+'.' in c['id'] for c in expected['cases']),count)
    def test_gates_reuse_exact_same_three_runs(self):
        a=json.loads((ROOT/'tests/runs/d1.06-matrix.json').read_text())
        b=json.loads((ROOT/'tests/runs/g1-native-matrix.json').read_text())
        self.assertEqual(a['required_runs'],b['required_runs']);self.assertEqual(len(a['required_runs']),3)
        self.assertTrue(all(r['task_id']=='D1.06' for r in a['required_runs']))
