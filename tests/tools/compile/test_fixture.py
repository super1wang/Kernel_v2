"""Profile 夹具护栏；合成命令只检验判定器，真实编译另存实测证据。"""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'tests/compile/contracts'))
import fixture


class FixtureGuards(unittest.TestCase):
    def test_all_cases_have_distinct_positive_and_negative_targets(self):
        targets = fixture.targets()
        self.assertEqual(len({x['case'] for x in targets.values()}), 10)
        for case in {x['case'] for x in targets.values()}:
            rows = [x for x in targets.values() if x['case'] == case]
            self.assertEqual(sum(x['positive'] for x in rows), 1)
            self.assertGreaterEqual(sum(not x['positive'] for x in rows), 1)

    def test_internal_target_names_fit_nested_windows_fixture_paths(self):
        self.assertLessEqual(max(map(len,fixture.targets())),12)

    def test_native_failure_is_checked_even_with_python_optimization(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)/'stdout.log'
            err = Path(directory)/'stderr.log'
            code = "import sys;sys.path.insert(0,sys.argv[1]);import verify_children as v;v.check_native({'status':'Exited','exit_code':3,'process_tree':{'active_after':0,'assigned_before_resume':True,'terminated_owned_job':False}})"
            row = fixture.execute([sys.executable,'-O','-c',code,str(ROOT/'tests/compile/contracts')],ROOT,out,err,30)
            self.assertEqual(row['status'],'Exited')
            self.assertNotEqual(row['exit_code'],0)
            self.assertEqual(row['process_tree']['active_after'],0)
            self.assertIn(b'ValueError: native control failed',err.read_bytes())

    def test_identity_changes_in_every_dimension(self):
        base = {'source':'a','compiler':'b','toolset':'c','sdk':'d','platform':'x64',
                'config':'Debug','crt':'MDd','asan':False,'options':['/EHsc'],
                'lock':'e','support':'f','run_id':'one'}
        for key in base:
            changed = dict(base, **{key: 'changed'})
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'identity'):
                fixture.check_identity(base, changed)

    def test_missing_or_failed_ready_cannot_reuse_old_build(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path/'build').mkdir()
            (path/'build'/'old.obj').write_bytes(b'old')
            with self.assertRaisesRegex(ValueError, 'ready'):
                fixture.read_ready(path)
            (path/'ready.json').write_text('{"status":"Failed"}')
            with self.assertRaisesRegex(ValueError, 'ready'):
                fixture.read_ready(path)

    def test_tool_failure_is_not_contract_rejection(self):
        row = {'status':'Exited','exit_code':1,'process_tree':{'active_after':0,'assigned_before_resume':True,'terminated_owned_job':False}}
        for raw in (b'x.cpp\n fatal error C1083: cannot open file', b'error MSB8020: missing tools',
                    b'x.cpp\nerror C2672: rejected\nfatal error C1083: missing'):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                fixture.check_compile(row, raw, 'x', positive=False)
        fixture.check_compile(row, b'x.cpp\nerror C2672: constraints not satisfied', 'x', positive=False)

    def test_other_target_or_cached_positive_not_accepted(self):
        row = {'status':'Exited','exit_code':0,'process_tree':{'active_after':0,'assigned_before_resume':True,'terminated_owned_job':False}}
        for raw in (b'', b'other.cpp\n'):
            with self.assertRaises(ValueError):
                fixture.check_compile(row, raw, 'x', positive=True)
        fixture.check_compile(row, b'x.cpp\n', 'x', positive=True)
        row['exit_code'] = 1
        with self.assertRaises(ValueError):
            fixture.check_compile(row, b'x.cpp\nerror C2672: rejected', 'x', positive=True)

    def test_cleanup_kill_is_never_a_pass(self):
        row = {'status':'Exited','exit_code':0,'process_tree':{'active_after':0,'assigned_before_resume':True,'terminated_owned_job':True}}
        self.assertFalse(fixture.good(row))
        with self.assertRaises(ValueError):
            fixture.check_compile(row,b'x.cpp\n','x',positive=True)

    def test_two_negative_results_must_both_be_present(self):
        expected = ['positive', 'negative_a', 'negative_b']
        for observed in (['positive','negative_a'], ['positive','negative_a','negative_a'],
                         ['positive','negative_a','negative_b','unknown']):
            with self.assertRaises(ValueError):
                fixture.check_execution(expected, observed)
        fixture.check_execution(expected, expected)

    def test_discovery_cannot_drop_or_invent_a_wrapper(self):
        names = [('T05' if x == 'typed_binding_fingerprint' else 'T02')+'.contracts.'+x
                 for x in sorted({item['case'] for item in fixture.targets().values()})]
        fixture.check_discovered(names)
        for changed in (names[:-1],names+[names[0]],names+['T02.contracts.unknown']):
            with self.assertRaises(ValueError):
                fixture.check_discovered(changed)

    def test_ready_rechecks_live_input_and_generated_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            a = SimpleNamespace(fixture=directory,run_id='0123456789abcdef')
            identity = {'run_id':a.run_id,'source':'original'}
            configure = {'status':'Exited','exit_code':0,'process_tree':{'active_after':0,'assigned_before_resume':True,'terminated_owned_job':False}}
            fixture.save_json(Path(directory)/'ready.json', {'status':'Ready','identity':identity,
                              'identity_sha256':fixture.digest(identity),'generated':['project-a'],'configure':configure})
            with patch.object(fixture,'input_identity',return_value=identity), patch.object(fixture,'generated_identity',return_value=['project-a']):
                fixture.require_ready(a)
            with patch.object(fixture,'input_identity',return_value=dict(identity,source='changed')):
                with self.assertRaisesRegex(ValueError,'identity'): fixture.require_ready(a)
            with patch.object(fixture,'input_identity',return_value=identity), patch.object(fixture,'generated_identity',return_value=[]):
                with self.assertRaisesRegex(ValueError,'identity'): fixture.require_ready(a)

    def test_prepare_never_overwrites_an_existing_run(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(FileExistsError):
                fixture.prepare(SimpleNamespace(fixture=directory),lambda *args: self.fail('must not launch'))

    def test_measurement_does_not_reuse_ctest_runtime_arguments(self):
        import measure
        with tempfile.TemporaryDirectory() as directory:
            discovery = Path(directory)/'tests.cmake'
            discovery.write_text('add_test([==[T02.contracts.read_shape]==] [==[python]==] [==[--binary]==] [==[real.exe]==] [==[--fixture]==] "${_fixture}" [==[--run-id]==] "${_run}")\n')
            self.assertEqual(measure.wrapper_arguments(discovery,'T02.contracts.read_shape'),['python','--binary','real.exe'])


if __name__ == '__main__':
    unittest.main()
