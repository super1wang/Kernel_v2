"""D0.02 架构守卫反例；不是尚未实现的产品运行测试。"""
from copy import deepcopy
from pathlib import Path
import unittest
from unittest.mock import patch
from tools.architecture.check import (load_manifest, validate_manifest, transitive_dependencies,
    validate_include, validate_frontend_access, validate_rpc_methods)

class ArchitectureTests(unittest.TestCase):
    def setUp(self):
        self.manifest = load_manifest()

    def test_approved_architecture_matches_manifest(self):
        self.assertEqual([], validate_manifest(self.manifest))

    def test_runtime_cannot_depend_on_state_even_transitively(self):
        self.manifest['targets']['CoreContracts']['dependencies'].append('State')
        self.assertTrue(validate_manifest(self.manifest))

    def test_target_cycle_is_rejected(self):
        self.manifest['targets']['Foundation']['dependencies'] = ['Runtime']
        self.assertTrue(validate_manifest(self.manifest))

    def test_state_only_depends_on_core_contracts(self):
        self.assertEqual({'CoreContracts', 'Foundation'}, transitive_dependencies('State', self.manifest['targets']))

    def test_client_has_no_server_dependency(self):
        self.assertFalse({'Runtime', 'Control', 'Workspace'} & transitive_dependencies('ControlClient', self.manifest['targets']))

    def test_observation_cannot_become_product_target(self):
        self.manifest['targets']['Observation'] = deepcopy(self.manifest['targets']['Control'])
        self.assertTrue(validate_manifest(self.manifest))

    def test_test_support_cannot_enter_product_dag(self):
        self.manifest['targets']['Runtime']['dependencies'].append('TestSupport')
        self.assertTrue(validate_manifest(self.manifest))

    def test_runtime_include_to_data_rejected(self):
        self.assertTrue(validate_include('Runtime', '#include <ock/data/payload.hpp>', self.manifest))

    def test_ock_include_path_traversal_is_rejected(self):
        self.assertTrue(validate_include('Runtime','#include <ock/contracts/../data/payload.hpp>',self.manifest))

    def test_include_comment_does_not_bypass_ownership(self):
        self.assertTrue(validate_include('Runtime','#include/**/<ock/data/payload.hpp>',self.manifest))

    def test_comment_markers_inside_strings_do_not_hide_includes(self):
        source='const char* begin = "/*";\n#include <ock/data/payload.hpp>\nconst char* end = "*/";'
        self.assertTrue(validate_include('Runtime',source,self.manifest))

    def test_multiline_comment_preserves_directive_line(self):
        self.assertTrue(validate_include('Runtime','int x; /* comment\n*/ #include <ock/data/payload.hpp>',self.manifest))

    def test_dot_include_prefix_is_rejected(self):
        self.assertTrue(validate_include('Runtime','#include <./ock/data/payload.hpp>',self.manifest))

    def test_raw_string_include_text_is_not_a_directive(self):
        self.assertEqual([],validate_include('Runtime','const char* p = R"tag(\n#include <ock/data/payload.hpp>\n)tag";',self.manifest))

    def test_macro_include_requires_explicit_review(self):
        self.assertTrue(validate_include('Runtime','#include HIDDEN_HEADER',self.manifest))

    def test_unregistered_h_header_is_detected(self):
        from tools.architecture.check import ROOT
        extra=ROOT/'packages/foundation/include/ock/foundation/unregistered.h'
        real_glob=Path.rglob;real_read=Path.read_text
        def glob_with_header(path,pattern):
            yield from real_glob(path,pattern)
            if path==ROOT/'packages' and pattern in ('*','*.h'):
                yield extra
        def read_with_header(path,*args,**kwargs):
            return '#pragma once' if path==extra else real_read(path,*args,**kwargs)
        with patch.object(Path,'rglob',glob_with_header),patch.object(Path,'read_text',read_with_header):
            self.assertTrue(validate_manifest(self.manifest))

    def test_runtime_include_to_state_rejected(self):
        self.assertTrue(validate_include('Runtime', '#include <ock/state/state.hpp>', self.manifest))

    def test_runtime_can_include_core_contracts(self):
        self.assertEqual([], validate_include('Runtime', '#include <ock/contracts/operation.hpp>', self.manifest))

    def test_workspace_cannot_include_runtime_private(self):
        self.assertTrue(validate_include('Workspace', '#include <ock/runtime/detail/registry.hpp>', self.manifest))

    def test_public_header_cannot_expose_private_dependency(self):
        self.assertTrue(validate_include('Data', '#include <jsoncons/json.hpp>', self.manifest, public=True))

    def test_unknown_ock_header_has_no_implicit_owner(self):
        self.assertTrue(validate_include('Runtime', '#include <ock/unknown/access.hpp>', self.manifest))

    def test_ui_business_write_must_use_operation(self):
        for kind in ('StateEdit','ExternalEffect','Lifecycle'):
            with self.subTest(kind=kind):
                self.assertTrue(validate_frontend_access('UI',kind,'Direct',True,True))
                self.assertEqual([],validate_frontend_access('UI',kind,'Operation',False,False))

    def test_workspace_write_cannot_bypass_operation(self):
        self.assertTrue(validate_frontend_access('Workspace','StateEdit','Direct',True,True))

    def test_authorized_immutable_read_is_allowed(self):
        self.assertEqual([],validate_frontend_access('UI','Read','Direct',True,True))
        self.assertTrue(validate_frontend_access('UI','Read','Direct',False,True))
        self.assertTrue(validate_frontend_access('UI','Read','Direct',True,False))

    def test_generic_state_rpc_is_rejected(self):
        self.assertTrue(validate_rpc_methods(['state.inspect']))
        self.assertTrue(validate_rpc_methods(['state.query']))
        self.assertEqual([],validate_rpc_methods(['operation.invoke','capabilities.describe']))

    def test_dsl_is_not_a_required_release_component(self):
        self.manifest['required_frontends'].append('DSL')
        self.assertTrue(validate_manifest(self.manifest))

    def test_version_is_independent_from_document_version(self):
        self.manifest['sdk_version'] = '3.3'
        self.assertTrue(validate_manifest(self.manifest))

    def test_public_header_signature_change_is_detected(self):
        self.manifest['headers'][0]['sha256'] = '0' * 64
        self.assertTrue(validate_manifest(self.manifest))

    def test_no_runtime_component_is_marked_implemented(self):
        self.manifest['targets']['Runtime']['implementation'] = 'Implemented'
        self.assertTrue(validate_manifest(self.manifest))

if __name__ == '__main__':
    unittest.main()
