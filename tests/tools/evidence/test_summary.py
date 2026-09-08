"""P0 摘要导航反例；合成数据只作单元夹具，最后一项运行真实采集器。"""
import copy
import importlib
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.evidence.common import digest, read_json, save_json, sha_file


class SummaryTests(unittest.TestCase):
    def setUp(self):
        self.api = importlib.import_module('tools.evidence.summary')
        self.tmp = tempfile.TemporaryDirectory(prefix='ock-summary-')
        self.addCleanup(self.tmp.cleanup)
        self.folder = Path(self.tmp.name)
        save_json(self.folder/'manifest.json', {'profile': 'fixture', 'repeat': 2})
        save_json(self.folder/'expected.json', {'cases': [{'id': 'T23.one'}, {'id': 'T23.two'}]})
        save_json(self.folder/'discovered.json', {'tests': [{'name': 'T23.one'}, {'name': 'T23.two'}]})
        (self.folder/'raw.log').write_bytes(b'unit fixture raw output')
        (self.folder/'source.zip').write_bytes(b'opaque unit archive: summary must not unzip')
        self.r = {'evidence_format': 'ock.evidence/1', 'task_id': 'D1.06', 'run_id': 'unit-only',
          'manifest': {'snapshot': 'manifest.json'}, 'expected_snapshot': 'expected.json',
          'source': {'archive': 'source.zip', 'archive_sha256': sha_file(self.folder/'source.zip')},
          'automated_status': 'Failed', 'package_status': 'Failed', 'errors': ['fixture failure'],
          'tests': {'expected': ['T23.one','T23.two'], 'discovered': ['T23.one','T23.two'], 'rounds': []},
          'commands': [{'id':'test-001', 'role':'test', 'argv':['fixture'], 'status':'Exited', 'exit_code':1,
            'started_at':'2026-09-08T01:00:00+00:00', 'finished_at':'2026-09-08T01:00:02.5+00:00',
            'raw':[self.ref('raw.log')]}]}
        self.junit(1, '<testcase name="T23.one"/><testcase name="T23.two"><failure/></testcase>')
        self.junit(2, '<testcase name="T23.one"><skipped/></testcase><testcase name="T23.extra"/>')
        self.report = self.folder/'report.json'
        self.save()

    def ref(self, name):
        p = self.folder/name
        return {'path':name, 'sha256':sha_file(p), 'size':p.stat().st_size}

    def junit(self, index, xml):
        name = f'round-{index:03}-junit.xml'
        (self.folder/name).write_text('<testsuite tests="2">'+xml+'</testsuite>', encoding='utf-8')
        self.r['tests']['rounds'].append({'round':index,'junit':self.ref(name)})

    def save(self):
        save_json(self.report, self.r)

    def test_extract_rounds_and_preserve_failure(self):
        path = self.api.write_summary(self.report)
        s = self.api.read_summary(path)
        self.assertEqual(s['report_claims']['automated_status'], 'Failed')
        self.assertNotIn('automated_status', s)
        self.assertEqual(s['counts'], {'expected':2,'discovered':2,'executed':4,'rounds':2})
        self.assertEqual(s['rounds'][0]['failed'], ['T23.two'])
        self.assertEqual(s['rounds'][1]['skipped'], ['T23.one'])
        self.assertEqual(s['rounds'][1]['missing'], ['T23.two'])
        self.assertEqual(s['rounds'][1]['unexpected'], ['T23.extra'])
        self.assertEqual(s['commands'][0]['duration_seconds'], 2.5)
        self.assertTrue(s['failure_signals'])

    def test_missing_optional_fields_are_null(self):
        del self.r['tests']
        del self.r['commands']
        self.save()
        (self.folder/'discovered.json').unlink()
        s = self.api.build_summary(self.report)
        self.assertIsNone(s['counts']['discovered'])
        self.assertIsNone(s['counts']['executed'])
        self.assertIsNone(s['counts']['rounds'])
        self.assertIsNone(s['commands'])
        self.assertIsNone(s['duration_seconds'])
        self.assertEqual(s['counts']['expected'], 2)

    def test_corrupt_summary_even_with_recomputed_digest(self):
        path = self.api.write_summary(self.report)
        original = read_json(path)
        for rehash in (False, True):
            s = copy.deepcopy(original)
            s['counts']['executed'] = 999
            if rehash:
                s['content_sha256'] = digest({k:v for k,v in s.items() if k != 'content_sha256'})
            save_json(path, s)
            with self.assertRaises(ValueError): self.api.read_summary(path)

    def test_changed_report_is_stale(self):
        path = self.api.write_summary(self.report)
        self.r['errors'].append('later revision'); self.save()
        with self.assertRaises(ValueError): self.api.read_summary(path)

    def test_changed_referenced_sources_rejected(self):
        path = self.api.write_summary(self.report)
        for name in ('manifest.json','expected.json','discovered.json','raw.log','round-001-junit.xml','source.zip'):
            with self.subTest(name=name):
                p=self.folder/name; original=p.read_bytes(); p.write_bytes(original+b' ')
                with self.assertRaises(ValueError): self.api.read_summary(path)
                p.write_bytes(original)
        self.r['commands'][0]['raw'][0]['sha256'] = '0'*64; self.save()
        with self.assertRaises(ValueError): self.api.build_summary(self.report)

    def test_missing_required_source_rejected(self):
        for name in ('manifest.json','expected.json','raw.log','round-001-junit.xml'):
            with self.subTest(name=name):
                p=self.folder/name; original=p.read_bytes(); p.unlink()
                with self.assertRaises(ValueError): self.api.write_summary(self.report)
                p.write_bytes(original)
        del self.r['expected_snapshot']; self.save()
        with self.assertRaises(ValueError): self.api.build_summary(self.report)

    def test_reference_path_escape_rejected(self):
        self.r['commands'][0]['raw'][0]['path'] = '../outside.log'; self.save()
        with self.assertRaises(ValueError): self.api.build_summary(self.report)

    def test_corrupt_junit_has_explicit_rejection(self):
        p = self.folder/'round-001-junit.xml'; p.write_text('broken', encoding='utf-8')
        self.r['tests']['rounds'][0]['junit'] = self.ref(p.name); self.save()
        with self.assertRaises(ValueError): self.api.write_summary(self.report)

    def test_missing_command_raw_is_not_complete_navigation(self):
        del self.r['commands'][0]['raw']; self.save()
        with self.assertRaises(ValueError): self.api.build_summary(self.report)

    def test_passed_claim_without_required_rounds_rejected(self):
        self.r['automated_status'] = 'Passed'; self.r['errors'] = []
        self.r['commands'][0]['exit_code'] = 0
        self.r['tests']['rounds'] = []; self.save()
        with self.assertRaises(ValueError): self.api.build_summary(self.report)

    def test_set_digests_and_top_level_command_costs(self):
        install = copy.deepcopy(self.r['commands'][0])
        install.update(id='install', role='check', argv=['cmake','--install','build'])
        self.r['commands'].append(install); self.save()
        s = self.api.build_summary(self.report)
        self.assertEqual(s['set_digests']['expected'], digest(['T23.one','T23.two']))
        self.assertEqual(s['set_digests']['discovered'], digest(['T23.one','T23.two']))
        self.assertEqual(s['set_digests']['executed'], digest(['T23.extra','T23.one','T23.two']))
        cost = s['command_costs']
        self.assertEqual(cost['scope'], 'top-level-only')
        self.assertEqual(cost['total'], 2)
        self.assertEqual(cost['install'], {'count':1,'duration_seconds':2.5})
        self.assertEqual(cost['configure'], {'count':0,'duration_seconds':0})
        self.assertIsNone(cost['nested_command_count'])
        del self.r['commands']; self.save()
        self.assertIsNone(self.api.build_summary(self.report)['command_costs']['total'])

    def test_write_oserror_is_explicit_rejection(self):
        from unittest.mock import patch
        with patch.object(self.api, 'save_json', side_effect=OSError('disk write fault')):
            with self.assertRaises(ValueError): self.api.write_summary(self.report)

    def test_review_archive_and_declared_snapshot_digests(self):
        (self.folder/'review-records.zip').write_bytes(b'opaque review fixture')
        self.r['review'] = {'records': [], 'archive': {'path':'review-records.zip',
                             'sha256':sha_file(self.folder/'review-records.zip')}}
        self.r['manifest']['sha256'] = sha_file(self.folder/'manifest.json')
        self.r['requirements_manifest_sha256'] = sha_file(self.folder/'expected.json')
        self.save()
        s = self.api.build_summary(self.report)
        self.assertEqual(len([r for r in s['references'] if r['role']=='review-archive']), 1)
        for field in ('manifest', 'expected'):
            with self.subTest(field=field):
                r = copy.deepcopy(self.r)
                if field == 'manifest': r['manifest']['sha256']='0'*64
                else: r['requirements_manifest_sha256']='0'*64
                save_json(self.report, r)
                with self.assertRaises(ValueError): self.api.build_summary(self.report)

    def test_original_snapshot_zip_guards(self):
        import json
        import warnings
        import zipfile
        from tools.evidence.common import sha_bytes
        original = json.dumps(read_json(self.folder/'manifest.json')).encode('utf-8')
        self.r['manifest'].update(path='run.json', sha256=sha_bytes(original))
        archive = self.folder/'source.zip'
        def write(members):
            with warnings.catch_warnings():
                warnings.simplefilter('ignore', UserWarning)
                with zipfile.ZipFile(archive, 'w') as z:
                    for name, data in members: z.writestr(name, data)
            self.r['source']['archive_sha256'] = sha_file(archive); self.save()
        write([('run.json',original)])
        self.api.build_summary(self.report)  # 合法 JSON 重排，不能直接比较快照 raw SHA。
        for members in ([], [('run.json',original),('run.json',original)], [('run.json',original),('../escape',b'x')]):
            with self.subTest(members=[n for n,_ in members]):
                write(members)
                with self.assertRaises(ValueError): self.api.build_summary(self.report)
        archive.write_bytes(b'not a zip')
        self.r['source']['archive_sha256'] = sha_file(archive); self.save()
        with self.assertRaises(ValueError): self.api.build_summary(self.report)

    def test_real_runner_writes_summary(self):
        sys.path.insert(0, str(Path(__file__).parent))
        from test_runner import EvidenceTests
        fixture = EvidenceTests('test_T23_evidence_actual_execution')
        fixture.setUp()
        path, report = fixture.collect()
        self.assertEqual(report['automated_status'], 'Passed', report['errors'])
        summary = self.api.read_summary(path.parent/'summary.json')
        self.assertEqual(summary['counts']['executed'], 1)
        self.assertEqual(summary['report_claims']['automated_status'], report['automated_status'])
        self.assertFalse(summary['failure_signals'])
        # 摘要故障独立记录，原正式报告不被它重写；CLI 单独以非零拒绝。
        from unittest.mock import patch
        from tools.evidence import run as runner
        with patch.object(self.api, 'write_summary', side_effect=ValueError('injected summary failure')):
            failed_path, unchanged = fixture.collect()
        self.assertEqual(unchanged['automated_status'], 'Passed')
        error = read_json(failed_path.parent/'summary-error.json')
        self.assertEqual(error['report_sha256'], sha_file(failed_path))
        with patch.object(runner, 'run', return_value=failed_path), patch.object(sys, 'argv', ['run.py', 'unused.json']):
            self.assertTrue(runner.main())


if __name__ == '__main__':
    unittest.main()
