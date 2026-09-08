"""导航只提供当前来源定位；不接受过期或伪造的规范引用。"""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('current', ROOT/'tools/dev/current.py')
current = importlib.util.module_from_spec(spec)
spec.loader.exec_module(current)


class CurrentTests(unittest.TestCase):
    def test_non_object_navigation_is_stale(self):
        self.assertTrue(current.stale_reasons(ROOT, [], 'head-a', 'work/test'))

    def test_current_roundtrip_and_head_mismatch(self):
        value = current.build(ROOT, 'head-a', 'work/test')
        self.assertFalse(current.stale_reasons(ROOT, value, 'head-a', 'work/test'))
        self.assertIn('HEAD changed', current.stale_reasons(ROOT, value, 'head-b', 'work/test'))

    def test_normative_hash_cannot_be_forged(self):
        value = current.build(ROOT, 'head-a', 'work/test')
        value['architecture_refs'][0]['sha256'] = '0'*64
        self.assertTrue(current.stale_reasons(ROOT, value, 'head-a', 'work/test'))

    def test_package_and_progress_bound(self):
        value = current.build(ROOT, 'head-a', 'work/test')
        value['current_package'] = 'D1.05'
        self.assertTrue(current.stale_reasons(ROOT, value, 'head-a', 'work/test'))

    def test_path_escape_and_removed_reference_rejected(self):
        value = current.build(ROOT, 'head-a', 'work/test')
        value['architecture_refs'][0]['path'] = '../outside.md'
        self.assertTrue(current.stale_reasons(ROOT, value, 'head-a', 'work/test'))
        value = current.build(ROOT, 'head-a', 'work/test')
        value['architecture_refs'] = []
        self.assertTrue(current.stale_reasons(ROOT, value, 'head-a', 'work/test'))


if __name__ == '__main__':
    unittest.main()
