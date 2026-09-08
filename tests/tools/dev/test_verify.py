"""开发入口选择规则：不得把缺失覆盖或空运行解释为开发通过。"""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('dev_verify', ROOT/'tools/dev/verify.py')
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


class DevSelectionTests(unittest.TestCase):
    def test_native_change_is_narrow(self):
        selected = verify.select(['packages/runtime/invocation/invocation.cpp'])
        self.assertEqual(selected['groups'], ['host','native'])
        self.assertFalse(selected['full_reasons'])

    def test_public_contract_includes_transitive_consumers(self):
        selected = verify.select(['packages/contracts/include/ock/contracts/context.hpp'])
        self.assertTrue({'contracts', 'registry', 'policy', 'native'} <= set(selected['groups']))
        self.assertIn('release', selected['risks'])
        self.assertIn('asan', selected['risks'])

    def test_unknown_cannot_fall_through_as_green(self):
        selected = verify.select(['packages/unmapped/new.cpp'])
        self.assertEqual(selected['unknown'], ['packages/unmapped/new.cpp'])
        self.assertTrue(selected['full_reasons'])

    def test_empty_is_not_passed(self):
        self.assertEqual(verify.select([])['groups'], [])
        self.assertEqual(verify.select(['docs/progress.md'])['groups'], [])

    def test_tool_change_selects_own_tests(self):
        selected = verify.select(['tools/dev/verify.py'])
        self.assertEqual(selected['groups'], ['dev-tools'])
        self.assertFalse(selected['unknown'])

    def test_runtime_template_change_requires_release(self):
        selected = verify.select(['packages/runtime/invocation/invocation.hpp'])
        self.assertIn('release', selected['risks'])

    def test_shared_contract_fixture_covers_downstream_runtime(self):
        selected = verify.select(['tests/compile/contracts/test_support.hpp'])
        self.assertTrue({'registry','policy','native'} <= set(selected['groups']))

    def test_invalid_actual_build_identity_blocks_execution(self):
        spec = {'build_dir':'old','build_outputs':['old/probe.exe']}
        with patch.object(verify, 'observe', return_value=({}, ['actual CRT differs'])):
            with self.assertRaisesRegex(ValueError, 'actual CRT differs'):
                verify.capture_build(ROOT, spec, 'new', [], [])

    def test_missing_actual_binary_blocks_execution(self):
        spec = {'build_dir':'old','build_outputs':['old/probe.exe']}
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as temporary:
            with patch.object(verify, 'observe', return_value=({}, [])):
                with self.assertRaises(ValueError):
                    verify.capture_build(Path(temporary), spec, 'new', [], [])

    def test_build_change_records_full_reason(self):
        self.assertTrue(verify.select(['cmake/LockedMSVC.cmake'])['full_reasons'])

    def test_missing_current_manifest_rejects_full(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as temporary:
            with self.assertRaises(ValueError):
                verify.package_cases(Path(temporary), 'D1.06', 'win-msvc-debug')

    def test_exact_ctest_names_and_missing_execution(self):
        regex = verify.exact_regex(['T02.native.a', 'T02.native.ab'])
        import re
        self.assertIsNotNone(re.fullmatch(regex, 'T02.native.a'))
        self.assertIsNone(re.fullmatch(regex, 'T02.native.a_extra'))
        with self.assertRaises(ValueError):
            verify.check_executed(['T02.native.a', 'T02.native.ab'],
                                  [{'name': 'T02.native.a', 'status': 'Passed'}])

    def test_skip_and_duplicate_execution_rejected(self):
        for rows in ([{'name':'x','status':'Skipped'}],
                     [{'name':'x','status':'Passed'}]*2):
            with self.assertRaises(ValueError):
                verify.check_executed(['x'], rows)


if __name__ == '__main__':
    unittest.main()
