"""B2 SDK 组件接线：检查真实库、薄客户端和 Native 隔离。"""
from copy import deepcopy
import unittest
from tools.architecture import check


class B2SurfaceTests(unittest.TestCase):
    def test_production_components(self):
        manifest = check.load_manifest()
        self.assertEqual(manifest['stage'], 'B2Subset')
        for name in ('Data', 'Dynamic', 'ControlProtocol', 'Control'):
            self.assertEqual(manifest['targets'][name]['kind'], 'STATIC_LIBRARY')
        self.assertEqual(check.validate_manifest(manifest), [])

    def test_native_and_protocol_closures(self):
        manifest = check.load_manifest()
        self.assertEqual(check.transitive_dependencies('Runtime', manifest['targets']),
                         {'CoreContracts', 'Foundation'})
        self.assertTrue(check.validate_include('Runtime', '#include <ock/data/payload.hpp>', manifest))
        self.assertTrue(check.validate_include('ControlProtocol', '#include <ock/runtime/host.hpp>', manifest))
        self.assertTrue(check.validate_include('Control', '#include <ock/automation/plan.hpp>', manifest))

    def test_implementation_cannot_be_claimed_without_library(self):
        manifest = deepcopy(check.load_manifest())
        manifest['targets']['Data']['kind'] = 'INTERFACE_LIBRARY'
        self.assertTrue(check.validate_manifest(manifest))

    def test_observation_source_owner_cannot_expand(self):
        manifest = deepcopy(check.load_manifest())
        manifest['targets']['Control']['source_roots'].append('packages/workspace')
        self.assertTrue(check.validate_manifest(manifest))
