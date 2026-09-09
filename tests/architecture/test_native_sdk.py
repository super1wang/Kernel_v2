"""D1.06 生产 NativeSubset 表面与最窄模板实现包含边界。"""
from copy import deepcopy
import unittest
from tools.architecture import check


class NativeSurfaceTests(unittest.TestCase):
    def test_log_component_enum_is_not_host_type(self):
        manifest = check.load_manifest()
        declaration = 'enum class LogComponent : std::uint8_t { Host=0, Registry=1, Policy=2, Invocation=3 };'
        self.assertEqual(check.validate_include('CoreContracts', declaration, manifest, True), [])
        self.assertTrue(check.validate_include('CoreContracts', declaration+' class Host {};', manifest, True))

    def test_actual_native_surface(self):
        manifest = check.load_manifest()
        self.assertEqual(manifest['stage'], 'B3Subset')
        self.assertEqual(manifest['sdk_version'], '0.1.0-dev.4')
        self.assertEqual(manifest['targets']['Runtime']['kind'], 'STATIC_LIBRARY')
        self.assertEqual(check.validate_manifest(manifest), [])

    def test_system_dependency_is_exact(self):
        for value in ([], [{'name':'sqlite3','platform':'Windows','link_only':True}],
                      [{'name':'bcrypt','platform':'Windows','link_only':False}]):
            manifest = deepcopy(check.load_manifest())
            manifest['targets']['Runtime']['system_dependencies'] = value
            self.assertTrue(check.validate_manifest(manifest))

    def test_fixed_surface_cannot_grow_with_manifest(self):
        manifest = deepcopy(check.load_manifest())
        manifest['headers'].append({'path':'packages/runtime/include/ock/runtime/extra.hpp',
            'target':'Runtime','classification':'experimental','sha256':'0'*64})
        self.assertIn('Runtime安装头集合与受审八头不符',check.validate_manifest(manifest))
        manifest = deepcopy(check.load_manifest())
        manifest['headers']=[h for h in manifest['headers'] if h['path'].endswith('/logging.hpp') is False]
        self.assertTrue(check.validate_contracts_surface(manifest))

    def test_unknown_detail_still_rejected(self):
        manifest = check.load_manifest()
        for include in ('ock/runtime/detail/unknown.hpp', 'ock/contracts/detail/internal.hpp'):
            self.assertTrue(check.validate_include('Runtime', '#include <'+include+'>', manifest, True))

    def test_approved_edge_needs_actual_source(self):
        manifest = check.load_manifest()
        include = '#include <ock/runtime/detail/host.hpp>'
        self.assertTrue(check.validate_include('Runtime', include, manifest, True))
        self.assertEqual(check.validate_include('Runtime', include, manifest, True,
                         'packages/runtime/include/ock/runtime/host.hpp'), [])
        self.assertTrue(check.validate_include('Runtime', include, manifest, True,
                         'packages/runtime/include/ock/runtime/policy.hpp'))

    def test_installed_detail_actual_edges_are_fixed(self):
        manifest=check.load_manifest()
        for source,includes in check.NATIVE_IMPLEMENTATION_INCLUDES.items():
            for header in includes:
                self.assertEqual(check.validate_include('Runtime','#include <'+header+'>',manifest,
                                 '/detail/' not in source,source),[])
        for source,header in [
            ('packages/runtime/include/ock/runtime/detail/host.hpp','ock/runtime/detail/private_bridge.hpp'),
            ('packages/runtime/include/ock/runtime/detail/invocation.hpp','ock/runtime/detail/unknown.hpp')]:
            with self.subTest(source=source,header=header):
                self.assertTrue(check.validate_include('Runtime','#include <'+header+'>',manifest,False,source))
        self.assertEqual(check.validate_include('Runtime','#include <ock/runtime/detail/private_bridge.hpp>',
                         manifest,False,'packages/runtime/invocation/invocation.cpp'),[])


if __name__ == '__main__':
    unittest.main()
