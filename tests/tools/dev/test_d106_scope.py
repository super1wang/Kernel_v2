"""D1.06 固定选择反例；不依赖本次CTest发现列表。"""
from pathlib import Path
import unittest
from tools.dev import verify

ROOT=Path(__file__).resolve().parents[3]

class NativeScopeSelectionTests(unittest.TestCase):
    def test_host_source_has_actual_consumers(self):
        chosen=verify.select(['packages/runtime/host/host.cpp'])
        self.assertTrue({'host','native','policy'} <= set(chosen['groups']))
        self.assertFalse(chosen['unknown'])

    def test_logging_and_tests_include_host(self):
        for path in ['packages/runtime/observability/logging.cpp','tests/conformance/logging/reference_backend.hpp']:
            chosen=verify.select([path])
            self.assertTrue({'logging','host'} <= set(chosen['groups']))
            self.assertIn('asan',chosen['risks'])

    def test_canonical_header_has_all_transitive_consumers(self):
        chosen=verify.select(['packages/runtime/include/ock/runtime/detail/host.hpp'])
        self.assertTrue({'contracts','registry','policy','native','host','logging','sdk'} <= set(chosen['groups']))
        self.assertTrue({'release','asan'} <= set(chosen['risks']))

    def test_core_contract_support_includes_host_and_logging(self):
        for path in ['packages/contracts/include/ock/contracts/logging.hpp','tests/compile/contracts/test_support.hpp']:
            chosen=verify.select([path])
            self.assertTrue({'host','logging','contracts','registry','policy','native'} <= set(chosen['groups']))

    def test_footprint_and_stateless_consumer_mappings(self):
        self.assertIn('footprint',verify.select(['tools/footprint/run.py'])['groups'])
        self.assertTrue({'sdk','host','footprint'} <= set(verify.select(['examples/stateless_service/main.cpp'])['groups']))

    def test_native_sdk_family_is_not_silently_omitted(self):
        names=verify.selected_cases(ROOT,'D1.06','win-msvc-debug',{'sdk'})
        self.assertIn('T24.native_sdk.installed_host',names)
        self.assertIn('T02.native_sdk.private_dispatch',names)
        self.assertIn('T01.native_sdk.surface_guard',names)

    def test_host_family_uses_current_package_expected(self):
        names=verify.selected_cases(ROOT,'D1.06','win-msvc-debug',{'host'})
        self.assertEqual(len(names),20)
        self.assertIn('T22.host.return_lifetime',names)

    def test_current_profile_does_not_inherit_inapplicable_cases(self):
        names=verify.selected_cases(ROOT,'D1.06','win-msvc-release',{'sdk','footprint','contracts'})
        self.assertNotIn('T01.native_sdk.surface_guard',names)
        self.assertNotIn('T23.footprint.budget_provenance',names)
        self.assertIn('T23.footprint.ready_latency',names)
        self.assertIn('T02.contracts.compile_fixture_setup',names)
        self.assertNotIn('T02.contracts.compile_fixture_guards',names)

    def test_formal_preload_is_kept_for_full_scope(self):
        argv=['cmake','--fresh','-C','tests/runs/d1.06-prerequisites.cmake','-B','old']
        self.assertIn('tests/runs/d1.06-prerequisites.cmake',verify.development_configure(argv,'old','new',True))
        limited=verify.development_configure(argv,'old','new',False)
        self.assertEqual(limited,['cmake','-B','new'])

if __name__=='__main__':unittest.main()
