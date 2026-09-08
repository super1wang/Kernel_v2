"""D1.02阶段、公开闭包及能力边界的正反守卫。"""
from copy import deepcopy
import unittest
from tools.architecture import check

HEADERS = ('identity', 'context', 'outcome', 'ports', 'observation', 'operation')

def manifest():
    value = deepcopy(check.load_manifest())
    value['stage'] = 'CoreContracts'
    value['sdk_version'] = '0.1.0-dev.1'
    for name, target in value['targets'].items():
        if name not in ('Foundation', 'CoreContracts'):
            target.update(implementation='ContractBaseline',kind='INTERFACE_LIBRARY',system_dependencies=[])
    for target in value['targets'].values(): target['public_compile_definitions'] = []
    value['targets']['CoreContracts']['implementation'] = 'Implemented'
    value['headers'] = [h for h in value['headers'] if h['target'] != 'CoreContracts'] + [
        {'path': f'packages/contracts/include/ock/contracts/{name}.hpp', 'target': 'CoreContracts', 'classification': 'experimental', 'sha256': '0'*64}
        for name in HEADERS]
    return value

class ComponentClosureTests(unittest.TestCase):
    def test_core_stage_preserves_foundation_and_only_adds_contracts(self):
        self.assertEqual(check.validate_implementation_stage(manifest()), [])
    def test_other_product_implementation_rejected(self):
        for name in ('Runtime', 'Data', 'Control', 'State'):
            with self.subTest(name=name):
                value = manifest(); value['targets'][name]['implementation'] = 'Implemented'
                self.assertTrue(check.validate_implementation_stage(value))
    def test_foundation_cannot_regress_or_lose_expected(self):
        for key, value in [('implementation', 'ContractBaseline'), ('external_dependencies', [])]:
            data = manifest(); data['targets']['Foundation'][key] = value
            self.assertTrue(check.validate_implementation_stage(data))
    def test_contracts_cannot_duplicate_or_add_third_party_backend(self):
        for value in (['expected'], ['sqlite'], ['jsoncons']):
            data = manifest(); data['targets']['CoreContracts']['external_dependencies'] = value
            self.assertTrue(check.validate_implementation_stage(data))
    def test_product_closure_rejects_other_modules_and_unknown_targets(self):
        data = manifest()
        self.assertEqual(check.transitive_dependencies('CoreContracts', data['targets']), {'Foundation'})
        for name in ('Runtime', 'Data', 'Control', 'Adapter::SQLite', 'Unknown'):
            changed = deepcopy(data); changed['targets']['CoreContracts']['dependencies'].append(name)
            self.assertIn('CoreContracts 直接依赖与 A02 不一致', check.validate_manifest(changed))

class PublicBoundaryTests(unittest.TestCase):
    def test_registered_public_macros_must_remain_explicitly_empty(self):
        data = manifest()
        diagnostic = 'CoreContracts 公开宏定义漂移'
        self.assertNotIn(diagnostic, check.validate_manifest(data))
        for values in (['OCK_CONTRACTS_UNSAFE=1'], ['$<$<CONFIG:Debug>:NDEBUG>'], None):
            changed = deepcopy(data)
            if values is None: changed['targets']['CoreContracts'].pop('public_compile_definitions')
            else: changed['targets']['CoreContracts']['public_compile_definitions'] = values
            self.assertIn(diagnostic, check.validate_manifest(changed))
    def test_actual_public_macros_and_compile_conditions_match_manifest(self):
        data = manifest()
        graph = {name: {'dependencies': t['dependencies'], 'implementation': t['implementation'],
                 'external_dependencies': t.get('external_dependencies', []),
                 'compile_features': t['public_compile_features'], 'compile_options': t['public_compile_options'],
                 'compile_definitions': t['public_compile_definitions']} for name, t in data['targets'].items()}
        for key, value, diagnostic in (
            ('compile_definitions', ['OCK_CONTRACTS_UNSAFE=1'], 'CoreContracts 实际公开宏定义漂移'),
            ('compile_definitions', ['$<$<CONFIG:Debug>:NDEBUG>'], 'CoreContracts 实际公开宏定义漂移'),
            ('compile_features', ['cxx_std_17'], 'CoreContracts 实际公开编译要求漂移'),
            ('compile_options', ['/utf-8', '/EHs-c-'], 'CoreContracts 实际公开选项漂移')):
            self.assertNotIn(diagnostic, check.validate_manifest(data, graph))
            changed = deepcopy(graph); changed['CoreContracts'][key] = value
            self.assertIn(diagnostic, check.validate_manifest(data, changed))
        changed = deepcopy(graph); changed['CoreContracts'].pop('compile_definitions')
        self.assertIn('CoreContracts 实际公开宏定义漂移', check.validate_manifest(data, changed))
    def test_exact_six_headers_required(self):
        self.assertEqual(check.validate_contracts_surface(manifest()), [])
        for change in ('missing', 'extra', 'duplicate', 'wrong_owner'):
            data = manifest()
            if change == 'missing': data['headers'].pop()
            elif change == 'extra': data['headers'].append({'path': 'packages/contracts/include/ock/contracts/extra.hpp', 'target': 'CoreContracts'})
            elif change == 'duplicate': data['headers'].append(deepcopy(data['headers'][-1]))
            else: data['headers'][-1]['target'] = 'Foundation'
            with self.subTest(change=change): self.assertTrue(check.validate_contracts_surface(data))
    def test_public_capability_backdoors_rejected(self):
        for text in ('class Host;', 'Document* document();', 'Payload payload();', 'ServiceLocator& services();', 'std::any get();', 'void* service();'):
            with self.subTest(text=text): self.assertTrue(check.validate_include('CoreContracts', text, manifest(), True))
    def test_comments_literals_and_narrow_types_not_backdoors(self):
        source = '// Host and void* are forbidden in interfaces\nconst char* text="Payload Document std::any";\nclass HostIncarnation;'
        self.assertEqual(check.validate_include('CoreContracts', source, manifest(), True), [])
    def test_dom_sql_and_runtime_includes_rejected(self):
        for header in ('ock/data/payload.hpp', 'ock/runtime/host.hpp', 'sqlite3.h', 'jsoncons/json.hpp'):
            with self.subTest(header=header): self.assertTrue(check.validate_include('CoreContracts', '#include <'+header+'>', manifest(), True))

if __name__ == '__main__': unittest.main()
