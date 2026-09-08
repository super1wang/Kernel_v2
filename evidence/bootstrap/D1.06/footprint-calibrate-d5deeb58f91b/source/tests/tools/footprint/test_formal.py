"""正式聚合不得挑样、复用 pilot、隐去超限或放过输入变动。"""
import unittest
from tools.footprint.formal import bounded_metrics, verify_inventory
from pathlib import Path
import tempfile


class FormalTests(unittest.TestCase):
    def test_exact_finite_metrics(self):
        bounded_metrics({'bytes': 10}, {'bytes': 10})
        for metrics, limits in [({'bytes': 11}, {'bytes': 10}),
                                ({}, {'bytes': 10}),
                                ({'bytes': float('nan')}, {'bytes': 10}),
                                ({'bytes': 0}, {'bytes': float('inf')}),
                                ({'bytes': 0}, {'bytes': True})]:
            with self.assertRaises(ValueError): bounded_metrics(metrics, limits)

    def test_missing_or_changed_raw_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'raw'; p.write_bytes(b'actual')
            with self.assertRaises(ValueError):
                verify_inventory([{'path':str(p),'sha256':'0'*64,'bytes':6}])
            with self.assertRaises(ValueError):
                verify_inventory([{'path':str(p)+'missing','sha256':'0'*64,'bytes':6}])

if __name__=='__main__': unittest.main()
